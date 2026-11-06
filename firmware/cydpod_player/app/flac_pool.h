#pragma once
#include "wav_track.h"
#ifdef ARDUINO
#include "audio.h"
#include <esp_timer.h>
#endif
#if defined(CYDPOD_FLAC_LEGACY)
#include "miniflac.h" // declarations only; the implementation is flac_core.c
// Native FLAC, 16-bit mono/stereo, block size <= 4608, no heap. miniflac decodes
// one whole frame into the two int32 channel arrays below (the dominant pool
// cost); metadata is walked with file seeks and never cached. Every frame
// header is validated before miniflac is allowed to write samples.
struct FlacState {
  static constexpr unsigned maxBlock = 4608, chunk = 2048, entryBytes = 18;
  static constexpr uint32_t seekBudget = 512u * 1024; // hard cap on bytes read per seek
  static constexpr uint32_t probeScan = 20u * 1024;   // sync search per bisect step
  static constexpr uint32_t resyncScan = 64u * 1024;  // sync search after a bad frame
  static constexpr uint64_t unknown = UINT64_MAX;
  enum class Seek : uint8_t { ok, cancelled, error };
  // miniflac trusts the residual partition arithmetic of an unverified frame (CRC is checked at its end): a hostile
  // partition order makes it write past block_size. Feeding it feedStep bytes at a time bounds one call's overshoot to
  // feedStep*8 samples, which `slack` absorbs; attempt() then rejects the frame as soon as residual_total is impossible.
  static constexpr unsigned slack = 72, feedStep = 8;
  int32_t rightTail[maxBlock + slack - 1152]; // must stay the first member: continues the second channel after the pool pcm area
  miniflac_t dec;
  int32_t block[maxBlock + slack]; // left / mid channel
  int32_t* right;            // second channel: pool pcm (first 1152 entries) then rightTail, contiguous (see codec_track.cpp)
  uint64_t total, nextSample, curStart;
  uint32_t first, end, bufAt, tab, tabBytes, rate, fixedBlock, spent;
  uint16_t blockSize, used, inPos, inLen;
  uint8_t channels, skipped, drops;
  bool io, coarse;

  static bool readAt(File& f, uint32_t at, uint8_t* out, uint32_t n) {
#if defined(ARDUINO) && !defined(CYDPOD_LIGHT_AUDIO_DIAGNOSTICS)
    const int64_t start = esp_timer_get_time();
#endif
    const bool ok = WavState::readAt(f, at, out, n);
#if defined(ARDUINO) && !defined(CYDPOD_LIGHT_AUDIO_DIAGNOSTICS)
    audio_flacReadTime(uint32_t(esp_timer_get_time() - start));
#endif
    return ok;
  }
  static uint32_t be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }
  static uint32_t be24(const uint8_t* p) { return be16(p) << 8 | p[2]; }
  static uint32_t be32(const uint8_t* p) { return be16(p) << 16 | be16(p + 2); }
  static uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
  static int16_t clip(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : int16_t(v); }

  void arm() { miniflac_reset(&dec, MINIFLAC_FRAME); } // keeps STREAMINFO rate/bps; fresh frame state
  uint64_t sampleOf(const miniflac_frame_header_t& h) const {
    if (h.blocking_strategy) return h.sample_number;
    return fixedBlock ? h.sample_number * fixedBlock : unknown; // fixed strategy codes a frame number
  }
  const char* unsupported(const miniflac_frame_header_t& h) const {
    if (h.block_size > maxBlock) return "flac_block_size";
    if (h.bps != 16) return "flac_bits";
    if (h.channels != channels) return "flac_channels";
    if (h.sample_rate != rate) return "flac_rate_change";
    return nullptr;
  }
  const char* open(File& f, uint8_t* in) {
    end = f.size(); uint8_t h[4];
    if (end < 42 || !readAt(f, 0, h, 4) || memcmp(h, "fLaC", 4)) return "flac_header";
    uint32_t at = 4; bool info = false, last = false; unsigned blocks = 0;
    while (!last) {
      if (++blocks > 1024 || end - at < 4 || !readAt(f, at, h, 4)) return "flac_metadata";
      last = h[0] >> 7; const unsigned type = h[0] & 127; const uint32_t len = be24(h + 1), body = at + 4;
      if (type == 127 || len > end - body) return "flac_metadata";
      if (!info) {
        if (type != 0 || len != 34 || !readAt(f, body, in, 34)) return "flac_streaminfo";
        const uint32_t minB = be16(in), maxB = be16(in + 2);
        rate = uint32_t(in[10]) << 12 | uint32_t(in[11]) << 4 | in[12] >> 4;
        channels = ((in[12] >> 1) & 7) + 1;
        const unsigned bps = ((in[12] & 1) << 4 | in[13] >> 4) + 1;
        total = uint64_t(in[13] & 15) << 32 | be32(in + 14);
        if (!rate) return "flac_streaminfo";
        if (bps != 16) return "flac_bits";            // 24-bit (and 8/12/20) rejected
        if (channels > 2) return "flac_channels";
        if (minB < 16 || maxB < minB) return "flac_streaminfo";
        if (maxB > maxBlock) return "flac_block_size"; // would not fit the pool
        fixedBlock = minB == maxB ? maxB : 0;
        info = true;
      } else if (type == 0) return "flac_streaminfo";
      else if (type == 3 && !tab) { tab = body; tabBytes = len / entryBytes * entryBytes; }
      at = body + len; // tags, PICTURE, padding, cuesheet: skipped by offset only
    }
    if (!info || end - at < 6) return "flac_no_audio";
    first = at; bufAt = at; inPos = inLen = 0; nextSample = 0;
    miniflac_init(&dec, MINIFLAC_CONTAINER_NATIVE);
    dec.metadata.streaminfo.sample_rate = rate; dec.metadata.streaminfo.bps = 16;
    arm();
    if (!fixedBlock) { // min != max in STREAMINFO: a fixed-strategy stream still has one block size, taken from the first frame
      uint64_t s = 0;
      if (headerAt(f, first, s) && !dec.frame.header.blocking_strategy) fixedBlock = dec.frame.header.block_size;
      arm(); spent = 0;
    }
    return nullptr;
  }
  bool fill(File& f, uint8_t* in) {
    const uint32_t at = bufAt + inPos;
    if (at >= end) return false;
    uint32_t n = end - at; if (n > chunk) n = chunk;
    if (!readAt(f, at, in, n)) { io = true; return false; }
    spent += n; bufAt = at; inPos = 0; inLen = uint16_t(n);
    return true;
  }
  // No retained callback/state: the audio command snapshot exposes the same
  // pending track/seek condition as audio.cpp's private requestPending().
  static bool cooperate(int (*hook)(void*), void* user) {
    if (hook && hook(user)) return false;
#ifdef ARDUINO
    const auto command = audio_commands();
    if (command.track >= 0 || command.seekMs >= 0) return false;
    taskYIELD(); // no tick delay added to the valid-frame runway
#endif
    return true;
  }
  // 1 decoded, 0 truncated, -1 corrupt, -2 fatal, -3 command cancellation.
  int attempt(File& f, uint8_t* in, const char*& why, bool resyncing, int (*hook)(void*) = nullptr, void* user = nullptr) {
    arm(); bool header = false; int32_t* out[2] = {block, right};
    for (;;) {
      if (inPos >= inLen && !fill(f, in)) { if (io) { why = "flac_read"; return -2; } return 0; }
      const unsigned channel = dec.frame.cur_subframe;
      uint32_t used32 = 0;
      const uint32_t len = header && inLen - inPos > feedStep ? feedStep : inLen - inPos;
      const MINIFLAC_RESULT r = header ? miniflac_decode(&dec, in + inPos, len, &used32, out)
                                       : miniflac_sync(&dec, in + inPos, len, &used32);
      inPos += uint16_t(used32);
      if (header && dec.frame.subframe.residual.residual_total > dec.frame.header.block_size) { why = "flac_frame"; return -1; }
      // Eight-byte guarded feeds already resume miniflac in place. At a
      // channel boundary LPC prediction is complete, so yield without a new
      // decoder state machine or publishing PCM before the frame CRC passes.
      if (header && channel != dec.frame.cur_subframe && !cooperate(hook, user)) return -3;
      if (r == MINIFLAC_CONTINUE) continue;
      if (r != MINIFLAC_OK) { why = r == MINIFLAC_FRAME_CRC16_INVALID || r == MINIFLAC_FRAME_CRC8_INVALID ? "flac_crc" : "flac_frame"; return -1; }
      if (header) return 1;
      if (const char* bad = unsupported(dec.frame.header)) { why = bad; return resyncing ? -1 : -2; }
      header = true; dec.frame.subframe.residual.residual_total = 0; // no stale total from the previous frame
    }
  }
  // Finds the next 0xFF 0xF8/0xF9 at or after `from`; 1 found, 0 end of file, -1 none within the cap.
  int resync(File& f, uint8_t* in, uint32_t from, int (*hook)(void*) = nullptr, void* user = nullptr) {
    bufAt = from; inPos = inLen = 0; uint32_t scanned = 0; int prev = -1;
    while (scanned < resyncScan) {
      if (!cooperate(hook, user)) return -2; // bounded to one existing 2048 B fill
      if (inPos >= inLen && !fill(f, in)) return io ? -1 : 0;
      while (inPos < inLen && scanned < resyncScan) {
        const uint8_t c = in[inPos];
        if (prev == 0xFF && (c & 0xFE) == 0xF8) { bufAt = bufAt + inPos - 1; inPos = inLen = 0; return 1; }
        prev = c; ++inPos; ++scanned;
      }
    }
    return -1;
  }
  // 1 frame, 0 EOF, -1 error (err set), -3 cancelled. Bad frames are skipped, boundedly.
  int frame(File& f, uint8_t* in, const char*& err, int (*hook)(void*) = nullptr, void* user = nullptr) {
    bool resyncing = false;
    for (;;) {
      if ((total && nextSample >= total) || bufAt + inPos >= end) return 0;
      const uint32_t start = bufAt + inPos; const char* why = "flac_frame";
      if (resyncing && !cooperate(hook, user)) return -3;
      const int r = attempt(f, in, why, resyncing, hook, user);
      if (r == -3) return -3;
      if (r == 1) {
        const uint64_t s = sampleOf(dec.frame.header);
        blockSize = uint16_t(dec.frame.header.block_size); used = 0; skipped = 0;
        curStart = s == unknown ? nextSample : s; nextSample = curStart + blockSize;
        return 1;
      }
      if (r == 0) { err = "flac_truncated"; return -1; }
      if (r == -2) { err = why; return -1; }
      if (++skipped > 8 || ++drops > 64) { err = why; return -1; }
      const int s = resync(f, in, start + 1, hook, user);
      if (s == -2) return -3;
      if (s == 0) return 0;      // damaged tail with no further frame: stop cleanly
      if (s < 0) { err = why; return -1; }
      resyncing = true;
    }
  }
  int read(File& f, uint8_t* in, int16_t* pcm, const char*& err) {
    if (used >= blockSize) { const int r = frame(f, in, err); if (r <= 0) return r; }
    uint32_t n = blockSize - used; if (n > 1152) n = 1152;
    if (total) {
      const uint64_t at = curStart + used;
      if (at >= total) return 0;
      if (total - at < n) n = uint32_t(total - at);
    }
    // `pcm` overlays the first 1152 entries of `right`. Frame i of this call lands on right[i], which is read first
    // (memcpy keeps the load ordered before the int16 stores) and is already consumed whenever used > 0.
    for (uint32_t i = 0; i < n; ++i) {
      const int16_t l = clip(block[used + i]);
      int16_t r = l;
      if (channels == 2) { int32_t v; memcpy(&v, &right[used + i], sizeof(v)); r = clip(v); }
      pcm[2 * i] = l;
      pcm[2 * i + 1] = r;
    }
    used += uint16_t(n);
    return int(n);
  }
  // Frame header at an absolute offset (CRC-8 checked, fields plausible); sample may be `unknown`.
  bool headerAt(File& f, uint32_t pos, uint64_t& sample) {
    uint8_t h[16]; const uint32_t n = end - pos < sizeof(h) ? end - pos : sizeof(h);
    if (n < 6 || !readAt(f, pos, h, n)) return false;
    spent += n; arm(); uint32_t used32 = 0;
    if (miniflac_sync(&dec, h, n, &used32) != MINIFLAC_OK || unsupported(dec.frame.header)) return false;
    sample = sampleOf(dec.frame.header);
    return sample == unknown || !total || sample < total;
  }
  bool probe(File& f, uint8_t* in, uint32_t from, uint32_t limit, uint32_t& pos, uint64_t& sample) {
    uint32_t at = from, scanned = 0;
    while (at < limit && scanned < probeScan) {
      uint32_t n = limit - at; if (n > chunk) n = chunk;
      if (n < 2 || !readAt(f, at, in, n)) return false;
      spent += n; scanned += n;
      for (uint32_t i = 0; i + 1 < n; ++i)
        if (in[i] == 0xFF && (in[i + 1] & 0xFE) == 0xF8 && headerAt(f, at + i, sample)) { pos = at + i; return true; }
      at += n - 1; // keep a possible sync split across chunks
    }
    return false;
  }
  // Exact for SEEKTABLE or sample-numbered streams: table narrows the byte range, header-CRC bisect finds the frame,
  // then at most a couple of frames are decoded and the remainder of the target frame is skipped in place.
  Seek seek(File& f, uint8_t* in, uint32_t ms, int (*hook)(void*), void* user, const char*& err) {
    spent = 0; skipped = drops = 0; blockSize = used = 0; coarse = false; io = false;
    uint64_t target = uint64_t(ms) * rate / 1000;
    if (total && target >= total) target = total - 1;
    uint32_t lo = first, hi = end; uint64_t loS = 0;
    if (tabBytes) {
      const uint32_t n = tabBytes / entryBytes; uint32_t a = 0, b = n; uint8_t e[16];
      auto entry = [&](uint32_t i, uint64_t& s, uint64_t& o) {
        spent += sizeof(e); if (!readAt(f, tab + i * entryBytes, e, sizeof(e))) return false;
        s = be64(e); o = be64(e + 8); return true; };
      uint64_t s = 0, o = 0; bool ok = true;
      while (ok && a < b) {
        const uint32_t m = a + (b - a) / 2;
        if (!(ok = entry(m, s, o))) break;
        if (s != unknown && s <= target) a = m + 1; else b = m;
      }
      if (ok && a > 0 && entry(a - 1, s, o) && o < end - first) {
        uint64_t s2 = 0, o2 = 0; uint64_t check = 0;
        const uint32_t cand = first + uint32_t(o);
        if (headerAt(f, cand, check) && check == s) {
          lo = cand; loS = s;
          if (a < n && entry(a, s2, o2) && s2 != unknown && o2 < end - first && first + o2 > lo) hi = first + uint32_t(o2);
        }
      }
    }
    for (unsigned it = 0; hi - lo > 64 && it < 40; ++it) {
      if (hook && hook(user)) return Seek::cancelled;
      if (spent > seekBudget) { coarse = true; break; }
      const uint32_t mid = lo + (hi - lo) / 2; uint32_t p = 0; uint64_t s = 0;
      if (!probe(f, in, mid, hi, p, s)) { hi = mid; continue; }
      if (s == unknown) { lo = p; loS = target; coarse = true; break; } // no sample numbers: proportional guess only
      if (s <= target) { lo = p; loS = s; } else hi = p;
    }
    bufAt = lo; inPos = inLen = 0; nextSample = loS;
    for (;;) {
      if (hook && hook(user)) return Seek::cancelled;
      const int r = frame(f, in, err, hook, user);
      if (r == -3) return Seek::cancelled;
      if (r < 0) return Seek::error;
      if (r == 0) { blockSize = used = 0; return Seek::ok; }
      if (curStart + blockSize > target) { used = uint16_t(target > curStart ? target - curStart : 0); return Seek::ok; }
      if (spent > seekBudget) { coarse = true; used = blockSize; return Seek::ok; }
    }
  }
};

#else
#include "flac_compact.h"
// One CRC-gated interleaved block in the codec union, caller-owned input and
// shared 1152-frame slice sink. No heap or residual/channel arrays.
struct FlacState {
  static constexpr unsigned maxBlock = 4096, chunk = 2048, entryBytes = 18;
  static constexpr uint32_t seekBudget = 512u * 1024, probeScan = 20u * 1024;
  static constexpr uint64_t unknown = UINT64_MAX;
  enum class Seek : uint8_t { ok, cancelled, error };
  flac_compact_state dec;
  int16_t block[FLAC_COMPACT_PCM_SAMPLES];
  uint64_t total, nextSample, curStart;
  uint32_t first, end, tab, tabBytes, rate, spent;
  uint16_t blockSize, used;
  uint8_t channels;
  bool coarse;
  struct Context { FlacState* state; File* file; int (*hook)(void*); void* user; };
  static bool readAt(File& f, uint32_t at, uint8_t* out, uint32_t n) {
#if defined(ARDUINO) && !defined(CYDPOD_LIGHT_AUDIO_DIAGNOSTICS)
    const int64_t start = esp_timer_get_time();
#endif
    const bool ok = WavState::readAt(f, at, out, n);
#if defined(ARDUINO) && !defined(CYDPOD_LIGHT_AUDIO_DIAGNOSTICS)
    audio_flacReadTime(uint32_t(esp_timer_get_time() - start));
#endif
    return ok;
  }
  static uint32_t be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }
  static uint32_t be24(const uint8_t* p) { return be16(p) << 8 | p[2]; }
  static uint32_t be32(const uint8_t* p) { return be16(p) << 16 | be16(p + 2); }
  static uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
  // No retained callback/state: the audio command snapshot exposes the same
  // pending track/seek condition as audio.cpp's private requestPending().
  static bool cooperate(int (*hook)(void*), void* user) {
    if (hook && hook(user)) return false;
#ifdef ARDUINO
    const auto command = audio_commands();
    if (command.track >= 0 || command.seekMs >= 0) return false;
    taskYIELD(); // no tick delay added to the valid-frame runway
#endif
    return true;
  }

  static int readCompact(void* opaque, uint64_t at, uint8_t* dst, size_t n) {
    auto& c = *static_cast<Context*>(opaque);
    if (at >= c.state->end) return 0;
    if (n > c.state->end - at) n = size_t(c.state->end - at);
    c.state->spent += uint32_t(n);
    return readAt(*c.file, uint32_t(at), dst, uint32_t(n)) ? int(n) : -1;
  }
  static int yieldCompact(void* opaque) {
    auto& c = *static_cast<Context*>(opaque);
    return !cooperate(c.hook, c.user);
  }
  const char* error(int r) const {
#ifdef ARDUINO
    if (r == FLACX_BLOCK_SIZE)
      Serial.printf("flac: reject block=%u max=4096\n", unsigned(dec.book.v.rejected_block));
#endif
    return flac_compact_error(r);
  }
  const char* open(File& f, uint8_t* in) {
    end = f.size(); Context c{this, &f, nullptr, nullptr};
    if (end < 42) return "flac_header";
    const int r = flac_compact_init(&dec, readCompact, &c, in);
    if (r < 0) return error(r);
    const auto& b = dec.book.v;
    first = uint32_t(b.first); total = b.total; rate = b.rate; channels = b.channels;
    tab = uint32_t(b.seek_table); tabBytes = b.seek_bytes;
    if (end - first < 6) return "flac_no_audio";
    if (!b.fixed_block) { uint64_t s; headerAt(f, first, s); }
    flac_compact_reset(&dec, first, 0); spent = 0;
    return nullptr;
  }
  int frame(File& f, uint8_t*, const char*& err, int (*hook)(void*) = nullptr, void* user = nullptr) {
    Context c{this, &f, hook, user};
    flac_compact_set_cooperate(&dec, yieldCompact, &c);
    const int r = flac_compact_next(&dec, readCompact, &c, block);
    flac_compact_set_cooperate(&dec, nullptr, nullptr); // never retain stack context
    if (r == FLACX_CANCELLED) return -3;
    if (r < 0) { err = error(r); return -1; }
    if (r == FLACX_FRAME) {
      blockSize = dec.book.v.block_size; used = 0;
      curStart = dec.book.v.frame_sample; nextSample = dec.book.v.next_sample;
    }
    return r;
  }
  int read(File& f, uint8_t* in, int16_t* pcm, const char*& err) {
    if (used >= blockSize) { const int r = frame(f, in, err); if (r <= 0) return r; }
    dec.book.v.used = used;
    const unsigned n = flac_compact_slice(&dec, block, pcm, 1152);
    used = dec.book.v.used;
    return int(n);
  }
  bool headerAt(File& f, uint32_t pos, uint64_t& sample) {
    Context c{this, &f, nullptr, nullptr};
    if (pos >= end || flac_compact_header_at(&dec, readCompact, &c, pos) != FLACX_FRAME) return false;
    sample = dec.book.v.frame_sample;
    return !total || sample < total;
  }
  int resync(File& f, uint8_t*, uint32_t from, int (*hook)(void*) = nullptr, void* user = nullptr) {
    Context c{this, &f, hook, user};
    flac_compact_set_cooperate(&dec, yieldCompact, &c);
    const int r = flac_compact_resync(&dec, readCompact, &c, from);
    flac_compact_set_cooperate(&dec, nullptr, nullptr);
    return r == FLACX_CANCELLED ? -2 : r < 0 ? -1 : r;
  }
  bool probe(File& f, uint8_t* in, uint32_t from, uint32_t limit, uint32_t& pos, uint64_t& sample) {
    uint32_t at = from, scanned = 0;
    while (at < limit && scanned < probeScan) {
      uint32_t n = limit - at; if (n > chunk) n = chunk;
      if (n < 2 || !readAt(f, at, in, n)) return false;
      spent += n; scanned += n;
      for (uint32_t i = 0; i + 1 < n; ++i)
        if (in[i] == 0xFF && (in[i + 1] & 0xFE) == 0xF8 && headerAt(f, at + i, sample)) { pos = at + i; return true; }
      at += n - 1; // keep a possible sync split across chunks
    }
    return false;
  }
  // Exact for SEEKTABLE or sample-numbered streams: table narrows the byte range, header-CRC bisect finds the frame,
  // then at most a couple of frames are decoded and the remainder of the target frame is skipped in place.
  Seek seek(File& f, uint8_t* in, uint32_t ms, int (*hook)(void*), void* user, const char*& err) {
    spent = 0; blockSize = used = 0; coarse = false;
    uint64_t target = uint64_t(ms) * rate / 1000;
    if (total && target >= total) target = total - 1;
    uint32_t lo = first, hi = end; uint64_t loS = 0;
    if (tabBytes) {
      const uint32_t n = tabBytes / entryBytes; uint32_t a = 0, b = n; uint8_t e[16];
      auto entry = [&](uint32_t i, uint64_t& s, uint64_t& o) {
        spent += sizeof(e); if (!readAt(f, tab + i * entryBytes, e, sizeof(e))) return false;
        s = be64(e); o = be64(e + 8); return true; };
      uint64_t s = 0, o = 0; bool ok = true;
      while (ok && a < b) {
        const uint32_t m = a + (b - a) / 2;
        if (!(ok = entry(m, s, o))) break;
        if (s != unknown && s <= target) a = m + 1; else b = m;
      }
      if (ok && a > 0 && entry(a - 1, s, o) && o < end - first) {
        uint64_t s2 = 0, o2 = 0; uint64_t check = 0;
        const uint32_t cand = first + uint32_t(o);
        if (headerAt(f, cand, check) && check == s) {
          lo = cand; loS = s;
          if (a < n && entry(a, s2, o2) && s2 != unknown && o2 < end - first && first + o2 > lo) hi = first + uint32_t(o2);
        }
      }
    }
    for (unsigned it = 0; hi - lo > 64 && it < 40; ++it) {
      if (hook && hook(user)) return Seek::cancelled;
      if (spent > seekBudget) { coarse = true; break; }
      const uint32_t mid = lo + (hi - lo) / 2; uint32_t p = 0; uint64_t s = 0;
      if (!probe(f, in, mid, hi, p, s)) { hi = mid; continue; }
      if (s == unknown) { lo = p; loS = target; coarse = true; break; } // no sample numbers: proportional guess only
      if (s <= target) { lo = p; loS = s; } else hi = p;
    }
    flac_compact_reset(&dec, lo, loS);
    for (;;) {
      if (hook && hook(user)) return Seek::cancelled;
      const int r = frame(f, in, err, hook, user);
      if (r == -3) return Seek::cancelled;
      if (r < 0) return Seek::error;
      if (r == 0) { blockSize = used = 0; return Seek::ok; }
      if (curStart + blockSize > target) { used = uint16_t(target > curStart ? target - curStart : 0); return Seek::ok; }
      if (spent > seekBudget) { coarse = true; used = blockSize; return Seek::ok; }
    }
  }
};

#endif
