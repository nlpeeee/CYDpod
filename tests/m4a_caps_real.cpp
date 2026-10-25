// Read-only regression: harness [media-folder [manifest.tsv]]. Exit 77 = SKIP.
#include <Arduino.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
using namespace cydpod;
namespace fs = std::filesystem;
struct Expected { std::string status; size_t frames; };

int main(int argc, char** argv) {
  if (argc > 3) {
    std::fprintf(stderr, "Usage: %s [media-folder [manifest.tsv]]\n", argv[0]);
    return 1;
  }
  const char* folder = argc > 1 ? argv[1] : std::getenv("CYDPOD_REAL_MEDIA");
  if (!folder || !*folder) {
    std::fprintf(stderr, "SKIP: set CYDPOD_REAL_MEDIA or pass the media folder (exit 77)\n");
    return 77;
  }
  std::error_code ec;
  bool exists = fs::exists(folder, ec);
  if (ec) {
    std::fprintf(stderr, "FAIL: cannot inspect folder: %s\n", ec.message().c_str());
    return 1;
  }
  if (!exists) {
    std::fprintf(stderr, "SKIP: real-media folder not present: %s (exit 77)\n", folder);
    return 77;
  }
  if (!fs::is_directory(folder, ec) || ec) {
    std::fprintf(stderr, "FAIL: media path is not an accessible directory\n");
    return 1;
  }
  const char* manifest = argc > 2 ? argv[2] : "tests/real_media_expected.tsv";
  std::ifstream input(manifest);
  if (!input) {
    std::fprintf(stderr, "FAIL: cannot open manifest: %s\n", manifest);
    return 1;
  }
  std::map<std::string, Expected> expected;
  std::string line;
  size_t lineNo = 0;
  while (std::getline(input, line)) {
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line);
    std::string name, status, count, extra;
    std::getline(row, name, '\t');
    std::getline(row, status, '\t');
    std::getline(row, count, '\t');
    bool validCount = !count.empty() && std::all_of(count.begin(), count.end(),
        [](unsigned char c) { return std::isdigit(c); });
    size_t frames = 0;
    try { if (validCount) frames = std::stoull(count); }
    catch (...) { validCount = false; }
    if (name.empty() || fs::path(name).filename() != name || name == "." || name == ".." ||
        (status != "ok" && status != "too_many_samples") || !validCount ||
        std::getline(row, extra, '\t') || !expected.emplace(name, Expected{status, frames}).second) {
      std::fprintf(stderr, "FAIL: invalid/duplicate manifest row %zu\n", lineNo);
      return 1;
    }
  }
  if (input.bad() || expected.empty()) {
    std::fprintf(stderr, "FAIL: unreadable or empty manifest\n");
    return 1;
  }
  size_t accepted = 0, rejected = 0, mismatches = 0;
  try {
    for (const auto& entry : fs::directory_iterator(folder)) {
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
      if (ext == ".m4a" && !expected.count(entry.path().filename().string())) {
        std::fprintf(stderr, "FAIL: unlisted media file: %s\n", entry.path().filename().c_str());
        ++mismatches;
      }
    }
    for (const auto& item : expected) {
      fs::path path = fs::path(folder) / item.first;
      FILE* f = fs::is_regular_file(path) ? std::fopen(path.c_str(), "rb") : nullptr;
      if (!f) {
        std::fprintf(stderr, "FAIL: missing/unreadable media file: %s\n", item.first.c_str());
        ++mismatches;
        continue;
      }
      M4aGuardedReader reader;
      size_t frames = 0;
      reader.begin([&](const uint8_t*, size_t) { ++frames; });
      M4aStatus st = M4aStatus::ok;
      uint8_t buffer[1024];
      size_t n;
      while (st == M4aStatus::ok && (n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
        st = reader.write(buffer, n);
      bool readError = std::ferror(f);
      if (st == M4aStatus::ok && !readError) st = reader.finish();
      std::fclose(f);
      const char* status = m4aStatusName(st);
      accepted += st == M4aStatus::ok;
      rejected += st != M4aStatus::ok;
      bool match = !readError && status == item.second.status && frames == item.second.frames;
      mismatches += !match;
      std::fprintf(stderr, "%s %s frames=%zu expected=%s/%zu %s\n",
          match ? "MATCH" : "FAIL", status, frames, item.second.status.c_str(),
          item.second.frames, item.first.c_str());
    }
  } catch (const fs::filesystem_error& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    ++mismatches;
  }
  std::fprintf(stderr, "%s accepted=%zu rejected=%zu expected=%zu mismatches=%zu\n",
      mismatches ? "FAIL" : "PASS", accepted, rejected, expected.size(), mismatches);
  return mismatches ? 1 : 0;
}
