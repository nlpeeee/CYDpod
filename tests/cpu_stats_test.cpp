#include "../firmware/cydpod_player/app/cpu_stats.h"
#include <cassert>
#include <cstdio>
int main() {
  using namespace cpustats;
  assert(delta(20, 10) == 10);
  assert(delta(5, UINT32_MAX - 4) == 10);
  assert(tenths(500000, 1000000) == 500);
  assert(tenths(1, 0) == 0);
  assert(tenths(UINT32_MAX, 1) == 1000);
  assert(tenths(1234567, 10000000) == 123);
  assert(remainder(500, 250, 1000) == 250);
  assert(remainder(1000, 1000, 1) == 0);
  assert(remainder(1000, 1100, 0) == 0);
  uint32_t previous = 0, identity = 0;
  assert(taskDelta(UINT32_MAX - 4, 1, previous, identity) == 0);
  assert(taskDelta(5, 1, previous, identity) == 10);
  assert(taskDelta(0, 0, previous, identity) == 0); // disappears
  assert(taskDelta(100, 2, previous, identity) == 0); // reappears
  assert(taskDelta(150, 2, previous, identity) == 50);
  assert(taskDelta(200, 3, previous, identity) == 0); // reused TCB
  puts("PASS: cpu stats arithmetic (wrap, percent, clamp, task lifecycle)");
}
