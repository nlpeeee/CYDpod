// LOGO-1: renders the startup screen (splash_core.h) into fake RGB565 framebuffers for all four
// rotations and checks background, centring, margins and ink; dumps PPMs for the PNG helper.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "splash_core.h"
#define PROGMEM
#include "Fonts/Font16.c"
#include "Fonts/glcdfont.c"

struct Fb {
  int w, h;
  std::vector<uint16_t> px;
  int rows = 0, maxN = 0;
  Fb(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 0x1234) {}
  void rect(int x, int y, int width, int height, uint16_t c) {
    if (x < 0 || y < 0 || x + width > w || y + height > h) std::abort();
    for (int yy = y; yy < y + height; ++yy)
      for (int xx = x; xx < x + width; ++xx) px[yy * w + xx] = c;
  }
  void text(const char* str, int cx, int y, int face) {
    int width = 0;
    for (const char* c = str; *c; ++c) width += face == 2 ? widtbl_f16[*c - 32] : 6;
    int x = cx - width / 2;
    for (const char* c = str; *c; ++c) {
      const int advance = face == 2 ? widtbl_f16[*c - 32] : 6;
      const int stride = (advance + 6) / 8;
      for (int yy = 0; yy < (face == 2 ? 16 : 8); ++yy)
        for (int xx = 0; xx < advance; ++xx) {
          const bool ink = face == 2 ? (chrtbl_f16[*c - 32][yy * stride + xx / 8] & (0x80 >> (xx % 8))) :
                                           xx < 5 && (font[(unsigned char)*c * 5 + xx] & (1 << yy));
          if (ink) rect(x + xx, y + yy, 1, 1, splash::INK);
        }
      x += advance;
    }
  }
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

static void progressTest(int rot, const char* outdir) {
  const int w = rot & 1 ? 320 : 240, h = rot & 1 ? 240 : 320;
  Fb f(w, h), strip(w * 8, h);
  splash::render(f, w, h);
  f.text("v34", w / 2, h - 24, 2);
  const auto base = f.px;
  const auto l = splash::progressLayout(w, h);
  const auto logo = splash::layout(w, h);
  CHECK(l.x >= w / 10 && l.x + l.w <= w - w / 10 && l.y > logo.y + logo.h &&
        l.textY() + 36 <= h - 24, "rot %d progress margins/overlap", rot);
  const char* labels[] = {"Starting...", "Sorting 10,000 songs", "Preparing song titles", "Sorting songs", "Sorting songs",
                          "No SD card", "SD card is read-only", "Library stale"};
  const char* details[] = {"", "", "", "", "", "insert a FAT32 card with /music", "library may be out of date", "using the previous list"};
  const int pc[] = {0, 37, 100, -1, -1, -1, -1, -1};
  splash::Progress p;
  for (int frame = 0; frame < 8; ++frame) {
    const auto before = f.px;
    p.update(f, w, h, labels[frame], pc[frame], frame * 125, frame >= 5, details[frame]);
    int changed = 0, outside = 0, textInk = 0, barInk = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
      const int at = y * w + x;
      const bool bar = x >= l.x && x < l.x + l.w && y >= l.y && y < l.y + 6;
      const bool text = x >= l.x && x < l.x + l.w && y >= l.textY() && y < l.textY() + 36;
      if (f.px[at] != before[at]) { ++changed; if (!bar && !text) ++outside; }
      if (x >= logo.x && x < logo.x + logo.w && y >= logo.y && y < logo.y + logo.h)
        CHECK(f.px[at] == base[at], "rot %d wordmark changed", rot);
      if (y == l.y && bar && f.px[at] == splash::INK) ++barInk;
      if (text && f.px[at] == splash::INK) ++textInk;
      if (frame >= 5 && text) CHECK(f.px[at] == splash::INK || f.px[at] == splash::BACKGROUND, "warning colour");
      strip.px[y * strip.w + frame * w + x] = f.px[at];
    }
    CHECK(outside == 0 && textInk > 0, "rot %d frame %d outside=%d textInk=%d", rot, frame, outside, textInk);
    if (frame < 3) CHECK(std::abs(barInk * 100 - l.w * pc[frame]) <= 100, "rot %d fill %d%% width=%d", rot, pc[frame], barInk);
    if (frame == 4) CHECK(changed > 0, "rot %d indeterminate frozen", rot);
    if (frame >= 5) CHECK(barInk == 0, "warning indicator enabled");
    const auto stable = f.px;
    p.tick(f, w, h, frame * 125 + 124);
    CHECK(f.px == stable, "rot %d throttle", rot);
    std::printf("rot %d frame %d: changed=%d outside=0 bar=%d px\n", rot, frame, changed, barInk);
  }
  const auto warning = f.px; p.tick(f, w, h, 3000);
  CHECK(f.px == warning, "rot %d warning animates", rot);
  p.update(f, w, h, "Sorting songs", 37, 3125); p.tick(f, w, h, 3375);
  const auto stale = f.px; p.tick(f, w, h, 3500);
  CHECK(f.px != stale, "rot %d stale percent does not animate from breathe", rot);
  p.active = false; const auto finished = f.px; p.tick(f, w, h, 4000);
  CHECK(f.px == finished, "finished splash changed");
  if (rot < 2) {
    char path[512]; std::snprintf(path, sizeof path, "%s/logo2-boot-%s.ppm", outdir, rot ? "landscape" : "portrait"); dump(strip, path);
  }
}

int main(int argc, char** argv) {
  const char* outdir = argc > 1 ? argv[1] : ".";
  CHECK(splash::BACKGROUND == 0xFE40, "background constant 0x%04X", splash::BACKGROUND);
  CHECK(splash::TRACK == 0xE5A0, "progress track unchanged 0x%04X", splash::TRACK);
  CHECK(splash::INK == 0x18E3, "ink constant 0x%04X", splash::INK);
  const int dims[4][2] = {{240, 320}, {320, 240}, {240, 320}, {320, 240}};   // rotations 0..3
  for (int rot = 0; rot < 4; rot++) {
    progressTest(rot, outdir);
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
  std::printf("PASS boot_progress: rotations 0-3, 0/37/100%% fill +/-1px, fixed wordmark, outside changes=0, animation/throttle/warnings\n");
  return 0;
}
