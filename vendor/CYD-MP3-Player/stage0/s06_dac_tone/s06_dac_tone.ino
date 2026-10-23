// ===========================================================================
// s06_dac_tone -- is there a path from GPIO26 to a noise?
// ===========================================================================
// SUPERSEDED, AND ITS ORIGINAL VERDICT WAS WRONG. Kept because the reasoning
// below is still sound and the trap it fell into is worth seeing.
//
// This sketch was silent in every phase, including the hardware cosine
// generator, and that was read as "the firmware is not the problem -- nothing
// is connected to GPIO26, or the amp is dead". It became the load-bearing
// claim of the whole investigation and it was false.
//
// The amplifier is held in SHUTDOWN unless GPIO4 is driven LOW, and a floating
// GPIO4 -- which is what this sketch leaves, since it never touches the pin --
// counts as shutdown. So all four phases below were played into a muted
// amplifier. They would have been silent whatever the DAC did.
//
// The fault in the method, worth remembering: a negative result was treated as
// evidence about the thing being tested, when the test itself had never been
// shown capable of producing a positive. Nothing here establishes that this
// sketch COULD make a noise.
//
// To make this sketch work, add to setup():
//     pinMode(CYD_AMP_ENABLE_PIN, OUTPUT);
//     digitalWrite(CYD_AMP_ENABLE_PIN, LOW);   // or cydAmpEnable()
// It is left out on purpose, so the file still shows what was actually run.
//
// Note also: phases C and D write 441 bytes, which is odd, and the DAC's DMA
// needs a multiple of 4 -- so their reported rates were wrong too, for a
// second and unrelated reason. See docs/stage0_results.md.
//
// The full account is in docs/decisions.md (2026-09-28) and in the standalone
// project ../CYD Speaker Test, whose FINDINGS.md carries the evidence trail.
// ===========================================================================
// Written to find out why app/speaker.cpp is silent on a board whose serial
// log says the DAC is running and the DMA is draining. It plays 1 kHz, full
// scale, three different ways, five seconds each, for ever:
//
//   A  dacWrite()        -- the simplest thing that exists: one 8-bit write at
//                           a time from a loop. No DMA, no peripheral clock.
//   B  cosine generator  -- the DAC's own hardware oscillator. No CPU at all.
//   C  dac_continuous    -- DMA at 44.1 kHz off PLL_D2: what the player does.
//   D  dac_continuous    -- the same, off APLL, which can hit audio rates exactly.
//
// What each outcome means:
//   nothing at all       -> the amp, CN1 or the speaker. Not the firmware.
//   A and B but not C    -> the DMA path or its clock; the player's bug.
//   all three            -> the player's own audio path (mix, dither, volume).
//
// Nothing else runs: no SD card, no display, no Bluetooth. If this is silent
// there is nothing left in software to blame.
#include <Arduino.h>
#include "board.h"
#include "driver/dac_oneshot.h"
#include "driver/dac_cosine.h"
#include "driver/dac_continuous.h"

static const int TONE_HZ = 1000;
static const int PHASE_MS = 5000;

// --- A: one write at a time ------------------------------------------------
static void phaseManual() {
  Serial.println("A: dacWrite() 1 kHz full scale");
  dac_oneshot_handle_t h = nullptr;
  dac_oneshot_config_t cfg = { .chan_id = DAC_CHAN_1 };   // GPIO26
  if (dac_oneshot_new_channel(&cfg, &h) != ESP_OK) {
    Serial.println("A: could not open the channel");
    return;
  }
  // 32 samples per cycle at 1 kHz = one every 31 us.
  uint8_t wave[32];
  for (int i = 0; i < 32; i++) wave[i] = (uint8_t)(127.5 + 127.5 * sin(2 * PI * i / 32));
  const uint32_t until = millis() + PHASE_MS;
  while (millis() < until)
    for (int i = 0; i < 32; i++) {
      dac_oneshot_output_voltage(h, wave[i]);
      delayMicroseconds(1000000 / TONE_HZ / 32);
    }
  dac_oneshot_del_channel(h);
}

// --- B: the hardware oscillator -------------------------------------------
static void phaseCosine() {
  Serial.println("B: hardware cosine generator, 1 kHz, no attenuation");
  dac_cosine_handle_t h = nullptr;
  dac_cosine_config_t cfg = {};
  cfg.chan_id = DAC_CHAN_1;
  cfg.freq_hz = TONE_HZ;
  cfg.clk_src = DAC_COSINE_CLK_SRC_DEFAULT;
  cfg.atten   = DAC_COSINE_ATTEN_DEFAULT;                 // full amplitude
  cfg.phase   = DAC_COSINE_PHASE_0;
  cfg.offset  = 0;
  cfg.flags.force_set_freq = true;
  if (dac_cosine_new_channel(&cfg, &h) != ESP_OK) {
    Serial.println("B: could not open the channel");
    return;
  }
  dac_cosine_start(h);
  delay(PHASE_MS);
  dac_cosine_stop(h);
  dac_cosine_del_channel(h);
}

// --- C: the player's own path ---------------------------------------------
static void phaseContinuous(const char* tag, dac_continuous_digi_clk_src_t clk) {
  Serial.printf("%s: dac_continuous DMA at 44100 Hz, 1 kHz full scale\n", tag);
  dac_continuous_handle_t h = nullptr;
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = CYD_SPEAKER_DAC_MASK;
  cfg.desc_num  = 6;
  cfg.buf_size  = 512;
  cfg.freq_hz   = 44100;
  cfg.offset    = 0;
  cfg.clk_src   = clk;
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
  esp_err_t err = dac_continuous_new_channels(&cfg, &h);
  if (err == ESP_OK) err = dac_continuous_enable(h);
  if (err != ESP_OK) {
    Serial.printf("%s: %s\n", tag, esp_err_to_name(err));
    if (h) dac_continuous_del_channels(h);
    return;
  }
  // 44100 / 1000 is not a whole number of samples, so the buffer is a whole
  // number of cycles instead: 441 samples is exactly 10 cycles of 1 kHz.
  static uint8_t buf[441];
  for (int i = 0; i < 441; i++)
    buf[i] = (uint8_t)(127.5 + 127.5 * sin(2 * PI * i * TONE_HZ / 44100));
  const uint32_t until = millis() + PHASE_MS;
  uint32_t writes = 0, stalls = 0;
  size_t loaded = 0;
  while (millis() < until) {
    if (dac_continuous_write(h, buf, sizeof(buf), &loaded, 500) != ESP_OK) stalls++;
    writes++;
  }
  // 44100 / 441 = 100 writes a second. A number far off 500 means the DMA is
  // not clocking at the rate it was asked for -- which would be the bug.
  // 441 samples a write, so 100 writes a second is the asked-for 44100.
  // The measured rate is what says whether the DMA clock is what it claims.
  Serial.printf("%s: %lu writes in %d ms (expect ~500) -> %lu Hz, %lu stalls\n",
                tag, writes, PHASE_MS, (unsigned long)((uint64_t)writes * 441 * 1000 / PHASE_MS), stalls);
  dac_continuous_disable(h);
  dac_continuous_del_channels(h);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\ns06_dac_tone");
  cydPrintBanner();
  Serial.printf("speaker pin GPIO%d\n\n", CYD_SPEAKER_PIN);
}

void loop() {
  phaseManual();    delay(400);
  phaseCosine();    delay(400);
  phaseContinuous("C  (PLL_D2)", DAC_DIGI_CLK_SRC_PLLD2); delay(400);
  phaseContinuous("D  (APLL)  ", DAC_DIGI_CLK_SRC_APLL);  delay(400);
  Serial.println("--- round done ---\n");
}
