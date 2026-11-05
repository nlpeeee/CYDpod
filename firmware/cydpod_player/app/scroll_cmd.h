// scroll_cmd.h - the `scroll` debug command (CY-108): live flick knobs, RAM only. Header-only and free of Arduino
// includes so the host test runs the very parser the firmware runs.
//   scroll                 print every value
//   scroll gap <ms>        sample gap that restarts the estimate          (cfg.gapMs)
//   scroll gain <x>        multiplier on the release velocity             (gain)
//   scroll tau <ms>        slow friction time constant                    (cfg.tauSlow)
//   scroll tauf <ms>       fast friction time constant                    (cfg.tauFast)
//   scroll cap <rows/s>    release speed cap                              (cfg.capRowsPerSec)
//   scroll minv <px/s>     slowest release that still coasts              (cfg.minVelPxS)
//   scroll mintravel <px>  least net travel that still coasts             (cfg.minTravelPx)
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scroll_core.h"

namespace cydscroll {

struct Knob { const char* name; float lo, hi; };
inline const Knob* knobs() {
  static const Knob k[] = {{"gap", 20, 1000}, {"gain", 0.1f, 10}, {"tau", 50, 20000}, {"tauf", 50, 60000},
                           {"cap", 1, 1000}, {"minv", 0, 2000}, {"mintravel", 0, 200}, {nullptr, 0, 0}};
  return k;
}

inline void printKnobs(const ScrollCfg& c, float gain, char* out, size_t n) {
  snprintf(out, n, "scroll: gap %lu gain %.2f tau %.0f tauf %.0f cap %.0f minv %.0f mintravel %.0f",
           (unsigned long)c.gapMs, (double)gain, (double)(c.tauSlow * 1000.0f), (double)(c.tauFast * 1000.0f),
           (double)c.capRowsPerSec, (double)c.minVelPxS, (double)c.minTravelPx);
}

// Handles one text line. Returns false when the line is not a `scroll` command (nothing touched). Otherwise `out` holds
// the reply (the values, or an error) and `*changed` says whether a value was set.
inline bool scrollCommand(const char* line, ScrollCfg& c, float& gain, char* out, size_t n, bool* changed = nullptr) {
  if (changed) *changed = false;
  while (*line == ' ' || *line == '\t') ++line;
  if (strncmp(line, "scroll", 6) != 0 || (line[6] != 0 && line[6] != ' ' && line[6] != '\t' && line[6] != '\r' && line[6] != '\n')) return false;
  line += 6;
  char name[16] = {};
  int used = 0;
  if (sscanf(line, " %15s %n", name, &used) < 1) { printKnobs(c, gain, out, n); return true; }
  const Knob* k = knobs();
  while (k->name && strcmp(k->name, name) != 0) ++k;
  if (!k->name) { snprintf(out, n, "scroll: unknown knob '%s' (gap gain tau tauf cap minv mintravel)", name); return true; }
  char* end = nullptr;
  const char* arg = line + used;
  if (*arg == 0 || *arg == '\r' || *arg == '\n') { printKnobs(c, gain, out, n); return true; }  // `scroll gap` without a value: show the values
  const double v = strtod(arg, &end);
  while (*end == ' ' || *end == '\r' || *end == '\n') ++end;
  if (end == arg || *end || !(v >= k->lo && v <= k->hi)) { snprintf(out, n, "scroll: %s needs a number %g..%g", name, (double)k->lo, (double)k->hi); return true; }
  const float f = (float)v;
  if (!strcmp(name, "gap")) c.gapMs = (uint32_t)(f + 0.5f);
  else if (!strcmp(name, "gain")) gain = f;
  else if (!strcmp(name, "tau")) c.tauSlow = f * 0.001f;
  else if (!strcmp(name, "tauf")) c.tauFast = f * 0.001f;
  else if (!strcmp(name, "cap")) c.capRowsPerSec = f;
  else if (!strcmp(name, "minv")) c.minVelPxS = f;
  else c.minTravelPx = f;
  if (changed) *changed = true;
  printKnobs(c, gain, out, n);
  return true;
}

}  // namespace cydscroll
