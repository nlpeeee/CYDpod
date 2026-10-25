// Host test (CY-4): moov-last and unsupported codec/track inputs must be rejected with a specific M4aStatus, no crash, no heap
// past the caps and no ADTS frame delivered. Cases are built in memory from fixtures/stereo-faststart.m4a (never written back).
// Heap is tracked by linking with -Wl,--wrap=malloc,... (see build_m4a_reject.sh).
#include <Arduino.h>
#include <malloc.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include <vector>
using namespace cydpod;
using Bytes = std::vector<uint8_t>;
#ifdef CYDPOD_ARENA_RUN  // build_m4a_arena.sh: same cases through the arena; peak = peak live arena bytes, held to the documented bound
#include "m4a_file_source.h"  // arena mode runs the shipping (file-backed) table: a resident table is limited to kM4aArenaResidentEntries there
#define PEAK_LIMIT kM4aArenaLiveBoundBytes
#define REQ_LIMIT kArenaReserveBytes  // maxReq = the most bytes requested during any one vendor call (CY-10b3 mechanical reserve check)
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
void* operator new(size_t n) { void* p = std::malloc(n); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static uint32_t rd32(const Bytes& b, size_t o) { return (uint32_t)b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]; }
static void wr32(Bytes& b, size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) b[o + i] = v >> (24 - 8 * i); }
static bool typeAt(const Bytes& b, size_t o, const char* t) { return !memcmp(&b[o + 4], t, 4); }
// First box of `type` among the boxes in [start, end); SIZE_MAX if none.
static size_t findBox(const Bytes& b, size_t start, size_t end, const char* type) {
  for (size_t o = start; o + 8 <= end;) {
    uint32_t sz = rd32(b, o); if (sz < 8) break;
    if (typeAt(b, o, type)) return o;
    o += sz;
  }
  return SIZE_MAX;
}
static size_t child(const Bytes& b, size_t box, const char* type) { return findBox(b, box + 8, box + rd32(b, box), type); }
struct Layout { size_t moov, trak, stsd, esds, oti, asc; };  // oti = objectTypeIndication byte, asc = 2-byte AudioSpecificConfig
static Layout layout(const Bytes& b) {
  Layout l; l.moov = findBox(b, 0, b.size(), "moov");
  l.trak = child(b, l.moov, "trak");
  size_t mdia = child(b, l.trak, "mdia"), minf = child(b, mdia, "minf"), stbl = child(b, minf, "stbl");
  l.stsd = child(b, stbl, "stsd");
  l.esds = l.stsd + 8 + 8 + 8 + 28;  // stsd header, version/count, entry header, AudioSampleEntry body
  if (!typeAt(b, l.esds, "esds")) { std::fprintf(stderr, "fixture layout: no esds at expected offset\n"); std::exit(2); }
  // esds payload: version/flags(4), 0x03 len ES_ID(2) flags(1), 0x04 len objectTypeIndication ...13 bytes... 0x05 len ASC
  size_t o = l.esds + 8 + 4;
  auto skipLen = [&] { for (int i = 0; i < 4 && (b[o++] & 0x80); ++i) {} };
  if (b[o++] != 0x03) std::exit(2);
  skipLen(); o += 3;
  if (b[o++] != 0x04) std::exit(2);
  skipLen(); l.oti = o; o += 13;
  if (b[o++] != 0x05) std::exit(2);
  skipLen(); l.asc = o;
  return l;
}
static void setAsc(Bytes& b, int aot, int sr, int ch) {
  size_t a = layout(b).asc; b[a] = aot << 3 | sr >> 1; b[a + 1] = (b[a + 1] & 0x07) | (sr & 1) << 7 | ch << 3;
}
static void setCodec(Bytes& b, const char* t) { memcpy(&b[layout(b).stsd + 8 + 8 + 4], t, 4); }
static Bytes reorderMoovLast(const Bytes& b) {
  size_t m = layout(b).moov, mend = m + rd32(b, m);
  Bytes r(b.begin(), b.begin() + m); r.insert(r.end(), b.begin() + mend, b.end()); r.insert(r.end(), b.begin() + m, b.begin() + mend);
  return r;  // ftyp mdat moov
}
// Inserts a copy of the first trak (sample entry renamed to `codec`) before or after the original, growing moov to match.
static Bytes withExtraTrak(const Bytes& b, const char* codec, bool before) {
  Layout l = layout(b); size_t tsz = rd32(b, l.trak);
  Bytes copy(b.begin() + l.trak, b.begin() + l.trak + tsz);
  Bytes r = b; wr32(r, l.moov, rd32(b, l.moov) + tsz);
  memcpy(&copy[l.stsd - l.trak + 8 + 8 + 4], codec, 4);
  r.insert(r.begin() + (before ? l.trak : l.trak + tsz), copy.begin(), copy.end());
  return r;
}
static Bytes renameTrak(const Bytes& b) { Bytes r = b; memcpy(&r[layout(b).trak + 4], "free", 4); return r; }

struct Result { M4aStatus st; size_t frames, peak, maxReq; };
// Each run is a forked child: the reader keeps function-static state, and a crash is reported as a failure.
static Result runInChild(const Bytes& in, size_t slice) {
  g_live = g_peak = g_maxReq = 0; size_t frames = 0; M4aStatus st = M4aStatus::ok;
  { auto* r = new M4aGuardedReader;
    #ifdef CYDPOD_ARENA_RUN
    MemSource src(in); r->begin([&](const uint8_t*, size_t) { ++frames; }, &src);
    #else
    r->begin([&](const uint8_t*, size_t) { ++frames; });
    #endif
    for (size_t i = 0; i < in.size() && st == M4aStatus::ok; i += slice) st = r->write(in.data() + i, std::min(slice, in.size() - i));
    if (st == M4aStatus::ok) st = r->finish();
    #ifdef CYDPOD_ARENA_RUN
    g_peak = r->arenaState().stat().peakLive; g_maxReq = r->maxCallBytes();
    #endif
    delete r; }
  return {st, frames, g_peak, g_maxReq};
}
static Result run(const Bytes& in, size_t slice) {
  int fd[2]; if (pipe(fd)) std::abort();
  pid_t pid = fork();
  if (pid == 0) { Result r = runInChild(in, slice); (void)!write(fd[1], &r, sizeof r); _exit(0); }
  Result r{M4aStatus::ok, 0, SIZE_MAX, SIZE_MAX}; int status = 0;
  ssize_t got = read(fd[0], &r, sizeof r); waitpid(pid, &status, 0);
  if (got != sizeof r) r = {M4aStatus::ok, 0, SIZE_MAX, SIZE_MAX};  // crash marker
  close(fd[0]); close(fd[1]); return r;
}
static int failures, cases;
static void expect(const char* name, const Bytes& in, M4aStatus want) {
  for (size_t slice : {(size_t)7, (size_t)1024, in.size()}) {
    Result r = run(in, slice);
    bool ok = r.st == want && r.peak <= PEAK_LIMIT && r.maxReq <= REQ_LIMIT;
    ok = ok && (want == M4aStatus::ok ? r.frames > 0 : r.frames == 0);  // a rejected input delivers no audio
    if (slice == 1024 || !ok) std::fprintf(stderr, "%-4s %-46s expected=%-17s actual=%-17s frames=%zu peak=%zu slice=%zu\n", ok ? "ok" : "FAIL", name, m4aStatusName(want), m4aStatusName(r.st), r.frames, r.peak, slice);
    failures += !ok; ++cases;
  }
}
static Bytes load(const char* path) {
  FILE* f = std::fopen(path, "rb"); if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
  Bytes b; uint8_t buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
  std::fclose(f); return b;
}

int main() {
  Bytes base = load("fixtures/stereo-faststart.m4a");
  expect("control: unmodified faststart", base, M4aStatus::ok);
  { Bytes b = base; setAsc(b, 2, 4, 2); expect("control: ASC rewritten to LC/44.1k/2ch", b, M4aStatus::ok); }

  // moov-last
  expect("fixture stereo-moov-last.m4a", load("fixtures/stereo-moov-last.m4a"), M4aStatus::moovAfterMdat);
  expect("synthetic ftyp mdat moov (from faststart)", reorderMoovLast(base), M4aStatus::moovAfterMdat);

  // codec / track
  for (const char* c : {"alac", ".mp3", "ac-3", "Opus", "fLaC"}) { Bytes b = base; setCodec(b, c); std::string n = std::string("sample entry ") + c; expect(n.c_str(), b, M4aStatus::notMp4a); }
  { Bytes b = base; setAsc(b, 5, 4, 2); expect("HE-AAC (AOT 5, SBR)", b, M4aStatus::notAacLc); }
  { Bytes b = base; setAsc(b, 29, 4, 2); expect("HE-AACv2 (AOT 29, PS)", b, M4aStatus::notAacLc); }
  { Bytes b = base; setAsc(b, 1, 4, 2); expect("AAC Main (AOT 1)", b, M4aStatus::notAacLc); }
  { Bytes b = base; setAsc(b, 4, 4, 2); expect("AAC LTP (AOT 4)", b, M4aStatus::notAacLc); }
  { Bytes b = base; b[layout(b).oti] = 0x6B; expect("objectTypeIndication 0x6B (MPEG-1 audio)", b, M4aStatus::notAacLc); }
  for (int sr : {0, 3, 5, 6, 7, 8, 11, 12, 15}) {
    Bytes b = base; setAsc(b, 2, sr, 2); std::string n = "sample rate index " + std::to_string(sr);
    expect(n.c_str(), b, M4aStatus::badSampleRate); }
  for (int ch : {0, 1, 3, 6, 7}) {
    Bytes b = base; setAsc(b, 2, 4, ch); std::string n = "channel config " + std::to_string(ch);
    expect(n.c_str(), b, M4aStatus::badChannels); }
  expect("no audio track (trak renamed free)", renameTrak(base), M4aStatus::noAudioTrack);
  { Bytes b = renameTrak(base); b.resize(layout(base).moov + rd32(base, layout(base).moov)); expect("no audio track, no mdat (ends after moov)", b, M4aStatus::noAudioTrack); }
  expect("first track video (avc1), audio second", withExtraTrak(base, "avc1", true), M4aStatus::firstTrackNotAudio);
  expect("first track video (hvc1), audio second", withExtraTrak(base, "hvc1", true), M4aStatus::firstTrackNotAudio);
  { Bytes b = base; setCodec(b, "avc1"); expect("only track is video (avc1)", b, M4aStatus::firstTrackNotAudio); }
  expect("audio first, video second", withExtraTrak(base, "avc1", false), M4aStatus::duplicateTrack);
  expect("two audio tracks", withExtraTrak(base, "mp4a", false), M4aStatus::duplicateTrack);

  std::fprintf(stderr, failures ? "FAILED %d of %d runs\n" : "PASS all %d runs\n", failures ? failures : cases, cases);
  return failures != 0;
}
