#pragma once
#include "wav_track.h"
extern "C" {
#include <libhelix-mp3/coder.h>
}
// Replace the eight buffers.c allocations with typed, aligned pool members.
// CY-51a table: 2032+56+328+284+4624+840+6944+8708 = 23816 B on ESP32.
struct Mp3Core {
  MP3DecInfo decoder;
  FrameHeader header;
  SideInfo side;
  ScaleFactorInfo scale;
  HuffmanInfo huffman;
  DequantInfo dequant;
  IMDCTInfo imdct;
  SubbandInfo subband;
  void init() {
    memset(this,0,sizeof(*this));
    decoder.FrameHeaderPS=&header; decoder.SideInfoPS=&side;
    decoder.ScaleFactorInfoPS=&scale; decoder.HuffmanInfoPS=&huffman;
    decoder.DequantInfoPS=&dequant; decoder.IMDCTInfoPS=&imdct; decoder.SubbandInfoPS=&subband;
  }
};
#if defined(ESP32)
static_assert(sizeof(Mp3Core)==23816,"pinned Helix eight-slice layout changed");
#endif
struct Mp3State {
  Mp3Core core;
  uint32_t at, end, rate, frameBytes, samples;
  uint64_t total;
  unsigned channels;
  bool first;
  static uint32_t be32(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
  bool header(const uint8_t* p) {
    const unsigned version=(p[1]>>3)&3, br=p[2]>>4, sr=(p[2]>>2)&3;
    if(p[0]!=255 || (p[1]&224)!=224 || version==1 || ((p[1]>>1)&3)!=1 || !br || br==15 || sr==3) return false;
    static const uint16_t br1[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    static const uint16_t br2[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    static const uint16_t rates[]={44100,48000,32000};
    rate=rates[sr]>>(version==3?0:version==2?1:2);
    samples=version==3?1152:576; channels=(p[3]>>6)==3?1:2;
    frameBytes=(version==3?144000:72000)*(version==3?br1[br]:br2[br])/rate+((p[2]>>1)&1);
    return frameBytes>=24 && frameBytes<=2048;
  }
  const char* open(File& f) {
    core.init(); at=0; end=f.size(); first=true; uint8_t h[10];
    if(end<4 || !WavState::readAt(f,0,h,end<10?4:10))return "mp3_truncated";
    if(!memcmp(h,"ID3",3)) {
      if(end<10 || h[3]<2 || h[3]>4 || h[4]==255 || (h[5] & (h[3]==2?63:h[3]==3?31:15)) ||
         ((h[6]|h[7]|h[8]|h[9])&128)) return "mp3_id3";
      at=10+(uint32_t(h[6])<<21|uint32_t(h[7])<<14|uint32_t(h[8])<<7|h[9]);
      if(h[3]==4 && (h[5]&16))at+=10;
      if(at>end || end-at<4)return "mp3_id3_truncated";
    }
    if(end>=128 && WavState::readAt(f,end-128,h,3) && !memcmp(h,"TAG",3))end-=128;
    if(at>end || end-at<4 || !WavState::readAt(f,at,h,4) || !header(h))return "mp3_header";
    total=uint64_t(end-at)*samples/frameBytes; return nullptr; // CBR estimate; Xing overrides
  }
  int read(File& f,uint8_t* input,int16_t* pcm,const char*& error) {
    for(;;) {
      if(at==end)return 0;
      if(end-at<4 || !WavState::readAt(f,at,input,4) || !header(input)){error="mp3_header";return -1;}
      if(frameBytes>end-at || !WavState::readAt(f,at,input,frameBytes)){error="mp3_truncated_frame";return -1;}
      at+=frameBytes;
      if(first) {
        const bool v1=((input[1]>>3)&3)==3;
        const unsigned x=4+((input[1]&1)?0:2)+(v1?(channels==1?17:32):(channels==1?9:17));
        if(x+12<=frameBytes && (!memcmp(input+x,"Xing",4)||!memcmp(input+x,"Info",4)) && (be32(input+x+4)&1))
          total=uint64_t(be32(input+x+8))*samples;
        first=false;
      }
      unsigned char* p=input; int left=frameBytes;
      const int rc=MP3Decode(&core.decoder,&p,&left,pcm,0);
      if(rc==ERR_MP3_MAINDATA_UNDERFLOW)continue; // reservoir priming, bounded by file length
      if(rc){error="mp3_decode";return -1;}
      MP3FrameInfo info{}; MP3GetLastFrameInfo(&core.decoder,&info);
      if(info.nChans!=int(channels)||info.samprate!=int(rate)||info.outputSamps!=int(samples*channels)){error="mp3_format_change";return -1;}
      if(channels==1)for(int i=int(samples)-1;i>=0;--i)pcm[2*i+1]=pcm[2*i]=pcm[i];
      return samples;
    }
  }
};
