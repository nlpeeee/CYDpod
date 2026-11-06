// Executes the production audio task, ring, command mailbox and real MiniAAC.
// FreeRTOS/SD are host stand-ins: this does not validate ESP timing or stack use.
#include "../firmware/cydpod_player/app/audio.cpp"
#include <cassert>
#include <vector>
int storage_trackCount(){return 2;}
static bool missingTrack=false;
static const char* formatPath=nullptr;
String storage_trackPath(int n){return formatPath?formatPath:missingTrack?"missing.m4a":n?"front.m4a":"tail.m4a";}
static void waitIdle(){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(audio_isSeeking()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
static void waitBuffered(){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(audio_bufferPct()<80){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
static bool silent(const int16_t* p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
static int seekFormat(const char* path){
 // Capture the uninterrupted production decoder output before starting its task.
 std::vector<M4aStereo> reference;CodecTrack t;File source(path,"r");assert(source);
 assert(t.begin(source)==MINIAAC_OK&&t.sampleRate()==44100);
 for(;;){auto rc=t.next();assert(rc!=CodecTrack::Pull::error);if(rc==CodecTrack::Pull::eof)break;
  reference.insert(reference.end(),t.pcm(),t.pcm()+t.pending());t.consume(t.pending());}
 t.end();source.close();assert(reference.size()>44100);
 formatPath=path;SD.root="/";audio_begin(0,0,100,false,REPEAT_ALL);audio_start();waitIdle();
 assert(audio_canSeek()&&!audio_isPlaying());
 for(uint32_t ms:{500u,1000u,10u,750u}){
  assert(audio_seekTo(ms));waitIdle();assert(!audio_isPlaying());
  assert(audio_positionMs()>=ms-1&&audio_positionMs()<=ms);waitBuffered();
  audio_setPlaying(true);int16_t out[256];audio_pull(out,128);audio_setPlaying(false);
  assert(!memcmp(out,reference.data()+uint64_t(ms)*44100/1000,sizeof(out)));
 }
 audio_seekRelative(1);waitIdle();assert(audio_positionMs()>=1751&&audio_positionMs()<=1753);
 assert(audio_seekTo(UINT32_MAX));waitIdle();assert(audio_positionMs()<=reference.size()*1000/44100);
 assert(audio_seekTo(100));waitIdle();assert(!audio_commands().blocked);
 decodeHandle->stop=true;decodeHandle->cv.notify_one();decodeHandle->thread.join();delete decodeHandle;decodeHandle=nullptr;closeTrack();free(rb);
 printf("PASS production WAV/MP3 seek: paused, relative, backwards, clamped EOF, exact PCM, truthful position: %s\n",path);return 0;
}
static int resampleTask(const char* path,bool expectReject){
 formatPath=path;SD.root="/";audio_begin(0,0,100,false,REPEAT_ALL);audio_start();waitIdle();
 if(expectReject){
  char error[48];assert(audio_error(error,sizeof(error))>0&&!strcmp(error,"flac_bits"));
  assert(audio_commands().blocked&&!audio_canSeek()&&!audio_isPlaying());
  formatPath="tests/out/miniaac_seek/fixtures/tail.m4a";SD.root=".";audio_playTrack(0);waitIdle();
  assert(!audio_commands().blocked&&audio_canSeek());waitBuffered();
  puts("PASS real 96k/24-bit FLAC: flac_bits error published before decode, next playback command recovers");
 }else{
  audio_setPlaying(true);int16_t out[256];
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
  while(audio_pullFrames()<20000 && std::chrono::steady_clock::now()<deadline)audio_pull(out,128);
  assert(audio_pullFrames()>10000&&!audio_commands().blocked);
  assert(hostOneTickYields>0); // independent of the ring-full 4-tick wait
  // A command still completes with the fast consumer and resampler active.
  audio_setPlaying(false);assert(audio_seekTo(100));waitIdle();
  printf("PASS production 48k task: fast consumer, %u voluntary yields, command acknowledged\n",unsigned(hostOneTickYields));
 }
 decodeHandle->stop=true;decodeHandle->cv.notify_one();decodeHandle->thread.join();delete decodeHandle;decodeHandle=nullptr;closeTrack();free(rb);return 0;
}
int main(int argc,char** argv){
 // CY-72: a fresh process models boot after an output-switch restart. The
 // persisted queue identity is restored before releasing the startup barrier.
 if(argc==2&&!strcmp(argv[1],"--output-resume")){
   SD.root="tests/out/miniaac_seek/fixtures";
   audio_begin(1,8456,40,false,REPEAT_ALL);
   audio_setQueue(nullptr,2,1,false);audio_start();waitIdle();waitBuffered();
   assert(audio_startupComplete()&&audio_currentTrack()==1&&!audio_isPlaying());
   assert(audio_queuePos(1)>=0&&audio_positionMs()>=8455&&audio_positionMs()<=8456);
   int16_t out[256];const auto pos=audio_positionMs();
   for(int i=0;i<10;++i){audio_pull(out,128);assert(silent(out,256));}
   assert(audio_positionMs()==pos&&!audio_isPlaying());
   decodeHandle->stop=true;decodeHandle->cv.notify_one();decodeHandle->thread.join();delete decodeHandle;decodeHandle=nullptr;closeTrack();free(rb);
   puts("OUTPUT_RESTART_RESUME_OK: same track 1, queue position and 8456 ms; paused pulls stay silent without advancing");return 0;
 }
 if(argc>2)return resampleTask(argv[1],!strcmp(argv[2],"reject"));
 if(argc>1)return seekFormat(argv[1]);
 SD.root="tests/out/miniaac_seek/fixtures";
 audio_begin(0,4000,100,false,REPEAT_ALL);audio_start();waitIdle();
 assert(audio_startupComplete()&&audio_canSeek()&&!audio_isPlaying());assert(audio_positionMs()>=3999&&audio_positionMs()<=4000);
 int16_t out[256];audio_pull(out,128);assert(silent(out,256));
 // CY54-02: idempotent remote Pause/Stop must revoke a pairing resume claim.
 audio_setPlaying(true);const auto userIntent=audio_playIntentSerial();
 audio_pauseForPairing();assert(!audio_isPlaying()&&audio_playIntentSerial()==userIntent);
 audio_resumeAfterPairing();assert(audio_isPlaying()&&audio_playIntentSerial()==userIntent);
 for(int key=0;key<2;++key){ // Pause and Stop both dispatch this explicit setter.
   audio_pauseForPairing();const auto pairIntent=audio_playIntentSerial();
   audio_setPlaying(false);assert(!audio_isPlaying()&&audio_playIntentSerial()!=pairIntent);
   if(cyd::currentCompletion(pairIntent,audio_playIntentSerial()))audio_resumeAfterPairing();
   assert(!audio_isPlaying());
 }
 audio_setPlaying(true);const auto playIntent=audio_playIntentSerial();
 audio_setPlaying(true);assert(audio_isPlaying()&&audio_playIntentSerial()!=playIntent);
 audio_setPlaying(false);
 assert(audio_seekTo(4000));waitIdle();audio_seekRelative(1);audio_seekRelative(1);waitIdle();
 assert(audio_positionMs()>=5999&&audio_positionMs()<=6000); // relative requests accumulate even before acknowledgment
 const auto epoch=audio_trackSerial();
 assert(audio_seekTo(7000));waitIdle();assert(audio_trackSerial()==epoch&&!audio_isPlaying());assert(audio_positionMs()>=6999&&audio_positionMs()<=7000);
 // No callback was needed to acknowledge either paused seek.
 for(int i=0;i<30;++i)assert(audio_seekTo(i%2?3000:9000));
 assert(audio_seekTo(5000));waitIdle();assert(audio_positionMs()>=4999&&audio_positionMs()<=5000);
 assert(!audio_isPlaying()&&audio_currentTrack()==0&&audio_queueLen()==2);
 // Pull must begin at new target, rather than stale frames buffered before seek.
 static uint8_t refWorkspace[MINIAAC_WORKSPACE_BYTES]; static int16_t refPcm[MINIAAC_PCM_SAMPLES];
 M4aTrack reference(refWorkspace,refPcm);File ref=SD.open("tail.m4a");assert(reference.begin(ref)==MINIAAC_OK);assert(reference.seek(5000,nullptr,nullptr)==MINIAAC_OK);
 audio_togglePause();for(int i=0;i<100&&audio_bufferPct()<10;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 audio_pull(out,128);assert(!silent(out,256));assert(!memcmp(out,reference.pcm(),sizeof(out)));reference.end();ref.close();
 // Track changes supersede old seek; newer seek cannot target an opening file.
 assert(audio_seekTo(10000));audio_playTrack(1);assert(!audio_seekTo(4000));waitIdle();assert(audio_currentTrack()==1&&audio_positionMs()==0);
 audio_togglePause();assert(!audio_isPlaying());assert(audio_seekTo(UINT32_MAX));waitIdle();assert(audio_currentTrack()==1&&!audio_isPlaying());
 for(int i=0;i<15;++i){audio_tick();std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(audio_currentTrack()==1); // paused near EOF stays in queue
 audio_seekRelative(-10);waitIdle();assert(audio_positionMs()<2001);
 // Real consumer/producer overlap while issuing seek commands.
 audio_togglePause();std::atomic<bool> stopConsumer{false};
 std::thread consumer([&]{int16_t pcm[256];while(!stopConsumer){audio_pull(pcm,128);std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
 for(int i=0;i<60;++i){assert(audio_seekTo(1000+(i*137)%9000));std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(audio_seekTo(6000));waitIdle();stopConsumer=true;consumer.join();audio_togglePause();
 assert(!audio_isPlaying()&&audio_currentTrack()==1&&audio_positionMs()>=5999&&audio_positionMs()<6500);
 const auto errors=audio_error(nullptr,0);waitBuffered();hostReadBudget=0;assert(audio_seekTo(8000));waitIdle();hostReadBudget=-1;
 assert(audio_error(nullptr,0)>errors&&!audio_isPlaying());assert(audio_currentTrack()==1);
 // CY-50: Play after failed seek must reopen the blocked decoder, not just light Play.
 const auto pausedIntent=audio_playIntentSerial();audio_togglePause();waitIdle();
 assert(audio_isPlaying()&&!decodeBlocked&&audio_currentTrack()==1&&audio_positionMs()==0);
 assert(audio_playIntentSerial()!=pausedIntent);audio_togglePause();
 waitBuffered();hostReadBudget=0;assert(audio_seekTo(8000));waitIdle();hostReadBudget=-1;
 assert(!audio_isPlaying()&&decodeBlocked);
 assert(audio_seekTo(2000));waitIdle();assert(audio_positionMs()>=1999&&audio_positionMs()<=2000);assert(!audio_isPlaying());
 { const auto before=audio_error(nullptr,0);file.close();assert(audio_seekTo(1000));waitIdle();assert(audio_error(nullptr,0)==before&&!audio_commands().seeking);} // F-05: seek after closeTrack is ignored
 // A completion/failure from the former command cannot skip a newer selection.
 const auto former=commandSerial;audio_playTrack(0);
 endedCommand=former;++endedSerial;failedCommand=former;++failSerial;audio_tick();waitIdle();
 assert(audio_currentTrack()==0);audio_togglePause();
 // Full ring, paused, and no consumer: every newer request must still be noticed.
 for(int i=0;i<100&&audio_bufferPct()<80;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 assert(audio_bufferPct()>=80);audio_next();audio_next();audio_prev();waitIdle();
 assert(audio_currentTrack()==1&&!audio_isPlaying());
 const auto commands=audio_commands();assert(commands.track==-1&&commands.seekMs==-1&&!commands.seeking);
 for(int i=0;i<1000&&audio_commands().opening;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 assert(!audio_commands().opening); // CY-118: opened, paused, nothing pulling is settled; the output switch quiesce waits on this
 // The timer multiplier changes samples, never the user's volume setting.
 assert(audio_seekTo(2000));waitIdle();audio_togglePause();
 for(int i=0;i<100&&audio_bufferPct()<30;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 const auto userVolume=audio_volume();audio_setSleepGain(0);audio_pull(out,128);assert(silent(out,256));
 audio_setSleepGain(100);audio_pull(out,128);assert(!silent(out,256)&&audio_volume()==userVolume);
 audio_togglePause();
 // CY54-05: freeze the producer so Next/Previous and Play share a pending mailbox.
 for(int previous=0;previous<2;++previous){
   audio_playTrack(0);waitIdle();audio_setPlaying(false);
   waitBuffered();hostReadBudget=0;assert(audio_seekTo(8000));waitIdle();hostReadBudget=-1;
   assert(audio_commands().blocked&&!audio_isPlaying());
   decodeHandle->pause=true;
   const auto pauseDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
   while(!decodeHandle->atPause){assert(std::chrono::steady_clock::now()<pauseDeadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
   if(previous)audio_prev();else audio_next();
   const auto pending=audio_commands();assert(pending.track==1);
   audio_setPlaying(true);const auto afterPlay=audio_commands();
   assert(afterPlay.track==pending.track&&afterPlay.serial==pending.serial);
   decodeHandle->pause=false;waitIdle();assert(audio_currentTrack()==1&&audio_isPlaying());
   audio_setPlaying(false);
 }
 // CY54-03: a one-song queue must recover from exhausted open/begin/decode failures.
 audio_setQueue(nullptr,1,0,false);
 auto retryTerminalFailure=[&]{
   audio_tick();assert(!audio_isPlaying()&&audio_commands().blocked);
   const auto opened=audio_trackSerial();audio_setPlaying(true);waitIdle();
   assert(audio_isPlaying()&&!audio_commands().blocked&&audio_currentTrack()==0&&audio_trackSerial()>opened);
   for(int i=0;i<100&&audio_bufferPct()<10;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
   audio_pull(out,128);assert(!silent(out,256));audio_setPlaying(false);
 };
 missingTrack=true;audio_playTrack(0);waitIdle();missingTrack=false;
 retryTerminalFailure();
 hostReadBudget=0;audio_playTrack(0);waitIdle();hostReadBudget=-1;
 retryTerminalFailure();
 audio_playTrack(0);waitIdle();const auto failureBefore=failSerial;hostReadBudget=0;
 const auto failureDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(failSerial==failureBefore){
   assert(std::chrono::steady_clock::now()<failureDeadline);audio_pull(out,128);
   std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 hostReadBudget=-1;retryTerminalFailure();
 decodeHandle->stop=true;decodeHandle->cv.notify_one();decodeHandle->thread.join();delete decodeHandle;decodeHandle=nullptr;closeTrack();free(rb);rb=nullptr;
 puts("PASS production audio: saved resume, paused/disconnected seek, coalescing, stale-ring removal, track supersession, paused EOF, read failure, Play recovery, stale completion/failure rejection, full-ring commands, sleep gain, idempotent intent, pending skip retained on Play, terminal open/begin/decode retry; queue/play intent retained");
}
