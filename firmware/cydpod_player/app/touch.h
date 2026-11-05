// ===========================================================================
// touch.h -- XPT2046 by bit-bang: raw reads, calibration, gestures
// ===========================================================================
// Why bit-banged: the ESP32 has two free SPI hosts. The display has HSPI and
// the SD card needs VSPI, so the touch controller -- polled, happy at a few
// hundred kHz -- is clocked by hand on its own pins (docs/stage0_results.md).
//
// Calibration is stored in the panel's NATIVE portrait coordinates (240x320)
// and turned into screen coordinates for whatever rotation is active, so one
// calibration serves portrait and landscape alike. It is measured on the
// device by the wizard (calibrate.h) and kept in NVS "mp3touch".
//
// Gestures are classified on RELEASE, so one press is exactly one event, however
// briefly it was seen (touch_gesture.h).
#pragma once

#include <Arduino.h>
#include "touch_gesture.h"   // TouchEvent, TOUCH_*_MS, the press classifier

// Native portrait mapping: raw value at pixel 0 and at the far edge, per axis.
// swapXY = true when raw X drives native Y. Reversed ranges are how an axis
// flip is represented; map() handles them.
struct TouchCal {
  long xAt0, xAtW;     // native x: 0 .. 239
  long yAt0, yAtH;     // native y: 0 .. 319
  bool swapXY;
  int  zThreshold;
  bool valid;
};

void       touch_begin();
TouchEvent touch_poll(int* x = nullptr, int* y = nullptr);
bool       touch_isDown();
uint32_t   touch_heldMs();
uint32_t   touch_lastSeenMs(); // sample clock, including the press just released
// Where the current press LANDED (screen coordinates), valid while down. This
// is the point touch_poll() reports when the press ends: a tap acts where the
// finger went down, not where it drifted to.
void       touch_position(int& x, int& y);
// Where the finger is NOW, smoothed, valid while down -- for drags. Tracking it
// does not change how a press is classified or where its event lands.
void       touch_livePosition(int& x, int& y);

bool touch_readRaw(int& rx, int& ry, int& z);
int  touch_measureNoiseFloor();

// Raw -> native portrait pixel, and raw -> screen pixel for the current rotation.
void touch_nativePoint(long rx, long ry, int& px, int& py);
void touch_screenPoint(long rx, long ry, int& sx, int& sy);

TouchCal touch_calibration();
void     touch_setCalibration(const TouchCal& c, bool persist);
bool     touch_hasStoredCalibration();
