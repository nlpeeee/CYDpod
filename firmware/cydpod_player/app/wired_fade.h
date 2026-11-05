// ===========================================================================
// wired_fade.h -- pop-free gain stage for the wired I2S route (CY-71 / W2)
// ===========================================================================
// Pure C++: no Arduino, no IDF, no allocation. The writer task (wired.cpp)
// runs every 256-frame chunk through process(); tests/wired_fade_test.cpp
// drives it on the host.
//
// WHY THIS EXISTS. audio_pull() hard-zeroes the output the moment playback
// pauses or a seek starts, and requestFlush() empties the ring on a track
// change. Into Bluetooth's codec that is harmless; into a DAC and a headphone
// amplifier a step from the last sample to zero is a click. The data to fade
// is already gone by the time the writer sees the pause, so the fade-out
// cannot be a gain on the signal. It is a gain on the LAST SAMPLE: the stage
// keeps the last real stereo frame ("hold") and, whenever the input is not
// live, plays hold * gain while the gain walks down a raised-cosine curve.
// The output is continuous with the last real sample and reaches zero in
// kRampFrames, with no look-ahead delay and no extra buffering.
//
// GAIN. One raised-cosine ramp position `pos` in [0, kRampFrames] moves one
// step per frame, up while the stage is open, down otherwise. Reversing
// mid-ramp (pause then resume within 7 ms) continues from the current
// position, so the gain is continuous and monotone on each leg.
//
// WHEN THE INPUT MAY CHANGE UNDER THE HOLD. Pause and ring starvation are
// continuous: the ring is not consumed, so the next live sample follows the
// held one and the stage can switch back at any gain. Seeks, track changes,
// the mute flag and leaving the wired route are NOT continuous. Those set
// `drain`: the output decays from the hold, the live input is ignored, and
// the stage re-opens only after the gain has reached exactly zero. That is
// what keeps the sample-to-sample step bounded (tests: wired_fade_test.cpp).
//
// CEILING. The wired module's headphone amplifier runs from 3.3 V on this
// board and clips near full scale, so the stage scales everything by
// the selected digital ceiling AFTER user volume (applied inside audio_pull).
// On is -30 dBFS, Off retains -6 dBFS analogue headroom. This is a gain
// limit, not a peak compressor or a calibrated listening-level guarantee.
// It is folded into the same multiply as the ramp.
//
// DITHER. Optional TPDF (+-1 LSB) before the final rounding, off by default.
// It is skipped where the result is silence so that digital zero stays
// digital zero (the PCM5100A auto-mutes on zero data).
#pragma once

#include <math.h>
#include <stdint.h>

namespace cydwired {

constexpr int32_t kSampleRate = 44100;
// 7.26 ms at 44.1 kHz. The spec window is 5..10 ms (221..441 frames).
constexpr int32_t kRampFrames = 320;
static_assert(kRampFrames * 1000 >= 5 * kSampleRate && kRampFrames * 1000 <= 10 * kSampleRate,
              "ramp must be 5..10 ms");
// -6.0 dBFS peak into the DAC: round(32768 * 10^(-6/20)) = 16423.
constexpr int32_t kCeilingQ15 = 16423;
// round(32768 * 10^(-30/20)). The 15387-Q15 gap slews in 348.91 ms.
constexpr int32_t kLimitedCeilingQ15 = 1036;
constexpr int32_t limitCeilingQ15(bool on) { return on ? kLimitedCeilingQ15 : kCeilingQ15; }
constexpr int32_t kUnityQ15 = 32768;

struct Inputs {
  bool playing = false;       // audio_isPlaying()
  bool seeking = false;       // audio_isSeeking()
  bool muted = false;         // wired_setMuted()
  bool wired = true;          // the route is OUT_WIRED; false while leaving it
  uint32_t trackSerial = 0;   // audio_trackSerial()
};

class Fade {
 public:
  explicit Fade(int32_t ceilingQ15 = kCeilingQ15, bool dither = false)
      : ceiling_(ceilingQ15), targetCeiling_(ceilingQ15), dither_(dither) {
    for (int32_t p = 0; p <= kRampFrames; ++p) {
      const float c = 0.5f * (1.0f - cosf(3.14159265358979f * (float)p / (float)kRampFrames));
      int32_t q = (int32_t)(c * (float)kUnityQ15 + 0.5f);
      table_[p] = (uint16_t)(q > kUnityQ15 ? kUnityQ15 : q);
    }
    table_[0] = 0;
    table_[kRampFrames] = (uint16_t)kUnityQ15;
    reset();
  }

  void reset() {
    pos_ = 0;
    drain_ = false;
    seen_ = false;
    serial_ = 0;
    holdL_ = holdR_ = 0;
    rng_ = 0x2545F491u;
  }

  // Writer only. A new stream starts at the chosen ceiling, never slews from Off.
  void resetLimit(bool on) {
    reset();
    ceiling_ = targetCeiling_ = limitCeilingQ15(on);
  }
  // Latest request replaces the target; reversals continue from the current gain.
  void requestLimit(bool on) { targetCeiling_ = limitCeilingQ15(on); }
  int32_t ceilingQ15() const { return ceiling_; }

  void setDither(bool on) { dither_ = on; }

  // True once the output is digital silence and will stay so until live audio
  // arrives (the gain is zero).
  bool silent() const { return pos_ == 0; }
  int32_t rampPos() const { return pos_; }
  // Ramp gain alone, Q15 (0..32768), before the ceiling.
  int32_t rampGainQ15() const { return table_[pos_]; }
  bool draining() const { return drain_; }

  // `st` is interleaved stereo, `frames` frames, processed in place. The
  // first `valid` frames hold live audio from audio_pull(); everything after
  // them is ignored (treated as "no data", whatever it contains) and replaced
  // by the held sample times the gain.
  void process(int16_t* st, int32_t frames, int32_t valid, const Inputs& in) {
    if (!seen_) {
      serial_ = in.trackSerial;
      seen_ = true;
    }
    const bool level = in.seeking || in.muted || !in.wired;
    if (in.trackSerial != serial_) {
      serial_ = in.trackSerial;
      drain_ = true;
    }
    if (level) drain_ = true;
    if (valid < 0) valid = 0;
    if (valid > frames) valid = frames;
    const bool live = in.playing && !level;

    for (int32_t i = 0; i < frames; ++i) {
      if (drain_ && pos_ == 0 && !level) drain_ = false;
      const bool open = live && !drain_ && i < valid;
      if (open) {
        holdL_ = st[i * 2];
        holdR_ = st[i * 2 + 1];
        if (pos_ < kRampFrames) ++pos_;
      } else if (pos_ > 0) {
        --pos_;
      }
      // One Q15 unit/frame, common to L/R, including held-sample drains.
      // Downward changes reach the lower bound only after settling. No extra
      // sample multiply: this updates the ceiling in the existing ramp product.
      if (ceiling_ < targetCeiling_) ++ceiling_;
      else if (ceiling_ > targetCeiling_) --ceiling_;
      const int32_t g = (int32_t)(((int64_t)table_[pos_] * ceiling_ + (1 << 14)) >> 15);
      st[i * 2] = scale(holdL_, g);
      st[i * 2 + 1] = scale(holdR_, g);
    }
  }

 private:
  int16_t scale(int32_t x, int32_t g) {
    if (x == 0 || g == 0) return 0;
    int32_t v = x * g + (1 << 14);      // |x| <= 32768, g <= 32768 * ceiling/32768
    if (dither_) v += tpdf();
    v >>= 15;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
  }

  // Two uniform 15-bit draws summed: triangular, +-1 output LSB in Q15.
  int32_t tpdf() {
    const int32_t a = (int32_t)(next() >> 17);   // 0..32767
    const int32_t b = (int32_t)(next() >> 17);
    return a + b - 32767;
  }
  uint32_t next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

  uint16_t table_[kRampFrames + 1];
  int32_t ceiling_;
  int32_t targetCeiling_;
  bool dither_;
  int32_t pos_ = 0;
  bool drain_ = false;
  bool seen_ = false;
  uint32_t serial_ = 0;
  int32_t holdL_ = 0, holdR_ = 0;
  uint32_t rng_ = 0x2545F491u;
};

}  // namespace cydwired
