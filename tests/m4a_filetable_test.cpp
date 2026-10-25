// CY-11a host test: the file-backed sample-size table (1 KiB window) against the old resident table.
// usage: m4a_filetable_test fixture.m4a [media-folder]   (media files are read-only and never copied; no folder = part b skipped, exit 77)
//  (a) fixture: 131 frames / 134144 samples per channel, PCM identical to the resident table
//  (b) every .m4a of the folder (25 real files): same frames, ADTS bytes and PCM hash as the resident table, count from the manifest
//  (c) source read failure / short read / seek error / changed entry: clean status, no stale frame, no abort, reusable afterwards
//  (d) caps with a source; resident bytes of the table (peak heap of the whole reader, largest single request)
#include <Arduino.h>
#include <malloc.h>
#include <cstdarg>
#include <filesystem>
#include <map>
#include <fstream>
#include <sstream>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include "m4a_file_source.h"
#include "../firmware/cydpod_probe/adts_decoder.h"
using namespace cydpod;
using Bytes = std::vector<uint8_t>;
extern "C" void* helix_malloc(int size) { return std::malloc(size); }
extern "C" void helix_free(void* p) { std::free(p); }

// Heap tracking (linked with -Wl,--wrap=malloc,...). Only the region between g_track=true/false is counted; test bookkeeping
// (collected ADTS bytes etc.) is allocated with tracking switched off so that it cannot be mistaken for reader memory.
extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t, size_t); void* __real_realloc(void*, size_t); void __real_free(void*);
static size_t g_live, g_peak, g_maxReq; static bool g_track;
static void track(size_t req, long delta) { if (!g_track) return; g_maxReq = std::max(g_maxReq, req); g_live += delta; g_peak = std::max(g_peak, g_live); }
void* __wrap_malloc(size_t n) { void* p = __real_malloc(n); if (p) track(n, malloc_usable_size(p)); return p; }
void* __wrap_calloc(size_t a, size_t b) { void* p = __real_calloc(a, b); if (p) track(a * b, malloc_usable_size(p)); return p; }
void* __wrap_realloc(void* o, size_t n) {
  size_t old = o ? malloc_usable_size(o) : 0; void* p = __real_realloc(o, n);
  if (p) { if (g_track) g_live -= old; track(n, malloc_usable_size(p)); } return p;
}
void __wrap_free(void* p) { if (p && g_track) g_live -= malloc_usable_size(p); __real_free(p); }
}
void* operator new(size_t n) { void* p = std::malloc(n); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static int failures;
static void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(bool ok, const char* fmt, ...) {
  char b[400]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
  std::fprintf(stderr, "%-4s %s\n", ok ? "ok" : "FAIL", b); failures += !ok;
}

// ---- synthetic m4a builder (same layout as m4a_caps_test) ----
static void put32(Bytes& b, uint32_t v) { for (int i = 24; i >= 0; i -= 8) b.push_back(v >> i); }
static Bytes box(const char* t, const Bytes& payload) {
  Bytes b; put32(b, 8 + payload.size()); b.insert(b.end(), t, t + 4); b.insert(b.end(), payload.begin(), payload.end()); return b;
}
static Bytes cat(std::initializer_list<Bytes> l) { Bytes r; for (auto& x : l) r.insert(r.end(), x.begin(), x.end()); return r; }
static Bytes stsd() {
  Bytes e = {0, 0, 0, 0, 3, 22, 0, 0, 0, 4, 17, 0x40, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 2, 2 << 3 | 4 >> 1, (4 & 1) << 7 | 2 << 3};
  Bytes entry(28, 0); entry[7] = 1; entry[17] = 2;
  Bytes en = box("mp4a", cat({entry, box("esds", e)}));
  Bytes p = {0, 0, 0, 0, 0, 0, 0, 1}; p.insert(p.end(), en.begin(), en.end()); return box("stsd", p);
}
static Bytes stsz(uint32_t count, const std::vector<uint32_t>& entries) {
  Bytes p = {0, 0, 0, 0}; put32(p, 0); put32(p, count); for (uint32_t e : entries) put32(p, e); return box("stsz", p);
}
static Bytes moov(const Bytes& sd, const Bytes& sz) { return box("moov", box("trak", box("mdia", box("minf", box("stbl", cat({sd, sz})))))); }
static Bytes ftyp() { return box("ftyp", Bytes{'M', '4', 'A', ' ', 0, 0, 0, 0}); }
// n frames; frame i has size sizeOf(i) and content (i + j) & 0xFF. tableOff = file offset of the first raw stsz entry.
static Bytes synth(size_t n, size_t (*sizeOf)(size_t), size_t* tableOff = nullptr) {
  std::vector<uint32_t> sz; Bytes md;
  for (size_t i = 0; i < n; ++i) { size_t s = sizeOf(i); sz.push_back(s); for (size_t j = 0; j < s; ++j) md.push_back(i + j); }
  Bytes pre = cat({ftyp(), moov(stsd(), stsz(n, sz))});
  if (tableOff) *tableOff = pre.size() - 4 * n;
  return cat({pre, box("mdat", md)});
}
static size_t size100(size_t) { return 100; }
static size_t sizeVar(size_t i) { return 20 + (i * 37) % 300; }

// ---- running a reader ----
struct Run { M4aStatus st = M4aStatus::ok; size_t frames = 0; Bytes adts; std::vector<size_t> sizes; };
static Run feed(M4aGuardedReader& r, const Bytes& in, M4aSeekableSource* src, size_t slice = 1024) {
  Run o;
  r.begin([&](const uint8_t* p, size_t n) { ++o.frames; o.sizes.push_back(n); o.adts.insert(o.adts.end(), p, p + n); }, src);
  for (size_t i = 0; i < in.size() && o.st == M4aStatus::ok; i += slice) o.st = r.write(in.data() + i, std::min(slice, in.size() - i));
  if (o.st == M4aStatus::ok) o.st = r.finish();
  return o;
}
static Bytes readFile(const std::string& path) {
  Bytes out; FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return out;
  uint8_t b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + n);
  std::fclose(f); return out;
}

// ---- decode + hash ----
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

// Resident reference: the old table path (no source), limit raised so the two long real files fit.
static Run residentRun(const Bytes& in) {
  auto* r = new M4aGuardedReader;
  Run o;
  r->begin([&](const uint8_t* p, size_t n) { ++o.frames; o.adts.insert(o.adts.end(), p, p + n); });
  r->sizeTable().residentLimit(kM4aMaxTableEntries);
  for (size_t i = 0; i < in.size() && o.st == M4aStatus::ok; i += 1024) o.st = r->write(in.data() + i, std::min<size_t>(1024, in.size() - i));
  if (o.st == M4aStatus::ok) o.st = r->finish();
  delete r; return o;
}

struct Mem { size_t peak = 0, maxReq = 0; M4aStatus st = M4aStatus::ok; size_t frames = 0; };
// Heap used by one whole reader (object + vendor allocations) while parsing `in`, with the table file-backed or resident.
static Mem measured(const Bytes& in, bool fileBacked) {
  MemSource src(in); Mem m;
  g_live = g_peak = g_maxReq = 0; g_track = true;
  auto* r = new M4aGuardedReader;
  r->begin([&](const uint8_t*, size_t) { ++m.frames; }, fileBacked ? &src : nullptr);
  for (size_t i = 0; i < in.size() && m.st == M4aStatus::ok; i += 1024) m.st = r->write(in.data() + i, std::min<size_t>(1024, in.size() - i));
  if (m.st == M4aStatus::ok) m.st = r->finish();
  delete r;
  g_track = false; m.peak = g_peak; m.maxReq = g_maxReq; return m;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: m4a_filetable_test fixture.m4a [media-folder]\n"); return 2; }
  Bytes fx = readFile(argv[1]);
  if (fx.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
  static_assert(kM4aWindowBytes == 1024 && kM4aWindowEntries == 256, "window is 1 KiB / 256 raw entries");

  // ---- (a) fixture, streamed from a FILE* whose handle the source shares ----
  {
    Run res = residentRun(fx);
    FILE* f = std::fopen(argv[1], "rb"); FileSource src(f);
    auto* r = new M4aGuardedReader; Run o;
    r->begin([&](const uint8_t* p, size_t n) { ++o.frames; o.adts.insert(o.adts.end(), p, p + n); }, &src);
    uint8_t b[1024]; size_t n;
    while (o.st == M4aStatus::ok && (n = std::fread(b, 1, sizeof b, f)) > 0) o.st = r->write(b, n);
    if (o.st == M4aStatus::ok) o.st = r->finish();
    delete r; std::fclose(f);
    Pcm pf = decode(o.adts), pr = decode(res.adts);
    check(o.st == M4aStatus::ok && o.frames == 131, "(a) fixture file-backed status=%s frames=%zu (want 131)", m4aStatusName(o.st), o.frames);
    check(pf.ok && pf.frames == 131 && size_t(pf.frames) * 1024 == 134144, "(a) fixture decodes to %u frames = %zu samples/channel", pf.frames, size_t(pf.frames) * 1024);
    check(o.adts == res.adts && pf.hash == pr.hash, "(a) fixture ADTS identical, PCM hash %016llx == resident %016llx", (unsigned long long)pf.hash, (unsigned long long)pr.hash);
    check(src.reads == 1 && src.largest == 131 * 4, "(a) fixture needed %zu source read(s), %zu bytes", src.reads, src.bytes);
  }

  // ---- (c) failures, window refills, rewind (synthetic 1000-frame stream: refills at entries 0/256/512/768) ----
  {
    size_t off = 0; Bytes s = synth(1000, sizeVar, &off);
    MemSource ok(s);
    auto* r = new M4aGuardedReader;
    Run base = feed(*r, s, &ok);
    bool sizesOk = base.sizes.size() == 1000; for (size_t i = 0; i < base.sizes.size(); ++i) sizesOk &= base.sizes[i] == sizeVar(i) + 7;
    check(base.st == M4aStatus::ok && base.frames == 1000 && sizesOk && ok.reads == 4, "(c) synthetic 1000 frames, every size matches its table entry, %zu refills (want 4)", ok.reads);
    check(residentRun(s).adts == base.adts, "(c) synthetic ADTS identical to the resident table");
    MemSource again(s);  // rewind: same reader, begin() again, same source
    Run second = feed(*r, s, &again, 7);
    check(second.st == M4aStatus::ok && second.adts == base.adts, "(c) rewind via begin(): identical output with 7-byte writes");
    struct Case { const char* name; M4aSourceResult res; M4aStatus want; };
    for (Case c : {Case{"read error", M4aSourceResult::readError, M4aStatus::sourceReadError},
                   Case{"short read", M4aSourceResult::shortRead, M4aStatus::sourceShortRead},
                   Case{"seek error", M4aSourceResult::seekError, M4aStatus::sourceSeekError}})
      for (size_t at : {size_t(0), size_t(2), size_t(3)}) {
        MemSource bad(s); bad.failAt = at; bad.failWith = c.res;
        Run o = feed(*r, s, &bad);
        size_t want = at * 256;  // frames before the failing refill; none after it
        bool noStale = true; for (size_t i = 0; i < o.sizes.size(); ++i) noStale &= o.sizes[i] == sizeVar(i) + 7;
        check(o.st == c.want && o.frames == want && noStale, "(c) %s on refill %zu -> %s, %zu frames (want %zu), no stale/garbage frame", c.name, at, m4aStatusName(o.st), o.frames, want);
        check(r->write(s.data(), 16) == c.want && r->finish() == c.want, "(c) %s status is sticky", c.name);
        Run clean = feed(*r, s, &ok);
        check(clean.st == M4aStatus::ok && clean.adts == base.adts, "(c) reader reusable after %s", c.name);
      }
    { Bytes shortFile(s.begin(), s.begin() + off + 4 * 600 + 2);  // source file truncated inside the table
      MemSource trunc(shortFile); Run o = feed(*r, s, &trunc);
      check(o.st == M4aStatus::sourceShortRead && o.frames == 512, "(c) source truncated mid-table -> %s after %zu frames", m4aStatusName(o.st), o.frames); }
    for (uint32_t bad : {0u, 2042u, 70000u}) {  // source changed after the scan validated the streamed bytes
      Bytes t = s; size_t e = off + 4 * 300; t[e] = bad >> 24; t[e + 1] = bad >> 16; t[e + 2] = bad >> 8; t[e + 3] = bad;
      MemSource mod(t);
      Run o = feed(*r, s, &mod);  // stream the original bytes, serve the modified table
      check(o.st == M4aStatus::badFrameSize && o.frames == 300, "(c) source entry changed to %u -> %s after %zu frames", bad, m4aStatusName(o.st), o.frames);
    }
    delete r;
    // the table object on its own: repeat reads of one index, forward only, reset forgets everything
    MemSource m(s); M4aCappedSizes t; t.source(&m); t.tableOffset(off);
    uint16_t v = 0, w = 0;
    bool a = t.resize(1000) && t.peek(v) && t.peek(w) && v == w && v == sizeVar(0) && t.read(v) && t.read(w) && w == sizeVar(1);
    check(a && m.reads == 1 && t.available() == 998, "(c) table: repeated peek/read of an index cause one refill (reads=%zu)", m.reads);
    t.reset(); check(t.available() == 0 && !t.peek(v) && !t.read(v), "(c) table: reset() forgets the entries");
    t.resize(1000); check(t.read(v) && v == sizeVar(0) && t.available() == 999, "(c) table: rewind restarts at entry 0");
    t.resize(5); check(t.available() == 5 && t.size() == 5 && t.address() == nullptr, "(c) table: file mode owns no resident array");
  }

  // ---- (d) caps with a source, malformed counts, memory ----
  {
    auto cap = [&](size_t count, size_t table, const char* name, M4aStatus want) {
      std::vector<uint32_t> e(table, 8); Bytes b = cat({ftyp(), moov(stsd(), stsz(count, e)), box("mdat", Bytes())});
      MemSource m(b); auto* r = new M4aGuardedReader; Run o = feed(*r, b, &m); delete r;
      check(o.st == want, "(d) %s -> %s", name, m4aStatusName(o.st));
    };
    cap(kM4aMaxTableEntries, kM4aMaxTableEntries, "file-backed table exactly at cap (524288)", M4aStatus::ok);
    cap(kM4aMaxTableEntries + 1, 1, "file-backed count cap+1", M4aStatus::tooManySamples);
    cap(100, 2, "box length shorter than count*4", M4aStatus::badStsz);
    cap(3, 4, "box length longer than count*4", M4aStatus::badStsz);
    cap(0xFFFFFFFFu, 1, "count 0xFFFFFFFF", M4aStatus::tooManySamples);
    cap(0, 0, "count 0", M4aStatus::badStsz);
    Bytes a = synth(10, size100), b2 = synth(1000, size100);
    std::vector<uint32_t> e(18100, 8); Bytes b3 = cat({ftyp(), moov(stsd(), stsz(18100, e)), box("mdat", Bytes())});
    Mem m1 = measured(a, true), m2 = measured(b2, true), m3 = measured(b3, true), r1 = measured(a, false), r2 = measured(b2, false);
    std::fprintf(stderr, "     whole-reader heap peak, file-backed: 10 entries=%zu, 1000=%zu, 18100=%zu B; largest request %zu/%zu/%zu B\n",
                 m1.peak, m2.peak, m3.peak, m1.maxReq, m2.maxReq, m3.maxReq);
    std::fprintf(stderr, "     whole-reader heap peak, resident:    10 entries=%zu, 1000=%zu B; largest request %zu/%zu B\n", r1.peak, r2.peak, r1.maxReq, r2.maxReq);
    std::fprintf(stderr, "     sizeof(M4aGuardedReader)=%zu sizeof(M4aCappedSizes)=%zu\n", sizeof(M4aGuardedReader), sizeof(M4aCappedSizes));
    check(m1.st == M4aStatus::ok && m3.st == M4aStatus::ok && m3.peak == m1.peak, "(d) heap peak is independent of the table size (18100 vs 10 entries: %+ld B)", (long)m3.peak - (long)m1.peak);
    check(std::max(m3.maxReq, m2.maxReq) <= sizeof(M4aGuardedReader), "(d) no table-sized request (old resident: 32,768 B); largest request is the reader object itself, %zu B", std::max(m3.maxReq, m2.maxReq));
    check(sizeof(M4aCappedSizes) <= kM4aWindowBytes + 256, "(d) table object = window 1024 B + %zu B bookkeeping", sizeof(M4aCappedSizes) - kM4aWindowBytes);
  }

  // ---- (b) real files ----
  const char* folder = argc > 2 ? argv[2] : std::getenv("CYDPOD_REAL_MEDIA");
  if (!folder || !*folder || !std::filesystem::is_directory(folder)) {
    std::fprintf(stderr, "SKIP (b): real-media folder not present; parts a, c, d %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 77;
  }
  std::map<std::string, size_t> expect;
  { std::ifstream in("tests/real_media_expected.tsv"); std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      std::istringstream row(line); std::string name, st, n; std::getline(row, name, '\t'); std::getline(row, st, '\t'); std::getline(row, n, '\t');
      expect[name] = st == "ok" ? std::stoul(n) : 0; } }
  size_t files = 0, largest = 0, maxPeak = 0, maxReq = 0, maxRead = 0, minPeak = SIZE_MAX;
  for (auto& e : std::filesystem::directory_iterator(folder)) {
    if (e.path().extension() != ".m4a") continue;
    std::string name = e.path().filename().string();
    Run res = residentRun(readFile(e.path().string()));
    FILE* f = std::fopen(e.path().c_str(), "rb"); FileSource src(f);  // streamed from f; the source shares the handle
    Run o; g_live = g_peak = g_maxReq = 0; g_track = true;
    auto* r = new M4aGuardedReader;
    r->begin([&](const uint8_t* p, size_t n) { g_track = false; ++o.frames; o.adts.insert(o.adts.end(), p, p + n); g_track = true; }, &src);
    uint8_t buf[1024]; size_t n;
    while (o.st == M4aStatus::ok && (n = std::fread(buf, 1, sizeof buf, f)) > 0) o.st = r->write(buf, n);
    if (o.st == M4aStatus::ok) o.st = r->finish();
    delete r; g_track = false; std::fclose(f);
    size_t peak = g_peak, req = g_maxReq;
    Pcm pf = decode(o.adts), pr = decode(res.adts);
    bool same = o.st == M4aStatus::ok && res.st == M4aStatus::ok && o.frames == res.frames && o.adts == res.adts && pf.ok && pr.ok && pf.hash == pr.hash && pf.frames == pr.frames;
    bool counted = expect.count(name) && expect[name] == o.frames && pf.frames == o.frames;
    check(same && counted, "(b) %s frames=%zu (manifest %zu) refills=%zu pcm=%016llx %s", name.c_str(), o.frames, expect.count(name) ? expect[name] : 0, src.reads, (unsigned long long)pf.hash, same ? "== resident" : "DIFFERS");
    ++files; largest = std::max(largest, o.frames); maxPeak = std::max(maxPeak, peak); minPeak = std::min(minPeak, peak); maxReq = std::max(maxReq, req); maxRead = std::max(maxRead, src.largest);
  }
  check(files == 25 && files == expect.size(), "(b) %zu real files compared (want 25)", files);
  std::fprintf(stderr, "     real files: whole-reader heap peak %zu..%zu B (window included), largest single request %zu B, largest source read %zu B, largest table %zu entries\n",
               minPeak, maxPeak, maxReq, maxRead, largest);
  check(maxRead <= kM4aWindowBytes, "(b) no source read larger than the %zu B window (%zu)", kM4aWindowBytes, maxRead);
  std::fprintf(stderr, failures ? "FAILED %d\n" : "PASS file-backed table\n", failures);
  return failures != 0;
}
