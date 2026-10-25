// Host test: unmodified AudioTools M4AAudioDemuxer (faststart .m4a) -> ADTS frames -> Helix, vs the .aac twin.
#include <Arduino.h>
#include "AudioTools/AudioCodecs/M4AAudioDemuxer.h"
#include "../firmware/cydpod_probe/adts_decoder.h"
#include <cstdio>
#include <vector>
using namespace cydpod;
extern "C" void* helix_malloc(int size) { return std::malloc(size); }
extern "C" void helix_free(void* p) { std::free(p); }

struct Decoded { unsigned frames = 0; std::vector<int16_t> pcm; };

static bool readFile(const char* path, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(path, "rb"); if (!f) return false;
  uint8_t b[4096]; size_t n;
  while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + n);
  std::fclose(f); return true;
}

static bool decodeAdts(const std::vector<uint8_t>& adts, Decoded& d, const char* name) {
  AdtsDecoder dec; if (!dec.begin()) return false;
  size_t pos = 0;
  for (;;) {
    DecodeResult r = dec.next([&](uint8_t* p, size_t n) { n = std::min(n, adts.size() - pos); std::memcpy(p, adts.data() + pos, n); pos += n; return n; });
    if (r == DecodeResult::eof) return true;
    if (r != DecodeResult::frame) { std::fprintf(stderr, "FAIL %s: %s helix=%d after %u frames\n", name, resultName(r), dec.lastError, d.frames); return false; }
    const int16_t* s = reinterpret_cast<const int16_t*>(dec.pcm());
    d.pcm.insert(d.pcm.end(), s, s + 2048); ++d.frames;
  }
}

int main(int argc, char** argv) {
  if (argc != 3) { std::fprintf(stderr, "usage: m4a_host_test in.m4a twin.aac\n"); return 2; }
  std::vector<uint8_t> m4a, twin;
  if (!readFile(argv[1], m4a) || !readFile(argv[2], twin)) return 2;

  std::vector<uint8_t> adts;
  audio_tools::M4AAudioDemuxer demux;
  demux.setReference(&adts);
  demux.setCallback([](const audio_tools::M4ACommonDemuxer::Frame& f, void* ref) {
    auto* v = static_cast<std::vector<uint8_t>*>(ref);
    v->insert(v->end(), f.data, f.data + f.size);
  });
  demux.begin();
  for (size_t i = 0; i < m4a.size(); i += 1024) demux.write(m4a.data() + i, std::min<size_t>(1024, m4a.size() - i));

  Decoded a, b;
  if (!decodeAdts(adts, a, "m4a") || !decodeAdts(twin, b, "aac")) return 4;
  std::printf("m4a  frames=%u samples_per_channel=%zu adts_bytes=%zu\n", a.frames, a.pcm.size() / 2, adts.size());
  std::printf("aac  frames=%u samples_per_channel=%zu adts_bytes=%zu\n", b.frames, b.pcm.size() / 2, twin.size());
  if (!a.frames || a.frames != b.frames || a.pcm.size() != b.pcm.size()) { std::printf("FAIL count mismatch\n"); return 1; }
  bool same = a.pcm == b.pcm;
  std::printf("pcm bytes %s\n", same ? "IDENTICAL" : "DIFFER");
  std::printf("PASS counts equal\n");
  return 0;
}
