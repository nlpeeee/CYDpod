#include "cpu_stats.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef CYD_UPLOADER
namespace {
enum Slot { Decoder, Loop, Ui, BtApp, Btc, Btu, Speaker, Wired, Controller, Count };
const char* const names[Count] = {
  "m4adec", "loopTask", "ui", "BtAppT", "BTC_TASK", "BTU_TASK",
  "spkout", "wiredout", "btController"
};
struct Task {
  TaskHandle_t handle;
  uint32_t previous, identity, used;
};
struct State {
  Task tasks[Count];
  int64_t previousWall;
  uint64_t wall;
  uint32_t previousIdle[2], idle[2], lastTasks;
};
State state;
static_assert(sizeof(State) + sizeof(names) < 400, "CPU diagnostic static RAM budget");

void percent(const char* label, uint32_t value) {
  Serial.printf(" %s=%lu.%lu", label, (unsigned long)(value / 10), (unsigned long)(value % 10));
}
unsigned found() {
  unsigned count = 0;
  for (const Task& t : state.tasks) if (t.handle) ++count;
  return count;
}
void sample(int64_t now) {
  TaskStatus_t info; // ONE reusable status; no task-list array or formatted API.
  for (unsigned i = 0; i < Count; ++i) {
    Task& t = state.tasks[i];
    // Revalidate cached handles before use: route changes may delete writers.
    // No heap; bounded name lookups also discover late Bluetooth startup.
    t.handle = xTaskGetHandle(names[i]);
    if (!t.handle) {
      cpustats::taskDelta(0, 0, t.previous, t.identity);
      continue;
    }
    vTaskGetInfo(t.handle, &info, pdFALSE, eInvalid);
    t.used += cpustats::taskDelta(info.ulRunTimeCounter, info.xTaskNumber,
                                t.previous, t.identity);
  }
  for (unsigned core = 0; core < 2; ++core) {
    const uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(core);
    if (state.previousWall) state.idle[core] += cpustats::delta(idle, state.previousIdle[core]);
    state.previousIdle[core] = idle;
  }
  if (state.previousWall) state.wall += uint64_t(now - state.previousWall);
  state.previousWall = now;
}
void printCpu() {
  const uint32_t idle0 = cpustats::tenths(state.idle[0], state.wall);
  const uint32_t idle1 = cpustats::tenths(state.idle[1], state.wall);
  uint32_t listed = 0;
  Serial.printf("stat: cpu dtUs=%llu", (unsigned long long)state.wall);
  percent("idle0", idle0); percent("idle1", idle1);
  const unsigned slots[] = {Decoder, Loop, Btc, Btu, BtApp};
  for (unsigned i : slots) {
    const uint32_t pct = cpustats::tenths(state.tasks[i].used, state.wall);
    percent(names[i], pct); listed += pct;
  }
  const uint32_t writer = cpustats::tenths(state.tasks[Speaker].used + state.tasks[Wired].used, state.wall);
  percent("writer", writer); listed += writer;
  percent("other", cpustats::remainder(idle0, idle1, listed));
  const unsigned total = uxTaskGetNumberOfTasks(), mapped = found();
  Serial.printf(" unmapped=%u\n", total > mapped ? total - mapped : 0);
  state.wall = 0;
  state.idle[0] = state.idle[1] = 0;
  for (Task& t : state.tasks) t.used = 0;
}
void printTasks() {
  TaskStatus_t info;
  Serial.print("stat: tasks");
  for (unsigned i = 0; i < Count; ++i) {
    const TaskHandle_t handle = xTaskGetHandle(names[i]);
    if (!handle) continue;
    vTaskGetInfo(handle, &info, pdFALSE, eInvalid);
    // IDF high-water API returns BYTES, not vanilla FreeRTOS stack words.
    const unsigned stack = uxTaskGetStackHighWaterMark(handle);
    const BaseType_t core = xTaskGetCoreID(handle);
    if (core == tskNO_AFFINITY)
      Serial.printf(" %s=*/%u/%u", names[i], (unsigned)info.uxCurrentPriority, stack);
    else
      Serial.printf(" %s=%d/%u/%u", names[i], (int)core, (unsigned)info.uxCurrentPriority, stack);
  }
  Serial.println();
}
}
void cpu_stats_begin() {
  sample(esp_timer_get_time());
  Serial.printf("cmd: cpustats ok tasks=%u timer=esp_timer\n", found());
}
void cpu_stats_tick(uint32_t nowMs, bool report) {
  const int64_t now = esp_timer_get_time();
  if (now - state.previousWall >= 1000000 || report) sample(now);
  if (!report) return;
  printCpu();
  if ((!state.lastTasks && nowMs >= 20000) ||
      (state.lastTasks && uint32_t(nowMs - state.lastTasks) >= 60000)) {
    printTasks();
    state.lastTasks = nowMs;
  }
}
#else
void cpu_stats_begin() {}
void cpu_stats_tick(uint32_t, bool) {}
#endif
