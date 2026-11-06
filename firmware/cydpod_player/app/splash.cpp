// splash.cpp -- binds splash_core.h to the TFT_eSPI panel. See splash_core.h.
#include "splash.h"
#include "display.h"
#include "splash_core.h"

namespace {
struct TftSink {
  void fill(uint16_t c) { tft.fillScreen(c); }
  void row(int x, int y, int n, uint16_t* px) {
    for (int i = 0; i < n; i++) px[i] = (px[i] >> 8) | (px[i] << 8);  // pushImage sends the buffer as-is
    tft.pushImage(x, y, n, 1, px);
  }
};
}  // namespace

void splash_show(const char* label) {
  TftSink sink;
  tft.startWrite();
  splash::render(sink, tft.width(), tft.height());
  tft.endWrite();
  if (label && *label) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(splash::INK, splash::BACKGROUND);
    tft.drawString(label, tft.width() / 2, tft.height() - 16, 2);
  }
}
