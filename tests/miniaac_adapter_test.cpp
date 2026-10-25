// CY-31: host test of firmware/cydpod_probe/miniaac_adapter.h over a File shim (tests/host_shim/FS.h).
//   miniaac_adapter_test [media-folder [manifest.tsv [max-files]]]     exit 77 = SKIP (no media folder)
// Decodes real files through the adapter's read callbacks, draining pending() in uneven slices like a nearly full PCM ring, and
// checks them against the old path's per-file counts in tests/real_media_expected.tsv (ADTS frames emitted by the CY-13c reader):
//   decoded blocks must equal the manifest count minus 0..2 (MiniAAC drops blocks wholly inside the edit-list trim) and the PCM frame
//   count must lie inside that many 1024-frame blocks. It does not compare PCM or judge decoder quality (MiniAAC validates itself).
// Also checks the rejection and misuse paths the firmware relies on (missing file data, non-AAC bytes, truncated file, end() twice).
#include "../firmware/cydpod_probe/miniaac_adapter.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
using namespace cydpod;
namespace fs = std::filesystem;
static MiniAacTrack track;  // as in the firmware: one static object, reused for every track
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Result { miniaac_status open; MiniAacTrack::Pull last; uint64_t blocks, frames; size_t reads; const char* name; };
static Result drain(const std::string& path, size_t truncateTo = SIZE_MAX) {
  Result r{MINIAAC_OK, MiniAacTrack::Pull::frames, 0, 0, 0, ""};
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); ++failures; r.open = MINIAAC_IO; return r; }
  FILE* use = f;
  if (truncateTo != SIZE_MAX) {  // truncation test: decode a shortened temp copy
    use = std::tmpfile(); std::vector<uint8_t> buf(truncateTo);
    buf.resize(std::fread(buf.data(), 1, truncateTo, f)); std::fwrite(buf.data(), 1, buf.size(), use); std::rewind(use); std::fclose(f);
  }
  File file(use);
  r.open = track.begin(file); r.name = track.name();
  if (r.open == MINIAAC_OK) {
    unsigned slice = 1;
    for (;;) {
      if (!track.pending()) {
        r.last = track.next();
        if (r.last != MiniAacTrack::Pull::frames) break;
        ++r.blocks;
      }
      size_t n = std::min<size_t>(track.pending(), 37 * slice);  // 37..999: never aligned with a block
      slice = slice % 27 + 1;
      track.consume(n); r.frames += n;
    }
    r.name = track.name();
    r.reads = track.reads.load();
    track.end(); track.end();  // idempotent
  }
  return r;
}

int main(int argc, char** argv) {
  const char* folder = argc > 1 ? argv[1] : std::getenv("CYDPOD_REAL_MEDIA");
  const char* manifest = argc > 2 ? argv[2] : "tests/real_media_expected.tsv";
  const size_t maxFiles = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 25;
  if (!folder || !*folder || !fs::is_directory(folder)) { std::fprintf(stderr, "SKIP: media folder not present (exit 77)\n"); return 77; }
  std::ifstream in(manifest);
  if (!in) { std::fprintf(stderr, "FAIL: cannot open manifest\n"); return 1; }
  std::map<std::string, size_t> expected;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line); std::string name, status, count;
    std::getline(row, name, '\t'); std::getline(row, status, '\t'); std::getline(row, count, '\t');
    if (status == "ok") expected[name] = std::stoull(count);
  }
  size_t done = 0; std::string firstPath;
  for (const auto& [name, count] : expected) {
    if (done == maxFiles) break;
    const std::string path = (fs::path(folder) / name).string();
    if (firstPath.empty()) firstPath = path;
    Result r = drain(path);
    const bool inRange = r.blocks <= count && r.blocks + 2 >= count && r.frames <= r.blocks * 1024 && r.frames + 1024 > r.blocks * 1024;
    const bool ok = r.open == MINIAAC_OK && r.last == MiniAacTrack::Pull::eof && inRange && !std::strcmp(r.name, "ok");
    std::fprintf(stderr, "%s blocks=%llu manifest=%zu pcm_frames=%llu reads=%zu %s\n", ok ? "MATCH" : "FAIL",
                 (unsigned long long)r.blocks, count, (unsigned long long)r.frames, r.reads, name.c_str());
    failures += !ok; ++done;
  }
  CHECK(done > 0);
  // Misuse and rejection paths.
  { Result r = drain(firstPath, 4096); CHECK((r.open != MINIAAC_OK || r.last == MiniAacTrack::Pull::error) && std::strcmp(r.name, "ok") != 0); }  // cut file: clean failure, named
  { Result r = drain(firstPath, 64);   CHECK(r.open != MINIAAC_OK && std::strcmp(r.name, "ok") != 0); }
  { Result r = drain(manifest);        CHECK(r.open != MINIAAC_OK); }  // a TSV is not AAC
  { Result r = drain(firstPath); CHECK(r.open == MINIAAC_OK && r.last == MiniAacTrack::Pull::eof); }  // reuse after failures
  CHECK(track.next() == MiniAacTrack::Pull::error);  // next() on a closed track is an error, never a crash
  std::fprintf(stderr, "%s files=%zu failures=%d\n", failures ? "FAIL" : "PASS", done, failures);
  return failures ? 1 : 0;
}
