// LOGO-1: renders the startup screen (splash_core.h) into fake RGB565 framebuffers for all four
// rotations and checks background, centring, margins and ink; dumps PPMs for the PNG helper.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "splash_core.h"

struct Fb {
  int w, h;
  std::vector<uint16_t> px;
  int rows = 0, maxN = 0;
  Fb(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 0x1234) {}
  void fill(uint16_t c) { for (auto& p : px) p = c; }
  void row(int x, int y, int n, uint16_t* src) {
    if (x < 0 || y < 0 || x + n > w || y >= h) { std::printf("FAIL row out of bounds %d,%d,%d\n", x, y, n); std::exit(1); }
    for (int i = 0; i < n; i++) px[(size_t)y * w + x + i] = src[i];
    rows++;
    if (n > maxN) maxN = n;
  }
};

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL " __VA_ARGS__); std::printf("\n"); } } while (0)

static void dump(const Fb& f, const char* path) {
  FILE* o = std::fopen(path, "wb");
  if (!o) return;
  std::fprintf(o, "P6\n%d %d\n255\n", f.w, f.h);
  for (uint16_t p : f.px) {
    const unsigned r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    const unsigned char c[3] = {(unsigned char)(r << 3 | r >> 2), (unsigned char)(g << 2 | g >> 4), (unsigned char)(b << 3 | b >> 2)};
    std::fwrite(c, 1, 3, o);
  }
  std::fclose(o);
}

int main(int argc, char** argv) {
  const char* outdir = argc > 1 ? argv[1] : ".";
  CHECK(splash::BACKGROUND == 0xFE83, "background constant 0x%04X", splash::BACKGROUND);
  CHECK(splash::INK == 0x18E3, "ink constant 0x%04X", splash::INK);
  const int dims[4][2] = {{240, 320}, {320, 240}, {240, 320}, {320, 240}};   // rotations 0..3
  for (int rot = 0; rot < 4; rot++) {
    const int w = dims[rot][0], h = dims[rot][1];
    Fb f(w, h);
    splash::render(f, w, h);
    int x0 = w, x1 = -1, y0 = h, y1 = -1, ink = 0, other = 0;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const uint16_t p = f.px[(size_t)y * w + x];
        if (p == splash::BACKGROUND) continue;
        if (p == splash::INK) ink++;
        else other++;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    CHECK(ink > 0 && other > 0, "rot %d ink=%d antialias=%d", rot, ink, other);
    const int cx2 = x0 + x1 + 1, cy2 = y0 + y1 + 1;           // twice the centre
    const int dx = cx2 - w, dy = cy2 - 2 * splash::centreY(h);
    CHECK(dx >= -4 && dx <= 4, "rot %d horizontal centre off by %d/2 px", rot, dx);
    CHECK(dy >= -4 && dy <= 4, "rot %d vertical centre off by %d/2 px", rot, dy);
    CHECK(x0 >= w / 10 && w - 1 - x1 >= w / 10, "rot %d side margins %d/%d < 10%% of %d", rot, x0, w - 1 - x1, w);
    CHECK(y0 >= h / 10 && h - 1 - y1 >= h / 10, "rot %d vertical margins %d/%d", rot, y0, h - 1 - y1);
    CHECK(f.maxN <= splash::MAX_ROW, "rot %d row buffer", rot);
    std::printf("rot %d %dx%d: bbox x %d..%d y %d..%d, ink %d, antialiased %d, rows pushed %d\n",
                rot, w, h, x0, x1, y0, y1, ink, other, f.rows);
    if (rot == 0 || rot == 1) {
      char path[512];
      std::snprintf(path, sizeof path, "%s/logo1-%s.ppm", outdir, rot == 0 ? "portrait" : "landscape");
      dump(f, path);
    }
  }
  if (failures) { std::printf("FAIL splash_test: %d failure(s)\n", failures); return 1; }
  std::printf("PASS splash_test: background 0x%04X exact, centred, margins >= 10%%, ink present, rotations 0-3\n", splash::BACKGROUND);
  return 0;
}
