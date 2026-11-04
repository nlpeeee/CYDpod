#include "../firmware/cydpod_i2s_diag/i2s_diag.h"
#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace i2sdiag;
static std::vector<int16_t> render(unsigned n, size_t frames, size_t chunk) {
  Generator gen(phase(n));
  std::vector<int16_t> out(frames*2);
  for (size_t i=0; i<frames; i+=std::min(chunk,frames-i))
    gen.fill(out.data()+2*i, std::min(chunk,frames-i));
  return out;
}
int main() {
  // Every boundary and all status times over multiple cycles, plus large uptime.
  for (uint64_t cycle=0; cycle<3; ++cycle) for (unsigned n=0; n<7; ++n) {
    uint64_t start=(cycle*7+n)*kPeriodMs;
    for (uint32_t ms : {0u,3999u,4000u,7999u,8000u,11999u,12000u,12500u,12549u}) {
      auto p=position(start+ms);
      assert(p.phase==n && p.ms==ms && p.zeros==(ms>=12000));
    }
    assert(position(start+kPeriodMs).phase==(n+1)%7);
  }
  const uint64_t far=(uint64_t(1)<<40)*7*kPeriodMs;
  assert(position(far).phase==0 && position(far).ms==0);
  assert(ledPin(0)==4 && ledPin(999)==4 && ledPin(1000)==16);
  assert(ledPin(1999)==16 && ledPin(2000)==17 && ledPin(2999)==17 && ledPin(3000)==-1);
  // Continuous tones: no gate, frequency per ear, amplitude and chunk invariance.
  for (unsigned n=0; n<7; ++n) {
    const auto p=phase(n);
    auto s=render(n,p.rate,256);
    assert(s==render(n,p.rate,7));
    if (!p.audio) {
      assert(std::all_of(s.begin(),s.end(),[](int16_t v){return v==0;}));
      continue;
    }
    assert(p.slotBits==(n==2?32u:16u));
    assert(p.bitShift==(n!=4));
    if (p.square) {
      int transitions=0;
      for (uint32_t i=0;i<p.rate;++i) {
        const int16_t expected=(uint64_t(i)*8/p.rate)%2 ? INT16_MIN : INT16_MAX;
        assert(s[2*i]==expected && s[2*i+1]==expected);
        if (i && s[2*i]!=s[2*(i-1)]) ++transitions;
      }
      assert(transitions==7); // eighth edge is the one-second wrap: four cycles
    } else {
      for (unsigned ch=0;ch<2;++ch) {
        int crossings=0, peak=0;
        for (uint32_t i=0;i<p.rate;++i) {
          const int v=s[2*i+ch];
          peak=std::max(peak,abs(v));
          assert(v>=-kAmplitude && v<=kAmplitude);
          if (i && s[2*(i-1)+ch]<0 && v>=0) ++crossings;
          // Independent floating-point oracle checks frequency, channel and phase.
          double ref=kAmplitude*sin(6.283185307179586*(ch?880:440)*i/p.rate);
          assert(fabs(v-ref)<3);
        }
        assert(abs(crossings-(ch?880:440))<=1);
        assert(peak>=kAmplitude-1 && fabs(20*log10(double(peak)/32767)+12)<0.01);
        printf("phase %u ch%u crossings=%d peak=%d\n",n,ch,crossings,peak);
      }
    }
    Generator gen(p); int16_t quiet[512];
    gen.fill(quiet,256,true);
    for (int v : quiet) assert(v==0);
    // Sustained generation exercises accumulator wrapping under sanitizers.
    for (unsigned i=0;i<1000;++i) gen.fill(quiet,256);
  }
  // Known wire vectors relative to WS, including sign bit and previous LSB.
  for (int16_t word : {int16_t(0x1234),int16_t(-32768),int16_t(-1),int16_t(32767)}) {
    for (bool shift : {false,true}) {
      uint16_t recovered=0;
      for (unsigned bit=0;bit<16;++bit)
        recovered=uint16_t((recovered<<1)|wireBit(word,1,bit+unsigned(shift),shift));
      assert(recovered==uint16_t(word));
    }
  }
  assert(wireBit(0x1234,1,0,true)==1 && wireBit(0x1234,1,0,false)==0);
  assert(wireBit(INT16_MIN,0,0,false)==1 && wireBit(INT16_MIN,0,1,true)==1);
  puts("I2S_DIAG_OK scheduler, tones, square, quiet, overflow and format model");
}
