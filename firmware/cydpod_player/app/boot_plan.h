#pragma once
#include "out_route.h"

struct BootPlan { bool bluetooth; bool wired; };
// Stored speaker/unknown/unavailable routes migrate to Bluetooth.
constexpr BootPlan boot_plan(uint8_t route, bool boardHasWired) {
  return {!(boardHasWired && route == OUT_WIRED), boardHasWired && route == OUT_WIRED};
}
