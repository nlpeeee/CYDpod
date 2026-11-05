// Host test for firmware/cydpod_player/app/scroll_core.h (CY-96). Run from the repo root, or pass the CSV path.
#include "scroll_core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cydscroll;

static void check(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

struct Row { float v, t, pos, vel, dur, total; };

int main(int argc, char** argv) {
  try {
    const char* path = argc > 1 ? argv[1] : "tests/data/aosp_fling.csv";
    std::vector<Row> rows;
    {
      FILE* f = std::fopen(path, "r");
      check(f != nullptr, std::string("cannot open ") + path);
      char line[256];
      std::fgets(line, sizeof line, f);  // header
      while (std::fgets(line, sizeof line, f)) {
        Row r;
        if (std::sscanf(line, "%f,%f,%f,%f,%f,%f", &r.v, &r.t, &r.pos, &r.vel, &r.dur, &r.total) == 6) rows.push_back(r);
      }
      std::fclose(f);
    }
    check(rows.size() > 500, "golden rows loaded");

    {  // Goldens: position < 1 px or 0.5% of total; velocity within 2% of peak + 5 px/s; duration/total tolerances.
      float worstPos = 0, worstDur = 0, worstTot = 0;
      for (const Row& r : rows) {
        Fling f;
        f.start(0, r.v, -1e7f, 1e7f, 0);
        const float p = f.position((uint32_t)r.t);
        const float tol = std::fmax(1.0f, 0.005f * r.total);
        worstPos = std::fmax(worstPos, std::fabs(p - r.pos));
        check(near(p, r.pos, tol), "position v=" + std::to_string(r.v) + " t=" + std::to_string(r.t) +
                                        " got " + std::to_string(p) + " want " + std::to_string(r.pos));
        const float vt = 0.02f * r.v + 5.0f;
        check(near(f.velocity((uint32_t)r.t), r.vel, vt), "velocity v=" + std::to_string(r.v) + " t=" + std::to_string(r.t));
        worstDur = std::fmax(worstDur, std::fabs(f.durationMs() - r.dur));
        worstTot = std::fmax(worstTot, std::fabs(f.finalY() - r.total));
        check(near(f.durationMs(), r.dur, 33.0f), "duration (1 frame) v=" + std::to_string(r.v));
        check(near(f.finalY(), r.total, std::fmax(1.0f, 0.005f * r.total)), "finalY v=" + std::to_string(r.v));
      }
      std::printf("PASS goldens (%zu rows; worst pos err %.3f px, duration err %.2f ms, final err %.3f px)\n",
                  rows.size(), worstPos, worstDur, worstTot);
    }

    {  // Report table sanity (section 1.1) and sign symmetry.
      const float vs[] = {200, 1000, 4000, 7150};
      const float dur[] = {184, 603, 1674, 2567}, dist[] = {13, 211, 2343, 6423};
      for (int i = 0; i < 4; ++i) {
        Fling f; f.start(0, vs[i], -1e7f, 1e7f, 0);
        check(near(f.durationMs(), dur[i], 3), "report duration");
        check(near(f.finalY(), dist[i], 2 + 0.003f * dist[i]), "report distance");
        Fling g; g.start(0, -vs[i], -1e7f, 1e7f, 0);
        check(near(g.finalY(), -f.finalY(), 1e-3f) && near(g.position(100), -f.position(100), 1e-3f), "negative symmetric");
      }
      std::printf("PASS report table and sign symmetry\n");
    }

    {  // Frame-rate independence: frame loops at 16, 33 and 50 ms record positions; at instants two loops
       // share (multiples of 400, 528, 1650 ms) the recorded values are identical.
      Fling f; f.start(100, 3000, -1e7f, 1e7f, 5000);
      static float rec[3][3300];
      const uint32_t periods[] = {16, 33, 50};
      for (int k = 0; k < 3; ++k)
        for (uint32_t t = 0; t < 3300; t += periods[k]) rec[k][t] = f.position(5000 + t);  // the frame loop
      int compared = 0;
      for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b)
          for (uint32_t t = 0; t < 3300; ++t)
            if (t % periods[a] == 0 && t % periods[b] == 0) {
              check(rec[a][t] == rec[b][t], "loops disagree at t=" + std::to_string(t));
              ++compared;
            }
      check(compared >= 10, "enough common instants compared");
      std::printf("PASS frame-rate independence (16, 33, 50 ms; %d common instants)\n", compared);
    }

    {  // Monotonic, front-loaded, velocity matches slope numerically.
      Fling f; f.start(0, 2000, -1e7f, 1e7f, 0);
      float prev = -1;
      for (uint32_t t = 0; t <= 1100; t += 4) {
        const float p = f.position(t);
        check(p >= prev, "monotonic");
        prev = p;
        if (t > 0 && t < 950) {
          const float num = (f.position(t + 1) - f.position(t - 1)) * 500.0f;
          check(near(num, f.velocity(t), 0.02f * 2000 + 3), "velocity is d(position)/dt at t=" + std::to_string(t));
        }
      }
      check(near(f.velocity(0), 2000, 20), "initial speed equals release velocity");
      check(f.position((uint32_t)(0.57f * f.durationMs())) >= 0.88f * f.finalY(), "front-loaded: ~90% by t=0.57");
      check(f.finished((uint32_t)f.durationMs() + 1) && !f.finished(10), "finished flag");
      std::printf("PASS monotonic, front-loaded, velocity consistent\n");
    }

    {  // Zero, sub-minimum, negative and over-max velocity.
      Fling z; z.start(50, 0, 0, 1000, 0);
      check(z.finished(0) && z.position(500) == 50 && z.velocity(0) == 0, "zero velocity: no motion");
      Fling s; s.start(50, 30, 0, 1000, 0);
      check(s.finished(0) && s.position(100) == 50, "below min fling: no motion");
      Fling n; n.start(500, -1000, 0, 1000, 0);
      check(n.position(50) < 500 && n.finalY() < 500 && n.finalY() > 0, "negative moves down");
      Fling hi; hi.start(0, 50000, -1e7f, 1e7f, 0);
      Fling mx; mx.start(0, 7150, -1e7f, 1e7f, 0);
      check(near(hi.finalY(), mx.finalY(), 1e-3f) && near(hi.durationMs(), mx.durationMs(), 1e-3f), "over-max clamped to 7150");
      std::printf("PASS zero / negative / over-max velocity\n");
    }

    {  // Clamp and edge: never beyond bound + overfling; ends exactly on the bound; springback is monotonic.
      Fling f; f.start(900, 3000, 0, 1000, 0);
      float maxP = 0, prevAfter = 1e9f; bool peaked = false;
      for (uint32_t t = 0; t < 3000; ++t) {
        const float p = f.position(t);
        check(p <= 1000 + f.cfg().overflingPx + 0.01f, "never beyond overfling cap");
        check(p >= 900 - 0.01f, "upward fling never moves down");
        maxP = std::fmax(maxP, p);
        if (p >= maxP - 1e-4f) peaked = false; else peaked = true;
        if (peaked) { check(p <= prevAfter + 0.01f, "springback monotonic"); prevAfter = p; }
      }
      check(maxP > 1000.5f && maxP <= 1005.01f, "overfling visible but capped at 5 px");
      check(f.finalY() == 1000 && f.position(100000) == 1000 && f.finished(100000), "ends on the bound");
      Fling lo; lo.start(100, -4000, 0, 1000, 0);
      float minP = 1e9f;
      for (uint32_t t = 0; t < 3000; ++t) minP = std::fmin(minP, lo.position(t));
      check(minP < -0.5f && minP >= -5.01f && lo.finalY() == 0 && lo.position(100000) == 0, "lower bound symmetric");
      Fling out; out.start(1030, 0, 0, 1000, 0);  // released beyond the bound: springs back
      check(near(out.position(0), 1030, 1e-3f) && out.position(10000) == 1000 && out.finished(10000), "springback from outside");
      float pp = 1031;
      for (uint32_t t = 0; t < 400; t += 4) { const float p = out.position(t); check(p <= pp + 1e-3f && p >= 1000, "outside monotonic"); pp = p; }
      Fling short1; short1.start(0, 3000, 0, 20, 0);  // bound inside the fling distance
      check(short1.finalY() == 20 && short1.position(5000) == 20, "short list clamps at max");
      std::printf("PASS clamp, overfling cap and springback\n");
    }

    {  // Flywheel: same sign adds velocity, opposite sign replaces.
      Fling f; f.start(0, 1000, -1e7f, 1e7f, 0);
      const float v0 = f.velocity(100);
      const float base = f.finalY();
      f.flywheel(1000, 100);
      check(f.velocity(100) > 1000 + 0.5f * v0 && f.finalY() > base, "same sign accumulates");
      Fling g; g.start(0, 1000, -1e7f, 1e7f, 0);
      g.flywheel(-1000, 100);
      check(g.finalY() < g.position(100), "opposite sign replaces");
      std::printf("PASS flywheel\n");
    }

    {  // Edge spring: rubber band, settles to 0, bounded envelope, deterministic at any sampling.
      Edge e;
      const float range = e.cfg().edgeRangePx;
      check(e.dampen(0) == 0 && e.dampen(-5) == 0, "no pull, no offset");
      check(e.dampen(20) > 0 && e.dampen(20) < 20 && e.dampen(100000) < range, "dampened below pull and range");
      check(e.dampen(40) > e.dampen(20), "dampen increasing");
      e.settle(30, 0, 1000);
      check(near(e.offset(1000), 30, 1e-3f) && !e.finished(1000), "starts at the release offset");
      
      for (uint32_t t = 1000; t < 6000; t += 8) {
        const float x = e.offset(t);
        check(std::fabs(x) <= 30.01f, "envelope never exceeds the start");
      }
      check(e.offset(1500) < 30, "decays");
      check(e.finished(1000 + 3000) && e.offset(1000 + 6000) == 0, "settles to zero");
      Edge z; z.settle(0.05f, 0, 0);
      check(z.finished(0) && z.offset(0) == 0, "tiny offset is already settled");
      Edge o; o.settle(0, 800, 0);  // outward velocity from rest: goes out then returns
      float mx = 0; for (uint32_t t = 0; t < 4000; t += 4) mx = std::fmax(mx, o.offset(t));
      check(mx > 20 && o.finished(4000), "velocity kick moves out and settles");
      std::printf("PASS edge spring\n");
    }
    {  // Velocity / Tracker on synthetic drags (integer coordinates like the touch panel). Tolerance: LSQ2 8% + 25 px/s, Impulse 15% + 25 px/s (noisier under jitter).
      struct Case { const char* name; float rate; float (*y)(float); float (*v)(float); int jit; float endS; };
      static const Case cases[] = {
        {"const +1000 @60", 60, [](float t) { return 500 + 1000 * t; }, [](float) { return 1000.0f; }, 0, 0.30f},
        {"const +1000 @100", 100, [](float t) { return 500 + 1000 * t; }, [](float) { return 1000.0f; }, 0, 0.30f},
        {"const -2500 @60", 60, [](float t) { return 900 - 2500 * t; }, [](float) { return -2500.0f; }, 0, 0.25f},
        {"const +300 @100", 100, [](float t) { return 100 + 300 * t; }, [](float) { return 300.0f; }, 0, 0.40f},
        {"accel 4000 @60", 60, [](float t) { return 200 + 2000 * t * t; }, [](float t) { return 4000 * t; }, 0, 0.30f},
        {"accel 4000 @100", 100, [](float t) { return 200 + 2000 * t * t; }, [](float t) { return 4000 * t; }, 0, 0.30f},
        {"decel 5000 @60", 60, [](float t) { return 100 + 2000 * t - 2500 * t * t; }, [](float t) { return 2000 - 5000 * t; }, 0, 0.30f},
        {"decel 5000 @100", 100, [](float t) { return 100 + 2000 * t - 2500 * t * t; }, [](float t) { return 2000 - 5000 * t; }, 0, 0.30f},
        {"jitter1 +1500 @60", 60, [](float t) { return 100 + 1500 * t; }, [](float) { return 1500.0f; }, 1, 0.30f},
        {"jitter1 +1500 @100", 100, [](float t) { return 100 + 1500 * t; }, [](float) { return 1500.0f; }, 1, 0.30f},
        {"jitter1 -800 @100", 100, [](float t) { return 700 - 800 * t; }, [](float) { return -800.0f; }, 1, 0.30f},
      };
      uint32_t rng = 12345;
      float worstL = 0, worstI = 0;
      for (const Case& c : cases) {
        Velocity tl, ti;  // CY-107: Tracker uses FlickEstimator; LSQ2/Impulse are still checked directly
        const uint32_t dt = (uint32_t)(1000.0f / c.rate + 0.5f);
        uint32_t tEnd = 0;
        for (uint32_t ms = 0; ms <= (uint32_t)(c.endS * 1000); ms += dt) {
          float y = std::floor(c.y(ms * 0.001f) + 0.5f);
          if (c.jit) { rng = rng * 1664525u + 1013904223u; y += (float)((int)((rng >> 16) % 3) - 1); }
          tl.add(ms, y); ti.add(ms, y);
          tEnd = ms;
        }
        const float want = c.v(tEnd * 0.001f), gl = tl.get(Strategy::Lsq2), gi = ti.get(Strategy::Impulse);
        const float tol = 0.08f * std::fabs(want) + 25.0f;
        worstL = std::fmax(worstL, std::fabs(gl - want) / (std::fabs(want) + 1)); worstI = std::fmax(worstI, std::fabs(gi - want) / (std::fabs(want) + 1));
        std::printf("  vel %-20s true %8.1f  LSQ2 %8.1f (%+6.1f)  Impulse %8.1f (%+6.1f)\n", c.name, want, gl, gl - want, gi, gi - want);
        check(near(gl, want, tol), std::string("LSQ2 velocity ") + c.name);
        check(near(gi, want, 0.15f * std::fabs(want) + 25.0f), std::string("Impulse velocity ") + c.name);
      }
      std::printf("PASS velocity on synthetic drags (worst relative error LSQ2 %.1f%%, Impulse %.1f%%)\n", 100 * worstL, 100 * worstI);
    }

    {  // Velocity rules: horizon, stale-sample clearing, clamp, degree reduction, ring wrap.
      Velocity v;
      check(v.windowCount() == 0 && v.get(Strategy::Lsq2) == 0 && v.get(Strategy::Impulse) == 0, "empty = 0");
      v.add(0, 0); check(v.get(Strategy::Lsq2) == 0 && v.get(Strategy::Impulse) == 0, "one sample = 0");
      v.add(10, 10); check(near(v.get(Strategy::Lsq2), 1000, 1) && near(v.get(Strategy::Impulse), 1000, 1), "two samples = slope");
      v.add(60, 20);  // 50 ms gap > 40: history cleared, one sample left
      check(v.count() == 1 && v.get(Strategy::Lsq2) == 0, "gap > 40 ms clears the history");
      Velocity w;  // fast then slow: only the last 100 ms count
      for (uint32_t ms = 0; ms <= 100; ms += 10) w.add(ms, 20.0f * ms);        // 20000 px/s, clamped anyway
      for (uint32_t ms = 110; ms <= 300; ms += 10) w.add(ms, 2000 + 0.1f * (ms - 100));  // 100 px/s
      check(near(w.get(Strategy::Lsq2), 100, 5) && near(w.get(Strategy::Impulse), 100, 5), "old samples outside the 100 ms horizon ignored");
      Velocity f;
      for (uint32_t ms = 0; ms <= 60; ms += 10) f.add(ms, 100.0f * ms);        // 100000 px/s
      check(f.get(Strategy::Lsq2) == ScrollCfg().maxFling && f.get(Strategy::Impulse) == ScrollCfg().maxFling, "clamped to +maxFling");
      Velocity g;
      for (uint32_t ms = 0; ms <= 60; ms += 10) g.add(ms, -100.0f * ms);
      check(g.get(Strategy::Lsq2) == -ScrollCfg().maxFling, "clamped to -maxFling");
      Velocity r;  // more than 20 samples: ring wraps, newest 100 ms still correct
      for (uint32_t ms = 0; ms <= 400; ms += 10) r.add(ms, 0.5f * ms);
      check(r.count() == 20 && r.windowCount() == 11 && near(r.get(Strategy::Lsq2), 500, 2) && near(r.get(Strategy::Impulse), 500, 2), "ring wrap");
      std::printf("PASS velocity rules (horizon 100 ms, stale 40 ms, clamp, ring of 20)\n");
    }

    {  // Tracker: slop, claim without jump, tap vs drag, fling start, hold-then-lift, tap-to-stop.
      ScrollCfg cfg; Tracker t;
      t.down(0, 300);
      check(!t.move(10, 303) && !t.move(20, 306) && t.delta() == 0 && !t.claimed(), "within slop: not claimed");
      check(!t.move(30, 307), "exactly slop: not claimed");
      check(t.move(40, 308) && near(t.delta(), 1.0f, 1e-4f), "claim: delta is overshoot past slop (no jump)");
      check(t.move(50, 320) && near(t.delta(), 13.0f, 1e-4f), "1:1 tracking after the claim");
      Tracker c2; c2.down(0, 300); c2.move(10, 292); c2.move(20, 290);
      check(c2.claimed() && near(c2.delta(), -3.0f, 1e-4f), "upward claim: first delta = 290 - 293");
      Fling fl;
      Tracker tap; tap.down(0, 100); tap.move(15, 102); tap.move(30, 103);
      check(!tap.up(50, fl, 0, 0, 1000) && tap.isTap() && fl.finished(50), "slop-sized wiggle is a tap, no fling");
      Tracker dr; dr.down(0, 400);
      for (uint32_t ms = 10; ms <= 100; ms += 10) dr.move(ms, 400 - 2.0f * ms);  // 2000 px/s upward
      const bool started = dr.up(105, fl, 800, 0, 100000);
      check(started && !dr.isTap() && dr.claimed() && !fl.finished(105) && fl.finalY() > 800, "fast drag starts a fling in the content direction");
      check(near(dr.releaseVelocity(), -2000, 60), "release velocity measured");
      Tracker slow; slow.down(0, 400);
      for (uint32_t ms = 10; ms <= 400; ms += 10) slow.move(ms, 400 + 0.04f * ms);  // 40 px/s, past the slop, under minFling
      Fling f2; check(!slow.up(405, f2, 50, 0, 1000) && f2.finished(405) && !slow.isTap(), "slow drag: no fling, not a tap");
      Tracker hold; hold.down(0, 400);
      for (uint32_t ms = 10; ms <= 100; ms += 10) hold.move(ms, 400 + 2.0f * ms);
      for (uint32_t ms = 110; ms <= 200; ms += 10) hold.move(ms, 400 + 200);  // finger rests, same y reports
      Fling f3; check(!hold.up(205, f3, 50, 0, 100000) && f3.finished(205) && !hold.isTap(), "hold-then-lift: no fling");
      Fling f4;
      Tracker late2; late2.down(0, 400);
      for (uint32_t ms = 10; ms <= 100; ms += 10) late2.move(ms, 400 + 2.0f * ms);
      check(late2.up(139, f4, 50, 0, 100000) && !late2.isTap(), "up 39 ms after the last sample still flings");
      Tracker late3; late3.down(0, 400);
      for (uint32_t ms = 10; ms <= 100; ms += 10) late3.move(ms, 400 + 2.0f * ms);
      Fling f5; check(late3.up(141, f5, 50, 0, 100000) && !f5.finished(141), "CY-107: up 41 ms after the last sample still flings (gap allowance 100 ms)");
      Tracker late4; late4.down(0, 400);
      for (uint32_t ms = 10; ms <= 100; ms += 10) late4.move(ms, 400 + 2.0f * ms);
      Fling f6; check(!late4.up(201, f6, 50, 0, 100000) && f6.finished(201), "CY-107: up 101 ms after the last movement: rest, no fling");
      // Tap-to-stop: a running fling is stopped in place and the touch is a drag, not a tap.
      Fling run; run.start(0, 3000, -1e7f, 1e7f, 1000);
      const float at = run.position(1200);
      Tracker ts; ts.down(1200, 500, &run);
      check(ts.stoppedFling() && !ts.isTap() && run.finished(1200) && near(run.position(5000), at, 1e-3f), "touch-down aborts the fling in place");
      check(ts.move(1210, 502) && near(ts.delta(), 2.0f, 1e-4f) && !ts.isTap(), "after tap-to-stop the touch is a drag (claimed, no slop)");
      Fling idle; Tracker tn; tn.down(0, 5, &idle);
      check(!tn.stoppedFling() && tn.isTap(), "down with no running fling: plain tap candidate");
      Fling done; done.start(0, 3000, -1e7f, 1e7f, 0); Tracker td; td.down(100000, 5, &done);
      check(!td.stoppedFling(), "down after the fling finished: not a stop");
      std::printf("PASS tracker (slop/claim no-jump, tap vs drag, fling start, hold-then-lift, late up, tap-to-stop)\n");
      (void)cfg;
    }

    {  // CY-107 FlickEstimator: raw positions, time-normalised smoothing, gap allowance, reversal, travel/velocity minimums.
      FlickEstimator e;  // 1000 px/s then 500 px/s: v follows 1-exp(-dt/50ms) toward the new segment speed
      e.add(0, 0); e.add(20, 20); e.add(40, 40);
      check(near(e.smoothed(), 1000, 1e-2f), "constant segments: v = segment speed");
      e.add(60, 50);  // 500 px/s for 20 ms
      check(near(e.smoothed(), 1000 + (1 - std::exp(-0.4f)) * (500 - 1000), 0.01f), "smoothing weight 1-exp(-dt/0.050)");
      FlickEstimator g1, g2;  // gap allowance: 100 ms bridged, 101 ms restarts
      g1.add(0, 0); g1.add(30, 30); g1.add(130, 80);
      check(g1.windowCount() == 3 && g1.release(130) > 0, "100 ms gap tolerated");
      g2.add(0, 0); g2.add(30, 30); g2.add(131, 80);
      check(g2.windowCount() == 1 && g2.release(131) == 0, "101 ms gap restarts the estimate");
      FlickEstimator a;  // dt < 20 ms aggregates into the next segment; duplicates and stale timestamps are ignored
      a.add(0, 0); a.add(10, 10); check(a.smoothed() == 0, "10 ms sample is aggregated, no segment yet");
      a.add(20, 20); check(near(a.smoothed(), 1000, 1e-2f), "aggregated segment spans anchor to newest sample");
      a.add(20, 99); a.add(15, 99); check(near(a.smoothed(), 1000, 1e-2f), "duplicate/stale timestamps ignored");
      FlickEstimator r;  // reversal resets v and the travel origin
      r.add(0, 0); r.add(20, 20); r.add(40, 40); r.add(60, 36); r.add(80, 32);
      check(near(r.smoothed(), -200, 1e-2f) && r.release(80) == 0, "reversal: v is the new direction, travel since the turn < 12 px");
      r.add(100, 24); r.add(120, 16);
      check(r.release(120) < -200 && r.release(120) > -1000, "after enough travel the new direction flings");
      FlickEstimator m;  // minimum travel 12 px, minimum velocity 80 px/s
      m.add(0, 0); m.add(20, 11); check(m.release(20) == 0, "11 px of travel: no fling");
      m.add(40, 22); check(m.release(40) > 0, "22 px: fling");
      FlickEstimator slowE; slowE.add(0, 0); slowE.add(100, 7); slowE.add(200, 14);
      check(slowE.release(200) == 0, "70 px/s: below the 80 px/s minimum");
      FlickEstimator rest;  // rest: > 100 ms since the last > 2 px movement
      rest.add(0, 0); rest.add(20, 20); rest.add(40, 40); rest.add(60, 41);
      check(rest.release(140) > 0 && rest.release(141) == 0, "rest timer runs from the last > 2 px movement (1 px jitter does not restart it)");
      check(rest.release(140, 100) > 0 && rest.release(140, 141) == 0, "optional rest clock (poll that noticed the lift)");
      // Tracker: claim and velocity use RAW positions; delta() uses the filtered ones with no jump at the claim.
      Tracker t; t.down(0, 300, 300.0f);
      check(!t.move(20, 300, 306) && !t.claimed(), "raw 6 px: inside the slop (filtered unmoved)");
      check(t.move(40, 300, 312) && t.claimed() && t.delta() == 0, "raw 12 px claims; filtered lags: delta 0 at the claim, no jump");
      t.move(60, 310, 330);
      check(near(t.delta(), 10.0f, 1e-4f), "delta keeps following the filtered y");
      Fling fl; check(t.up(70, fl, 0, 0, 1e6f) && t.releaseVelocity() > 400, "raw velocity (300..900 px/s segments) releases, not the lagging filtered one");
      Tracker nf; nf.down(0, 300, 300.0f); nf.move(20, 300, 312);
      Coast cc; check(nf.claimed() && nf.up(30, cc, 100, 0, 1e6f) && !cc.finished(30) && cc.finalY() < 100 + 1e-3f, "Coast overload of up(): content moves opposite to the finger");
    }

    {  // CY-107 Coast: closed form, speed-dependent friction, cap, clamps, minimum animation, stop.
      Coast slow; slow.start(1000, 300, 0, 1e7f, 0);  // 300 px/s < 10 rows/s: tau = 0.32 s, stops at 40 px/s
      check(near(slow.finalY() - 1000, 0.32f * (300 - 40), 0.01f) && near(slow.durationMs(), 320 * std::log(300.0f / 40), 1.0f), "slow region: x = tau (v0 - vStop), t = tau ln(v0/vStop)");
      Coast mid; mid.start(0, 1200, -1e7f, 1e7f, 0);  // 30 rows/s: crosses into the slow region
      Coast fast; fast.start(0, 4800, -1e7f, 1e7f, 0);  // the 120 rows/s cap
      Coast over; over.start(0, 90000, -1e7f, 1e7f, 0);
      check(near(over.finalY(), fast.finalY(), 1e-2f) && near(over.durationMs(), fast.durationMs(), 1e-2f), "release speed capped at 120 rows/s");
      check(slow.finalY() - 1000 < mid.finalY() && mid.finalY() < fast.finalY(), "distance rises with speed");
      std::printf("  coast rows/duration: 300 px/s %.1f/%.2f s, 1200 px/s %.1f/%.2f s, cap 4800 px/s %.1f/%.2f s\n",
                  (slow.finalY() - 1000) / 40, slow.durationMs() / 1000, mid.finalY() / 40, mid.durationMs() / 1000, fast.finalY() / 40, fast.durationMs() / 1000);
      float prev = -1, prevV = 1e9f;  // monotone position, non-increasing speed, velocity is the slope, ends at finalY
      for (uint32_t t = 0; t <= (uint32_t)fast.durationMs() + 20; t += 9) {
        const float p = fast.position(t), v = fast.velocity(t);
        check(p >= prev && v <= prevV + 1e-3f && v >= 0, "coast monotone, decelerating");
        if (t > 20 && t + 20 < (uint32_t)fast.durationMs()) check(near((fast.position(t + 1) - fast.position(t - 1)) * 500.0f, v, 0.01f * v + 3.0f), "velocity is d(position)/dt");
        prev = p; prevV = v;
      }
      check(fast.finished((uint32_t)fast.durationMs() + 1) && fast.position(1000000) == fast.finalY() && fast.velocity(1000000) == 0, "coast ends at finalY");
      static float rec[3][3300];  // frame-rate independence: positions are a pure function of time
      const uint32_t periods[] = {16, 33, 50};
      for (int k = 0; k < 3; ++k) for (uint32_t t = 0; t < 3300; t += periods[k]) rec[k][t] = mid.position(t);
      for (uint32_t t = 0; t < 3300; t += 33 * 50) check(rec[1][t] == rec[2][t], "33 ms and 50 ms loops agree at common instants");
      Coast dn; dn.start(1000, -2400, 0, 1e7f, 0);
      check(dn.finalY() < 1000 && dn.position(50) < 1000, "negative velocity moves down");
      Coast hi; hi.start(900, 3000, 0, 1000, 0);  // list end inside the throw: hard clamp, no overfling, ends there
      float maxP = 0; for (uint32_t t = 0; t < 8000; t += 3) maxP = std::fmax(maxP, hi.position(t));
      check(maxP <= 1000 && hi.finalY() == 1000 && hi.position(100000) == 1000 && hi.finished((uint32_t)hi.durationMs() + 1) && hi.durationMs() < 2000, "stops at the list end");
      Coast lo; lo.start(10, -3000, 0, 1000, 0);
      check(lo.finalY() == 0 && lo.position(100000) == 0, "stops at the list start");
      Coast edge; edge.start(1000, 3000, 0, 1000, 0);
      check(edge.finished(0) && edge.position(50) == 1000, "already at the end: no motion");
      Coast tiny; tiny.start(500, 42, 0, 1e6f, 100);  // just over the stop speed: a sub-pixel throw still lasts one frame
      check(!tiny.finished(100) && !tiny.finished(120) && tiny.finished(133) && tiny.durationMs() >= 33 && tiny.position(120) >= 500, "minimum animation: one frame");
      Coast z; z.start(500, 40, 0, 1e6f, 0); check(z.finished(0) && z.position(10) == 500, "at or below the stop speed: no motion");
      Coast st; st.start(0, 2000, -1e7f, 1e7f, 0); const float at = st.position(300); st.stop(300);
      check(st.finished(300) && near(st.position(5000), at, 1e-3f), "stop() freezes in place");
      std::printf("PASS estimator and coast (closed form, cap, clamps, rest, raw claim)\n");
    }
    std::printf("PASS all\n");
    return 0;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "FAIL scroll core: %s\n", ex.what());
    return 1;
  }
}
