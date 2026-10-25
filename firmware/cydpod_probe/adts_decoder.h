#pragma once
#include "audio_core.h"
#include <libhelix-aac/aacdec.h>
#include <ConfigHelix.h>

#if !defined(CYDPOD_AAC_LC_ONLY) || defined(HELIX_FEATURE_AUDIO_CODEC_AAC_SBR)
#error CY-17 PCM bound requires the pinned LC-only Helix build
#endif

// Keep strict format diagnostics local to probe code, after dependency includes.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wformat"
#pragma GCC diagnostic error "-Wformat-security"
#endif

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
  static constexpr int kPcmShorts=2048;
  static_assert(AAC_MAX_NCHANS==2 && AAC_MAX_NSAMPS*AAC_MAX_NCHANS==kPcmShorts,
                "CY-17 buffer and accepted outputSamps must match the Helix LC bound");
  static_assert(sizeof(short)==2, "16-bit PCM shorts");
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
       info.sampRateOut!=44100 || info.bitsPerSample!=16 || info.outputSamps!=kPcmShorts)
      return DecodeResult::codec_error;
    return DecodeResult::frame;
  }
  const Stereo* pcm() const { return reinterpret_cast<const Stereo*>(pcm_); }
  AACFrameInfo info{};
  int lastError=0;
 private:
  HAACDecoder decoder_=nullptr;
  uint8_t encoded_[2048]{};
  // CY-17: LC-only stereo output bound, including mismatched elements; see cy17-pcm-buffer.md.
  alignas(4) short pcm_[kPcmShorts]{};
};
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
