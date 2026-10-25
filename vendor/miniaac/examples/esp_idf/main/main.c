/* ESP-IDF 5.x SD decode benchmark. Replace consume_pcm() with an output queue. */
#include "miniaac.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <limits.h>

static uint64_t workspace[MINIAAC_WORKSPACE_BYTES/8];
static int16_t pcm[MINIAAC_PCM_SAMPLES];
static volatile uint32_t checksum;
static int read_at(void *user,uint64_t offset,void *dst,size_t bytes) {
    FILE *f=(FILE *)user;
    // This stdio adapter intentionally uses fseek/long (2 GiB on ESP32).
    if(offset>LONG_MAX || fseek(f,(long)offset,SEEK_SET)) return -1;
    return fread(dst,1,bytes,f)==bytes ? 0 : -1;
}
static void consume_pcm(const int16_t *data,size_t count,const miniaac_info *info) {
    /* Discard sink for benchmarking. A real sink must accept/copy all samples
       before returning. You may block here in the producer task. Never put
       decoding/SD reads into an ISR or Bluetooth audio callback. */
    (void)info;
    for(size_t i=0;i<count;i++) checksum=checksum*33u+(uint16_t)data[i];
}
void app_main(void) {
    sdmmc_host_t host=SDSPI_HOST_DEFAULT(); host.slot=SPI3_HOST;
    spi_bus_config_t bus={.mosi_io_num=23,.miso_io_num=19,.sclk_io_num=18,
                         .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=4096};
    sdspi_device_config_t slot=SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id=host.slot; slot.gpio_cs=5;
    esp_vfs_fat_sdmmc_mount_config_t mount={.format_if_mount_failed=false,.max_files=1,
                                          .allocation_unit_size=4096};
    sdmmc_card_t *card=NULL;
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot,&bus,SPI_DMA_CH_AUTO));
    esp_err_t err=esp_vfs_fat_sdspi_mount("/sdcard",&host,&slot,&mount,&card);
    if(err!=ESP_OK) { printf("SD mount error: %s\n",esp_err_to_name(err)); spi_bus_free(host.slot); return; }
    FILE *file=fopen("/sdcard/track.m4a","rb");
    if(!file) { puts("Put /track.m4a on SD"); goto unmount; }
    if(fseek(file,0,SEEK_END)) { fclose(file); goto unmount; }
    long size=ftell(file); if(size<0) { fclose(file); goto unmount; }
    miniaac_source source={read_at,file,(uint64_t)size};
    miniaac *decoder=NULL; miniaac_info info; size_t count;
    uint64_t frames=0,worst=0; int64_t started=esp_timer_get_time();
    miniaac_status rc=miniaac_open(workspace,sizeof(workspace),&source,&decoder);
    if(rc!=MINIAAC_OK) { printf("open: %s\n",miniaac_status_string(rc)); fclose(file); goto unmount; }
    miniaac_get_info(decoder,&info);
    printf("%luHz %u-channel; workspace required=%u, allocated=%u; PCM=%u\n",
           (unsigned long)info.sample_rate,info.channels,(unsigned)miniaac_workspace_size(),
           (unsigned)sizeof(workspace),(unsigned)sizeof(pcm));
    for(;;) {
        int64_t start=esp_timer_get_time();
        rc=miniaac_decode_next(decoder,pcm,MINIAAC_PCM_SAMPLES,&count);
        uint64_t took=(uint64_t)(esp_timer_get_time()-start); if(took>worst) worst=took;
        if(rc!=MINIAAC_OK) break;
        consume_pcm(pcm,count,&info); frames+=count/info.channels;
        /* Periodically yield during this faster-than-playback discard benchmark. */
        if(!(frames% (1024*16))) vTaskDelay(1);
    }
    printf("%s: %llu frames; audio %.3fs; wall %.3fs; max read/decode %lluus\n",
           miniaac_status_string(rc),(unsigned long long)frames,(double)frames/info.sample_rate,
           (esp_timer_get_time()-started)/1e6,(unsigned long long)worst);
    printf("stack high water=%u bytes; free internal heap=%u bytes\n",
           (unsigned)uxTaskGetStackHighWaterMark(NULL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    miniaac_close(decoder); fclose(file);
unmount:
    esp_vfs_fat_sdcard_unmount("/sdcard",card); spi_bus_free(host.slot);
}
