// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "eq.h"
#include "eq_xtensa.h"
#include <Arduino.h>
#include <esp_cpu.h>
#include <esp_timer.h>

// Loop task publishes; the decoder adopts at its next chunk. One short lock
// covers the 112-byte copy, never a design or a filter run.
static portMUX_TYPE eqLock = portMUX_INITIALIZER_UNLOCKED;
static cydeq::Coeffs published;
static volatile uint32_t publishedSerial = 0;
static volatile bool publishedOn = false;

// Decoder task only.
static cydeq::Coeffs active;
static cydeq::State state;
static uint32_t appliedSerial = 0;
static int32_t scratch[2 * EqChunkFrames];
static int16_t output[2 * EqChunkFrames];
static cydeq::StageFn kernel = cydeq::stageRefStride2;
static volatile uint32_t statFrames = 0, statCycles = 0;

static bool kernelIsXtensa = false;
static uint32_t testAsmCycles = 0, testRefCycles = 0;

static void adopt() {
  portENTER_CRITICAL(&eqLock);
  const cydeq::Coeffs next = published;
  const uint32_t serial = publishedSerial;
  portEXIT_CRITICAL(&eqLock);
  state.retarget(active, next);  // level continues from where it was, then glides
  state.seed(next);
  active = next;
  appliedSerial = serial;
}

bool eq_bypass() {
  if (publishedSerial != appliedSerial) adopt();
  return active.count == 0;
}

void eq_flush() {
  const uint32_t limited = state.limited;
  state.reset();
  state.seed(active);           // every running band restarts from silence
  state.gain = active.outGain;  // after a flush there is no level to glide from
  state.limited = limited;
}

const int16_t* eq_process(const int16_t* in, uint32_t n) {
  const uint32_t at = esp_cpu_get_cycle_count();
  cydeq::process(active, state, in, output, n, scratch, kernel);
  statCycles = statCycles + (esp_cpu_get_cycle_count() - at);
  statFrames = statFrames + n;
  return output;
}

// Runs both loops over the same 256 frames with every band at +12 dB. Called
// from setup() before the decoder task exists, so the statics are free.
void eq_begin() {
  const int8_t gains[cydeq::Bands] = {12, 12, 12, 12, 12};
  cydeq::Coeffs c;
  cydeq::design(gains, c);
  cydeq::State refState, asmState;
  refState.seed(c); asmState.seed(c);
  int16_t in[2 * EqChunkFrames], ref[2 * EqChunkFrames];
  uint32_t seed = 0x2545f491u, mismatches = 0, refCycles = 0, asmCycles = 0;
  for (int chunk = 0; chunk < 8; ++chunk) {
    for (auto& v : in) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; v = int16_t(seed); }
    if (chunk == 0) { in[0] = 32767; in[1] = -32768; }
    uint32_t at = esp_cpu_get_cycle_count();
    cydeq::process(c, refState, in, ref, EqChunkFrames, scratch, cydeq::stageRefStride2);
    refCycles += esp_cpu_get_cycle_count() - at;
    at = esp_cpu_get_cycle_count();
    cydeq::process(c, asmState, in, output, EqChunkFrames, scratch, cydeq_stage_xtensa);
    asmCycles += esp_cpu_get_cycle_count() - at;
    for (uint32_t i = 0; i < 2 * EqChunkFrames; ++i) mismatches += ref[i] != output[i];
  }
  testRefCycles = refCycles / (8 * EqChunkFrames);
  testAsmCycles = asmCycles / (8 * EqChunkFrames);
  kernelIsXtensa = mismatches == 0;
  kernel = kernelIsXtensa ? cydeq_stage_xtensa : cydeq::stageRefStride2;
  const uint32_t mhz = getCpuFrequencyMhz();
  // Percent of one core at 44.1 kHz, in tenths: cycles/frame * 44100 / (MHz * 1e6) * 1000.
  const uint32_t asmTenths = uint32_t(uint64_t(testAsmCycles) * 441 / (mhz * 100));
  const uint32_t refTenths = uint32_t(uint64_t(testRefCycles) * 441 / (mhz * 100));
  Serial.printf("eq: selftest %s mismatches %lu  5 bands xtensa %lu cyc/frame (%lu.%lu%% of %lu MHz core)  c++ %lu cyc/frame (%lu.%lu%%)  kernel %s\n",
                mismatches ? "FAIL" : "PASS", (unsigned long)mismatches, (unsigned long)testAsmCycles,
                (unsigned long)(asmTenths / 10), (unsigned long)(asmTenths % 10), (unsigned long)mhz,
                (unsigned long)testRefCycles, (unsigned long)(refTenths / 10), (unsigned long)(refTenths % 10),
                kernelIsXtensa ? "xtensa" : "c++");
  state.reset();
}

void eq_apply(bool on, const int8_t gains[cydeq::Bands]) {
  int8_t g[cydeq::Bands] = {};
  if (on) for (int b = 0; b < cydeq::Bands; ++b) g[b] = int8_t(cydeq::clampGain(gains[b]));
  const int64_t at = esp_timer_get_time();
  cydeq::Coeffs c;
  const bool ok = cydeq::design(g, c);
  const uint32_t us = uint32_t(esp_timer_get_time() - at);
  portENTER_CRITICAL(&eqLock);
  published = c;
  publishedOn = on;
  publishedSerial = publishedSerial + 1;
  portEXIT_CRITICAL(&eqLock);
  // peak: the slider curve's maximum; level: net overall gain (half-loudness match, <= 0).
  const int peak = c.peakCentiDb, level = c.preGainCentiDb;
  Serial.printf("eq: %s gains %d %d %d %d %d  stages %u peak %c%d.%02d dB level %c%d.%02d dB  design %lu us%s\n",
                on ? "on" : "off", g[0], g[1], g[2], g[3], g[4], unsigned(c.count),
                peak < 0 ? '-' : '+', abs(peak) / 100, abs(peak) % 100, level < 0 ? '-' : '+', abs(level) / 100, abs(level) % 100,
                (unsigned long)us, ok ? "" : "  REJECTED (bypass)");
}

EqStats eq_stats() {
  EqStats s;
  portENTER_CRITICAL(&eqLock);
  s.on = publishedOn; s.bands = published.count; s.preGainCentiDb = published.preGainCentiDb;
  portEXIT_CRITICAL(&eqLock);
  s.frames = statFrames; s.cycles = statCycles; s.limited = state.limited;  // single-word reads
  s.xtensa = kernelIsXtensa; s.selftestAsmCycles = testAsmCycles; s.selftestRefCycles = testRefCycles;
  return s;
}

#endif // CYD_UPLOADER
