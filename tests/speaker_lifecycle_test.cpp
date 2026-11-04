#include <Arduino.h>
#include <driver/dac_continuous.h>
#include <set>
#include <iostream>
static std::set<void*> allocations;
static int failAllocation=-1;
static void* testMalloc(size_t n){if(failAllocation==0){failAllocation=-1;return nullptr;}if(failAllocation>0)--failAllocation;void* p=std::malloc(n);if(p)allocations.insert(p);return p;}
static void testFree(void* p){if(!p)return;if(dacWriting)throw std::runtime_error("freed speaker storage during write");if(!allocations.erase(p))throw std::runtime_error("double/foreign free");std::free(p);}
static void cydAmpEnable(){}
static void cydAmpDisable(){}
static std::atomic<unsigned> pulled{0};
void audio_pull(int16_t* p,int32_t frames){memset(p,0,frames*4);++pulled;}
#define malloc testMalloc
#define free testFree
#include "../firmware/cydpod_player/app/speaker.cpp"
#undef malloc
#undef free
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>static void until(F condition){const uint32_t start=millis();while(!condition()&&millis()-start<1500)delay(1);check(condition(),"timed out waiting for mock writer");}
int main(){try{
  check(speaker_begin(OUT_BT),"BT startup");check(liveTasks==0&&allocations.empty()&&!dacEnabled,"BT has no writer or buffers");
  for(int failure=0;failure<2;++failure){failAllocation=failure;check(!speaker_setRoute(OUT_SPEAKER),"buffer allocation failure reported");check(speaker_route()==OUT_BT&&allocations.empty()&&liveTasks==0,"buffer failure rolls back to BT");}
  failTask=true;check(!speaker_setRoute(OUT_SPEAKER),"task failure reported");check(speaker_route()==OUT_BT&&allocations.empty()&&liveTasks==0,"task failure releases both buffers");
  for(int i=0;i<25;++i){unsigned before=pulled;check(speaker_setRoute(OUT_SPEAKER),"speaker starts");until([&]{return pulled>before;});check(allocations.size()==2&&liveTasks==1,"speaker owns exactly two buffers and one task");check(speaker_setRoute(OUT_BT),"BT handoff");check(allocations.empty()&&liveTasks==0&&!dacEnabled,"BT handoff stops DAC before freeing buffers/task");}
  check(speaker_setRoute(OUT_SPEAKER),"start for restart teardown");
  until([]{return dacEnabled.load();});
  check(speaker_setRoute(OUT_NONE),"restart parks writer on no output");
  check(speaker_route()==OUT_NONE&&allocations.empty()&&liveTasks==0&&!dacEnabled,"restart final teardown frees only after park");
  check(speaker_setRoute(OUT_NONE),"repeat park is safe");
  check(speaker_setRoute(OUT_BT),"restore route after simulated save failure");
  failDac=true;check(speaker_setRoute(OUT_SPEAKER),"asynchronous DAC failure setup");until([]{return speaker_route()==OUT_NONE;});check(speaker_setRoute(OUT_BT),"recover after DAC failure");check(allocations.empty()&&liveTasks==0,"DAC failure cleanup");
  check(speaker_setRoute(OUT_SPEAKER),"start for stalled DAC test");until([]{return pulled>0&&dacEnabled;});blockWrites=true;until([]{return dacWriting.load();});
  check(!speaker_setRoute(OUT_BT),"park timeout reported");check(speaker_route()==OUT_SPEAKER&&allocations.size()==2&&liveTasks==1,"timeout preserves buffers/task and old route");
  blockWrites=false;check(speaker_setRoute(OUT_BT),"retry handoff after stalled write");check(allocations.empty()&&liveTasks==0&&!dacEnabled,"retry releases everything");
  std::cout<<"PASS speaker lifecycle: BT has no allocations, 25 route cycles, both buffer failures, task/DAC failure, blocked-write timeout and safe retry\n";return 0;
}catch(const std::exception& e){blockWrites=false;if(writerHandle){route=OUT_NONE;while(!parked)delay(1);vTaskDelete(writerHandle);}std::cerr<<"FAIL speaker lifecycle: "<<e.what()<<"\n";return 1;}}
