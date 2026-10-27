#ifndef CYD_UPLOADER
#include "codec_track.h"
#include <new>
#include "wav_track.h"
#ifdef ARDUINO
#include <cydpod_helix_mp3.h> // unique header selects the byte-identical MP3-only staged core
#endif
#include "mp3_pool.h"
#include "flac_pool.h"
#include <string.h>
#include <stddef.h>
namespace {
struct AacState {
  alignas(8) uint8_t workspace[MINIAAC_WORKSPACE_BYTES];
  M4aTrack track;
  AacState(int16_t* pcm) : track(workspace, pcm) {}
};
union CodecState { AacState aac; WavState wav; Mp3State mp3; FlacState flac; CodecState() {} ~CodecState() {} };
struct PoolLayout {
  alignas(8) int16_t pcm[2304];      // 1152 stereo MP3 frames, also AAC/WAV. Placed first: FLAC's second channel
                                     // (4608 int32) starts here and continues into FlacState::rightTail.
  CodecState state;
  uint8_t input[2048];
  FirState fir;
  uint8_t kind; // 0 closed, 1 AAC, 2 WAV, 3 MP3, 4 FLAC
  File* file;
  size_t frames, used;
  uint64_t total, seekFrame;
  uint32_t rate;
  const char* error;
};
static_assert(sizeof(AacState) + 4608 + 2048 + 524 + 128 <= 40960, "AAC inclusive pool budget");
static_assert(sizeof(Mp3State) + 4608 + 2048 + 524 + 128 <= 40960, "MP3 inclusive pool budget");
static_assert(sizeof(WavState) + 4608 + 2048 + 524 + 128 <= 40960, "WAV inclusive pool budget");
static_assert(sizeof(FlacState) + 4608 + 2048 + 524 + 128 <= 40960, "FLAC inclusive pool budget");
static_assert(sizeof(PoolLayout) <= 40960, "inclusive codec pool overflow");
static_assert(offsetof(PoolLayout,state)==sizeof(PoolLayout::pcm) && offsetof(FlacState,rightTail)==0, "FLAC right channel must run from pcm into FlacState::rightTail");
union alignas(8) DecoderPool {
  PoolLayout layout;
  uint8_t bytes[40960];
  DecoderPool() : layout{} {}
  ~DecoderPool() {}
};
static_assert(sizeof(DecoderPool)==40960,"single inclusive pool size");
static DecoderPool codecPool; // the ONLY decoder/PCM/input/FIR allocation (.bss)
PoolLayout& pool() { return codecPool.layout; }
}
miniaac_status CodecTrack::begin(File& file) {
  end(); pool().fir.reset(); pool().file=&file; pool().error="ok"; pool().rate=44100;
  uint8_t magic[4]={};
  if (WavState::readAt(file,0,magic,4) && !memcmp(magic,"RIFF",4)) {
    pool().kind=2; new (&pool().state.wav) WavState{};
    const char* error=pool().state.wav.open(file);
    if (error) { pool().error=error; return MINIAAC_UNSUPPORTED; }
    pool().rate=pool().state.wav.rate;
    if(pool().rate!=44100 && !firBank(pool().rate)){pool().error="wav_sample_rate";return MINIAAC_UNSUPPORTED;}
    pool().total=pool().state.wav.bytes/pool().state.wav.align;
    return MINIAAC_OK;
  }
  if (!memcmp(magic,"fLaC",4)) {
    pool().kind=4; new (&pool().state.flac) FlacState{}; pool().state.flac.right=reinterpret_cast<int32_t*>(pool().pcm);
    const char* error=pool().state.flac.open(file,pool().input);
    if (error) { pool().error=error; return MINIAAC_UNSUPPORTED; }
    pool().rate=pool().state.flac.rate;
    if(pool().rate!=44100 && !firBank(pool().rate)){pool().error="flac_sample_rate";return MINIAAC_UNSUPPORTED;}
    pool().total=pool().state.flac.total;
    return MINIAAC_OK;
  }
  if (!memcmp(magic,"ID3",3) || (magic[0]==255 && (magic[1]&224)==224 && ((magic[1]>>1)&3)==1)) {
    pool().kind=3; new (&pool().state.mp3) Mp3State{};
    const char* error=pool().state.mp3.open(file);
    if(error){pool().error=error;return MINIAAC_UNSUPPORTED;}
    const uint32_t at=pool().state.mp3.at;
    if (pool().state.mp3.frameBytes<=pool().state.mp3.end-at && WavState::readAt(file,at,pool().input,pool().state.mp3.frameBytes)) {
      const uint8_t* b=pool().input;const bool v1=((b[1]>>3)&3)==3;
      const unsigned x=4+((b[1]&1)?0:2)+(v1?(pool().state.mp3.channels==1?17:32):(pool().state.mp3.channels==1?9:17));
      if(x+12<=pool().state.mp3.frameBytes && (!memcmp(b+x,"Xing",4)||!memcmp(b+x,"Info",4)) && (Mp3State::be32(b+x+4)&1))
        pool().state.mp3.total=uint64_t(Mp3State::be32(b+x+8))*pool().state.mp3.samples;
    }
    pool().rate=pool().state.mp3.rate;pool().total=pool().state.mp3.total;return MINIAAC_OK;
  }
  new (&pool().state.aac) AacState(pool().pcm); pool().kind=1;
  const miniaac_status rc=pool().state.aac.track.begin(file);
  if(rc!=MINIAAC_OK)return rc;
  pool().rate=pool().state.aac.track.sampleRate();
  if(pool().rate!=44100 && !firBank(pool().rate)){pool().error="aac_sample_rate";return MINIAAC_UNSUPPORTED;}
  return MINIAAC_OK;
}
void CodecTrack::end() {
  if (pool().kind==1) pool().state.aac.~AacState();
  memset(codecPool.bytes, 0, sizeof(codecPool));
  new (&codecPool.layout) PoolLayout{};
}
CodecTrack::Pull CodecTrack::next() {
  if(pool().kind==1) return pool().state.aac.track.next();
  if(pool().kind==3) {
    pool().used=pool().frames=0;
    const int n=pool().state.mp3.read(*pool().file,pool().input,pool().pcm,pool().error);
    pool().total=pool().state.mp3.total;
    if(n<0)return Pull::error;
    if(pool().rate!=pool().state.mp3.rate){pool().error="mp3_rate_change";return Pull::error;}
    pool().frames=size_t(n);return n?Pull::frames:Pull::eof;
  }
  if(pool().kind==4) {
    pool().used=pool().frames=0;
    const int n=pool().state.flac.read(*pool().file,pool().input,pool().pcm,pool().error);
    if(n<0)return Pull::error;
    pool().frames=size_t(n);return n?Pull::frames:Pull::eof;
  }
  if(pool().kind!=2) return Pull::error;
  pool().used=pool().frames=0;
  const int n=pool().state.wav.read(*pool().file,pool().input,pool().pcm);
  if(n<0) {pool().error="wav_read";return Pull::error;}
  pool().frames=size_t(n); return n?Pull::frames:Pull::eof;
}
bool CodecTrack::seekable() const { return pool().kind==1 ? pool().state.aac.track.seekable() : pool().kind>=2 && pool().total; }
miniaac_status CodecTrack::seek(uint32_t ms, miniaac_seek_hook hook, void* user) {
  pool().fir.reset();
  if (!seekable()) return MINIAAC_UNSUPPORTED;
  pool().seekFrame=uint64_t(ms)*pool().rate/1000;
  if(pool().seekFrame>=totalFrames())pool().seekFrame=totalFrames()-1;
  if(pool().kind==2) {
    if(hook && hook(user))return MINIAAC_CANCELLED;
    auto& wav=pool().state.wav;
    const uint32_t cursor=uint32_t(pool().seekFrame*wav.align);
    if(!pool().file->seek(wav.data+cursor)){pool().error="wav_seek";return MINIAAC_IO;}
    wav.cursor=cursor;pool().frames=pool().used=0;pool().error="ok";
    return MINIAAC_OK;
  }
  if(pool().kind==3) {
    // Replay from the first frame: exact for CBR without Xing and VBR alike,
    // and rebuilds both the bit reservoir and synthesis overlap. Yield/cancel
    // every 32 frames so long seeks remain responsive, including while paused.
    auto& mp3=pool().state.mp3;
    const char* error=mp3.open(*pool().file);
    if(error){pool().error=error;return MINIAAC_IO;}
    mp3.drops=mp3.run=0;
    pool().frames=pool().used=0;
    uint64_t decoded=0;size_t last=0;unsigned blocks=0;
    for(;;) {
      if((blocks++%32)==0 && hook && hook(user))return MINIAAC_CANCELLED;
      const int n=mp3.read(*pool().file,pool().input,pool().pcm,pool().error);
      if(n<0)return MINIAAC_IO;
      if(!n) {
        if(!last){pool().error="mp3_seek_eof";return MINIAAC_IO;}
        mp3.total=pool().total=decoded;pool().seekFrame=decoded-1;
        pool().frames=last;pool().used=last-1;break;
      }
      last=size_t(n);
      if(pool().seekFrame<decoded+last) {
        pool().frames=last;pool().used=size_t(pool().seekFrame-decoded);break;
      }
      decoded+=last;
    }
    pool().error="ok";return MINIAAC_OK;
  }
  if(pool().kind==4) {
    pool().frames=pool().used=0;
    switch(pool().state.flac.seek(*pool().file,pool().input,ms,hook,user,pool().error)) {
      case FlacState::Seek::ok: pool().error="ok"; return MINIAAC_OK;
      case FlacState::Seek::cancelled: return MINIAAC_CANCELLED;
      default: return MINIAAC_IO;
    }
  }
  return pool().kind==1 ? pool().state.aac.track.seek(ms,hook,user) : MINIAAC_UNSUPPORTED;
}
uint32_t CodecTrack::seekPositionMs() const { return uint32_t(pool().seekFrame*1000/pool().rate); }
size_t CodecTrack::pending() const { return pool().kind==1 ? pool().state.aac.track.pending() : pool().frames-pool().used; }
const M4aStereo* CodecTrack::pcm() const {
  return pool().kind==1 ? pool().state.aac.track.pcm() : reinterpret_cast<const M4aStereo*>(pool().pcm)+pool().used;
}
void CodecTrack::consume(size_t frames) { if(pool().kind==1) pool().state.aac.track.consume(frames); else pool().used+=frames; }
uint64_t CodecTrack::totalFrames() const { return pool().kind==1 ? pool().state.aac.track.totalFrames() : pool().total; }
uint32_t CodecTrack::sampleRate() const { return pool().rate; }
bool CodecTrack::output(int16_t l,int16_t r,PcmSink sink,void* user) {
  if(pool().rate==44100)return sink(user,l,r);
  const FirBank* b=firBank(pool().rate);
  return b && pool().fir.push(l,r,*b,sink,user);
}
bool CodecTrack::drain(PcmSink sink,void* user) {
  if(pool().rate==44100)return true;
  const FirBank* b=firBank(pool().rate);if(!b)return false;
  // Causal zero-start FIR retains its ~63.5-source-frame delay. Flush 127
  // zero frames once at EOF; track switches/seek discard history instead.
  for(unsigned i=0;i<127;++i)if(!pool().fir.push(0,0,*b,sink,user))return false;
  pool().fir.reset();return true;
}
const char* CodecTrack::name() const { if(pool().error && strcmp(pool().error,"ok"))return pool().error;return pool().kind==1 ? pool().state.aac.track.name() : pool().error?pool().error:"closed"; }
#endif
