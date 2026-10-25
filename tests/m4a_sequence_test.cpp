// Host test (CY-5): repeated, mixed playback must not carry state from an earlier track into a later one. Plays an .aac, then several
// .m4a (faststart, shorter-table variants built in memory, an aborted one, a rejected moov-last, optional real files) through one
// M4aGuardedReader reused with begin() and through a new reader per track, and compares each track with a fresh-process run (the
// vendored currentSampleSize() keeps function-static state, so only a forked child is a clean reference).
// usage: m4a_sequence_test [extra.m4a ...]   (extra files are read-only and never copied)
#include <Arduino.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
#include "../firmware/cydpod_probe/adts_decoder.h"
#include <vector>
#include <string>
using namespace cydpod;
using Bytes = std::vector<uint8_t>;
extern "C" void* helix_malloc(int size) { return std::malloc(size); }
extern "C" void helix_free(void* p) { std::free(p); }

static uint32_t rd32(const Bytes& b, size_t o) { return (uint32_t)b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]; }
static void wr32(Bytes& b, size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) b[o + i] = v >> (24 - 8 * i); }
static size_t findBox(const Bytes& b, size_t s, size_t e, const char* t) {
  for (size_t o = s; o + 8 <= e;) { uint32_t sz = rd32(b, o); if (sz < 8) break; if (!memcmp(&b[o + 4], t, 4)) return o; o += sz; }
  return SIZE_MAX;
}
static size_t child(const Bytes& b, size_t box, const char* t) { return findBox(b, box + 8, box + rd32(b, box), t); }
static Bytes load(const char* path) {
  FILE* f = std::fopen(path, "rb"); if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
  Bytes b; uint8_t buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
  std::fclose(f); return b;
}

// Faststart m4a -> same file with only the first `keep` stsz entries (all ancestor boxes and the mdat resized to match).
static Bytes shrink(const Bytes& b, uint32_t keep) {
  size_t moov = findBox(b, 0, b.size(), "moov"), trak = child(b, moov, "trak"), mdia = child(b, trak, "mdia"),
         minf = child(b, mdia, "minf"), stbl = child(b, minf, "stbl"), stsz = child(b, stbl, "stsz");
  size_t mdat = findBox(b, moov + rd32(b, moov), b.size(), "mdat");
  uint32_t count = rd32(b, stsz + 16); size_t tab = stsz + 20, dropped = 4ull * (count - keep);
  size_t payload = 0; for (uint32_t i = 0; i < keep; ++i) payload += rd32(b, tab + 4 * i);
  Bytes r(b.begin(), b.begin() + tab + 4 * keep);
  r.insert(r.end(), b.begin() + tab + 4 * count, b.begin() + mdat);
  size_t m2 = r.size(); r.insert(r.end(), b.begin() + mdat, b.begin() + mdat + 8 + payload);
  for (size_t box : {moov, trak, mdia, minf, stbl, stsz}) wr32(r, box, rd32(b, box) - dropped);
  wr32(r, stsz + 16, keep);
  wr32(r, m2, 8 + payload);
  return r;
}
// Aborted playback: the mdat ends 5 bytes short of the first frame (the table still lists every frame).
static Bytes cutInFirstFrame(const Bytes& b) {
  Bytes r = b; size_t moov = findBox(r, 0, r.size(), "moov"), mdat = findBox(r, moov + rd32(r, moov), r.size(), "mdat");
  size_t trak = child(r, moov, "trak"), stsz = child(r, child(r, child(r, child(r, trak, "mdia"), "minf"), "stbl"), "stsz");
  size_t cut = mdat + 8 + rd32(r, stsz + 20) - 5; r.resize(cut); wr32(r, mdat, (uint32_t)(cut - mdat)); return r;
}

struct Out { int status; size_t frames, adtsBytes, samples; uint64_t adtsHash, pcmHash; };
static uint64_t fnv(const void* p, size_t n) { auto* c = (const uint8_t*)p; uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; ++i) h = (h ^ c[i]) * 1099511628211ull; return h; }
static bool same(const Out& a, const Out& b) { return a.status == b.status && a.frames == b.frames && a.adtsBytes == b.adtsBytes && a.samples == b.samples && a.adtsHash == b.adtsHash && a.pcmHash == b.pcmHash; }

static Out decode(Out o, const Bytes& adts) {
  AdtsDecoder dec; Bytes pcm;
  if (!adts.empty() && dec.begin()) {
    size_t pos = 0;
    for (;;) {
      DecodeResult r = dec.next([&](uint8_t* p, size_t n) { n = std::min(n, adts.size() - pos); std::memcpy(p, adts.data() + pos, n); pos += n; return n; });
      if (r != DecodeResult::frame) break;
      { auto* s = reinterpret_cast<const uint8_t*>(dec.pcm()); pcm.insert(pcm.end(), s, s + 4096); } ++o.frames;
    }
  }
  o.adtsBytes = adts.size(); o.adtsHash = fnv(adts.data(), adts.size()); o.samples = pcm.size() / 4; o.pcmHash = fnv(pcm.data(), pcm.size());
  return o;
}

// One track through `r` (begin() is called here, so a reused reader goes through begin() again).
static Out playM4a(M4aGuardedReader& r, const Bytes& in, size_t slice = 1024) {
  Bytes adts; r.begin([&](const uint8_t* p, size_t n) { adts.insert(adts.end(), p, p + n); });
  M4aStatus st = M4aStatus::ok;
  for (size_t i = 0; i < in.size() && st == M4aStatus::ok; i += slice) st = r.write(in.data() + i, std::min(slice, in.size() - i));
  if (st == M4aStatus::ok) st = r.finish();
  Out o{(int)st, 0, 0, 0, 0, 0}; return decode(o, adts);
}
struct Track { std::string name; Bytes bytes; bool aac; };
static Out play(M4aGuardedReader* r, const Track& t) {
  if (t.aac) return decode(Out{0, 0, 0, 0, 0, 0}, t.bytes);
  return playM4a(*r, t.bytes);
}
static Out reference(const Track& t) {  // fresh process, fresh reader: the only state-free baseline
  int fd[2]; if (pipe(fd)) std::abort();
  pid_t pid = fork();
  if (pid == 0) { auto* r = new M4aGuardedReader; Out o = play(r, t); (void)!write(fd[1], &o, sizeof o); _exit(0); }
  Out o{-1, 0, 0, 0, 0, 0}; if (read(fd[0], &o, sizeof o) != sizeof o) o.status = -1;
  int s; waitpid(pid, &s, 0); close(fd[0]); close(fd[1]); return o;
}

static int failures;
static std::vector<std::pair<std::string, Out>> refs;  // computed before anything is played in this process (a fork inherits statics)
static const Out& refOf(const Track& t) { for (auto& r : refs) if (r.first == t.name) return r.second; std::abort(); }
// Each sequence runs in its own forked child: statics start clean (as in the parent before any play) and a crash is reported.
static int sequenceInChild(const std::vector<Track>& seq, bool reuse) {
  M4aGuardedReader* shared = reuse ? new M4aGuardedReader : nullptr;
  int bad = 0;
  for (const Track& t : seq) {
    const Out& ref = refOf(t);
    std::fprintf(stderr, "  .... %-30s", t.name.c_str());
    M4aGuardedReader* r = shared ? shared : new M4aGuardedReader;
    Out got = play(r, t);
    if (!shared) delete r;
    bool ok = same(got, ref);
    std::fprintf(stderr, "  %-4s %-30s status=%-16s frames=%zu/%zu samples=%zu/%zu adts=%zu/%zu pcm %s\n", ok ? "ok" : "FAIL", t.name.c_str(),
                 got.status < 0 ? "?" : m4aStatusName((M4aStatus)got.status), got.frames, ref.frames, got.samples, ref.samples,
                 got.adtsBytes, ref.adtsBytes, got.pcmHash == ref.pcmHash ? "same" : "DIFFERENT");
    bad += !ok;
  }
  return bad;
}
static void runSequence(const char* label, const std::vector<Track>& seq, bool reuse) {
  std::fprintf(stderr, "-- %s (%s)\n", label, reuse ? "one reader reused via begin()" : "new reader per track");
  std::fflush(stderr);
  pid_t pid = fork();
  if (pid == 0) _exit(std::min(sequenceInChild(seq, reuse), 100));
  int s = 0; waitpid(pid, &s, 0);
  if (WIFSIGNALED(s)) { std::fprintf(stderr, "\n  CRASH signal %d\n", WTERMSIG(s)); ++failures; }
  else failures += WEXITSTATUS(s);
}

int main(int argc, char** argv) {
  Bytes base = load("fixtures/stereo-faststart.m4a");
  Track aac{"stereo-192.aac", load("fixtures/stereo-192.aac"), true};
  Track m4a{"faststart.m4a (131 frames)", base, false};
  Track m40{"variant 40-frame table", shrink(base, 40), false};
  Track m1{"variant 1-frame table", shrink(base, 1), false};
  Track cut{"aborted inside frame 0", cutInFirstFrame(base), false};
  Track bad{"moov-last (rejected)", load("fixtures/stereo-moov-last.m4a"), false};

  std::vector<Track> seqA{aac, m4a, m40, m4a, bad, m4a};
  std::vector<Track> seqB{m4a, m1, m4a, cut, m4a, m40, m4a};
  for (int i = 1; i < argc; ++i) { seqA.push_back(Track{argv[i], load(argv[i]), false}); seqA.push_back(m4a); }
  for (const auto* seq : {&seqA, &seqB}) for (const Track& t : *seq) { bool have = false; for (auto& r : refs) have |= r.first == t.name; if (!have) refs.push_back({t.name, reference(t)}); }
  for (bool reuse : {false, true}) { runSequence("sequence A", seqA, reuse); runSequence("sequence B", seqB, reuse); }
  std::fprintf(stderr, failures ? "FAILED %d track comparisons\n" : "PASS every track equals its fresh-reader result\n", failures);
  return failures != 0;
}
