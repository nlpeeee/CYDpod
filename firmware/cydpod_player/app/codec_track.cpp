#ifndef CYD_UPLOADER
#include "codec_track.h"
#include <new>
#include "wav_track.h"
#include <string.h>
namespace {
struct AacState {
  alignas(8) uint8_t workspace[MINIAAC_WORKSPACE_BYTES];
  M4aTrack track;
  AacState(int16_t* pcm) : track(workspace, pcm) {}
};
union CodecState { AacState aac; WavState wav; CodecState() {} ~CodecState() {} };
struct PoolLayout {
  CodecState state;
  int16_t pcm[2304];                 // 1152 stereo MP3 frames, also AAC/WAV
  uint8_t input[2048];
  uint8_t firHistory[524];           // reserved for the streaming FIR
  uint8_t kind; // 0 closed, 1 AAC, 2 WAV, 3 MP3
  File* file;
  size_t frames, used;
  uint64_t total;
  uint32_t rate;
  const char* error;
};
static_assert(sizeof(AacState) + 4608 + 2048 + 524 + 128 <= 40960, "AAC inclusive pool budget");
static_assert(sizeof(WavState) + 4608 + 2048 + 524 + 128 <= 40960, "WAV inclusive pool budget");
static_assert(sizeof(PoolLayout) <= 40960, "inclusive codec pool overflow");
alignas(8) static uint8_t codecPool[40960]; // the ONLY decoder/PCM/input/FIR allocation
PoolLayout& pool() { return *reinterpret_cast<PoolLayout*>(codecPool); }
}
miniaac_status CodecTrack::begin(File& file) {
  end(); pool().file=&file; pool().error="ok"; pool().rate=44100;
  uint8_t magic[4];
  if (WavState::readAt(file,0,magic,4) && !memcmp(magic,"RIFF",4)) {
    pool().kind=2; new (&pool().state.wav) WavState{};
    const char* error=pool().state.wav.open(file);
    if (error) { pool().error=error; return MINIAAC_UNSUPPORTED; }
    pool().rate=pool().state.wav.rate; pool().total=pool().state.wav.bytes/pool().state.wav.align;
    return MINIAAC_OK;
  }
  new (&pool().state.aac) AacState(pool().pcm); pool().kind=1;
  return pool().state.aac.track.begin(file);
}
void CodecTrack::end() {
  if (pool().kind==1) pool().state.aac.~AacState();
  memset(codecPool, 0, sizeof(codecPool));
}
CodecTrack::Pull CodecTrack::next() {
  if(pool().kind==1) return pool().state.aac.track.next();
  if(pool().kind!=2) return Pull::error;
  pool().used=pool().frames=0;
  const int n=pool().state.wav.read(*pool().file,pool().input,pool().pcm);
  if(n<0) {pool().error="wav_read";return Pull::error;}
  pool().frames=size_t(n); return n?Pull::frames:Pull::eof;
}
bool CodecTrack::seekable() const { return pool().kind==1 && pool().state.aac.track.seekable(); }
miniaac_status CodecTrack::seek(uint32_t ms, miniaac_seek_hook hook, void* user) {
  return pool().kind==1 ? pool().state.aac.track.seek(ms,hook,user) : MINIAAC_UNSUPPORTED;
}
size_t CodecTrack::pending() const { return pool().kind==1 ? pool().state.aac.track.pending() : pool().frames-pool().used; }
const M4aStereo* CodecTrack::pcm() const {
  return pool().kind==1 ? pool().state.aac.track.pcm() : reinterpret_cast<const M4aStereo*>(pool().pcm)+pool().used;
}
void CodecTrack::consume(size_t frames) { if(pool().kind==1) pool().state.aac.track.consume(frames); else pool().used+=frames; }
uint64_t CodecTrack::totalFrames() const { return pool().kind==1 ? pool().state.aac.track.totalFrames() : pool().total; }
uint32_t CodecTrack::sampleRate() const { return pool().rate; }
const char* CodecTrack::name() const { return pool().kind==1 ? pool().state.aac.track.name() : pool().error?pool().error:"closed"; }
#endif
