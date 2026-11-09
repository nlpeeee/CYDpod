#include "../firmware/cydpod_player/app/codec_track.h"
extern "C" {
#include <libhelix-mp3/mp3dec.h>
}
#include <cassert>
#include <vector>
extern unsigned helixHostAllocations;
int main(int argc,char** argv){
 assert(argc>1);CodecTrack track;
 for(int arg=1;arg<argc;++arg){
  FILE* raw=fopen(argv[arg],"rb");assert(raw);fseek(raw,0,SEEK_END);std::vector<unsigned char> data(ftell(raw));rewind(raw);assert(fread(data.data(),1,data.size(),raw)==data.size());fclose(raw);
  size_t at=0;if(!memcmp(data.data(),"ID3",3))at=10+(data[6]<<21|data[7]<<14|data[8]<<7|data[9])+((data[5]&16)?10:0);
  size_t end=data.size();if(end>=128&&!memcmp(data.data()+end-128,"TAG",3))end-=128;
  unsigned before=helixHostAllocations;auto stock=MP3InitDecoder();assert(stock&&helixHostAllocations==before+8);
  before=helixHostAllocations;
  File f(argv[arg],"r");assert(track.begin(f)==MINIAAC_OK);
  // Gapless reference: stock Helix over every frame after the Xing/Info tag frame (which holds no
  // audio), cut to the LAME/Lavc window [delay+529, frames*spf-padding+529).
  const bool v1=((data[at+1]>>3)&3)==3,mono=(data[at+3]>>6)==3;
  const size_t x=at+4+((data[at+1]&1)?0:2)+(v1?(mono?17:32):(mono?9:17));
  const bool tag=!memcmp(&data[x],"Xing",4)||!memcmp(&data[x],"Info",4);
  uint64_t lead=0,keep=UINT64_MAX;
  if(tag){
   const uint32_t flags=uint32_t(data[x+4])<<24|data[x+5]<<16|data[x+6]<<8|data[x+7];
   const size_t lame=x+8+((flags&1)?4:0)+((flags&2)?4:0)+((flags&4)?100:0)+((flags&8)?4:0);
   if(!memcmp(&data[lame],"LAME",4)||!memcmp(&data[lame],"Lavc",4)||!memcmp(&data[lame],"Lavf",4)){
    const uint32_t delay=uint32_t(data[lame+21])<<4|data[lame+22]>>4,padding=uint32_t(data[lame+22]&15)<<8|data[lame+23];
    const uint64_t frames=uint32_t(data[x+8])<<24|data[x+9]<<16|data[x+10]<<8|data[x+11];
    lead=delay+529;if(flags&1)keep=frames*(v1?1152:576)-delay-padding;
   }
  }
  std::vector<M4aStereo> stream;int16_t reference[2304];unsigned char* p=data.data()+at;int left=int(end-at);bool first=true;
  while(left>0){
   const int status=MP3Decode(stock,&p,&left,reference,0);
   if(status==ERR_MP3_MAINDATA_UNDERFLOW)continue;
   if(status)break; // the cut or tagged end of the stream
   MP3FrameInfo info{};MP3GetLastFrameInfo(stock,&info);assert(track.sampleRate()==unsigned(info.samprate));
   if(first&&tag){first=false;continue;}
   first=false;
   for(int i=0;i<info.outputSamps/info.nChans;++i)stream.push_back({reference[i*info.nChans],reference[i*info.nChans+info.nChans-1]});
  }
  assert(stream.size()>lead);
  const size_t want=size_t(std::min<uint64_t>(keep,stream.size()-lead));
  std::vector<M4aStereo> decoded;size_t count=0;
  for(;;){auto rc=track.next();if(rc==CodecTrack::Pull::error)fprintf(stderr,"MP3 error %s: %s\n",argv[arg],track.name());assert(rc!=CodecTrack::Pull::error);if(rc==CodecTrack::Pull::eof)break;
   while(track.pending()){decoded.push_back(*track.pcm());track.consume(1);++count;}
  }
  assert(decoded.size()==want&&!memcmp(decoded.data(),stream.data()+lead,want*sizeof(M4aStereo)));
  if(keep!=UINT64_MAX)assert(track.totalFrames()==keep);
  assert(count>0&&helixHostAllocations==before);assert(track.seekable());
  for(uint32_t ms:{100u,0u,50u,999999u,25u}) {
   const uint64_t target=std::min<uint64_t>(uint64_t(ms)*track.sampleRate()/1000,track.totalFrames()-1);
   assert(track.seek(ms,nullptr,nullptr)==MINIAAC_OK);
   const size_t frame=std::min<uint64_t>(target,decoded.size()-1);
   assert(track.seekPositionMs()==frame*1000/track.sampleRate());
   assert(track.pending()>0);
   const size_t n=std::min(track.pending(),decoded.size()-frame);
   assert(!memcmp(track.pcm(),decoded.data()+frame,n*sizeof(M4aStereo)));
  }
  assert(track.seek(100,[](void*){return 1;},nullptr)==MINIAAC_CANCELLED);
  assert(track.seek(50,nullptr,nullptr)==MINIAAC_OK);
  assert(helixHostAllocations==before);track.end();f.close();MP3FreeDecoder(stock);
  // Malformed tag and truncated final frame reject deterministically.
  auto save=[&](const std::vector<unsigned char>& bytes){FILE* out=fopen("tests/out/bad.mp3","wb");assert(out);assert(fwrite(bytes.data(),1,bytes.size(),out)==bytes.size());fclose(out);};
  // Frames decoded from a mutated copy; -1 when the track ended with an error instead of EOF.
  auto run=[&](const std::vector<unsigned char>& bytes){save(bytes);f=File("tests/out/bad.mp3","r");assert(track.begin(f)==MINIAAC_OK);long n=0;
   for(;;){auto rc=track.next();if(rc==CodecTrack::Pull::error){fprintf(stderr,"err %s\n",track.name());n=-1;break;}if(rc==CodecTrack::Pull::eof)break;++n;track.consume(track.pending());}track.end();f.close();return n;};
  // F-02: a cut last frame and any trailing non-frame bytes end the track cleanly as EOF.
  auto bad=data;bad.pop_back();
  size_t tail=data.size();if(tail>=128&&!memcmp(data.data()+tail-128,"TAG",3))tail-=128;
  auto junk=[&](const std::vector<unsigned char>& extra){std::vector<unsigned char> d(data.begin(),data.begin()+tail);d.insert(d.end(),extra.begin(),extra.end());return d;};
  const long clean=run(junk({}));assert(clean>0);
  assert(run(bad)==clean-1 || run(bad)==clean);
  std::vector<unsigned char> ape={'A','P','E','T','A','G','E','X'};ape.resize(300,'x');
  ape[100]=0xFF;ape[101]=0xFB;ape[102]=0x90;ape[103]=0x00; // a false sync inside the tag must not decode
  {long r=run(junk(ape));fprintf(stderr,"ape %ld clean %ld\n",r,clean);assert(r==clean);}
  assert(run(junk(std::vector<unsigned char>(5000,0)))==clean);
  assert(run(junk(std::vector<unsigned char>(3,0xFF)))==clean);
  { std::vector<unsigned char> id3={'I','D','3',4,0,0,0,0,0,20};id3.resize(30,'y'); assert(run(junk(id3))==clean); }
  // F-03: damaged frame bodies are skipped (silent frame) instead of failing the track; a fully wrecked file still ends.
  { auto d=junk({});size_t fb=4;while(fb<2048&&at+fb+3<=d.size()&&memcmp(&d[at+fb],&d[at],3))++fb; // CBR: next frame carries the same header bytes
    if(fb<2048&&at+3*fb<=d.size()) {
      for(size_t i=4;i<fb;++i)d[at+2*fb+i]=(i%3)?0xFF:0x55; // headers intact, frame body wrecked
      // wrecked side info may read as reservoir underflow, which drops the frame
      const long r=run(d); assert(r==clean||r==clean-1);
    } }
  { auto d=junk({});const size_t mid=at+(tail-at)/2;for(size_t i=0;i<400&&mid+i<d.size();++i)d[mid+i]=(i%3)?0xFF:0x55;
    const long n=run(d);assert(n>0&&n<=clean+1); }
  { auto d=junk({});for(size_t i=at+4;i<d.size();++i)if(i%2)d[i]=0xFF;else d[i]=0x00;const long n=run(d);assert(n>=-1); }
  bad={'I','D','3',4,0,0,128,0,0,0};save(bad);f=File("tests/out/bad.mp3","r");assert(track.begin(f)!=MINIAAC_OK);assert(!strcmp(track.name(),"mp3_id3"));track.end();f.close();
  printf("PASS MP3 bit-exact stock Helix (gapless: tag frame skipped, lead %llu, trimmed length), eight allocations replaced, zero pool allocations: %s frames=%zu\n",(unsigned long long)lead,argv[arg],count);
 }
}
