// ===========================================================================
// wired_pump.h -- the wired writer's chunk logic, without the driver (CY-71 / W3)
// ===========================================================================
// Pure C++ templates over three small adapters, so tests/wired_pump_test.cpp
// can drive the same code the writer task runs against a mock ring and a mock
// I2S sink. wired.cpp supplies the real ones (audio.h and i2s_channel_write).
//
//   Audio  playing() seeking() serial() pulledFrames() underruns()
//          pull(int16_t* stereo, int32_t frames)         -- audio_pull()
//   Write  WriteStatus (const uint8_t* p, size_t bytes, size_t* written)
//
// Nothing here blocks except through the Write adapter, and the adapter is
// given a bounded timeout: a stuck DMA costs at most kMaxWriteAttempts
// timeouts before the chunk is dropped and counted, never an endless wait.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "wired_fade.h"

namespace cydwired {

constexpr int32_t kChunkFrames = 256;                       // 5.8 ms, same as speaker.cpp
constexpr size_t kChunkBytes = kChunkFrames * 2 * sizeof(int16_t);
constexpr int kMaxWriteAttempts = 3;                         // per chunk, timeouts and errors together
constexpr int32_t kLeadInFrames = 9 * kChunkFrames;          // 52 ms of zeros: PLL lock + DAC un-mute
// Zeros after the final fade. i2s_channel_write() returns once the data is QUEUED, and the
// DMA holds up to kDmaCapacityFrames of it, so the fade is on the wire only after at least
// that many further frames have been accepted. One chunk of margin on top.
constexpr int32_t kDmaCapacityFrames = 6 * 256;              // dma_desc_num x dma_frame_num (wired.cpp)
constexpr int32_t kTailFrames = kDmaCapacityFrames + kChunkFrames;   // 1792 frames, 40.6 ms

enum class WriteStatus : uint8_t { Ok, Timeout, Error };

struct Counters {
  uint32_t chunks = 0;         // chunks handed to the sink (whole or dropped)
  uint32_t frames = 0;         // frames the DMA accepted
  uint32_t underruns = 0;      // engine-counted ring underruns seen during a pull
  uint32_t writeErrors = 0;    // non-timeout driver errors
  uint32_t stalls = 0;         // write timeouts (the DMA stayed full)
  uint32_t dropped = 0;        // chunks abandoned after kMaxWriteAttempts
};

// Frames audio_pull() actually took from the ring, from its running counter
// (which wraps). Clamped: the counter is shared with other readers in theory.
inline int32_t takenFrames(uint32_t before, uint32_t after, int32_t chunk) {
  const uint32_t d = after - before;
  return d > (uint32_t)chunk ? chunk : (int32_t)d;
}

// The writer does not rely on the producer to have cleared the tail.
inline void zeroTail(int16_t* st, int32_t valid, int32_t frames) {
  if (valid < 0) valid = 0;
  if (valid < frames) memset(st + (size_t)valid * 2, 0, (size_t)(frames - valid) * 2 * sizeof(int16_t));
}

// Hand `bytes` to the sink, resuming after partial writes. Returns true when
// everything was accepted.
template <class Write>
inline bool writeAll(const void* data, size_t bytes, Write&& write, Counters& c) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  size_t left = bytes;
  int attempts = 0;
  ++c.chunks;
  while (left > 0) {
    size_t w = 0;
    const WriteStatus st = write(p, left, &w);
    if (w > left) w = left;
    p += w;
    left -= w;
    c.frames += (uint32_t)(w / 4);
    if (left == 0) break;
    if (st == WriteStatus::Timeout) ++c.stalls;
    else if (st == WriteStatus::Error) ++c.writeErrors;
    else if (w > 0) continue;            // Ok with a partial count: keep going without penalty
    else ++c.stalls;                     // Ok but nothing accepted: do not spin
    if (++attempts >= kMaxWriteAttempts) { ++c.dropped; return false; }
  }
  return true;
}

// One writer iteration: pull, account, fade, write. `buf` holds kChunkFrames
// stereo frames.
template <class Audio, class Write>
inline bool pumpOnce(Audio& audio, Fade& fade, int16_t* buf, Write&& write, Counters& c, bool wired, bool muted) {
  Inputs in;
  in.playing = audio.playing();
  in.seeking = audio.seeking();
  in.trackSerial = audio.serial();
  in.muted = muted;
  in.wired = wired;
  const uint32_t framesBefore = audio.pulledFrames();
  const uint32_t underrunsBefore = audio.underruns();
  audio.pull(buf, kChunkFrames);
  const int32_t valid = takenFrames(framesBefore, audio.pulledFrames(), kChunkFrames);
  if (audio.underruns() != underrunsBefore && valid < kChunkFrames) ++c.underruns;
  zeroTail(buf, valid, kChunkFrames);
  fade.process(buf, kChunkFrames, valid, in);
  return writeAll(buf, kChunkBytes, write, c);
}

// Zeros through the stage. Used before the first sample (lead-in) and after the
// final fade (tail). `frames` is rounded up to whole chunks.
template <class Write>
inline bool writeZeros(int16_t* buf, int32_t frames, Write&& write, Counters& c) {
  memset(buf, 0, kChunkBytes);
  for (int32_t done = 0; done < frames; done += kChunkFrames)
    if (!writeAll(buf, kChunkBytes, write, c)) return false;
  return true;
}

// Leaving the route: let the held sample fade to digital zero, then a short
// run of zeros, so disabling the channel is silent. Never pulls from the ring.
// Gives up at the first dropped chunk: a stuck DMA must not stretch the teardown
// past speaker_setRoute()'s park timeout.
template <class Write>
inline bool finishStream(Fade& fade, int16_t* buf, Write&& write, Counters& c) {
  Inputs in;
  in.playing = false;
  in.wired = false;
  const int maxChunks = (int)(kRampFrames / kChunkFrames) + 2;
  for (int i = 0; i < maxChunks; ++i) {
    memset(buf, 0, kChunkBytes);
    fade.process(buf, kChunkFrames, 0, in);
    if (!writeAll(buf, kChunkBytes, write, c)) return false;
    if (fade.silent()) break;
  }
  return writeZeros(buf, kTailFrames, write, c);
}

}  // namespace cydwired
