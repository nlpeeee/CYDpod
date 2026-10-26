#ifndef CYD_UPLOADER
#include "resampler.h"
#include "fir_coefficients.h"
const FirBank* firBank(uint32_t rate) {
  static const FirBank banks[]={
    {48000,147,160,&fir48000[0][0]}, {32000,441,320,&fir32000[0][0]},
    {24000,147,80,&fir24000[0][0]}, {22050,2,1,&fir22050[0][0]},
    {16000,441,160,&fir16000[0][0]}, {12000,147,40,&fir12000[0][0]},
    {11025,4,1,&fir11025[0][0]}, {8000,441,80,&fir8000[0][0]}
  };
  for(const auto& b:banks)if(b.rate==rate)return &b;
  return nullptr;
}
// Split at history wrap outside the MAC loop. Each half has a proven int32
// bound in the generated header; combine halves in int64 only once/output.
int16_t FirState::filter(const int16_t* c,unsigned channel) const {
  int32_t halves[2]={0,0};
  for(unsigned half=0;half<2;++half) {
    unsigned pos=(head+half*64)&127,left=64,k=half*64;
    while(left) {
      const unsigned n=left<128-pos?left:128-pos;
      const int16_t* x=&history[pos*2+channel];const int16_t* end=c+k+n;
      const int16_t* coeff=c+k;
      for(unsigned i=0;coeff!=end;++i)halves[half]+=int32_t(x[i*2])*(*coeff++);
      left-=n;k+=n;pos=0;
    }
  }
  const int64_t sum=(int64_t(halves[0])+halves[1]+16384)>>15;
  return sum>32767?32767:sum<-32768?-32768:int16_t(sum);
}
bool FirState::push(int16_t l,int16_t r,const FirBank& b,PcmSink sink,void* user) {
  history[head*2]=l;history[head*2+1]=r;head=(head+1)&127;
  if(wait)--wait;
  while(!wait) {
    const int16_t* c=b.coefficients+phase*128;
    if(!sink(user,filter(c,0),filter(c,1)))return false;
    const uint32_t next=phase+b.step;wait=next/b.phases;phase=next%b.phases;
  }
  return true;
}
#endif
