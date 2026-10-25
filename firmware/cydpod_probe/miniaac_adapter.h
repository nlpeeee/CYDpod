#pragma once
// CY-31: MiniAAC (vendor/miniaac, Arduino library "MinimalAAC") over the one open SD File. Replaces the AudioTools M4A reader,
// guard, arena, allocator hook and ADTS queue in the probe's play path. No heap use at all: the workspace and the 4 KiB PCM
// buffer are members of one static object.
#include <FS.h>
#include <atomic>
#include "audio_core.h"
#include "miniaac.h"

// Keep strict format diagnostics local to probe code, after dependency includes.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wformat"
#pragma GCC diagnostic error "-Wformat-security"
#endif

namespace cydpod {
// Names for m4aState / "ERR m4a <name>". MiniAAC's own status set, lower-case, plus the probe's output-chain rejection.
inline const char* miniAacName(miniaac_status s) {
  switch(s) {
    case MINIAAC_OK: return "ok";
    case MINIAAC_EOF: return "eof";
    case MINIAAC_ARGUMENT: return "argument";
    case MINIAAC_MEMORY: return "memory";
    case MINIAAC_IO: return "io";
    case MINIAAC_FORMAT: return "format";
    case MINIAAC_UNSUPPORTED: return "unsupported";
    case MINIAAC_FRAME_TOO_LARGE: return "frame_too_large";
    case MINIAAC_CODEC: return "codec";
    case MINIAAC_OUTPUT_TOO_SMALL: return "output_too_small";
    default: return "unknown";
  }
}
constexpr const char* kNotStereo44100="not_44100_stereo"; // the A2DP/I2S chain is fixed at 44.1 kHz stereo; nothing resamples or mixes down

class MiniAacTrack {
 public:
  enum class Pull : uint8_t { frames, eof, error };
  static_assert(MINIAAC_WORKSPACE_BYTES%MINIAAC_ALIGNMENT==0, "8-byte aligned workspace");
  static_assert(MINIAAC_PCM_SAMPLES*sizeof(int16_t)==4096, "MiniAAC PCM buffer is 2048 int16_t");
  static_assert(sizeof(Stereo)==2*sizeof(int16_t), "stereo frames are two int16_t");
  ~MiniAacTrack() { end(); }
  // Opens file (M4A or ADTS, decided by MiniAAC from the content). On failure name() says why and nothing is left open.
  miniaac_status begin(File& file) {
    end(); reads.store(0); name_=miniAacName(MINIAAC_OK); frames_=used_=0; file_=&file;
    const miniaac_source source{readAt,this,uint64_t(file.size())};
    miniaac_status rc=miniaac_open(workspace_,sizeof(workspace_),&source,&decoder_);
    const char* reason=nullptr;
    if(rc==MINIAAC_OK) {
      miniaac_info info{};
      rc=miniaac_get_info(decoder_,&info);
      if(rc==MINIAAC_OK && (info.sample_rate!=44100 || info.channels!=2)) { reason=kNotStereo44100; rc=MINIAAC_UNSUPPORTED; }
    }
    if(rc!=MINIAAC_OK) { name_=reason?reason:miniAacName(rc); end(); }
    return rc;
  }
  // Idempotent. Must run before the File is closed.
  void end() { if(decoder_) miniaac_close(decoder_); decoder_=nullptr; file_=nullptr; frames_=used_=0; }
  // Decodes the next block once the previous one is fully consumed (partial ring pushes keep the remainder).
  Pull next() {
    if(!decoder_) { name_=miniAacName(MINIAAC_ARGUMENT); return Pull::error; }
    size_t count=0; frames_=used_=0;
    const miniaac_status rc=miniaac_decode_next(decoder_,pcm_,MINIAAC_PCM_SAMPLES,&count);
    if(rc==MINIAAC_EOF) return Pull::eof;
    if(rc!=MINIAAC_OK) { name_=miniAacName(rc); return Pull::error; }
    frames_=count/2; return Pull::frames; // stereo only (checked in begin)
  }
  size_t pending() const { return frames_-used_; }
  const Stereo* pcm() const { return reinterpret_cast<const Stereo*>(pcm_)+used_; }
  void consume(size_t frames) { used_+=frames; }
  const char* name() const { return name_; } // static-lifetime string
  std::atomic<uint32_t> reads{0}; // source read callbacks of the current track (producer writes)

 private:
  // MiniAAC asks for absolute offsets; this is the only user of the handle, so seek only when the position differs.
  static int readAt(void* user,uint64_t offset,void* dst,size_t bytes) {
    MiniAacTrack* self=static_cast<MiniAacTrack*>(user);
    self->reads.fetch_add(1);
    File* file=self->file_;
    if(!file || offset>UINT32_MAX) return 1;
    if(file->position()!=offset && !file->seek(uint32_t(offset))) return 1;
    return file->read(static_cast<uint8_t*>(dst),bytes)==bytes ? 0 : 1;
  }
  alignas(MINIAAC_ALIGNMENT) uint8_t workspace_[MINIAAC_WORKSPACE_BYTES]; // 22,528 B; miniaac_workspace_size() is 22,480 B on xtensa
  alignas(4) int16_t pcm_[MINIAAC_PCM_SAMPLES];
  miniaac* decoder_=nullptr;
  File* file_=nullptr;
  size_t frames_=0, used_=0;
  const char* name_="ok";
};
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
