// splash.cpp -- binds splash_core.h to the TFT_eSPI panel. See splash_core.h.
#include "splash.h"
#include "display.h"
#include "splash_core.h"

namespace {
struct TftSink {
  void rect(int x, int y, int w, int h, uint16_t c) { if (w > 0) tft.fillRect(x, y, w, h, c); }
  void text(const char* text, int cx, int y, int font) {
    tft.setTextSize(1); tft.setTextPadding(0); tft.setTextDatum(TC_DATUM);
    tft.setTextColor(splash::INK, splash::BACKGROUND);
    tft.drawString(text, cx, y, font);
  }
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

namespace { splash::Progress progress; uint32_t shownAt; }
void splash_progress(const char* phase, int percent) {
  TftSink sink;
  progress.update(sink, tft.width(), tft.height(), phase, percent, millis());
}
void splash_tick() {
  TftSink sink;
  progress.tick(sink, tft.width(), tft.height(), millis());
}
void splash_warning(const char* phase, const char* detail) {
  TftSink sink;
  progress.update(sink, tft.width(), tft.height(), phase, -1, millis(), true, detail ? detail : "");
}
void splash_start() {
  shownAt = millis(); progress = splash::Progress{};
  splash_progress("Starting...", 0);
}
void splash_finish() {
  while (uint32_t(millis() - shownAt) < splash::MINIMUM_MS) { splash_tick(); delay(1); }
  progress.active = false;
}
