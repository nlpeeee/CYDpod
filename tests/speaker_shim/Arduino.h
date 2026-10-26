#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
using std::min;using std::max;
struct SerialStub {template<class... T>void printf(const char* f,T...a){std::printf(f,a...);}void println(const char* s){std::puts(s);}};
inline SerialStub Serial;
struct ESPStub{unsigned getFreeHeap(){return 64000;}unsigned getMaxAllocHeap(){return 48000;}};
inline ESPStub ESP;
inline uint32_t millis(){return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline void delay(uint32_t){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
constexpr unsigned portMAX_DELAY=0xffffffff,pdTRUE=1,pdPASS=1;
inline unsigned pdMS_TO_TICKS(unsigned ms){return ms;}
struct Task{std::thread thread;std::mutex mutex;std::condition_variable cv;bool notified=false;std::atomic<bool> stop{false};};
using TaskHandle_t=Task*;
inline thread_local Task* thisTask=nullptr;
inline bool failTask=false;inline int liveTasks=0;
struct StopTask{};
inline void vTaskDelay(unsigned){if(thisTask->stop)throw StopTask{};delay(1);if(thisTask->stop)throw StopTask{};}
inline unsigned ulTaskNotifyTake(unsigned,unsigned){std::unique_lock<std::mutex> lock(thisTask->mutex);thisTask->cv.wait(lock,[]{return thisTask->notified||thisTask->stop;});if(thisTask->stop)throw StopTask{};thisTask->notified=false;return 1;}
inline unsigned xTaskNotifyGive(Task* t){{std::lock_guard<std::mutex> lock(t->mutex);t->notified=true;}t->cv.notify_one();return 1;}
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned stack,void* arg,int,TaskHandle_t* out,int core){
  if(failTask){failTask=false;return 0;}if(stack!=3072||core!=0)throw std::runtime_error("speaker stack/core changed");
  Task* t=new Task;*out=t;++liveTasks;t->thread=std::thread([=]{thisTask=t;try{fn(arg);}catch(const StopTask&){};});return pdPASS;
}
inline void vTaskDelete(Task* t){t->stop=true;t->cv.notify_one();t->thread.join();delete t;--liveTasks;}
