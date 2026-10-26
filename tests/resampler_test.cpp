#include "../firmware/cydpod_player/app/resampler.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
static const double pi=3.14159265358979323846;
static bool collect(void* v,int16_t l,int16_t r){assert(l==r);static_cast<std::vector<int16_t>*>(v)->push_back(l);return true;}
static int16_t clipped(int64_t v){v=(v+16384)>>15;return v>32767?32767:v<-32768?-32768:int16_t(v);}
int main(){
 for(unsigned rate:{48000,32000,24000,22050,16000,12000,11025,8000}){
  const auto& b=*firBank(rate);FirState fir;fir.reset();
  const double cutoff=rate==48000?20800./rate:.47;
  std::vector<double> coeff(b.phases*128);
  for(unsigned p=0;p<b.phases;++p){double sum=0;int qsum=0;
   for(int k=0;k<128;++k){double x=k-63.5-double(p)/b.phases;double w=.42-.5*cos(2*pi*k/127)+.08*cos(4*pi*k/127);
    coeff[p*128+k]=(x==0?2*cutoff:sin(2*pi*cutoff*x)/(pi*x))*w;sum+=coeff[p*128+k];qsum+=b.coefficients[p*128+k];}
   assert(qsum==32768);for(int k=0;k<128;++k)coeff[p*128+k]/=sum;
   for(unsigned head:{0,1,63,64,127})for(int pattern=0;pattern<4;++pattern){
    fir.head=head;int64_t reference=0;
    for(unsigned k=0;k<128;++k){int c=b.coefficients[p*128+k];int16_t x=pattern==0?32767:pattern==1?-32768:pattern==2?(c<0?-32768:32767):(c<0?32767:-32768);
     fir.history[((head+k)&127)*2]=x;reference+=int64_t(x)*c;}
    assert(fir.filter(b.coefficients+p*128,0)==clipped(reference));
   }
  }
  fir.reset();std::vector<int16_t> input(rate*2),output;
  for(size_t i=0;i<input.size();++i){double t=double(i)/rate;double high=rate==48000?20000:rate*.42;input[i]=lround(22000*sin(2*pi*(20*t+(high-20)*t*t/4)));}
  for(auto x:input)assert(fir.push(x,x,b,collect,&output));
  const size_t wanted=(uint64_t(input.size())*b.phases+b.step-1)/b.step;assert(output.size()==wanted);
  double power=0,error=0;
  for(size_t j=200;j+200<output.size();++j){uint64_t n=j*b.step/b.phases;unsigned p=j*b.step%b.phases;double ref=0;
   for(int k=0;k<128;++k){int64_t i=int64_t(n)+k-127;if(i>=0&&i<int64_t(input.size()))ref+=input[i]*coeff[p*128+k];}
   power+=ref*ref;double d=output[j]-ref;error+=d*d;}
  double snr=10*log10(power/error);assert(snr>79.5);
  printf("PASS FIR %u -> 44100: sweep SNR %.3f dB, frames=%zu, full-scale DC/adversarial overflow and saturation all phases/wraps, history=%zu B\n",rate,snr,output.size(),sizeof(FirState));
  // Streaming phase is independent of decoder block boundaries; reset erases tails.
  fir.reset();std::vector<int16_t> again;for(size_t base=0;base<input.size();base+=257)for(size_t i=base;i<std::min(base+257,input.size());++i)assert(fir.push(input[i],input[i],b,collect,&again));assert(output==again);
  fir.reset();again.clear();for(int i=0;i<256;++i)assert(fir.push(0,0,b,collect,&again));for(auto v:again)assert(v==0);
 }
}
