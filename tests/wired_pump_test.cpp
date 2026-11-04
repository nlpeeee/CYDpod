// CY-71 host test for firmware/cydpod_player/app/wired_pump.h: the writer's chunk
// logic against a mock ring (audio_pull semantics) and a mock I2S sink.
#include "../firmware/cydpod_player/app/wired_pump.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <deque>
#include <vector>

using namespace cydwired;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Mirrors audio.cpp's audio_pull(): live frames only while playing and not
// seeking, silence for the rest, counters bumped the same way.
struct MockRing {
  bool isPlaying = false, isSeeking = false, starting = false;
  uint32_t trackSerial = 1, pulled = 0, under = 0;
  size_t have = 0;              // live frames waiting in the ring
  double phase = 0;
  bool playing() const { return isPlaying; }
  bool seeking() const { return isSeeking; }
  uint32_t serial() const { return trackSerial; }
  uint32_t pulledFrames() const { return pulled; }
  uint32_t underruns() const { return under; }
  void pull(int16_t* out, int32_t frames) {
    int32_t i = 0;
    if (isPlaying && !isSeeking) {
      const int32_t n = (int32_t)(have < (size_t)frames ? have : (size_t)frames);
      for (; i < n; ++i) {
        const int16_t v = (int16_t)lround(20000.0 * sin(phase));
        phase += 2.0 * 3.14159265358979323846 * 300.0 / 44100.0;
        out[2 * i] = v; out[2 * i + 1] = (int16_t)-v;
      }
      have -= (size_t)n;
      pulled += (uint32_t)n;
      if (n < frames && !starting) ++under;
    }
    memset(out + (size_t)i * 2, 0, (size_t)(frames - i) * 2 * sizeof(int16_t));
  }
};

struct MockSink {
  std::vector<int16_t> data;        // everything the "DMA" accepted
  std::deque<WriteStatus> script;   // statuses to return (front first); empty = Ok
  std::deque<size_t> accept;        // bytes to accept per call; empty = all
  int calls = 0;
  WriteStatus operator()(const uint8_t* p, size_t bytes, size_t* written) {
    ++calls;
    WriteStatus st = WriteStatus::Ok;
    if (!script.empty()) { st = script.front(); script.pop_front(); }
    size_t n = bytes;
    if (!accept.empty()) { n = accept.front() < bytes ? accept.front() : bytes; accept.pop_front(); }
    if (st == WriteStatus::Error) n = 0;
    if (st == WriteStatus::Timeout && accept.empty()) n = 0;
    for (size_t i = 0; i + 1 < n; i += 2) { int16_t v; memcpy(&v, p + i, 2); data.push_back(v); }
    *written = n;
    return st;
  }
};

static int maxStep(const std::vector<int16_t>& d) {
  int m = 0;
  for (size_t i = 2; i + 1 < d.size(); i += 2) { int s = abs((int)d[i] - (int)d[i - 2]); if (s > m) m = s; }
  return m;
}

int main() {
  // takenFrames: wraparound and clamping.
  CHECK(takenFrames(10, 266, 256) == 256, "taken full");
  CHECK(takenFrames(0xFFFFFFF0u, 0x00000010u, 256) == 32, "taken wraps");
  CHECK(takenFrames(5, 5, 256) == 0, "taken zero");
  CHECK(takenFrames(0, 100000, 256) == 256, "taken clamped");
  CHECK(takenFrames(7, 3, 256) == 256, "counter going backwards is clamped, not trusted");

  // zeroTail.
  {
    int16_t b[16]; for (int i = 0; i < 16; ++i) b[i] = 5;
    zeroTail(b, 3, 8);
    CHECK(b[5] == 5 && b[6] == 0 && b[15] == 0, "zeroTail");
    for (int i = 0; i < 16; ++i) b[i] = 5;
    zeroTail(b, -4, 8);
    CHECK(b[0] == 0 && b[15] == 0, "negative valid zeroes all");
    for (int i = 0; i < 16; ++i) b[i] = 5;
    zeroTail(b, 8, 8);
    CHECK(b[15] == 5, "full valid untouched");
  }

  // writeAll: partial writes resume, counters add up.
  {
    Counters c; MockSink s; s.accept = {100, 500, 4};
    int16_t buf[kChunkFrames * 2]; for (auto& v : buf) v = 1;
    const bool ok = writeAll(buf, kChunkBytes, s, c);
    CHECK(ok && s.data.size() == (size_t)kChunkFrames * 2, "partial writes complete: %zu", s.data.size());
    CHECK(c.frames == (uint32_t)kChunkFrames && c.stalls == 0 && c.writeErrors == 0 && c.dropped == 0 && c.chunks == 1, "partial counters");
    CHECK(s.calls == 4, "calls %d", s.calls);
  }
  // Timeouts then success: stalls counted, nothing dropped, data intact.
  {
    Counters c; MockSink s; s.script = {WriteStatus::Timeout, WriteStatus::Timeout};
    int16_t buf[kChunkFrames * 2]; for (int i = 0; i < kChunkFrames * 2; ++i) buf[i] = (int16_t)i;
    const bool ok = writeAll(buf, kChunkBytes, s, c);
    CHECK(ok && c.stalls == 2 && c.dropped == 0 && c.frames == (uint32_t)kChunkFrames, "two stalls then ok: stalls=%u", c.stalls);
    CHECK(s.data.size() == (size_t)kChunkFrames * 2 && s.data[511] == 511, "data intact");
  }
  // A stuck DMA is bounded: kMaxWriteAttempts, then drop.
  {
    Counters c; MockSink s; s.script = {WriteStatus::Timeout, WriteStatus::Timeout, WriteStatus::Timeout, WriteStatus::Timeout, WriteStatus::Timeout};
    int16_t buf[kChunkFrames * 2] = {};
    const bool ok = writeAll(buf, kChunkBytes, s, c);
    CHECK(!ok && s.calls == kMaxWriteAttempts && c.stalls == (uint32_t)kMaxWriteAttempts && c.dropped == 1 && c.frames == 0, "stuck DMA bounded: calls=%d", s.calls);
  }
  // Hard errors are counted separately; Ok-with-nothing does not spin.
  {
    Counters c; MockSink s; s.script = {WriteStatus::Error, WriteStatus::Error, WriteStatus::Error};
    int16_t buf[kChunkFrames * 2] = {};
    CHECK(!writeAll(buf, kChunkBytes, s, c) && c.writeErrors == 3 && c.dropped == 1, "errors counted");
    Counters c2; MockSink z; z.accept = {0, 0, 0, 0, 0};
    CHECK(!writeAll(buf, kChunkBytes, z, c2) && z.calls == kMaxWriteAttempts && c2.stalls == (uint32_t)kMaxWriteAttempts, "no spin on zero-byte Ok");
  }

  // pumpOnce: playing, paused, resumed, track change, seek, underrun -- compare the fade's guarantees on real pump output.
  {
    MockRing ring; Fade fade; Counters c; MockSink sink;
    int16_t buf[kChunkFrames * 2];
    auto run = [&](int n, size_t feed) { for (int i = 0; i < n; ++i) { ring.have += feed; pumpOnce(ring, fade, buf, sink, c, true, false); } };
    // Silence before play: nothing but zeros, no underrun counted (not playing).
    run(3, 0);
    CHECK(c.underruns == 0 && c.frames == 3u * kChunkFrames, "idle chunks written as zeros");
    for (int16_t v : sink.data) if (v) { CHECK(false, "idle output not silent"); break; }
    ring.isPlaying = true; ring.starting = true; run(2, 256);   // starting: the engine does not count underruns
    ring.starting = false; run(10, 256);
    CHECK(c.underruns == 0, "no underruns while fed");
    // Ring runs dry mid-chunk: engine counts it, we count it, output decays to digital zero.
    ring.have = 100; pumpOnce(ring, fade, buf, sink, c, true, false);
    CHECK(c.underruns == 1, "underrun counted once, got %u", c.underruns);
    run(2, 0);
    CHECK(c.underruns == 3, "each empty pull while playing is an underrun, got %u", c.underruns);
    CHECK(fade.silent(), "decayed to silent");
    for (int i = 0; i < kChunkFrames; ++i) CHECK(buf[2 * i] == 0 && buf[2 * i + 1] == 0, "underrun chunk is zeros at %d", i);
    // Recovery, pause, resume, seek, track change.
    run(4, 256);
    ring.isPlaying = false; run(3, 256); ring.isPlaying = true; run(4, 256);
    ring.isSeeking = true; run(3, 256); ring.isSeeking = false; run(4, 256);
    ring.trackSerial++; ring.phase += 2.0; run(5, 256);
    // The ring was fed while paused/seeking but nothing was consumed, so the stream stays continuous.
    const int step = maxStep(sink.data);
    const double natural = 20000.0 * 2 * 3.14159265358979 * 300.0 / 44100.0 * kCeilingQ15 / kUnityQ15;
    CHECK(step <= (int)natural + 60, "end-to-end step %d (natural %.0f)", step, natural);
    CHECK(c.writeErrors == 0 && c.stalls == 0 && c.dropped == 0, "clean sink");
    CHECK(c.frames == c.chunks * (uint32_t)kChunkFrames, "frames == chunks * 256");
  }

  // finishStream: from full level to digital zero, then tail zeros; never pulls.
  {
    MockRing ring; ring.isPlaying = true; ring.have = 100000; Fade fade; Counters c; MockSink sink;
    int16_t buf[kChunkFrames * 2];
    for (int i = 0; i < 6; ++i) pumpOnce(ring, fade, buf, sink, c, true, false);
    const uint32_t pulledBefore = ring.pulled;
    const size_t at = sink.data.size();
    finishStream(fade, buf, sink, c);
    CHECK(ring.pulled == pulledBefore, "finishStream must not pull");
    CHECK(fade.silent(), "silent after finish");
    const std::vector<int16_t> tail(sink.data.begin() + (long)at, sink.data.end());
    CHECK(maxStep(tail) <= 80, "final fade step %d", maxStep(tail));
    size_t lastNz = 0; for (size_t i = 0; i < tail.size(); ++i) if (tail[i]) lastNz = i;
    CHECK(lastNz / 2 < (size_t)kRampFrames + 1, "final fade ends within a ramp (+%zu frames)", lastNz / 2);
    CHECK(tail.size() / 2 >= (size_t)kTailFrames, "tail zeros written: %zu frames", tail.size() / 2);
    // The DMA holds kDmaCapacityFrames behind the write position, so everything before the
    // last kDmaCapacityFrames accepted frames is on the wire. The fade must be inside that part.
    CHECK(tail.size() / 2 > (size_t)kDmaCapacityFrames + lastNz / 2, "fade (ends +%zu) is not behind a full DMA of zeros (%zu frames written)", lastNz / 2, tail.size() / 2);
    // Already-silent stage: only the tail zeros.
    Counters c2; MockSink s2; Fade f2;
    finishStream(f2, buf, s2, c2);
    CHECK(c2.chunks == (uint32_t)(1 + (kTailFrames + kChunkFrames - 1) / kChunkFrames), "idle finish chunks %u", c2.chunks);
  }

  // A sink that errors on every call: pumpOnce reports the drop (the writer then backs off),
  // finishStream gives up at the first dropped chunk instead of grinding through the tail.
  {
    MockRing ring; ring.isPlaying = true; ring.have = 100000; Fade fade; Counters c; MockSink s;
    for (int i = 0; i < 40; ++i) s.script.push_back(WriteStatus::Error);
    int16_t buf[kChunkFrames * 2];
    CHECK(!pumpOnce(ring, fade, buf, s, c, true, false), "failed sink reported");
    CHECK(c.dropped == 1 && c.writeErrors == (uint32_t)kMaxWriteAttempts, "drop counted");
    Counters c2; MockSink s2; for (int i = 0; i < 40; ++i) s2.script.push_back(WriteStatus::Timeout);
    CHECK(!finishStream(fade, buf, s2, c2) && c2.dropped == 1 && s2.calls == kMaxWriteAttempts, "finishStream stops at the first dropped chunk (calls %d)", s2.calls);
  }

  // Lead-in.
  {
    Counters c; MockSink s; int16_t buf[kChunkFrames * 2];
    writeZeros(buf, kLeadInFrames, s, c);
    CHECK(c.frames == (uint32_t)kLeadInFrames && s.data.size() == (size_t)kLeadInFrames * 2, "lead-in length");
    CHECK(kLeadInFrames * 1000 / 44100 >= 50, "lead-in >= 50 ms");
  }

  if (failures) { printf("WIRED_PUMP_FAILED %d\n", failures); return 1; }
  printf("WIRED_PUMP_OK\n");
  return 0;
}
