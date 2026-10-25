/* Read-only SD hardware benchmark. Waits for 'G' on Serial before decoding.
 * Selects three representative corpus tracks, falling back to first AAC files.
 * No display, Bluetooth or DAC is initialized. PCM goes to a rolling hash.
 */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <miniaac.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

alignas(8) static uint8_t workspace[MINIAAC_WORKSPACE_BYTES];
static int16_t pcm[MINIAAC_PCM_SAMPLES];
static char targets[3][512],fallback[3][512];
static const char *wanted[3]={"06-01 - Madness - Baggy Trousers.m4a",
                             "07-20 - Daft Punk - Get Lucky.m4a",
                             "03-06 - New Order - Blue Monday.m4a"};
static unsigned fallback_count=0,job=0,files=0;
static bool started=false;
static volatile bool finished=false;
static constexpr uint32_t PRODUCER_STACK=4096;
static uint64_t read_us,read_calls,read_bytes;

static void json_string(const char *s) {
    Serial.write('"');
    while(*s) {
        if(*s=='"' || *s=='\\') Serial.write('\\');
        if((unsigned char)*s>=32) Serial.write(*s);
        s++;
    }
    Serial.write('"');
}
static void scan(const char *path,unsigned depth) {
    if(depth>6) return;
    File dir=SD.open(path); if(!dir || !dir.isDirectory()) return;
    File entry=dir.openNextFile();
    while(entry) {
        char child[512];
        int n=snprintf(child,sizeof(child),"%s%s%s",path,strcmp(path,"/")?"/":"",entry.name());
        bool directory=entry.isDirectory();
        if(n>0 && n<(int)sizeof(child)) {
            if(directory) { entry.close(); scan(child,depth+1); }
            else {
                String name=entry.name();String lower=name;lower.toLowerCase();
                if(lower.endsWith(".m4a") || lower.endsWith(".aac")) {
                    files++;
                    if(fallback_count<3) strlcpy(fallback[fallback_count++],child,sizeof(fallback[0]));
                    for(unsigned i=0;i<3;i++) if(name.equalsIgnoreCase(wanted[i]))
                        strlcpy(targets[i],child,sizeof(targets[0]));
                }
            }
        }
        entry.close(); entry=dir.openNextFile();
    }
    dir.close();
}
static int read_at(void *user,uint64_t offset,void *dst,size_t bytes) {
    File *f=(File *)user;int64_t start=esp_timer_get_time();
    bool ok=offset<=UINT32_MAX && f->seek((uint32_t)offset,SeekSet) && f->read((uint8_t *)dst,bytes)==bytes;
    read_us+=esp_timer_get_time()-start;read_calls++;read_bytes+=bytes;
    return ok?0:-1;
}
static void benchmark(const char *path) {
    Serial.print("{\"event\":\"start\",\"file\":");json_string(path);Serial.println("}");
    File file=SD.open(path,FILE_READ);
    if(!file) { Serial.println("{\"event\":\"error\",\"error\":\"file open failed\"}");return; }
    uint32_t heap_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    miniaac_source source={read_at,&file,file.size()};miniaac *d=nullptr;miniaac_info info;
    read_us=read_calls=read_bytes=0;
    int64_t start=esp_timer_get_time();
    miniaac_status rc=miniaac_open(workspace,sizeof(workspace),&source,&d);
    uint64_t open_us=esp_timer_get_time()-start;
    uint32_t heap_after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(rc!=MINIAAC_OK) {
        Serial.printf("{\"event\":\"error\",\"status\":%d,\"error\":\"%s\"}\n",rc,miniaac_status_string(rc));file.close();return;
    }
    miniaac_get_info(d,&info);
    uint64_t frames=0,decode_us=0,max_us=0,max_steady_us=0,blocks=0,over_deadline=0;
    uint32_t hash=0;size_t count;
    int64_t playing=esp_timer_get_time(),last_progress=playing;
    const uint64_t deadline=(1024ull*1000000)/info.sample_rate;
    for(;;) {
        int64_t begin=esp_timer_get_time();
        rc=miniaac_decode_next(d,pcm,MINIAAC_PCM_SAMPLES,&count);
        uint64_t us=esp_timer_get_time()-begin;decode_us+=us;
        if(us>max_us) max_us=us;
        if(blocks && us>max_steady_us) max_steady_us=us;
        if(blocks && us>deadline) over_deadline++;
        if(rc!=MINIAAC_OK) break;
        for(size_t i=0;i<count;i++) hash=hash*33u+(uint16_t)pcm[i];
        frames+=count/info.channels;blocks++;
        if(!(blocks%16)) delay(1);
        if(esp_timer_get_time()-last_progress>3000000) {
            Serial.printf("{\"event\":\"progress\",\"job\":%u,\"frames\":%llu,\"audio_seconds\":%.3f}\n",
                          job,(unsigned long long)frames,(double)frames/info.sample_rate);
            last_progress=esp_timer_get_time();
        }
    }
    uint64_t wall_us=esp_timer_get_time()-playing;
    Serial.print("{\"event\":\"result\",\"file\":");json_string(path);
    Serial.printf(",\"status\":%d,\"rate\":%u,\"channels\":%u,\"frames\":%llu,\"expected_frames\":%llu,"
                  "\"pcm_hash\":%u,\"audio_seconds\":%.6f,\"wall_us\":%llu,\"open_us\":%llu,"
                  "\"decode_total_us\":%llu,\"read_total_us\":%llu,\"max_decode_us\":%llu,"
                  "\"max_steady_decode_us\":%llu,\"over_deadline_calls\":%llu,\"blocks\":%llu,"
                  "\"read_calls\":%llu,\"read_bytes\":%llu,\"workspace\":%u,\"pcm_bytes\":%u,"
                  "\"heap_before_open\":%u,\"heap_after_open\":%u,\"heap_end\":%u,"
                  "\"stack_budget\":%u,\"stack_high_water_bytes\":%u}\n",
                  rc,(unsigned)info.sample_rate,(unsigned)info.channels,(unsigned long long)frames,
                  (unsigned long long)info.total_frames,(unsigned)hash,(double)frames/info.sample_rate,
                  (unsigned long long)wall_us,(unsigned long long)open_us,(unsigned long long)decode_us,
                  (unsigned long long)read_us,(unsigned long long)max_us,(unsigned long long)max_steady_us,
                  (unsigned long long)over_deadline,(unsigned long long)blocks,(unsigned long long)read_calls,
                  (unsigned long long)read_bytes,(unsigned)miniaac_workspace_size(),(unsigned)sizeof(pcm),
                  heap_before,heap_after,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                  (unsigned)PRODUCER_STACK,(unsigned)uxTaskGetStackHighWaterMark(nullptr));
    miniaac_close(d);file.close();
}
void setup() {
    Serial.begin(115200);delay(300);
    SPI.begin(18,19,23,5);
    if(!SD.begin(5,SPI,20000000)) { Serial.println("{\"event\":\"fatal\",\"error\":\"SD mount failed\"}");finished=true;return; }
    scan("/",0);
    for(unsigned i=0;i<3;i++) if(!targets[i][0]) {
        for(unsigned f=0;f<fallback_count;f++) {
            bool duplicate=false;
            for(unsigned j=0;j<3;j++) if(!strcmp(targets[j],fallback[f])) duplicate=true;
            if(!duplicate) { strlcpy(targets[i],fallback[f],sizeof(targets[0]));break; }
        }
    }
    Serial.printf("{\"event\":\"ready\",\"files\":%u,\"psram_bytes\":%u,\"cpu_mhz\":%u,\"workspace\":%u}\n",
                  files,(unsigned)ESP.getPsramSize(),(unsigned)ESP.getCpuFreqMHz(),(unsigned)miniaac_workspace_size());
    for(unsigned i=0;i<3;i++) {
        Serial.print("{\"event\":\"selected\",\"file\":");json_string(targets[i]);Serial.println("}");
    }
}
static void producer(void *) {
    while(job<3) { if(targets[job][0]) benchmark(targets[job]);job++; }
    Serial.println("{\"event\":\"done\"}");SD.end();finished=true;
    vTaskDelete(nullptr);
}
void loop() {
    if(finished) { delay(100);return; }
    if(!started) {
        if(Serial.available() && Serial.read()=='G') {
            started=true;
            if(xTaskCreatePinnedToCore(producer,"aac-bench",PRODUCER_STACK,nullptr,1,nullptr,1)!=pdPASS) {
                Serial.println("{\"event\":\"fatal\",\"error\":\"producer task allocation failed\"}");finished=true;
            }
        }
        else { delay(10);return; }
    }
    delay(10);
}
