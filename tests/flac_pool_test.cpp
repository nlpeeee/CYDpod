#include "../firmware/cydpod_player/app/codec_track.h"
#include "../firmware/cydpod_player/app/flac_pool.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
// Host checks for the FLAC codec on the shared pool: bit-exact PCM, clean rejects, bounded metadata and seek reads.
struct Row { std::string name, status; unsigned rate, ch; uint64_t frames; };
static std::vector<int16_t> slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary); std::vector<char> raw((std::istreambuf_iterator<char>(in)), {});
  std::vector<int16_t> out(raw.size() / 2); memcpy(out.data(), raw.data(), out.size() * 2); return out;
}
static std::string flacPath(const std::string& n) { return "tests/out/flac/" + n + ".flac"; }
struct Hook { unsigned calls = 0, cancelAt = 0; };
static int hook(void* p) { Hook* h = static_cast<Hook*>(p); ++h->calls; return h->cancelAt && h->calls >= h->cancelAt; }
static int64_t bigBudget() { return int64_t(1) << 40; }
static bool drain(CodecTrack& t, unsigned ch, std::vector<int16_t>& got, size_t limit, const char** error) {
  for (;;) {
    if (got.size() / ch >= limit) return true;
    auto rc = t.next();
    if (rc == CodecTrack::Pull::error) { *error = t.name(); return false; }
    if (rc == CodecTrack::Pull::eof) return true;
    while (t.pending() && got.size() / ch < limit) {
      auto s = *t.pcm(); got.push_back(s.l); if (ch == 2) got.push_back(s.r); t.consume(1);
    }
  }
}
int main() {
  std::ifstream mf("tests/out/flac/manifest.tsv"); std::string line; std::vector<Row> rows;
  while (std::getline(mf, line)) {
    std::istringstream ls(line); Row r; std::string rate, ch, frames;
    std::getline(ls, r.name, '\t'); std::getline(ls, r.status, '\t'); std::getline(ls, rate, '\t'); std::getline(ls, ch, '\t'); std::getline(ls, frames, '\t');
    r.rate = std::stoul(rate); r.ch = std::stoul(ch); r.frames = std::stoull(frames); rows.push_back(r);
  }
  assert(rows.size() >= 20);
  CodecTrack track; unsigned positives = 0, rejects = 0;
  for (const Row& r : rows) {
    const std::vector<int16_t> want = slurp("tests/out/flac/" + r.name + ".pcm");
    File f(flacPath(r.name), "r"); assert(f);
    const std::string kind = r.status.substr(0, r.status.find(':')), what = r.status.find(':') == std::string::npos ? "" : r.status.substr(r.status.find(':') + 1);
    if (r.name == "stereo-tags-table") hostReadBudget = 8192; // ~160 kB of tags/PICTURE/padding must be skipped by offset
    auto rc = track.begin(f); hostReadBudget = -1;
    if (kind == "reject") {
      if (rc == MINIAAC_OK) fprintf(stderr, "expected reject %s\n", r.name.c_str());
      assert(rc != MINIAAC_OK); assert(what == track.name());
      printf("PASS reject %-18s -> %s\n", r.name.c_str(), track.name()); ++rejects; track.end(); f.close(); continue;
    }
    assert(rc == MINIAAC_OK); assert(track.sampleRate() == r.rate); assert(track.totalFrames() == r.frames || kind != "ok");
    std::vector<int16_t> got; const char* error = nullptr; const bool clean = drain(track, r.ch, got, SIZE_MAX, &error);
    if (kind == "stop") { assert(!clean && what == error && got.empty()); printf("PASS stop   %-18s -> %s\n", r.name.c_str(), error); ++rejects; }
    else if (kind == "truncated") {
      assert(!clean && what == error); assert(got.size() == (r.frames - 100) * r.ch && !memcmp(got.data(), want.data(), got.size() * 2));
      printf("PASS truncated %-15s -> %s after %zu exact frames\n", r.name.c_str(), error, got.size() / r.ch);
    } else {
      assert(clean); if (got.size() != want.size()) fprintf(stderr, "%s: got %zu want %zu\n", r.name.c_str(), got.size(), want.size());
      assert(got.size() == want.size() && !memcmp(got.data(), want.data(), got.size() * 2));
      printf("PASS %s %-18s %u Hz x%u: %zu frames bit-exact\n", kind.c_str(), r.name.c_str(), r.rate, r.ch, got.size() / r.ch); ++positives;
    }
    track.end(); f.close();
  }
  // 48 kHz goes through the existing Q15 FIR: output count tracks 44.1/48 plus the 127-frame flush.
  { File f(flacPath("48000-2-4096"), "r"); assert(track.begin(f) == MINIAAC_OK); size_t out = 0; auto sink = [](void* u, int16_t, int16_t) { ++*static_cast<size_t*>(u); return true; };
    for (;;) { auto rc = track.next(); if (rc != CodecTrack::Pull::frames) { assert(rc == CodecTrack::Pull::eof); break; }
      while (track.pending()) { auto s = *track.pcm(); assert(track.output(s.l, s.r, sink, &out)); track.consume(1); } }
    assert(track.drain(sink, &out)); const size_t base = 12805ull * 44100 / 48000; assert(out >= base && out <= base + 130);
    printf("PASS 48 kHz FLAC -> FIR: %zu output frames for 12805 source frames\n", out); track.end(); f.close(); }
  // Seeks: exact against the source PCM, bytes read per seek capped, cancellable, restartable.
  static FlacState direct; // host-only second instance for the counters
  const char* seekNames[] = {"table-long", "notable-long", "stereo-tags-table", "variable-long", "mono-long", "44100-2-1152"};
  unsigned seeks = 0;
  for (const char* name : seekNames) {
    const Row* row = nullptr; for (const Row& r : rows) if (r.name == name) row = &r; assert(row);
    const std::vector<int16_t> want = slurp(std::string("tests/out/flac/") + name + ".pcm");
    File f(flacPath(name), "r"); assert(track.begin(f) == MINIAAC_OK); assert(track.seekable());
    const uint32_t durationMs = uint32_t(row->frames * 1000 / row->rate);
    uint32_t targets[] = {0, 1, 37, 500, durationMs / 7, durationMs / 3, durationMs / 2, durationMs * 3 / 4, durationMs - 200, durationMs - 1, durationMs + 5000};
    int64_t worst = 0; size_t compared = 0;
    for (uint32_t ms : targets) {
      Hook h; hostReadBudget = bigBudget(); auto rc = track.seek(ms, hook, &h); const int64_t bytes = bigBudget() - hostReadBudget; hostReadBudget = -1;
      assert(rc == MINIAAC_OK); assert(bytes <= int64_t(FlacState::seekBudget) + 4096); if (bytes > worst) worst = bytes;
      uint64_t target = uint64_t(ms) * row->rate / 1000; if (target >= row->frames) target = row->frames - 1;
      std::vector<int16_t> got; const char* error = nullptr; assert(drain(track, row->ch, got, 3000, &error));
      const size_t n = got.size() / row->ch; assert(n > 0 || target + 1 >= row->frames);
      const size_t take = std::min<size_t>(n, 3000);
      if (memcmp(got.data(), want.data() + target * row->ch, take * row->ch * 2)) { fprintf(stderr, "seek mismatch %s ms=%u target=%llu\n", name, ms, (unsigned long long)target); assert(false); }
      compared += take; ++seeks;
    }
    // Cancellation: the hook stops the second probe; the next seek still lands exactly.
    { Hook h; h.cancelAt = 2; auto rc = track.seek(durationMs / 2, hook, &h); assert(rc == MINIAAC_CANCELLED && h.calls == 2);
      Hook ok; assert(track.seek(durationMs / 4, hook, &ok) == MINIAAC_OK); std::vector<int16_t> got; const char* e = nullptr; assert(drain(track, row->ch, got, 200, &e));
      const uint64_t target = uint64_t(durationMs / 4) * row->rate / 1000; assert(!memcmp(got.data(), want.data() + target * row->ch, 200 * row->ch * 2)); }
    printf("PASS seek %-18s %zu targets exact (%zu frames compared); worst read %lld B (cap %u); cancel+restart ok\n", name, sizeof(targets) / sizeof(targets[0]), compared, (long long)worst, FlacState::seekBudget);
    track.end(); f.close();
  }
  // Hostile input: flipped bytes, cuts and garbage must terminate with eof/error and never write outside the pool (ASan/UBSan build).
  { std::ifstream in(flacPath("notable-long"), std::ios::binary); std::vector<unsigned char> src((std::istreambuf_iterator<char>(in)), {});
    unsigned state = 12345, eofs = 0, errs = 0; auto rnd = [&] { state = state * 1103515245u + 12345u; return state >> 8; };
    for (unsigned v = 0; v < 160; ++v) {
      auto bad = src; const unsigned mode = v % 4;
      if (mode == 0) for (unsigned k = 0; k < 1 + v / 8; ++k) bad[60 + rnd() % (bad.size() - 60)] ^= 1 << (rnd() % 8);
      else if (mode == 1) bad.resize(60 + rnd() % (bad.size() - 60));
      else if (mode == 2) { size_t at = 60 + rnd() % (bad.size() - 300); for (unsigned k = 0; k < 200; ++k) bad[at + k] = rnd(); }
      else for (unsigned k = 0; k < 30; ++k) bad[8 + rnd() % 60] ^= 0xFF; // damage metadata/first headers
      std::ofstream out("tests/out/flac/fuzz.flac", std::ios::binary); out.write((const char*)bad.data(), bad.size()); out.close();
      File f("tests/out/flac/fuzz.flac", "r"); if (track.begin(f) != MINIAAC_OK) { ++errs; track.end(); f.close(); continue; }
      std::vector<int16_t> got; const char* error = nullptr; const bool clean = drain(track, 2, got, 400000, &error); clean ? ++eofs : ++errs;
      Hook h; track.seek((rnd() % 4000), hook, &h); // seek over damaged data also terminates
      track.end(); f.close();
    }
    printf("PASS hostile input: 160 damaged variants terminated (%u ended, %u rejected/errored), no crash\n", eofs, errs); }
  printf("PASS FLAC pool: %u decoded fixtures, %u clean rejects, %u seeks, sizeof(FlacState)=%zu (host)\n", positives, rejects, seeks, sizeof(FlacState));
  (void)direct;
}
