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
        Tracker tl(ScrollCfg(), Strategy::Lsq2), ti(ScrollCfg(), Strategy::Impulse);
        const uint32_t dt = (uint32_t)(1000.0f / c.rate + 0.5f);
        uint32_t tEnd = 0;
        for (uint32_t ms = 0; ms <= (uint32_t)(c.endS * 1000); ms += dt) {
          float y = std::floor(c.y(ms * 0.001f) + 0.5f);
          if (c.jit) { rng = rng * 1664525u + 1013904223u; y += (float)((int)((rng >> 16) % 3) - 1); }
          if (ms == 0) { tl.down(ms, y); ti.down(ms, y); } else { tl.move(ms, y); ti.move(ms, y); }
          tEnd = ms;
        }
        const float want = c.v(tEnd * 0.001f), gl = tl.velocity(), gi = ti.velocity();
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
      Fling f5; check(!late3.up(141, f5, 50, 0, 100000) && f5.finished(141), "up 41 ms after the last sample: no fling");
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
      std::printf("PASS tracker (slop/claim no-jump, tap vs drag, fling start, hold-then-lift, tap-to-stop)\n");
      (void)cfg;
    }
    std::printf("PASS all\n");
    return 0;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "FAIL scroll core: %s\n", ex.what());
    return 1;
  }
}
