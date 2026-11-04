// CY-71 host test for firmware/cydpod_player/app/wired_fade.h (no Arduino, no IDF).
// A tiny simulator reproduces what audio_pull() hands the writer (live frames
// while playing and not seeking, zeros otherwise) and checks the stage's output
// for steps, monotone ramps, the ceiling and the state-machine corner cases.
#include "../firmware/cydpod_player/app/wired_fade.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace cydwired;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const int kChunk = 256;
static const double kPi = 3.14159265358979323846;

struct Sim {
  Fade fade;
  double amp, freq, phase = 0.0;
  bool playing = false, seeking = false, muted = false, wired = true;
  uint32_t serial = 1;
  bool dc = false;              // constant +amp / -amp instead of a sine
  std::vector<int16_t> out;     // interleaved stereo

  explicit Sim(double a = 30000.0, double f = 200.0, bool dither = false) : fade(kCeilingQ15, dither), amp(a), freq(f) {}

  void jump() { phase += 1.7; }   // a seek or new track lands somewhere else in the waveform

  // One chunk. `avail` is how many live frames the ring holds (256 = full).
  void chunk(int avail = kChunk) {
    int16_t buf[kChunk * 2];
    int valid = (playing && !seeking) ? (avail < kChunk ? avail : kChunk) : 0;
    for (int i = 0; i < kChunk; ++i) {
      if (i < valid) {
        const double s = dc ? 1.0 : sin(phase);
        buf[2 * i] = (int16_t)lround(amp * s);
        buf[2 * i + 1] = (int16_t)lround(-amp * s * 0.5);
        phase += 2.0 * kPi * freq / kSampleRate;
      } else {
        buf[2 * i] = buf[2 * i + 1] = 0;
      }
    }
    Inputs in;
    in.playing = playing; in.seeking = seeking; in.muted = muted; in.wired = wired; in.trackSerial = serial;
    fade.process(buf, kChunk, valid, in);
    out.insert(out.end(), buf, buf + kChunk * 2);
  }
  void chunks(int n, int avail = kChunk) { for (int i = 0; i < n; ++i) chunk(avail); }
  size_t frames() const { return out.size() / 2; }
};

// Largest |difference| between consecutive samples of one channel in [from, to).
static int maxStep(const std::vector<int16_t>& s, size_t from, size_t to, int ch) {
  int m = 0;
  for (size_t i = from + 1; i < to && i < s.size() / 2; ++i) {
    const int d = abs((int)s[2 * i + ch] - (int)s[2 * (i - 1) + ch]);
    if (d > m) m = d;
  }
  return m;
}
static int maxStepAll(const Sim& sim, int ch = 0) { return maxStep(sim.out, 0, sim.frames(), ch); }

// A sine's own largest step, scaled by the ceiling, plus the extra slope of the
// ramp acting on the held sample. A hard cut from the peak would be amp * 0.5.
static int stepBound(const Sim& sim) {
  const double ceil = (double)kCeilingQ15 / kUnityQ15;
  const double natural = sim.amp * 2.0 * kPi * sim.freq / kSampleRate * ceil;
  const double ramp = sim.amp * ceil * (kPi / 2.0) / kRampFrames;
  return (int)(natural + 2.0 * ramp) + 4;
}

static size_t lastNonZero(const std::vector<int16_t>& s, size_t from, size_t to) {
  size_t last = (size_t)-1;
  for (size_t i = from; i < to && i < s.size() / 2; ++i)
    if (s[2 * i] != 0 || s[2 * i + 1] != 0) last = i;
  return last;
}

int main() {
  // ----- table / constants -----
  {
    Fade f;
    CHECK(kRampFrames * 1000 / kSampleRate >= 5 && kRampFrames * 1000 / kSampleRate <= 10, "ramp ms");
    CHECK(f.rampGainQ15() == 0 && f.silent(), "starts silent");
    const double db = 20.0 * log10((double)kCeilingQ15 / kUnityQ15);
    CHECK(fabs(db + 6.0) < 0.01, "ceiling is %.4f dB, want -6.0", db);
    // Walk the table via a DC run and compare with the closed form.
    Sim s(20000, 200, false); s.dc = true; s.playing = true; s.chunks(3);
    int prev = -1; bool mono = true;
    for (size_t i = 0; i < s.frames() && i < (size_t)kRampFrames + 1; ++i) {
      const int v = s.out[2 * i];
      if (v < prev) mono = false;
      prev = v;
      const double c = 0.5 * (1.0 - cos(kPi * (double)(i + 1) / kRampFrames));
      const double want = 20000.0 * c * kCeilingQ15 / kUnityQ15;
      if (i < (size_t)kRampFrames && fabs((double)v - want) > 2.0) { CHECK(false, "ramp[%zu]=%d want %.1f", i, v, want); break; }
    }
    CHECK(mono, "fade-in monotone");
    CHECK(s.out[2 * (kRampFrames + 5)] == (int16_t)lround(20000.0 * kCeilingQ15 / kUnityQ15), "settles at ceiling*input: %d", s.out[2 * (kRampFrames + 5)]);
    const int mid = s.out[2 * (kRampFrames / 2 - 1)];
    CHECK(abs(mid - (int)lround(20000.0 * kCeilingQ15 / kUnityQ15 / 2.0)) <= 1, "midpoint is half of the final level: %d", mid);
  }

  // ----- ceiling -----
  {
    Sim s(32767, 1000, false); s.playing = true; s.chunks(6);
    int peak = 0;
    for (int16_t v : s.out) if (abs(v) > peak) peak = abs(v);
    CHECK(peak <= 16423 && peak >= 16400, "full-scale sine peak %d, want ~16423 (-6 dBFS)", peak);
    // Both polarities at full scale, DC.
    Fade f; int16_t buf[kChunk * 2];
    for (int pass = 0; pass < 2; ++pass) {
      for (int i = 0; i < kChunk; ++i) { buf[2 * i] = pass ? -32768 : 32767; buf[2 * i + 1] = pass ? 32767 : -32768; }
      Inputs in; in.playing = true;
      for (int k = 0; k < 4; ++k) { if (k) for (int i = 0; i < kChunk; ++i) { buf[2 * i] = pass ? -32768 : 32767; buf[2 * i + 1] = pass ? 32767 : -32768; } f.process(buf, kChunk, kChunk, in); }
      for (int i = 0; i < kChunk; ++i) CHECK(abs(buf[2 * i]) <= 16423 + 1 && abs(buf[2 * i + 1]) <= 16423 + 1, "ceiling exceeded: %d %d", buf[2 * i], buf[2 * i + 1]);
    }
    // A unity ceiling (a build that wants no headroom) does not overflow either.
    Fade u(kUnityQ15, true);
    Inputs in; in.playing = true;
    for (int k = 0; k < 4; ++k) { for (int i = 0; i < kChunk; ++i) buf[2 * i] = buf[2 * i + 1] = (i & 1) ? 32767 : -32768; u.process(buf, kChunk, kChunk, in); }
    for (int i = 0; i < kChunk; ++i) CHECK(buf[2 * i] >= -32768 && buf[2 * i] <= 32767, "unity overflow");
  }

  // ----- steady state is a clean scale -----
  {
    Sim s(20000, 1000, false); s.playing = true; s.chunks(4);
    Sim ref(20000, 1000, false); ref.playing = true; ref.fade = Fade(kUnityQ15, false); ref.chunks(4);
    int worst = 0;
    for (size_t i = 2 * kChunk; i < s.frames(); ++i) {
      const double want = (double)ref.out[2 * i] * kCeilingQ15 / kUnityQ15;
      const int e = (int)fabs((double)s.out[2 * i] - want);
      if (e > worst) worst = e;
    }
    CHECK(worst <= 1, "steady-state error %d LSB", worst);
  }

  // ----- pause / resume sweeps: step bound at every phase offset -----
  {
    int worstStep = 0, bound = 0;
    for (int pauseAt = 0; pauseAt < 40; ++pauseAt) {
      Sim s; bound = stepBound(s);
      s.playing = true; s.chunks(3 + pauseAt / 7);
      // Pause lands on a chunk boundary but the phase of the waveform there varies.
      s.phase += pauseAt * 0.37;
      s.playing = false; s.chunks(4);
      const size_t pausedAt = (size_t)(3 + pauseAt / 7) * kChunk;
      const size_t end = lastNonZero(s.out, pausedAt, s.frames());
      CHECK(end == (size_t)-1 || end < pausedAt + kRampFrames, "pause: output still nonzero at +%zu frames", end - pausedAt);
      s.playing = true; s.chunks(4);
      const int m = maxStepAll(s); if (m > worstStep) worstStep = m;
      CHECK(m <= bound, "pause/resume step %d > bound %d (offset %d)", m, bound, pauseAt);
    }
    printf("pause/resume worst step %d (bound %d; a hard cut would be ~%d)\n", worstStep, bound, 30000 / 2);
  }

  // ----- fade-out and fade-in are monotone on DC; reversal mid-ramp is continuous -----
  {
    Sim s(20000, 200, false); s.dc = true; s.playing = true; s.chunks(3);
    const size_t n0 = s.frames();
    s.playing = false; s.chunks(2);
    for (size_t i = n0 + 1; i < s.frames(); ++i) CHECK(s.out[2 * i] <= s.out[2 * (i - 1)], "fade-out not monotone at %zu", i - n0);
    CHECK(s.out[2 * (n0 + kRampFrames + 2)] == 0 && s.out[2 * (n0 + kRampFrames + 2) + 1] == 0, "fade-out reaches digital zero");
    s.playing = true; const size_t n1 = s.frames(); s.chunks(2);
    for (size_t i = n1 + 1; i < n1 + kRampFrames; ++i) CHECK(s.out[2 * i] >= s.out[2 * (i - 1)], "fade-in not monotone at %zu", i - n1);
    // Reverse in the middle of a ramp, one frame at a time: 100 frames up, 60 down, 120 up, 150 down, 60 up.
    Fade f; Inputs in; std::vector<int16_t> o;
    const struct { int n; bool open; } legs[] = {{100, true}, {60, false}, {120, true}, {150, false}, {60, true}};
    for (const auto& leg : legs) {
      in.playing = leg.open;
      for (int i = 0; i < leg.n; ++i) { int16_t b[2] = {20000, 20000}; f.process(b, 1, leg.open ? 1 : 0, in); o.push_back(b[0]); }
    }
    int worst = 0;
    for (size_t i = 1; i < o.size(); ++i) worst = abs(o[i] - o[i - 1]) > worst ? abs(o[i] - o[i - 1]) : worst;
    CHECK(worst <= 52, "reversal step %d", worst);   // steepest point of a ramp: 20000 * 0.5012 * (pi/2)/320 = 49.2
  }

  // ----- seek: output decays to zero, the post-seek jump is invisible, then ramps back -----
  {
    int worstStep = 0;
    for (int variant = 0; variant < 12; ++variant) {
      Sim s; const int bound = stepBound(s);
      s.playing = true; s.chunks(5);
      s.phase += variant * 0.5;
      const size_t at = s.frames();
      s.seeking = true; s.chunks(3);                 // audio_pull returns zeros while seeking
      CHECK(lastNonZero(s.out, at + kRampFrames, s.frames()) == (size_t)-1, "seek: not silent after the ramp");
      s.jump(); s.seeking = false; s.chunks(5);      // new position, live data at once
      const int m = maxStepAll(s); if (m > worstStep) worstStep = m;
      CHECK(m <= bound, "seek step %d > bound %d", m, bound);
      CHECK(s.fade.rampPos() == kRampFrames, "seek: ramp reopened");
    }
    printf("seek worst step %d\n", worstStep);
  }

  // ----- track change while playing: live data of the NEW track arrives in the same chunk -----
  {
    int worstStep = 0;
    for (int variant = 0; variant < 12; ++variant) {
      Sim s; const int bound = stepBound(s);
      s.playing = true; s.chunks(5);
      s.phase += variant * 0.5;
      const size_t at = s.frames();
      s.jump(); s.serial++;                          // flush + open: the very next chunk is new audio
      s.chunks(6);
      const int m = maxStepAll(s); if (m > worstStep) worstStep = m;
      CHECK(m <= bound, "track-change step %d > bound %d", m, bound);
      // Gain went all the way to zero before the new track came in: a dip of about two ramps.
      bool sawZero = false;
      for (size_t i = at; i < at + 2 * kRampFrames + kChunk; ++i) if (s.out[2 * i] == 0 && s.out[2 * i + 1] == 0) sawZero = true;
      CHECK(sawZero, "track change never reached zero");
      CHECK(s.fade.rampPos() == kRampFrames && !s.fade.draining(), "track change: reopened");
    }
    printf("track-change worst step %d\n", worstStep);
  }

  // ----- corner cases -----
  {
    // Pause during a fade-out then resume (chunk boundary inside the ramp, via short data).
    Sim s; s.playing = true; s.chunks(4);
    s.playing = false; s.chunk();                    // ramp is 256/320 of the way down
    CHECK(s.fade.rampPos() == kRampFrames - kChunk, "ramp position after one chunk: %d", s.fade.rampPos());
    s.playing = true; s.chunks(3);
    CHECK(maxStepAll(s) <= stepBound(s), "pause during ramp then resume: step %d", maxStepAll(s));
    CHECK(s.fade.rampPos() == kRampFrames, "reopens");

    // Track change while paused: stays silent, then the new track fades in from zero.
    Sim p; p.playing = true; p.chunks(3); p.playing = false; p.chunks(3);
    p.serial++; p.jump(); p.chunks(2);
    CHECK(lastNonZero(p.out, 3 * kChunk + kRampFrames, p.frames()) == (size_t)-1, "paused track change must stay silent");
    p.playing = true; const size_t n = p.frames(); p.chunks(3);
    CHECK(abs(p.out[2 * n]) <= 2, "new track starts from silence, first sample %d", p.out[2 * n]);
    CHECK(maxStepAll(p) <= stepBound(p), "track change while paused step %d", maxStepAll(p));

    // Seek while paused (seeking asserted with playing false), then play.
    Sim q; q.playing = true; q.chunks(3); q.playing = false; q.chunks(2);
    q.seeking = true; q.chunks(2); q.jump(); q.seeking = false; q.chunks(2);
    CHECK(lastNonZero(q.out, 3 * kChunk + kRampFrames, q.frames()) == (size_t)-1, "seek while paused silent");
    q.playing = true; q.chunks(3);
    CHECK(maxStepAll(q) <= stepBound(q), "seek while paused step %d", maxStepAll(q));

    // Track change that arrives exactly when seeking ends and play resumes.
    Sim r; r.playing = true; r.chunks(3); r.seeking = true; r.chunks(1); r.serial++; r.jump(); r.seeking = false; r.chunks(4);
    CHECK(maxStepAll(r) <= stepBound(r), "serial+seek overlap step %d", maxStepAll(r));
    CHECK(r.fade.rampPos() == kRampFrames, "serial+seek overlap reopens");

    // Mute while playing: continuous fade to zero; unmute after the audio moved on does not step.
    Sim m; m.playing = true; m.chunks(3); m.muted = true; m.chunks(1);
    m.jump(); m.chunks(2);                           // ring is still consumed while muted
    CHECK(lastNonZero(m.out, 3 * kChunk + kRampFrames, m.frames()) == (size_t)-1, "muted output silent");
    m.muted = false; m.chunks(3);
    CHECK(maxStepAll(m) <= stepBound(m), "mute/unmute step %d", maxStepAll(m));

    // Events SHORTER than a ramp: the gain is still partly open when the live input returns from
    // somewhere else, so only the drain rule keeps the step bounded. (One chunk is 256 < 320 frames.)
    for (int kind = 0; kind < 2; ++kind) {
      int worst = 0;
      for (int variant = 0; variant < 12; ++variant) {
        Sim e; e.playing = true; e.chunks(5); e.phase += variant * 0.5;
        if (kind == 0) { e.seeking = true; e.chunks(1); e.jump(); e.seeking = false; }
        else { e.muted = true; e.chunks(1); e.jump(); e.muted = false; }
        e.chunks(5);
        const int m = maxStepAll(e); if (m > worst) worst = m;
        CHECK(m <= stepBound(e), "short %s step %d > bound %d", kind ? "mute" : "seek", m, stepBound(e));
        CHECK(e.fade.rampPos() == kRampFrames, "short %s reopens", kind ? "mute" : "seek");
      }
    }

    // Leaving the wired route: wired=false with no data fades out the held sample.
    Sim w; w.playing = true; w.chunks(3);
    Fade& wf = w.fade; int16_t b[kChunk * 2]; memset(b, 0, sizeof(b));
    Inputs leave; leave.playing = false; leave.wired = false;
    const int before = wf.rampPos();
    wf.process(b, kChunk, 0, leave); wf.process(b, kChunk, 0, leave);
    CHECK(before == kRampFrames && wf.silent() && b[2 * (kChunk - 1)] == 0, "route-change fade-out completes within two chunks");

    // Underrun: partial chunk, then empty chunks, then live data returns (continuous source).
    Sim u; u.playing = true; u.chunks(3);
    u.chunk(97);                                     // ring ran dry mid-chunk
    u.chunk(0); u.chunk(0);
    CHECK(lastNonZero(u.out, 3 * kChunk + 97 + kRampFrames, u.frames()) == (size_t)-1, "underrun decays to digital zero");
    u.chunks(3);                                     // phase continues: the stream resumed where it left off
    CHECK(maxStepAll(u) <= stepBound(u), "underrun recovery step %d (bound %d)", maxStepAll(u), stepBound(u));
    // Underrun refills before the ramp finishes: reversal, still continuous.
    Sim v; v.playing = true; v.chunks(3); v.chunk(10); v.chunk(0); v.chunks(3);
    CHECK(maxStepAll(v) <= stepBound(v), "short underrun step %d", maxStepAll(v));

    // Out-of-range valid counts are clamped, not trusted.
    Fade f; int16_t bb[kChunk * 2]; Inputs in; in.playing = true;
    for (int i = 0; i < kChunk * 2; ++i) bb[i] = 1000;
    f.process(bb, kChunk, kChunk * 10, in);
    for (int i = 0; i < kChunk * 2; ++i) bb[i] = 1000;
    f.process(bb, kChunk, -5, in);
    CHECK(bb[2 * (kChunk - 1)] == 0, "negative valid treated as zero (stage was silent)");

    // Reset gives digital silence and forgets the serial.
    f.reset(); CHECK(f.silent() && !f.draining(), "reset");
  }

  // ----- chunk size does not matter when the inputs are constant -----
  {
    const int sizes[] = {1, 77, 256, 1000};
    std::vector<int16_t> ref;
    for (int sz : sizes) {
      Fade f; Inputs in; in.playing = true; std::vector<int16_t> o;
      for (int done = 0; done < 3000; done += sz) {
        const int n = (3000 - done) < sz ? (3000 - done) : sz;
        std::vector<int16_t> b(n * 2);
        for (int i = 0; i < n; ++i) { b[2 * i] = (int16_t)(((done + i) * 37) % 20000 - 10000); b[2 * i + 1] = (int16_t)(-b[2 * i]); }
        f.process(b.data(), n, n, in);
        o.insert(o.end(), b.begin(), b.end());
      }
      if (ref.empty()) ref = o; else CHECK(o == ref, "chunk size %d changes the output", sz);
    }
  }

  // ----- dither: off is exact, on stays within 2 LSB, silence stays digital zero, -----
  {
    Sim a(12345, 1000, false), b(12345, 1000, true);
    a.playing = b.playing = true; a.chunks(6); b.chunks(6);
    int worst = 0; long long sum = 0; size_t nSamples = 0; bool differs = false;
    for (size_t i = 2 * kChunk; i < a.out.size(); ++i) {
      const int d = (int)b.out[i] - (int)a.out[i];
      if (abs(d) > worst) worst = abs(d);
      if (d) differs = true;
      sum += d; ++nSamples;
    }
    CHECK(worst <= 2, "dither error %d LSB", worst);
    CHECK(differs, "dither has no effect");
    CHECK(fabs((double)sum / (double)nSamples) < 0.1, "dither bias %.3f", (double)sum / (double)nSamples);
    a.playing = b.playing = false; const size_t n = b.frames(); b.chunks(3);
    CHECK(lastNonZero(b.out, n + kRampFrames, b.frames()) == (size_t)-1, "dither leaves silence digital zero");
    Sim c(12345, 1000, true); c.playing = true; c.chunks(6);
    Sim d2(12345, 1000, true); d2.playing = true; d2.chunks(6);
    CHECK(c.out == d2.out, "dither is deterministic from reset");
  }

  if (failures) { printf("WIRED_FADE_FAILED %d\n", failures); return 1; }
  printf("WIRED_FADE_OK\n");
  return 0;
}
