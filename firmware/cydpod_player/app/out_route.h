// ===========================================================================
// out_route.h -- where the sound goes, and how a stored route is read back
// ===========================================================================
// Pure (board.h's preprocessor part only), so tests/out_route_test.cpp can
// build it for a wired and a non-wired board.
//
// The values are persisted by number in Settings::outRoute (NVS key "out"):
// never renumber, only append.
//
// WHAT THE USER CAN CHOOSE (CY-71 scope change): Bluetooth, and Headphones (the
// wired I2S module) on boards that have it. The on-board speaker was retired from
// the menu, the badge and the stored setting: OUT_SPEAKER keeps its number and its
// driver (speaker.cpp) is still compiled, but nothing selects it and a stored 0
// reads back as Bluetooth. Removing speaker.cpp is a later cleanup.
#pragma once

#include <stdint.h>
#include "board.h"

enum OutRoute : uint8_t {
  OUT_SPEAKER = 0,   // retired: unreachable from the UI and from storage
  OUT_BT = 1,
  OUT_WIRED = 2,     // "Headphones": I2S to the PCM5100A module; only where CYD_WIRED_OUTPUT
#if CYD_WIRED_OUTPUT
  OUT_ROUTE_COUNT = 3
#else
  OUT_ROUTE_COUNT = 2   // OUT_WIRED is not available here
#endif
};

// Routes the Output setting offers, in menu order.
#if CYD_WIRED_OUTPUT
constexpr int OUT_SELECTABLE_COUNT = 2;
#else
constexpr int OUT_SELECTABLE_COUNT = 1;
#endif
inline uint8_t outRouteSelectable(int index) {
#if CYD_WIRED_OUTPUT
  return index == 1 ? (uint8_t)OUT_WIRED : (uint8_t)OUT_BT;
#else
  (void)index;
  return OUT_BT;
#endif
}

// A stored byte -> a route this build offers. A saved speaker (0) or anything unknown or
// unavailable (a wired choice read by a build without the module) becomes Bluetooth.
inline uint8_t outRouteSanitize(uint8_t stored) {
#if CYD_WIRED_OUTPUT
  return stored == OUT_WIRED ? (uint8_t)OUT_WIRED : (uint8_t)OUT_BT;
#else
  (void)stored;
  return OUT_BT;
#endif
}
