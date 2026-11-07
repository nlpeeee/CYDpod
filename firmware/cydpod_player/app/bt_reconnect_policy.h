#pragma once
#include <stdint.h>
namespace cyd {
// When the player may page its saved speaker by itself. Pure state, no radio:
// bt.cpp feeds it events (under a lock) and asks mayPage() from the loop task.
//
// A page that is refused, busy or unanswered is a failure; after
// MAX_FAILED_PAGES of them the player stops until the owner acts. A link that
// the other side ended on purpose (DISC_RSN_NORMAL) is never chased: the
// speaker was taken by someone else, or switched off. Only signal loss
// (DISC_RSN_ABNORMAL) is chased, with growing gaps.
struct ReconnectPolicy {
  static const uint8_t  MAX_FAILED_PAGES    = 5;      // then hold until the owner acts
  static const uint32_t FIRST_PAGE_DELAY_MS = 5000;   // after signal loss / owner action
  static const uint32_t BOOT_PAGE_WINDOW_MS = 20000;  // library's one boot page (0.5 s stack wait, or 10 s with btfast off, + page)
  static const uint32_t PAGE_TIMEOUT_MS     = 25000;  // a page with no answer counts as failed
  static const uint32_t PAIR_RESUME_WINDOW_MS = 30000; // resume-after-pairing is forgotten after this
  // Wait after the n-th failed page (n >= 1): 15, 30, 60, 120, 120 s.
  static uint32_t backoffMs(uint8_t failedPages) {
    return failedPages <= 1 ? 15000u : failedPages == 2 ? 30000u : failedPages == 3 ? 60000u : 120000u;
  }

  enum Mode : uint8_t {
    Armed,       // may page when nextAt passes
    HeldPeer,    // the other side ended the link on purpose
    HeldUser,    // the owner disconnected / forgot / switched away
    Exhausted    // too many failed pages, or an owner-requested connect failed
  };
  Mode     mode = Armed;
  uint8_t  failed = 0;
  bool     inFlight = false;
  uint32_t nextAt = 0, flightEnds = 0;

  static bool reached(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }

  // Boot. libraryPages: the library itself pages the saved speaker once after
  // the stack is up; leave it that window rather than paging twice.
  void boot(uint32_t now, bool libraryPages) {
    mode = Armed; failed = 0; inFlight = libraryPages;
    flightEnds = nextAt = now + (libraryPages ? BOOT_PAGE_WINDOW_MS : 0);
  }
  // Tap on a device, Pair new, Cancel: the owner acts, so the budget is new.
  void userAction(uint32_t now) {
    mode = Armed; failed = 0; inFlight = false; nextAt = now + FIRST_PAGE_DELAY_MS;
  }
  // Deliberate disconnect, forget, switch to wired: stay down until userAction().
  void userStop() { mode = HeldUser; inFlight = false; }
  bool acceptsPeer() const { return mode != HeldUser; }
  // An owner-requested connect did not work: they were told, do not retry behind their back.
  void connectFailed() { if (mode == Armed) mode = Exhausted; inFlight = false; }
  // A link came up (either side started it).
  void linkUp() { mode = Armed; failed = 0; inFlight = false; nextAt = 0; }
  // The link layer reported DISCONNECTED. ours: we asked for it. wasUp: it had been
  // CONNECTED (otherwise this is a page that did not get through).
  void linkDown(uint32_t now, bool ours, bool wasUp, bool abnormal) {
    if (ours) { inFlight = false; return; }
    if (wasUp) {
      inFlight = false;
      if (mode != Armed && mode != Exhausted) return;       // owner already stopped it
      if (abnormal) { mode = Armed; failed = 0; nextAt = now + FIRST_PAGE_DELAY_MS; }
      else mode = HeldPeer;
      return;
    }
    if (inFlight) pageFailed(now);
  }
  void pageStarted(uint32_t now) { inFlight = true; flightEnds = now + PAGE_TIMEOUT_MS; }
  void pageFailed(uint32_t now) {
    inFlight = false;
    if (mode != Armed) return;
    if (++failed >= MAX_FAILED_PAGES) mode = Exhausted;
    else nextAt = now + backoffMs(failed);
  }
  // A page that never produced an event.
  void expire(uint32_t now) { if (inFlight && reached(now, flightEnds)) pageFailed(now); }
  // connected / busy: the radio is in use (link, pairing session, owner's own connect).
  bool mayPage(uint32_t now, bool connected, bool busy, bool haveAddress) const {
    return mode == Armed && !inFlight && !connected && !busy && haveAddress && reached(now, nextAt);
  }
};
}
