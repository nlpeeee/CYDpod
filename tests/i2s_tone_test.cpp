// CY-70 host test for firmware/cydpod_i2s_tone/i2s_tone.h (no Arduino, no IDF).
#include "../firmware/cydpod_i2s_tone/i2s_tone.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace i2stone;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<int16_t> render(const Params& p, size_t frames, size_t chunk) {
  Generator g(p);
  std::vector<int16_t> out(frames * 2);
  for (size_t done = 0; done < frames;) {
    size_t n = frames - done < chunk ? frames - done : chunk;
    g.fill(&out[done * 2], n);
    done += n;
  }
  return out;
}

static int risingZeroCrossings(const std::vector<int16_t>& s, size_t from, size_t frames, int ch) {
  int n = 0;
  for (size_t i = from + 1; i < from + frames; ++i)
    if (s[2 * (i - 1) + ch] < 0 && s[2 * i + ch] >= 0) ++n;
  return n;
}

static double dbfs(int peak) { return 20.0 * log10((double)peak / 32767.0); }

int main() {
  Params cont;  // continuous tone, no boot silence
  cont.bootSilence = 0;
  cont.offFrames = 0;

  // Sine table accuracy.
  {
    double worst = 0;
    for (uint32_t i = 0; i < 100000; ++i) {
      uint32_t ph = i * 42949;  // spreads across the whole 32-bit circle
      double ref = 32767.0 * sin(2.0 * 3.14159265358979323846 * ph / 4294967296.0);
      double e = fabs((double)sineQ15(ph) - ref);
      if (e > worst) worst = e;
    }
    CHECK(worst <= 2.0, "sine error %.3f LSB", worst);
    printf("sine max error %.3f LSB\n", worst);
  }

  // Frequency by zero-crossing count over 1 s.
  {
    auto s = render(cont, kRate, 256);
    int l = risingZeroCrossings(s, 0, kRate, 0), r = risingZeroCrossings(s, 0, kRate, 1);
    CHECK(abs(l - 440) <= 1, "left crossings %d", l);
    CHECK(abs(r - 880) <= 1, "right crossings %d", r);
    printf("zero crossings in 1 s: L=%d R=%d\n", l, r);
  }

  // Amplitude -20 dBFS +-0.2 dB (peak) over 1 s of the continuous tone.
  {
    auto s = render(cont, kRate, 256);
    int pl = 0, pr = 0;
    for (size_t i = 0; i < kRate; ++i) {
      if (abs(s[2 * i]) > pl) pl = abs(s[2 * i]);
      if (abs(s[2 * i + 1]) > pr) pr = abs(s[2 * i + 1]);
    }
    CHECK(fabs(dbfs(pl) + 20.0) <= 0.2, "left peak %d = %.3f dBFS", pl, dbfs(pl));
    CHECK(fabs(dbfs(pr) + 20.0) <= 0.2, "right peak %d = %.3f dBFS", pr, dbfs(pr));
    printf("peak L=%d (%.3f dBFS) R=%d (%.3f dBFS)\n", pl, dbfs(pl), pr, dbfs(pr));
  }

  // Fade ramps: start at 0, monotonic, symmetric, no step above one table
  // increment bound, flat 1.0 in between, zero outside.
  {
    int32_t prev = -1, maxStep = 0;
    CHECK(gateGainQ15(0, kOnFrames, kFadeFrames) == 0, "fade-in must start at 0");
    for (uint32_t i = 0; i < kFadeFrames; ++i) {
      int32_t g = gateGainQ15(i, kOnFrames, kFadeFrames);
      CHECK(g >= prev, "fade-in not monotonic at %u", i);
      if (prev >= 0 && g - prev > maxStep) maxStep = g - prev;
      prev = g;
    }
    CHECK(maxStep <= 32767 / (int32_t)kFadeFrames * 2, "fade-in step %d", maxStep);
    prev = 1 << 20;
    for (uint32_t i = kOnFrames - kFadeFrames; i < kOnFrames; ++i) {
      int32_t g = gateGainQ15(i, kOnFrames, kFadeFrames);
      CHECK(g <= prev, "fade-out not monotonic at %u", i);
      prev = g;
    }
    CHECK(gateGainQ15(kOnFrames - 1, kOnFrames, kFadeFrames) == 0, "fade-out must end at 0");
    CHECK(gateGainQ15(kFadeFrames, kOnFrames, kFadeFrames) == 32767, "plateau gain");
    CHECK(gateGainQ15(kOnFrames, kOnFrames, kFadeFrames) == 0, "off window gain");
    CHECK(kFadeFrames == 220, "5 ms at 44.1 kHz is 220 frames, got %u", (unsigned)kFadeFrames);
    printf("fade max step %d of 32767 over %u frames\n", maxStep, (unsigned)kFadeFrames);
  }

  // Full gated stream: boot silence, 1 s on / 1 s off, click-free steps.
  {
    const size_t frames = kBootSilenceFrames + 4 * kRate;
    auto s = render(Params(), frames, 480);
    for (size_t i = 0; i < kBootSilenceFrames; ++i)
      if (s[2 * i] || s[2 * i + 1]) { CHECK(false, "boot silence broken at %zu", i); break; }
    int maxL = 0, maxR = 0, peakL = 0, peakR = 0;
    for (size_t i = 1; i < frames; ++i) {
      int dl = abs(s[2 * i] - s[2 * (i - 1)]), dr = abs(s[2 * i + 1] - s[2 * (i - 1) + 1]);
      if (dl > maxL) maxL = dl;
      if (dr > maxR) maxR = dr;
      if (abs(s[2 * i]) > peakL) peakL = abs(s[2 * i]);
      if (abs(s[2 * i + 1]) > peakR) peakR = abs(s[2 * i + 1]);
    }
    // Largest legal step of a 3277-peak sine: A*2*pi*f/fs = 205 (440) / 411 (880),
    // plus the fade slope contribution (A * pi/2 / 220 = 23) and rounding.
    CHECK(maxL <= 215, "left max step %d", maxL);
    CHECK(maxR <= 435, "right max step %d", maxR);
    CHECK(peakL <= kAmpQ15 && peakR <= kAmpQ15, "gated peak %d/%d above amplitude", peakL, peakR);
    // Tone only inside the on windows, silence in the off windows.
    for (int w = 0; w < 2; ++w) {
      size_t on = kBootSilenceFrames + (size_t)w * 2 * kRate;
      int c = risingZeroCrossings(s, on, kRate, 0);
      CHECK(abs(c - 440) <= 2, "window %d left crossings %d", w, c);
      for (size_t i = on + kRate; i < on + 2 * kRate; ++i)
        if (s[2 * i] || s[2 * i + 1]) { CHECK(false, "off window %d not silent at %zu", w, i); break; }
      CHECK(s[2 * on] == 0 && s[2 * (on + kRate - 1)] == 0, "gate edges not zero");
    }
    printf("gated stream max step L=%d R=%d, peak L=%d R=%d\n", maxL, maxR, peakL, peakR);
  }

  // No overflow even at an absurd amplitude setting.
  {
    Params loud = cont;
    loud.ampQ15 = 32767;
    auto s = render(loud, kRate, 256);
    int lo = 0, hi = 0;
    for (int16_t v : s) { if (v < lo) lo = v; if (v > hi) hi = v; }
    CHECK(lo >= -32767 && hi <= 32767 && hi > 32000 && lo < -32000, "full-scale range %d..%d", lo, hi);
    printf("full-scale range %d..%d\n", lo, hi);
  }

  // Phase continuity: any chunking gives the identical stream.
  {
    const size_t frames = kBootSilenceFrames + 2 * kRate + 1000;
    auto ref = render(Params(), frames, frames);
    const size_t chunks[] = {1, 7, 256, 480};
    for (size_t c : chunks) {
      auto s = render(Params(), frames, c);
      CHECK(s == ref, "chunk size %zu differs from single-call render", c);
    }
    printf("chunk sizes 1, 7, 256, 480 identical to a single call over %zu frames\n", frames);
  }

  if (failures) { printf("I2S_TONE_FAIL %d\n", failures); return 1; }
  printf("I2S_TONE_OK\n");
  return 0;
}
