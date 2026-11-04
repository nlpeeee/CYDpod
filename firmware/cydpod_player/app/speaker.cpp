// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "speaker.h"
#include "audio.h"
#include "board.h"
#include "driver/dac_continuous.h"
#if CYD_WIRED_OUTPUT
#include "wired.h"
#endif

// ---------------------------------------------------------------------------
// The route
// ---------------------------------------------------------------------------
// audio.cpp's ring has one tail pointer and therefore exactly one consumer.
// Bluetooth's onData() and the writer task below both check this before they
// pull, and a switch parks on OUT_NONE long enough for whichever of them was
// mid-callback to finish. That is cheaper and safer than a lock the A2DP task
// would have to take on every 128 frames.

static volatile uint8_t route  = OUT_NONE;
static volatile bool    parked = true;        // the writer task is not pulling
static volatile uint32_t stalls = 0;
static TaskHandle_t writerHandle = nullptr;

uint8_t speaker_route() { return route; }
#if CYD_WIRED_OUTPUT
void speaker_routeFailed(uint8_t r) { if (route == r) route = OUT_NONE; }
#endif
uint32_t speaker_stalls() { return stalls; }

// ---------------------------------------------------------------------------
// The DAC
// ---------------------------------------------------------------------------
// 256 frames is 5.8 ms: short enough that a route switch is not audible as a
// gap, long enough that the writer task wakes ~172 times a second and not
// more. desc_num * buf_size = 3 KB of DMA, about 35 ms of cushion behind
// audio.cpp's own 46 ms ring.
//
// CHUNK MUST STAY A MULTIPLE OF 4, and no larger than desc_num * buf_size.
// The DAC's DMA works in 32-bit words, and dac_continuous_write() does not
// tell you when you have broken that rule -- it just misbehaves, measurably:
//
//   length % 4 == 0   full rate          (256, 440, 512, 1024, 3072)
//   length % 2 == 0   EXACTLY HALF rate  (442, 446 -- everything plays an
//                                         octave low, and nothing reports it)
//   length odd        the DMA stalls, writes time out, playback deadlocks
//   length > 3072     deadlocks (that is desc_num * buf_size)
//
// buf_size has to be a multiple of 4 as well. Measured on this board; see
// ../../CYD Speaker Test/FINDINGS.md for the sweep this came from.
static const int32_t CHUNK = 256;
static dac_continuous_handle_t dac = nullptr;
static int16_t* stereo = nullptr;
static uint8_t* mono = nullptr;
static uint8_t lastLevel = 128;               // where the ramp starts

bool speaker_active() { return dac != nullptr; }

static bool dacStart() {
  if (dac) return true;
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = CYD_SPEAKER_DAC_MASK;       // GPIO26. CH0 is GPIO25 = touch clock.
  cfg.desc_num  = 6;
  cfg.buf_size  = 512;
  cfg.freq_hz   = 44100;                      // what audio.cpp resamples everything to
  cfg.offset    = 0;
  cfg.clk_src   = DAC_DIGI_CLK_SRC_DEFAULT;   // 19.6 kHz upward on the ESP32
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
  esp_err_t err = dac_continuous_new_channels(&cfg, &dac);
  if (err == ESP_OK) err = dac_continuous_enable(dac);
  if (err != ESP_OK) {
    Serial.printf("speaker: DAC start failed (%s)\n", esp_err_to_name(err));
    if (dac) { dac_continuous_del_channels(dac); dac = nullptr; }
    return false;
  }
  // Settle the pin at mid-rail BEFORE the amplifier is listening. The DAC
  // rests at 0 V until something is written; turning the amp on first and then
  // jumping to 128 is a thump through the speaker. This is the counterpart of
  // dacRampDown() below.
  size_t loaded = 0;
  for (int32_t i = 0; i < CHUNK; i++) mono[i] = 128;
  dac_continuous_write(dac, mono, CHUNK, &loaded, 200);
  lastLevel = 128;

  cydAmpEnable();          // GPIO4 LOW. Lights the red LED: same pin (board.h).

  Serial.printf("speaker: DAC on GPIO%d at 44100 Hz, amp on GPIO%d  heap %u\n",
                CYD_SPEAKER_PIN, CYD_AMP_ENABLE_PIN, ESP.getFreeHeap());
  return true;
}

// Idle is mid-rail (128), the pin at rest is 0 V. Walking between the two
// instead of jumping is the difference between a soft sigh and a click.
static void dacRampDown() {
  if (!dac) return;
  size_t loaded = 0;
  for (int step = 0; step < 8; step++) {
    const int from = lastLevel - lastLevel * step / 8;
    const int to   = lastLevel - lastLevel * (step + 1) / 8;
    for (int32_t i = 0; i < CHUNK; i++) mono[i] = (uint8_t)(from + (to - from) * i / CHUNK);
    dac_continuous_write(dac, mono, CHUNK, &loaded, 200);
  }
  lastLevel = 0;
}

static void dacStop() {
  if (!dac) return;
  dacRampDown();           // walk to 0 V while the amp can still hear it
  cydAmpDisable();         // then mute, so the teardown below is inaudible
  dac_continuous_disable(dac);
  dac_continuous_del_channels(dac);
  dac = nullptr;
  Serial.println("speaker: DAC off, amp in shutdown");
}

// ---------------------------------------------------------------------------
// Writer task
// ---------------------------------------------------------------------------
// dac_continuous_write() blocks until the DMA has room, so it is the playback
// clock for this route exactly as the A2DP callback is for the other one --
// which is what keeps audio.cpp's pushFrame() moving.
//
// Stereo to mono, 16 bits to 8. The truncation is noise-shaped: the volume in
// audio_pull() is applied in 16 bits, so at the default 40% only about five
// bits reach the DAC, and plain truncation there is audible as buzz on quiet
// passages. Carrying the error into the next sample pushes it up out of the
// way for the cost of two adds.
static void writerTask(void*) {
  // Wait until the creator has published the route and unparked state.
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  int32_t err = 0;
  for (;;) {
    if (route != OUT_SPEAKER) {
      if (!parked) { dacStop(); err = 0; parked = true; }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    parked = false; // includes DAC startup: never delete/free during a driver call
    if (!dac && !dacStart()) { route = OUT_NONE; continue; }

    audio_pull(stereo, CHUNK);
    for (int32_t i = 0; i < CHUNK; i++) {
      const int32_t mix = ((int32_t)stereo[i * 2] + stereo[i * 2 + 1]) / 2;
      int32_t v = mix + err;
      if (v >  32767) v =  32767;
      if (v < -32768) v = -32768;
      int32_t q = (v + 128) >> 8;              // round, not truncate
      if (q >  127) q =  127;                  // +128 would wrap the byte
      if (q < -128) q = -128;
      err = v - (q << 8);
      mono[i] = (uint8_t)(q + 128);            // 128 is mid-rail, and silence
    }
    lastLevel = mono[CHUNK - 1];

    size_t loaded = 0;
    if (dac_continuous_write(dac, mono, CHUNK, &loaded, 500) != ESP_OK) stalls++;
  }
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
bool speaker_begin(uint8_t startRoute) { return speaker_setRoute(startRoute); }

bool speaker_setRoute(uint8_t r) {
  if (r != OUT_NONE && r >= OUT_ROUTE_COUNT) r = OUT_BT;
  if (r == route && r != OUT_NONE) return true;
  // Reserve the new buffers before interrupting a working Bluetooth route.
  int16_t* nextStereo = nullptr;
  uint8_t* nextMono = nullptr;
  if (r == OUT_SPEAKER) {
    nextStereo = (int16_t*)malloc(CHUNK * 2 * sizeof(int16_t));
    nextMono = (uint8_t*)malloc(CHUNK);
    if (!nextStereo || !nextMono) {
      free(nextStereo); free(nextMono);
      Serial.println("speaker: no memory, route unchanged");
      return false;
    }
  }
#if CYD_WIRED_OUTPUT
  if (r == OUT_WIRED && !wired_reserve()) {
    Serial.println("speaker: no memory for the wired route, route unchanged");
    return false;
  }
#endif
  const uint8_t previous = route;
  route = OUT_NONE;
  // One 500 ms write and eight 200 ms ramp writes can be outstanding.
  const uint32_t waitAt = millis();
#if CYD_WIRED_OUTPUT
  // The wired task needs a final fade and a channel teardown: a few chunks of writes
  // at 100 ms timeouts, well inside this.
  while (((writerHandle && !parked) || !wired_parked()) && millis() - waitAt < 3000) delay(10);
  if ((writerHandle && !parked) || !wired_parked()) {
#else
  while (writerHandle && !parked && millis() - waitAt < 3000) delay(10);
  if (writerHandle && !parked) {
#endif
    route = previous;
    free(nextStereo); free(nextMono);
#if CYD_WIRED_OUTPUT
    if (r == OUT_WIRED) wired_release();   // refuses while a wired task still exists
#endif
    Serial.println("speaker: park timed out, route unchanged");
    return false;
  }
  // parked is published only AFTER dacStop() has finished using the buffers.
  if (writerHandle) { vTaskDelete(writerHandle); writerHandle = nullptr; }
  free(stereo); free(mono); stereo = nullptr; mono = nullptr;
#if CYD_WIRED_OUTPUT
  // wired_parked() is published only AFTER the channel is gone and the last write has returned.
  wired_deleteTask();
  if (r != OUT_WIRED) wired_release();
#endif
  delay(50); // let any in-flight Bluetooth audio_pull finish before the new consumer
  if (r == OUT_SPEAKER) {
    stereo = nextStereo; mono = nextMono;
    parked = false;
    if (xTaskCreatePinnedToCore(writerTask, "spkout", 3072, nullptr, 5, &writerHandle, 0) != pdPASS) {
      writerHandle = nullptr; parked = true;
      free(stereo); free(mono); stereo = nullptr; mono = nullptr;
      route = previous == OUT_BT ? OUT_BT : OUT_NONE;
      Serial.println("speaker: task allocation failed");
      return false;
    }
    route = r;
    xTaskNotifyGive(writerHandle);
  }
#if CYD_WIRED_OUTPUT
  else if (r == OUT_WIRED) {
    if (!wired_startTask()) {
      wired_release();
      route = previous == OUT_BT ? OUT_BT : OUT_NONE;
      Serial.println("speaker: wired task allocation failed");
      return false;
    }
    route = r;
    wired_kick();
  }
#endif
  else route = r;
#if CYD_WIRED_OUTPUT
  const char* const name = r == OUT_NONE ? "off" : r == OUT_SPEAKER ? "speaker" : r == OUT_WIRED ? "wired" : "bluetooth";
  Serial.printf("cmd: route %s\n", name);
  Serial.printf("speaker: route -> %s  heap %u largest %u\n", name, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#else
  Serial.printf("speaker: route -> %s  heap %u largest %u\n",
                r == OUT_SPEAKER ? "speaker" : "bluetooth", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  return true;
}

#endif  // !CYD_UPLOADER
