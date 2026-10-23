#include "../firmware/cydpod_probe/audio_core.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace cydpod;
int main() {
  PcmRing<8> ring;
  Stereo input[12], output[12]{};
  for (int i=0;i<12;++i) input[i] = {static_cast<int16_t>(i), static_cast<int16_t>(-i)};
  assert(ring.pop(output, 3)==0);
  assert(ring.push(input, 12)==8);
  assert(ring.push(input, 1)==0);
  assert(ring.pop(output, 5)==5);
  assert(ring.push(input+8, 4)==4);
  assert(ring.pop(output+5, 7)==7);
  for (int i=0;i<12;++i) assert(output[i].left==i && output[i].right==-i);
  ring.clear(); assert(ring.size()==0);
  // Stress publication ordering and wrap-around with independent endpoints.
  constexpr int total=1000000;
  std::thread writer([&] { for(int i=0;i<total;) { Stereo s{int16_t(i%32767),int16_t(-(i%32767))}; if(ring.push(&s,1)) ++i; } });
  for(int i=0;i<total;) { Stereo s; if(ring.pop(&s,1)) { assert(s.left==i%32767 && s.right==-(i%32767)); ++i; } }
  writer.join(); assert(ring.size()==0);
  uint8_t adts[]={0xff,0xf1,0x50,0x80,0x01,0x7f,0xfc}; size_t len=0;
  assert(adtsHeader(adts,6,len)==HeaderResult::need_more);
  assert(adtsHeader(adts,7,len)==HeaderResult::valid && len==11);
  adts[2]=0x10; assert(adtsHeader(adts,7,len)==HeaderResult::unsupported); // Main
  adts[2]=0x4c; assert(adtsHeader(adts,7,len)==HeaderResult::unsupported); // 48k
  adts[2]=0x50; adts[3]=0x40; assert(adtsHeader(adts,7,len)==HeaderResult::unsupported); // mono
  adts[3]=0x80; adts[4]=0; adts[5]=0; assert(adtsHeader(adts,7,len)==HeaderResult::malformed);
  GainRamp gain;
  Stereo edge{-32768,32767};
  gain.apply(&edge,1,100,false); assert(edge.left==-8 && edge.right==7);
  for(int i=0;i<4096;++i) { edge={-32768,32767}; gain.apply(&edge,1,100,false); }
  assert(edge.left==-32768 && edge.right==32767);
  gain.apply(&edge,1,100,true); assert(edge.left==0 && edge.right==0);
  edge={-32768,32767}; gain.apply(&edge,1,100,false); assert(edge.left==-8 && edge.right==7);
  gain.reset(); edge={-32768,32767}; gain.apply(&edge,1,0,false); assert(edge.left==0 && edge.right==0);
  assert(safeSavedLevel(255,10)==10 && safeSavedLevel(3,10)==3);
  LineBuffer<8> line;
  for(char c : "toolong mute\n") if(c) assert(!line.add(c));
  assert(!line.add('o')); assert(!line.add('k')); assert(line.add('\n')); assert(!std::strcmp(line.text(),"ok"));
  puts("PASS ring capacity/wrap/concurrency, ADTS rejection, gain, saved levels, command overflow");
}
