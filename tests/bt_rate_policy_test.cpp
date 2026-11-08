// SBC rate policy (firmware/cydpod_player/app/bt_rate_policy.h) and the
// play-reconnect rule (ReconnectPolicy::ownerPlay). Pure logic, no hardware.
#include "../firmware/cydpod_player/app/bt_rate_policy.h"
#include "../firmware/cydpod_player/app/bt_reconnect_policy.h"
#include <iostream>
#include <stdexcept>
#include <string>
using cyd::BtRatePolicy;
using cyd::ReconnectPolicy;
static void ck(bool b, const char* name) { if (!b) throw std::runtime_error(name); }

// Feed one sample a second for `seconds`, with these per-second counters.
static int run(BtRatePolicy& p, uint32_t& now, int seconds, uint32_t stalls, uint32_t txq, uint32_t drops) {
  int changes = 0;
  for (int i = 0; i < seconds; ++i) { now += 1000; if (p.sample(now, stalls, txq, drops)) ++changes; }
  return changes;
}

int main() {
  try {
    // 1. A clean speaker stays at the stock 328 kbps.
    {
      BtRatePolicy p; uint32_t now = 0; p.start(now, 0);
      ck(run(p, now, 300, 0, 2, 0) == 0 && p.kbps() == 328, "speaker stays at 328");
      ck(!p.congested(now) && p.rememberKbps() == 0, "speaker: nothing to remember");
    }
    // 2. Congestion steps down one notch per 2 s window, to the floor.
    {
      BtRatePolicy p; uint32_t now = 0; p.start(now, 0);
      run(p, now, 2, 20, 0, 0);
      ck(p.kbps() == 292 && p.congested(now), "stalls: one step down");
      run(p, now, 2, 0, 20, 0);
      ck(p.kbps() == 257, "full TX queue: next step");
      run(p, now, 2, 0, 0, 1);
      ck(p.kbps() == 229, "a drop: next step");
      run(p, now, 20, 50, 27, 3);
      ck(p.kbps() == 165 && p.step == BtRatePolicy::STEP_COUNT - 1, "floor at 165");
      ck(p.rememberKbps() == 165, "remembers the floor");
    }
    // 3. Stalls at or under the limit do not count.
    {
      BtRatePolicy p; uint32_t now = 0; p.start(now, 0);
      ck(run(p, now, 60, 5, 12, 0) == 0 && p.kbps() == 328, "stalls under the limit ignored");
    }
    // 4. Recovery: up one notch after 60 s clean, never above remembered - 1 step.
    {
      BtRatePolicy p; uint32_t now = 0; p.start(now, 229);
      ck(p.kbps() == 229 && p.ceiling == 2, "starts at the remembered rate");
      ck(run(p, now, 59, 0, 0, 0) == 0, "no climb before 60 s");
      run(p, now, 2, 0, 0, 0);
      ck(p.kbps() == 257, "climbs one notch after 60 s clean");
      run(p, now, 600, 0, 0, 0);
      ck(p.kbps() == 257, "never above one notch better than remembered");
      run(p, now, 2, 30, 0, 0);
      ck(p.kbps() == 229 && p.rememberKbps() == 229, "congested again: back down");
    }
    // 5. A rate between steps maps to the next lower-quality step.
    ck(BtRatePolicy::indexFor(240) == 3 && BtRatePolicy::indexFor(400) == 0 && BtRatePolicy::indexFor(10) == 5, "indexFor");
    // 6. A pinned rate never moves, and survives start().
    {
      BtRatePolicy p; p.pin(229); uint32_t now = 0; p.start(now, 0);
      ck(p.kbps() == 229, "pin applies at start");
      ck(run(p, now, 20, 99, 27, 5) == 0 && p.kbps() == 229, "pin ignores congestion");
      p.pin(0); p.start(now, 0);
      ck(p.kbps() == 328, "unpin returns to stock");
    }
    // 7. congested() clears after the hold, and an inactive policy does nothing.
    {
      BtRatePolicy p; uint32_t now = 0; p.start(now, 0);
      run(p, now, 2, 20, 0, 0);
      run(p, now, 11, 0, 0, 0);
      ck(!p.congested(now), "congested clears after 10 s");
      p.stop(); ck(!p.sample(now + 5000, 99, 27, 9), "stopped policy ignores samples");
    }
    // 7b. A long pause between samples counts as neither a window nor clean time.
    {
      BtRatePolicy p; uint32_t now = 0; p.start(now, 229);
      run(p, now, 30, 0, 0, 0);
      now += 3600000;                          // an hour paused, no samples
      ck(run(p, now, 20, 0, 0, 0) == 0 && p.kbps() == 229, "pause is not clean time");
      run(p, now, 12, 0, 0, 0);
      ck(p.kbps() == 257, "climbs after 60 s of streaming in total");
    }
    // 8. Play with no link re-arms a peer-held or exhausted policy, not a user stop.
    {
      ReconnectPolicy r; r.boot(0, false); r.linkUp();
      r.linkDown(1000, false, true, false);
      ck(r.mode == ReconnectPolicy::HeldPeer && !r.mayPage(2000, false, false, true), "normal drop holds");
      ck(r.ownerPlay(2000) && r.mayPage(2000, false, false, true), "play pages at once");
      r.pageStarted(2000);
      ck(!r.ownerPlay(2100), "no second page while one is in flight");
      r.userStop();
      ck(!r.ownerPlay(3000) && !r.mayPage(3000, false, false, true), "a deliberate disconnect stays down");
      ReconnectPolicy e; e.boot(0, false); e.connectFailed(); e.mode = ReconnectPolicy::Exhausted;
      ck(e.ownerPlay(10) && e.mayPage(10, false, false, true), "play after exhausted pages");
    }
    std::cout << "PASS bt rate policy and play reconnect\n";
    return 0;
  } catch (const std::exception& e) {
    std::cout << "FAIL " << e.what() << "\n";
    return 1;
  }
}
