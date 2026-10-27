#pragma once
#include <FS.h>
// Bounded RIFF PCM only. No packed casts, heap, or whole-file buffers.
struct WavState {
  uint32_t data, bytes, cursor, rate;
  uint16_t channels, bits, align;
  static uint16_t le16(const uint8_t* p) { return p[0] | uint16_t(p[1])<<8; }
  static uint32_t le32(const uint8_t* p) { return le16(p) | uint32_t(le16(p+2))<<16; }
  static bool readAt(File& f, uint32_t at, uint8_t* p, size_t n) {
    return (f.position()==at || f.seek(at)) && f.read(p,n)==n;
  }
  const char* open(File& f) {
    uint8_t h[24];
    if (!readAt(f,0,h,12) || memcmp(h,"RIFF",4) || memcmp(h+8,"WAVE",4)) return "wav_header";
    // Streaming recorders and pipes write 0 or 0xFFFFFFFF; copies can be cut short: those sizes mean "to the end of the file".
    const uint32_t riff=le32(h+4);
    const bool unsized=riff==0 || riff==0xFFFFFFFFu;
    uint64_t end=uint64_t(riff)+8;
    if (unsized || end>f.size()) end=f.size();
    if (end<12) return "wav_truncated_riff";
    bool fmt=false, found=false;
    for (uint64_t at=12; at<end;) {
      if (end-at<8) break; // 1..7 stray bytes after the last chunk
      if (!readAt(f,uint32_t(at),h,8)) return "wav_truncated_chunk";
      uint32_t n=le32(h+4); uint64_t next=at+8+uint64_t(n)+(n&1);
      const bool isData=!memcmp(h,"data",4);
      if (isData && !found && (n==0xFFFFFFFFu || (n==0 && unsized) || next>end)) { n=uint32_t(end-(at+8)); next=end; } // clamp to the file
      if (next>end) return "wav_truncated_chunk";
      if (!memcmp(h,"fmt ",4)) {
        if (fmt || n<16 || (n&1) || !readAt(f,uint32_t(at+8),h,16)) return "wav_fmt";
        if (le16(h)!=1) return "wav_not_pcm"; // float/extensible/compressed rejected explicitly
        channels=le16(h+2); rate=le32(h+4); align=le16(h+12); bits=le16(h+14);
        if ((channels!=1 && channels!=2) || (bits!=8 && bits!=16 && bits!=24 && bits!=32) ||
            !rate || rate>96000 || align!=channels*(bits/8) || uint64_t(rate)*align!=le32(h+8)) return "wav_format";
        fmt=true;
      } else if (isData) {
        if (found) return "wav_duplicate_data";
        data=uint32_t(at+8); bytes=n; found=true;
      }
      at=next;
    }
    if (!fmt || !found) return "wav_incomplete_frame";
    bytes-=bytes%align; // a cut last frame is dropped, not an error
    cursor=0; return nullptr;
  }
  static int16_t sample(const uint8_t* p, unsigned bits) {
    if (bits==8) return int16_t((int(p[0])-128)*256);
    // Discard low bits for 24/32 PCM, preserving two's-complement sign.
    p+=bits/8-2; return int16_t(le16(p));
  }
  int read(File& f, uint8_t* input, int16_t* pcm) {
    if (cursor==bytes) return 0;
    const uint32_t remaining=bytes-cursor;
    const size_t capacity=(2048/align<1152?2048/align:1152)*align;
    const size_t n=remaining<capacity ? remaining : capacity;
    if (!readAt(f,data+cursor,input,n)) return -1;
    const size_t frames=n/align, step=bits/8;
    for(size_t i=0;i<frames;++i) {
      pcm[2*i]=sample(input+i*align,bits);
      pcm[2*i+1]=channels==1?pcm[2*i]:sample(input+i*align+step,bits);
    }
    cursor+=n; return int(frames);
  }
};
