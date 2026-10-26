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
  size_t count=0;int16_t reference[2304];unsigned char* p=data.data()+at;int left=end-at;
  for(;;){auto rc=track.next();if(rc==CodecTrack::Pull::error)fprintf(stderr,"MP3 error %s: %s\n",argv[arg],track.name());assert(rc!=CodecTrack::Pull::error);if(rc==CodecTrack::Pull::eof)break;
   int status;do{assert(left>0);status=MP3Decode(stock,&p,&left,reference,0);}while(status==ERR_MP3_MAINDATA_UNDERFLOW);assert(status==0);
   MP3FrameInfo info{};MP3GetLastFrameInfo(stock,&info);assert(track.sampleRate()==unsigned(info.samprate));assert(track.pending()==size_t(info.outputSamps/info.nChans));
   for(int i=0;i<info.outputSamps/info.nChans;++i){auto pcm=*track.pcm();assert(pcm.l==reference[i*info.nChans]&&pcm.r==reference[i*info.nChans+info.nChans-1]);track.consume(1);++count;}
  }
  assert(count>0&&helixHostAllocations==before);track.end();f.close();MP3FreeDecoder(stock);
  printf("PASS MP3 bit-exact stock Helix, eight allocations replaced, zero pool allocations: %s frames=%zu\n",argv[arg],count);
 }
}
