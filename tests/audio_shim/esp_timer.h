#pragma once
#include <chrono>
#include <cstdint>
inline int64_t hostTimerUs = -1; // deterministic timer for diagnostic tests only
inline int64_t esp_timer_get_time(){
  if(hostTimerUs>=0){const auto t=hostTimerUs;hostTimerUs+=5;return t;}
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
