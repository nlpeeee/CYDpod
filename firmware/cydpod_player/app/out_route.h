// ===========================================================================
// out_route.h -- where the sound goes, and how a stored route is read back
// ===========================================================================
// Pure (board.h's preprocessor part only), so tests/out_route_test.cpp can
// build it for a wired and a non-wired board.
//
// The values are persisted by number in Settings::outRoute (NVS key "out"):
// never renumber, only append.
#pragma once

#include <stdint.h>
#include "board.h"

enum OutRoute : uint8_t {
  OUT_SPEAKER = 0,
  OUT_BT = 1,
  OUT_WIRED = 2,   // I2S to the PCM5100A module; routed only where CYD_WIRED_OUTPUT
#if CYD_WIRED_OUTPUT
  OUT_ROUTE_COUNT = 3
#else
  OUT_ROUTE_COUNT = 2   // OUT_WIRED is not selectable here
#endif
};

// A stored byte -> a route this build can run.
//  * Boards without the wired route keep the original `% OUT_ROUTE_COUNT`, so a
//    stored 2 reads as the speaker there and the image is unchanged.
//  * Wired boards reject anything out of range to the speaker (a modulo would turn a
//    corrupt 5 into a wired route nobody chose).
inline uint8_t outRouteSanitize(uint8_t stored) {
#if CYD_WIRED_OUTPUT
  return stored < OUT_ROUTE_COUNT ? stored : (uint8_t)OUT_SPEAKER;
#else
  return stored % OUT_ROUTE_COUNT;
#endif
}
