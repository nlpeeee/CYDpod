#ifndef CYD_UPLOADER
#include "codec_track.h"
#include <new>
#include <string.h>
namespace {
struct AacState {
  alignas(8) uint8_t workspace[MINIAAC_WORKSPACE_BYTES];
  M4aTrack track;
  AacState(int16_t* pcm) : track(workspace, pcm) {}
};
union CodecState { AacState aac; CodecState() {} ~CodecState() {} };
struct PoolLayout {
  CodecState state;
  int16_t pcm[2304];                 // 1152 stereo MP3 frames, also AAC/WAV
  uint8_t input[2048];
  uint8_t firHistory[524];           // reserved for the streaming FIR
  bool active;
};
static_assert(sizeof(AacState) + 4608 + 2048 + 524 + 128 <= 40960, "AAC inclusive pool budget");
static_assert(sizeof(PoolLayout) <= 40960, "inclusive codec pool overflow");
alignas(8) static uint8_t codecPool[40960]; // the ONLY decoder/PCM/input/FIR allocation
PoolLayout& pool() { return *reinterpret_cast<PoolLayout*>(codecPool); }
}
miniaac_status CodecTrack::begin(File& file) {
  end();
  memset(codecPool, 0, sizeof(codecPool));
  new (&pool().state.aac) AacState(pool().pcm);
  pool().active = true;
  return pool().state.aac.track.begin(file);
}
void CodecTrack::end() {
  if (pool().active) pool().state.aac.~AacState();
  memset(codecPool, 0, sizeof(codecPool));
}
CodecTrack::Pull CodecTrack::next() { return pool().state.aac.track.next(); }
bool CodecTrack::seekable() const { return pool().active && pool().state.aac.track.seekable(); }
miniaac_status CodecTrack::seek(uint32_t ms, miniaac_seek_hook hook, void* user) { return pool().state.aac.track.seek(ms,hook,user); }
size_t CodecTrack::pending() const { return pool().active ? pool().state.aac.track.pending() : 0; }
const M4aStereo* CodecTrack::pcm() const { return pool().state.aac.track.pcm(); }
void CodecTrack::consume(size_t frames) { pool().state.aac.track.consume(frames); }
uint64_t CodecTrack::totalFrames() const { return pool().state.aac.track.totalFrames(); }
uint32_t CodecTrack::sampleRate() const { return 44100; }
const char* CodecTrack::name() const { return pool().active ? pool().state.aac.track.name() : "closed"; }
#endif
