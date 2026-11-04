// CY-71 host test for firmware/cydpod_player/app/out_route.h: the persisted route numbers,
// what the Output setting offers, and how a stored byte is read back. Built twice by
// build_wired_fade.sh: once for the JC2432W328C (wired route compiled in) and once for the
// default board (it is not).
#include "../firmware/cydpod_player/app/out_route.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Persisted by number in NVS ("out"): existing values must never move.
static_assert(OUT_SPEAKER == 0, "OUT_SPEAKER keeps its number");
static_assert(OUT_BT == 1, "OUT_BT is persisted as 1");
static_assert(OUT_WIRED == 2, "OUT_WIRED is persisted as 2");

int main() {
  // Whatever the board, every stored byte reads back as something the menu offers.
  for (int v = 0; v < 256; ++v) {
    const uint8_t r = outRouteSanitize((uint8_t)v);
    bool offered = false;
    for (int i = 0; i < OUT_SELECTABLE_COUNT; ++i) offered |= outRouteSelectable(i) == r;
    CHECK(offered, "stored %d reads back as %d, which the menu does not offer", v, r);
    CHECK(r != OUT_SPEAKER, "stored %d must never select the retired speaker route", v);
  }
  CHECK(outRouteSanitize(OUT_SPEAKER) == OUT_BT, "a saved speaker choice migrates to Bluetooth");
  CHECK(outRouteSanitize(OUT_BT) == OUT_BT, "Bluetooth stays Bluetooth");
  CHECK(outRouteSanitize(255) == OUT_BT, "erased flash (0xFF) selects Bluetooth");
  CHECK(outRouteSelectable(0) == OUT_BT, "Bluetooth is the first entry");
#if CYD_WIRED_OUTPUT
  static_assert(OUT_ROUTE_COUNT == 3, "wired boards know three route numbers");
  static_assert(OUT_SELECTABLE_COUNT == 2, "wired boards offer Bluetooth and Headphones");
  CHECK(outRouteSelectable(1) == OUT_WIRED, "Headphones is the second entry");
  CHECK(outRouteSanitize(OUT_WIRED) == OUT_WIRED, "a saved Headphones choice survives");
  CHECK(outRouteSanitize(5) == OUT_BT, "a corrupt 5 must not select the wired route");
  printf("out_route: wired board, Bluetooth + Headphones\n");
#else
  static_assert(OUT_ROUTE_COUNT == 2, "other boards know two route numbers");
  static_assert(OUT_SELECTABLE_COUNT == 1, "other boards offer Bluetooth only");
  CHECK(outRouteSanitize(OUT_WIRED) == OUT_BT, "a Headphones choice saved by another build reads as Bluetooth here");
  printf("out_route: non-wired board, Bluetooth only\n");
#endif
  if (failures) { printf("OUT_ROUTE_FAILED %d\n", failures); return 1; }
  printf("OUT_ROUTE_OK\n");
  return 0;
}
