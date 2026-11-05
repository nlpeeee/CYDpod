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
    std::printf("PASS all\n");
    return 0;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "FAIL scroll core: %s\n", ex.what());
    return 1;
  }
}
