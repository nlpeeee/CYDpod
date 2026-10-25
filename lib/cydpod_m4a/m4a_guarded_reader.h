// CYDpod-side wrapper around the unmodified AudioTools M4AAudioDemuxer: a streaming box-header guard enforces the
// caps in m4a_caps.h on the raw bytes BEFORE they reach the reader, so the reader's resizes/asserts never see
// out-of-cap values. Failures come back as M4aStatus; nothing here asserts or aborts.
#pragma once
#include <functional>
#include "AudioTools/AudioCodecs/M4AAudioDemuxer.h"
#include "m4a_caps.h"
#include "m4a_file_sizes.h"
#include "m4a_state_reset.h"
#include "m4a_arena.h"
#include <new>

namespace cydpod {
enum class M4aStatus : uint8_t {
  ok, badBoxHeader, tooDeep, stsdTooLarge, badStsd, notMp4a, badStsz, tooManySamples, badFrameSize,
  moovAfterMdat, duplicateTrack, truncated, noSampleTable, tableOverflow, tooManyBoxes,
  firstTrackNotAudio, notAacLc, badSampleRate, badChannels, noAudioTrack, boxTooLarge, offsetOverflow,
  sourceReadError, sourceShortRead, sourceSeekError,  // CY-11a: file-backed table source failures
  noMemory  // CY-10b2: the reader arena could not be acquired, or a vendor call outgrew it (sticky exhaustion flag)
};
inline const char* m4aStatusName(M4aStatus s) {
  static const char* n[] = {"ok", "bad_box_header", "too_deep", "stsd_too_large", "bad_stsd", "not_mp4a", "bad_stsz",
                            "too_many_samples", "bad_frame_size", "moov_after_mdat", "duplicate_track", "truncated",
                            "no_sample_table", "table_overflow", "too_many_boxes", "first_track_not_audio", "not_aac_lc",
                            "bad_sample_rate", "bad_channels", "no_audio_track", "box_too_large", "offset_overflow",
                            "source_read_error", "source_short_read", "source_seek_error", "no_memory"};
  return n[static_cast<uint8_t>(s)];
}

class M4aGuardedReader {
 public:
  using AdtsCallback = std::function<void(const uint8_t* adts, size_t size)>;
  M4aGuardedReader() = default;
  M4aGuardedReader(const M4aGuardedReader&) = delete;
  M4aGuardedReader& operator=(const M4aGuardedReader&) = delete;
  ~M4aGuardedReader() { destroyVendor(); }  // vendor objects die under the hook, before the arena memory is released

  // Valid after a successful begin(): the vendored objects are constructed there, not at construction (CY-10b2).
  M4aCappedSizes& sizeTable() { return *sizesPtr(); }
  audio_tools::M4AAudioDemuxer& reader() { return *demuxPtr(); }  // test access only
  const M4aArena& arenaState() const { return arena; }
  size_t maxCallRequests() const { return callMaxRequests; }  // evidence: largest request count / byte total of one vendor call
  size_t maxCallBytes() const { return callMaxBytes; }
  void setArenaProbe(const M4aArena::Probe* p) { arena.setProbe(p); }  // test aid (allocation-failure sweep); never set in the firmware

  // Arena memory source (default: new (std::nothrow)); nullptr selects the legacy mode in which the vendor allocates from the heap
  // directly (unprotected; host baselines only). Only effective when the allocator-hook overlay is applied (CYDPOD_ALLOCATOR_HOOK).
  // Call before begin(); it ends any running track. Sizes default to kArenaMainBytes / kArenaReserveBytes (tests shrink them).
  void setArenaMemory(const M4aArenaMemory* m) { destroyVendor(); arena.close(); mem = m; }
  void setArenaSizes(size_t mainBytes, size_t reserveBytes) { destroyVendor(); arena.close(); mainB = mainBytes; resB = reserveBytes; }
  // Gives the arena memory back (the vendor objects are destroyed first). The next begin() acquires it again.
  void end() { destroyVendor(); arena.close(); }

  // begin() starts a new track and is a full reset: the vendored demuxer and the size table are destroyed and rebuilt (the vendored
  // M4AAudioDemuxer::begin() alone does not make a used instance safe to reuse), the arena is reset as a whole, the guard state is
  // cleared, and the vendored function-static sample-size cache is primed (m4a_state_reset.h). A new instance per track is equivalent.
  // With the overlay, the first begin() makes the one fallible allocation (the arena); failure returns M4aStatus::noMemory and nothing
  // else is constructed. The vendored objects are constructed only after the arena exists and the hook is installed.
  // With a source the sample-size table stays in the file (1 KiB window, m4a_file_sizes.h); the source must serve the same stream
  // offsets that are fed to write() and stay valid until the next begin(). Without one the table is resident (<= kM4aResidentEntries;
  // <= kM4aArenaResidentEntries in arena mode).
  M4aStatus begin(AdtsCallback cb, M4aSeekableSource* source = nullptr) {
    onFrame = cb; st = M4aStatus::ok; sc = Scan(); nz = 0; heldLen = 0;
    bool fresh = !built;
#ifndef CYDPOD_NO_STATE_RESET  // test-only switch that restores the pre-CY-5 behaviour for before/after evidence
    destroyVendor(); fresh = true;
#endif
    if (fresh) {
      if (arenaMode()) {
        if (!arena.isOpen()) { if (!arena.open(mem, mainB, resB)) return st = M4aStatus::noMemory; }
        else arena.reset();
      }
      Scope hook(*this);
      arena.clearCallCounters();
      new (sizesBuf) M4aCappedSizes; new (demuxBuf) audio_tools::M4AAudioDemuxer; built = true;
      if (!vendorDone()) return abortBuild();
#ifndef CYDPOD_NO_STATE_RESET
      m4aPrimeSampleSizeCache();
      if (!vendorDone()) return abortBuild();
#endif
    }
    Scope hook(*this);
    M4aCappedSizes& sizes = *sizesPtr(); audio_tools::M4AAudioDemuxer& demux = *demuxPtr();
    if (arenaMode()) sizes.hardLimit(kM4aArenaResidentEntries);
    sizes.source(source);
    demux.setSampleSizesBuffer(sizes);
    demux.setReference(this);  // setReference must precede setCallback
    demux.setCallback([](const audio_tools::M4ACommonDemuxer::Frame& f, void* r) {
      auto* self = static_cast<M4aGuardedReader*>(r);
      if (self->onFrame) self->onFrame(f.data, f.size);
    });
    demux.begin();
    if (!vendorDone()) return abortBuild();
    return st;
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
    nz = 0; const uint64_t base = sc.pos;
    if (sc.zoneOpen) zones[nz++] = {0, 0, true, false};
    if (!scan(d, n)) return st;
    size_t c = 0;
    for (unsigned k = 0; k < nz; ++k) {
      Zone& z = zones[k];
      size_t e = z.end ? z.end : n;
      if (!z.continuing) { if (z.start > c && !feed(d + c, z.start - c, base + c)) return st; heldLen = 0; }
      memcpy(held + heldLen, d + (z.continuing ? 0 : z.start), e - (z.continuing ? 0 : z.start));
      heldLen += e - (z.continuing ? 0 : z.start);
      if (!z.end) { c = n; break; }
      if (z.rewrite) memcpy(held + 4, "free", 4);
      if (!vendorWrite(held, heldLen)) return st;
      heldLen = 0; c = e;
    }
    if (c < n && !feed(d + c, n - c, base + c)) return st;
    if (sz().overflow) st = M4aStatus::tableOverflow;
    else if (sz().error != M4aSourceResult::ok) st = sourceStatus();
    return st;
  }
  M4aStatus finish() {
    if (st != M4aStatus::ok) return st;
    if (sz().error != M4aSourceResult::ok) return st = sourceStatus();
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
    uint64_t payloadFrom = UINT64_MAX, payloadTo = 0;  // top-level mdat payload after its first byte (delivered with the header), sliceable
    bool zoneOpen = false, mdatSeen = false, isMdat = false, openedMoov = false, moovDone = false, stsdSeen = false, stszSeen = false;
    uint8_t stsd[kM4aMaxStsdBytes];
  };
  struct Zone { size_t start, end; bool continuing, rewrite; };
  static constexpr unsigned kZones = 12;  // stsd, stsz, mdat, one pending header, plus neutralized boxes
  Zone zones[kZones]; unsigned nz = 0;
  uint8_t held[kM4aMaxStsdBytes + 16]; size_t heldLen = 0;
  // Vendored objects live in raw storage and are constructed in begin(), after the arena exists and the hook is installed.
  alignas(audio_tools::M4AAudioDemuxer) unsigned char demuxBuf[sizeof(audio_tools::M4AAudioDemuxer)];
  alignas(M4aCappedSizes) unsigned char sizesBuf[sizeof(M4aCappedSizes)];
  bool built = false;
  M4aArena arena; const M4aArenaMemory* mem = m4aNothrowMemory(); size_t mainB = kArenaMainBytes, resB = kArenaReserveBytes;
#ifdef CYDPOD_ALLOCATOR_HOOK
  audio_tools::AllocatorHook hook{M4aArena::hookAllocate, M4aArena::hookRelease, &arena};
#endif
  size_t callMaxRequests = 0, callMaxBytes = 0;

  AdtsCallback onFrame;
  Scan sc;
  M4aStatus st = M4aStatus::ok;

  M4aCappedSizes* sizesPtr() { return reinterpret_cast<M4aCappedSizes*>(sizesBuf); }
  audio_tools::M4AAudioDemuxer* demuxPtr() { return reinterpret_cast<audio_tools::M4AAudioDemuxer*>(demuxBuf); }
  M4aCappedSizes& sz() { return *sizesPtr(); }
  const M4aCappedSizes& sz() const { return *reinterpret_cast<const M4aCappedSizes*>(sizesBuf); }
#ifdef CYDPOD_ALLOCATOR_HOOK
  bool arenaMode() const { return mem != nullptr; }
#else
  bool arenaMode() const { return false; }
#endif

  // Installs this reader's hook for exactly one call into the vendored code and restores the previous slot value afterwards. The
  // slot is a plain global written only here, i.e. only on the task that owns the reader (the producer task); other tasks never run
  // the vendored Allocator (they use malloc / heap_caps directly), so nothing else can be routed into the arena. Source and frame
  // callbacks run inside the scope and must not allocate through audio_tools containers.
  struct Scope {
#ifdef CYDPOD_ALLOCATOR_HOOK
    audio_tools::AllocatorHook* prev;
    explicit Scope(M4aGuardedReader& r) : prev(audio_tools::allocatorHook()) { if (r.arenaMode()) audio_tools::setAllocatorHook(&r.hook); }
    ~Scope() { audio_tools::setAllocatorHook(prev); }
#else
    explicit Scope(M4aGuardedReader&) {}
#endif
  };
  void destroyVendor() {
    if (!built) return;
    Scope hook(*this);
    demuxPtr()->~M4AAudioDemuxer(); sizesPtr()->~M4aCappedSizes();
    built = false;
  }
  // Checked after EVERY call into the vendored code. Exhaustion is sticky; the vendor state after it is junk, so the track is over.
  bool vendorDone() {
    if (!arenaMode()) return true;
    const M4aArena::Stats& s = arena.stat();
    if (s.callRequests > callMaxRequests) callMaxRequests = s.callRequests;
    if (s.callBytes > callMaxBytes) callMaxBytes = s.callBytes;
    arena.clearCallCounters();
    if (arena.exhausted()) { st = M4aStatus::noMemory; return false; }
    return true;
  }
  M4aStatus abortBuild() { destroyVendor(); return st; }
  bool vendorWrite(const uint8_t* p, size_t n) {
    { Scope hook(*this); demuxPtr()->write(p, n); }
    return vendorDone();
  }
  // Hands [p, p+len) (stream offset `pos`) to the vendor. Frame extraction is the only repeating allocator in the vendor (the
  // frame buffer and ADTS scratch grow to each larger frame), so the top-level mdat payload is delivered in kM4aVendorSliceBytes
  // slices with an exhaustion check between them; that bounds the requests of one call (see docs/research/cy10b-alloc-failure.md).
  // Everything else (whole headers/zones, other boxes) is delivered in one piece as before.
  bool feed(const uint8_t* p, size_t len, uint64_t pos) {
    while (len) {
      size_t t = len;
      if (pos < sc.payloadFrom) t = (size_t)std::min<uint64_t>(t, sc.payloadFrom - pos);
      else if (pos < sc.payloadTo) t = (size_t)std::min<uint64_t>(std::min<size_t>(t, kM4aVendorSliceBytes), sc.payloadTo - pos);
      if (!vendorWrite(p, t)) return false;
      p += t; pos += t; len -= t;
    }
    return true;
  }

  static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
  M4aStatus sourceStatus() const {
    switch (sz().error) {
      case M4aSourceResult::shortRead: return M4aStatus::sourceShortRead;
      case M4aSourceResult::seekError: return M4aStatus::sourceSeekError;
      case M4aSourceResult::badEntry: return M4aStatus::badFrameSize;  // an entry the scan accepted now reads back out of range
      default: return M4aStatus::sourceReadError;
    }
  }
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
  static bool isCodecCallback(const uint8_t* t) {
    return is(t, "alac") || is(t, "mp4a") || is(t, "esds") || is(t, "ac-3");
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
    uint64_t start = sc.pos - hl;
    if (!open && size > UINT64_MAX - start) return fail(M4aStatus::offsetOverflow);
    // Bound the full declared size explicitly to the ESP32 width on every host.
    if (!open && size > uint64_t(UINT32_MAX)) return fail(M4aStatus::boxTooLarge);
    uint64_t end = open ? UINT64_MAX : start + size;
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
    // stsd payload is scanned in Mode::stsd and validated as a whole before delivery.
    // Any codec header reaching this branch is outside that validated payload.
    if (!alnum || isKnownContainer(h + 4) || isCodecCallback(h + 4)) {
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
      sz().tableOffset(sc.pos + 12);  // first raw entry; version/flags, sample size and count precede it (hl == 8 here)
    } else if (mdat) {
      if (sc.depth == 0 && !sc.moovDone) return fail(M4aStatus::moovAfterMdat);  // the reader needs the sample tables first; moov-last is not supported
      if (sc.depth == 0 && !sc.stsdSeen) return fail(M4aStatus::noAudioTrack);
      if (sc.depth == 0 && !sc.stszSeen) return fail(M4aStatus::noSampleTable);
      if (sc.mdatSeen) return fail(M4aStatus::duplicateTrack);
      sc.mdatSeen = true; need += sc.payload ? 1 : 0;  // the first payload byte carries the size to the reader
      if (sc.depth == 0) { sc.payloadFrom = sc.pos + 1; sc.payloadTo = open ? UINT64_MAX : sc.pos + sc.payload; }
    }
    if (!sc.remaining) sc.mode = Mode::header;
    return holdHeader(idx, hl, start, need, rewrite);
  }

  // Registers (or finalizes the pending-header zone of) a box whose first `need` bytes must be delivered together.
  bool holdHeader(size_t idx, unsigned hl, uint64_t start, uint64_t need, bool rewrite) {
    if (need > UINT64_MAX - start) return fail(M4aStatus::offsetOverflow);
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
    if (uint64_t(n) > UINT64_MAX - sc.pos) return fail(M4aStatus::offsetOverflow);
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
