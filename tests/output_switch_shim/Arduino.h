// Minimal Arduino shim so firmware/cydpod_player/app/settings.cpp builds on a host g++ (CY-71).
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
using std::max;
using std::min;
#define constrain(a, lo, hi) ((a) < (lo) ? (lo) : ((a) > (hi) ? (hi) : (a)))
struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return (unsigned)s.size(); }
  bool isEmpty() const { return s.empty(); }
};

#include <atomic>
#include <stdexcept>
inline std::string serialLog;
struct SerialStub {
  template<class... T> void printf(const char* f,T... a){char b[1024];snprintf(b,sizeof(b),f,a...);serialLog+=b;}
  void println(const char* s=""){serialLog+=s;serialLog+='\n';}
  void flush(){}
};
inline SerialStub Serial;
inline unsigned restarts=0;
struct ESPStub {unsigned getFreeHeap(){return 64000;}unsigned getMaxAllocHeap(){return 48000;}void restart(){++restarts;}};
inline ESPStub ESP;
inline uint32_t clockMs=0;
inline void (*delayHook)()=nullptr;
inline uint32_t millis(){return clockMs;}
inline void delay(uint32_t ms){clockMs+=ms;if(delayHook)delayHook();}
constexpr unsigned portMAX_DELAY=0xffffffff,pdTRUE=1,pdPASS=1;
inline unsigned pdMS_TO_TICKS(unsigned ms){return ms;}
using TaskHandle_t=int*;
inline unsigned ulTaskNotifyTake(unsigned,unsigned){return 1;}
inline void vTaskDelay(unsigned ms){delay(ms);}
inline unsigned xTaskNotifyGive(TaskHandle_t){return 1;}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,int,TaskHandle_t* out,int){*out=new int;return pdPASS;}
inline void vTaskDelete(TaskHandle_t t){delete t;}
inline void cydAmpEnable(){}
inline void cydAmpDisable(){}
