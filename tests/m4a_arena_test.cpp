// CY-10b2 host test: the guarded reader serves every vendored allocation from one fallible arena (m4a_arena.h) built on top of the
// allocator-hook overlay (patches/audio-tools/0001+0002).
//  (a) arena allocator unit tests: first fit / split / coalescing, reserve never aliases live blocks, foreign frees ignored
//  (b) fixture through the arena: 131 frames, 134144 samples per channel, ADTS and PCM identical to the heap (legacy) path; track reuse
//  (c) arena acquisition failure: noMemory, nothing constructed, no leak, hook slot untouched, reader usable afterwards
//  (d) forced exhaustion (sweep of tiny main regions): sticky flag, clean status, no out-of-bounds write, reserve never overrun
//  (e) hook hygiene: slot restored after every call (nested hook preserved), two readers interleaved, other-thread allocations never routed
//  (f) peak live bytes / per-call request totals: fixture, adversarial frame-size patterns, every real .m4a of the media folder
// usage: m4a_arena_test fixture.m4a [media-folder]    (no folder: part (f) real files skipped, exit 77 if everything else passed)
#include <Arduino.h>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include "m4a_file_source.h"
#include "../firmware/cydpod_probe/adts_decoder.h"
using namespace cydpod;
using Bytes = std::vector<uint8_t>;
extern "C" void* helix_malloc(int size) { return std::malloc(size); }
extern "C" void helix_free(void* p) { std::free(p); }

#ifndef CYDPOD_ALLOCATOR_HOOK
#error "m4a_arena_test needs the allocator-hook overlay (tests/apply_audiotools_overlay.sh)"
#endif

static int failures;
static void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(bool ok, const char* fmt, ...) {
  char b[500]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
  std::fprintf(stderr, "%-4s %s\n", ok ? "ok" : "FAIL", b); failures += !ok;
}

// ---- fake arena memory source ----
struct FakeMem {
  M4aArenaMemory vt{[](size_t n, void* c) -> void* { return static_cast<FakeMem*>(c)->acquire(n); },
                    [](void* p, void* c) { static_cast<FakeMem*>(c)->release(p); }, this};
  bool fail = false; int acquires = 0, releases = 0; size_t lastBytes = 0; std::set<void*> live;
  void* acquire(size_t n) { ++acquires; lastBytes = n; if (fail) return nullptr; void* p = ::operator new(n); live.insert(p); return p; }
  void release(void* p) { ++releases; check(live.erase(p) == 1, "release of a block this source handed out"); ::operator delete(p); }
};

// ---- synthetic m4a builder (as in m4a_caps_test / m4a_filetable_test) ----
static void put32(Bytes& b, uint32_t v) { for (int i = 24; i >= 0; i -= 8) b.push_back(v >> i); }
static Bytes box(const char* t, const Bytes& payload) {
  Bytes b; put32(b, 8 + payload.size()); b.insert(b.end(), t, t + 4); b.insert(b.end(), payload.begin(), payload.end()); return b;
}
static Bytes cat(std::initializer_list<Bytes> l) { Bytes r; for (auto& x : l) r.insert(r.end(), x.begin(), x.end()); return r; }
static Bytes stsd(size_t tail = 0) {
  Bytes e = {0, 0, 0, 0, 3, 22, 0, 0, 0, 4, 17, 0x40, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 2, 2 << 3 | 4 >> 1, (4 & 1) << 7 | 2 << 3};
  Bytes entry(28, 0); entry[7] = 1; entry[17] = 2;
  Bytes en = box("mp4a", cat({entry, box("esds", e), Bytes(tail, 0)}));
  Bytes p = {0, 0, 0, 0, 0, 0, 0, 1}; p.insert(p.end(), en.begin(), en.end()); return box("stsd", p);
}
static Bytes stsz(const std::vector<uint32_t>& entries) {
  Bytes p = {0, 0, 0, 0}; put32(p, 0); put32(p, (uint32_t)entries.size()); for (uint32_t e : entries) put32(p, e); return box("stsz", p);
}
static Bytes moov(const Bytes& sd, const Bytes& sz, int extraDepth = 0) {
  Bytes t = box("minf", box("stbl", cat({sd, sz})));
  for (int i = 0; i < extraDepth; ++i) t = box(i % 2 ? "trak" : "mdia", t);
  return box("moov", box("trak", box("mdia", t)));  // 5 + extraDepth levels; the cap is 8
}
static Bytes ftyp() { return box("ftyp", Bytes{'M', '4', 'A', ' ', 0, 0, 0, 0}); }
static Bytes synth(const std::vector<uint32_t>& sizes, size_t stsdTail = 0, int extraDepth = 0) {
  Bytes md; for (size_t i = 0; i < sizes.size(); ++i) for (uint32_t j = 0; j < sizes[i]; ++j) md.push_back(i + j);
  return cat({ftyp(), moov(stsd(stsdTail), stsz(sizes), extraDepth), box("mdat", md)});
}
static Bytes readFile(const std::string& path) {
  Bytes out; FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return out;
  uint8_t b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + n);
  std::fclose(f); return out;
}

// ---- running a reader ----
struct Run { M4aStatus st = M4aStatus::ok; size_t frames = 0; Bytes adts; M4aStatus beginSt = M4aStatus::ok; };
static Run feed(M4aGuardedReader& r, const Bytes& in, bool fileBacked = true, size_t slice = 1024, bool keepAdts = true) {
  Run o; MemSource src(in);
  o.beginSt = r.begin([&](const uint8_t* p, size_t n) { ++o.frames; if (keepAdts) o.adts.insert(o.adts.end(), p, p + n); }, fileBacked ? &src : nullptr);
  o.st = o.beginSt;
  for (size_t i = 0; i < in.size() && o.st == M4aStatus::ok; i += slice) o.st = r.write(in.data() + i, std::min(slice, in.size() - i));
  if (o.st == M4aStatus::ok) o.st = r.finish();
  return o;
}
struct Pcm { unsigned frames = 0; uint64_t hash = 14695981039346656037ull; bool ok = false; };
static Pcm decode(const Bytes& adts) {
  Pcm d; AdtsDecoder dec; if (!dec.begin()) return d;
  size_t pos = 0;
  for (;;) {
    DecodeResult r = dec.next([&](uint8_t* p, size_t n) { n = std::min(n, adts.size() - pos); std::memcpy(p, adts.data() + pos, n); pos += n; return n; });
    if (r == DecodeResult::eof) { d.ok = true; return d; }
    if (r != DecodeResult::frame) return d;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(dec.pcm());
    for (size_t i = 0; i < 4096; ++i) d.hash = (d.hash ^ s[i]) * 1099511628211ull;
    ++d.frames;
  }
}
// Smallest main region (8-byte steps) with which the track still plays without touching the reserve: the real requirement, fragmentation included.
static size_t minMain(const Bytes& in, size_t slice, bool fileBacked = true) {
  size_t lo = 64, hi = 32 * 1024;
  while (lo < hi) {
    size_t mid = ((lo + hi) / 2) & ~size_t(7);
    M4aGuardedReader r; r.setArenaSizes(mid, kArenaReserveBytes);
    Run o = feed(r, in, fileBacked, slice, false);
    if (o.st == M4aStatus::ok && !r.arenaState().exhausted()) hi = mid; else lo = mid + 8;
  }
  return lo;
}
static bool hookClear() { return audio_tools::allocatorHook() == nullptr; }

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: m4a_arena_test fixture.m4a [media-folder]\n"); return 2; }
  Bytes fixture = readFile(argv[1]);
  if (fixture.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

  // ---- (a) allocator unit tests ----
  {
    FakeMem mem; M4aArena a;
    check(!a.open(nullptr, 1024, 256) && !a.isOpen(), "(a) no memory source -> open fails");
    mem.fail = true;
    check(!a.open(&mem.vt, 1024, 256) && !a.isOpen() && mem.live.empty(), "(a) refused acquisition -> not open, nothing held");
    mem.fail = false;
    check(a.open(&mem.vt, 1024, 4096) && mem.acquires == 2 && mem.lastBytes == 4096 + 1024 + kArenaAlignSlack && mem.live.size() == 1, "(a) one acquisition of main+reserve");
    void* p1 = a.allocate(100); void* p2 = a.allocate(200); void* p3 = a.allocate(300);
    check(p1 && p2 && p3 && p1 != p2 && (uint8_t*)p2 >= (uint8_t*)p1 + 100 && (uint8_t*)p3 >= (uint8_t*)p2 + 200 && !((uintptr_t)p1 & 7) && !((uintptr_t)p2 & 7), "(a) first fit, 8-byte aligned, disjoint");
    std::memset(p1, 1, 100); std::memset(p2, 2, 200); std::memset(p3, 3, 300);
    a.release(p2);
    void* q = a.allocate(150);
    check(q == p2 && ((uint8_t*)p1)[99] == 1 && ((uint8_t*)p3)[0] == 3, "(a) freed hole reused (first fit) without touching neighbours");
    a.release(q); a.release(p1); a.release(p3);
    void* big = a.allocate(1024 - 8);
    check(big != nullptr && !a.exhausted() && a.stat().live == 1024, "(a) free + coalesce restores the whole main region");
    a.release(big);
    check(a.stat().live == 0, "(a) live bytes back to 0");
    // exhaustion: sticky flag, reserve bump region never overlaps a live main block, then nullptr only past the reserve
    void* m1 = a.allocate(1000); std::memset(m1, 0xAA, 1000);
    void* r1 = a.allocate(100);
    check(a.exhausted() && r1 != nullptr && (uint8_t*)r1 >= (uint8_t*)m1 + 1024 - 8, "(a) request beyond main sets the sticky flag and is served from the reserve");
    std::memset(r1, 0xBB, 100);
    a.release(m1); void* m2 = a.allocate(500);  // main has room again; flag stays set
    check(m2 != nullptr && a.exhausted() && (uint8_t*)m2 < (uint8_t*)r1, "(a) flag is sticky; later requests may use the main region again");
    a.release(r1);  // reserve release: no-op
    void* r2 = a.allocate(2000);
    check(r2 != nullptr && (uint8_t*)r2 >= (uint8_t*)r1 + 104 && a.stat().reserveRequests == 2, "(a) reserve is bump-only: a released reserve block is not reused");
    void* r3 = a.allocate(1u << 20);
    check(r3 == nullptr, "(a) a request beyond the reserve returns nullptr (vendor stop(); outside the proven bound)");
    int bogus; a.release(&bogus); a.release((uint8_t*)m2 + 3); a.release(nullptr);
    check(a.stat().foreignFrees == 2, "(a) foreign / misaligned frees ignored (%zu)", a.stat().foreignFrees);
    a.release(m2);
    a.reset();
    check(!a.exhausted() && a.stat().live == 0 && a.allocate(1000) != nullptr && !a.exhausted(), "(a) reset clears the flag, the reserve and the main region");
    a.close();
    check(mem.live.empty() && mem.releases == 1, "(a) close releases the single block");
  }

  // ---- (b) fixture through the arena ----
  Pcm legacyPcm; Bytes legacyAdts; size_t legacyFrames = 0;
  {
    M4aGuardedReader legacy; legacy.setArenaMemory(nullptr);
    Run o = feed(legacy, fixture); legacyAdts = o.adts; legacyFrames = o.frames; legacyPcm = decode(o.adts);
    check(o.st == M4aStatus::ok && o.frames == 131 && legacyPcm.ok && legacyPcm.frames * 1024 == 134144, "(b) legacy heap path: %zu frames, %u samples/channel", o.frames, legacyPcm.frames * 1024);
  }
  {
    FakeMem mem; auto* r = new M4aGuardedReader; r->setArenaMemory(&mem.vt);
    Run o = feed(*r, fixture); Pcm p = decode(o.adts);
    const M4aArena::Stats& s = r->arenaState().stat();
    check(o.st == M4aStatus::ok && o.frames == 131 && p.ok && p.frames * 1024 == 134144, "(b) fixture via arena: %zu frames, %u samples per channel", o.frames, p.frames * 1024);
    check(o.adts == legacyAdts && p.hash == legacyPcm.hash, "(b) ADTS (%zu B) and PCM identical to the heap path (hash %016llx)", o.adts.size(), (unsigned long long)p.hash);
    check(mem.acquires == 1 && mem.lastBytes == kArenaAcquireBytes && !r->arenaState().exhausted() && s.reserveRequests == 0,
          "(b) one acquisition of %zu B (main %zu + reserve %zu + alignment slack); never exhausted; reserve unused", mem.lastBytes, kArenaMainBytes, kArenaReserveBytes);
    std::fprintf(stderr, "     fixture: allocs=%zu frees=%zu peakLive=%zu minMain=%zu maxRequest=%zu maxCallRequests=%zu maxCallBytes=%zu\n",
                 s.allocs, s.frees, s.peakLive, minMain(fixture, 1024), s.maxRequest, r->maxCallRequests(), r->maxCallBytes());
    check(hookClear(), "(b) hook slot empty after the track");
    // next track on the same reader: whole-arena reset, no second acquisition, no leak, identical output (CY-5 reset behaviour)
    Run o2 = feed(*r, fixture);
    check(o2.st == M4aStatus::ok && o2.adts == o.adts && mem.acquires == 1 && r->arenaState().stat().leakedAtReset == 0, "(b) second track on the same reader: identical, arena reused, leaked at reset = %zu", r->arenaState().stat().leakedAtReset);
    // 1-frame table then a full track: the CY-5 stale-cache case, now through the arena
    Run one = feed(*r, synth({100}));
    Run again = feed(*r, fixture);
    check(one.st == M4aStatus::ok && one.frames == 1 && again.adts == o.adts, "(b) 1-frame track then fixture: no stale sample-size cache");
    delete r;
    check(mem.live.empty() && mem.releases == 1 && hookClear(), "(b) destruction releases the arena (live blocks: %zu)", mem.live.size());
    // resident (no source) table in arena mode: bounded at kM4aArenaResidentEntries
    M4aGuardedReader rr; Run res = feed(rr, fixture, false);
    check(res.st == M4aStatus::tableOverflow, "(b) resident table: the 131-entry fixture exceeds the %zu-entry arena limit (%s)", kM4aArenaResidentEntries, m4aStatusName(res.st));
    { std::vector<uint32_t> few(kM4aArenaResidentEntries, 40); Bytes fs = synth(few); Run viaFile = feed(rr, fs, true), viaRes = feed(rr, fs, false);
      check(viaRes.st == M4aStatus::ok && viaRes.adts == viaFile.adts && viaRes.frames == few.size(), "(b) resident table at the limit: identical to the file-backed run"); }
    std::vector<uint32_t> ok(kM4aArenaResidentEntries, 8), over(kM4aArenaResidentEntries + 1, 8);
    Run a = feed(rr, synth(ok), false), b = feed(rr, synth(over), false);
    check(a.st == M4aStatus::ok && b.st == M4aStatus::tableOverflow, "(b) resident table at %zu entries ok, +1 -> %s", kM4aArenaResidentEntries, m4aStatusName(b.st));
    { Bytes ov = synth(over); M4aStatus cs = rr.begin([](const uint8_t*, size_t) {});
      rr.sizeTable().residentLimit(1u << 20);  // as the legacy tests do after begin()
      for (size_t i = 0; i < ov.size() && cs == M4aStatus::ok; i += 1024) cs = rr.write(ov.data() + i, std::min<size_t>(1024, ov.size() - i));
      check(cs == M4aStatus::tableOverflow, "(b) residentLimit() cannot lift the arena-mode hard limit (%s)", m4aStatusName(cs)); }
  }

  // ---- (c) acquisition failure ----
  {
    FakeMem mem; mem.fail = true; auto* r = new M4aGuardedReader; r->setArenaMemory(&mem.vt);
    Run o = feed(*r, fixture);
    check(o.beginSt == M4aStatus::noMemory && o.st == M4aStatus::noMemory && o.frames == 0 && r->status() == M4aStatus::noMemory, "(c) refused arena -> begin() = %s, no frames", m4aStatusName(o.beginSt));
    check(r->write(fixture.data(), 10) == M4aStatus::noMemory && r->finish() == M4aStatus::noMemory, "(c) write()/finish() keep returning no_memory");
    check(mem.acquires == 1 && mem.live.empty() && hookClear() && !r->arenaState().isOpen(), "(c) nothing constructed, nothing held, hook never installed");
    mem.fail = false;
    Run ok = feed(*r, fixture);
    check(ok.st == M4aStatus::ok && ok.frames == 131 && ok.adts == legacyAdts && mem.acquires == 2, "(c) same reader plays after the memory comes back");
    delete r;
    check(mem.live.empty(), "(c) no leak");
    check(std::strcmp(m4aStatusName(M4aStatus::noMemory), "no_memory") == 0, "(c) status name");
  }

  // ---- (d) forced exhaustion ----
  {
    size_t exhaustedRuns = 0, okRuns = 0, mixed = 0, maxReserve = 0, maxReserveReq = 0, firstOk = 0;
    Bytes adversarial; { std::vector<uint32_t> s; for (uint32_t i = 1; i <= 200; ++i) s.push_back(i); adversarial = synth(s); }
    Bytes jumps; { std::vector<uint32_t> s(17, 1); for (uint32_t i = 1; i <= 30; ++i) s.push_back(i); s.push_back(2041); s.push_back(1500); s.push_back(2041); jumps = synth(s, 0, 3); }
    for (const Bytes* in : {&fixture, &adversarial, &jumps})
      for (size_t mainBytes = 16; mainBytes <= 24 * 1024; mainBytes += mainBytes < 4096 ? 16 : 256) {
        FakeMem mem; auto* r = new M4aGuardedReader; r->setArenaMemory(&mem.vt); r->setArenaSizes(mainBytes, kArenaReserveBytes);
        Run o = feed(*r, *in, true, 1024, false);
        const M4aArena::Stats& s = r->arenaState().stat();
        bool exhausted = r->arenaState().exhausted();
        bool clean = ((o.st == M4aStatus::ok && !exhausted) || (o.st == M4aStatus::noMemory && exhausted)) && r->arenaState().consistent();
        if (!clean || !hookClear() || s.reserveUsed > kArenaReserveBytes) check(false, "(d) main=%zu: status=%s exhausted=%d reserveUsed=%zu", mainBytes, m4aStatusName(o.st), exhausted, s.reserveUsed);
        if (exhausted) { ++exhaustedRuns; maxReserve = std::max(maxReserve, s.reserveUsed); maxReserveReq = std::max(maxReserveReq, s.reserveRequests); if (o.frames >= (in == &fixture ? 131u : 200u)) ++mixed; }
        else { ++okRuns; if (!firstOk) firstOk = mainBytes; }
        failures += !clean;
        delete r;
        if (!mem.live.empty()) { check(false, "(d) main=%zu leaked the arena", mainBytes); }
      }
    check(exhaustedRuns > 100 && okRuns > 10, "(d) sweep of tiny main regions: %zu exhausted runs (all no_memory), %zu fitting runs (all ok); first fitting main = %zu B", exhaustedRuns, okRuns, firstOk);
    std::fprintf(stderr, "     worst exhausted run: reserve used %zu of %zu B, %zu reserve requests; runs that still delivered every frame before the flag: %zu\n", maxReserve, kArenaReserveBytes, maxReserveReq, mixed);
    // exhausted reader recovers with a normal arena on the next begin()
    FakeMem mem; M4aGuardedReader r; r.setArenaMemory(&mem.vt); r.setArenaSizes(256, kArenaReserveBytes);
    Run bad = feed(r, fixture);
    r.setArenaSizes(kArenaMainBytes, kArenaReserveBytes);
    Run good = feed(r, fixture);
    check(bad.st == M4aStatus::noMemory && good.st == M4aStatus::ok && good.adts == legacyAdts, "(d) exhausted reader plays the next track with a normal arena");
    // sticky: write() after exhaustion keeps returning no_memory and delivers nothing
    M4aGuardedReader t; t.setArenaMemory(&mem.vt); t.setArenaSizes(256, kArenaReserveBytes);
    Run x = feed(t, fixture); size_t before = x.frames;
    check(x.st == M4aStatus::noMemory && t.write(fixture.data(), 100) == M4aStatus::noMemory && before == x.frames, "(d) status stays no_memory (sticky)");
  }

  // ---- (e) hook hygiene ----
  {
    audio_tools::AllocatorHook outer{[](size_t, void*) -> void* { std::abort(); }, [](void*, void*) { std::abort(); }, nullptr};
    audio_tools::setAllocatorHook(&outer);
    M4aGuardedReader r; Run o = feed(r, fixture);
    check(o.st == M4aStatus::ok && audio_tools::allocatorHook() == &outer, "(e) a hook installed by someone else is never used by the reader and is restored after every call");
    audio_tools::setAllocatorHook(nullptr);
    // two readers: each routes only into its own arena
    M4aGuardedReader a, b; Run ra, rb; MemSource sa(fixture), sb(fixture);
    a.begin([&](const uint8_t* p, size_t n) { ++ra.frames; ra.adts.insert(ra.adts.end(), p, p + n); }, &sa);
    b.begin([&](const uint8_t* p, size_t n) { ++rb.frames; rb.adts.insert(rb.adts.end(), p, p + n); }, &sb);
    // (the vendored sample-size cache is a function static shared by all instances, so two readers cannot be fed at the same time
    // anyway; they are begun together and played one after the other)
    for (M4aGuardedReader* x : {&a, &b})
      for (size_t i = 0; i < fixture.size(); i += 512) x->write(fixture.data() + i, std::min<size_t>(512, fixture.size() - i));
    check(a.finish() == M4aStatus::ok && b.finish() == M4aStatus::ok && ra.adts == legacyAdts && rb.adts == legacyAdts && hookClear(), "(e) two readers begun together, played in turn: both identical");
    check(a.arenaState().stat().allocs == b.arenaState().stat().allocs && a.arenaState().stat().allocs > 0, "(e) each arena saw its own %zu allocations", a.arenaState().stat().allocs);
    check(audio_tools::allocatorHook() == nullptr, "(e) slot empty between calls (the only window in which it is set is inside write()/begin() on the owner task)");
  }

  // ---- (f) peak live bytes ----
  size_t maxLive = 0, maxHigh = 0 /* largest minMain */, maxReq = 0, maxCallB = 0, maxCallN = 0;
  auto measure = [&](const char* name, const Bytes& in, M4aStatus want, size_t slice, bool quiet) {
    M4aGuardedReader* r = new M4aGuardedReader;
    Run o = feed(*r, in, true, slice, false);
    const M4aArena::Stats& s = r->arenaState().stat();
    bool chain = r->arenaState().consistent();
    r->end();  // destroys the vendor objects: every block must be back
    size_t need = minMain(in, slice);
    bool ok = o.st == want && !r->arenaState().exhausted() && s.peakLive <= kM4aArenaLiveBoundBytes && need <= kArenaMainBytes && r->maxCallBytes() <= kArenaReserveBytes && s.live == 0 && chain;
    maxLive = std::max(maxLive, s.peakLive); maxHigh = std::max(maxHigh, need); maxReq = std::max(maxReq, s.maxRequest);
    maxCallB = std::max(maxCallB, r->maxCallBytes()); maxCallN = std::max(maxCallN, r->maxCallRequests());
    if (!ok || !quiet) std::fprintf(stderr, "%-4s %-40s slice=%-5zu status=%-8s peakLive=%zu minMain=%zu maxReq=%zu callBytes=%zu callReqs=%zu\n", ok ? "ok" : "FAIL", name, slice, m4aStatusName(o.st), s.peakLive, need, s.maxRequest, r->maxCallBytes(), r->maxCallRequests());
    failures += !ok; delete r;
  };
  for (size_t slice : {(size_t)7, (size_t)256, (size_t)1024, (size_t)65536, fixture.size()}) measure("fixture", fixture, M4aStatus::ok, slice, true);
  { std::vector<uint32_t> up, one, down, big, mixed;
    for (uint32_t i = 1; i <= 2041; ++i) { up.push_back(i); down.push_back(2042 - i); }
    one.assign(3000, 1); big.assign(40, 2041);
    for (uint32_t i = 0; i < 1500; ++i) mixed.push_back(i % 2 ? 2041 : 1 + i % 60);
    for (size_t slice : {(size_t)7, (size_t)256, (size_t)65536, (size_t)1 << 22}) {
      measure("adversarial: sizes 1..2041 ascending", synth(up), M4aStatus::ok, slice, true);
      measure("adversarial: sizes 2041..1 descending", synth(down), M4aStatus::ok, slice, true);
      measure("adversarial: 3000 one-byte frames", synth(one), M4aStatus::ok, slice, true);
      measure("adversarial: 40 max-size frames", synth(big), M4aStatus::ok, slice, true);
      measure("adversarial: alternating tiny/max", synth(mixed), M4aStatus::ok, slice, true);
      measure("stsd payload at cap (1024 B)", synth(mixed, kM4aMaxStsdBytes - 80), M4aStatus::ok, slice, true);
      measure("container depth at cap (8)", synth(up, 0, 3), M4aStatus::ok, slice, true);
      measure("stsd at cap + depth 8 + ascending", synth(up, kM4aMaxStsdBytes - 80, 3), M4aStatus::ok, slice, true);
    } }
  { uint32_t seed = 12345; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };  // random frame-size streams
    for (int k = 0, nRandom = std::getenv("CYDPOD_ARENA_RANDOM") ? std::atoi(std::getenv("CYDPOD_ARENA_RANDOM")) : 40; k < nRandom; ++k) {
      std::vector<uint32_t> s; size_t n = 300 + rnd() % 1500; uint32_t maxSz = 1 + rnd() % 2041;
      for (size_t i = 0; i < n; ++i) s.push_back(1 + rnd() % maxSz);
      measure("random frame sizes", synth(s, rnd() % 2 ? kM4aMaxStsdBytes - 80 : 0, rnd() % 4), M4aStatus::ok, (size_t)1 << (rnd() % 12), true);
    } }
  std::fprintf(stderr, "     synthetic + fixture maxima: peakLive=%zu minMain=%zu largest request=%zu; one vendor call: %zu requests, %zu B\n", maxLive, maxHigh, maxReq, maxCallN, maxCallB);

  // ---- (g) mechanical reserve bound and fallback disjointness ----
  // The reserve must cover the bytes ONE vendor call can request, with margin. The vendor is fed the mdat payload in kM4aVendorSliceBytes
  // slices, so the worst call is a slice in which the frame buffer and the ADTS scratch both jump to the largest size on top of a chain of
  // strictly growing small frames. Place that slice at every alignment (0..slice-1 tiny frames before it) and measure the real call totals.
  {
    size_t worstCall = 0, runs = 0; bool allOk = true;
    for (size_t pad = 0; pad < kM4aVendorSliceBytes; ++pad) {
      std::vector<uint32_t> s(pad, 1);
      for (uint32_t i = 1; i <= 30; ++i) s.push_back(i);                      // growing chain: scratch grows at every frame
      s.push_back(2041); for (uint32_t i = 1; i <= 30; ++i) s.push_back(i);  // both buffers jump to the maximum, then small frames again
      s.push_back(1500); s.push_back(2041); s.push_back(1025); s.push_back(2041);
      for (size_t chunk : {(size_t)1024, (size_t)65536}) {
        M4aGuardedReader r; Run o = feed(r, synth(s), true, chunk, false);
        worstCall = std::max(worstCall, r.maxCallBytes()); ++runs;
        allOk = allOk && o.st == M4aStatus::ok && !r.arenaState().exhausted() && r.maxCallBytes() <= kArenaReserveBytes;
      }
    }
    check(allOk, "(g) %zu slice-alignment runs: worst single vendor call %zu B (with headers) <= reserve %zu B", runs, worstCall, kArenaReserveBytes);
    check(maxCallB <= kArenaReserveBytes && worstCall <= kArenaReserveBytes, "(g) fixture, adversarial and random streams: one vendor call <= %zu B (reserve %zu B, margin %zu B)", std::max(maxCallB, worstCall), kArenaReserveBytes, kArenaReserveBytes - std::max(maxCallB, worstCall));
    // allocator model test: random alloc/free (small, class-size and oversize requests) against a main region too small for them, so the
    // fallback is exercised; every grant is checked by address range: reserve grants never overlap each other or any main block, main
    // blocks never overlap each other, all stay inside the single block, and a request beyond the reserve is the only null.
    struct Rng { const uint8_t* p; size_t n; bool res; };
    for (size_t mainBytes : {(size_t)1024, (size_t)4096, kArenaMainBytes}) {
      FakeMem mem; M4aArena a; a.open(&mem.vt, mainBytes, 6144);
      const uint8_t* lo = a.regionBase(); const uint8_t* hi = lo + mainBytes + 6144;
      std::vector<Rng> live; std::vector<void*> order; uint32_t seed = 99 + (uint32_t)mainBytes; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
      size_t bad = 0, nulls = 0, grants = 0, reserveGrants = 0;
      for (int step = 0; step < 20000; ++step) {
        if (step % 500 == 499) { a.reset(); live.clear(); }  // a new track: the reserve starts again from its first byte
        if (live.empty() || rnd() % 5 < 3) {
          size_t n = rnd() % 4 == 0 ? 513 + rnd() % 1544 : rnd() % 4 == 1 ? 1 + rnd() % 3000 : 1 + rnd() % 400;
          void* p = a.allocate(n);
          if (!p) { ++nulls; if (a.stat().reserveUsed + ((n + 7) & ~size_t(7)) <= 6144) ++bad; continue; }  // null only when the reserve cannot take it
          const uint8_t* b = static_cast<uint8_t*>(p); bool res = b >= lo + mainBytes;
          ++grants; reserveGrants += res;
          if (b < lo || b + n > hi || (res ? b < lo + mainBytes : b + n > lo + mainBytes) || ((uintptr_t)b & 7)) ++bad;
          for (const Rng& r : live) if (b < r.p + r.n && r.p < b + n) ++bad;
          live.push_back({b, n, res});
          std::memset(p, 0x5A, n);
        } else {
          size_t i = rnd() % live.size();
          a.release(const_cast<uint8_t*>(live[i].p));
          if (!live[i].res) live.erase(live.begin() + i);  // a reserve block stays reserved (never reused) until reset
        }
        if (!a.consistent()) ++bad;
      }
      check(bad == 0 && grants > 1000, "(g) allocator model, main %zu B: %zu grants (%zu from the reserve), %zu refusals beyond the reserve, %zu violations", mainBytes, grants, reserveGrants, nulls, bad);
      a.close();
    }
  }

  const char* folder = argc > 2 ? argv[2] : std::getenv("CYDPOD_REAL_MEDIA");
  if (!folder || !*folder || !std::filesystem::is_directory(folder)) {
    std::fprintf(stderr, "SKIP (f): real-media folder not present; other parts %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 77;
  }
  std::map<std::string, size_t> expect;
  { std::ifstream in("tests/real_media_expected.tsv"); std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      std::istringstream row(line); std::string name, st, n; std::getline(row, name, '\t'); std::getline(row, st, '\t'); std::getline(row, n, '\t');
      expect[name] = st == "ok" ? std::stoul(n) : 0; } }
  size_t files = 0, realLive = 0, realHigh = 0, realCallB = 0, realCallN = 0, minLive = SIZE_MAX;
  for (auto& e : std::filesystem::directory_iterator(folder)) {
    if (e.path().extension() != ".m4a") continue;
    std::string name = e.path().filename().string(); Bytes in = readFile(e.path().string());
    for (size_t slice : {(size_t)256, (size_t)1024}) {
      M4aGuardedReader* r = new M4aGuardedReader; Run o = feed(*r, in, true, slice, slice == 1024);
      const M4aArena::Stats& s = r->arenaState().stat();
      bool ok = o.st == M4aStatus::ok && o.frames == expect[name] && !r->arenaState().exhausted() && s.peakLive <= kM4aArenaLiveBoundBytes && r->maxCallBytes() <= kArenaReserveBytes;
      if (slice == 1024) {
        Pcm p = decode(o.adts); ok = ok && p.ok && p.frames == o.frames;
        M4aGuardedReader legacy; legacy.setArenaMemory(nullptr); Run l = feed(legacy, in);
        ok = ok && l.adts == o.adts;
        check(ok, "(f) %s frames=%zu peakLive=%zu minMain=%zu callBytes=%zu callReqs=%zu == heap path", name.c_str(), o.frames, s.peakLive, slice == 1024 ? minMain(in, slice) : 0, r->maxCallBytes(), r->maxCallRequests());
      } else failures += !ok;
      realLive = std::max(realLive, s.peakLive); realHigh = std::max(realHigh, slice == 1024 ? minMain(in, slice) : size_t(0)); minLive = std::min(minLive, s.peakLive);
      realCallB = std::max(realCallB, r->maxCallBytes()); realCallN = std::max(realCallN, r->maxCallRequests());
      delete r;
    }
    ++files;
  }
  check(files == 25 && files == expect.size(), "(f) %zu real files through the arena (want 25)", files);
  std::fprintf(stderr, "     real files: peak live %zu..%zu B, min main <= %zu B, one vendor call <= %zu requests / %zu B\n", minLive, realLive, realHigh, realCallN, realCallB);
  std::fprintf(stderr, failures ? "FAILED %d\n" : "PASS arena\n", failures);
  return failures != 0;
}
