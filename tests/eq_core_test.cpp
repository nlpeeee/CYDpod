// Host tests for eq_core.h (docs/specs/eq-5band.md): frequency response against
// double-precision biquads, no wrap with every band boosted and full-scale input,
// bit-exact bypass, and continuity when a band switches on mid-stream.
#include "eq_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace cydeq;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// Runs the fixed-point EQ over interleaved stereo, in 32-frame chunks like eq.cpp.
static std::vector<int16_t> runEq(const Coeffs& c, const std::vector<int16_t>& in, State* keep = nullptr) {
  State local; State& s = keep ? *keep : local;
  if (!keep) s.seed(c);
  std::vector<int16_t> out(in.size());
  int32_t buf[64];
  const uint32_t frames = uint32_t(in.size() / 2);
  for (uint32_t f = 0; f < frames; f += 32) {
    const uint32_t n = frames - f < 32 ? frames - f : 32;
    process(c, s, &in[2 * f], &out[2 * f], n, buf);
  }
  return out;
}

// Double-precision reference cascade with the same design (pre-gain included).
struct RefCascade {
  DesignBiquad f[Bands]; int n = 0; double z[2][Bands][4] = {};
  explicit RefCascade(const int8_t gains[Bands]) {
    for (int b = 0; b < Bands; ++b) if (clampGain(gains[b])) f[n++] = peaking(CentreHz[b], clampGain(gains[b]));
    if (!n) return;
    double peak = 0;
    for (int i = 0; i < 160 + Bands; ++i) {
      const double hz = i < 160 ? 20.0 * pow(1000.0, i / 159.0) : CentreHz[i - 160];
      double m = 1; for (int k = 0; k < n; ++k) m *= magnitude(f[k], 2 * M_PI * hz / SampleRate);
      if (m > peak) peak = m;
    }
    double g = pow(10.0, -MarginDb / 20.0) / peak; if (g > 1) g = 1;
    f[0].b0 *= g; f[0].b1 *= g; f[0].b2 *= g;
  }
  double response(double hz) const {
    double m = 1; for (int k = 0; k < n; ++k) m *= magnitude(f[k], 2 * M_PI * hz / SampleRate); return m;
  }
  double step(int ch, double x) {
    for (int k = 0; k < n; ++k) {
      double* s = z[ch][k];
      const double y = f[k].b0 * x + f[k].b1 * s[0] + f[k].b2 * s[1] - f[k].a1 * s[2] - f[k].a2 * s[3];
      s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y; x = y;
    }
    return x;
  }
};

static std::vector<int16_t> sine(double hz, double amp, uint32_t frames) {
  std::vector<int16_t> v(2 * frames);
  for (uint32_t i = 0; i < frames; ++i) {
    const double s = amp * sin(2 * M_PI * hz * i / SampleRate);
    v[2 * i] = int16_t(lrint(s)); v[2 * i + 1] = int16_t(lrint(-s));
  }
  return v;
}

// Amplitude of the `hz` component in the second half (steady state) of channel ch.
static double toneAmplitude(const std::vector<int16_t>& v, double hz, int ch) {
  // Whole cycles of the second half only, so leakage does not bias the estimate.
  const size_t frames = v.size() / 2, start = frames / 2;
  const double period = SampleRate / hz;
  const size_t end = start + size_t(floor(floor((frames - start) / period) * period));
  double re = 0, im = 0; size_t n = 0;
  for (size_t i = start; i < end; ++i, ++n) {
    const double w = 2 * M_PI * hz * double(i) / SampleRate;
    re += v[2 * i + ch] * cos(w); im += v[2 * i + ch] * sin(w);
  }
  return 2 * sqrt(re * re + im * im) / double(n);
}

static uint32_t lcg = 12345;
static int16_t noise16() { lcg = lcg * 1664525u + 1013904223u; return int16_t(lcg >> 16); }

static const int8_t AllMax[Bands] = {12, 12, 12, 12, 12};
static const int8_t AllMin[Bands] = {-12, -12, -12, -12, -12};
static const int8_t Mixed[Bands] = {8, -4, 0, 6, -10};
static const int8_t Smile[Bands] = {10, 4, -6, 4, 10};

static void testDesign() {
  Coeffs c;
  const int8_t flat[Bands] = {};
  CHECK(design(flat, c) && c.count == 0, "flat design is bypass");
  CHECK(design(Mixed, c) && c.count == 4 && c.band[2] == 3, "0 dB band is skipped (count %u)", c.count);
  // No-wrap bound for every gain of every band, and a sample of combinations.
  for (int b = 0; b < Bands; ++b)
    for (int db = MinDb; db <= MaxDb; ++db) {
      int8_t g[Bands] = {}; g[b] = int8_t(db);
      CHECK(design(g, c), "band %d at %d dB rejected", b, db);
    }
  for (int i = 0; i < 3000; ++i) {
    int8_t g[Bands];
    for (int b = 0; b < Bands; ++b) { lcg = lcg * 1664525u + 1013904223u; g[b] = int8_t(MinDb + int((lcg >> 8) % 13) * StepDb); }
    CHECK(design(g, c), "combination %d rejected", i);
  }
  CHECK(design(AllMax, c) && c.count == 5, "all-max design");
  std::printf("design: all +12 dB peak %+.2f dB pre-gain %+.2f dB\n", c.peakCentiDb / 100.0, c.preGainCentiDb / 100.0);
}

// Measured fixed-point response at 31 frequencies within 0.05 dB of the float design.
static void testResponse() {
  const int8_t* sets[] = {AllMax, AllMin, Mixed, Smile};
  const char* names[] = {"all+12", "all-12", "mixed", "smile"};
  for (int s = 0; s < 4; ++s) {
    Coeffs c; design(sets[s], c); RefCascade ref(sets[s]);
    double worst = 0;
    for (int i = 0; i < 31; ++i) {
      const double hz = 25.0 * pow(2.0, i / 3.2);
      if (hz > 20000) break;
      const uint32_t frames = hz < 100 ? 44100 : 11025;
      const double amp = 8000;
      const auto out = runEq(c, sine(hz, amp, frames));
      const double got = toneAmplitude(out, hz, 0) / amp, want = ref.response(hz);
      const double errDb = fabs(20 * log10(got / want));
      if (errDb > worst) worst = errDb;
      CHECK(errDb < 0.05, "%s %.0f Hz: fixed %.4f float %.4f", names[s], hz, got, want);
    }
    // Sample-by-sample against the double cascade on noise.
    RefCascade dbl(sets[s]);
    std::vector<int16_t> in(2 * 44100);
    for (auto& v : in) v = int16_t(noise16() / 4);
    const auto out = runEq(c, in);
    double maxErr = 0, sumErr = 0;
    for (size_t i = 0; i < in.size() / 2; ++i) {
      const double want = dbl.step(0, in[2 * i]);
      const double e = out[2 * i] - want;
      if (fabs(e) > maxErr) maxErr = fabs(e);
      sumErr += e;
    }
    const double dc = sumErr / double(in.size() / 2);
    std::printf("response %-7s worst %.4f dB, noise max error %.2f LSB, mean %.3f LSB\n", names[s], worst, maxErr, dc);
    CHECK(maxErr <= 4.0, "%s noise error %.2f LSB", names[s], maxErr);
    CHECK(fabs(dc) <= 0.5, "%s mean error %.3f LSB", names[s], dc);
  }
}

// Every band at +12 dB, full-scale input. Sines must not clip at all; wideband
// worst cases may saturate but must never wrap (stay within 1 dB of the
// clipped double reference's sign and size).
static void testHeadroom() {
  Coeffs c; design(AllMax, c);
  uint32_t sineClips = 0;
  double sinePeak = 0;
  for (int i = 0; i < 120; ++i) {
    const double hz = 20.0 * pow(1000.0, i / 119.0);
    const auto out = runEq(c, sine(hz, 32767, hz < 100 ? 44100 : 8820));
    for (size_t k = out.size() / 2; k < out.size(); ++k) {
      if (out[k] >= 32767 || out[k] <= -32768) ++sineClips;
      if (abs(out[k]) > sinePeak) sinePeak = abs(out[k]);
    }
  }
  CHECK(sineClips == 0, "full-scale sines clipped %u samples", sineClips);
  std::printf("headroom: full-scale sine sweep peak %.0f (%.2f dBFS), clipped samples %u\n",
              sinePeak, 20 * log10(sinePeak / 32768.0), sineClips);
  struct Case { const char* name; int kind; } cases[] = {
    {"square 60 Hz", 0}, {"square 1 kHz", 1}, {"white noise", 2}, {"alternating +-FS", 3}, {"DC +FS", 4}, {"impulses", 5}};
  for (const auto& k : cases) {
    std::vector<int16_t> in(2 * 44100);
    for (size_t i = 0; i < in.size() / 2; ++i) {
      int16_t v = 0;
      switch (k.kind) {
        case 0: v = (i / 368) % 2 ? 32767 : -32768; break;
        case 1: v = (i / 22) % 2 ? 32767 : -32768; break;
        case 2: v = noise16(); break;
        case 3: v = i % 2 ? 32767 : -32768; break;
        case 4: v = 32767; break;
        case 5: v = i % 4410 == 0 ? 32767 : 0; break;
      }
      in[2 * i] = v; in[2 * i + 1] = int16_t(-v - 1);
    }
    const auto out = runEq(c, in);
    RefCascade dbl(AllMax);
    uint32_t clips = 0, wraps = 0; double worst = 0;
    for (size_t i = 0; i < in.size() / 2; ++i) {
      double want = dbl.step(0, in[2 * i]);
      if (fabs(want) > 32767) ++clips;
      want = want > 32767 ? 32767 : want < -32768 ? -32768 : want;
      const double e = fabs(out[2 * i] - want);
      if (e > worst) worst = e;
      if (e > 1024) ++wraps;
    }
    std::printf("headroom: %-17s ideal output beyond full scale %5u samples, max |fixed - clipped ideal| %.1f LSB\n",
                k.name, clips, worst);
    CHECK(wraps == 0, "%s: %u samples far from clipped ideal (wrap)", k.name, wraps);
  }
}

static void testBypass() {
  // design() gives count 0 for flat; process() with count 0 is also exact.
  Coeffs c; const int8_t flat[Bands] = {};
  design(flat, c);
  std::vector<int16_t> in(2 * 4096);
  for (auto& v : in) v = noise16();
  in[0] = -32768; in[1] = 32767;
  const auto out = runEq(c, in);
  CHECK(memcmp(in.data(), out.data(), in.size() * 2) == 0, "bypass is not bit-exact");
}

// A band switched on mid-stream starts as the identity filter: no step.
static void testSeed() {
  const int8_t a[Bands] = {6, 0, 0, 0, 0}, b[Bands] = {6, 2, 0, 0, 0};
  Coeffs ca, cb; design(a, ca); design(b, cb);
  State s; s.seed(ca);
  const auto in = sine(440, 12000, 8192);
  std::vector<int16_t> half1(in.begin(), in.begin() + 8192), half2(in.begin() + 8192, in.end());
  const auto o1 = runEq(ca, half1, &s);
  s.seed(cb);
  const auto o2 = runEq(cb, half2, &s);
  int jump = 0;
  for (int i = 2; i < 64; i += 2) jump = std::max(jump, abs(o2[i] - o2[i - 2]));
  const int across = abs(o2[0] - o1[o1.size() - 2]);
  const int normal = abs(o1[o1.size() - 2] - o1[o1.size() - 4]);
  std::printf("seed: step across switch %d LSB, typical step %d LSB\n", across, normal);
  CHECK(across < 3 * normal + 64, "step across band switch %d (typical %d)", across, normal);
}

int main() {
  testDesign();
  testResponse();
  testHeadroom();
  testBypass();
  testSeed();
  if (failures) { std::printf("FAIL: eq core (%d failures)\n", failures); return 1; }
  std::printf("PASS: eq core (response vs float, full-scale headroom, bit-exact bypass, band seeding)\n");
  return 0;
}
