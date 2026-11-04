// CY-70: pure test-tone generator for the I2S test sketch. No Arduino or IDF
// includes so tests/i2s_tone_test.cpp can run it on the host.
//
// Output: interleaved stereo int16, L = 440 Hz, R = 880 Hz, -20 dBFS peak,
// gated 1 s on / 1 s off with 5 ms raised-cosine fades at both gate edges,
// preceded by 3 s of silence after start so the amplifier settles.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace i2stone {

constexpr uint32_t kRate = 44100;
constexpr uint32_t kFreqLeft = 440;
constexpr uint32_t kFreqRight = 880;
constexpr int32_t kAmpQ15 = 3277;               // round(0.1 * 32768): -20 dBFS peak
constexpr uint32_t kBootSilenceFrames = 3 * kRate;
constexpr uint32_t kOnFrames = kRate;           // 1 s tone
constexpr uint32_t kOffFrames = kRate;          // 1 s silence
constexpr uint32_t kFadeFrames = kRate / 200;   // 5 ms = 220 frames

constexpr int kSineBits = 10;
constexpr int kSineSize = 1 << kSineBits;       // full-wave table, linear interpolation

struct Tables {
  int16_t sine[kSineSize + 1];                  // Q15, one guard entry for interpolation
  int16_t fade[kFadeFrames];                    // Q15 raised cosine, 0 -> just below 1
  Tables() {
    const double pi = 3.14159265358979323846;
    for (int i = 0; i <= kSineSize; ++i) {
      sine[i] = (int16_t)lround(32767.0 * sin(2.0 * pi * i / kSineSize));
    }
    for (uint32_t i = 0; i < kFadeFrames; ++i) {
      fade[i] = (int16_t)lround(32767.0 * 0.5 * (1.0 - cos(pi * i / kFadeFrames)));
    }
  }
};

inline const Tables& tables() {
  static const Tables t;
  return t;
}

// Phase increment of a 32-bit accumulator for freq at the given rate.
constexpr uint32_t phaseStep(uint32_t freq, uint32_t rate) {
  return (uint32_t)(((uint64_t)freq << 32) / rate);
}

// Sine of a 32-bit phase, Q15, linear interpolation between table entries.
inline int32_t sineQ15(uint32_t phase) {
  const Tables& t = tables();
  const uint32_t idx = phase >> (32 - kSineBits);
  const int32_t frac = (int32_t)((phase >> (32 - kSineBits - 16)) & 0xFFFF);
  const int32_t a = t.sine[idx];
  const int32_t b = t.sine[idx + 1];
  return a + (((b - a) * frac) >> 16);
}

// Gate gain in Q15 for a frame position inside the tone-on window [0, on).
// Rises over the first fade frames, falls over the last fade frames.
inline int32_t gateGainQ15(uint32_t pos, uint32_t on, uint32_t fade) {
  if (pos >= on) return 0;
  const Tables& t = tables();
  if (pos < fade) return t.fade[pos];
  const uint32_t tail = on - 1 - pos;           // 0 on the last tone frame
  if (tail < fade) return t.fade[tail];
  return 32767;
}

struct Params {
  uint32_t bootSilence = kBootSilenceFrames;
  uint32_t onFrames = kOnFrames;
  uint32_t offFrames = kOffFrames;              // 0 = continuous tone (tests)
  int32_t ampQ15 = kAmpQ15;
  uint32_t rate = kRate;
};

class Generator {
 public:
  explicit Generator(const Params& p = Params())
      : p_(p),
        stepL_(phaseStep(kFreqLeft, p.rate)),
        stepR_(phaseStep(kFreqRight, p.rate)) {}

  // Writes `frames` interleaved L,R int16 frames. Any chunk size gives the
  // same overall stream (phase and gate position are carried across calls).
  void fill(int16_t* out, size_t frames) {
    const uint32_t fade = kFadeFrames;
    const uint32_t period = p_.onFrames + p_.offFrames;
    for (size_t i = 0; i < frames; ++i) {
      int32_t l = 0, r = 0;
      if (boot_ < p_.bootSilence) {
        ++boot_;
      } else {
        const int32_t g = (period == p_.onFrames) ? 32767 : gateGainQ15(pos_, p_.onFrames, fade);
        if (g != 0) {
          l = (sineQ15(phaseL_) * p_.ampQ15) >> 15;
          r = (sineQ15(phaseR_) * p_.ampQ15) >> 15;
          l = (l * g) >> 15;
          r = (r * g) >> 15;
        }
        phaseL_ += stepL_;
        phaseR_ += stepR_;
        if (++pos_ >= period) pos_ = 0;
      }
      out[2 * i] = (int16_t)l;
      out[2 * i + 1] = (int16_t)r;
    }
  }

 private:
  Params p_;
  uint32_t stepL_, stepR_;
  uint32_t phaseL_ = 0, phaseR_ = 0;
  uint32_t boot_ = 0;
  uint32_t pos_ = 0;
};

}  // namespace i2stone
