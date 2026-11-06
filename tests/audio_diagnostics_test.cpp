// Exercise production copy/reset and pull counters with fake window values.
#include "../firmware/cydpod_player/app/audio.cpp"
#include <cassert>
#include <atomic>
int storage_trackCount(){return 0;}
String storage_trackPath(int){return "";}
int main(){
  flacOpen=true;
  audio_flacReadTime(10001);audio_flacReadTime(10000);
  diagnostic.flac.frames=3;diagnostic.flac.nextMaxUs=40000;
  diagnostic.flac.nonIoMaxUs=19999;diagnostic.flac.ringStartMin=17;diagnostic.flac.block=4096;
  const auto a=audio_takeDiagnosticWindow();
  assert(a.flac.sdSumUs==20001&&a.flac.sdMaxUs==10001&&a.flac.readsGt10==1);
  assert(a.flac.frames==3&&a.flac.nextMaxUs==40000&&a.flac.nonIoMaxUs==19999);
  assert(a.flac.ringStartMin==17&&a.flac.block==4096);
  const auto b=audio_takeDiagnosticWindow();
  assert(b.flac.active&&b.flac.frames==0&&b.flac.sdSumUs==0&&b.flac.sdMaxUs==0);
  assert(b.flac.nextMaxUs==0&&b.flac.nonIoMaxUs==0&&b.flac.readsGt10==0&&b.flac.block==0);
  assert(b.flac.ringStartMin==UINT32_MAX&&flacSdTotalUs==20001);
  // Interleaved writer/readers must return a complete 64-bit value and matching fields.
  std::atomic<bool> done{false};
  std::thread writer([&]{for(uint32_t n=1;n<=10000;++n){
    portENTER_CRITICAL(&stateLock);
    diagnostic.flac.sdSumUs=(uint64_t(n)<<32)|n;diagnostic.flac.frames=n;
    portEXIT_CRITICAL(&stateLock);
  }done=true;});
  do{const auto w=audio_takeDiagnosticWindow();
    assert(uint32_t(w.flac.sdSumUs>>32)==uint32_t(w.flac.sdSumUs));
    assert(w.flac.frames==uint32_t(w.flac.sdSumUs));
  }while(!done);
  writer.join();audio_takeDiagnosticWindow();
  int16_t ring[RB_FRAMES*2]={},out[256];rb=ring;playing=true;seeking=false;
  rbHead=64;rbTail=0;hostTimerUs=10000;audio_pull(out,128);
  hostTimerUs=10500;audio_pull(out,128); // same burst
  hostTimerUs=40000;audio_pull(out,128); // new burst
  auto p=audio_takeDiagnosticWindow().pull;
  assert(p.calls==3&&p.gapMaxUs==29500&&p.burstFrames==256&&p.missingFrames==320);
  assert(p.lockWaitUs==5&&p.lockHoldUs==5&&!strcmp(p.task,"hostpull")&&p.core==0&&p.prio==10);
  p=audio_takeDiagnosticWindow().pull;
  assert(!p.calls&&!p.gapMaxUs&&!p.burstFrames&&!p.lockWaitUs&&!p.lockHoldUs&&!p.missingFrames);
  assert(!strcmp(p.task,"hostpull"));
  hostTimerUs=40500;playing=false;audio_pull(out,128);
  p=audio_takeDiagnosticWindow().pull;
  assert(p.calls==1&&p.gapMaxUs==500&&p.burstFrames==128&&!p.missingFrames);
  flacOpen=false;audio_takeDiagnosticWindow();assert(!audio_takeDiagnosticWindow().flac.active);
  puts("PASS: audio diagnostic window (atomic copy/reset, 64-bit totals, cadence, missing frames)");
}
