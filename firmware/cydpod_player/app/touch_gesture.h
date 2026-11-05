// ===========================================================================
// touch_gesture.h -- the press classifier, free of hardware so a host test can
// run the very code the firmware runs (touch.cpp feeds it one sample per poll)
// ===========================================================================
// Contract: every down it admits ends in exactly ONE event (TAP, LONG or
// RECAL) once the finger is gone. How long the finger was seen is never a
// reason to drop an admitted press: the UI has already drawn it as pressed, and
// a poll that arrives late (redraw, SD, Bluetooth) can see a one-sample press.
#pragma once

#include <stdint.h>

enum TouchEvent : uint8_t {
  TOUCH_NONE = 0,
  TOUCH_TAP,           // < 600 ms
  TOUCH_LONG_PRESS,    // 0.6 - 4 s
  TOUCH_RECALIBRATE    // > 4 s
};

static const uint32_t TOUCH_LONG_MS     = 600;
static const uint32_t TOUCH_RECAL_MS    = 4000;
static const uint32_t TOUCH_DEBOUNCE_MS = 120;   // no new down this soon after a release

namespace cydtouch {

// What the periodic "stat: touch" line reports. Counters are cumulative; the
// poll gap is the maximum since the last report.
struct Stats {
  uint32_t slop = 0;      // releases refused: finger ended outside the target's slop
  uint32_t claim = 0;     // releases refused: a list drag claimed the press
  uint32_t rearm = 0;     // contacts ignored: down inside the re-arm window
  uint32_t wake = 0;      // touches consumed to wake the screen
  uint32_t bridged = 0;   // pressure dropouts bridged by the release grace
  uint32_t maxGapMs = 0;  // longest time between two touch polls
};
inline Stats& stats() { static Stats s; return s; }

class PressTracker {
 public:
  // One dropped sample mid-press must not split a press in two.
  static const uint32_t ReleaseGraceMs = 60;

  // One call per poll. sx/sy are screen coordinates and only read when `valid`.
  // On the call that ends a press the landing point goes to *ex/*ey.
  TouchEvent update(uint32_t now, bool valid, int sx, int sy, int* ex = nullptr, int* ey = nullptr) {
    Stats& st = stats();
    if (polled_ && now - lastPoll_ > st.maxGapMs) st.maxGapMs = now - lastPoll_;
    polled_ = true;
    lastPoll_ = now;

    if (valid) {
      lastSeen_ = now;
      if (!down_) {
        if (now - lastUpAt_ < TOUCH_DEBOUNCE_MS) {
          if (!rearmCounted_) { rearmCounted_ = true; ++st.rearm; }
          return TOUCH_NONE;
        }
        down_ = true;
        dropout_ = false;
        downAt_ = now;
        landX_ = liveX_ = sx;               // where the press LANDED
        landY_ = liveY_ = sy;
      } else {
        if (dropout_) { dropout_ = false; ++st.bridged; }
        // Half-way toward each new sample: a resistive panel's readings jitter by
        // a few pixels, and a drag should glide rather than shiver.
        liveX_ = (liveX_ + sx) / 2;
        liveY_ = (liveY_ + sy) / 2;
      }
      return TOUCH_NONE;
    }

    rearmCounted_ = false;
    if (!down_) return TOUCH_NONE;
    if (now - lastSeen_ < ReleaseGraceMs) { dropout_ = true; return TOUCH_NONE; }

    down_ = false;
    lastUpAt_ = now;
    const uint32_t held = lastSeen_ - downAt_;       // may be 0: still a press
    if (ex) *ex = landX_;
    if (ey) *ey = landY_;
    if (held >= TOUCH_RECAL_MS) return TOUCH_RECALIBRATE;
    if (held >= TOUCH_LONG_MS)  return TOUCH_LONG_PRESS;
    return TOUCH_TAP;
  }

  bool     down() const { return down_; }
  uint32_t lastSeenMs() const { return lastSeen_; } // latest valid sample, retained through release grace
  uint32_t heldMs(uint32_t now) const { return down_ ? now - downAt_ : 0; }
  void     landing(int& x, int& y) const { x = landX_; y = landY_; }
  void     live(int& x, int& y) const { x = liveX_; y = liveY_; }

 private:
  bool     down_ = false, dropout_ = false, polled_ = false, rearmCounted_ = false;
  uint32_t downAt_ = 0, lastSeen_ = 0, lastUpAt_ = 0, lastPoll_ = 0;
  int      landX_ = 0, landY_ = 0, liveX_ = 0, liveY_ = 0;
};

}  // namespace cydtouch
