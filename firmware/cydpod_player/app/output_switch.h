#pragma once
#include "boot_plan.h"
#include "settings.h"

inline bool output_usesBluetooth() {
  return boot_plan(g_settings.outRoute, CYD_WIRED_OUTPUT).bluetooth;
}

// Shared by both Output menus and the Bluetooth-off page. No allocation.
bool output_switch(uint8_t next);

// Ordering is deliberate: validate route -> freeze resume -> final fade/park ->
// commit resume and route -> restart. Failure never publishes the new setting.
template<class Ops> bool output_switch_run(uint8_t old, uint8_t next, Ops& ops) {
  if (next != outRouteSanitize(next)) return false;
  if (old == next) return true;
  if (!ops.route(next)) return false;
  ops.capture();
  if (!ops.park()) { ops.restore(old); return false; }
  if (!ops.save(next)) { ops.restore(old); return false; }
  ops.restart(next);
  return true;
}
