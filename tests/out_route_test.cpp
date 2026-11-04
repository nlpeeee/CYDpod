// CY-71 host test for firmware/cydpod_player/app/out_route.h: the persisted route numbers
// and how a stored byte is read back. Built twice by build_wired_fade.sh: once for the
// JC2432W328C (wired route compiled in) and once for the default board (it is not).
#include "../firmware/cydpod_player/app/out_route.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Persisted by number in NVS ("out"): existing values must never move.
static_assert(OUT_SPEAKER == 0, "OUT_SPEAKER is persisted as 0");
static_assert(OUT_BT == 1, "OUT_BT is persisted as 1");
static_assert(OUT_WIRED == 2, "OUT_WIRED is persisted as 2");

int main() {
#if CYD_WIRED_OUTPUT
  static_assert(OUT_ROUTE_COUNT == 3, "wired boards have three routes");
  for (int v = 0; v < 256; ++v) {
    const uint8_t r = outRouteSanitize((uint8_t)v);
    CHECK(r < OUT_ROUTE_COUNT, "stored %d -> %d is out of range", v, r);
    if (v < 3) CHECK(r == v, "stored %d must read back unchanged, got %d", v, r);
    else CHECK(r == OUT_SPEAKER, "out-of-range stored %d must fall back to the speaker, got %d", v, r);
  }
  CHECK(outRouteSanitize(5) != OUT_WIRED, "a corrupt 5 must not select the wired route");
  CHECK(outRouteSanitize(255) == OUT_SPEAKER, "erased flash (0xFF) selects the speaker");
  printf("out_route: wired board, 3 routes\n");
#else
  static_assert(OUT_ROUTE_COUNT == 2, "other boards keep two routes");
  for (int v = 0; v < 256; ++v) {
    const uint8_t r = outRouteSanitize((uint8_t)v);
    CHECK(r == (uint8_t)(v % 2), "stored %d -> %d, the original modulo gives %d", v, r, v % 2);
    CHECK(r != OUT_WIRED, "wired must be unreachable here (stored %d)", v);
  }
  CHECK(outRouteSanitize(OUT_WIRED) == OUT_SPEAKER, "a wired choice saved by another build reads as the speaker");
  printf("out_route: non-wired board, 2 routes\n");
#endif
  if (failures) { printf("OUT_ROUTE_FAILED %d\n", failures); return 1; }
  printf("OUT_ROUTE_OK\n");
  return 0;
}
