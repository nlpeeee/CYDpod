// Host test: malformed/huge M4A inputs must return an error inside the allocation caps (no assert, no abort).
// Heap is tracked by linking with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free (see build_m4a_caps.sh).
#include <Arduino.h>
#include <malloc.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include <vector>
using namespace cydpod;
using Bytes = std::vector<uint8_t>;
#ifdef CYDPOD_ARENA_RUN  // build_m4a_arena.sh: same cases through the arena; peak = peak live arena bytes, held to the documented bound
#define PEAK_LIMIT kM4aArenaLiveBoundBytes
#define REQ_LIMIT kM4aArenaLiveBoundBytes
#else
#define PEAK_LIMIT kM4aHeapBudgetBytes
#define REQ_LIMIT kM4aTableBytes
#endif

extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t, size_t); void* __real_realloc(void*, size_t); void __real_free(void*);
static size_t g_live, g_peak, g_maxReq;
static void track(size_t req, long delta) { g_maxReq = std::max(g_maxReq, req); g_live += delta; g_peak = std::max(g_peak, g_live); }
void* __wrap_malloc(size_t n) { void* p = __real_malloc(n); if (p) track(n, malloc_usable_size(p)); return p; }
void* __wrap_calloc(size_t a, size_t b) { void* p = __real_calloc(a, b); if (p) track(a * b, malloc_usable_size(p)); return p; }
void* __wrap_realloc(void* o, size_t n) {
  size_t old = o ? malloc_usable_size(o) : 0; void* p = __real_realloc(o, n);
  if (p) { g_live -= old; track(n, malloc_usable_size(p)); } return p;
}
void __wrap_free(void* p) { if (p) g_live -= malloc_usable_size(p); __real_free(p); }
}

static void put32(Bytes& b, uint32_t v) { for (int i = 24; i >= 0; i -= 8) b.push_back(v >> i); }
static Bytes box(const char* t, const Bytes& payload, int64_t sizeOverride = -1) {
  Bytes b; put32(b, sizeOverride >= 0 ? (uint32_t)sizeOverride : 8 + payload.size());
  b.insert(b.end(), t, t + 4); b.insert(b.end(), payload.begin(), payload.end()); return b;
}
static Bytes cat(std::initializer_list<Bytes> l) { Bytes r; for (auto& x : l) r.insert(r.end(), x.begin(), x.end()); return r; }
static Bytes esdsPayload(int aot = 2, int sr = 4, int ch = 2) {
  Bytes e = {0, 0, 0, 0, 3, 22, 0, 0, 0, 4, 17, 0x40, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 2};
  e.push_back(aot << 3 | sr >> 1); e.push_back((sr & 1) << 7 | ch << 3); return e;
}
static Bytes stsd(const char* codec = "mp4a", int sr = 4, const Bytes& tail = {}) {
  Bytes entry(28, 0); entry[7] = 1; entry[17] = 2;
  Bytes e = cat({entry, box("esds", esdsPayload(2, sr)), tail});
  Bytes en = box(codec, e);
  Bytes p = {0, 0, 0, 0, 0, 0, 0, 1}; p.insert(p.end(), en.begin(), en.end()); return box("stsd", p);
}
static Bytes stsz(uint32_t fixed, uint32_t count, const std::vector<uint32_t>& entries, int64_t sizeOverride = -1) {
  Bytes p = {0, 0, 0, 0}; put32(p, fixed); put32(p, count); for (uint32_t e : entries) put32(p, e); return box("stsz", p, sizeOverride);
}
static Bytes moov(const Bytes& sd, const Bytes& sz) { return box("moov", box("trak", box("mdia", box("minf", box("stbl", cat({sd, sz})))))); }
static Bytes ftyp() { return box("ftyp", Bytes{'M', '4', 'A', ' ', 0, 0, 0, 0}); }
static Bytes frames(int n, int sz) { Bytes b; for (int i = 0; i < n; ++i) for (int j = 0; j < sz; ++j) b.push_back(i + j); return b; }
static Bytes valid(int n = 4, int sz = 100) { return cat({ftyp(), moov(stsd(), stsz(0, n, std::vector<uint32_t>(n, sz))), box("mdat", frames(n, sz))}); }

void* operator new(size_t n) { void* p = std::malloc(n); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
static int failures;
struct Result { M4aStatus st; size_t frames, peak, maxReq; };
// Each run is a forked child: the reader keeps function-static state (currentSampleSize cache) that would leak between
// runs, and a crash/abort in the child is reported as a failure instead of killing the suite.
static Result runInChild(const Bytes& in, size_t slice, bool finish) {
  g_live = g_peak = g_maxReq = 0; size_t frames = 0;
  M4aStatus st;
  {
    auto* r = new M4aGuardedReader;
    r->begin([&](const uint8_t*, size_t) { ++frames; });
    st = M4aStatus::ok;
    for (size_t i = 0; i < in.size() && st == M4aStatus::ok; i += slice) st = r->write(in.data() + i, std::min(slice, in.size() - i));
    if (st == M4aStatus::ok && finish) st = r->finish();
    #ifdef CYDPOD_ARENA_RUN
    g_peak = r->arenaState().stat().peakLive; g_maxReq = r->arenaState().stat().maxRequest;
    #endif
    delete r;
  }
  return {st, frames, g_peak, g_maxReq};
}
static Result run(const Bytes& in, size_t slice, bool finish = true) {
  int fd[2]; if (pipe(fd)) std::abort();
  pid_t pid = fork();
  if (pid == 0) { Result r = runInChild(in, slice, finish); (void)!write(fd[1], &r, sizeof r); _exit(0); }
  Result r{M4aStatus::ok, 0, SIZE_MAX, SIZE_MAX};  // crash marker: exceeds every cap
  int status = 0; ssize_t got = read(fd[0], &r, sizeof r); waitpid(pid, &status, 0);
  if (got != sizeof r) r = {M4aStatus::ok, 0, SIZE_MAX, SIZE_MAX};
  close(fd[0]); close(fd[1]); return r;
}
static void expect(const char* name, const Bytes& in, M4aStatus want, size_t wantFrames = 0, bool finish = true) {
  if (const char* only = std::getenv("CASE")) if (!std::strstr(name, only)) return;  // CASE=substring runs one case
  for (size_t slice : {(size_t)7, (size_t)1024, in.size() ? in.size() : 1}) {
    Result r = run(in, slice, finish);
    bool ok = r.st == want && r.peak <= PEAK_LIMIT && r.maxReq <= REQ_LIMIT && (want != M4aStatus::ok || r.frames == wantFrames);
    if (!ok || std::getenv("VERBOSE")) std::fprintf(stderr, "%-4s %-28s slice=%-7zu -> %-17s frames=%zu peak=%zu maxReq=%zu\n", ok ? "ok" : "FAIL", name, slice, m4aStatusName(r.st), r.frames, r.peak, r.maxReq);
    failures += !ok;
  }
}

int main() {
  expect("valid synthetic", valid(), M4aStatus::ok, 4);
  // CY-11a: without a source the table is resident and capped at kM4aResidentEntries (tableOverflow above it); the
  // file-backed cap kM4aMaxTableEntries (tooManySamples above it) is exercised in m4a_filetable_test.
  { std::vector<uint32_t> e(kM4aResidentEntries, 8);
#ifndef CYDPOD_ARENA_RUN  // arena mode caps the resident table at kM4aArenaResidentEntries (m4a_arena_test covers it)
    expect("resident table exactly at cap", cat({ftyp(), moov(stsd(), stsz(0, kM4aResidentEntries, e)), box("mdat", Bytes())}), M4aStatus::ok, 0);
#endif
    e.push_back(8);
    expect("resident table cap+1", cat({ftyp(), moov(stsd(), stsz(0, kM4aResidentEntries + 1, e)), box("mdat", Bytes())}), M4aStatus::tableOverflow); }
  expect("file table count cap+1", cat({ftyp(), moov(stsd(), stsz(0, kM4aMaxTableEntries + 1, {100}))}), M4aStatus::tooManySamples);
  expect("stsz count 0xFFFFFFFF", cat({ftyp(), moov(stsd(), stsz(0, 0xFFFFFFFF, {100}))}), M4aStatus::tooManySamples);
  expect("stsz count 0", cat({ftyp(), moov(stsd(), stsz(0, 0, {}))}), M4aStatus::badStsz);
  expect("stsz table shorter than count", cat({ftyp(), moov(stsd(), stsz(0, 100, {100, 100}))}), M4aStatus::badStsz);
  expect("stsz declared size huge", cat({ftyp(), moov(stsd(), stsz(0, 2, {100, 100}, 0x7FFFFFFF))}), M4aStatus::badBoxHeader);
  expect("stsz box 4 bytes", cat({ftyp(), box("moov", box("stsz", Bytes{0, 0, 0, 0}))}), M4aStatus::badStsz);
  expect("frame size 0", cat({ftyp(), moov(stsd(), stsz(0, 2, {100, 0}))}), M4aStatus::badFrameSize);
  expect("frame size 2042 (+7 > 2048)", cat({ftyp(), moov(stsd(), stsz(0, 1, {2042}))}), M4aStatus::badFrameSize);
  expect("frame size 0x10064 (aliases 100)", cat({ftyp(), moov(stsd(), stsz(0, 1, {0x10064}))}), M4aStatus::badFrameSize);
  expect("fixed frame size 0xFFFFFFFF", cat({ftyp(), moov(stsd(), stsz(0xFFFFFFFF, 10, {}))}), M4aStatus::badStsz);
  expect("fixed frame size 2041 (unsupported)", cat({ftyp(), moov(stsd(), stsz(2041, 1, {})), box("mdat", frames(1, 2041))}), M4aStatus::badStsz);
  { Bytes sd = stsd(); sd[0] = 0xFF; sd[1] = 0xFF; sd[2] = 0xFF; sd[3] = 0xFF;
    expect("stsd box size 4 GiB-1", cat({ftyp(), box("moov", cat({sd, stsz(0, 1, {100})}))}), M4aStatus::badBoxHeader); }
  { Bytes big(kM4aMaxStsdBytes + 1, 0);
    expect("stsd payload cap+1", cat({ftyp(), box("moov", box("stsd", big))}), M4aStatus::stsdTooLarge);
    expect("stsd 1 GiB declared", cat({ftyp(), box("moov", box("stsd", Bytes(64, 0), 0x40000000))}), M4aStatus::badBoxHeader);
    Bytes sd; put32(sd, 0x20000000); sd.insert(sd.end(), {'s', 't', 's', 'd'});
    expect("stsd 512 MiB top-level", cat({ftyp(), sd}), M4aStatus::stsdTooLarge); }
  expect("stsd not mp4a (alac)", cat({ftyp(), moov(stsd("alac"), stsz(0, 1, {100}))}), M4aStatus::notMp4a);
  expect("stsd + btrt child (real files)", cat({ftyp(), moov(stsd("mp4a", 4, box("btrt", Bytes(12, 0))), stsz(0, 4, {100, 100, 100, 100})), box("mdat", frames(4, 100))}), M4aStatus::ok, 4);
  expect("stsd tail looks like size-8 box", cat({ftyp(), moov(stsd("mp4a", 4, Bytes{0, 0, 0, 8, 65, 65, 65, 65}), stsz(0, 1, {100}))}), M4aStatus::badStsd);
  expect("stsd 48 kHz", cat({ftyp(), moov(stsd("mp4a", 3), stsz(0, 1, {100}))}), M4aStatus::badSampleRate);
  expect("stsd truncated entry", cat({ftyp(), box("moov", box("stsd", Bytes(20, 0)))}), M4aStatus::badStsd);
  expect("box size 4 (< header)", cat({ftyp(), Bytes{0, 0, 0, 4, 'f', 'r', 'e', 'e'}}), M4aStatus::badBoxHeader);
  expect("non-alnum box type (neutralized)", cat({ftyp(), Bytes{0, 0, 0, 16, 'a', 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0}}), M4aStatus::noSampleTable);
  expect("child larger than parent", cat({ftyp(), box("moov", box("free", Bytes(8, 0), 100))}), M4aStatus::badBoxHeader);
  expect("largesize on non-mdat", cat({ftyp(), Bytes{0, 0, 0, 1, 'f', 'r', 'e', 'e', 0, 0, 0, 0, 0, 0, 0, 16}}), M4aStatus::badBoxHeader);
  expect("mdat before moov", cat({ftyp(), box("mdat", frames(2, 100)), moov(stsd(), stsz(0, 2, {100, 100}))}), M4aStatus::moovAfterMdat);
  { Bytes n; for (int i = 0; i < 20000; ++i) { put32(n, 8 * (20000 - i)); n.insert(n.end(), {'m', 'o', 'o', 'v'}); }
    expect("20000 nested moov", n, M4aStatus::tooDeep); }
  { Bytes n = box("free", Bytes()); for (int i = 0; i < kM4aMaxBoxDepth + 1; ++i) n = box(i % 2 ? "mdia" : "trak", n);
    expect("depth cap+1", cat({ftyp(), n}), M4aStatus::tooDeep);
    n = box("free", Bytes()); for (int i = 0; i < kM4aMaxBoxDepth; ++i) n = box(i % 2 ? "mdia" : "trak", n);
    expect("depth at cap, no sample table", cat({ftyp(), n}), M4aStatus::noSampleTable);
    // metadata containers are neutralized, so nesting inside them cannot grow the reader's level stack
    Bytes u = box("free", Bytes()); for (int i = 0; i < 5000; ++i) u = box("udta", u);
    expect("5000 nested udta (neutralized)", cat({ftyp(), box("moov", cat({box("udta", u), box("trak", box("mdia", box("minf", box("stbl", cat({stsd(), stsz(0, 4, {100, 100, 100, 100})})))))})), box("mdat", frames(4, 100))}), M4aStatus::ok, 4);
    Bytes tag = box("\xA9nam", Bytes(40, 'x')), ilst = box("ilst", cat({tag, tag}));
    expect("iTunes metadata (non-alnum atoms)", cat({ftyp(), box("moov", cat({box("udta", box("meta", cat({Bytes(4, 0), ilst}))), box("trak", box("mdia", box("minf", box("stbl", cat({stsd(), stsz(0, 4, {100, 100, 100, 100})})))))})), box("mdat", frames(4, 100))}), M4aStatus::ok, 4); }
  { Bytes v = valid();
    expect("duplicate track (2nd stsz)", cat({ftyp(), box("moov", cat({moov(stsd(), stsz(0, 1, {100})), stsz(0, 1, {100})}))}), M4aStatus::duplicateTrack);
    expect("truncated inside trak header", Bytes(v.begin(), v.begin() + 30), M4aStatus::truncated);
    expect("truncated inside stsz table", Bytes(v.begin(), v.begin() + 170), M4aStatus::truncated);
    expect("truncated inside mdat (ok, 2 frames)", Bytes(v.begin(), v.begin() + 440), M4aStatus::ok, 2); }
  { // backstop: the capped table alone, with the guard bypassed, must latch instead of abort/allocating
    g_live = g_peak = g_maxReq = 0;
    { M4aCappedSizes t; bool r = t.resize(0xFFFFFFFFu); bool w = t.write(1);
      bool ok = !r && w && t.overflow && g_maxReq <= kM4aTableBytes;
      std::fprintf(stderr, "%-4s backstop table resize(4G)    -> resize=%d write_swallowed=%d overflow=%d maxReq=%zu\n", ok ? "ok" : "FAIL", r, w, t.overflow, g_maxReq);
      failures += !ok; } }
  std::fprintf(stderr, failures ? "FAILED %d\n" : "PASS all malformed cases returned errors within caps\n", failures);
  return failures != 0;
}
