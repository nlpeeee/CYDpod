// CY-75: pure scheduling, format and PCM generation; no Arduino/IDF dependency.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace i2sdiag {
constexpr uint32_t kPhaseMs = 12000, kZeroMs = 500, kStatusMs = 4000;
constexpr uint32_t kDrainMs = 40;  // > 6*256/44100 s of queued DMA
constexpr uint32_t kPeriodMs = kPhaseMs + kZeroMs + kDrainMs;
constexpr unsigned kPhaseCount = 7;
constexpr int32_t kAmplitude = 8231;  // -12 dBFS peak, round(32767 * 10^(-12/20))
struct Phase {
  uint32_t rate;
  unsigned slotBits;
  bool bitShift, audio, square;
  const char* description;
};
inline Phase phase(unsigned n) {
  switch (n) {
    case 0: return {44100, 16, true, false, false, "LED pin test; no I2S"};
    case 2: return {44100, 32, true, true, false, "Philips 44100 Hz slot32; 440 L / 880 R -12 dBFS"};
    case 3: return {48000, 16, true, true, false, "Philips 48000 Hz slot16; 440 L / 880 R -12 dBFS"};
    case 4: return {44100, 16, false, true, false, "left-justified 44100 Hz slot16; 440 L / 880 R -12 dBFS"};
    case 5: return {44100, 16, true, false, false, "SKIPPED MCLK: ESP32 supports GPIO0/1/3 only; GPIO22 invalid"};
    case 6: return {44100, 16, true, true, true, "Philips 44100 Hz slot16; 4 Hz FULL-SCALE square both ears"};
    default: return {44100, 16, true, true, false, "Philips 44100 Hz slot16; 440 L / 880 R -12 dBFS"};
  }
}
struct Position { unsigned phase; uint32_t ms; bool zeros; };
inline Position position(uint64_t elapsedMs) {
  const uint32_t ms = elapsedMs % kPeriodMs;
  return {unsigned((elapsedMs / kPeriodMs) % kPhaseCount), ms, ms >= kPhaseMs};
}
inline int ledPin(uint32_t ms) {
  return ms < 1000 ? 4 : ms < 2000 ? 16 : ms < 3000 ? 17 : -1;
}
// IDF sends unchanged signed 16-bit DMA words in both formats. Only the
// peripheral's bitShift changes the MSB's timing relative to the WS edge.
// Reference wire model: edge=0 is the WS transition, edge=16 is next WS.
inline unsigned wireBit(int16_t current, int16_t previous, unsigned edge, bool shift) {
  if (shift && edge == 0) return uint16_t(previous) & 1u;
  const unsigned bit = edge - unsigned(shift);
  return bit < 16 ? (uint16_t(current) >> (15 - bit)) & 1u : 0;
}
class Generator {
 public:
  explicit Generator(const Phase& p) : p_(p) {
    for (unsigned i = 0; i <= 256; ++i)
      sine_[i] = int16_t(lround(kAmplitude * sin(6.283185307179586 * i / 256)));
  }
  void fill(int16_t* out, size_t frames, bool zeros = false) {
    for (size_t i = 0; i < frames; ++i) {
      int16_t l = 0, r = 0;
      if (p_.audio && !zeros) {
        if (p_.square) l = r = (uint64_t(frame_) * 8 / p_.rate) % 2 ? INT16_MIN : INT16_MAX;
        else { l = sample(left_); r = sample(right_); }
      }
      out[2*i] = l; out[2*i+1] = r;
      left_ += uint32_t((uint64_t(440) << 32) / p_.rate);
      right_ += uint32_t((uint64_t(880) << 32) / p_.rate);
      if (++frame_ == p_.rate) frame_ = 0;
    }
  }
 private:
  int16_t sample(uint32_t ph) const {
    const unsigned index = ph >> 24;
    const int32_t frac = (ph >> 8) & 65535;
    return int16_t(sine_[index] + (int32_t(sine_[index+1] - sine_[index]) * frac) / 65536);
  }
  Phase p_;
  int16_t sine_[257];
  uint32_t left_ = 0, right_ = 0, frame_ = 0;
};
}  // namespace i2sdiag
