// CY-17: exact-sized Helix output with trailing canaries, including real guarded-reader frames.
#include <Arduino.h>
#include "../firmware/cydpod_probe/adts_decoder.h"
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include "m4a_file_source.h"
#include <cassert>
#include <filesystem>
#include <vector>
using namespace cydpod;
extern "C" void* helix_malloc(int n) { return std::malloc(n); }
extern "C" void helix_free(void* p) { std::free(p); }
struct Output { short pcm[AdtsDecoder::kPcmShorts]; uint32_t canary[16]; };
static size_t frames=0;
static int decode(HAACDecoder d, const uint8_t* p, size_t n) {
  size_t length=0; assert(adtsHeader(p,n,length)==HeaderResult::valid && length==n);
  Output out{};
  std::fill(std::begin(out.canary),std::end(out.canary),0xabcddcba);
  auto* cursor=const_cast<uint8_t*>(p); int left=n;
  int error=AACDecode(d,&cursor,&left,out.pcm);
  for(auto word:out.canary) assert(word==0xabcddcba);
  if(!error) { AACFrameInfo info{}; AACGetLastFrameInfo(d,&info); assert(info.outputSamps==2048); ++frames; }
  return error;
}
static std::vector<uint8_t> load(const char* path) {
  FILE* f=std::fopen(path,"rb"); assert(f); std::vector<uint8_t> v;
  uint8_t b[4096]; size_t n; while((n=std::fread(b,1,sizeof b,f))) v.insert(v.end(),b,b+n);
  assert(!std::ferror(f)); std::fclose(f); return v;
}
struct Bits {
  std::vector<uint8_t> v; unsigned count=0;
  void put(unsigned value,unsigned n) { for(unsigned i=n;i;--i) {
    if(count%8==0) v.push_back(0);
    v.back()|=((value>>(i-1))&1)<<(7-count%8); ++count;
  } }
  void channel() { put(100,8); put(0,1); put(0,2); put(0,1); put(0,6); put(0,1); put(0,3); }
  void pair() { put(1,3); put(0,4); put(0,1); channel(); channel(); }
  void single() { put(0,3); put(0,4); channel(); }
  void pce() { put(5,3); put(0,4); put(1,2); put(4,4); put(3,4); put(0,4); put(0,4);
    put(0,2); put(0,3); put(0,4); put(0,3);
    for(int i=0;i<3;++i) { put(1,1); put(i,4); }
    while(count%8) put(0,1); put(0,8);
  }
  std::vector<uint8_t> frame() { put(7,3); while(count%8) put(0,1);
    size_t n=v.size()+7; std::vector<uint8_t> h={0xff,0xf1,0x50,uint8_t(0x80|(n>>11)),uint8_t(n>>3),uint8_t((n<<5)|31),0xfc};
    h.insert(h.end(),v.begin(),v.end()); return h;
  }
};
int main(int argc,char** argv) {
  assert(argc==2);
  for(const char* name:{"128","192","256"}) {
    auto v=load((std::string("fixtures/stereo-")+name+".aac").c_str());
    HAACDecoder d=AACInitDecoder(); assert(d);
    for(size_t pos=0;pos<v.size();) { size_t n=0; assert(adtsHeader(v.data()+pos,v.size()-pos,n)==HeaderResult::valid);
      assert(n<=v.size()-pos && decode(d,v.data()+pos,n)==0); pos+=n; }
    AACFreeDecoder(d);
  }
  // Header pre-check must consume no payload and leave PCM untouched.
  auto original=load("fixtures/stereo-192.aac");
  for(unsigned channels:{0u,1u,3u,6u,7u,8u,9u,10u}) {
    auto v=original; v[2]=(v[2]&~1u)|(channels>>2); v[3]=(v[3]&63)|(channels<<6);
    if(channels>=8) { v=original;
      if(channels==8) v[2]&=63; // Main
      if(channels==9) v[2]=(v[2]&0xc3)|12; // 48 kHz
      if(channels==10) v[6]|=1; // two raw blocks
    }
    AdtsDecoder d; assert(d.begin()); size_t pos=0;
    auto r=d.next([&](uint8_t* p,size_t n) { n=std::min(n,v.size()-pos); std::memcpy(p,v.data()+pos,n); pos+=n; return n; });
    assert(r==DecodeResult::unsupported && pos==7 && d.lastError==0);
    for(size_t i=0;i<1024;++i) assert(d.pcm()[i].left==0 && d.pcm()[i].right==0);
  }
  for(int test=0;test<3;++test) {
    Bits b;
    if(test==0) { b.pair(); b.pair(); } // stereo header, four audio channels
    if(test==1) { b.single(); b.single(); b.single(); }
    if(test==2) { b.pce(); b.pair(); } // PCE declares six channels; ADTS remains stereo
    auto v=b.frame(); HAACDecoder d=AACInitDecoder(); assert(d);
    int e=decode(d,v.data(),v.size()); assert(test==2 ? e==0 : e==ERR_AAC_NCHANS_TOO_HIGH); AACFreeDecoder(d);
  }
  size_t files=0;
  std::vector<std::filesystem::path> paths={"fixtures/stereo-faststart.m4a"};
  for(const auto& entry:std::filesystem::directory_iterator(argv[1])) {
    if(entry.path().extension()!=".m4a") continue;
    paths.push_back(entry.path());
  }
  assert(paths.size()==26);
  for(const auto& path:paths) {
    FILE* f=std::fopen(path.c_str(),"rb"); assert(f);
    FileSource source(f); M4aGuardedReader reader; HAACDecoder d=AACInitDecoder(); assert(d);
    size_t before=frames;
    reader.begin([&](const uint8_t* p,size_t n) { assert(decode(d,p,n)==0); },&source);
    uint8_t buf[1024]; size_t n; M4aStatus st=M4aStatus::ok;
    while((n=std::fread(buf,1,sizeof buf,f))) { assert(st==M4aStatus::ok); st=reader.write(buf,n); }
    assert(!std::ferror(f) && st==M4aStatus::ok && reader.finish()==M4aStatus::ok && frames>before);
    AACFreeDecoder(d); std::fclose(f); ++files;
  }
  assert(files==26);
  std::fprintf(stderr,"PASS PCM canaries: %zu M4A files (25 real + 1 accepted fixture), %zu decoded frames; header and element mismatch checks\n",files,frames);
}
