#include "../firmware/cydpod_player/app/codec_track.h"
#include <cassert>
#include <vector>
#include <cmath>
static void put16(std::vector<uint8_t>& b,unsigned n){b.push_back(n);b.push_back(n>>8);}
static void put32(std::vector<uint8_t>& b,unsigned n){put16(b,n);put16(b,n>>16);}
static void tag(std::vector<uint8_t>& b,const char* s){b.insert(b.end(),s,s+4);}
static void save(const char* name,const std::vector<uint8_t>& b){FILE* f=fopen(name,"wb");assert(f);assert(fwrite(b.data(),1,b.size(),f)==b.size());fclose(f);}
static std::vector<uint8_t> wav(unsigned bits,unsigned ch){
 std::vector<uint8_t> b;tag(b,"RIFF");put32(b,0);tag(b,"WAVE");tag(b,"JUNK");put32(b,3);b.insert(b.end(),{1,2,3,0});
 tag(b,"fmt ");put32(b,16);put16(b,1);put16(b,ch);put32(b,44100);put32(b,44100*ch*bits/8);put16(b,ch*bits/8);put16(b,bits);
 tag(b,"data");put32(b,10240*ch*bits/8);
 for(int i=0;i<10240;++i)for(unsigned c=0;c<ch;++c){int v=(i%256-128)*256;if(c)v=-v/2;
  if(bits==8)b.push_back((v/256)+128);else{uint32_t u=uint32_t(uint16_t(v))<<(bits-16);for(unsigned k=0;k<bits/8;++k)b.push_back(u>>(8*k));}}
 unsigned n=b.size()-8;for(int k=0;k<4;++k)b[4+k]=n>>(8*k);return b;
}
int main(int argc,char** argv){
 CodecTrack track;
 for(unsigned bits:{8,16,24,32})for(unsigned ch:{1,2}){
  auto b=wav(bits,ch);save("tests/out/pool.wav",b);File f("tests/out/pool.wav","r");assert(track.begin(f)==MINIAAC_OK);assert(track.totalFrames()==10240&&track.sampleRate()==44100);
  int at=0;while(true){auto rc=track.next();assert(rc!=CodecTrack::Pull::error);if(rc==CodecTrack::Pull::eof)break;
   while(track.pending()){auto p=*track.pcm();int v=(at%256-128)*256;int right=ch==1?v:-v/2;if(bits==8)right=(right/256)*256;
    assert(p.l==v&&p.r==right);track.consume(1);++at;}}
  assert(at==10240);assert(track.seekable());
  for(uint32_t ms:{100u,0u,232u,50u,99999u}) {
   assert(track.seek(ms,nullptr,nullptr)==MINIAAC_OK);
   unsigned frame=std::min<uint64_t>(uint64_t(ms)*44100/1000,10239);
   assert(track.seekPositionMs()==frame*1000/44100);
   assert(track.next()==CodecTrack::Pull::frames);
   int v=(frame%256-128)*256,right=ch==1?v:-v/2;
   if(bits==8)right=(right/256)*256;
   assert(track.pcm()->l==v&&track.pcm()->r==right);
  }
  assert(track.seek(0,[](void*){return 1;},nullptr)==MINIAAC_CANCELLED);
  track.end();f.close();
  // F-07: a cut last frame is dropped; unsized/oversized RIFF and data sizes mean "to the end of the file".
  auto open=[&](const std::vector<uint8_t>& v,uint64_t frames){save("tests/out/pool.wav",v);File g("tests/out/pool.wav","r");assert(track.begin(g)==MINIAAC_OK);if(track.totalFrames()!=frames)fprintf(stderr,"wav total %llu want %llu bits=%u ch=%u\n",(unsigned long long)track.totalFrames(),(unsigned long long)frames,bits,ch);assert(track.totalFrames()==frames);
   uint64_t got=0;for(;;){auto rc=track.next();assert(rc!=CodecTrack::Pull::error);if(rc==CodecTrack::Pull::eof)break;got+=track.pending();track.consume(track.pending());}assert(got==frames);track.end();g.close();};
  auto set32=[](std::vector<uint8_t>& v,size_t at,uint32_t n){for(int k=0;k<4;++k)v[at+k]=n>>(8*k);};
  { auto c=b;c.pop_back();open(c,10239); }
  { auto c=b;set32(c,4,0);open(c,10240); }
  { auto c=b;set32(c,4,0xFFFFFFFFu);open(c,10240); }
  { auto c=b;set32(c,4,0);set32(c,52,0);open(c,10240); }
  { auto c=b;set32(c,52,0xFFFFFFFFu);open(c,10240); }
  { auto c=b;set32(c,52,0x7FFFFFF0u);open(c,10240); }
  { auto c=b;c.insert(c.end(),{1,2,3});set32(c,4,uint32_t(c.size()-8));open(c,10240); } // 3 stray bytes inside RIFF after the data
  { auto c=b;c.insert(c.end(),{1,2,3,4,5,6,7});set32(c,4,uint32_t(c.size()-8));set32(c,52,uint32_t(c.size()-56));open(c,10240+(7/(ch*bits/8))); }
 }
 // PCM capacity bound: largest mono8 batches never exceed 1152 frames.
 // Reject incomplete frame, non-PCM fmt, missing odd-chunk padding, bad align.
 for(int field:{32,36,44,46}){auto bad=wav(16,2);bad[field]^=1;save("tests/out/pool.wav",bad);File f("tests/out/pool.wav","r");assert(track.begin(f)!=MINIAAC_OK);track.end();f.close();}
 auto b=wav(16,2);b[44]=3;save("tests/out/pool.wav",b);File f("tests/out/pool.wav","r");assert(track.begin(f)!=MINIAAC_OK);track.end();f.close();
 for(int arg=1;arg<argc;++arg){File f(argv[arg],"r");
  hostReadBudget=1000000000;assert(track.begin(f)==MINIAAC_OK);
  const long openBytes=1000000000-hostReadBudget;hostReadBudget=-1;
  assert(track.sampleRate()==48000);size_t count=0;std::vector<M4aStereo> output;
  auto sink=[](void* v,int16_t l,int16_t r){static_cast<std::vector<M4aStereo>*>(v)->push_back({l,r});return true;};
  while(true){auto rc=track.next();assert(rc!=CodecTrack::Pull::error);if(rc==CodecTrack::Pull::eof)break;
   while(track.pending()){auto p=*track.pcm();assert(track.output(p.l,p.r,sink,&output));track.consume(1);++count;assert(count<48000*120);}}
  assert(count>0);assert(track.drain(sink,&output));
  assert(output.size()==((count+127)*147+159)/160);
  // Synthetic tones have a known frequency and level independent of the FIR.
  if(!strstr(argv[arg],"Sample_BeeMoved")) {
   const bool wav=strstr(argv[arg],".wav");
   for(unsigned ch=0;ch<2;++ch){const double freq=wav&&ch?1499:997,level=wav?(ch?6000:12000):(strstr(argv[arg],"48000-1")?4095:4095/sqrt(2.));
    double re=0,im=0,cc=0,ss=0,cs=0;size_t n=0;
    for(size_t j=4096;j+2048<output.size();++j){double x=ch?output[j].r:output[j].l,a=2*3.141592653589793*freq*j/44100;
     re+=x*cos(a);im+=x*sin(a);cc+=cos(a)*cos(a);ss+=sin(a)*sin(a);cs+=sin(a)*cos(a);++n;}
    double det=cc*ss-cs*cs,real=(re*ss-im*cs)/det,imag=(im*cc-re*cs)/det;
    double amplitude=sqrt(real*real+imag*imag),error=0,signal=0;
    for(size_t j=4096;j+2048<output.size();++j){double x=ch?output[j].r:output[j].l,a=2*3.141592653589793*freq*j/44100;
     double expected=real*cos(a)+imag*sin(a);error+=(x-expected)*(x-expected);signal+=expected*expected;}
    double snr=10*log10(signal/std::max(1.,error));
    printf("tone ch%u %.0f Hz level %.1f expected %.0f SNR %.1f dB\n",ch,freq,amplitude,level,snr);
    assert(fabs(amplitude-level)/level<.10&&snr>25);
   }
  }
  printf("PASS production decode+48k FIR: %s input=%zu output=%zu ratio=147/160 open-read=%ld B\n",argv[arg],count,output.size(),openBytes);
  track.end();f.close();}
 puts("PASS AAC 48 kHz mono/stereo pool decode");
 puts("PASS WAV pool: 8/16/24/32-bit mono/stereo exact PCM; unknown odd chunks, padding, truncated and invalid formats; repeated reset");
}
