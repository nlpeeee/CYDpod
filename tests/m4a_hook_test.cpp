// CY-10b1: with the allocator-hook overlay (patches/audio-tools/0001) every allocation made while constructing, running and
// destroying an M4aGuardedReader goes through the hook. A global operator new / malloc-family counter (link with
// -Wl,--wrap=malloc,calloc,realloc,free) sees what bypasses it; the hook's own backing store calls __real_calloc, so the
// global counters measure bypass only. Compared with a hook-less run, whose global count must equal the hooked run's hook count.
#include <Arduino.h>
#include <new>
#include <vector>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
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

struct Counts { size_t allocs = 0, frees = 0, bytes = 0, live = 0; };
static void* hookAlloc(size_t n, void* ctx) {
  auto* c = static_cast<Counts*>(ctx); ++c->allocs; c->bytes += n; ++c->live; return __real_calloc(1, n);
}
static void hookFree(void* p, void* ctx) { auto* c = static_cast<Counts*>(ctx); ++c->frees; --c->live; __real_free(p); }

struct Run { size_t frames = 0, adtsBytes = 0, global = 0, globalFree = 0; Counts hook; M4aStatus st = M4aStatus::ok; };
alignas(M4aGuardedReader) static unsigned char g_storage[sizeof(M4aGuardedReader)];  // constructed in place: no operator new for the object

static Run run(const std::vector<uint8_t>& file, bool hooked) {
  static Run out; out = Run();
  audio_tools::AllocatorHook hook{hookAlloc, hookFree, &out.hook};
  if (hooked) audio_tools::setAllocatorHook(&hook);
  g_global = g_globalFree = 0; g_count = true;
  auto* r = new (g_storage) M4aGuardedReader;
  r->begin([](const uint8_t*, size_t) {});  // begin() rebuilds the demuxer in place; the second call covers that path too
  static Run* cur; cur = &out;
  r->begin([](const uint8_t*, size_t n) { ++cur->frames; cur->adtsBytes += n; });
  for (size_t i = 0; i < file.size() && out.st == M4aStatus::ok; i += 1024)
    out.st = r->write(file.data() + i, std::min<size_t>(1024, file.size() - i));
  if (out.st == M4aStatus::ok) out.st = r->finish();
  r->~M4aGuardedReader();
  g_count = false; out.global = g_global; out.globalFree = g_globalFree;
  audio_tools::setAllocatorHook(nullptr);
  return out;
}

int main() {
  std::vector<uint8_t> file;
  FILE* f = std::fopen("fixtures/stereo-faststart.m4a", "rb"); if (!f) { std::fprintf(stderr, "no fixture\n"); return 2; }
  uint8_t b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) file.insert(file.end(), b, b + n); std::fclose(f);
  Run plain = run(file, false), hooked = run(file, true);
  std::printf("unhooked: status=%s frames=%zu adts=%zu global allocs=%zu frees=%zu\n", m4aStatusName(plain.st), plain.frames,
              plain.adtsBytes, plain.global, plain.globalFree);
  std::printf("hooked:   status=%s frames=%zu adts=%zu hook allocs=%zu frees=%zu live=%zu bytes=%zu; bypassing global allocs=%zu frees=%zu\n",
              m4aStatusName(hooked.st), hooked.frames, hooked.adtsBytes, hooked.hook.allocs, hooked.hook.frees, hooked.hook.live,
              hooked.hook.bytes, hooked.global, hooked.globalFree);
  bool ok = plain.st == M4aStatus::ok && hooked.st == M4aStatus::ok && plain.frames > 0 && hooked.frames == plain.frames &&
            hooked.adtsBytes == plain.adtsBytes && hooked.hook.allocs > 0 && hooked.global == 0 && hooked.globalFree == 0 &&
            hooked.hook.allocs == plain.global && hooked.hook.frees == plain.globalFree && hooked.hook.live == 0;
  std::printf("%s\n", ok ? "PASS every reader allocation went through the hook" : "FAIL");
  return ok ? 0 : 1;
}
