#pragma once
#include "../catalog_shim/Arduino.h"
#include <thread>
#include <atomic>
#include <condition_variable>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
constexpr unsigned pdTRUE=1,pdPASS=1;
inline unsigned pdMS_TO_TICKS(unsigned n){return n;}
struct HostAudioTask {std::thread thread;std::mutex mutex;std::condition_variable cv;bool notified=false;std::atomic<bool> stop{false},pause{false},atPause{false};};
using TaskHandle_t=HostAudioTask*;
inline thread_local HostAudioTask* currentTask=nullptr;
struct StopAudioTask{};
inline std::atomic<unsigned> hostOneTickYields{0};
inline void vTaskDelay(unsigned n){
 if(currentTask&&n==1)++hostOneTickYields;
 if(currentTask){
   while(currentTask->pause&&!currentTask->stop){currentTask->atPause=true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
   currentTask->atPause=false;if(currentTask->stop)throw StopAudioTask{};
 }
 std::this_thread::sleep_for(std::chrono::milliseconds(n));
 if(currentTask&&currentTask->stop)throw StopAudioTask{};
}
inline unsigned ulTaskNotifyTake(unsigned,unsigned){std::unique_lock<std::mutex> lock(currentTask->mutex);currentTask->cv.wait(lock,[]{return currentTask->notified||currentTask->stop;});if(currentTask->stop)throw StopAudioTask{};currentTask->notified=false;return 1;}
inline unsigned xTaskNotifyGive(TaskHandle_t t){{std::lock_guard<std::mutex> lock(t->mutex);t->notified=true;}t->cv.notify_one();return 1;}
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned stack,void* arg,int,TaskHandle_t* out,int core){
 if(stack!=4096||core!=1)throw std::runtime_error("decode stack/core changed");
 auto* t=new HostAudioTask;*out=t;t->thread=std::thread([=]{currentTask=t;try{fn(arg);}catch(const StopAudioTask&){};});return pdPASS;
}
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 0;}
inline uint32_t esp_random(){return 1234;}

inline uint32_t micros(){return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
