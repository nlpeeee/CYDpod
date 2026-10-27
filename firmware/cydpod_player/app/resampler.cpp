#ifndef CYD_UPLOADER
#include "resampler.h"
#include "fir_coefficients.h"
const FirBank* firBank(uint32_t rate) {
  static const FirBank banks[]={
    {48000,44100,48000,&firPolyphase[0][0]}, {32000,44100,32000,&firPolyphase[0][0]},
    {24000,44100,24000,&firPolyphase[0][0]}, {22050,44100,22050,&firPolyphase[0][0]},
    {16000,44100,16000,&firPolyphase[0][0]}, {12000,44100,12000,&firPolyphase[0][0]},
    {11025,44100,11025,&firPolyphase[0][0]}, {8000,44100,8000,&firPolyphase[0][0]}
  };
  for(const auto& b:banks)if(b.rate==rate)return &b;
  return nullptr;
}
int16_t FirState::filter(const int16_t* c,unsigned channel) const {
  const int16_t* x=&history[head*2+channel];
  int32_t sum=0;
  // Generator proves sum(abs(c))*32768 + 16384 < INT32_MAX for every phase.
  for(unsigned k=0;k<32;++k)sum+=int32_t(x[k*2])*c[k];
  int32_t v=(sum+16384)>>15;
  v^=(v^32767)&-int32_t(v>32767);
  v^=(v^-32768)&-int32_t(v<-32768);
  return int16_t(v);
}
bool FirState::push(int16_t l,int16_t r,const FirBank& b,PcmSink sink,void* user) {
  history[head*2]=history[(head+32)*2]=l;
  history[head*2+1]=history[(head+32)*2+1]=r;
  head=(head+1)&31;
  if(wait)--wait;
  while(!wait) {
    const unsigned p=phase*256/44100;
    const int16_t* c=b.coefficients+p*32;
    const uint32_t next=phase+b.step;
    int16_t left=filter(c,0),right=filter(c,1);
    // EOF: fade the last 16 source frames inside the original duration to zero.
    // Normal samples and 44.1k bypass never pass through this multiplier.
    if(tail){const int32_t gain=next/b.phases>=tail?0:int32_t(tail-1);
      left=int32_t(left)*gain/16;right=int32_t(right)*gain/16;}
    if(!sink(user,left,right))return false;
    wait=next/b.phases;phase=next%b.phases;
  }
  return true;
}
bool FirState::drain(const FirBank& b,PcmSink sink,void* user) {
  // Discard 16-frame startup delay, supply the matching lookahead at EOF.
  // Thus including drain the output count is ceil(input*44100/rate).
  for(tail=16;tail;--tail)if(!push(0,0,b,sink,user))return false;
  reset();return true;
}
#endif
