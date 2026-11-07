// CY-114: saved-speaker reconnect policy (firmware/cydpod_player/app/bt_reconnect_policy.h).
#include "../firmware/cydpod_player/app/bt_reconnect_policy.h"
#include <iostream>
#include <stdexcept>
#include <string>
using cyd::ReconnectPolicy;
static std::string what;
static void ck(bool b, const char* name) { if (!b) { what = name; throw std::runtime_error(name); } }

// A page is started when allowed and counts as failed when the link layer reports a
// disconnect that never connected. Returns the time of the next allowed page.
static bool tryPage(ReconnectPolicy& p, uint32_t now) {
  if (!p.mayPage(now, false, false, true)) return false;
  p.pageStarted(now);
  return true;
}
static bool wantsPage(const ReconnectPolicy& p, uint32_t now) { return p.mayPage(now, false, false, true); }

int main() {
  try {
    const uint32_t S = 1000;
    // 1. Refused / busy connects stop after the cap, with growing gaps; nothing loops.
    {
      ReconnectPolicy p; p.boot(0, false);
      uint32_t now = 0, last = 0; int pages = 0;
      uint32_t gaps[8] = {0};
      for (int guard = 0; guard < 100000; ++guard, now += 100) {
        if (tryPage(p, now)) {
          if (pages) gaps[pages - 1] = now - last;
          last = now; ++pages;
          p.linkDown(now + 1, false, false, false);   // refused: CONNECTING -> DISCONNECTED
        }
      }
      ck(pages == ReconnectPolicy::MAX_FAILED_PAGES, "refused: page count capped");
      ck(gaps[0] >= 15 * S && gaps[0] < 16 * S && gaps[1] >= 30 * S && gaps[1] < 31 * S &&
         gaps[2] >= 60 * S && gaps[2] < 61 * S && gaps[3] >= 120 * S && gaps[3] < 121 * S, "refused: backoff 15/30/60/120 s");
      ck(p.mode == ReconnectPolicy::Exhausted && !wantsPage(p, now + 3600 * S), "refused: stays stopped");
    }
    // 2. A page with no answer at all (no event) is failed by expire().
    {
      ReconnectPolicy p; p.boot(0, false);
      ck(tryPage(p, 0), "silent: first page");
      ck(!wantsPage(p, 1 * S), "silent: no page while one is in flight");
      p.expire(ReconnectPolicy::PAGE_TIMEOUT_MS + 1);
      ck(p.failed == 1 && !p.inFlight, "silent: counted as failed");
    }
    // 3. The other side ended the link on purpose: no chasing, ever.
    {
      ReconnectPolicy p; p.linkUp();
      p.linkDown(10 * S, false, true, false);            // DISC_RSN_NORMAL, link had been up
      ck(p.mode == ReconnectPolicy::HeldPeer, "peer drop: held");
      ck(!wantsPage(p, 10 * S) && !wantsPage(p, 3600 * S), "peer drop: never pages");
      p.expire(100000 * S);
      ck(!wantsPage(p, 100000 * S), "peer drop: still held much later");
    }
    // 4. Signal loss is chased, bounded.
    {
      ReconnectPolicy p; p.linkUp();
      p.linkDown(10 * S, false, true, true);             // DISC_RSN_ABNORMAL
      ck(p.mode == ReconnectPolicy::Armed && !wantsPage(p, 10 * S + 4000), "loss: waits FIRST_PAGE_DELAY");
      ck(wantsPage(p, 10 * S + ReconnectPolicy::FIRST_PAGE_DELAY_MS), "loss: pages after the delay");
    }
    // 5. Deliberate disconnect / forget / switch to wired: nothing until the owner acts.
    {
      ReconnectPolicy p; p.linkUp();
      p.userStop();
      p.linkDown(10 * S, true, true, false);             // our own disconnect
      ck(!wantsPage(p, 3600 * S), "user stop: held");
      p.linkDown(11 * S, false, true, true);             // even a later "signal loss" report
      ck(!wantsPage(p, 3600 * S), "user stop: abnormal report does not re-arm");
    }
    // 6. The owner re-arms with a fresh budget (tap Connect, Pair, toggle Bluetooth).
    {
      ReconnectPolicy p; p.boot(0, false);
      uint32_t now = 0;
      for (int guard = 0; guard < 100000; ++guard, now += 100)
        if (tryPage(p, now)) p.linkDown(now + 1, false, false, false);
      ck(p.mode == ReconnectPolicy::Exhausted, "rearm: exhausted first");
      p.userAction(now);
      ck(p.mode == ReconnectPolicy::Armed && p.failed == 0 && !wantsPage(p, now) &&
         wantsPage(p, now + ReconnectPolicy::FIRST_PAGE_DELAY_MS), "rearm: fresh budget");
      ReconnectPolicy q; q.userStop(); q.userAction(5 * S);
      ck(q.mode == ReconnectPolicy::Armed, "rearm: from user stop");
    }
    // 7. Never while connected, busy (pairing session / owner's own connect) or with no saved speaker.
    {
      ReconnectPolicy p; p.boot(0, false);
      ck(!p.mayPage(S, true, false, true), "no page while connected");
      ck(!p.mayPage(S, false, true, true), "no page while busy");
      ck(!p.mayPage(S, false, false, false), "no page without a saved speaker");
      ck(p.mayPage(S, false, false, true), "page when idle");
    }
    // 8. Boot: the library's one page counts; bt_tick waits for it, then follows the policy.
    {
      ReconnectPolicy p; p.boot(0, true);
      ck(!wantsPage(p, 20 * S), "boot: no second page during the library's window");
      p.linkDown(12 * S, false, false, false);           // library page refused
      ck(p.failed == 1 && p.inFlight == false && !wantsPage(p, 20 * S), "boot: refused page counted, backoff applies");
      ck(wantsPage(p, 12 * S + 15 * S), "boot: next page after 15 s backoff");
      ReconnectPolicy q; q.boot(0, true); q.linkUp();
      ck(q.mode == ReconnectPolicy::Armed && q.failed == 0 && !q.inFlight, "boot: connect clears the window");
    }
    // 9. A connect the owner asked for that fails does not loop behind their back.
    {
      ReconnectPolicy p; p.userAction(0); p.connectFailed();
      ck(!wantsPage(p, 3600 * S), "owner connect failure: stopped");
      ReconnectPolicy q; q.userStop(); q.connectFailed();
      ck(q.mode == ReconnectPolicy::HeldUser, "owner connect failure keeps user stop");
    }
    // 10. Success resets the counter; a stale failure count does not carry over.
    {
      ReconnectPolicy p; p.boot(0, false);
      ck(tryPage(p, 0), "reset: page"); p.linkDown(1, false, false, false);
      ck(p.failed == 1, "reset: one failure");
      p.linkUp();
      ck(p.failed == 0 && wantsPage(p, 0), "reset: link up clears");
    }
    {
      ReconnectPolicy p;p.boot(0,false);p.userStop();
      ck(!p.acceptsPeer()&&!wantsPage(p,3600*S),"manual disconnect holds incoming and outgoing links");
      p.linkDown(S,true,true,false);
      ck(!p.acceptsPeer(),"disconnect completion preserves user hold");
      p.userAction(2*S);
      ck(p.acceptsPeer(),"explicit Connect or Pair permits the peer again");
      p.userStop();p.boot(3*S,false);
      ck(p.acceptsPeer()&&wantsPage(p,3*S),"next boot restores startup connection policy");
    }
    std::cout << "PASS bt reconnect policy: refused-connect cap with 15/30/60/120 s backoff, silent-page expiry, peer drop held, signal loss bounded, owner stop and re-arm, no page while connected/busy, boot window, owner-connect failure\n";
    return 0;
  } catch (...) {
    std::cerr << "FAIL bt reconnect policy: " << what << "\n";
    return 1;
  }
}
