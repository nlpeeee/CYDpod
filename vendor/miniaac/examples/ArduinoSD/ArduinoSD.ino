/* Decode-only SD benchmark: replace consume_pcm() with your output queue.
 * No DAC/Bluetooth dependency. Default SD SPI pins match classic CYD.
 * Never decode or read SD inside a Bluetooth audio callback; feed a queue
 * from your application task and let that callback drain it.
 */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <miniaac.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

static constexpr int SD_SCK=18, SD_MISO=19, SD_MOSI=23, SD_CS=5;
static constexpr const char *TRACK="/track.m4a";
alignas(8) static uint8_t workspace[MINIAAC_WORKSPACE_BYTES];
static int16_t pcm[MINIAAC_PCM_SAMPLES];
static File track;
static miniaac *decoder=nullptr;
static miniaac_info info;
static size_t pending=0, consumed=0;
static uint64_t frames=0, started=0, worst_decode_us=0;
static volatile uint32_t checksum=0;
static unsigned blocks=0;

static int read_at(void *user,uint64_t offset,void *dst,size_t bytes) {
    File *file=static_cast<File *>(user);
    // Arduino FS::File uses 32-bit positions. Custom readers may support 64-bit.
    if(offset>UINT32_MAX || !file->seek(static_cast<uint32_t>(offset),SeekSet)) return -1;
    return file->read(static_cast<uint8_t *>(dst),bytes)==bytes ? 0 : -1;
}
static size_t consume_pcm(const int16_t *samples,size_t count) {
    // Demo sink consumes/discards everything. Real sink: return the number of
    // int16_t values accepted (a multiple of info.channels), or zero when full.
    for(size_t i=0;i<count;i++) checksum=checksum*33u+static_cast<uint16_t>(samples[i]);
    return count;
}
static void finish(miniaac_status rc) {
    double elapsed=(esp_timer_get_time()-started)/1e6;
    Serial.printf("%s; frames=%llu, audio=%.3fs, wall=%.3fs, max decode/read=%lluus\n",
                  miniaac_status_string(rc),(unsigned long long)frames,
                  (double)frames/info.sample_rate,elapsed,(unsigned long long)worst_decode_us);
    Serial.printf("stack high water=%u bytes; free internal heap=%u\n",
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    miniaac_close(decoder); decoder=nullptr; track.close();
}
void setup() {
    Serial.begin(115200);
    SPI.begin(SD_SCK,SD_MISO,SD_MOSI,SD_CS);
    if(!SD.begin(SD_CS,SPI,20000000)) { Serial.println("SD mount failed"); return; }
    track=SD.open(TRACK,FILE_READ);
    if(!track) { Serial.println("Put /track.m4a on the SD card"); return; }
    miniaac_source source={read_at,&track,track.size()};
    started=esp_timer_get_time();
    miniaac_status rc=miniaac_open(workspace,sizeof(workspace),&source,&decoder);
    if(rc!=MINIAAC_OK) { Serial.println(miniaac_status_string(rc)); track.close(); return; }
    miniaac_get_info(decoder,&info);
    Serial.printf("%u Hz, %u channels; required workspace=%u, allocated=%u, PCM=%u bytes\n",
                  (unsigned)info.sample_rate,(unsigned)info.channels,
                  (unsigned)miniaac_workspace_size(),(unsigned)sizeof(workspace),(unsigned)sizeof(pcm));
}
void loop() {
    if(!decoder) { delay(100); return; }
    if(consumed<pending) {
        size_t accepted=consume_pcm(pcm+consumed,pending-consumed);
        if(accepted>pending-consumed || accepted%info.channels) { finish(MINIAAC_ARGUMENT); return; }
        consumed+=accepted;
        if(consumed<pending) { delay(1); return; }
    }
    uint64_t start=esp_timer_get_time();
    miniaac_status rc=miniaac_decode_next(decoder,pcm,MINIAAC_PCM_SAMPLES,&pending);
    uint64_t took=esp_timer_get_time()-start;
    if(took>worst_decode_us) worst_decode_us=took;
    if(rc!=MINIAAC_OK) { finish(rc); return; }
    consumed=0; frames+=pending/info.channels;
    if(!(++blocks%16)) delay(1); // let other tasks run during the discard benchmark
}
