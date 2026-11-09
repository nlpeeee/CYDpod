// Host tests for eq_core.h (docs/specs/eq-5band.md): slider accuracy and ripple of
// the corrected design, loudness-matched gain staging, fixed-point response against
// double-precision biquads, the output limiter, no wrap at full scale, bit-exact
// bypass, presets and continuity when bands or designs change mid-stream.
#include "eq_core.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace cydeq;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// A fresh state as eq.cpp leaves it once the output gain has settled.
static State settled(const Coeffs& c) { State s; s.seed(c); s.gain = c.outGain; return s; }

// Runs the fixed-point EQ over interleaved stereo, in 32-frame chunks like eq.cpp.
static std::vector<int16_t> runEq(const Coeffs& c, const std::vector<int16_t>& in, State* keep = nullptr, bool limit = true) {
  State local = settled(c); State& s = keep ? *keep : local;
  std::vector<int16_t> out(in.size());
  int32_t buf[64];
  const uint32_t frames = uint32_t(in.size() / 2);
  for (uint32_t f = 0; f < frames; f += 32) {
    const uint32_t n = frames - f < 32 ? frames - f : 32;
    process(c, s, &in[2 * f], &out[2 * f], n, buf, stageRefStride2, limit);
  }
  return out;
}

// Double-precision reference: the same Design (scale in the first stage), times outGain.
struct RefCascade {
  Design d; double z[2][Bands][4] = {};
  explicit RefCascade(const int8_t gains[Bands]) { designBiquads(gains, d); }
  // |H| of the cascade alone (slider response, no gain staging).
  double sliderDb(double hz) const {
    double p = 1; const double c = cosAt(hz);
    for (int k = 0; k < d.n; ++k) p *= power(d.f[k], c);
    return 10 * log10(p) - 20 * log10(d.scale);
  }
  // What the listener gets: cascade with scale, times outGain.
  double netDb(double hz) const { return sliderDb(hz) + 20 * log10(d.scale * d.outGain); }
  double step(int ch, double x) {
    for (int k = 0; k < d.n; ++k) {
      double* s = z[ch][k]; const DesignBiquad& f = d.f[k];
      const double y = f.b0 * x + f.b1 * s[0] + f.b2 * s[1] - f.a1 * s[2] - f.a2 * s[3];
      s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y; x = y;
    }
    return x * d.outGain;
  }
};

static std::vector<int16_t> sine(double hz, double amp, uint32_t frames) {
  std::vector<int16_t> v(2 * frames);
  for (uint32_t i = 0; i < frames; ++i) {
    const double s = amp * sin(2 * Pi * hz * i / SampleRate);
    v[2 * i] = int16_t(lrint(s)); v[2 * i + 1] = int16_t(lrint(-s));
  }
  return v;
}

// Amplitude of the `hz` component in whole cycles of the second half of channel ch.
static double toneAmplitude(const std::vector<int16_t>& v, double hz, int ch) {
  const size_t frames = v.size() / 2, start = frames / 2;
  const double period = SampleRate / hz;
  const size_t end = start + size_t(floor(floor((frames - start) / period) * period));
  double re = 0, im = 0; size_t n = 0;
  for (size_t i = start; i < end; ++i, ++n) {
    const double w = 2 * Pi * hz * double(i) / SampleRate;
    re += v[2 * i + ch] * cos(w); im += v[2 * i + ch] * sin(w);
  }
  return 2 * sqrt(re * re + im * im) / double(n);
}

static uint32_t lcg = 12345;
static int16_t noise16() { lcg = lcg * 1664525u + 1013904223u; return int16_t(lcg >> 16); }

static const int8_t AllMax[Bands] = {12, 12, 12, 12, 12};
static const int8_t AllSix[Bands] = {6, 6, 6, 6, 6};
static const int8_t AllMin[Bands] = {-12, -12, -12, -12, -12};
static const int8_t Mixed[Bands] = {8, -4, 0, 6, -10};
static const int8_t Smile[Bands] = {10, 4, -6, 4, 10};

static void testDesign() {
  Coeffs c;
  const int8_t flat[Bands] = {};
  CHECK(design(flat, c) && c.count == 0, "flat design is bypass");
  // No-wrap bound for every gain of every band, and a sample of combinations.
  for (int b = 0; b < Bands; ++b)
    for (int db = MinDb; db <= MaxDb; ++db) {
      int8_t g[Bands] = {}; g[b] = int8_t(db);
      CHECK(design(g, c) && (db == 0 || c.count > 0), "band %d at %d dB rejected", b, db);
    }
  for (int i = 0; i < 3000; ++i) {
    int8_t g[Bands];
    for (int b = 0; b < Bands; ++b) { lcg = lcg * 1664525u + 1013904223u; g[b] = int8_t(MinDb + int((lcg >> 8) % 13) * StepDb); }
    CHECK(design(g, c), "combination %d rejected", i);
  }
  const int8_t alternating[Bands] = {12, -12, 12, -12, 12};
  CHECK(design(alternating, c), "alternating extremes rejected");
  CHECK(design(AllMax, c) && c.count == 5, "all-max design");
  std::printf("design: all +12 dB peak %+.2f dB, net level %+.2f dB\n", c.peakCentiDb / 100.0, c.preGainCentiDb / 100.0);
}

// The slider curve: targets met at the centres, small ripple between them.
static void testCurve() {
  auto range = [](const RefCascade& r, double& lo, double& hi) {
    lo = 1e9; hi = -1e9;
    for (int i = 0; i <= 96; ++i) { const double v = r.sliderDb(60.0 * pow(14000.0 / 60.0, i / 96.0)); lo = std::min(lo, v); hi = std::max(hi, v); }
  };
  double lo, hi;
  RefCascade a12(AllMax), a6(AllSix);
  range(a12, lo, hi);
  std::printf("curve: all +12 dB spans %+.2f..%+.2f dB from 60 Hz to 14 kHz (ripple %.2f dB)\n", lo, hi, hi - lo);
  CHECK(hi - lo <= 2.0 && lo >= 10.5 && hi <= 13.0, "all +12 ripple %.2f (%.2f..%.2f)", hi - lo, lo, hi);
  range(a6, lo, hi);
  std::printf("curve: all +6 dB spans %+.2f..%+.2f dB (ripple %.2f dB)\n", lo, hi, hi - lo);
  CHECK(hi - lo <= 1.0 && lo >= 5.0 && hi <= 6.6, "all +6 ripple %.2f (%.2f..%.2f)", hi - lo, lo, hi);
  for (int b = 0; b < Bands; ++b)
    for (int db : {12, -12, 6}) {
      int8_t g[Bands] = {}; g[b] = int8_t(db);
      RefCascade r(g);
      const double at = r.sliderDb(CentreHz[b]);
      double other = 0;
      for (int k = 0; k < Bands; ++k) if (k != b) other = std::max(other, fabs(r.sliderDb(CentreHz[k])));
      CHECK(fabs(at - db) <= 0.6 && other <= 1.0, "band %d at %d dB: centre %.2f, other centres up to %.2f", b, db, at, other);
    }
}

// Gain staging: each preset's net level (pink average 100 Hz-10 kHz) rises by at
// most half its average boost, and its boosted region really is louder than bypass.
static void testLoudness() {
  for (int p = 1; p < PresetCount; ++p) {
    RefCascade r(Presets[p].gain);
    double sum = 0;
    for (int i = 0; i < 48; ++i) sum += pow(10.0, r.netDb(100.0 * pow(100.0, i / 47.0)) / 10.0);
    const double level = 10 * log10(sum / 48);
    double best = -99;
    for (int b = 0; b < Bands; ++b) best = std::max(best, r.netDb(CentreHz[b]));
    std::printf("loudness: %-8s level %+.2f dB vs bypass, net at 60/250/1k/4k/14k Hz %+.1f %+.1f %+.1f %+.1f %+.1f dB\n",
                Presets[p].name, level, r.netDb(60), r.netDb(250), r.netDb(1000), r.netDb(4000), r.netDb(14000));
    CHECK(level >= 0.0 && level <= 3.0, "%s level %.2f dB (half-loudness match)", Presets[p].name, level);
    CHECK(best >= 2.0, "%s never rises above bypass (best %.2f dB)", Presets[p].name, best);
  }
}

// Fixed point against the double reference, limiter idle (moderate level).
static void testResponse() {
  const int8_t* sets[] = {AllMax, AllMin, Mixed, Smile, Presets[1].gain, Presets[2].gain};
  const char* names[] = {"all+12", "all-12", "mixed", "smile", "bass", "vocal"};
  for (int s = 0; s < 6; ++s) {
    Coeffs c; design(sets[s], c); RefCascade ref(sets[s]);
    double worst = 0;
    for (int i = 0; i < 31; ++i) {
      const double hz = 25.0 * pow(2.0, i / 3.2);
      if (hz > 20000) break;
      const uint32_t frames = hz < 100 ? 44100 : 11025;
      const double amp = 2000;
      const auto out = runEq(c, sine(hz, amp, frames));
      const double got = toneAmplitude(out, hz, 0) / amp, want = pow(10.0, ref.netDb(hz) / 20);
      const double errDb = fabs(20 * log10(got / want));
      if (errDb > worst) worst = errDb;
      CHECK(errDb < 0.05, "%s %.0f Hz: fixed %.4f float %.4f", names[s], hz, got, want);
    }
    RefCascade dbl(sets[s]);
    std::vector<int16_t> in(2 * 44100);
    for (auto& v : in) v = int16_t(noise16() / 16);
    const auto out = runEq(c, in);
    double maxErr = 0, sumErr = 0;
    for (size_t i = 0; i < in.size() / 2; ++i) {
      const double e = out[2 * i] - dbl.step(0, in[2 * i]);
      maxErr = std::max(maxErr, fabs(e)); sumErr += e;
    }
    const double dc = sumErr / double(in.size() / 2);
    std::printf("response %-7s worst %.4f dB, noise max error %.2f LSB, mean %.3f LSB\n", names[s], worst, maxErr, dc);
    // Includes output rounding. The rest is Q3.29 coefficient rounding of the low band at
    // large internal gains (the response itself is within 0.01 dB above), scaled by the
    // output gain, so the bound grows with it.
    const double gain = ref.d.scale * ref.d.outGain;
    CHECK(maxErr <= 2.0 + 3.0 * (gain > 1 ? gain : 1) * ref.d.peak, "%s noise error %.2f LSB", names[s], maxErr);
    CHECK(fabs(dc) <= 0.5, "%s mean error %.3f LSB", names[s], dc);
  }
}

// Every band at +12 dB and full-scale input: the limiter holds peaks at -1 dBFS,
// nothing wraps, and music-level input is left alone.
static void testLimiter() {
  Coeffs c; design(AllMax, c);
  const int ceiling = int(32768 * 0.8912509) + 2;
  struct Case { const char* name; int kind; } cases[] = {
    {"sine sweep", -1}, {"square 60 Hz", 0}, {"square 1 kHz", 1}, {"white noise", 2}, {"alternating +-FS", 3}, {"DC +FS", 4}, {"impulses", 5}};
  for (const auto& k : cases) {
    std::vector<int16_t> in(2 * 44100);
    for (size_t i = 0; i < in.size() / 2; ++i) {
      int16_t v = 0;
      switch (k.kind) {
        case -1: v = int16_t(lrint(32767 * sin(2 * Pi * 20.0 * pow(1000.0, double(i) / 44100) * i / SampleRate))); break;
        case 0: v = (i / 368) % 2 ? 32767 : -32768; break;
        case 1: v = (i / 22) % 2 ? 32767 : -32768; break;
        case 2: v = noise16(); break;
        case 3: v = i % 2 ? 32767 : -32768; break;
        case 4: v = 32767; break;
        case 5: v = i % 4410 == 0 ? 32767 : 0; break;
      }
      in[2 * i] = v; in[2 * i + 1] = int16_t(-v - 1);
    }
    State s = settled(c);
    const auto out = runEq(c, in, &s);
    int peak = 0;
    for (int16_t v : out) peak = std::max(peak, abs(int(v)));
    std::printf("limiter: %-17s peak %5d (%.2f dBFS), frames limited %u\n", k.name, peak, 20 * log10(peak / 32768.0), s.limited);
    CHECK(peak <= ceiling, "%s peak %d above the -1 dBFS ceiling", k.name, peak);
  }
  // Bass preset on -20 dBFS noise: no limiting.
  Coeffs b; design(Presets[1].gain, b);
  std::vector<int16_t> in(2 * 44100);
  for (auto& v : in) v = int16_t(noise16() / 10);
  State s = settled(b);
  runEq(b, in, &s);
  CHECK(s.limited == 0, "Bass on -20 dBFS noise limited %u frames", s.limited);
}

static void testBypass() {
  Coeffs c; const int8_t flat[Bands] = {};
  design(flat, c);
  std::vector<int16_t> in(2 * 4096);
  for (auto& v : in) v = noise16();
  in[0] = -32768; in[1] = 32767;
  // eq.cpp never calls process() when count is 0; process() itself is also exact with the limiter off.
  const auto out = runEq(c, in, nullptr, false);
  CHECK(c.count == 0 && memcmp(in.data(), out.data(), in.size() * 2) == 0, "bypass is not bit-exact");
}

// A band switched on mid-stream starts as the identity filter: no step. And
// switching the EQ on from bypass keeps the level continuous, then glides.
static void testSeed() {
  const int8_t a[Bands] = {6, 0, 0, 0, 0}, b[Bands] = {6, 6, 2, 0, 0};
  Coeffs ca, cb; design(a, ca); design(b, cb);
  State s = settled(ca);
  const auto in = sine(440, 12000, 8192);
  std::vector<int16_t> half1(in.begin(), in.begin() + 8192), half2(in.begin() + 8192, in.end());
  const auto o1 = runEq(ca, half1, &s);
  s.retarget(ca, cb); s.seed(cb);
  const auto o2 = runEq(cb, half2, &s);
  const int across = abs(o2[0] - o1[o1.size() - 2]);
  const int normal = abs(o1[o1.size() - 2] - o1[o1.size() - 4]);
  std::printf("seed: step across a design change %d LSB, typical step %d LSB\n", across, normal);
  CHECK(across < 3 * normal + 64, "step across design change %d (typical %d)", across, normal);
  Coeffs flat; State t;
  t.retarget(flat, cb); t.seed(cb);
  CHECK(fabs(t.gain * cb.scale - 1.0f) < 1e-5f, "switching on starts at unity overall gain (%.5f)", t.gain * cb.scale);
}

// Presets: on the UI grid, designable, matched back, cycled; Custom detected.
static void testPresets() {
  CHECK(PresetCount == 5 && presetMatch(Presets[0].gain) == 0, "Flat is preset 0");
  for (int p = 0; p < PresetCount; ++p) {
    Coeffs c;
    CHECK(design(Presets[p].gain, c), "preset %s rejected", Presets[p].name);
    for (int b = 0; b < Bands; ++b)
      CHECK(Presets[p].gain[b] % StepDb == 0 && clampGain(Presets[p].gain[b]) == Presets[p].gain[b], "preset %s off grid", Presets[p].name);
    CHECK(presetMatch(Presets[p].gain) == p, "preset %s not matched", Presets[p].name);
    CHECK(presetNext(Presets[p].gain) == (p + 1) % PresetCount, "preset %s next", Presets[p].name);
  }
  const int8_t custom[Bands] = {2, 0, 0, 0, 0};
  CHECK(presetMatch(custom) == -1 && presetNext(custom) == 1, "Custom detected and cycles to Bass");
}

int main() {
  testDesign();
  testCurve();
  testLoudness();
  testResponse();
  testLimiter();
  testBypass();
  testSeed();
  testPresets();
  if (failures) { std::printf("FAIL: eq core (%d failures)\n", failures); return 1; }
  std::printf("PASS: eq core (corrected curve, loudness staging, response vs float, limiter, bit-exact bypass, continuity, presets)\n");
  return 0;
}
