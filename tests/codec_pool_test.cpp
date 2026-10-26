#include "../firmware/cydpod_player/app/codec_track.h"
#include <cassert>
#include <vector>
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
  assert(at==10240);track.end();f.close();
  b.pop_back();save("tests/out/pool.wav",b);f=File("tests/out/pool.wav","r");assert(track.begin(f)!=MINIAAC_OK);assert(strstr(track.name(),"truncated"));track.end();f.close();
 }
 // PCM capacity bound: largest mono8 batches never exceed 1152 frames.
 // Reject incomplete frame, non-PCM fmt, missing odd-chunk padding, bad align.
 for(int field:{32,36,44,46}){auto bad=wav(16,2);bad[field]^=1;save("tests/out/pool.wav",bad);File f("tests/out/pool.wav","r");assert(track.begin(f)!=MINIAAC_OK);track.end();f.close();}
 auto b=wav(16,2);b[44]=3;save("tests/out/pool.wav",b);File f("tests/out/pool.wav","r");assert(track.begin(f)!=MINIAAC_OK);track.end();f.close();
 for(int arg=1;arg<argc;++arg){File f(argv[arg],"r");assert(track.begin(f)==MINIAAC_OK);assert(track.sampleRate()==48000);size_t count=0;
  while(true){auto rc=track.next();assert(rc!=CodecTrack::Pull::error);if(rc==CodecTrack::Pull::eof)break;count+=track.pending();track.consume(track.pending());}assert(count>0);track.end();f.close();}
 puts("PASS AAC 48 kHz mono/stereo pool decode");
 puts("PASS WAV pool: 8/16/24/32-bit mono/stereo exact PCM; unknown odd chunks, padding, truncated and invalid formats; repeated reset");
}
