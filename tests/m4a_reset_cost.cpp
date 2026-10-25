// CY-5: heap cost of constructing a fresh M4aGuardedReader per track and of the state-reset primer (host g++, malloc wrapped; see build_m4a_sequence.sh).
#include <Arduino.h>
#include <malloc.h>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
using namespace cydpod;
extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t, size_t); void* __real_realloc(void*, size_t); void __real_free(void*);
static size_t g_live, g_peak;
static void track(long delta) { g_live += delta; g_peak = std::max(g_peak, g_live); }
void* __wrap_malloc(size_t n) { void* p = __real_malloc(n); if (p) track(malloc_usable_size(p)); return p; }
void* __wrap_calloc(size_t a, size_t b) { void* p = __real_calloc(a, b); if (p) track(malloc_usable_size(p)); return p; }
void* __wrap_realloc(void* o, size_t n) { size_t old = o ? malloc_usable_size(o) : 0; void* p = __real_realloc(o, n); if (p) { g_live -= old; track(malloc_usable_size(p)); } return p; }
void __wrap_free(void* p) { if (p) g_live -= malloc_usable_size(p); __real_free(p); }
}
void* operator new(size_t n) { void* p = std::malloc(n); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
int main() {
  size_t base = g_live; g_peak = g_live;
  m4aPrimeSampleSizeCache();
  std::fprintf(stderr, "primer:                       peak +%zu B, live after +%zu B\n", g_peak - base, g_live - base);
  g_peak = g_live = 0;
  auto* r = new M4aGuardedReader;
  std::fprintf(stderr, "new M4aGuardedReader:         live %zu B (sizeof object %zu B)\n", g_live, sizeof(M4aGuardedReader));
  size_t before = g_live; g_peak = g_live;
  r->begin(nullptr);
  std::fprintf(stderr, "begin() (reset + primer):     peak +%zu B, live after +%zu B (total live %zu B)\n", g_peak - before, g_live - before, g_live);
  delete r;
  std::fprintf(stderr, "delete reader:                live %zu B (heap returns to baseline)\n", g_live);
  return g_live != 0;
}
