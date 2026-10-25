// Minimal ORIGINAL ESP32 probe. No UI, Wi-Fi or PSRAM. AAC-LC/M4A/ADTS decoding is MiniAAC (CY-31, vendor/miniaac) via miniaac_adapter.h.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ff.h> // FatFs directory attributes (hidden/system are not exposed by File).
#include <Preferences.h>
#include <BluetoothA2DPSource.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include "miniaac_adapter.h" // pulls in miniaac.h: the MinimalAAC library, supplied with --library by tools/check.ps1
#if !defined(CONFIG_IDF_TARGET_ESP32) || !defined(CYDPOD_AAC_LC_ONLY)
#error This probe requires original ESP32 and global CYDPOD_AAC_LC_ONLY
#endif

// Keep strict format diagnostics local to probe code, after dependency includes.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wformat"
#pragma GCC diagnostic error "-Wformat-security"
#endif

using namespace cydpod;
enum class Output : uint8_t { bluetooth, line, headphone };
enum class Op : uint8_t { play, stop, output, pair, ls };
struct Command { Op op; Output output; uint32_t epoch; char text[128]; };
PcmRing<2048> pcmRing; // exactly 8192 bytes of PCM
MiniAacTrack miniPlayer; // static: MiniAAC workspace 22,528 B + PCM 4,096 B, no heap
SPIClass sdSpi(VSPI);
BluetoothA2DPSource a2dp;
Preferences prefs;
QueueHandle_t commands;
TaskHandle_t producerHandle=nullptr, sinkHandle=nullptr;
i2s_chan_handle_t i2sTx=nullptr;
std::atomic<uint32_t> epoch{0}, btReaders{0}, sinkReaders{0};
std::atomic<bool> lsPending{false}; // includes a listing currently running on producer
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
std::atomic<const char*> m4aState{"na"}; // published static-lifetime status strings
// Admission (CY-31, docs/research/cy31-miniaac-integration.md). MiniAAC allocates nothing: workspace and PCM buffer are inside the static
// miniPlayer, so a track needs no heap beyond what the rest of the firmware (SD, Bluetooth, stacks) already holds. What is left is the playback
// margin, kept at 10,240 B (CY-13a's 2,048 B was provisional): free internal heap after the file is open must stay at or above it.
constexpr uint32_t kPerTrackHeapBytes=0;
constexpr uint32_t kPlaybackMarginBytes=10240;
constexpr uint32_t trackNeedFree() { return kPerTrackHeapBytes+kPlaybackMarginBytes; }
void sampleLargest() {
  uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  uint32_t old=minLargest.load();
  while(largest<old && !minLargest.compare_exchange_weak(old,largest)) {}
}

// One stable line per track event: TRACK ev=live|start|end|skip name=<path> m4a=<status|na> heap=<free> largest=<block> min_heap=<since boot> min_sampled_largest=<min> producer_stack=<bytes> source_reads=<n>
// m4a= carries the MiniAAC status name (ok, format, io, unsupported, ... or not_44100_stereo), for .aac tracks too. ev=live is logged once per track right after MiniAAC opened it (ev=start follows at the same state).
void logTrack(const char* ev) {
  sampleLargest();
  const char* state=m4aState.load();
  Serial.printf("TRACK ev=%s name=%s m4a=%s heap=%u largest=%u min_heap=%u min_sampled_largest=%u producer_stack=%u source_reads=%u\n",ev,track,state,
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
    unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(minLargest.load()),
    unsigned(producerHandle?uxTaskGetStackHighWaterMark(producerHandle):0),unsigned(miniPlayer.reads.load()));
}
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
// Producer only, idle only. SD is the probe's first and only FAT volume (0:).
// FatFs directory operations read metadata only; no files are opened for writing.
void listDirectory(const char* path) {
  char fatPath[132]; snprintf(fatPath,sizeof(fatPath),"0:%s",path);
  FF_DIR dir{}; FILINFO entry{};
  if(f_opendir(&dir,fatPath)!=FR_OK) { Serial.println("ERR ls open"); return; }
  unsigned count=0; bool truncated=false;
  for(;;) {
    FRESULT result=f_readdir(&dir,&entry);
    if(result!=FR_OK) { Serial.println("ERR ls read"); truncated=true; break; }
    if(!entry.fname[0]) break;
    if(entry.fname[0]=='.' || (entry.fattrib&(AM_HID|AM_SYS))) continue;
    if(count==64) { truncated=true; break; }
    if(strlen(entry.fname)>96) truncated=true;
    Serial.printf("LS name=%.96s size=%llu kind=%s\n",entry.fname,
      static_cast<unsigned long long>(entry.fsize),(entry.fattrib&AM_DIR)?"dir":"file");
    ++count;
  }
  f_closedir(&dir);
  Serial.printf("LS done count=%u truncated=%u\n",count,unsigned(truncated));
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
    if(command.op==Op::ls) { listDirectory(command.text); lsPending.store(false); continue; }
    stopConsumers(); miniPlayer.end();
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
    m4aState="na";
    // Admission (cy31-miniaac-integration.md): checked after the file is open (its buffers are already paid), before anything is decoded.
    const uint32_t heap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(heap<trackNeedFree()) {
      stopConsumers(); m4aState="low_memory"; logTrack("skip");
      Serial.printf("ERR m4a low_memory heap=%u need=%u\n",unsigned(heap),unsigned(trackNeedFree()));
      file.close(); continue;
    }
    if(miniPlayer.begin(file)!=MINIAAC_OK) {  // io/format/unsupported/..., or not_44100_stereo: skip the track, keep serving commands
      stopConsumers(); m4aState=miniPlayer.name(); logTrack("skip");
      Serial.printf("ERR m4a %s\n",miniPlayer.name());
      file.close(); continue;
    }
    m4aState="ok";
    sampleLargest(); logTrack("live"); logTrack("start");
    btConsume.store(output==Output::bluetooth); i2sConsume.store(output!=Output::bluetooth);
    MiniAacTrack::Pull result=MiniAacTrack::Pull::frames;
    while(epoch.load()==command.epoch) {
      if(!miniPlayer.pending()) {  // a partial ring push keeps the rest of the block for the next pass
        result=miniPlayer.next();
        if(result!=MiniAacTrack::Pull::frames) break;
        decodedFrames.fetch_add(1);
        if((decodedFrames.load()&15)==0) sampleLargest();
      }
      miniPlayer.consume(pcmRing.push(miniPlayer.pcm(),miniPlayer.pending()));
      if(pcmRing.size()>=1024) ready.store(true);
      // A disconnected A2DP source cannot drain; wait, bounded by pair timeout.
      if(a2dpStarted && !connected.load() && millis()-connectionChanged.load()>30000) {
        Serial.println("ERR Bluetooth connect/reconnect timeout; pair again"); epoch.fetch_add(1); break;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    miniPlayer.end();
    file.close(); // MiniAAC read through this handle until now
    if(result==MiniAacTrack::Pull::error && epoch.load()==command.epoch) {
      // Read/format/codec error: publish the status, drop queued PCM, skip this track, keep serving the next command.
      m4aState=miniPlayer.name(); stopConsumers(); logTrack("skip"); Serial.printf("ERR m4a %s\n",miniPlayer.name());
    } else if(epoch.load()==command.epoch && result==MiniAacTrack::Pull::eof) {
      ready.store(true);
      uint32_t until=millis()+2000;
      while(pcmRing.size() && epoch.load()==command.epoch && int32_t(until-millis())>0) vTaskDelay(pdMS_TO_TICKS(1));
      logTrack("end"); Serial.println("EOF");
    }
    stopConsumers();
  }
}
void metrics() {
  uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  sampleLargest();
  const char* state=m4aState.load();
  Serial.printf("METRIC m4a=%s heap=%u min_heap=%u largest=%u min_sampled_largest=%u producer_stack=%u sink_stack=%u loop_stack=%u ring=%u decoded=%u underrun_frames=%u underrun_calls=%u queue_rejected=%u connected=%u muted=%u level=%u source_reads=%u\n",
    state,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(largest),unsigned(minLargest.load()),
    unsigned(producerHandle?uxTaskGetStackHighWaterMark(producerHandle):0),unsigned(sinkHandle?uxTaskGetStackHighWaterMark(sinkHandle):0),unsigned(uxTaskGetStackHighWaterMark(nullptr)),
    unsigned(pcmRing.size()),unsigned(decodedFrames.load()),unsigned(underrunFrames.load()),unsigned(underrunCalls.load()),unsigned(queueRejected.load()),unsigned(connected.load()),unsigned(muted.load()),targetLevel.load(),unsigned(miniPlayer.reads.load()));
}
bool submit(Op op,const char* text=nullptr,Output output=Output::bluetooth) {
  Command c{}; c.op=op; c.output=output;
  if(text) strlcpy(c.text,text,sizeof(c.text));
  // loop is the sole writer. Bound listings to one and reserve a cancel slot.
  if(op==Op::ls) {
    if(lsPending.exchange(true)) { queueRejected.fetch_add(1); Serial.println("ERR ls busy"); return false; }
    if(uxQueueSpacesAvailable(commands)<=1) {
      lsPending.store(false); queueRejected.fetch_add(1); Serial.println("ERR ls busy"); return false;
    }
  } else if(!uxQueueSpacesAvailable(commands)) {
    // Keep cancellation available even after a burst of track changes.
    // A producer dequeue racing this only creates more free capacity.
    Command dropped{};
    if(xQueueReceive(commands,&dropped,0)==pdTRUE) {
      if(dropped.op==Op::ls) lsPending.store(false);
      queueRejected.fetch_add(1); Serial.println("ERR pending command superseded");
    }
  }
  // Listings wait for idle without cancelling or muting; SD stays producer-only.
  c.epoch=op==Op::ls ? epoch.load() : epoch.fetch_add(1)+1;
  if(xQueueSend(commands,&c,0)!=pdTRUE) {
    if(op==Op::ls) lsPending.store(false);
    queueRejected.fetch_add(1); return false;
  }
  if(op!=Op::ls) muted.store(true);
  return true;
}
void serialCommand(const char* s) {
  if(!strcmp(s,"stats")) { metrics(); return; }
  if(!strcmp(s,"mute")) { muted.store(true); return; }
  if(!strcmp(s,"unmute")) { muted.store(false); return; }
  if(!strcmp(s,"stop")) { submit(Op::stop); return; }
  if(!strcmp(s,"ls")) { submit(Op::ls,"/Music"); return; }
  if(!strncmp(s,"ls /",4)) { submit(Op::ls,s+3); return; }
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
  Serial.println("Commands: pair NAME | output bt|line|hp | play /path.aac|.m4a | ls [/dir] | stop | mute | unmute | volume 0..25 | stats");
}
void setup() {
  Serial.begin(115200); delay(300);
  Serial.printf("CYDpod AAC probe chip=%s psram=%u; provisional I2S 22/27/17; outputs unverified\n",ESP.getChipModel(),unsigned(ESP.getPsramSize()));
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

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
