#pragma once
#include <stdint.h>
namespace cyd {
// SBC bitrate for the current A2DP link (docs/specs/bt-rate-and-earbuds.md).
// Pure state, no radio: bt_diag.cpp samples the Bluedroid link counters
// (tools/bluedroid/patches/0002) and feeds them here once a second.
//
// Speakers keep the stock 328 kbps. A link whose queue backs up (true-wireless
// earbuds, a weak signal) steps down one notch per congested 2 s window, and
// climbs back one notch after 60 s clean, never above one notch better than the
// rate the device was remembered at. The owner can pin a rate (`btd rate`).
struct BtRatePolicy {
  static const uint8_t  STEP_COUNT = 6;
  static uint16_t stepKbps(uint8_t i) {
    static const uint16_t k[STEP_COUNT] = {328, 292, 257, 229, 200, 165};
    return k[i < STEP_COUNT ? i : STEP_COUNT - 1];
  }
  static const uint32_t WINDOW_MS     = 2000;   // congestion is judged per window
  static const uint32_t CLEAN_UP_MS   = 60000;  // clean this long before stepping up
  static const uint32_t QUIET_HOLD_MS = 10000;  // congested() stays true this long
  static const uint32_t STALL_LIMIT   = 10;     // L2CAP stalls per window
  static const uint32_t TXQ_LIMIT     = 13;     // of Bluedroid's 27-packet TX queue

  uint8_t  step = 0, ceiling = 0, downs = 0, ups = 0;
  bool     pinned = false, active = false;
  uint32_t lastSample = 0, windowMs = 0, cleanMs = 0, congestedAt = 0;
  uint32_t stalls = 0, txqMax = 0, drops = 0;

  // Nearest step at or below this rate (lower rate = higher index); 0 = stock.
  static uint8_t indexFor(uint16_t kbps) {
    if (!kbps) return 0;
    for (uint8_t i = 0; i < STEP_COUNT; ++i) if (stepKbps(i) <= kbps) return i;
    return STEP_COUNT - 1;
  }
  uint16_t kbps() const { return stepKbps(step); }

  // A link came up. remembered: the device's stored rate (0 = never stepped down).
  void start(uint32_t now, uint16_t remembered) {
    active = true; downs = ups = 0;
    if (!pinned) step = indexFor(remembered);
    ceiling = step ? uint8_t(step - 1) : 0;
    lastSample = now; windowMs = cleanMs = 0; congestedAt = 0;
    stalls = txqMax = drops = 0;
  }
  void stop() { active = false; }
  void pin(uint16_t kbpsValue) { pinned = kbpsValue != 0; if (pinned) step = indexFor(kbpsValue); }

  // Counters since the last sample, called about once a second while audio is
  // streaming. Time between samples counts at most 2 s, so a pause neither
  // ends a window early nor counts as clean time. Returns true when the rate
  // should change.
  bool sample(uint32_t now, uint32_t stallDelta, uint32_t txqMaxNow, uint32_t dropDelta) {
    if (!active) return false;
    uint32_t dt = now - lastSample; lastSample = now; if (dt > WINDOW_MS) dt = WINDOW_MS;
    windowMs += dt;
    stalls += stallDelta; drops += dropDelta; if (txqMaxNow > txqMax) txqMax = txqMaxNow;
    if (windowMs < WINDOW_MS) return false;
    const bool congestedWindow = stalls > STALL_LIMIT || txqMax >= TXQ_LIMIT || drops > 0;
    const uint32_t span = windowMs;
    windowMs = 0; stalls = txqMax = drops = 0;
    if (congestedWindow) {
      congestedAt = now | 1; cleanMs = 0;
      if (!pinned && step + 1 < STEP_COUNT) { ++step; ++downs; return true; }
      return false;
    }
    cleanMs += span;
    if (!pinned && step > ceiling && cleanMs >= CLEAN_UP_MS) { --step; ++ups; cleanMs = 0; return true; }
    return false;
  }
  // Recently congested: the UI keeps its animations quiet (bt_diag.h btlink_quietUi).
  bool congested(uint32_t now) const {
    return active && congestedAt && int32_t(now - congestedAt) < int32_t(QUIET_HOLD_MS);
  }
  // What to remember for the device when the link ends: 0 if it stayed at stock.
  uint16_t rememberKbps() const { return step ? kbps() : 0; }
};
}
