#include "touch.h"
#include "board.h"
#include "display.h"
#if !CYD_TOUCH_CAPACITIVE
#include <Preferences.h>

static Preferences prefs;
static const char* NVS_NS = "mp3touch";

// Fallback until the wizard runs: measured on the owner's board in stage 0
// (s01), where these put the dot under the finger in all four rotations.
static TouchCal cal = {
  /*xAt0*/ 3896, /*xAtW*/ 176,
  /*yAt0*/ 193,  /*yAtH*/ 3782,
  /*swapXY*/ false,
  /*zThreshold*/ 400,
  /*valid*/ false
};

// ---------------------------------------------------------------------------
// Bit-banged XPT2046 (proven in stage0/s01)
// ---------------------------------------------------------------------------
static inline void clockDelay() { delayMicroseconds(5); }

static uint16_t xpt(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(CYD_TOUCH_MOSI_PIN, (cmd >> i) & 1);
    digitalWrite(CYD_TOUCH_CLK_PIN, HIGH); clockDelay();
    digitalWrite(CYD_TOUCH_CLK_PIN, LOW);  clockDelay();
  }
  digitalWrite(CYD_TOUCH_MOSI_PIN, LOW);
  // Null bit, then D11..D0, each shifted out on a falling edge.
  uint16_t v = 0;
  for (int i = 11; i >= 0; i--) {
    digitalWrite(CYD_TOUCH_CLK_PIN, HIGH); clockDelay();
    digitalWrite(CYD_TOUCH_CLK_PIN, LOW);  clockDelay();
    v |= (uint16_t)digitalRead(CYD_TOUCH_MISO_PIN) << i;
  }
  return v;
}

bool touch_readRaw(int& rx, int& ry, int& z) {
  digitalWrite(CYD_TOUCH_CS_PIN, LOW);
  const int z1 = xpt(0xB1), z2 = xpt(0xC1);
  z = max(0, z1 + 4095 - z2);          // same convention as XPT2046_Touchscreen
  // First read after switching channel is the noisy one: discard, average two.
  xpt(0xD1);
  const int x1 = xpt(0xD1), x2 = xpt(0xD1);
  xpt(0x91);
  const int y1 = xpt(0x91), y2 = xpt(0x90);   // PD=00: power down, PENIRQ armed
  digitalWrite(CYD_TOUCH_CS_PIN, HIGH);
  rx = (x1 + x2) / 2;
  ry = (y1 + y2) / 2;
  return z >= cal.zThreshold;
}

int touch_measureNoiseFloor() {
  int worst = 0, rx, ry, z;
  for (int i = 0; i < 60; i++) {
    touch_readRaw(rx, ry, z);
    worst = max(worst, z);
    delay(5);
  }
  return worst;
}

void touch_nativePoint(long rx, long ry, int& px, int& py) {
  const long a = cal.swapXY ? ry : rx;
  const long b = cal.swapXY ? rx : ry;
  px = constrain((int)map(a, cal.xAt0, cal.xAtW, 0, CYD_PANEL_W - 1), 0, CYD_PANEL_W - 1);
  py = constrain((int)map(b, cal.yAt0, cal.yAtH, 0, CYD_PANEL_H - 1), 0, CYD_PANEL_H - 1);
}

void touch_screenPoint(long rx, long ry, int& sx, int& sy) {
  int px, py;
  touch_nativePoint(rx, ry, px, py);
  // Rotation convention verified on the panel in stage0/s01.
  switch (tft.getRotation()) {
    case 0:  sx = px;                   sy = py;                   break;
    case 1:  sx = py;                   sy = CYD_PANEL_W - 1 - px; break;
    case 2:  sx = CYD_PANEL_W - 1 - px; sy = CYD_PANEL_H - 1 - py; break;
    default: sx = CYD_PANEL_H - 1 - py; sy = px;                   break;
  }
}

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------
// The classifier is touch_gesture.h, shared with the host test.
static cydtouch::PressTracker tracker;

void touch_begin() {
  pinMode(CYD_TOUCH_CS_PIN, OUTPUT);
  pinMode(CYD_TOUCH_CLK_PIN, OUTPUT);
  pinMode(CYD_TOUCH_MOSI_PIN, OUTPUT);
  pinMode(CYD_TOUCH_MISO_PIN, INPUT);
  pinMode(CYD_TOUCH_IRQ_PIN, INPUT);
  digitalWrite(CYD_TOUCH_CS_PIN, HIGH);
  digitalWrite(CYD_TOUCH_CLK_PIN, LOW);

  if (touch_hasStoredCalibration())
    Serial.printf("touch: calibration x[%ld..%ld] y[%ld..%ld] swap=%d z>=%d\n",
                  cal.xAt0, cal.xAtW, cal.yAt0, cal.yAtH, cal.swapXY, cal.zThreshold);
  else
    Serial.println("touch: no stored calibration -- using stage 0 defaults");
}

bool     touch_isDown() { return tracker.down(); }
uint32_t touch_heldMs() { return tracker.heldMs(millis()); }
uint32_t touch_lastSeenMs() { return tracker.lastSeenMs(); }
void     touch_position(int& x, int& y) { tracker.landing(x, y); }
void     touch_livePosition(int& x, int& y) { tracker.live(x, y); }
bool     touch_rawPosition(int& x, int& y) { tracker.raw(x, y); return tracker.down(); }

bool touch_confirmedLift(uint32_t&) { return false; }

TouchEvent touch_poll(int* x, int* y, TouchListMode) {
  const uint32_t now = millis();
  int rx, ry, z, sx = 0, sy = 0;
  const bool valid = touch_readRaw(rx, ry, z);
  if (valid) touch_screenPoint(rx, ry, sx, sy);
  return tracker.update(now, valid, sx, sy, x, y);
}

// ---------------------------------------------------------------------------
// Calibration storage
// ---------------------------------------------------------------------------
TouchCal touch_calibration() { return cal; }

void touch_setCalibration(const TouchCal& c, bool persist) {
  cal = c;
  cal.valid = true;
  if (!persist) return;
  prefs.begin(NVS_NS, false);
  prefs.putLong("xAt0", cal.xAt0);
  prefs.putLong("xAtW", cal.xAtW);
  prefs.putLong("yAt0", cal.yAt0);
  prefs.putLong("yAtH", cal.yAtH);
  prefs.putBool("swap", cal.swapXY);
  prefs.putInt ("z",    cal.zThreshold);
  prefs.putBool("ok",   true);            // last: a half-written record is not valid
  prefs.end();
  Serial.println("touch: calibration saved");
}

bool touch_hasStoredCalibration() {
  prefs.begin(NVS_NS, false);
  const bool ok = prefs.getBool("ok", false);
  if (ok) {
    cal.xAt0       = prefs.getLong("xAt0", cal.xAt0);
    cal.xAtW       = prefs.getLong("xAtW", cal.xAtW);
    cal.yAt0       = prefs.getLong("yAt0", cal.yAt0);
    cal.yAtH       = prefs.getLong("yAtH", cal.yAtH);
    cal.swapXY     = prefs.getBool("swap", cal.swapXY);
    cal.zThreshold = prefs.getInt ("z",    cal.zThreshold);
    cal.valid      = true;
  }
  prefs.end();
  return ok;
}

// The resistive panel has no wake-capable INT wiring here: it never sleeps.
void touch_setIdle(bool, bool) {}
TouchIdleStats touch_idleStats() { return {0, 0, 0, false, 0, 0, true}; }
bool touch_sleepPrepare() { return false; }
void touch_sleepFinish(bool) {}
bool touch_sleepProbe(uint32_t) { return false; }

#endif // !CYD_TOUCH_CAPACITIVE
