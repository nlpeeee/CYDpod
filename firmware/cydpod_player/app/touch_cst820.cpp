#include "board.h"
#if CYD_TOUCH_CAPACITIVE
#include "touch.h"
#include "display.h"
#include "cst820_report.h"
#include <Wire.h>
#include <driver/gpio.h>

// Register/reset authority: factory CST820.cpp/.h at maxpill/JC2432W328
// commit 9712396 (full links in hq CY-67 report). Unlike that demo's retry loop,
// every transaction is bounded, with no retries within a poll.
namespace {
cydtouch::PressTracker tracker;
cst820::ListLift listLift;
bool lastLiftConfirmed = false;
uint32_t lastLiftMs = 0;
enum class Recovery : uint8_t { Ready, ResetLow, Settling };
Recovery recovery = Recovery::Ready;
uint32_t resetAt = 0, lastReset = 0, errors = 0, lastErrorLog = 0;
uint32_t maxReadUs = 0, lastStat = 0;
uint8_t failures = 0;
bool busReady = false;

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(uint8_t(CYD_TOUCH_I2C_ADDRESS));
  Wire.write(reg); Wire.write(value);
  return Wire.endTransmission(true) == 0;
}
bool readReg(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(uint8_t(CYD_TOUCH_I2C_ADDRESS));
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(uint8_t(CYD_TOUCH_I2C_ADDRESS), size_t(1), true) != 1) return false;
  value = uint8_t(Wire.read());
  return true;
}
bool wake() { return writeReg(0xfe, 0xff); } // disable auto-sleep

// Screen-off reading (power.h). INT pulses low when the controller has a report;
// CST816-family IrqPluseWidth (0xED, 0.1 ms units, 1..200) is widened to 20 ms so
// a light-sleep level wake cannot miss it. Unverified on the CST820: the read-back
// is reported in `stat: pwr pulse=`.
volatile uint32_t intEdges = 0;
volatile bool intFlag = false;
void IRAM_ATTR onInt() { ++intEdges; intFlag = true; }
bool idle = false, idleAuto = false, windowByInt = false, lastWakeByInt = false;
uint32_t windowUntil = 0, lastBackstop = 0, intWakes = 0, pollWakes = 0;
uint8_t pulseReg = 0;
void attachInt() { attachInterrupt(digitalPinToInterrupt(CYD_TOUCH_INT_PIN), onInt, FALLING); }

void failed(uint32_t now) {
  ++errors;
  if (failures < 255) ++failures;
  if (errors == 1 || now - lastErrorLog >= 1000) {
    Serial.printf("touch: CST820 I2C error count=%lu consecutive=%u\n", (unsigned long)errors, failures);
    lastErrorLog = now;
  }
  // Reset only the controller. LOW10/HIGH300ms uses deadlines, never loop delays.
  if (failures >= 20 && now - lastReset >= 5000) {
    digitalWrite(CYD_TOUCH_RST_PIN, LOW);
    resetAt = lastReset = now;
    recovery = Recovery::ResetLow;
    failures = 0;
  }
}
bool ready(uint32_t now) {
  if (!busReady) return false;
  if (recovery == Recovery::ResetLow) {
    if (now - resetAt < 10) return false;
    digitalWrite(CYD_TOUCH_RST_PIN, HIGH);
    resetAt = now;
    recovery = Recovery::Settling;
    return false;
  }
  if (recovery == Recovery::Settling) {
    if (now - resetAt < 300) return false;
    recovery = Recovery::Ready;
    if (!wake()) { failed(now); return false; }
    Serial.println("touch: CST820 reset + wake complete");
  }
  return true;
}
} // namespace

void touch_begin() {
  pinMode(CYD_TOUCH_INT_PIN, INPUT); // controller output; do not drive it
  pinMode(CYD_TOUCH_RST_PIN, OUTPUT);
  digitalWrite(CYD_TOUCH_RST_PIN, LOW); delay(10);
  digitalWrite(CYD_TOUCH_RST_PIN, HIGH); delay(300);
  const bool buffersReady = Wire.setBufferSize(32) == 32; // pinned Wire minimum: 32-byte RX + TX
  Wire.setTimeOut(2);     // pinned ESP32 Wire combined write/read transaction timeout
  busReady = buffersReady && Wire.begin(CYD_TOUCH_SDA_PIN, CYD_TOUCH_SCL_PIN, 400000);
  const bool awake = busReady && wake();
  lastReset = millis();
  if (!awake) failed(lastReset);
  if (awake && writeReg(0xed, 200) && readReg(0xed, pulseReg))
    Serial.printf("touch: CST820 INT pulse register 0xED=%u\n", unsigned(pulseReg));
  attachInt();
  Serial.printf("touch: CST820 addr=0x15 SDA%d SCL%d RST%d INT%d 400kHz ready=%d calibration=none\n",
                CYD_TOUCH_SDA_PIN, CYD_TOUCH_SCL_PIN, CYD_TOUCH_RST_PIN, CYD_TOUCH_INT_PIN, awake);
}

// quiet: a dozing controller NACKs; that is expected, never a bus error.
static cst820::Report readReport(bool quiet = false) {
  const uint32_t now = millis();
  if (!ready(now)) return {};
  uint8_t bytes[6];
  const uint32_t started = micros();
  Wire.beginTransmission(uint8_t(CYD_TOUCH_I2C_ADDRESS));
  Wire.write(uint8_t(0x01));
  const uint8_t status = Wire.endTransmission(false);
  const size_t count = status == 0 ? Wire.requestFrom(uint8_t(CYD_TOUCH_I2C_ADDRESS), sizeof(bytes), true) : 0;
  const uint32_t elapsed = micros() - started;
  if (elapsed > maxReadUs) maxReadUs = elapsed;
  if (status != 0 || count != sizeof(bytes)) { if (!quiet) failed(now); return {}; }
  for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = uint8_t(Wire.read());
  failures = 0;
  return cst820::decode(bytes, sizeof(bytes));
}

bool touch_readRaw(int& rx, int& ry, int& z) {
  rx = ry = z = 0;
  const cst820::Report report = readReport();
  if (report.state != cst820::State::Contact) return false;
  rx = report.x; ry = report.y; z = 1;
  return true;
}

bool touch_isDown() { return tracker.down(); }
uint32_t touch_heldMs() { return tracker.heldMs(millis()); }
uint32_t touch_lastSeenMs() { return tracker.lastSeenMs(); }
void touch_position(int& x, int& y) { tracker.landing(x, y); }
void touch_livePosition(int& x, int& y) { tracker.live(x, y); }
bool touch_rawPosition(int& x, int& y) { tracker.raw(x, y); return tracker.down(); }
void touch_nativePoint(long rx, long ry, int& px, int& py) { px = int(rx); py = int(ry); }
void touch_screenPoint(long rx, long ry, int& sx, int& sy) {
  cst820::screenPoint(int(rx), int(ry), tft.getRotation(), sx, sy);
}
bool touch_confirmedLift(uint32_t& firstZeroMs) {
  firstZeroMs = lastLiftMs;
  return lastLiftConfirmed;
}

// Screen off: read only inside a 500 ms window after an INT edge, or at the
// 250 ms backstop. Once a press is down every pass reads, as when lit.
static cst820::Report idleReport(uint32_t now) {
  if (intFlag) { intFlag = false; windowUntil = now + 500; windowByInt = true; }
  const bool inWindow = int32_t(now - windowUntil) < 0;
  if (!tracker.down() && !inWindow) {
    if (now - lastBackstop < 250) return {};
    lastBackstop = now; windowByInt = false;
  }
  const cst820::Report report = readReport(true);
  if (report.state == cst820::State::Contact && !tracker.down()) {
    lastWakeByInt = inWindow && windowByInt;
    if (lastWakeByInt) ++intWakes; else ++pollWakes;
    Serial.printf("touch: screen-off press seen by %s (edges %lu)\n", lastWakeByInt ? "INT" : "backstop poll",
                  (unsigned long)intEdges);
  }
  return report;
}

void touch_setIdle(bool on, bool autoSleep) {
  autoSleep = on && autoSleep;
  if (on == idle && autoSleep == idleAuto) return;
  if (autoSleep && !idleAuto) writeReg(0xfe, 0x00);  // let the controller doze
  if (!autoSleep && idleAuto) {
    bool ok = false;
    for (int i = 0; i < 3 && !ok; ++i) ok = wake();
    if (!ok) Serial.println("touch: CST820 did not answer the auto-sleep exit");
  }
  if (on && !idle) { intFlag = false; windowUntil = 0; lastBackstop = millis(); }
  idle = on; idleAuto = autoSleep;
}

TouchIdleStats touch_idleStats() {
  return {intEdges, intWakes, pollWakes, intWakes > 0 && lastWakeByInt, pulseReg};
}

bool touch_sleepPrepare() {
  if (!busReady || digitalRead(CYD_TOUCH_INT_PIN) == LOW) return false;
  detachInterrupt(digitalPinToInterrupt(CYD_TOUCH_INT_PIN));
  gpio_wakeup_enable(gpio_num_t(CYD_TOUCH_INT_PIN), GPIO_INTR_LOW_LEVEL);
  return true;
}

void touch_sleepFinish(bool byInt) {
  gpio_wakeup_disable(gpio_num_t(CYD_TOUCH_INT_PIN));
  attachInt();
  intFlag = false;
  windowUntil = millis() + 500;  // the waking finger is usually still down
  windowByInt = byInt;           // only an INT wake counts as INT evidence
}

bool touch_sleepProbe(uint32_t windowMs) {
  const uint32_t start = millis();
  do {
    if (readReport(true).state == cst820::State::Contact) return true;
    if (windowMs) delay(5);
  } while (millis() - start < windowMs);
  return false;
}

TouchEvent touch_poll(int* x, int* y, TouchListMode mode) {
  const uint32_t polledAt = millis();
  const cst820::Report report = idle ? idleReport(polledAt) : readReport();
  const uint32_t now = millis();
  int sx = 0, sy = 0;
  const bool down = report.state == cst820::State::Contact;
  if (down) touch_screenPoint(report.x, report.y, sx, sy);
  const auto liftState = mode == TouchListMode::Drag ? report.state : cst820::State::Invalid;
  const bool confirmed = listLift.update(liftState, now);
  const TouchEvent ev = tracker.update(now, down, sx, sy, x, y, confirmed, mode == TouchListMode::Coast);
  if (ev != TOUCH_NONE) {
    lastLiftConfirmed = confirmed;
    lastLiftMs = listLift.firstMs();
    if (mode == TouchListMode::Normal) {
      int lx, ly; tracker.landing(lx, ly);
      Serial.printf("touch: CST820 release landing=%d,%d event=%u\n", lx, ly, (unsigned)ev);
    }
  }
  if (mode == TouchListMode::Normal && !tracker.down() && now - lastStat >= 10000) {
    Serial.printf("stat: CST820 readMaxUs=%lu errors=%lu\n", (unsigned long)maxReadUs, (unsigned long)errors);
    maxReadUs = 0; lastStat = now;
  }
  // A 4s hold is a normal long press here, never a calibration wizard.
  return ev == TOUCH_RECALIBRATE ? TOUCH_LONG_PRESS : ev;
}
int touch_measureNoiseFloor() { return 0; }
TouchCal touch_calibration() { return {0,239,0,319,false,1,true}; }
void touch_setCalibration(const TouchCal&, bool) {} // never load resistive NVS
bool touch_hasStoredCalibration() { return true; }
#endif // CYD_TOUCH_CAPACITIVE
