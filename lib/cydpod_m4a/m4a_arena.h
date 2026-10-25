// CY-10b2/b3: one fallible up-front arena for everything the vendored M4A reader allocates, plus a small separate reserve.
//
// The vendored Allocator::allocate() calls stop() on a null result and its callers (Vector growth, SingleBuffer::resize, createArray)
// write through the result unconditionally, so a null cannot be returned. Instead:
//   1. the arena is acquired ONCE through M4aArenaMemory (the only fallible allocation; failure -> M4aStatus::noMemory, nothing else
//      is constructed);
//   2. requests are served from the main region (best fit, split, coalescing free: Vector growth allocates the new block before it
//      frees the old one, so frees must be real). CY-10b3 fragmentation control: every request of kArenaClassMinRequest+1 .. kArenaClassPayload
//      bytes (all the vendor's 1-2 KiB buffers: parser, demuxer, frame buffer, ADTS scratch) is served as ONE block size, cut from the
//      HIGH end of the best-fitting free block, while smaller requests are cut from the low end; so a freed large block is always
//      exactly reusable by the next large request and the two kinds do not carve each other's holes (measured: minimum workable main
//      region = peak live + 192 B on every input; plain first fit needed 1.47x the peak);
//   3. a request the main region cannot serve sets the sticky `exhausted` flag and is served from the reserve region, which is
//      bump-only, never reused, never overlaps a main block, and is sized (constants below) for everything one vendor call can
//      ask for. The hook therefore never returns null, Allocator::allocate() never reaches stop(), and the reader checks the flag after
//      every call into the vendored code and turns it into a clean status. Releases of reserve blocks are no-ops.
// Memory handed out is not cleared here: the overlay's Allocator::allocate() zero-fills it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include "m4a_caps.h"

namespace cydpod {

// The single fallible allocation, faked by host tests. Firmware: heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / free.
struct M4aArenaMemory {
  void* (*acquire)(size_t bytes, void* ctx);  // returns nullptr on failure; 8-byte aligned
  void (*release)(void* block, void* ctx);
  void* ctx;
};
inline const M4aArenaMemory* m4aNothrowMemory() {  // host / default implementation
  static const M4aArenaMemory m{[](size_t n, void*) -> void* { return ::operator new(n, std::nothrow); },
                                [](void* p, void*) { ::operator delete(p); }, nullptr};
  return &m;
}

// Sizes (derivation and measured evidence: docs/research/cy10b-alloc-failure.md). Bytes, host (64-bit) layouts; the 32-bit target is not larger.
constexpr size_t kArenaHdrBytes = 8;                  // per-block header
constexpr size_t kArenaClassMinRequest = 512;         // requests above this, up to kArenaClassPayload, are served as one fixed block size
constexpr size_t kArenaClassPayload = 2056;           // parser 2048, demuxer <= 2056, frame buffer <= 2048, ADTS scratch <= 2048 (frames <= 2041 B)
constexpr size_t kArenaClassBlock = kArenaClassPayload + kArenaHdrBytes;
constexpr size_t kM4aArenaResidentEntries = 64;       // no-source (resident table) mode inside an arena: 2 bytes each = 128 B (file-backed, the shipping mode: none)
constexpr size_t kM4aVendorSliceBytes = 64;           // largest mdat payload slice handed to the vendor in one call
// Proven bound on live bytes: 5 class blocks (parser, demuxer, frame buffer, ADTS scratch, and the predecessor of whichever of the last
// two is growing: only one grows at a time) + callbacks 6 x 56 + containers 17 x 16 + levelStack 8 x 8 + resident table 64 x 2, each
// block with its header, + one small predecessor while a small vector grows (the largest is 280 + 8).
constexpr size_t kM4aArenaLiveBoundBytes = 5 * kArenaClassBlock + (336 + 8) + (272 + 8) + (64 + 8) + (128 + 8) + (280 + 8);  // 11,440
// Main region: the bound plus an allowance for fragmentation (observed: minimum workable main = peak live + 192 B; allowance 336 B).
constexpr size_t kArenaMainBytes = 11776;
// Reserve: bump-only, plain request sizes (no headers). Worst single vendor call, measured 4,096 (begin: parser + demuxer) and bounded by
// one 64-byte mdat slice: frame buffer jump 2,048 + ADTS scratch jump 2,056 + the strictly growing small steps inside the slice (< 250) +
// 8-byte rounding < 4,320; moov: 2,448 + 288; construction: 2,368.
constexpr size_t kArenaReserveBytes = 4608;
constexpr size_t kArenaTotalBytes = kArenaMainBytes + kArenaReserveBytes;  // 16,384: the one contiguous block taken from the heap
constexpr size_t kArenaAlignSlack = 8;  // heap_caps_malloc guarantees only 4-byte alignment: open() asks for this much more and aligns the base itself
constexpr size_t kArenaAcquireBytes = kArenaTotalBytes + kArenaAlignSlack;  // the one request made to the heap
static_assert(kArenaTotalBytes == 16384, "arena total");
static_assert(kArenaMainBytes >= kM4aArenaLiveBoundBytes + 336, "main region must cover the live bound plus the fragmentation allowance");

class M4aArena {
 public:
  struct Stats {
    size_t allocs = 0, frees = 0, live = 0, peakLive = 0, maxRequest = 0, reserveUsed = 0, reserveRequests = 0,
           foreignFrees = 0, leakedAtReset = 0, callRequests = 0, callBytes = 0;
  };

  // Test aid (null in the firmware): lets the allocation-failure sweep refuse requests and audit every grant by address range.
  struct Probe {
    bool (*refuse)(size_t n, void* ctx);                                       // true: the main region is treated as unable to serve this request
    void (*allocated)(const void* p, size_t bytes, bool reserve, void* ctx);   // every non-null grant; bytes = payload granted
    void (*released)(const void* p, bool reserve, void* ctx);                  // every honoured release
    void* ctx;
  };
  void setProbe(const Probe* p) { probe = p; }

  // Acquires main + reserve bytes in one block. Returns false (and holds nothing) if the memory source refuses.
  bool open(const M4aArenaMemory* memory, size_t mainBytes, size_t reserveBytes) {
    close();
    mainBytes &= ~size_t(7); reserveBytes &= ~size_t(7);
    if (!memory || mainBytes < 2 * kHdr) return false;
    void* b = memory->acquire(mainBytes + reserveBytes + kArenaAlignSlack, memory->ctx);
    if (!b) return false;
    raw = b; mem = memory; base = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(b) + 7) & ~uintptr_t(7)); mainSize = mainBytes; resSize = reserveBytes;
    reset();
    return true;
  }
  void close() {
    if (base) mem->release(raw, mem->ctx);
    base = nullptr; raw = nullptr; mainSize = resSize = 0;
  }
  ~M4aArena() { close(); }
  bool isOpen() const { return base != nullptr; }

  // Whole-arena reset between tracks: one free block, empty reserve, flag cleared. Live blocks are counted as leaks.
  void reset() {
    stats.leakedAtReset = stats.live;
    stats.live = 0; stats.reserveUsed = 0; exhausted_ = false;
    Hdr* h = hdr(0); h->size = uint32_t(mainSize); h->state = kFree;
    resUsed = 0;
  }
  bool exhausted() const { return exhausted_; }
  // Walks every main-region block header: an out-of-bounds write from one block into the next would break the chain.
  bool consistent() const {
    size_t off = 0;
    while (base && off < mainSize) {
      const Hdr* h = reinterpret_cast<const Hdr*>(base + off);
      if ((h->state != kFree && h->state != kUsed) || h->size < kHdr + 8 || (h->size & 7) || h->size > mainSize - off) return false;
      off += h->size;
    }
    return !base || off == mainSize;
  }
  void clearCallCounters() { stats.callRequests = stats.callBytes = 0; }  // per-vendor-call accounting (evidence only)
  const Stats& stat() const { return stats; }
  size_t mainBytes() const { return mainSize; }
  size_t reserveBytes() const { return resSize; }
  const uint8_t* regionBase() const { return base; }  // evidence: address-range audits

  void* allocate(size_t n) {
    if (!base) return nullptr;
    const bool refused = probe && probe->refuse && probe->refuse(n, probe->ctx);
    size_t need = (n + 7) & ~size_t(7);
    if (need < 8) need = 8;
    ++stats.allocs; ++stats.callRequests; stats.callBytes += need + kHdr;  // per-call accounting is of the plain request, not of the class
    if (n > stats.maxRequest) stats.maxRequest = n;
    const size_t plain = need;  // the reserve is never reused, so it serves the plain request
    const bool big = n > kArenaClassMinRequest && n <= kArenaClassPayload && mainSize >= kArenaClassBlock;
    if (big) need = kArenaClassPayload;  // every vendor buffer of the admitted range is one block size, so freed ones are always reusable
    if (!refused && need <= mainSize - kHdr) {
      size_t total = need + kHdr;
      // Best fit; small blocks are cut from the low end of a block and sit at low addresses, class blocks are cut from the high end
      // (ties: highest address) so the two kinds do not carve each other's holes.
      Hdr* best = nullptr; size_t bestOff = 0;
      for (size_t off = 0; off < mainSize;) {
        Hdr* h = hdr(off);
        if (h->state == kFree && h->size >= total &&
            (!best || h->size < best->size || (big && h->size == best->size))) { best = h; bestOff = off; }
        off += h->size;
      }
      if (best) {
        Hdr* h = best; size_t rest = h->size - total, at = bestOff;
        if (rest >= kHdr + 8) {
          if (big) { h->size = uint32_t(rest); at = bestOff + rest; h = hdr(at); h->size = uint32_t(total); }  // the low part stays free
          else { h->size = uint32_t(total); Hdr* nx = hdr(bestOff + total); nx->size = uint32_t(rest); nx->state = kFree; }
        }
        h->state = kUsed;
        stats.live += h->size;
        if (stats.live > stats.peakLive) stats.peakLive = stats.live;
        if (probe && probe->allocated) probe->allocated(base + at + kHdr, h->size - kHdr, false, probe->ctx);
        return base + at + kHdr;
      }
    }
    exhausted_ = true;  // sticky until reset()
    ++stats.reserveRequests;
    if (plain > resSize - resUsed) return nullptr;  // beyond the proven reserve bound: the vendor's stop() path (documented residual)
    void* p = base + mainSize + resUsed;
    resUsed += plain; stats.reserveUsed = resUsed;
    if (probe && probe->allocated) probe->allocated(p, plain, true, probe->ctx);
    return p;
  }

  void release(void* p) {
    if (!base || !p) return;
    uint8_t* b = static_cast<uint8_t*>(p);
    if (b >= base + mainSize && b < base + mainSize + resSize) { ++stats.frees; if (probe && probe->released) probe->released(p, true, probe->ctx); return; }  // reserve: never reused
    if (b < base + kHdr || b >= base + mainSize || ((b - base) & 7)) { ++stats.foreignFrees; return; }
    Hdr* h = reinterpret_cast<Hdr*>(b - kHdr);
    if (h->state != kUsed) { ++stats.foreignFrees; return; }
    if (probe && probe->released) probe->released(p, false, probe->ctx);
    h->state = kFree; ++stats.frees; stats.live -= h->size;
    for (size_t off = 0; off < mainSize;) {  // coalesce adjacent free blocks
      Hdr* a = hdr(off);
      size_t nxt = off + a->size;
      if (a->state == kFree && nxt < mainSize && hdr(nxt)->state == kFree) a->size += hdr(nxt)->size;
      else off = nxt;
    }
  }

  // Hook adapters (AllocatorHook::allocate / release with ctx == this).
  static void* hookAllocate(size_t n, void* ctx) { return static_cast<M4aArena*>(ctx)->allocate(n); }
  static void hookRelease(void* p, void* ctx) { static_cast<M4aArena*>(ctx)->release(p); }

 private:
  static constexpr uint32_t kFree = 0xF4EEF4EEu, kUsed = 0xA110C8EDu;
  struct Hdr { uint32_t size, state; };  // size = whole block including this header
  static constexpr size_t kHdr = sizeof(Hdr);
  Hdr* hdr(size_t off) { return reinterpret_cast<Hdr*>(base + off); }
  const M4aArenaMemory* mem = nullptr;
  uint8_t* base = nullptr; void* raw = nullptr;  // raw = what the memory source handed out (base is raw rounded up to 8)
  const Probe* probe = nullptr;
  size_t mainSize = 0, resSize = 0, resUsed = 0;
  bool exhausted_ = false;
  Stats stats;
};
}  // namespace cydpod
