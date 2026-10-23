#pragma once
#include "audio_core.h"
#include <libhelix-aac/aacdec.h>

namespace cydpod {
enum class DecodeResult { frame, eof, truncated, unsupported, malformed, allocation, codec_error };
inline const char* resultName(DecodeResult r) {
  switch(r) {
    case DecodeResult::frame: return "frame";
    case DecodeResult::eof: return "eof";
    case DecodeResult::truncated: return "truncated ADTS frame";
    case DecodeResult::unsupported: return "requires AAC-LC 44100 stereo, one block, frame <=2048 bytes";
    case DecodeResult::malformed: return "invalid ADTS header (M4A/ID3/ADIF unsupported)";
    case DecodeResult::allocation: return "AAC allocation failed";
    default: return "Helix decode/profile error";
  }
}
class AdtsDecoder {
 public:
  ~AdtsDecoder() { end(); }
  bool begin() { end(); decoder_ = AACInitDecoder(); return decoder_ != nullptr; }
  void end() { if(decoder_) AACFreeDecoder(decoder_); decoder_=nullptr; }
  // Reader returns actual bytes read; 0 means EOF or read failure.
  template <typename Reader> DecodeResult next(Reader read) {
    if(!decoder_) return DecodeResult::allocation;
    size_t n=read(encoded_, 7);
    if(n==0) return DecodeResult::eof;
    while(n<7) { size_t got=read(encoded_+n,7-n); if(!got) return DecodeResult::truncated; n+=got; }
    size_t length=0;
    HeaderResult header=adtsHeader(encoded_,7,length);
    if(header==HeaderResult::unsupported || length>sizeof(encoded_)) return DecodeResult::unsupported;
    if(header!=HeaderResult::valid) return DecodeResult::malformed;
    while(n<length) { size_t got=read(encoded_+n,length-n); if(!got) return DecodeResult::truncated; n+=got; }
    uint8_t* cursor=encoded_; int remaining=static_cast<int>(length);
    lastError=AACDecode(decoder_,&cursor,&remaining,pcm_);
    if(lastError || remaining!=0) return DecodeResult::codec_error;
    AACGetLastFrameInfo(decoder_, &info);
    if(info.profile!=AAC_PROFILE_LC || info.nChans!=2 || info.sampRateCore!=44100 ||
       info.sampRateOut!=44100 || info.bitsPerSample!=16 || info.outputSamps!=2048)
      return DecodeResult::codec_error;
    return DecodeResult::frame;
  }
  const Stereo* pcm() const { return reinterpret_cast<const Stereo*>(pcm_); }
  AACFrameInfo info{};
  int lastError=0;
 private:
  HAACDecoder decoder_=nullptr;
  uint8_t encoded_[2048]{};
  // Sized for Helix's maximum stereo SBR output as a defensive bound; SBR is disabled in the build.
  alignas(4) short pcm_[4096]{};
};
}
