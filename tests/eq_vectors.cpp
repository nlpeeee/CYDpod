// EQ-5BAND: writes stage-loop test vectors from cydeq::stageRef (the C++ reference)
// for tests/eq_xtensa_sim.py. Coefficients come from real designs and from
// pseudo-random words, including unstable and out-of-range sets, so the clamp and
// wrapping paths are covered. One case per line:
//   n b0 b1 b2 na1 na2 x1 x2 y1 y2 | 2*97 input words | 2*97 expected words | expected history
#include "eq_core.h"
#include <cstdio>
using namespace cydeq;
static uint32_t seed = 0x2545f491u;
static uint32_t rnd() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
int main(int argc, char** argv) {
  FILE* f = std::fopen(argc > 1 ? argv[1] : "eq_vectors.txt", "w");
  if (!f) return 1;
  const int8_t sets[][Bands] = {{12, 12, 12, 12, 12}, {-12, -12, -12, -12, -12}, {8, -4, 2, 6, -10}};
  int cases = 0;
  for (int t = 0; t < 600; ++t) {
    Stage c;
    if (t < 15) { Coeffs d; design(sets[t / 5], d); c = d.stage[t % 5]; }
    else { int32_t* w = &c.b0; for (int k = 0; k < 5; ++k) w[k] = t % 5 == 0 ? int32_t(rnd()) : int32_t(rnd()) >> 1; }
    History h; int32_t* hw = &h.x1;
    for (int k = 0; k < 4; ++k) hw[k] = int32_t(rnd()) >> (t % 3 ? 3 : 0);
    const uint32_t n = t < 15 ? 97 : t % 98;
    const int ch = t & 1;
    int32_t buf[2 * 97], in[2 * 97];
    for (int i = 0; i < 2 * 97; ++i) in[i] = buf[i] = t < 15 ? int32_t(uint32_t(int16_t(rnd())) << InShift) : int32_t(rnd()) >> (t % 4 ? 3 : 0);
    std::fprintf(f, "%u %d %ld %ld %ld %ld %ld %ld %ld %ld %ld", n, ch, long(c.b0), long(c.b1), long(c.b2), long(c.na1), long(c.na2),
                 long(h.x1), long(h.x2), long(h.y1), long(h.y2));
    stageRef(buf + ch, n, 2, c, h);
    for (int i = 0; i < 2 * 97; ++i) std::fprintf(f, " %ld", long(in[i]));
    for (int i = 0; i < 2 * 97; ++i) std::fprintf(f, " %ld", long(buf[i]));
    std::fprintf(f, " %ld %ld %ld %ld\n", long(h.x1), long(h.x2), long(h.y1), long(h.y2));
    ++cases;
  }
  std::fclose(f);
  std::printf("vectors: %d cases\n", cases);
  return 0;
}
