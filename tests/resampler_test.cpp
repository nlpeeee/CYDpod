#include "../firmware/cydpod_player/app/resampler.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <chrono>
#include <numeric>
struct Stereo {int16_t l,r;bool operator==(const Stereo& b)const{return l==b.l&&r==b.r;}};
static const double pi=3.14159265358979323846;
static bool collect(void* v,int16_t l,int16_t r){static_cast<std::vector<Stereo>*>(v)->push_back({l,r});return true;}
static int16_t clipped(int64_t v){v=(v+16384)>>15;return v>32767?32767:v<-32768?-32768:int16_t(v);}
static std::vector<Stereo> run(unsigned rate,const std::vector<Stereo>& in,unsigned block){
 FirState fir;fir.reset();std::vector<Stereo> out;const auto& b=*firBank(rate);
 for(size_t base=0;base<in.size();base+=block)for(size_t i=base;i<std::min(base+block,in.size());++i)
  assert(fir.push(in[i].l,in[i].r,b,collect,&out));
 assert(fir.drain(b,collect,&out));assert(out.size()==(uint64_t(in.size())*44100+rate-1)/rate);
 assert(out.empty()||(out.back().l==0&&out.back().r==0));
 const auto n=out.size();assert(fir.drain(b,collect,&out));assert(out.size()==n);
 return out;
}
// Coherent one-second Goertzel measurement, excluding startup and EOF fade.
static double amplitude(const std::vector<Stereo>& out,double frequency){
 const size_t start=11025,n=44100;assert(out.size()>start+n);
 const double a=2*pi*frequency/44100,coeff=2*cos(a);double q1=0,q2=0;
 for(size_t j=start;j<start+n;++j){double q=out[j].l+coeff*q1-q2;q2=q1;q1=q;}
 return 2*sqrt(std::max(0.,q1*q1+q2*q2-coeff*q1*q2))/n;
}
static double folded(double f){f=fmod(f,44100);if(f<0)f+=44100;return f>22050?44100-f:f;}
int main(){
 for(unsigned rate:{48000,32000,24000,22050,16000,12000,11025,8000}){
  const auto& b=*firBank(rate);FirState fir;fir.reset();
  // Independent int64 reference checks every coefficient phase and ring wrap,
  // including adversarial full-scale patterns that exceed the int16 output range.
  for(unsigned p=0;p<256;++p){const auto* c=b.coefficients+p*32;int sum=0,l1=0;
   for(unsigned k=0;k<32;++k){sum+=c[k];l1+=abs(c[k]);}assert(sum==32768);assert(int64_t(l1)*32768+16384<INT32_MAX);
   for(unsigned head:{0,1,15,31})for(int pattern=0;pattern<4;++pattern){fir.head=head;int64_t ref=0;
    for(unsigned k=0;k<32;++k){int16_t x=pattern==0?32767:pattern==1?-32768:pattern==2?(c[k]<0?-32768:32767):(c[k]<0?32767:-32768);
     unsigned pos=(head+k)&31;fir.history[pos*2]=fir.history[(pos+32)*2]=x;ref+=int64_t(x)*c[k];}
    assert(fir.filter(c,0)==clipped(ref));
   }
  }
  std::vector<Stereo> input(rate/5);
  for(size_t i=0;i<input.size();++i){int16_t l=int16_t((i*7919)%65536-32768),r=int16_t((i*3571)%65536-32768);input[i]={l,r};}
  auto expected=run(rate,input,1);
  for(unsigned block:{7,480,1024})assert(run(rate,input,block)==expected);
  for(unsigned n=0;n<35;++n){std::vector<Stereo> tiny(n,{30000,-30000});run(rate,tiny,7);}
  std::vector<Stereo> dc(4096,{32767,-32768});auto dcOut=run(rate,dc,480);
  for(size_t i=200;i+200<dcOut.size();++i)assert(dcOut[i].l==32767&&dcOut[i].r==-32768);
  for(size_t i=0;i<dc.size();++i)dc[i]=(i/64)%2?Stereo{32767,-32768}:Stereo{-32768,32767};
  auto square=run(rate,dc,1024);for(size_t i=300;i+300<square.size();++i)assert(square[i].l>0?square[i].r<=0:square[i].r>=0);
  // Reset removes all track/seek history, including phase and a pending tail.
  fir.reset();std::vector<Stereo> zeros;for(unsigned i=0;i<100;++i)assert(fir.push(0,0,b,collect,&zeros));
  for(auto v:zeros)assert(v.l==0&&v.r==0);
  for(unsigned frequency:rate==48000?std::vector<unsigned>{1000,10000,18000}:std::vector<unsigned>{1000,rate*3/8}){
   input.resize(rate*2);for(size_t i=0;i<input.size();++i){int16_t x=lround(20000*sin(2*pi*frequency*i/rate));input[i]={x,int16_t(-x)};}
   auto start=std::chrono::steady_clock::now();auto out=run(rate,input,1024);auto elapsed=std::chrono::steady_clock::now()-start;
   const double main=amplitude(out,frequency),db=20*log10(main/20000);assert(fabs(db)<.1);
   assert(amplitude(out,frequency-1)<main*.001&&amplitude(out,frequency+1)<main*.001);
   // Rational scheduling/phase-quantization spurs lie on the gcd frequency grid.
   double spur=0;for(unsigned f=std::gcd(rate,44100u);f<44100;f+=std::gcd(rate,44100u)){
    double at=folded(frequency+f);if(at>0&&fabs(at-frequency)>.5)spur=std::max(spur,amplitude(out,at));}
   const double rejection=20*log10(main/std::max(spur,1e-12));assert(rejection>35);
   printf("PASS %u -> 44100 %u Hz: gain %+.5f dB, largest image/spur %.2f dBc, host %.1f ns/output stereo frame\n",rate,frequency,db,rejection,std::chrono::duration<double,std::nano>(elapsed).count()/out.size());
  }
  printf("PASS %u: 272 B state; exact blocks 1/7/480/1024, short EOF, DC/square/adversarial saturation, reset\n",rate);
 }
 // Input above output Nyquist must alias only at the attenuated level.
 for(unsigned f:{22500,23000,23500}){
  std::vector<Stereo> in(96000);for(size_t i=0;i<in.size();++i){int16_t x=lround(20000*sin(2*pi*f*i/48000));in[i]={x,x};}
  auto out=run(48000,in,1024);double db=20*log10(amplitude(out,44100-f)/20000);assert(db<-37);
  printf("PASS 48k stopband input %u Hz alias %u Hz: %.2f dB\n",f,44100-f,db);
 }
 puts("PASS 32 taps/channel = 64 MAC/stereo output, 2822400 MAC/s; ESP32 throughput unmeasured");
}
