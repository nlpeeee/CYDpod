// Runs the production press classifier (firmware/cydpod_player/app/touch_gesture.h)
// against simulated finger contacts polled at fast and delayed intervals.
#include "touch_gesture.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

struct Outcome { int taps = 0, longs = 0, recals = 0, downs = 0; uint32_t endAt = 0; int ex = -1, ey = -1; };

static void check(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }

// Finger on the glass for [start, start+duration). Polls happen at `first`, then
// every `period` ms until `until`. x drifts by `wobble` px on alternate samples.
static Outcome run(cydtouch::PressTracker& t, uint32_t start, uint32_t duration, uint32_t first, uint32_t period,
                   uint32_t until, int x = 100, int y = 200, int wobble = 0) {
  Outcome o;
  bool wasDown = t.down();
  int n = 0;
  for (uint32_t now = first; now <= until; now += period, ++n) {
    const bool valid = now >= start && now < start + duration;
    int ex = -1, ey = -1;
    const TouchEvent ev = t.update(now, valid, x + ((n & 1) ? wobble : 0), y, &ex, &ey);
    if (!wasDown && t.down()) ++o.downs;
    wasDown = t.down();
    if (ev == TOUCH_TAP) ++o.taps;
    if (ev == TOUCH_LONG_PRESS) ++o.longs;
    if (ev == TOUCH_RECALIBRATE) ++o.recals;
    if (ev != TOUCH_NONE) { o.endAt = now; o.ex = ex; o.ey = ey; }
  }
  return o;
}

int main() {
  try {
    const uint32_t durations[] = {20, 50, 100, 300, 650, 4000, 4500};
    const uint32_t periods[] = {10, 80, 100, 150};
    int cases = 0;
    for (uint32_t d : durations) for (uint32_t p : periods) {
      // Phase: the first poll lands inside the press, so it is observable at all.
      for (uint32_t phase : {0u, 7u}) {
        if (phase >= d) continue;
        cydtouch::PressTracker t;
        const uint32_t start = 1000;
        const Outcome o = run(t, start, d, start + phase, p, start + d + 400);
        const std::string tag = "dur " + std::to_string(d) + " period " + std::to_string(p) + " phase " + std::to_string(phase);
        check(o.downs == 1, "one admitted down: " + tag);
        const int ends = o.taps + o.longs + o.recals;
        check(ends == 1, "exactly one end event: " + tag + " got " + std::to_string(ends));
        // Classified from the span the polls actually saw.
        if (d >= 4500) check(o.recals == 1, "recal hold: " + tag);
        else if (d <= 300) check(o.taps == 1, "short press is a tap: " + tag);
        check(o.ex == 100 && o.ey == 200, "event carries the landing point: " + tag);
        check(!t.down(), "released: " + tag);
        ++cases;
      }
    }
    std::printf("PASS %d duration x poll-period cases: one down, exactly one end event, landing point reported\n", cases);

    {  // The CY-60 case: one valid sample, then a stalled loop. Formerly span 0 -> silent NONE.
      cydtouch::PressTracker t;
      check(t.update(1000, true, 50, 60) == TOUCH_NONE, "down is not an event");
      check(t.down(), "one sample admits the down");
      int x = 0, y = 0;
      check(t.update(1120, false, 0, 0, &x, &y) == TOUCH_TAP, "one-sample press taps after a 120 ms stall");
      check(x == 50 && y == 60, "landing point of one-sample press");
      std::printf("PASS one valid sample then a late poll still ends in a tap\n");
    }

    {  // Grace: a dropout under 60 ms does not split the press; it is counted.
      cydtouch::PressTracker t;
      const uint32_t bridgedBefore = cydtouch::stats().bridged;
      int taps = 0;
      auto feed = [&](uint32_t now, bool v) { if (t.update(now, v, 80, 90) == TOUCH_TAP) ++taps; };
      feed(1000, true); feed(1010, true); feed(1020, false); feed(1040, false); feed(1050, true); feed(1060, true);
      check(taps == 0 && t.down(), "dropout shorter than the grace keeps one press");
      check(cydtouch::stats().bridged == bridgedBefore + 1, "bridged dropout counted");
      feed(1070, false); feed(1100, false);
      check(taps == 0 && t.down(), "grace still running 40 ms after the last sample");
      feed(1130, false);
      check(taps == 1 && !t.down(), "released once the grace expires");
      std::printf("PASS 60 ms release grace bridges a dropout, then releases once\n");
    }

    {  // Re-arm: a contact within 120 ms of a release is ignored (counted once), then accepted.
      cydtouch::PressTracker t;
      const uint32_t before = cydtouch::stats().rearm;
      auto step = [&](uint32_t now, bool v) { return t.update(now, v, 10, 10); };
      step(5000, true);
      check(step(5060, false) == TOUCH_TAP, "first press ends");
      step(5070, true); step(5080, true);
      check(!t.down(), "contact 10 ms after release is not admitted");
      check(cydtouch::stats().rearm == before + 1, "suppressed contact counted once");
      step(5090, false);
      step(5179, true);
      check(!t.down(), "still inside the 120 ms window at 119 ms");
      step(5185, false);
      step(5200, true);
      check(t.down(), "admitted once 120 ms have passed");
      std::printf("PASS 120 ms re-arm window unchanged\n");
    }

    {  // A confirmed list lift may end early and rearm just to catch its coast.
      cydtouch::PressTracker t;
      t.update(2000,true,60,180);
      t.update(2010,true,60,150);
      check(t.update(2020,false,0,0)==TOUCH_NONE&&t.down(),"one zero keeps the press");
      check(t.update(2028,false,0,0,nullptr,nullptr,true)==TOUCH_TAP&&!t.down(),
            "confirmed lift ends the drag before grace expires");
      t.update(2030,true,60,150);
      check(!t.down(),"ordinary touch still waits for rearm");
      t.update(2031,true,60,150,nullptr,nullptr,false,true);
      check(t.down(),"a contact can immediately catch a confirmed coast");
      std::printf("PASS confirmed list lift and immediate coast catch\n");
    }

    {  // Long hold with a jittery pointer: one event, landing point not drift.
      cydtouch::PressTracker t;
      const Outcome o = run(t, 2000, 650, 2000, 10, 3000, 100, 200, 6);
      check(o.longs == 1 && o.taps == 0 && o.ex == 100 && o.ey == 200, "650 ms jittery press is one LONG at the landing point");
      int lx, ly;
      t.live(lx, ly);
      std::printf("PASS jittery press: landing point fixed, live point %d,%d\n", lx, ly);
    }

    {  // Poll gap statistic: loop-owned timing, max kept until the reader clears it.
      cydtouch::PressTracker t;
      cydtouch::stats().maxGapMs = 0;
      t.update(100, false, 0, 0); t.update(104, false, 0, 0); t.update(250, false, 0, 0); t.update(255, false, 0, 0);
      check(cydtouch::stats().maxGapMs == 146, "max touch poll gap recorded");
      std::printf("PASS max poll gap statistic\n");
    }

    {  // Never-admitted noise: a poll that sees nothing yields nothing.
      cydtouch::PressTracker t;
      for (uint32_t now = 1000; now < 3000; now += 10) check(t.update(now, false, 0, 0) == TOUCH_NONE, "idle polls make no events");
      std::printf("PASS idle polling is silent\n");
    }
    std::printf("PASS touch gesture classifier\n");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL touch gesture classifier: %s\n", e.what());
    return 1;
  }
}
