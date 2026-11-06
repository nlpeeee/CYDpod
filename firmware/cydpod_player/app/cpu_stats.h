#pragma once
#include <stdint.h>

namespace cpustats {
// SDK esp_timer counters are microseconds, uint32_t: wrap every 71.6 min.
// Unsigned subtraction is valid when successive samples are < one wrap apart.
inline uint32_t delta(uint32_t now, uint32_t before) { return now - before; }
inline uint32_t tenths(uint32_t cpuUs, uint64_t wallUs) {
  if (!wallUs) return 0;
  const uint64_t value = uint64_t(cpuUs) * 1000 / wallUs;
  return value > 1000 ? 1000 : uint32_t(value);
}
inline uint32_t remainder(uint32_t idle0, uint32_t idle1, uint32_t listed) {
  const uint32_t idle = idle0 + idle1;
  const uint32_t busy = idle < 2000 ? 2000 - idle : 0;
  return busy > listed ? busy - listed : 0;
}
// Identity is xTaskNumber, so even reuse of a deleted task's TCB resets baseline.
// A missing/new task contributes zero until it has two adjacent observations.
inline uint32_t taskDelta(uint32_t counter, uint32_t identity,
                          uint32_t& previous, uint32_t& previousIdentity) {
  const uint32_t used = identity && identity == previousIdentity ? delta(counter, previous) : 0;
  previous = counter;
  previousIdentity = identity;
  return used;
}
}
void cpu_stats_begin();
// Call from loop; report uses the existing 10-second stat cadence.
void cpu_stats_tick(uint32_t nowMs, bool report);
