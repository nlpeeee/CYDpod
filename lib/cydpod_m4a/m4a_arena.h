// CY-10b2: one fallible up-front arena for everything the vendored M4A reader allocates, plus a small separate reserve.
//
// The vendored Allocator::allocate() calls stop() on a null result and its callers (Vector growth, SingleBuffer::resize, createArray)
// write through the result unconditionally, so a null cannot be returned. Instead:
//   1. the arena is acquired ONCE through M4aArenaMemory (the only fallible allocation; failure -> M4aStatus::noMemory, nothing else
//      is constructed);
//   2. requests are served from the main region (first fit, split, coalescing free: Vector growth allocates the new block before it
//      frees the old one, so frees must be real);
//   3. a request the main region cannot serve sets the sticky `exhausted` flag and is served from the reserve region, which is
//      bump-only, never reused, never overlaps a main block, and is sized (m4a_arena_sizes below) for everything one vendor call can
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

// Sizes (derivation and measured evidence: docs/research/cy10b-alloc-failure.md).
// Live bound = parser buffer 2048 + demuxer buffer 2056 + callbacks 336 + containers 272 + levelStack 64 + frame buffer 2041 + ADTS scratch 2048
// + its predecessor during growth 2047 + resident table 512, rounded to 8 B, plus 9 blocks x 8 B headers = 11,504; stated as 11,520.
constexpr size_t kM4aArenaLiveBoundBytes = 11520;
constexpr size_t kM4aArenaMainBytes = 20 * 1024;     // bound + first-fit fragmentation slack (worst observed high water 16,128 B); exhaustion here is availability, never safety
constexpr size_t kM4aArenaReserveBytes = 8 * 1024;   // bump-only; proven worst one-call request total < 5 KiB (begin: 4,112 B measured)
constexpr size_t kM4aVendorSliceBytes = 256;         // largest mdat payload slice handed to the vendor in one call
constexpr size_t kM4aArenaResidentEntries = 256;     // no-source (resident table) mode inside an arena: 2 bytes each = 512 B

class M4aArena {
 public:
  struct Stats {
    size_t allocs = 0, frees = 0, live = 0, peakLive = 0, highWater = 0, maxRequest = 0, reserveUsed = 0, reserveRequests = 0,
           foreignFrees = 0, leakedAtReset = 0, callRequests = 0, callBytes = 0;
  };

  // Acquires main + reserve bytes in one block. Returns false (and holds nothing) if the memory source refuses.
  bool open(const M4aArenaMemory* memory, size_t mainBytes, size_t reserveBytes) {
    close();
    mainBytes &= ~size_t(7); reserveBytes &= ~size_t(7);
    if (!memory || mainBytes < 2 * kHdr) return false;
    void* b = memory->acquire(mainBytes + reserveBytes, memory->ctx);
    if (!b) return false;
    mem = memory; base = static_cast<uint8_t*>(b); mainSize = mainBytes; resSize = reserveBytes;
    reset();
    return true;
  }
  void close() {
    if (base) mem->release(base, mem->ctx);
    base = nullptr; mainSize = resSize = 0;
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

  void* allocate(size_t n) {
    if (!base) return nullptr;
    size_t need = (n + 7) & ~size_t(7);
    if (need < 8) need = 8;
    ++stats.allocs; ++stats.callRequests; stats.callBytes += need + kHdr;
    if (n > stats.maxRequest) stats.maxRequest = n;
    if (need <= mainSize - kHdr) {
      size_t total = need + kHdr;
      for (size_t off = 0; off < mainSize;) {
        Hdr* h = hdr(off);
        if (h->state == kFree && h->size >= total) {
          size_t rest = h->size - total;
          if (rest >= kHdr + 8) { h->size = uint32_t(total); Hdr* nx = hdr(off + total); nx->size = uint32_t(rest); nx->state = kFree; }
          h->state = kUsed;
          stats.live += h->size;
          if (stats.live > stats.peakLive) stats.peakLive = stats.live;
          if (off + h->size > stats.highWater) stats.highWater = off + h->size;
          return base + off + kHdr;
        }
        off += h->size;
      }
    }
    exhausted_ = true;  // sticky until reset()
    ++stats.reserveRequests;
    if (need > resSize - resUsed) return nullptr;  // beyond the proven reserve bound: the vendor's stop() path (documented residual)
    void* p = base + mainSize + resUsed;
    resUsed += need; stats.reserveUsed = resUsed;
    return p;
  }

  void release(void* p) {
    if (!base || !p) return;
    uint8_t* b = static_cast<uint8_t*>(p);
    if (b >= base + mainSize && b < base + mainSize + resSize) { ++stats.frees; return; }  // reserve: never reused
    if (b < base + kHdr || b >= base + mainSize || ((b - base) & 7)) { ++stats.foreignFrees; return; }
    Hdr* h = reinterpret_cast<Hdr*>(b - kHdr);
    if (h->state != kUsed) { ++stats.foreignFrees; return; }
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
  uint8_t* base = nullptr;
  size_t mainSize = 0, resSize = 0, resUsed = 0;
  bool exhausted_ = false;
  Stats stats;
};
}  // namespace cydpod
