// Player image only (firmware.h), and only where board.h routes the I2S pins.
#include "board.h"
#if CYD_WIRED_OUTPUT && !defined(CYD_UPLOADER)

#include "wired.h"
#include "audio.h"
#include "speaker.h"
#include "wired_pump.h"
#include <driver/i2s_std.h>
#include <new>

// ---------------------------------------------------------------------------
// The driver: ESP-IDF 5.5 `i2s_std` only
// ---------------------------------------------------------------------------
// Arduino-ESP32 3.3.10 ships it as esp32-libs/3.3.10/include/esp_driver_i2s/
// include/driver/i2s_std.h. The legacy driver/i2s.h must not be linked into
// the same image (the new driver aborts at boot if it finds it); this player
// does not use it, and its other audio driver, dac_continuous, is the new-style
// one (speaker.cpp). The two routes never run together: speaker_setRoute() parks
// one consumer before it starts the other, and the DAC and I2S channels are
// never open at once. i2s_new_channel(I2S_NUM_AUTO) takes whichever controller
// is free.
//
// THE PROVEN SETTINGS (CY-70 tone sketch, which the user heard working):
// 44.1 kHz, 16-bit data in a 16-bit slot, Philips, master, MCLK unused (the
// PCM5100A derives its clock from BCK through its PLL, datasheet 9.3.5.3),
// BCK = 32 x fS = 1.4112 MHz, DMA 6 x 256 frames, auto_clear so a stalled
// writer produces zeros instead of a repeating buffer. If the sound turns out
// better with another phase of the CY-75 diagnostic, change these two and
// rebuild; nothing else depends on them:
//   CYD_WIRED_SLOT_BITS   16 (BCK = 32 fS) or 32 (BCK = 64 fS, MCLK x512)
//   CYD_WIRED_MSB_FORMAT  0 = Philips (I2S), 1 = MSB / left-justified
#ifndef CYD_WIRED_SLOT_BITS
#define CYD_WIRED_SLOT_BITS 16
#endif
#ifndef CYD_WIRED_MSB_FORMAT
#define CYD_WIRED_MSB_FORMAT 0
#endif
#if CYD_WIRED_SLOT_BITS != 16 && CYD_WIRED_SLOT_BITS != 32
#error "CYD_WIRED_SLOT_BITS must be 16 or 32"
#endif

namespace {

const uint32_t kRateHz = 44100;          // what audio.cpp resamples everything to
const uint32_t kDmaDescNum = 6;
const uint32_t kDmaFrameNum = 256;       // 6 x 256 x 4 B = 6 KB, 35 ms behind the 46 ms ring
// A write blocks until the DMA has room. 100 ms is 17 chunks of headroom; three
// failures in a row drop the chunk (wired_pump.h). Nothing in the audio path
// waits forever. i2s_channel_write() takes MILLISECONDS (esp_driver_i2s/include/
// driver/i2s_common.h), not ticks.
const uint32_t kWriteTimeoutMs = 100;
// After a dropped chunk, one chunk time before trying again: a sink that fails at once
// (driver error) would otherwise spin, pulling the ring dry at CPU speed.
const TickType_t kBackoffTicks = pdMS_TO_TICKS(6);
static_assert(cydwired::kDmaCapacityFrames == 6 * 256, "keep kDmaCapacityFrames in step with the DMA config");
// Core 0 priority 5, the same as the on-board speaker's writer: Bluetooth's
// controller tasks sit far above it and are idle on this route, the decode task
// (core 1, priority 5) and the UI loop are on the other core, and a 35 ms DMA
// cushion absorbs any scheduling jitter, so a higher priority buys nothing.
const UBaseType_t kTaskPriority = 5;
const uint32_t kTaskStackBytes = 4096;   // Serial.printf with floats is not used; ~1.3 KB peak expected
const BaseType_t kTaskCore = 0;

struct Work {
  int16_t chunk[cydwired::kChunkFrames * 2];
  cydwired::Fade fade;
};

Work* work = nullptr;
i2s_chan_handle_t tx = nullptr;
bool enabled = false;
cydwired::Counters counters;
volatile bool parked = true;
volatile bool muted = false;
volatile uint32_t sessions = 0;
volatile uint32_t heapCost = 0;
uint32_t heapBefore = 0;
TaskHandle_t taskHandle = nullptr;
esp_err_t lastWriteErr = ESP_OK;

struct AudioSource {
  bool playing() const { return audio_isPlaying(); }
  bool seeking() const { return audio_isSeeking(); }
  uint32_t serial() const { return audio_trackSerial(); }
  uint32_t pulledFrames() const { return audio_pullFrames(); }
  uint32_t underruns() const { return audio_underruns(); }
  void pull(int16_t* stereo, int32_t frames) const { audio_pull(stereo, frames); }
};

struct Sink {
  cydwired::WriteStatus operator()(const uint8_t* p, size_t bytes, size_t* written) const {
    const esp_err_t err = i2s_channel_write(tx, p, bytes, written, kWriteTimeoutMs);
    if (err == ESP_OK) return cydwired::WriteStatus::Ok;
    if (err == ESP_ERR_TIMEOUT) return cydwired::WriteStatus::Timeout;
    lastWriteErr = err;
    return cydwired::WriteStatus::Error;
  }
};

void teardownChannel() {
  if (tx) {
    if (enabled) i2s_channel_disable(tx);
    i2s_del_channel(tx);
  }
  tx = nullptr;
  enabled = false;
  // GPIO4/16/17 are the RGB LED channels (active low): put them back to "off",
  // the state cydRgbLedOff() left them in at boot.
  cydRgbLedOff();
}

bool startStream() {
  heapBefore = heapBefore ? heapBefore : ESP.getFreeHeap();
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan.dma_desc_num = kDmaDescNum;
  chan.dma_frame_num = kDmaFrameNum;
  chan.auto_clear = true;
  esp_err_t err = i2s_new_channel(&chan, &tx, nullptr);
  if (err != ESP_OK) {
    Serial.printf("i2s-wired: i2s_new_channel failed err=%d (%s)\n", (int)err, esp_err_to_name(err));
    tx = nullptr;
    return false;
  }
  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRateHz);
#if CYD_WIRED_MSB_FORMAT
  cfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#else
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#endif
#if CYD_WIRED_SLOT_BITS == 32
  cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;  // BCK = 64 x fS
  cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512;
#endif
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.bclk = (gpio_num_t)CYD_I2S_BCK_PIN;
  cfg.gpio_cfg.ws = (gpio_num_t)CYD_I2S_WS_PIN;
  cfg.gpio_cfg.dout = (gpio_num_t)CYD_I2S_DOUT_PIN;
  cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  err = i2s_channel_init_std_mode(tx, &cfg);
  if (err != ESP_OK) {
    Serial.printf("i2s-wired: i2s_channel_init_std_mode failed err=%d (%s)\n", (int)err, esp_err_to_name(err));
    teardownChannel();
    return false;
  }
  // The on-board speaker amp's input (GPIO26, DAC2) must not float while the
  // wired route owns the audio.
  pinMode(CYD_SPEAKER_PIN, OUTPUT);
  digitalWrite(CYD_SPEAKER_PIN, LOW);
  err = i2s_channel_enable(tx);
  if (err != ESP_OK) {
    Serial.printf("i2s-wired: i2s_channel_enable failed err=%d (%s)\n", (int)err, esp_err_to_name(err));
    teardownChannel();
    return false;
  }
  enabled = true;
  work->fade.reset();
  ++sessions;
  // Clocks run on zeros before any signal: >= 16 LRCK for the PLL and the DAC's
  // 104-sample un-mute, with margin.
  Sink sink;
  cydwired::writeZeros(work->chunk, cydwired::kLeadInFrames, sink, counters);
  heapCost = heapBefore > ESP.getFreeHeap() ? heapBefore - ESP.getFreeHeap() : 0;
  Serial.printf("i2s-wired: start bck=%d ws=%d dout=%d rate=%u slot=%d %s dma=%ux%u heap %u (cost %u) largest %u\n",
                CYD_I2S_BCK_PIN, CYD_I2S_WS_PIN, CYD_I2S_DOUT_PIN, (unsigned)kRateHz, CYD_WIRED_SLOT_BITS,
                CYD_WIRED_MSB_FORMAT ? "msb" : "philips", (unsigned)kDmaDescNum, (unsigned)kDmaFrameNum,
                ESP.getFreeHeap(), (unsigned)heapCost, ESP.getMaxAllocHeap());
  return true;
}

void stopStream() {
  if (!tx) return;
  Sink sink;
  // Final fade-out of the held sample, then enough zeros to push it through the DMA
  // (wired_pump.h kTailFrames), then the channel goes away: a route switch must not pop.
  const bool clean = cydwired::finishStream(work->fade, work->chunk, sink, counters);
  teardownChannel();
  Serial.printf("i2s-wired: stop%s frames=%u underruns=%u errors=%u stalls=%u dropped=%u\n",
                clean ? "" : " (sink stuck, fade cut short)", (unsigned)counters.frames,
                (unsigned)counters.underruns, (unsigned)counters.writeErrors, (unsigned)counters.stalls,
                (unsigned)counters.dropped);
}

void logWriteErrors(uint32_t& seen) {
  static uint32_t lastLogMs = 0;
  if (counters.writeErrors == seen) return;
  seen = counters.writeErrors;
  const uint32_t now = millis();
  if (now - lastLogMs < 1000) return;   // bounds the log rate if a failure repeats
  lastLogMs = now;
  Serial.printf("i2s-wired: i2s_channel_write err=%d (%s) errors=%u stalls=%u dropped=%u\n", (int)lastWriteErr,
                esp_err_to_name(lastWriteErr), (unsigned)counters.writeErrors, (unsigned)counters.stalls,
                (unsigned)counters.dropped);
}

void writerTask(void*) {
  // The creator publishes the route, then notifies. Bounded wait, even here.
  while (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100))) {}
  AudioSource audio;
  Sink sink;
  uint32_t errorsSeen = 0, stallsSeen = 0;
  for (;;) {
    if (speaker_route() != OUT_WIRED) {
      if (!parked) { stopStream(); parked = true; }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    parked = false;   // includes channel startup: never delete or free during a driver call
    if (!tx && !startStream()) {
      Serial.println("i2s-wired: start failed, output is silent until the route is set again");
      speaker_routeFailed(OUT_WIRED);
      continue;
    }
    if (!cydwired::pumpOnce(audio, work->fade, work->chunk, sink, counters, true, muted))
      vTaskDelay(kBackoffTicks);
    logWriteErrors(errorsSeen);
    if (counters.stalls != stallsSeen) {
      if (stallsSeen == 0) Serial.printf("i2s-wired: DMA stall (write timed out), stalls=%u\n", (unsigned)counters.stalls);
      stallsSeen = counters.stalls;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
bool wired_reserve() {
  if (work) return true;
  heapBefore = ESP.getFreeHeap();
  void* mem = malloc(sizeof(Work));
  if (!mem) return false;
  work = new (mem) Work();
  return true;
}

void wired_release() {
  if (taskHandle || !work) return;   // the task may still be using it
  work->~Work();
  free(work);
  work = nullptr;
  heapBefore = 0;
}

bool wired_startTask() {
  if (taskHandle) return true;
  if (!work) return false;
  parked = false;
  if (xTaskCreatePinnedToCore(writerTask, "wiredout", kTaskStackBytes, nullptr, kTaskPriority, &taskHandle,
                              kTaskCore) != pdPASS) {
    taskHandle = nullptr;
    parked = true;
    Serial.println("i2s-wired: task allocation failed");
    return false;
  }
  return true;
}

void wired_kick() {
  if (taskHandle) xTaskNotifyGive(taskHandle);
}

bool wired_parked() { return taskHandle == nullptr || parked; }

void wired_deleteTask() {
  if (!taskHandle) return;
  vTaskDelete(taskHandle);
  taskHandle = nullptr;
  parked = true;
}

bool wired_active() { return enabled; }

void wired_setMuted(bool m) { muted = m; }

WiredStats wired_stats() {
  WiredStats s = {};
  s.active = enabled;
  s.sessions = sessions;
  s.chunks = counters.chunks;
  s.frames = counters.frames;
  s.underruns = counters.underruns;
  s.writeErrors = counters.writeErrors;
  s.stalls = counters.stalls;
  s.dropped = counters.dropped;
  s.stackFree = taskHandle ? (uint32_t)uxTaskGetStackHighWaterMark(taskHandle) : 0;
  s.heapCost = heapCost;
  return s;
}

#endif  // CYD_WIRED_OUTPUT && !CYD_UPLOADER
