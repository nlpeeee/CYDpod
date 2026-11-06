// Production order/navigation under host shims; no decoder or device started.
#include "../firmware/cydpod_player/app/audio.cpp"
#include <cassert>
#include <new>
static size_t allocations=0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n){++allocations;return __real_malloc(n);}
void* operator new(size_t n){++allocations;if(void* p=__real_malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,size_t) noexcept {free(p);}
static int tracks=0;
int storage_trackCount(){return tracks;}
static bool unavailable=false;
static int sourceAt(void*,int pos){return unavailable?-1:tracks-1-pos;}
static void step(bool previous=false){if(previous)audio_prev();else audio_next();}
int main(){
  static_assert(sizeof(AudioOrder)<100,"resident bound");
  freopen("/dev/null","w",stdout); // production command diagnostics
  static int16_t ring[RB_FRAMES*2];rb=ring;
  HostAudioTask task;decodeHandle=&task;
  static bool seen[10000];
  for(int n:{1,2,3,2000,10000}){
    tracks=n;
    for(uint32_t seed:{0u,1u,0x12345678u,0xffffffffu}){
      for(int first:{-1,0,n/2,n-1}){
        AudioOrder a,b;a.reset(n,true,first,seed);b.reset(n,true,first,seed);
        memset(seen,0,sizeof(seen));
        const size_t before=allocations;
        for(int i=0;i<n;++i){const int id=a.at(i);
          assert(id>=0&&id<n&&!seen[id]);seen[id]=true;
          assert(id==b.at(i)&&a.position(id)==i);
        }
        assert(allocations==before);
        assert(first<0||a.at(0)==first);
        assert(a.at(-1)==-1&&a.at(n)==-1&&a.position(n)==-1);
      }
    }
    const int selected=n/2;
    audio_setQueueSource({nullptr,nullptr,sourceAt},n,selected,true);
    audio_setShuffle(true); // restore selected-first even on the first case
    audio_setQueueSource({nullptr,nullptr,sourceAt},n,selected,true);
    audio_playTrack(selected);assert(orderPos==0&&reqTrack==selected);
    memset(seen,0,sizeof(seen));
    const size_t before=allocations;
    for(int i=0;i<n;++i){
      const int id=reqTrack;assert(id>=0&&id<n&&!seen[id]);seen[id]=true;
      step();const int next=reqTrack;step(true);assert(reqTrack==id);
      step();assert(reqTrack==next);
    }
    assert(reqTrack==selected&&allocations==before);
    audio_setRepeat(REPEAT_ALL);orderPos=n-1;advance(true);assert(orderPos==0&&reqTrack==selected);
    audio_setRepeat(REPEAT_OFF);playing=true;orderPos=n-1;advance(true);assert(!playing&&orderPos==0);
    audio_setRepeat(REPEAT_ONE);curTrack=selected;orderPos=n-1;advance(true);assert(orderPos==n-1&&reqTrack==selected);
    audio_setShuffle(false);assert(order.at(n-1)==n-1);
    audio_playTrack(selected);assert(orderPos==n-1-selected);
    audio_setQueueSource({nullptr,nullptr,sourceAt},n,-1,false);assert(orderPos==-1);
    step();assert(reqTrack==n-1);
    fprintf(stderr,"PASS order N=%d: bijection/deterministic/selected-first/next-prev/repeat; step allocations=0; resident=%zu B\n",n,sizeof(AudioOrder));
  }
  tracks=3;
  AudioOrder a,b;a.reset(tracks,true,-1,1);b.reset(tracks,true,-1,2);
  // Seed keys change even where a tiny domain happens to produce the same map.
  audio_setQueueSource({nullptr,nullptr,sourceAt},tracks,1,true);
  unavailable=true;const auto serial=commandSerial;step();
  assert(!playing&&orderLen==0&&commandSerial==serial);
  char error[48];assert(audio_error(error,sizeof(error))&&!strcmp(error,"queue_unavailable"));
  fprintf(stderr,"PASS order unavailable queue: stopped without invalid request\n");
  decodeHandle=nullptr;rb=nullptr;
}
