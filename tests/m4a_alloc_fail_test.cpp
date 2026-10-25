// CY-10b3 allocation-failure sweep. A counting run records every allocation event the reader can make while it begins, plays and is
// destroyed: the arena acquisition (M4aArenaMemory), every vendored-allocator request (the arena hook) and anything that bypasses both
// (global operator new / malloc family, counted by wrappers). Then, for every k in 1..N, the k-th event fails and the run must
//   - return a status (no abort, no crash, no exception): noMemory for an accepted input, the rejection status or noMemory for a rejected one;
//     the status is sticky (more write()/finish() keep returning it);
//   - give everything back: arena live bytes 0, no block of the memory source left, no hook grant left, no bypassing allocation left;
//   - never hand out overlapping memory (every grant, main and reserve, is audited by address range, and stays inside the arena block);
//   - leave a reader that plays the next valid track identically, both the same reader (arena kept, and after end()) and a fresh one.
// Two failure modes per k: only the k-th event fails ("single"), and the k-th and every later one fails ("sticky"). A hook allocation
// that "fails" is refused by the main region and served from the reserve (the design: the hook never returns null), which raises the
// exhaustion flag; an acquisition failure returns null from the memory source; a bypassing allocation (none expected) returns null / throws.
// usage: m4a_alloc_fail_test fixture.m4a moov-last.m4a [real-file.m4a]   (no real file: that input is skipped, exit 77 if all else passed)
#include <Arduino.h>
#include <csignal>
#include <cstdarg>
#include <exception>
#include <new>
#include <unistd.h>
#include <vector>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include "m4a_file_source.h"
#include "../firmware/cydpod_probe/adts_decoder.h"
using namespace cydpod;
using Bytes = std::vector<uint8_t>;
extern "C" void* helix_malloc(int size) { return std::malloc(size); }
extern "C" void helix_free(void* p) { std::free(p); }

#ifndef CYDPOD_ALLOCATOR_HOOK
#error "m4a_alloc_fail_test needs the allocator-hook overlay (tests/apply_audiotools_overlay.sh)"
#endif

// ---- event counting and fault injection ----
extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t, size_t); void* __real_realloc(void*, size_t); void __real_free(void*);
}
static bool g_window;                         // true only while the reader runs (never while the test itself allocates)
static size_t g_event, g_failAt;              // events seen in this run; the one that fails (0 = none)
static bool g_sticky;                         // fail g_failAt and every later event
static size_t g_global, g_globalFree;         // bypassing allocations / frees inside the window
static char g_kinds[8192]; static size_t g_kindCount;  // kind of each event of the counting run: A acquisition, H hook, G global (fixed storage: recording must not allocate)
static bool takeEvent(char kind) {
  if (!g_window) return false;
  ++g_event; if (g_event <= sizeof g_kinds) { g_kinds[g_event - 1] = kind; if (g_event > g_kindCount) g_kindCount = g_event; }
  return g_failAt && (g_sticky ? g_event >= g_failAt : g_event == g_failAt);
}
extern "C" {
void* __wrap_malloc(size_t n) { if (g_window) { ++g_global; if (takeEvent('G')) return nullptr; } return __real_malloc(n); }
void* __wrap_calloc(size_t a, size_t b) { if (g_window) { ++g_global; if (takeEvent('G')) return nullptr; } return __real_calloc(a, b); }
void* __wrap_realloc(void* o, size_t n) { if (g_window) { ++g_global; if (takeEvent('G')) return nullptr; } return __real_realloc(o, n); }
void __wrap_free(void* p) { if (p && g_window) ++g_globalFree; __real_free(p); }
}
void* operator new(size_t n) {
  if (g_window) { ++g_global; if (takeEvent('G')) throw std::bad_alloc(); }
  void* p = __real_malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { if (p && g_window) ++g_globalFree; __real_free(p); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }

// Fixed-capacity container: the harness must not allocate while the reader runs, or it would perturb the very events being counted.
template <class T, size_t N> struct Fixed {
  T a[N]; size_t n = 0;
  void push_back(const T& v) { if (n == N) { std::fputs("harness capacity exceeded\n", stderr); std::abort(); } a[n++] = v; }
  void erase_at(size_t i) { for (; i + 1 < n; ++i) a[i] = a[i + 1]; --n; }
  size_t size() const { return n; } bool empty() const { return !n; } void clear() { n = 0; }
  T& operator[](size_t i) { return a[i]; } const T& operator[](size_t i) const { return a[i]; }
  T* begin() { return a; } T* end() { return a + n; } const T* begin() const { return a; } const T* end() const { return a + n; }
};

static int failures;
static char g_ctx[200];
static void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(bool ok, const char* fmt, ...) {
  char b[400]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
  if (!ok) { std::fprintf(stderr, "FAIL [%s] %s\n", g_ctx, b); ++failures; }
}
static void onSignal(int s) {  // a crash or abort is a failure; say where
  char b[300]; int n = std::snprintf(b, sizeof b, "\nFAIL crash/abort (signal %d) at [%s]\n", s, g_ctx);
  (void)!write(2, b, n); _exit(86);
}

// ---- memory source for the arena: counts as an event, hands out blocks that are only 4-byte aligned like heap_caps_malloc ----
struct FakeMem {
  M4aArenaMemory vt{[](size_t n, void* c) -> void* { return static_cast<FakeMem*>(c)->acquire(n); },
                    [](void* p, void* c) { static_cast<FakeMem*>(c)->release(p); }, this};
  int acquires = 0, releases = 0; Fixed<void*, 8> live;
  void* acquire(size_t n) {
    ++acquires;
    if (takeEvent('A')) return nullptr;
    uint8_t* raw = static_cast<uint8_t*>(__real_malloc(n + 16)); if (!raw) return nullptr;
    live.push_back(raw);
    return raw + 4;  // 4 mod 8 whenever malloc returns 16-aligned memory
  }
  void release(void* p) {
    ++releases; void* raw = static_cast<uint8_t*>(p) - 4;
    for (size_t i = 0; i < live.size(); ++i) if (live[i] == raw) { live.erase_at(i); __real_free(raw); return; }
    check(false, "release of a block this source did not hand out");
  }
};

// ---- audit of every grant by address range ----
struct Audit {
  struct Rng { const uint8_t* p; size_t n; bool reserve; };
  Fixed<Rng, 256> live;              // main blocks currently granted, and every reserve grant (reserve blocks are never reused)
  size_t grants = 0, reserveGrants = 0, overlaps = 0, outside = 0, badFree = 0;
  void dropReserve() { for (size_t i = live.size(); i-- > 0;) if (live[i].reserve) live.erase_at(i); }  // the arena reuses the reserve from its start after a reset
  M4aArena::Probe probe{
      [](size_t, void*) { return takeEvent('H'); },
      [](const void* p, size_t n, bool reserve, void* c) { static_cast<Audit*>(c)->granted(static_cast<const uint8_t*>(p), n, reserve); },
      [](const void* p, bool reserve, void* c) { static_cast<Audit*>(c)->freed(static_cast<const uint8_t*>(p), reserve); }, this};
  const M4aArena* arena = nullptr;
  void granted(const uint8_t* p, size_t n, bool reserve) {
    ++grants; reserveGrants += reserve;
    const uint8_t* b = arena->regionBase();
    size_t mainEnd = arena->mainBytes();
    if (p < b || p + n > b + mainEnd + arena->reserveBytes() || (reserve ? p < b + mainEnd : p + n > b + mainEnd)) ++outside;
    for (const Rng& r : live) if (p < r.p + r.n && r.p < p + n) ++overlaps;
    live.push_back({p, n, reserve});
  }
  void freed(const uint8_t* p, bool reserve) {
    if (reserve) return;  // a reserve block is never reused: its range stays reserved until the arena is reset
    for (size_t i = 0; i < live.size(); ++i) if (live[i].p == p && !live[i].reserve) { live.erase_at(i); return; }
    ++badFree;
  }
  size_t mainLive() const { size_t k = 0; for (const Rng& r : live) k += !r.reserve; return k; }
};

// ---- playing a track ----
struct Out {
  M4aStatus st = M4aStatus::ok; size_t frames = 0, bytes = 0; uint64_t hash = 14695981039346656037ull; bool threw = false;
  bool operator==(const Out& o) const { return st == o.st && frames == o.frames && bytes == o.bytes && hash == o.hash; }
};
static void hashIn(Out& o, const uint8_t* p, size_t n) { ++o.frames; o.bytes += n; for (size_t i = 0; i < n; ++i) o.hash = (o.hash ^ p[i]) * 1099511628211ull; }
// Runs begin/write.../finish on `r`. The callback only hashes (no allocation), so every event in the window belongs to the reader.
static Out play(M4aGuardedReader& r, const Bytes& in, size_t chunk, Bytes* keep = nullptr) {
  struct Sink { Out o; Bytes* keep; } sink{Out(), keep}; Out& o = sink.o; MemSource src(in);
  // one captured pointer: the std::function stays in its small buffer (as in the firmware) and the harness itself makes no allocation in the window
  M4aGuardedReader::AdtsCallback cb = [s = &sink](const uint8_t* p, size_t n) { hashIn(s->o, p, n); if (s->keep) s->keep->insert(s->keep->end(), p, p + n); };
  try {
    o.st = r.begin(cb, &src);
    for (size_t i = 0; i < in.size() && o.st == M4aStatus::ok; i += chunk) o.st = r.write(in.data() + i, std::min(chunk, in.size() - i));
    if (o.st == M4aStatus::ok) o.st = r.finish();
  } catch (...) { o.threw = true; }
  return sink.o;
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

struct Input { const char* name; const Bytes* data; M4aStatus want; };
struct Reference { Out out; Bytes adts; Pcm pcm; bool havePcm = false; };
static const Bytes* g_valid;  // the valid track the warm-up plays
static Reference g_validRef;

static void verifyRecovery(M4aGuardedReader& r, const Reference& ref, const Bytes& valid, const char* what, bool pcm) {
  g_failAt = 0; g_window = false;
  Bytes adts; Out o = play(r, valid, 1024, pcm ? &adts : nullptr);
  check(o == ref.out, "%s: next valid track differs (status %s, %zu frames vs %zu)", what, m4aStatusName(o.st), o.frames, ref.out.frames);
  if (pcm && ref.havePcm) { Pcm p = decode(adts); check(p.ok && p.frames == ref.pcm.frames && p.hash == ref.pcm.hash, "%s: PCM of the next valid track differs", what); }
}
static void leakCheck(M4aGuardedReader& r, FakeMem& mem, Audit& a, const char* what) {
  const M4aArena::Stats& s = r.arenaState().stat();
  check(s.live == 0, "%s: %zu arena bytes still live after destruction", what, s.live);
  check(mem.live.empty(), "%s: memory source still holds %zu block(s)", what, mem.live.size());
  check(a.mainLive() == 0, "%s: %zu hook grant(s) never released", what, a.mainLive());
  check(a.overlaps == 0 && a.outside == 0 && a.badFree == 0, "%s: audit overlaps=%zu outside=%zu badFree=%zu", what, a.overlaps, a.outside, a.badFree);
  check(g_global == g_globalFree, "%s: %zu bypassing allocation(s) vs %zu free(s)", what, g_global, g_globalFree);
}

struct Counts { size_t events = 0, acquisitions = 0, hook = 0, global = 0, reserveGrants = 0, runs = 0, noMemory = 0, rejected = 0; };
// One run. k == 0: counting run (nothing fails). `warm` plays the valid track first on the same reader, so the failure is in a second
// track on a reader whose arena is kept. After the faulted run the same reader is reused without end(), then after end(), then a fresh
// reader is used; each must play the valid track identically.
static void runCase(const Input& in, const Reference& inRef, size_t chunk, size_t k, bool sticky, bool warm, bool pcm, Counts& c) {
  std::snprintf(g_ctx, sizeof g_ctx, "%s chunk=%zu k=%zu %s%s", in.name, chunk, k, sticky ? "sticky" : "single", warm ? " warm" : "");
  FakeMem mem; Audit audit;
  auto* r = new M4aGuardedReader; audit.arena = &r->arenaState();
  r->setArenaMemory(&mem.vt); r->setArenaProbe(&audit.probe);
  g_global = g_globalFree = 0; g_kindCount = 0;
  if (warm) verifyRecovery(*r, g_validRef, *g_valid, "warm-up track", false);
  audit.dropReserve();
  g_event = 0; g_failAt = k; g_sticky = sticky; g_window = true;
  Out o = play(*r, *in.data, chunk);
  M4aStatus again = M4aStatus::ok, fin = M4aStatus::ok;
  if (o.st != M4aStatus::ok) { try { again = r->write(in.data->data(), 1); fin = r->finish(); } catch (...) { o.threw = true; } }
  g_window = false;
  const size_t events = g_event;
  if (!k) {
    c.events = events; c.runs = 0;
    c.acquisitions = c.hook = c.global = 0;
    for (size_t i = 0; i < g_kindCount; ++i) { c.acquisitions += g_kinds[i] == 'A'; c.hook += g_kinds[i] == 'H'; c.global += g_kinds[i] == 'G'; }
    check(o == inRef.out && !o.threw, "counting run: status %s, %zu frames (reference %s, %zu)", m4aStatusName(o.st), o.frames, m4aStatusName(inRef.out.st), inRef.out.frames);
  } else {
    ++c.runs; c.reserveGrants += audit.reserveGrants;
    check(!o.threw, "an exception escaped the reader");
    check(events >= k, "event %zu was never reached (run saw %zu): not deterministic", k, events);
    if (in.want == M4aStatus::ok) check(o.st == M4aStatus::noMemory, "accepted input returned %s, want no_memory", m4aStatusName(o.st));
    else check(o.st == in.want || o.st == M4aStatus::noMemory, "rejected input returned %s, want %s or no_memory", m4aStatusName(o.st), m4aStatusName(in.want));
    c.noMemory += o.st == M4aStatus::noMemory; c.rejected += o.st == in.want;
    check(again == o.st && fin == o.st, "status not sticky: write()=%s finish()=%s after %s", m4aStatusName(again), m4aStatusName(fin), m4aStatusName(o.st));
    if (o.st == M4aStatus::noMemory) check(o.frames <= inRef.out.frames, "more frames than a clean run");
  }
  // recovery 1: same reader, arena kept open (begin() resets it)
  if (k) { audit.dropReserve(); verifyRecovery(*r, inRef, *in.data, "same reader (arena kept)", pcm); }
  // end(): vendor objects destroyed under the hook, arena handed back
  g_window = true; r->end(); g_window = false;
  leakCheck(*r, mem, audit, "after end()");
  // recovery 2: same reader, arena acquired again
  if (k) {
    audit.dropReserve();
    verifyRecovery(*r, inRef, *in.data, "same reader after end()", pcm);
    r->end(); leakCheck(*r, mem, audit, "after recovery end()");
  }
  delete r;
  // recovery 3: a fresh reader with a fresh memory source
  if (k) {
    FakeMem mem2; Audit audit2; auto* f = new M4aGuardedReader; audit2.arena = &f->arenaState();
    f->setArenaMemory(&mem2.vt); f->setArenaProbe(&audit2.probe);
    verifyRecovery(*f, inRef, *in.data, "fresh reader", pcm);
    f->end(); leakCheck(*f, mem2, audit2, "fresh reader end()");
    delete f;
  }
  g_failAt = 0;
}

static Reference reference(const Input& in, bool pcm) {
  Reference ref; FakeMem mem; Audit audit; M4aGuardedReader r; audit.arena = &r.arenaState();
  r.setArenaMemory(&mem.vt); r.setArenaProbe(&audit.probe); g_window = false; g_failAt = 0;
  ref.out = play(r, *in.data, 1024, &ref.adts);
  if (pcm && ref.out.st == M4aStatus::ok) { ref.pcm = decode(ref.adts); ref.havePcm = ref.pcm.ok; }
  r.end(); ref.adts.clear(); ref.adts.shrink_to_fit();
  return ref;
}

static size_t sweep(const Input& in, const Reference& ref, size_t chunk, bool warm, bool pcmEvery, const char* label) {
  Counts c; runCase(in, ref, chunk, 0, false, warm, false, c);
  const size_t n = c.events;
  std::fprintf(stderr, "%-10s %-22s N=%zu allocation events (%zu acquisition, %zu hook, %zu bypassing)\n", in.name, label, n, c.acquisitions, c.hook, c.global);
  check(n > 0 && c.global == 0, "%s %s: N=%zu, bypassing allocations=%zu (want N>0, none)", in.name, label, n, c.global);
  const int before = failures;
  for (bool sticky : {false, true})
    for (size_t k = 1; k <= n; ++k) {
      bool pcm = pcmEvery || k == 1 || k == n || k == n / 2;
      runCase(in, ref, chunk, k, sticky, warm, pcm, c);
    }
  std::fprintf(stderr, "%-10s %-22s %zu faulted runs (single + sticky, k = 1..%zu): %zu no_memory, %zu rejected, %zu reserve grants audited: %s\n",
               in.name, label, c.runs, n, c.noMemory, c.rejected, c.reserveGrants, failures == before ? "ok" : "FAILED");
  return n;
}

// Acquisition failure in the begin() of a second track on the same reader, then recovery.
static void secondTrackAcquisition(const Input& fx, const Reference& ref) {
  std::snprintf(g_ctx, sizeof g_ctx, "second-track acquisition");
  FakeMem mem; Audit audit; M4aGuardedReader r; audit.arena = &r.arenaState();
  r.setArenaMemory(&mem.vt); r.setArenaProbe(&audit.probe);
  verifyRecovery(r, ref, *fx.data, "track 1", true);
  r.end(); check(mem.live.empty() && mem.releases == 1, "track 1 released the arena");
  audit.dropReserve();
  g_event = 0; g_failAt = 1; g_sticky = false; g_window = true;
  Out o = play(r, *fx.data, 1024);
  M4aStatus w = r.write(fx.data->data(), 16), f = r.finish();
  g_window = false; g_failAt = 0;
  check(o.st == M4aStatus::noMemory && w == M4aStatus::noMemory && f == M4aStatus::noMemory && o.frames == 0 && mem.live.empty(), "acquisition refused in begin() of track 2: %s", m4aStatusName(o.st));
  verifyRecovery(r, ref, *fx.data, "track 3 after the refusal", true);
  r.end(); leakCheck(r, mem, audit, "second-track acquisition");
}

static Bytes readFile(const char* path) {
  Bytes out; FILE* f = std::fopen(path, "rb"); if (!f) return out;
  uint8_t b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + n);
  std::fclose(f); return out;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: m4a_alloc_fail_test fixture.m4a moov-last.m4a [real.m4a]\n"); return 2; }
  for (int s : {SIGABRT, SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGALRM}) std::signal(s, onSignal);
  alarm(1500);  // a reader that hangs (the vendor stop() path) is a failure too
  Bytes fixture = readFile(argv[1]), moovLast = readFile(argv[2]), real = argc > 3 ? readFile(argv[3]) : Bytes();
  if (fixture.empty() || moovLast.empty()) { std::fprintf(stderr, "cannot read the fixtures\n"); return 2; }
  g_valid = &fixture;
  Input fx{"fixture", &fixture, M4aStatus::ok}, ml{"moov-last", &moovLast, M4aStatus::moovAfterMdat}, rl{"real-file", &real, M4aStatus::ok};
  Reference fxRef = reference(fx, true);
  check(fxRef.out.st == M4aStatus::ok && fxRef.out.frames == 131 && fxRef.havePcm && fxRef.pcm.frames * 1024 == 134144, "reference fixture run: %s, %zu frames", m4aStatusName(fxRef.out.st), fxRef.out.frames);
  g_validRef = fxRef;
  Reference mlRef = reference(ml, false);
  check(mlRef.out.st == M4aStatus::moovAfterMdat, "reference moov-last run: %s", m4aStatusName(mlRef.out.st));
  secondTrackAcquisition(fx, fxRef);

  size_t total = 0;
  total += sweep(fx, fxRef, 256, false, true, "256-byte chunks");
  total += sweep(fx, fxRef, 4096, false, true, "4096-byte chunks");
  total += sweep(fx, fxRef, 256, true, true, "second track (warm)");
  total += sweep(ml, mlRef, 256, false, false, "256-byte chunks");
  total += sweep(ml, mlRef, 4096, false, false, "4096-byte chunks");
  bool skipped = real.empty();
  if (!skipped) {
    Reference rlRef = reference(rl, false);
    check(rlRef.out.st == M4aStatus::ok && rlRef.out.frames > 1000, "reference real file: %s, %zu frames", m4aStatusName(rlRef.out.st), rlRef.out.frames);
    total += sweep(rl, rlRef, 256, false, false, "256-byte chunks");
  } else std::fprintf(stderr, "SKIP real-file sweep: no file given\n");
  g_failAt = 0;
  if (failures) std::fprintf(stderr, "FAILED %d (swept %zu events)\n", failures, total);
  else std::fprintf(stderr, "PASS alloc-fail sweep (%zu events swept, each in single and sticky mode)\n", total);
  return failures ? 1 : skipped ? 77 : 0;
}
