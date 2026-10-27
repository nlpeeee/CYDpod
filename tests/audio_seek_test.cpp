// Executes the production audio task, ring, command mailbox and real MiniAAC.
// FreeRTOS/SD are host stand-ins: this does not validate ESP timing or stack use.
#include "../firmware/cydpod_player/app/audio.cpp"
#include <cassert>
int storage_trackCount(){return 2;}
String storage_trackPath(int n){return n?"front.m4a":"tail.m4a";}
static void waitIdle(){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(audio_isSeeking()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
static bool silent(const int16_t* p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
int main(){
 SD.root="tests/out/miniaac_seek/fixtures";
 audio_begin(0,4000,100,false,REPEAT_ALL);audio_start();waitIdle();
 assert(audio_startupComplete()&&audio_canSeek()&&!audio_isPlaying());assert(audio_positionMs()>=3999&&audio_positionMs()<=4000);
 int16_t out[256];audio_pull(out,128);assert(silent(out,256));
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
 const auto errors=audio_error(nullptr,0);hostReadBudget=0;assert(audio_seekTo(8000));waitIdle();hostReadBudget=-1;
 assert(audio_error(nullptr,0)>errors&&!audio_isPlaying());assert(audio_currentTrack()==1);
 // CY-50: Play after failed seek must reopen the blocked decoder, not just light Play.
 const auto pausedIntent=audio_playIntentSerial();audio_togglePause();waitIdle();
 assert(audio_isPlaying()&&!decodeBlocked&&audio_currentTrack()==1&&audio_positionMs()==0);
 assert(audio_playIntentSerial()!=pausedIntent);audio_togglePause();
 hostReadBudget=0;assert(audio_seekTo(8000));waitIdle();hostReadBudget=-1;
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
 // The timer multiplier changes samples, never the user's volume setting.
 assert(audio_seekTo(2000));waitIdle();audio_togglePause();
 for(int i=0;i<100&&audio_bufferPct()<30;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 const auto userVolume=audio_volume();audio_setSleepGain(0);audio_pull(out,128);assert(silent(out,256));
 audio_setSleepGain(100);audio_pull(out,128);assert(!silent(out,256)&&audio_volume()==userVolume);
 audio_togglePause();
 decodeHandle->stop=true;decodeHandle->cv.notify_one();decodeHandle->thread.join();delete decodeHandle;decodeHandle=nullptr;closeTrack();free(rb);free(order);rb=nullptr;order=nullptr;
 puts("PASS production audio: saved resume, paused/disconnected seek, coalescing, stale-ring removal, track supersession, paused EOF, read failure, Play recovery, stale completion/failure rejection, full-ring commands, sleep gain and retry; queue/play intent retained");
}
