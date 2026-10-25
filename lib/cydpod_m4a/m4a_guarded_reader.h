// CYDpod-side wrapper around the unmodified AudioTools M4AAudioDemuxer: a streaming box-header guard enforces the
// caps in m4a_caps.h on the raw bytes BEFORE they reach the reader, so the reader's resizes/asserts never see
// out-of-cap values. Failures come back as M4aStatus; nothing here asserts or aborts.
#pragma once
#include <functional>
#include "AudioTools/AudioCodecs/M4AAudioDemuxer.h"
#include "m4a_caps.h"

namespace cydpod {
enum class M4aStatus : uint8_t {
  ok, badBoxHeader, tooDeep, stsdTooLarge, badStsd, notMp4a, badStsz, tooManySamples, badFrameSize,
  moovAfterMdat, duplicateTrack, truncated, noSampleTable, tableOverflow, tooManyBoxes,
  firstTrackNotAudio, notAacLc, badSampleRate, badChannels, noAudioTrack
};
inline const char* m4aStatusName(M4aStatus s) {
  static const char* n[] = {"ok", "bad_box_header", "too_deep", "stsd_too_large", "bad_stsd", "not_mp4a", "bad_stsz",
                            "too_many_samples", "bad_frame_size", "moov_after_mdat", "duplicate_track", "truncated",
                            "no_sample_table", "table_overflow", "too_many_boxes", "first_track_not_audio", "not_aac_lc",
                            "bad_sample_rate", "bad_channels", "no_audio_track"};
  return n[static_cast<uint8_t>(s)];
}

// Backstop for the reader's sample-size table: refuses to grow past the cap and swallows further writes (the reader
// wraps write() in assert) after latching the overflow.
class M4aCappedSizes : public audio_tools::SingleBuffer<audio_tools::stsz_sample_size_t> {
 public:
  bool overflow = false;
  bool resize(size_t n) override {
    if (n > kM4aMaxTableEntries) { overflow = true; return false; }
    return SingleBuffer::resize(n);
  }
  bool write(audio_tools::stsz_sample_size_t v) override { return overflow ? true : SingleBuffer::write(v); }
};

class M4aGuardedReader {
 public:
  using AdtsCallback = std::function<void(const uint8_t* adts, size_t size)>;
  M4aCappedSizes& sizeTable() { return sizes; }
  audio_tools::M4AAudioDemuxer& reader() { return demux; }  // test access only

  // A reader instance serves one track; construct a new one (or destroy) per track.
  void begin(AdtsCallback cb) {
    onFrame = cb; st = M4aStatus::ok; sc = Scan(); nz = 0; heldLen = 0;
    demux.setSampleSizesBuffer(sizes);
    demux.setReference(this);  // setReference must precede setCallback
    demux.setCallback([](const audio_tools::M4ACommonDemuxer::Frame& f, void* r) {
      auto* self = static_cast<M4aGuardedReader*>(r);
      if (self->onFrame) self->onFrame(f.data, f.size);
    });
    demux.begin();
  }
  M4aStatus status() const { return st; }
  uint64_t offset() const { return sc.pos; }  // stream offset the guard had reached (for error reports)

  // The reader misbehaves when the start of a box is split across write() calls (stsd is read from stale bytes, stsz header
  // words become table entries -> assert, the mdat size is never set), and it re-syncs on non-alphanumeric box types
  // (iTunes ilst atoms) by scanning payload bytes for look-alike boxes. So the guard (1) holds every incomplete header and
  // delivers stsd, stsz and the mdat start in one reader write ("zones"; at most kM4aMaxStsdBytes + 16 bytes held), and
  // (2) renames every box the player does not need (udta, meta, edts, dinf, non-alphanumeric types ...) to "free" so the
  // reader skips it by size and never descends into attacker-controlled metadata.
  M4aStatus write(const uint8_t* d, size_t n) {
    if (st != M4aStatus::ok) return st;
    nz = 0;
    if (sc.zoneOpen) zones[nz++] = {0, 0, true, false};
    if (!scan(d, n)) return st;
    size_t c = 0;
    for (unsigned k = 0; k < nz; ++k) {
      Zone& z = zones[k];
      size_t e = z.end ? z.end : n;
      if (!z.continuing) { if (z.start > c) demux.write(d + c, z.start - c); heldLen = 0; }
      memcpy(held + heldLen, d + (z.continuing ? 0 : z.start), e - (z.continuing ? 0 : z.start));
      heldLen += e - (z.continuing ? 0 : z.start);
      if (!z.end) { c = n; break; }
      if (z.rewrite) memcpy(held + 4, "free", 4);
      demux.write(held, heldLen); heldLen = 0; c = e;
    }
    if (c < n) demux.write(d + c, n - c);
    if (sizes.overflow) st = M4aStatus::tableOverflow;
    return st;
  }
  M4aStatus finish() {
    if (st != M4aStatus::ok) return st;
    popLevels();
    bool mdatOpen = sc.mode == Mode::skip && sc.isMdat && !sc.depth;  // payload may legitimately end early
    if (!mdatOpen && (sc.mode != Mode::header || sc.hdrLen || sc.depth)) st = M4aStatus::truncated;
    else if (!sc.stszSeen) st = sc.moovDone && !sc.stsdSeen ? M4aStatus::noAudioTrack : M4aStatus::noSampleTable;
    return st;
  }

 private:
  enum class Mode : uint8_t { header, skip, stsd, stsz };
  struct Scan {
    Mode mode = Mode::header;
    uint8_t hdr[16]; unsigned hdrLen = 0, depth = 0;
    uint64_t pos = 0, ends[kM4aMaxBoxDepth], remaining = 0, payload = 0;
    uint32_t word = 0;
    uint64_t zoneEnd = 0;
    bool zoneOpen = false, mdatSeen = false, isMdat = false, openedMoov = false, moovDone = false, stsdSeen = false, stszSeen = false;
    uint8_t stsd[kM4aMaxStsdBytes];
  };
  struct Zone { size_t start, end; bool continuing, rewrite; };
  static constexpr unsigned kZones = 12;  // stsd, stsz, mdat, one pending header, plus neutralized boxes
  Zone zones[kZones]; unsigned nz = 0;
  uint8_t held[kM4aMaxStsdBytes + 16]; size_t heldLen = 0;
  audio_tools::M4AAudioDemuxer demux;
  M4aCappedSizes sizes;
  AdtsCallback onFrame;
  Scan sc;
  M4aStatus st = M4aStatus::ok;

  static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
  bool fail(M4aStatus s) { st = s; return false; }
  static bool is(const uint8_t* p, const char* t) { return !memcmp(p, t, 4); }
  static bool isKeepContainer(const uint8_t* t) {
    static const char* c[] = {"moov", "trak", "mdia", "minf", "stbl"};  // the path to stsd/stsz
    for (const char* x : c) if (is(t, x)) return true;
    return false;
  }
  static bool isKnownContainer(const uint8_t* t) {
    static const char* c[] = {"edts", "dinf", "udta", "ilst", "moof", "traf", "mfra", "tref", "iprp", "sinf", "schi", "meta"};
    for (const char* x : c) if (is(t, x)) return true;
    return false;
  }
  void popLevels() {
    while (sc.depth && sc.pos >= sc.ends[sc.depth - 1]) {
      if (--sc.depth == 0 && sc.openedMoov) sc.moovDone = true;
    }
  }

  bool onHeader(size_t idx) {
    const uint8_t* h = sc.hdr; unsigned hl = sc.hdrLen;
    uint32_t s32 = u32(h);
    if (s32 == 1 && hl < 16) return true;  // wait for largesize
    uint64_t size = s32, hsz = 8; bool open = false;
    if (s32 == 1) { size = (uint64_t)u32(h + 8) << 32 | u32(h + 12); hsz = 16; }
    else if (s32 == 0) { open = true; }
    bool mdat = is(h + 4, "mdat");
    if ((open || hsz == 16) && !(mdat && sc.depth == 0)) return fail(M4aStatus::badBoxHeader);
    if (!open && size < hsz) return fail(M4aStatus::badBoxHeader);
    uint64_t start = sc.pos - hl, end = open ? UINT64_MAX : start + size;
    if (sc.depth && end > sc.ends[sc.depth - 1]) return fail(M4aStatus::badBoxHeader);
    sc.hdrLen = 0;
    bool alnum = isalnum(h[4]) && isalnum(h[5]) && isalnum(h[6]) && isalnum(h[7]);
    uint64_t need = hl;  // bytes from the header start that must reach the reader in one write
    bool rewrite = false;
    if (alnum && isKeepContainer(h + 4)) {
      if (sc.depth >= kM4aMaxBoxDepth) return fail(M4aStatus::tooDeep);
      if (sc.depth == 0 && is(h + 4, "moov")) sc.openedMoov = true;
      sc.ends[sc.depth++] = end;
      return holdHeader(idx, hl, start, need, false);
    }
    sc.payload = sc.remaining = open ? UINT64_MAX : size - hsz;
    sc.isMdat = mdat; sc.mode = Mode::skip;
    if (!alnum || isKnownContainer(h + 4)) {
      rewrite = true;  // neutralized: the reader sees a "free" box of the same size
    } else if (is(h + 4, "stsd")) {
      if (sc.payload > kM4aMaxStsdBytes) return fail(M4aStatus::stsdTooLarge);
      if (sc.payload < 16) return fail(M4aStatus::badStsd);  // version/flags, count, first entry header; the rest is checked in onStsdDone
      if (sc.stsdSeen) return fail(M4aStatus::duplicateTrack);
      sc.stsdSeen = true; sc.mode = Mode::stsd; need += sc.payload;
    } else if (is(h + 4, "stsz")) {
      if (sc.payload < 12) return fail(M4aStatus::badStsz);
      if (sc.stszSeen) return fail(M4aStatus::duplicateTrack);
      sc.stszSeen = true; sc.mode = Mode::stsz; need += 13;  // header words + first entry
    } else if (mdat) {
      if (sc.depth == 0 && !sc.moovDone) return fail(M4aStatus::moovAfterMdat);  // the reader needs the sample tables first; moov-last is not supported
      if (sc.depth == 0 && !sc.stsdSeen) return fail(M4aStatus::noAudioTrack);
      if (sc.depth == 0 && !sc.stszSeen) return fail(M4aStatus::noSampleTable);
      if (sc.mdatSeen) return fail(M4aStatus::duplicateTrack);
      sc.mdatSeen = true; need += sc.payload ? 1 : 0;  // the first payload byte carries the size to the reader
    }
    if (!sc.remaining) sc.mode = Mode::header;
    return holdHeader(idx, hl, start, need, rewrite);
  }

  // Registers (or finalizes the pending-header zone of) a box whose first `need` bytes must be delivered together.
  bool holdHeader(size_t idx, unsigned hl, uint64_t start, uint64_t need, bool rewrite) {
    if (sc.zoneOpen) { sc.zoneEnd = start + need; zones[nz - 1].rewrite = rewrite; return true; }
    if (need == hl && !rewrite) return true;  // plain header inside this write: nothing to hold
    if (nz >= kZones) return fail(M4aStatus::tooManyBoxes);
    zones[nz++] = {idx + 1 - hl, 0, false, rewrite}; sc.zoneOpen = true; sc.zoneEnd = start + need;
    return true;
  }

  // The reader walks the stsd entry with parseString(), which advances by (size - 8) and can loop forever or invoke
  // alac/esds handlers on garbage; accept only entries where its second probe hits non-box bytes or the end.
  static bool probeEndsWalk(const uint8_t* str, uint64_t len) {
    uint64_t idx = u32(str) - 8u;
    if (idx >= len) return true;
    return idx + 8 <= len && !(isalnum(str[idx + 4]) && isalnum(str[idx + 5]) && isalnum(str[idx + 6]) && isalnum(str[idx + 7]));
  }
  static bool isVideoEntry(const uint8_t* t) {
    static const char* v[] = {"avc1", "avc3", "hvc1", "hev1", "mp4v", "vp08", "vp09", "av01", "encv", "s263", "jpeg", "dvh1"};
    for (const char* x : v) if (is(t, x)) return true;
    return false;
  }
  // objectTypeIndication of the esds DecoderConfigDescriptor, found with the same walk as ESDSParser (which skips it); 0 if absent.
  static uint8_t esdsObjectType(const uint8_t* d, size_t n) {
    const uint8_t* p = d; const uint8_t* end = d + n;
    auto skipLen = [&] { for (int i = 0; i < 4 && p < end; ++i) if (!(*p++ & 0x80)) break; };
    p += 4;
    if (p >= end || *p++ != 0x03) return 0;
    skipLen(); p += 3;
    if (p >= end || *p++ != 0x04) return 0;
    skipLen();
    return p < end ? *p : 0;
  }
  bool onStsdDone() {
    const uint8_t* s = sc.stsd; uint64_t p = sc.payload;
    if (p < 16 || u32(s + 4) != 1 || u32(s + 8) != p - 8) return fail(M4aStatus::badStsd);
    if (!is(s + 12, "mp4a")) return fail(isVideoEntry(s + 12) ? M4aStatus::firstTrackNotAudio : M4aStatus::notMp4a);
    if (p < 52) return fail(M4aStatus::badStsd);
    uint32_t esdsSize = u32(s + 44);
    if (esdsSize < 16 || esdsSize > p - 44 || !is(s + 48, "esds")) return fail(M4aStatus::badStsd);
    if (!probeEndsWalk(s + 8, p - 8) || !probeEndsWalk(s + 44, p - 44)) return fail(M4aStatus::badStsd);
    audio_tools::M4ACommonDemuxer::ESDSParser e;
    if (!e.parse(s + 52, esdsSize - 8)) return fail(M4aStatus::badStsd);
    if (esdsObjectType(s + 52, esdsSize - 8) != 0x40 || e.audioObjectType != 2) return fail(M4aStatus::notAacLc);  // HE-AAC (5/29), Main (1), MPEG audio (0x6B) ...
    if (e.samplingRateIndex != 4) return fail(M4aStatus::badSampleRate);
    if (e.channelConfiguration != 2) return fail(M4aStatus::badChannels);
    return true;
  }

  bool onStszWord(uint64_t k, uint32_t w) {
    if (k == 1) { if (w) return fail(M4aStatus::badStsz); }  // fixed-size tables: the reader cannot parse a 12-byte stsz
    else if (k == 2) {
      if (w > kM4aMaxTableEntries) return fail(M4aStatus::tooManySamples);
      if (w == 0) return fail(M4aStatus::badStsz);
      if (sc.payload != 12 + 4ull * w) return fail(M4aStatus::badStsz);
    } else if (k > 2 && (w == 0 || w > kM4aMaxFrameBytes - kM4aAdtsHeaderBytes)) return fail(M4aStatus::badFrameSize);
    return true;
  }

  bool scan(const uint8_t* d, size_t n) {
    size_t i = 0;
    for (;; ) {
      if (sc.zoneOpen && sc.pos >= sc.zoneEnd) { zones[nz - 1].end = i - (size_t)(sc.pos - sc.zoneEnd); sc.zoneOpen = false; }
      if (i >= n) break;
      if (sc.mode == Mode::header) {
        if (!sc.hdrLen) popLevels();
        sc.hdr[sc.hdrLen++] = d[i++]; ++sc.pos;
        if (sc.hdrLen >= 8 && !onHeader(i - 1)) return false;
        continue;
      }
      size_t t = (size_t)std::min<uint64_t>(sc.remaining, n - i);
      if (sc.mode == Mode::skip) { sc.pos += t; sc.remaining -= t; i += t; if (sc.remaining == 0) sc.mode = Mode::header; continue; }
      for (size_t e = i + t; i < e; ++i) {
        uint64_t idx = sc.payload - sc.remaining;
        if (sc.mode == Mode::stsd) sc.stsd[idx] = d[i];
        else {
          sc.word = sc.word << 8 | d[i];
          if ((idx & 3) == 3 && !onStszWord(idx >> 2, sc.word)) return false;
        }
        ++sc.pos; --sc.remaining;
      }
      if (sc.remaining == 0) {
        bool stsdDone = sc.mode == Mode::stsd;
        sc.mode = Mode::header;
        if (stsdDone && !onStsdDone()) return false;
      }
    }
    if (sc.mode == Mode::header && sc.hdrLen && !sc.zoneOpen) {  // header split across writes: hold it back
      if (nz >= kZones) return fail(M4aStatus::tooManyBoxes);
      zones[nz++] = {n - sc.hdrLen, 0, false, false}; sc.zoneOpen = true; sc.zoneEnd = UINT64_MAX;
    }
    return true;
  }
};
}  // namespace cydpod
