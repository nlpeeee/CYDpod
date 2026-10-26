#pragma once
#include "m4a_track.h"
#include "resampler.h"
// Single-producer codec contract. All live storage is in codecPool (.bss).
class CodecTrack {
 public:
  using Pull = M4aTrack::Pull;
  miniaac_status begin(File& file);
  void end();
  bool output(int16_t l,int16_t r,PcmSink sink,void* user);
  bool drain(PcmSink sink,void* user);
  Pull next();
  bool seekable() const;
  miniaac_status seek(uint32_t ms, miniaac_seek_hook hook, void* user);
  size_t pending() const;
  const M4aStereo* pcm() const;
  void consume(size_t frames);
  uint64_t totalFrames() const;
  uint32_t sampleRate() const;
  const char* name() const;
};
