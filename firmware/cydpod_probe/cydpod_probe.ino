// Minimal ORIGINAL ESP32 probe. No UI, Wi-Fi, PSRAM, codec or MP4 implementation.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <BluetoothA2DPSource.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <AACDecoderHelix.h> // Arduino dependency discovery; raw C API below does the decoding.
#include "adts_decoder.h"
#include "m4a_guarded_reader.h" // lib/cydpod_m4a, include path supplied by tools/check.ps1
#if !defined(CONFIG_IDF_TARGET_ESP32) || !defined(CYDPOD_AAC_LC_ONLY)
#error This probe requires original ESP32 and global CYDPOD_AAC_LC_ONLY
#endif

using namespace cydpod;
enum class Output : uint8_t { bluetooth, line, headphone };
enum class Op : uint8_t { play, stop, output, pair };
struct Command { Op op; Output output; uint32_t epoch; char text[128]; };
PcmRing<2048> pcmRing; // exactly 8192 bytes of PCM
AdtsDecoder decoder;
SPIClass sdSpi(VSPI);
BluetoothA2DPSource a2dp;
Preferences prefs;
QueueHandle_t commands;
TaskHandle_t producerHandle=nullptr, sinkHandle=nullptr;
i2s_chan_handle_t i2sTx=nullptr;
std::atomic<uint32_t> epoch{0}, btReaders{0}, sinkReaders{0};
std::atomic<uint32_t> underrunFrames{0}, underrunCalls{0}, decodedFrames{0}, queueRejected{0};
std::atomic<bool> btConsume{false}, i2sConsume{false}, ready{false}, muted{true}, connected{false};
std::atomic<unsigned> targetLevel{5};
std::atomic<uint32_t> connectionChanged{0};
bool operational=false;
Output selected=Output::bluetooth; // main task owns saved selection
unsigned levels[3]={10,10,5};
char peer[64]=""; // producer owns this while Bluetooth is active
char track[128]=""; // producer only; changing output restarts this path
bool a2dpStarted=false;
uint32_t pairStarted=0;
std::atomic<uint32_t> minLargest{UINT32_MAX};
const char* m4aState="na"; // producer writes, serial format only; "na" for .aac tracks
void sampleLargest() {
  uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  uint32_t old=minLargest.load();
  while(largest<old && !minLargest.compare_exchange_weak(old,largest)) {}
}

bool endsWithNoCase(const char* s,const char* suffix) {
  size_t a=strlen(s), b=strlen(suffix);
  return a>=b && !strcasecmp(s+a-b,suffix);
}
// One stable line per track event: TRACK ev=start|end|skip name=<path> m4a=<status|na> heap=<free> largest=<block> min_heap=<since boot> min_sampled_largest=<min>
void logTrack(const char* ev) {
  sampleLargest();
  Serial.printf("TRACK ev=%s name=%s m4a=%s heap=%u largest=%u min_heap=%u min_sampled_largest=%u\n",ev,track,m4aState,
    heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
    heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),minLargest.load());
}
// Pull adapter: file bytes -> guarded M4A reader -> ADTS frames queued here -> AdtsDecoder reads them like an .aac file.
struct M4aFeed {
  cydpod::M4aGuardedReader reader;
  uint8_t adts[4096]; size_t head=0, tail=0;
  uint8_t chunk[256];
  bool done=false, overflow=false;
  void begin() {
    reader.begin([this](const uint8_t* d,size_t n) {
      if(head==tail) head=tail=0;
      if(tail+n>sizeof(adts)) { overflow=true; return; }
      memcpy(adts+tail,d,n); tail+=n;
    });
  }
  size_t read(File& file,uint8_t* out,size_t n) {
    while(head==tail && !done && reader.status()==cydpod::M4aStatus::ok && !overflow) {
      size_t got=file.read(chunk,sizeof(chunk));
      if(got==0) { reader.finish(); done=true; }
      else reader.write(chunk,got);
    }
    size_t take=std::min(n,tail-head);
    memcpy(out,adts+head,take); head+=take;
    return take;
  }
  // ok only if the whole file demuxed cleanly and no frame was dropped.
  const char* state() const { return overflow ? "adts_queue_overflow" : cydpod::m4aStatusName(reader.status()); }
  bool failed() const { return overflow || reader.status()!=cydpod::M4aStatus::ok; }
};

// Only the active consumer calls this. Gain ramps after unmute; mute is immediate.
void render(Stereo* data, size_t count, GainRamp& gain) {
  size_t got=pcmRing.pop(data,count);
  if(got<count) {
    memset(data+got,0,(count-got)*sizeof(Stereo));
    underrunFrames.fetch_add(count-got); underrunCalls.fetch_add(1);
  }
  gain.apply(data,count,targetLevel.load(),muted.load());
}
int32_t btData(uint8_t* data,int32_t length) {
  if(length<=0) return 0;
  memset(data,0,length);
  btReaders.fetch_add(1);
  static GainRamp gain;
  if(btConsume.load() && ready.load()) {
    // Byte callback need not supply aligned memory; use bounded local frames.
    Stereo block[64]; size_t offset=0, frames=length/sizeof(Stereo);
    while(frames) { size_t n=std::min(frames,size_t(64)); render(block,n,gain); memcpy(data+offset,block,n*sizeof(Stereo)); offset+=n*sizeof(Stereo); frames-=n; }
  } else gain.reset();
  btReaders.fetch_sub(1);
  return length; // always complete, zero padded; no SD, decoder, waits or logging
}
void btConnection(esp_a2d_connection_state_t state,void*) {
  connected.store(state==ESP_A2D_CONNECTION_STATE_CONNECTED);
  connectionChanged.store(millis());
  muted.store(true); // deliberate unmute required after every connect/disconnect
}
void sinkTask(void*) {
  Stereo block[128]; GainRamp gain;
  for(;;) {
    sinkReaders.fetch_add(1);
    if(i2sConsume.load() && ready.load()) {
      render(block,128,gain);
      size_t written=0;
      esp_err_t error=i2s_channel_write(i2sTx,block,sizeof(block),&written,20);
      if(error!=ESP_OK || written!=sizeof(block)) { underrunCalls.fetch_add(1); muted.store(true); }
    } else { gain.reset(); }
    sinkReaders.fetch_sub(1);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
void stopConsumers() {
  muted.store(true); ready.store(false); btConsume.store(false); i2sConsume.store(false);
  while(btReaders.load() || sinkReaders.load()) vTaskDelay(pdMS_TO_TICKS(1));
  pcmRing.clear(); // endpoints quiesced; no concurrent reset
}
void closeOutput() {
  stopConsumers();
  if(a2dpStarted) { a2dp.cancel_discovery(); a2dp.end(false); a2dpStarted=false; connected.store(false); }
  if(i2sTx) { i2s_channel_disable(i2sTx); i2s_del_channel(i2sTx); i2sTx=nullptr; }
}
bool openOutput(Output output) {
  muted.store(true);
  if(output==Output::bluetooth) {
    if(!peer[0]) { Serial.println("ERR pair NAME first"); return false; }
    a2dp.set_auto_reconnect(true,3); a2dp.set_local_name("CYDpod AAC probe");
    a2dp.set_data_callback(btData); a2dp.set_on_connection_state_changed(btConnection);
    a2dp.set_ssp_enabled(true); a2dp.start(peer); a2dp.set_volume(127);
    a2dpStarted=true; pairStarted=millis(); connectionChanged.store(pairStarted); btConsume.store(true); return true;
  }
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
  channel.dma_desc_num=4; channel.dma_frame_num=128;
  if(i2s_new_channel(&channel,&i2sTx,nullptr)!=ESP_OK) { i2sTx=nullptr; return false; }
  i2s_std_config_t config={};
  config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(44100);
  config.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk=I2S_GPIO_UNUSED; config.gpio_cfg.bclk=GPIO_NUM_22;
  config.gpio_cfg.ws=GPIO_NUM_27; config.gpio_cfg.dout=GPIO_NUM_17; config.gpio_cfg.din=I2S_GPIO_UNUSED;
  if(i2s_channel_init_std_mode(i2sTx,&config)!=ESP_OK || i2s_channel_enable(i2sTx)!=ESP_OK) {
    i2s_del_channel(i2sTx); i2sTx=nullptr; return false;
  }
  i2sConsume.store(true); return true;
}
void producerTask(void*) {
  Output output=Output::bluetooth; Command command{};
  for(;;) {
    if(xQueueReceive(commands,&command,pdMS_TO_TICKS(100))!=pdTRUE) {
      if(a2dpStarted && !connected.load() && millis()-connectionChanged.load()>30000) {
        Serial.println("ERR Bluetooth timeout; pair again"); closeOutput();
      }
      continue;
    }
    stopConsumers(); decoder.end();
    if(command.op==Op::stop) { track[0]=0; continue; }
    if(command.op==Op::output || command.op==Op::pair) {
      closeOutput(); output=command.output;
      if(command.op==Op::pair) strlcpy(peer,command.text,sizeof(peer));
    }
    if(command.op==Op::play) strlcpy(track,command.text,sizeof(track));
    if(!a2dpStarted && !i2sTx && !openOutput(output)) { Serial.println("ERR output initialization"); continue; }
    Serial.printf("OUTPUT ready mode=%u muted=1; switching restarts track\n",unsigned(output));
    if(!track[0]) continue;
    File file=SD.open(track,FILE_READ);
    if(!file || file.isDirectory()) { Serial.println("ERR track open"); continue; }
    M4aFeed* feed=nullptr; m4aState="na";
    if(endsWithNoCase(track,".m4a")) {
      feed=new(std::nothrow) M4aFeed;
      if(!feed) { m4aState="alloc"; logTrack("skip"); Serial.println("ERR M4A allocation"); file.close(); continue; }
      feed->begin(); m4aState="ok";
    }
    if(!decoder.begin()) { Serial.println("ERR AAC allocation"); file.close(); delete feed; continue; }
    sampleLargest(); logTrack("start");
    btConsume.store(output==Output::bluetooth); i2sConsume.store(output!=Output::bluetooth);
    DecodeResult result=DecodeResult::frame;
    while(epoch.load()==command.epoch) {
      result=decoder.next([&file,feed](uint8_t* p,size_t n)->size_t { return feed?feed->read(file,p,n):file.read(p,n); });
      if(feed) m4aState=feed->state();
      if(result!=DecodeResult::frame) break;
      decodedFrames.fetch_add(1);
      if((decodedFrames.load()&15)==0) sampleLargest();
      size_t sent=0;
      while(sent<1024 && epoch.load()==command.epoch) {
        sent+=pcmRing.push(decoder.pcm()+sent,1024-sent);
        if(pcmRing.size()>=1024) ready.store(true);
        // A disconnected A2DP source cannot drain; wait, bounded by pair timeout.
        if(a2dpStarted && !connected.load() && millis()-connectionChanged.load()>30000) {
          Serial.println("ERR Bluetooth connect/reconnect timeout; pair again"); epoch.fetch_add(1); break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    file.close(); decoder.end();
    bool m4aFailed=feed && feed->failed();
    delete feed; feed=nullptr;
    if(m4aFailed && epoch.load()==command.epoch) {
      // Demux error: drop queued PCM, skip this track, keep serving the next command.
      pcmRing.clear(); logTrack("skip"); Serial.printf("ERR m4a %s\n",m4aState);
    } else if(epoch.load()==command.epoch && result==DecodeResult::eof) {
      ready.store(true);
      uint32_t until=millis()+2000;
      while(pcmRing.size() && epoch.load()==command.epoch && int32_t(until-millis())>0) vTaskDelay(pdMS_TO_TICKS(1));
      logTrack("end"); Serial.println("EOF");
    } else if(epoch.load()==command.epoch) Serial.printf("ERR %s helix=%d\n",resultName(result),decoder.lastError);
    stopConsumers();
  }
}
void metrics() {
  uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  sampleLargest();
  Serial.printf("METRIC m4a=%s heap=%u min_heap=%u largest=%u min_sampled_largest=%u producer_stack=%u sink_stack=%u loop_stack=%u ring=%u decoded=%u underrun_frames=%u underrun_calls=%u queue_rejected=%u connected=%u muted=%u level=%u\n",
    heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),largest,minLargest.load(),
    producerHandle?uxTaskGetStackHighWaterMark(producerHandle):0,sinkHandle?uxTaskGetStackHighWaterMark(sinkHandle):0,uxTaskGetStackHighWaterMark(nullptr),
    unsigned(pcmRing.size()),decodedFrames.load(),underrunFrames.load(),underrunCalls.load(),queueRejected.load(),connected.load(),muted.load(),targetLevel.load());
}
bool submit(Op op,const char* text=nullptr,Output output=Output::bluetooth) {
  Command c{}; c.op=op; c.output=output;
  if(text) strlcpy(c.text,text,sizeof(c.text));
  // Increment only once a slot is available; loop is the sole queue writer.
  if(!uxQueueSpacesAvailable(commands)) { queueRejected.fetch_add(1); Serial.println("ERR command queue full"); return false; }
  c.epoch=epoch.fetch_add(1)+1;
  if(xQueueSend(commands,&c,0)!=pdTRUE) { queueRejected.fetch_add(1); return false; }
  muted.store(true);
  return true;
}
void serialCommand(const char* s) {
  if(!strcmp(s,"stats")) { metrics(); return; }
  if(!strcmp(s,"mute")) { muted.store(true); return; }
  if(!strcmp(s,"unmute")) { muted.store(false); return; }
  if(!strcmp(s,"stop")) { submit(Op::stop); return; }
  if(!strncmp(s,"play /",6)) { submit(Op::play,s+5); return; }
  if(!strncmp(s,"pair ",5) && strlen(s+5)>0 && strlen(s+5)<sizeof(peer)) {
    if(submit(Op::pair,s+5,Output::bluetooth)) {
      selected=Output::bluetooth; targetLevel.store(levels[0]); prefs.putString("peer",s+5);
    }
    return;
  }
  if(!strncmp(s,"output ",7)) {
    Output o;
    if(!strcmp(s+7,"bt")) o=Output::bluetooth;
    else if(!strcmp(s+7,"line")) o=Output::line;
    else if(!strcmp(s+7,"hp")) o=Output::headphone;
    else { Serial.println("ERR output bt|line|hp"); return; }
    if(submit(Op::output,nullptr,o)) { selected=o; targetLevel.store(levels[unsigned(o)]); }
    return;
  }
  if(!strncmp(s,"volume ",7)) {
    char* end=nullptr; long value=strtol(s+7,&end,10);
    if(end==s+7 || *end || value<0 || value>25) { Serial.println("ERR volume 0..25 percent"); return; }
    levels[unsigned(selected)]=unsigned(value); targetLevel.store(unsigned(value));
    const char* keys[]={"bt","line","hp"}; prefs.putUChar(keys[unsigned(selected)],uint8_t(value)); return;
  }
  Serial.println("Commands: pair NAME | output bt|line|hp | play /path.aac|.m4a | stop | mute | unmute | volume 0..25 | stats");
}
void setup() {
  Serial.begin(115200); delay(300);
  Serial.printf("CYDpod AAC probe chip=%s psram=%u; provisional I2S 22/27/17; outputs unverified\n",ESP.getChipModel(),ESP.getPsramSize());
  prefs.begin("cydpod-probe",false);
  const char* keys[]={"bt","line","hp"};
  for(unsigned i=0;i<3;++i) levels[i]=safeSavedLevel(prefs.getUChar(keys[i],i==2?5:10),10);
  targetLevel.store(levels[0]); prefs.getString("peer",peer,sizeof(peer));
  sdSpi.begin(18,19,23,5);
  if(!SD.begin(5,sdSpi,10000000)) { Serial.println("FATAL SD mount; reset after checking FAT32 card"); return; }
  commands=xQueueCreate(4,sizeof(Command));
  if(!commands || xTaskCreatePinnedToCore(producerTask,"aac-producer",8192,nullptr,3,&producerHandle,1)!=pdPASS ||
     xTaskCreatePinnedToCore(sinkTask,"i2s-consumer",3072,nullptr,4,&sinkHandle,0)!=pdPASS) {
    Serial.println("FATAL task allocation; reset required"); return;
  }
  operational=true;
  serialCommand("help"); metrics();
}
void loop() {
  static LineBuffer<128> line;
  while(Serial.available()) { char c=Serial.read(); if(line.add(c) && operational) serialCommand(line.text()); }
  static uint32_t last=0;
  if(millis()-last>=5000) { last=millis(); metrics(); }
  delay(2);
}
