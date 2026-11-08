// EQ-5BAND host tool: runs the firmware EQ (eq_core.h, the C++ reference the
// Xtensa loop matches bit for bit) over raw s16le stereo 44.1 kHz, and prints
// the equivalent ffmpeg filter chain, for tests/eq_ffmpeg_check.sh.
//   eq_render filter G0 G1 G2 G3 G4           -> ffmpeg -af chain for the same design
//   eq_render run    G0..G4 in.raw out.raw [nolimit]
//   eq_render legacy G0..G4 in.raw out.raw    -> the first shipped design (Q 0.9,
//                                                gains as filter gains, -(peak+1 dB))
//   eq_render double G0..G4 in.raw out.raw    -> same design in double precision, no limiter
//   eq_render compare a.raw b.raw [maxLSB]    -> max / mean / RMS difference; fails above maxLSB (4)
#include "eq_core.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>

using namespace cydeq;

static std::vector<int16_t> load(const char* path) {
  std::vector<int16_t> v;
  FILE* f = std::fopen(path, "rb");
  if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
  int16_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 2, 4096, f)) > 0) v.insert(v.end(), buf, buf + n);
  std::fclose(f);
  v.resize(v.size() & ~size_t(1));
  return v;
}
static void save(const char* path, const std::vector<int16_t>& v) {
  FILE* f = std::fopen(path, "wb");
  if (!f) { std::fprintf(stderr, "cannot write %s\n", path); std::exit(2); }
  std::fwrite(v.data(), 2, v.size(), f);
  std::fclose(f);
}
static void gainsFrom(char** argv, int8_t g[Bands]) { for (int b = 0; b < Bands; ++b) g[b] = int8_t(std::atoi(argv[b])); }

static std::vector<int16_t> run(const Coeffs& c, const std::vector<int16_t>& in, bool limit) {
  State s; s.seed(c); s.gain = c.outGain;
  std::vector<int16_t> out(in);
  if (!c.count) return out;  // firmware bypass: untouched
  int32_t buf[64];
  for (size_t f = 0; f < in.size() / 2; f += 32) {
    const uint32_t n = uint32_t(in.size() / 2 - f < 32 ? in.size() / 2 - f : 32);
    process(c, s, &in[2 * f], &out[2 * f], n, buf, stageRefStride2, limit);
  }
  return out;
}

// The first shipped design, rebuilt for listening comparisons only.
static Coeffs legacy(const int8_t g[Bands]) {
  Coeffs c;
  DesignBiquad f[Bands];
  int n = 0;
  for (int b = 0; b < Bands; ++b) {
    if (!g[b]) continue;
    const double A = pow(10.0, g[b] / 40.0), w0 = 2.0 * Pi * CentreHz[b] / SampleRate;
    const double alpha = sin(w0) / (2.0 * 0.9), cw = cos(w0), a0 = 1.0 + alpha / A;
    f[n] = {(1.0 + alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha / A) / a0};
    c.band[n++] = uint8_t(b);
  }
  if (!n) return c;
  double peak = 0;
  for (int i = 0; i < 165; ++i) {
    const double cw = cosAt(i < 160 ? 20.0 * pow(1000.0, i / 159.0) : CentreHz[i - 160]);
    double p = 1; for (int k = 0; k < n; ++k) p *= power(f[k], cw);
    if (p > peak) peak = p;
  }
  double s = pow(10.0, -1.0 / 20.0) / sqrt(peak); if (s > 1) s = 1;
  f[0].b0 *= s; f[0].b1 *= s; f[0].b2 *= s;
  for (int k = 0; k < n; ++k) {
    quantise(f[k].b0, c.stage[k].b0); quantise(f[k].b1, c.stage[k].b1); quantise(f[k].b2, c.stage[k].b2);
    quantise(-f[k].a1, c.stage[k].na1); quantise(-f[k].a2, c.stage[k].na2);
  }
  c.count = uint8_t(n); c.scale = float(s); c.outGain = 1.0f;
  return c;
}

int main(int argc, char** argv) {
  if (argc >= 7 && !std::strcmp(argv[1], "filter")) {
    int8_t g[Bands]; gainsFrom(argv + 2, g);
    Design d; designBiquads(g, d);
    if (!d.n) { std::printf("anull\n"); return 0; }
    std::printf("aformat=sample_fmts=dblp");
    for (int k = 0; k < d.n; ++k)
      std::printf(",equalizer=f=%.6f:t=q:w=%.6f:g=%.9f", CentreHz[d.band[k]], Q, d.internalDb[d.band[k]]);
    std::printf(",volume=%.12f:precision=double\n", d.scale * d.outGain);
    return 0;
  }
  if (argc >= 9 && (!std::strcmp(argv[1], "run") || !std::strcmp(argv[1], "legacy"))) {
    int8_t g[Bands]; gainsFrom(argv + 2, g);
    const bool isLegacy = !std::strcmp(argv[1], "legacy");
    Coeffs c;
    if (isLegacy) c = legacy(g); else design(g, c);
    const auto out = run(c, load(argv[7]), !isLegacy && !(argc > 9 && !std::strcmp(argv[9], "nolimit")));
    save(argv[8], out);
    return 0;
  }
  if (argc >= 9 && !std::strcmp(argv[1], "double")) {
    int8_t g[Bands]; gainsFrom(argv + 2, g);
    Design d; designBiquads(g, d);
    std::vector<int16_t> v = load(argv[7]);
    if (d.n) {
      double z[2][Bands][4] = {};
      for (size_t i = 0; i < v.size(); ++i) {
        double x = v[i];
        for (int k = 0; k < d.n; ++k) {
          double* st = z[i & 1][k]; const DesignBiquad& f = d.f[k];
          const double y = f.b0 * x + f.b1 * st[0] + f.b2 * st[1] - f.a1 * st[2] - f.a2 * st[3];
          st[1] = st[0]; st[0] = x; st[3] = st[2]; st[2] = y; x = y;
        }
        x = std::round(x * d.outGain);
        v[i] = int16_t(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
      }
    }
    save(argv[8], v);
    return 0;
  }
  if ((argc == 4 || argc == 5) && !std::strcmp(argv[1], "compare")) {
    const auto a = load(argv[2]), b = load(argv[3]);
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    double maxd = 0, sum = 0, sig = 0, mean = 0;
    for (size_t i = 0; i < n; ++i) {
      const double d = double(a[i]) - b[i];
      if (d < 0 ? -d > maxd : d > maxd) maxd = d < 0 ? -d : d;
      sum += d * d; sig += double(a[i]) * a[i]; mean += d;
    }
    mean /= double(n);
    const double rms = sqrt(sum / double(n)), srms = sqrt(sig / double(n));
    std::printf("compare: %zu samples, max diff %.0f LSB, mean %+.3f LSB, RMS diff %.3f LSB, signal RMS %.1f LSB\n",
                n, maxd, mean, rms, srms);
    return maxd <= (argc == 5 ? std::atof(argv[4]) : 4.0) && a.size() == b.size() ? 0 : 1;
  }
  std::fprintf(stderr, "usage: see the header of tests/eq_render.cpp\n");
  return 2;
}
