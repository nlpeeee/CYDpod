// Runs the guarded reader over real .m4a files (read-only, streamed) and prints the verdict per file.
#include <Arduino.h>
#include "../lib/cydpod_m4a/m4a_guarded_reader.h"
using namespace cydpod;

int main(int argc, char** argv) {
  int rejected = 0;
  for (int a = 1; a < argc; ++a) {
    FILE* f = std::fopen(argv[a], "rb"); if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[a]); return 2; }
    auto* r = new M4aGuardedReader; size_t frames = 0, adtsBytes = 0;
    r->begin([&](const uint8_t*, size_t n) { ++frames; adtsBytes += n; });
    uint8_t b[1024]; size_t n; M4aStatus st = M4aStatus::ok;
    while (st == M4aStatus::ok && (n = std::fread(b, 1, sizeof b, f)) > 0) st = r->write(b, n);
    if (st == M4aStatus::ok) st = r->finish();
    std::fclose(f);
    std::fprintf(stderr, "%-8s %-17s frames=%zu adts_bytes=%zu %.40s\n", st == M4aStatus::ok ? "ACCEPT" : "REJECT", m4aStatusName(st), frames, adtsBytes, argv[a] + std::strlen(argv[a]) - std::min<size_t>(40, std::strlen(argv[a])));
    rejected += st != M4aStatus::ok; delete r;
  }
  std::fprintf(stderr, "rejected %d of %d\n", rejected, argc - 1);
  return 0;
}
