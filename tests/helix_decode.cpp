#include "../firmware/cydpod_probe/adts_decoder.h"
#include <cstdio>
#include <cstdlib>
using namespace cydpod;
struct alignas(std::max_align_t) Allocation { size_t size; };
static size_t liveBytes=0, peakBytes=0;
static unsigned attempts=0, failAt=0;
extern "C" void* helix_malloc(int size) {
  if(++attempts==failAt) return nullptr;
  Allocation* p=static_cast<Allocation*>(std::malloc(sizeof(Allocation)+size));
  if(!p) return nullptr;
  p->size=size; liveBytes+=size; peakBytes=std::max(peakBytes,liveBytes); return p+1;
}
extern "C" void helix_free(void* ptr) {
  if(!ptr) return;
  Allocation* p=static_cast<Allocation*>(ptr)-1; liveBytes-=p->size; std::free(p);
}
int main(int argc,char** argv) {
  if(argc==2 && !std::strcmp(argv[1],"--allocation-test")) {
    for(unsigned failure=1;failure<=2;++failure) {
      attempts=0; failAt=failure; AdtsDecoder d;
      if(d.begin() || liveBytes) return 7;
    }
    failAt=0;
    for(unsigned i=0;i<100;++i) { AdtsDecoder d; if(!d.begin()) return 8; d.end(); if(liveBytes) return 9; }
    std::printf("PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=%zu live=%zu\n",peakBytes,liveBytes);
    return 0;
  }
  if(argc!=3) { std::fprintf(stderr,"usage: helix_decode input.aac output.s16le\n"); return 2; }
  FILE* input=std::fopen(argv[1],"rb"); FILE* output=std::fopen(argv[2],"wb");
  if(!input || !output) return 2;
  AdtsDecoder decoder;
  if(!decoder.begin()) return 3;
  unsigned frames=0;
  for(;;) {
    DecodeResult r=decoder.next([&](uint8_t* p,size_t n) { return std::fread(p,1,n,input); });
    if(r==DecodeResult::eof) break;
    if(r!=DecodeResult::frame) { std::fprintf(stderr,"FAIL %s helix=%d after %u frames\n",resultName(r),decoder.lastError,frames); return 4; }
    if(std::fwrite(decoder.pcm(),sizeof(Stereo),1024,output)!=1024) return 5;
    ++frames;
  }
  std::fclose(input); std::fclose(output);
  if(!frames) return 6;
  std::printf("PASS Helix AAC-LC 44100 stereo frames=%u samples_per_channel=%u peak_decoder_heap=%zu\n",frames,frames*1024,peakBytes);
}
