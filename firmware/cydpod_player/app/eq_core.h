// ===========================================================================
// eq_core.h -- 5-band graphic equalizer: design and fixed-point reference
// ===========================================================================
// Pure C++: no Arduino, no IDF, no allocation. eq.cpp runs it on the decoder
// task; tests/eq_core_test.cpp drives it on the host. Spec: docs/specs/eq-5band.md.
//
// FILTERS. Five RBJ peaking biquads (Q 0.5), Direct Form I, cascaded in
// ascending frequency. The slider gains are targets, not filter gains: with
// two octaves between centres, neighbouring bands overlap, so design() solves
// for internal gains whose combined response hits each target at the centres
// and the average of neighbours halfway between (interaction-matrix least
// squares, as in Valimaki & Liski's accurate cascade graphic EQ). A band whose
// internal gain is ~0 dB is not run; with every slider at 0 dB (or the EQ off)
// the caller does not call process(), so bypass is bit-exact and free.
//
// NUMBER FORMATS (the bit-exact contract shared with the Xtensa loop in eq_xtensa.h)
//   coefficients  Q3.29 int32, {b0, b1, b2, -a1, -a2}, normalised by a0
//   samples       int32, int16 full scale = 2^28 (x16 << 13), so the cascade
//                 has 12 dB of internal headroom up to the stage clamp
//   product       mulsh(c, s) = (int64(c) * s) >> 32, floor; one MULSH on Xtensa
//   accumulator   int32, 2^25 per full scale: sum of five products + 2
//                 (the +2 offsets most of the floor bias of five truncated products)
//   stage clamp   acc to [-(2^27 - 1), 2^27 - 1] (+-4 x full scale), then
//                 y = (acc << 3) + 4. The +4 (half an accumulator step) returns
//                 through -a1-a2 ~ 1 and cancels the rest of the floor bias, which
//                 the low band's DC gain 1/(1+a1+a2) would otherwise turn into
//                 several LSB of offset.
// The accumulator cannot wrap: stage outputs are clamped to |y| <= 2^30, and
// design() rejects any stage whose |b0|+|b1|+|b2|+|a1|+|a2| reaches 16, so
// |acc| < 16 * 2^29 * 2^30 / 2^32 = 2^31.
//
// GAIN STAGING. Three parts, so that switching the EQ on keeps the perceived
// level and nothing ever wraps or hard-clips silently:
//   scale    min(1, 1/max|H|) folded into the first stage: no stage can exceed
//            full scale on a sine, whatever the sliders.
//   outGain  after the cascade: 10^(pre/20) / scale, where pre is minus half
//            the pink-weighted average power boost from 100 Hz to 10 kHz
//            (<= 0). Bass then plays bass ~4.5 dB louder and the mids about as
//            before, instead of everything 9 dB quieter; the overall level rises
//            by half the average boost.
//   limiter  peak limiter on the outGain stage, linked across channels:
//            instant attack, ~150 ms release, threshold -1 dBFS. outGain itself
//            glides (~10 ms time constant) whenever the design changes.
#pragma once

#include <stdint.h>
#include <math.h>

namespace cydeq {

constexpr int Bands = 5;
constexpr int MinDb = -12, MaxDb = 12, StepDb = 2;
constexpr double SampleRate = 44100.0;
constexpr double Pi = 3.14159265358979323846;  // not M_PI: MSVC host builds include this
constexpr double Q = 0.5;
constexpr double CentreHz[Bands] = {60.0, 250.0, 1000.0, 4000.0, 14000.0};
constexpr double InternalMaxDb = 18.0;  // clamp on the corrected filter gains
constexpr double ActiveDb = 0.05;       // internal gains below this are not run
// Share of the average boost taken back before the output: 0.5 keeps a preset's
// boost audible as a boost while the overall level rises by only half as much.
constexpr double LoudnessMatch = 0.5;

constexpr int CoefShift = 29;           // Q3.29
constexpr int InShift = 13;             // int16 -> int32 sample
constexpr int32_t AccLimit = (1 << 27) - 1;
constexpr int AccShift = 3;             // y = acc << 3
constexpr int32_t AccBias = 2;
constexpr int32_t OutBias = 4;
constexpr float FullScale = float(1 << 28);
constexpr float LimitThreshold = 0.8912509f * FullScale;  // -1 dBFS
constexpr float GainGlide = 0.0023f;    // per frame: ~10 ms time constant
constexpr float LimitRelease = 0.00015f; // per frame: ~150 ms time constant
constexpr int OutGainShift = 24;        // per-frame output gain in Q7.24

struct Stage { int32_t b0, b1, b2, na1, na2; };  // layout read by the Xtensa loop
static_assert(sizeof(Stage) == 20, "Stage layout");
struct History { int32_t x1, x2, y1, y2; };       // layout read by the Xtensa loop
static_assert(sizeof(History) == 16, "History layout");

struct Coeffs {
  Stage stage[Bands] = {};
  uint8_t band[Bands] = {};  // band index of each active stage, ascending
  uint8_t count = 0;         // active stages; 0 = bypass
  float scale = 1.0f;        // folded into the first stage (<= 1)
  float outGain = 1.0f;      // target gain after the cascade
  int16_t preGainCentiDb = 0; // loudness pre-gain, for logs (<= 0)
  int16_t peakCentiDb = 0;    // cascade max|H| at the slider targets, for logs
};

inline int clampGain(int db) { return db < MinDb ? MinDb : db > MaxDb ? MaxDb : db; }

// Presets are only gain sets: the shown name is derived, nothing extra is stored.
// Aimed at the range a small speaker reproduces (bass at 60-250 Hz together).
struct Preset { const char* name; int8_t gain[Bands]; };
constexpr Preset Presets[] = {
  {"Flat", {0, 0, 0, 0, 0}},
  {"Bass", {6, 6, 2, 0, 0}},
  {"Vocal", {-2, 0, 4, 4, 2}},
  {"Treble", {0, 0, 2, 6, 6}},
  {"Loudness", {6, 4, 0, 2, 6}},
};
constexpr int PresetCount = int(sizeof(Presets) / sizeof(Presets[0]));
// Index of the preset equal to `g`, or -1 (Custom).
inline int presetMatch(const int8_t g[Bands]) {
  for (int p = 0; p < PresetCount; ++p) {
    bool same = true;
    for (int b = 0; b < Bands; ++b) same = same && Presets[p].gain[b] == g[b];
    if (same) return p;
  }
  return -1;
}
// The preset after the current one; Custom goes to the first non-flat preset.
inline int presetNext(const int8_t g[Bands]) {
  const int p = presetMatch(g);
  return p < 0 ? 1 : (p + 1) % PresetCount;
}

struct DesignBiquad { double b0, b1, b2, a1, a2; };

// RBJ cookbook peaking EQ, normalised by a0.
inline DesignBiquad peaking(double hz, double db) {
  const double A = pow(10.0, db / 40.0), w0 = 2.0 * Pi * hz / SampleRate;
  const double alpha = sin(w0) / (2.0 * Q), c = cos(w0), a0 = 1.0 + alpha / A;
  return {(1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha / A) / a0};
}

// |H|^2 of one biquad from cos(w) alone (real coefficients), so a frequency
// grid needs one cosine per point however many filters are evaluated.
inline double power(const DesignBiquad& f, double c) {
  const double c2 = 2.0 * c * c - 1.0;
  const double num = f.b0 * f.b0 + f.b1 * f.b1 + f.b2 * f.b2 + 2.0 * (f.b0 * f.b1 + f.b1 * f.b2) * c + 2.0 * f.b0 * f.b2 * c2;
  const double den = 1.0 + f.a1 * f.a1 + f.a2 * f.a2 + 2.0 * (f.a1 + f.a1 * f.a2) * c + 2.0 * f.a2 * c2;
  return num / den;
}
inline double cosAt(double hz) { return cos(2.0 * Pi * hz / SampleRate); }
inline double dbAt(const DesignBiquad& f, double c) { return 10.0 * log10(power(f, c)); }

// Solves the 5-unknown least-squares problem A x = t (rows x 5) by normal equations.
inline void leastSquares(const double A[][Bands], const double* t, int rows, double x[Bands]) {
  double M[Bands][Bands + 1] = {};
  for (int i = 0; i < Bands; ++i) {
    for (int j = 0; j < Bands; ++j) for (int k = 0; k < rows; ++k) M[i][j] += A[k][i] * A[k][j];
    for (int k = 0; k < rows; ++k) M[i][Bands] += A[k][i] * t[k];
  }
  for (int i = 0; i < Bands; ++i) {
    int p = i;
    for (int r = i + 1; r < Bands; ++r) if (fabs(M[r][i]) > fabs(M[p][i])) p = r;
    for (int j = 0; j <= Bands; ++j) { const double v = M[i][j]; M[i][j] = M[p][j]; M[p][j] = v; }
    for (int r = 0; r < Bands; ++r) {
      if (r == i) continue;
      const double f = M[r][i] / M[i][i];
      for (int j = i; j <= Bands; ++j) M[r][j] -= f * M[i][j];
    }
  }
  for (int i = 0; i < Bands; ++i) x[i] = M[i][Bands] / M[i][i];
}

// The double-precision design behind design(): active biquads (scale folded
// into the first), output gain, and the numbers logged and tested.
struct Design {
  DesignBiquad f[Bands] = {};
  uint8_t band[Bands] = {};
  int n = 0;
  double internalDb[Bands] = {};
  double scale = 1.0, outGain = 1.0, peak = 1.0, loudnessDb = 0.0;
};

inline void designBiquads(const int8_t gains[Bands], Design& d) {
  d = Design{};
  // Control points: the centres, then the geometric midpoints between them.
  constexpr int Points = 2 * Bands - 1;
  double c[Points], t[Points];
  bool any = false;
  for (int b = 0; b < Bands; ++b) { c[b] = cosAt(CentreHz[b]); t[b] = clampGain(gains[b]); any = any || t[b] != 0; }
  if (!any) return;
  for (int b = 0; b + 1 < Bands; ++b) {
    c[Bands + b] = cosAt(sqrt(CentreHz[b] * CentreHz[b + 1]));
    t[Bands + b] = 0.5 * (t[b] + t[b + 1]);
  }
  // Interaction matrix at a 12 dB reference, then two refinements with each
  // filter's actual response per dB at its current gain.
  double A[Points][Bands], x[Bands];
  for (int j = 0; j < Bands; ++j) {
    const DesignBiquad r = peaking(CentreHz[j], 12.0);
    for (int k = 0; k < Points; ++k) A[k][j] = dbAt(r, c[k]) / 12.0;
  }
  leastSquares(A, t, Points, x);
  for (int pass = 0; pass < 2; ++pass) {
    for (int j = 0; j < Bands; ++j) {
      if (fabs(x[j]) < 1e-3) continue;
      const DesignBiquad r = peaking(CentreHz[j], x[j]);
      for (int k = 0; k < Points; ++k) A[k][j] = dbAt(r, c[k]) / x[j];
    }
    leastSquares(A, t, Points, x);
  }
  for (int j = 0; j < Bands; ++j) {
    x[j] = x[j] > InternalMaxDb ? InternalMaxDb : x[j] < -InternalMaxDb ? -InternalMaxDb : x[j];
    d.internalDb[j] = x[j];
    if (fabs(x[j]) < ActiveDb) continue;
    d.f[d.n] = peaking(CentreHz[j], x[j]);
    d.band[d.n++] = uint8_t(j);
  }
  if (!d.n) return;
  // Peak: 96 log points 20 Hz..20 kHz plus the centres. Loudness: pink
  // (log-uniform) average of the power gain over 48 points 100 Hz..10 kHz.
  double peak = 0.0, sum = 0.0;
  for (int i = 0; i < 96 + Bands; ++i) {
    const double hz = i < 96 ? 20.0 * pow(1000.0, i / 95.0) : CentreHz[i - 96];
    const double cw = cosAt(hz);
    double p = 1.0;
    for (int k = 0; k < d.n; ++k) p *= power(d.f[k], cw);
    if (p > peak) peak = p;
  }
  for (int i = 0; i < 48; ++i) {
    const double cw = cosAt(100.0 * pow(100.0, i / 47.0));
    double p = 1.0;
    for (int k = 0; k < d.n; ++k) p *= power(d.f[k], cw);
    sum += p;
  }
  d.peak = sqrt(peak);
  d.loudnessDb = 10.0 * log10(sum / 48.0);
  d.scale = d.peak > 1.0 ? 1.0 / d.peak : 1.0;
  const double pre = d.loudnessDb > 0.0 ? -LoudnessMatch * d.loudnessDb : 0.0;
  d.outGain = pow(10.0, pre / 20.0) / d.scale;
  d.f[0].b0 *= d.scale; d.f[0].b1 *= d.scale; d.f[0].b2 *= d.scale;
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
  Design d;
  designBiquads(gains, d);
  if (!d.n) return true;
  for (int k = 0; k < d.n; ++k) {
    const DesignBiquad& f = d.f[k];
    if (fabs(f.b0) + fabs(f.b1) + fabs(f.b2) + fabs(f.a1) + fabs(f.a2) >= 15.99) { out = Coeffs{}; return false; }
    Stage& s = out.stage[k];
    if (!quantise(f.b0, s.b0) || !quantise(f.b1, s.b1) || !quantise(f.b2, s.b2) ||
        !quantise(-f.a1, s.na1) || !quantise(-f.a2, s.na2)) { out = Coeffs{}; return false; }
    out.band[k] = d.band[k];
  }
  out.count = uint8_t(d.n);
  out.scale = float(d.scale);
  out.outGain = float(d.outGain);
  out.preGainCentiDb = int16_t(floor(100.0 * 20.0 * log10(d.outGain * d.scale) + 0.5));
  out.peakCentiDb = int16_t(floor(2000.0 * log10(d.peak) + 0.5));
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
    const int32_t y = int32_t((uint32_t(a) << AccShift) + uint32_t(OutBias));
    x2 = x1; x1 = x0; y2 = y1; y1 = y;
    *p = y;
  }
  h.x1 = x1; h.x2 = x2; h.y1 = y1; h.y2 = y2;
}

inline int16_t toPcm(int64_t y) {
  const int64_t v = (y + (1 << (InShift - 1))) >> InShift;
  return int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

// Per-band, per-channel history plus the output stage's gain and limiter.
// A band that becomes active starts as an identity filter fed by its input
// history (no step); see seed().
struct State {
  History h[2][Bands] = {};
  uint8_t active[Bands] = {};  // per band: running in the current cascade
  float gain = 1.0f;           // output gain, gliding toward Coeffs::outGain
  float env = 1.0f;            // limiter gain, 1 = no reduction
  uint32_t limited = 0;        // frames the limiter reduced (monotonic)
  void reset() { *this = State{}; }
  // Called when `next` replaces `prev`. Keeps scale * gain continuous, so the
  // level glides from where it was instead of stepping when the scale moves.
  void retarget(const Coeffs& prev, const Coeffs& next) {
    if (!next.count) return;
    gain = prev.count ? gain * prev.scale / next.scale : 1.0f / next.scale;
    if (!prev.count) env = 1.0f;
  }
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

// Output stage: glide the gain, limit stereo-linked peaks to -1 dBFS, convert.
inline void outputStage(const Coeffs& c, State& s, const int32_t* buf, int16_t* out, uint32_t n, bool limit) {
  for (uint32_t i = 0; i < n; ++i) {
    const int32_t l = buf[2 * i], r = buf[2 * i + 1];
    s.gain += (c.outGain - s.gain) * GainGlide;
    const int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;  // |y| <= 2^30: no overflow
    const float peak = float(al > ar ? al : ar);
    s.env += (1.0f - s.env) * LimitRelease;  // release first, so the check below sees the gain used
    if (limit && peak * s.gain * s.env > LimitThreshold) { s.env = LimitThreshold / (peak * s.gain); ++s.limited; }
    // float -> int32 is one FPU instruction on the ESP32 (int64 would be a libcall).
    // gain <= 1/scale ~ 5 for any slider setting, so this stays far below 2^31.
    const float gf = s.gain * s.env * float(1 << OutGainShift) + 0.5f;
    const int32_t g = gf < 2.0e9f ? int32_t(gf) : 2000000000;
    out[2 * i] = toPcm((int64_t(l) * g) >> OutGainShift);
    out[2 * i + 1] = toPcm((int64_t(r) * g) >> OutGainShift);
  }
}

// Interleaved stereo in -> out, n frames, through `buf` (2n int32).
inline void process(const Coeffs& c, State& s, const int16_t* in, int16_t* out, uint32_t n, int32_t* buf,
                    StageFn stage = stageRefStride2, bool limit = true) {
  for (uint32_t i = 0; i < 2 * n; ++i) buf[i] = int32_t(uint32_t(int32_t(in[i])) << InShift);
  for (int ch = 0; ch < 2; ++ch)
    for (int k = 0; k < c.count; ++k) stage(buf + ch, n, &c.stage[k], &s.h[ch][c.band[k]]);
  outputStage(c, s, buf, out, n, limit);
}

}  // namespace cydeq
