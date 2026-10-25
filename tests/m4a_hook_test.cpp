// CY-10b1/b2: with the allocator-hook overlay (patches/audio-tools/0001+0002) every allocation made while constructing, running and
// destroying an M4aGuardedReader goes through the hook -- since CY-10b2 the reader installs its own arena hook for each vendor call.
// A global operator new / malloc-family counter (link with -Wl,--wrap=malloc,calloc,realloc,free) sees what bypasses it. The hook-less
// (legacy heap) run is the baseline: its global count must equal the arena's allocation count, and the arena run must show none.
#include <Arduino.h>
#include <new>
#include <vector>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include "m4a_file_source.h"
using namespace cydpod;

extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t, size_t); void* __real_realloc(void*, size_t); void __real_free(void*);
static bool g_count; static size_t g_global, g_globalFree;
void* __wrap_malloc(size_t n) { g_global += g_count; return __real_malloc(n); }
void* __wrap_calloc(size_t a, size_t b) { g_global += g_count; return __real_calloc(a, b); }
void* __wrap_realloc(void* o, size_t n) { g_global += g_count; return __real_realloc(o, n); }
void __wrap_free(void* p) { if (p) g_globalFree += g_count; __real_free(p); }
}
void* operator new(size_t n) { void* p = std::malloc(n); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

// The arena's one backing block is static storage here, so the arena run makes no global allocation of its own.
alignas(16) static unsigned char g_arenaMem[kM4aArenaMainBytes + kM4aArenaReserveBytes];
static const M4aArenaMemory g_staticMem{[](size_t n, void*) -> void* { return n <= sizeof g_arenaMem ? g_arenaMem : nullptr; }, [](void*, void*) {}, nullptr};

struct Run { size_t frames = 0, adtsBytes = 0, global = 0, globalFree = 0, allocs = 0, frees = 0, live = 0, bytesPeak = 0; M4aStatus st = M4aStatus::ok; bool exhausted = false; };
alignas(M4aGuardedReader) static unsigned char g_storage[sizeof(M4aGuardedReader)];  // constructed in place: no operator new for the object

static Run run(const std::vector<uint8_t>& file, bool arena) {
  static Run out; out = Run();
  MemSource src(file);
  g_global = g_globalFree = 0; g_count = true;
  auto* r = new (g_storage) M4aGuardedReader;
  r->setArenaMemory(arena ? &g_staticMem : nullptr);
  r->begin([](const uint8_t*, size_t) {}, &src);  // begin() rebuilds the demuxer in place; the second call covers that path too
  static Run* cur; cur = &out;
  r->begin([](const uint8_t*, size_t n) { ++cur->frames; cur->adtsBytes += n; }, &src);
  for (size_t i = 0; i < file.size() && out.st == M4aStatus::ok; i += 1024)
    out.st = r->write(file.data() + i, std::min<size_t>(1024, file.size() - i));
  if (out.st == M4aStatus::ok) out.st = r->finish();
  const M4aArena::Stats s = r->arenaState().stat();
  r->end();  // vendor objects destroyed under the hook, then the arena released
  const M4aArena::Stats e = r->arenaState().stat();
  out.allocs = e.allocs; out.frees = e.frees; out.live = e.live; out.bytesPeak = s.peakLive; out.exhausted = r->arenaState().exhausted();
  r->~M4aGuardedReader();
  g_count = false; out.global = g_global; out.globalFree = g_globalFree;
  return out;
}

int main() {
  std::vector<uint8_t> file;
  FILE* f = std::fopen("fixtures/stereo-faststart.m4a", "rb"); if (!f) { std::fprintf(stderr, "no fixture\n"); return 2; }
  uint8_t b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) file.insert(file.end(), b, b + n); std::fclose(f);
  Run plain = run(file, false), arena = run(file, true);
  std::printf("heap (no hook): status=%s frames=%zu adts=%zu global allocs=%zu frees=%zu\n", m4aStatusName(plain.st), plain.frames,
              plain.adtsBytes, plain.global, plain.globalFree);
  std::printf("arena:          status=%s frames=%zu adts=%zu arena allocs=%zu frees=%zu live=%zu peak live=%zu; bypassing global allocs=%zu frees=%zu\n",
              m4aStatusName(arena.st), arena.frames, arena.adtsBytes, arena.allocs, arena.frees, arena.live, arena.bytesPeak, arena.global, arena.globalFree);
  bool ok = plain.st == M4aStatus::ok && arena.st == M4aStatus::ok && plain.frames > 0 && arena.frames == plain.frames &&
            arena.adtsBytes == plain.adtsBytes && arena.allocs > 0 && arena.global == 0 && arena.globalFree == 0 && !arena.exhausted &&
            arena.allocs == plain.global && arena.frees == plain.globalFree && arena.live == 0;
  std::printf("%s\n", ok ? "PASS every reader allocation went through the arena hook" : "FAIL");
  return ok ? 0 : 1;
}
