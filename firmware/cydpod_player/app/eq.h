// ===========================================================================
// eq.h -- 5-band graphic equalizer on the decoder task (docs/specs/eq-5band.md)
// ===========================================================================
// The EQ runs inside audio.cpp's producer, before PCM enters the ring, so
// Bluetooth, wired and speaker output all hear it and the Bluetooth task's
// audio_pull() is unchanged. Design (double precision) happens on the caller of
// eq_apply(); the decoder only copies published coefficients at a chunk edge.
#pragma once

#include <stdint.h>
#include "eq_core.h"

// Boot: self-test the Xtensa loop against the C++ reference and measure both.
// Falls back to the reference loop (and says so) if they ever differ.
void eq_begin();
// Loop task. Off, or every band at 0 dB, is bypass. Gains are clamped to +-12 dB.
void eq_apply(bool on, const int8_t gains[cydeq::Bands]);

// Decoder task only.
bool eq_bypass();                       // true: copy PCM untouched (no EQ cost)
void eq_flush();                        // ring flushed: drop filter history
// Interleaved stereo, n <= EqChunkFrames, in -> out (out may alias the scratch).
static constexpr uint32_t EqChunkFrames = 32;
const int16_t* eq_process(const int16_t* in, uint32_t n);

struct EqStats {
  bool on = false; uint8_t bands = 0; int16_t preGainCentiDb = 0;
  uint32_t frames = 0, cycles = 0;      // monotonic, wrap; difference two samples
  uint32_t limited = 0;                 // monotonic: frames the output limiter reduced
  bool xtensa = false; uint32_t selftestAsmCycles = 0, selftestRefCycles = 0; // per frame, 5 bands
};
EqStats eq_stats();
