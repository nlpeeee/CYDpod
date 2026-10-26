#pragma once
// CYDpod: MiniAAC (vendor/miniaac, Arduino library "MinimalAAC") over the one open SD File.
// Adapted from firmware/cydpod_probe/miniaac_adapter.h (CY-31) without the probe's audio_core.h dependency.
// Caller owns workspace and PCM; this adapter never allocates.
#include <FS.h>
#include "miniaac.h"

struct M4aStereo { int16_t l, r; };

class M4aTrack {
 public:
  enum class Pull : uint8_t { frames, eof, error };
  static_assert(MINIAAC_WORKSPACE_BYTES % MINIAAC_ALIGNMENT == 0, "8-byte aligned workspace");
  static_assert(MINIAAC_PCM_SAMPLES * sizeof(int16_t) == 4096, "MiniAAC PCM buffer is 2048 int16_t");
  M4aTrack(uint8_t* workspace, int16_t* pcm) : workspace_(workspace), pcm_(pcm) {}
  ~M4aTrack() { end(); }
  // Opens file (M4A or ADTS, decided by MiniAAC from the content). On failure name() says why and nothing is left open.
  miniaac_status begin(File& file) {
    end();
    name_ = "ok"; frames_ = used_ = 0; total_ = 0; file_ = &file;
    const miniaac_source source{readAt, this, uint64_t(file.size())};
    miniaac_status rc = miniaac_open(workspace_, MINIAAC_WORKSPACE_BYTES, &source, &decoder_);
    const char* reason = nullptr;
    if (rc == MINIAAC_OK) {
      miniaac_info info{};
      rc = miniaac_get_info(decoder_, &info);
      if (rc == MINIAAC_OK) {
        if (!info.sample_rate || (info.channels != 1 && info.channels != 2)) { reason = "aac_format"; rc = MINIAAC_UNSUPPORTED; }
        else { rate_=info.sample_rate;channels_=info.channels; }
        if (rc==MINIAAC_OK && info.total_frames != UINT64_MAX) total_ = info.total_frames;
      }
    }
    if (rc != MINIAAC_OK) { name_ = reason ? reason : miniaac_status_string(rc); end(); }
    return rc;
  }
  // Idempotent. Must run before the File is closed.
  void end() { if (decoder_) miniaac_close(decoder_); decoder_ = nullptr; file_ = nullptr; frames_ = used_ = 0; }
  // Decodes the next block once the previous one is fully consumed.
  Pull next() {
    if (!decoder_) { name_ = "argument"; return Pull::error; }
    size_t count = 0; frames_ = used_ = 0;
    const miniaac_status rc = miniaac_decode_next(decoder_, pcm_, MINIAAC_PCM_SAMPLES, &count);
    if (rc == MINIAAC_EOF) return Pull::eof;
    if (rc != MINIAAC_OK) { name_ = miniaac_status_string(rc); return Pull::error; }
    stereo(count);
    return Pull::frames;
  }
  bool seekable() const { return total_ && miniaac_seekable(decoder_); }
  miniaac_status seek(uint32_t ms, miniaac_seek_hook hook, void* user) {
    size_t count = 0; frames_ = used_ = 0;
    const uint64_t frame = min<uint64_t>(uint64_t(ms) * rate_ / 1000, total_ ? total_ - 1 : 0);
    const miniaac_status rc = miniaac_seek(decoder_, frame, pcm_, MINIAAC_PCM_SAMPLES, &count, hook, user);
    if (rc == MINIAAC_OK) { stereo(count); name_ = "ok"; }
    else if (rc != MINIAAC_CANCELLED) name_ = miniaac_status_string(rc);
    return rc;
  }
  size_t pending() const { return frames_ - used_; }
  const M4aStereo* pcm() const { return reinterpret_cast<const M4aStereo*>(pcm_) + used_; }
  void consume(size_t frames) { used_ += frames; }
  uint32_t sampleRate() const { return rate_; }
  uint64_t totalFrames() const { return total_; }   // 0: unknown
  const char* name() const { return name_; }

 private:
  void stereo(size_t count) {
    frames_=count/channels_;
    if(channels_==1)for(int i=int(frames_)-1;i>=0;--i)pcm_[2*i+1]=pcm_[2*i]=pcm_[i];
  }
  uint32_t rate_=44100;
  uint8_t channels_=2;
  // MiniAAC asks for absolute offsets; this is the only user of the handle, so seek only when the position differs.
  static int readAt(void* user, uint64_t offset, void* dst, size_t bytes) {
    File* file = static_cast<M4aTrack*>(user)->file_;
    if (!file || offset > UINT32_MAX) return 1;
    if (file->position() != offset && !file->seek(uint32_t(offset))) return 1;
    return file->read(static_cast<uint8_t*>(dst), bytes) == bytes ? 0 : 1;
  }
  uint8_t* workspace_;
  int16_t* pcm_;
  miniaac* decoder_ = nullptr;
  File* file_ = nullptr;
  size_t frames_ = 0, used_ = 0;
  uint64_t total_ = 0;
  const char* name_ = "ok";
};
