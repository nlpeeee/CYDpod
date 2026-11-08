// ===========================================================================
// eq_core.h -- 5-band graphic equalizer: design and fixed-point reference
// ===========================================================================
// Pure C++: no Arduino, no IDF, no allocation. eq.cpp runs it on the decoder
// task; tests/eq_core_test.cpp drives it on the host. Spec: docs/specs/eq-5band.md.
//
// FILTERS. Five RBJ peaking biquads, Direct Form I, one per band, cascaded in
// ascending frequency. A band at 0 dB is not run at all; with every band at
// 0 dB (or the EQ switched off) the caller does not call process(), so bypass
// is bit-exact and costs nothing.
//
// NUMBER FORMATS (the bit-exact contract shared with the Xtensa loop in eq.cpp)
//   coefficients  Q2.30 int32, {b0, b1, b2, -a1, -a2}, normalised by a0
//   samples       int32, int16 full scale = 2^28 (x16 << 13), so the cascade
//                 has 12 dB of internal headroom up to the stage clamp
//   product       mulsh(c, s) = (int64(c) * s) >> 32, floor; one MULSH on Xtensa
//   accumulator   int32, 2^26 per full scale: sum of five products + 2
//                 (the +2 offsets the floor bias of five truncated products)
//   stage clamp   acc to [-(2^28 - 1), 2^28 - 1] (+-4 x full scale), then
//                 y = (acc << 2) + 2. The +2 (half an accumulator step) returns
//                 through -a1-a2 ~ 1 and cancels the rest of the floor bias, which
//                 the low bands' DC gain 1/(1+a1+a2) (~13,700 at 60 Hz) would
//                 otherwise turn into several LSB of offset.
//   output        (y + 2^12) >> 13, saturated to int16
// The accumulator cannot wrap: stage outputs are clamped to |y| < 2^30, and
// design() rejects any stage whose |b0|+|b1|+|b2|+|a1|+|a2| reaches 8, so
// |acc| < 8 * 2^30 * 2^30 / 2^32 = 2^31.
//
// HEADROOM. design() evaluates the whole cascade's magnitude response on a
// log grid plus the band centres and folds 1 / max|H| into the first active
// stage's b coefficients, so a full-scale sine at any frequency leaves the
// cascade at or below full scale with all bands boosted. Wideband peaks can
// still exceed it briefly; the stage clamp and the int16 saturation bound
// them instead of letting them wrap.
#pragma once

#include <stdint.h>
#include <math.h>

namespace cydeq {

constexpr int Bands = 5;
constexpr int MinDb = -12, MaxDb = 12, StepDb = 2;
constexpr double SampleRate = 44100.0;
constexpr double Pi = 3.14159265358979323846;  // not M_PI: MSVC host builds include this
constexpr double Q = 0.9;
constexpr double CentreHz[Bands] = {60.0, 250.0, 1000.0, 4000.0, 14000.0};
// Extra pre-gain margin below 1 / max|H|, for wideband overshoot (spec).
constexpr double MarginDb = 1.0;

constexpr int CoefShift = 30;           // Q2.30
constexpr int InShift = 13;             // int16 -> int32 sample
constexpr int32_t AccLimit = (1 << 28) - 1;
constexpr int32_t AccBias = 2;
constexpr int32_t OutBias = 2;

struct Stage { int32_t b0, b1, b2, na1, na2; };  // layout read by the Xtensa loop
static_assert(sizeof(Stage) == 20, "Stage layout");
struct History { int32_t x1, x2, y1, y2; };       // layout read by the Xtensa loop
static_assert(sizeof(History) == 16, "History layout");

struct Coeffs {
  Stage stage[Bands] = {};
  uint8_t band[Bands] = {};  // band index of each active stage, ascending
  uint8_t count = 0;         // active stages; 0 = bypass
  int16_t preGainCentiDb = 0; // applied pre-gain, for logs (<= 0)
  int16_t peakCentiDb = 0;    // cascade max|H| before pre-gain, for logs
};

inline int clampGain(int db) { return db < MinDb ? MinDb : db > MaxDb ? MaxDb : db; }

struct DesignBiquad { double b0, b1, b2, a1, a2; };

// RBJ cookbook peaking EQ, normalised by a0.
inline DesignBiquad peaking(double hz, double db) {
  const double A = pow(10.0, db / 40.0), w0 = 2.0 * Pi * hz / SampleRate;
  const double alpha = sin(w0) / (2.0 * Q), c = cos(w0), a0 = 1.0 + alpha / A;
  return {(1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha / A) / a0};
}

// |H(e^jw)| of one biquad.
inline double magnitude(const DesignBiquad& f, double w) {
  const double c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
  const double nr = f.b0 + f.b1 * c1 + f.b2 * c2, ni = -(f.b1 * s1 + f.b2 * s2);
  const double dr = 1.0 + f.a1 * c1 + f.a2 * c2, di = -(f.a1 * s1 + f.a2 * s2);
  return sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

inline bool quantise(double v, int32_t& out) {
  const double q = floor(v * double(1 << CoefShift) + 0.5);
  if (!(q >= -2147483648.0 && q <= 2147483647.0)) return false;
  out = int32_t(q);
  return true;
}

// Builds the active cascade for `gains` (dB, clamped). False only if a stage
// would break the no-wrap bound; `out` is then bypass.
inline bool design(const int8_t gains[Bands], Coeffs& out) {
  out = Coeffs{};
  DesignBiquad f[Bands];
  int n = 0;
  for (int b = 0; b < Bands; ++b) {
    const int db = clampGain(gains[b]);
    if (db == 0) continue;
    f[n] = peaking(CentreHz[b], db);
    out.band[n++] = uint8_t(b);
  }
  if (!n) return true;
  // Peak of the cascade: 160 log-spaced points 20 Hz..20 kHz plus the centres.
  double peak = 0.0;
  for (int i = 0; i < 160 + Bands; ++i) {
    const double hz = i < 160 ? 20.0 * pow(1000.0, i / 159.0) : CentreHz[i - 160];
    const double w = 2.0 * Pi * hz / SampleRate;
    double m = 1.0;
    for (int k = 0; k < n; ++k) m *= magnitude(f[k], w);
    if (m > peak) peak = m;
  }
  double g = pow(10.0, -MarginDb / 20.0) / peak;
  if (g > 1.0) g = 1.0;
  f[0].b0 *= g; f[0].b1 *= g; f[0].b2 *= g;
  for (int k = 0; k < n; ++k) {
    const DesignBiquad& d = f[k];
    if (fabs(d.b0) + fabs(d.b1) + fabs(d.b2) + fabs(d.a1) + fabs(d.a2) >= 7.999) { out = Coeffs{}; return false; }
    Stage& s = out.stage[k];
    if (!quantise(d.b0, s.b0) || !quantise(d.b1, s.b1) || !quantise(d.b2, s.b2) ||
        !quantise(-d.a1, s.na1) || !quantise(-d.a2, s.na2)) { out = Coeffs{}; return false; }
  }
  out.count = uint8_t(n);
  out.preGainCentiDb = int16_t(floor(2000.0 * log10(g) + 0.5));
  out.peakCentiDb = int16_t(floor(2000.0 * log10(peak) + 0.5));
  return true;
}

inline int32_t mulsh(int32_t a, int32_t b) { return int32_t((int64_t(a) * b) >> 32); }

// One stage over n samples spaced `stride` int32 apart, in place. This is the
// reference for the Xtensa loop: same operations, same order of rounding.
inline void stageRef(int32_t* p, uint32_t n, uint32_t stride, const Stage& c, History& h) {
  int32_t x1 = h.x1, x2 = h.x2, y1 = h.y1, y2 = h.y2;
  for (uint32_t i = 0; i < n; ++i, p += stride) {
    const int32_t x0 = *p;
    uint32_t acc = uint32_t(mulsh(c.b0, x0)) + uint32_t(mulsh(c.b1, x1)) + uint32_t(mulsh(c.b2, x2)) +
                   uint32_t(mulsh(c.na1, y1)) + uint32_t(mulsh(c.na2, y2)) + uint32_t(AccBias);
    int32_t a = int32_t(acc);
    a = a > AccLimit ? AccLimit : a < -AccLimit ? -AccLimit : a;
    const int32_t y = int32_t((uint32_t(a) << 2) + uint32_t(OutBias));
    x2 = x1; x1 = x0; y2 = y1; y1 = y;
    *p = y;
  }
  h.x1 = x1; h.x2 = x2; h.y1 = y1; h.y2 = y2;
}

inline int16_t toPcm(int32_t y) {
  const int32_t v = (y + (1 << (InShift - 1))) >> InShift;
  return int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

// Per-band, per-channel history. A band that becomes active starts as an
// identity filter fed by its input history (no step); see seed().
struct State {
  History h[2][Bands] = {};
  uint8_t active[Bands] = {};  // per band: running in the current cascade
  void reset() { *this = State{}; }
  // Called when `next` replaces the running cascade. Bands that keep running
  // keep their history; a newly active band gets x = y = its input history,
  // which is exactly the state of the identity filter it replaces.
  void seed(const Coeffs& next) {
    uint8_t now[Bands] = {};
    for (int k = 0; k < next.count; ++k) now[next.band[k]] = 1;
    for (int ch = 0; ch < 2; ++ch) {
      for (int b = 0; b < Bands; ++b) {
        if (!now[b] || active[b]) continue;
        // Input of band b: output of the nearest running band below it, else the
        // raw input, which the lowest running band holds as its x history.
        int below = -1, lowest = -1;
        for (int k = 0; k < Bands; ++k) if (active[k]) { if (k < b) below = k; if (lowest < 0) lowest = k; }
        History& d = h[ch][b];
        if (below >= 0) { d.x1 = d.y1 = h[ch][below].y1; d.x2 = d.y2 = h[ch][below].y2; }
        else if (lowest >= 0) { d.x1 = d.y1 = h[ch][lowest].x1; d.x2 = d.y2 = h[ch][lowest].x2; }
        else d = History{};
      }
    }
    for (int b = 0; b < Bands; ++b) active[b] = now[b];
  }
};

// Stage kernel signature shared by the reference and the Xtensa loop.
using StageFn = void (*)(int32_t* p, uint32_t n, const Stage* c, History* h);
inline void stageRefStride2(int32_t* p, uint32_t n, const Stage* c, History* h) { stageRef(p, n, 2, *c, *h); }

// Interleaved stereo in -> out (may alias), n frames, through `buf` (2n int32).
inline void process(const Coeffs& c, State& s, const int16_t* in, int16_t* out, uint32_t n, int32_t* buf,
                    StageFn stage = stageRefStride2) {
  for (uint32_t i = 0; i < 2 * n; ++i) buf[i] = int32_t(uint32_t(int32_t(in[i])) << InShift);
  for (int ch = 0; ch < 2; ++ch)
    for (int k = 0; k < c.count; ++k) stage(buf + ch, n, &c.stage[k], &s.h[ch][c.band[k]]);
  for (uint32_t i = 0; i < 2 * n; ++i) out[i] = toPcm(buf[i]);
}

}  // namespace cydeq
