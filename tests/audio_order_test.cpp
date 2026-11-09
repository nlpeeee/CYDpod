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

  // Up Next: front/back order, the list resumes after the interrupted song, prev, repeat, cap.
  tracks=10;unavailable=false;audio_setRepeat(REPEAT_ALL);
  audio_setQueueSource({nullptr,nullptr,nullptr},tracks,2,false);audio_setShuffle(false);
  audio_upNextClear();audio_playTrack(2);assert(orderPos==2&&reqTrack==2&&!reqContinuous&&seeking);
  const uint16_t back2[]={7,8},front1[]={5};
  assert(audio_upNextAdd(back2,2,false)==2&&audio_upNextAdd(front1,1,true)==1);
  assert(audio_upNextLen()==3&&audio_upNextAt(0)==5&&audio_upNextAt(1)==7&&audio_upNextAt(2)==8);
  step();assert(reqTrack==5&&audio_playingUpNext()&&orderPos==2&&audio_upNextLen()==2);
  step(true);assert(reqTrack==2&&!audio_playingUpNext()&&orderPos==2);   // back to the song it interrupted
  step();assert(reqTrack==7&&audio_playingUpNext());
  advance(true,true);assert(reqTrack==8&&reqContinuous&&!seeking);         // natural end: gapless, tail stays audible
  advance(true,true);assert(reqTrack==3&&orderPos==3&&!audio_playingUpNext()&&reqContinuous);
  step();assert(reqTrack==4&&!reqContinuous&&seeking);                     // a skip still silences the old tail
  audio_upNextAdd(front1,1,false);audio_setRepeat(REPEAT_ONE);curTrack=4;
  advance(true,true);assert(reqTrack==4&&reqContinuous&&audio_upNextLen()==1); // Repeat One wins over Up Next
  audio_setRepeat(REPEAT_OFF);playing=true;orderPos=tracks-1;
  advance(true,true);assert(reqTrack==5&&playing&&audio_upNextLen()==0);   // queued songs play before the end
  rbTail=0;rbHead=100;advance(true,true);assert(stopPending&&playing);       // the end waits for the last tail
  audio_tick();assert(stopPending&&playing);
  rbTail=100;audio_tick();assert(!stopPending&&!playing&&orderPos==0&&reqTrack==0);
  uint16_t many[80];for(int i=0;i<80;++i)many[i]=uint16_t(i%tracks);
  assert(audio_upNextAdd(many,80,false)==AUDIO_UP_NEXT_CAP&&audio_upNextAdd(front1,1,true)==0);
  assert(audio_upNextRemove(0)&&audio_upNextLen()==AUDIO_UP_NEXT_CAP-1&&audio_upNextAt(0)==1);
  const uint16_t mixed[]={uint16_t(tracks),3};assert(audio_upNextAdd(mixed,2,true)==1&&audio_upNextAt(0)==3); // unknown ids skipped
  audio_upNextClear();assert(audio_upNextLen()==0&&audio_upNextAt(0)==-1);
  audio_upNextAdd(back2,2,false);playing=false;
  assert(audio_upNextPlay(1)&&reqTrack==8&&playing&&audio_playingUpNext()&&audio_upNextLen()==1&&audio_upNextAt(0)==7);
  audio_upNextClear();
  fprintf(stderr,"PASS up next: front/back order, list resumes, prev, Repeat One/Off, deferred end, cap %d, play now\n",AUDIO_UP_NEXT_CAP);

  // Gapless position carry: the old tail never counts toward the new track's clock.
  int16_t out[2*256];
  seeking=false;playing=true;reqTrack=-1;reqSeekMs=-1;rbTail=0;rbHead=300;
  carryFrames=200;framesPlayed=0;posBaseMs=0;starting=true;gapArmed=gapOpened=true;gapFrames=0;
  audio_pull(out,150);assert(framesPlayed==0&&carryFrames==50&&starting&&gapArmed);
  const uint32_t serialBefore=gapSerial,underrunsBefore=underruns;
  audio_pull(out,100);assert(framesPlayed==50&&carryFrames==0&&!starting&&!gapArmed&&gapLast==0&&gapSerial==serialBefore+1);
  // A real gap: the ring runs dry between the tail and the new track's first frame.
  rbTail=rbHead=0;gapArmed=true;gapOpened=false;gapFrames=0;
  audio_pull(out,128);assert(gapArmed&&gapFrames==128&&underruns==underrunsBefore);
  gapOpened=true;rbHead=64;audio_pull(out,128);assert(!gapArmed&&gapLast==192&&underruns==underrunsBefore);
  fprintf(stderr,"PASS gapless carry: tail frames excluded from the clock, boundary gap measured, not an underrun\n");
  decodeHandle=nullptr;rb=nullptr;
}
