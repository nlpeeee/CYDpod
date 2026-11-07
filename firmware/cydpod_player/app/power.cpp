// Player image only (firmware.h).
#ifndef CYD_UPLOADER
#include "power.h"
#include "board.h"
#include "settings.h"
#include "audio.h"
#include "bt.h"
#include "display.h"
#include "sleep_mode.h"
#include "touch.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>

// docs/specs/power-plan.md. The CPU stays at 240 MHz: with Bluetooth up, a
// switch to 80/160 MHz reconfigures the PLL (TRM Table 7.2-2, §7.2.5), which
// IDF's own frequency scaling refuses. Savings come from the panel, touch,
// loop cadence and A2DP stream while idle, and from light sleep with the
// Bluetooth stack stopped in place (no restart: RAM, screen and track survive).
namespace {
const char* const NAMES[PWR_LEVER_COUNT] = {"panel", "touchint", "touchsleep", "loop", "a2dp", "sleep", "backstop"};
bool levers[PWR_LEVER_COUNT] = {true, true, false, true, true, true, true};

uint32_t lastPlayAt = 0, lastStat = 0, retryAt = 0, forceUntil = 0;
bool started = false;
// `pwr sleepnow` skips only the 5-minute wait, and only for the next 10 s.
bool forced(uint32_t now) { return forceUntil && int32_t(now - forceUntil) < 0; }
uint32_t sleeps = 0, spurious = 0, backstopWakes = 0, lastSleepMs = 0;
const char* lastWake = "none";

// `pwr btcycle <n>`: stop and restart the Bluetooth stack n times, without
// light sleep, logging heap after each restart has fully settled (P3a gate).
int cyclesLeft = 0, cycleNo = 0;
uint8_t cyclePhase = 0;
uint32_t cycleAt = 0, cycleWaitStart = 0, firstLargest = 0, firstFree = 0;

void heapLine(const char* what) {
  Serial.printf("pwr: %s heap %u min %u largest %u\n", what, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                ESP.getMaxAllocHeap());
}

int leverIndex(const char* name) {
  for (int i = 0; i < PWR_LEVER_COUNT; ++i) if (strcmp(name, NAMES[i]) == 0) return i;
  return -1;
}

void printStatus(uint32_t now) {
  const TouchIdleStats t = touch_idleStats();
  Serial.printf("stat: pwr state=%s levers=", sleep_screenOff() ? "Idle" : "Active");
  for (int i = 0; i < PWR_LEVER_COUNT; ++i) Serial.printf("%s%s=%d", i ? "," : "", NAMES[i], int(levers[i]));
  const uint32_t quiet = min(now - lastPlayAt, sleep_idleMs(now));
  Serial.printf(" sleepIn=%lds panel=%s bt=%s stream=%s int edges=%lu intWakes=%lu pollWakes=%lu verified=%d pulse=%u "
                "sleeps=%lu lastWake=%s lastSleepS=%lu spurious=%lu backstopWakes=%lu\n",
                long(int32_t(POWER_SLEEP_AFTER_MS - quiet) / 1000), display_panelAsleep() ? "asleep" : "awake",
                bt_stackSuspended() ? "suspended" : bt_stackSettled() ? "up" : "starting",
                bt_streamIdle() ? "suspended" : "normal", (unsigned long)t.intEdges, (unsigned long)t.intWakes,
                (unsigned long)t.pollWakes, int(t.intVerified), unsigned(t.pulseReg), (unsigned long)sleeps, lastWake,
                (unsigned long)(lastSleepMs / 1000), (unsigned long)spurious, (unsigned long)backstopWakes);
}

// Wake detection inside light sleep. Returns the wake reason.
const char* sleepUntilWake() {
  uint8_t spuriousRun = 0;
  for (bool first = true;; first = false) {
    // Between re-sleeps let this core's idle task run (task watchdog), and the
    // paused decoder and outputs finish whatever their 10 ms waits ask for.
    if (!first) vTaskDelay(1);
    Serial.flush();
    esp_light_sleep_start();
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_UART) return "serial";
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
      ++backstopWakes;
      if (touch_sleepProbe(0)) return "backstop";
      continue;
    }
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
      // INT is a pulse; the finger is still down, so a real touch reads as contact.
      if (touch_sleepProbe(300)) return "touch";
      ++spurious;
      if (++spuriousRun >= 20) return "spurious";
      continue;
    }
    return "other";
  }
}

void enterSleep(uint32_t now, bool bluetoothStarted) {
  Serial.printf("pwr: sleep entering after %lus without playback or touch\n",
                (unsigned long)(min(now - lastPlayAt, sleep_idleMs(now)) / 1000));
  heapLine("sleep before");
  if (!sleep_screenOff()) sleep_forceScreenOff();  // `pwr sleepnow` with the screen lit
  settings_saveResume();  // power-loss insurance only: wake resumes in place
  const bool btWasUp = bluetoothStarted && !bt_stackSuspended();
  if (btWasUp && !bt_suspendStack()) {
    Serial.println("pwr: sleep aborted (Bluetooth stop failed); restarting the stack");
    bt_resumeStack();
    retryAt = millis() + 60000;
    return;
  }
  if (!touch_sleepPrepare()) {
    Serial.println("pwr: sleep aborted (touch INT already low)");
    if (btWasUp) bt_resumeStack();
    retryAt = millis() + 10000;
    return;
  }
  gpio_hold_en(gpio_num_t(CYD_TFT_BL_PIN));
  esp_sleep_enable_gpio_wakeup();
  if (levers[PWR_BACKSTOP]) esp_sleep_enable_timer_wakeup(1000000ULL);
  if (uart_set_wakeup_threshold(UART_NUM_0, 3) == ESP_OK) esp_sleep_enable_uart_wakeup(UART_NUM_0);
  const uint32_t asleepAt = millis();
  lastWake = sleepUntilWake();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_hold_dis(gpio_num_t(CYD_TFT_BL_PIN));
  touch_sleepFinish(strcmp(lastWake, "touch") == 0);
  lastSleepMs = millis() - asleepAt;
  ++sleeps;
  if (strcmp(lastWake, "serial") == 0) while (Serial.available()) Serial.read();  // the wake bytes
  const bool pressed = strcmp(lastWake, "touch") == 0 || strcmp(lastWake, "backstop") == 0;
  sleep_wakeNow(millis(), pressed);
  Serial.printf("pwr: woke from sleep after %lus cause=%s\n", (unsigned long)(lastSleepMs / 1000), lastWake);
  if (btWasUp) bt_resumeStack();
  heapLine("sleep after");
  lastPlayAt = millis();
}

bool sleepAllowed(uint32_t now, bool bluetoothStarted) {
#if !CYD_TOUCH_CAPACITIVE
  (void)now; (void)bluetoothStarted;
  return false;  // only the CST820's INT can wake the board
#else
  if (!levers[PWR_SLEEP] || cyclesLeft > 0 || int32_t(now - retryAt) < 0) return false;
  if (!forced(now)) {
    if (g_settings.screenTimeout == 0 || !sleep_screenOff()) return false;  // "screen always on" never sleeps
    if (now - lastPlayAt < POWER_SLEEP_AFTER_MS || sleep_idleMs(now) < POWER_SLEEP_AFTER_MS) return false;
  }
  if (audio_isPlaying() || touch_isDown()) return false;
  const AudioCommands c = audio_commands();
  if (c.track >= 0 || c.seekMs >= 0 || c.opening || c.seeking) return false;
  if (bluetoothStarted && !bt_stackSuspended() &&
      (!bt_stackSettled() || bt_pairing() || bt_connecting() || bt_choosing())) return false;
  // Never sleep on a wake path not yet seen working: the 1 s backstop reads an
  // awake CST820, otherwise a touch must have woken the dark screen by INT.
  const bool backstopWorks = levers[PWR_BACKSTOP] && !levers[PWR_TOUCHSLEEP];
  return backstopWorks || touch_idleStats().intVerified;
#endif
}

void tickCycle(uint32_t now) {
  if (cyclesLeft <= 0 || int32_t(now - cycleAt) < 0) return;
  if (cyclePhase == 0) {
    if (!bt_stackSettled()) { cycleAt = now + 500; return; }
    ++cycleNo;
    Serial.printf("pwr: btcycle %d stop\n", cycleNo);
    if (!bt_suspendStack()) Serial.println("pwr: btcycle stop reported a failure");
    heapLine("btcycle stopped");
    cyclePhase = 1; cycleAt = millis() + 2000;
  } else if (cyclePhase == 1) {
    Serial.printf("pwr: btcycle %d start\n", cycleNo);
    if (!bt_resumeStack()) Serial.println("pwr: btcycle start reported a failure");
    cyclePhase = 2; cycleAt = millis() + 500; cycleWaitStart = millis();
  } else if (bt_stackSettled()) {
    heapLine("btcycle settled");
    if (cycleNo == 1) { firstFree = ESP.getFreeHeap(); firstLargest = ESP.getMaxAllocHeap(); }
    else Serial.printf("pwr: btcycle %d drift free %ld largest %ld (vs cycle 1)\n", cycleNo,
                       long(ESP.getFreeHeap()) - long(firstFree), long(ESP.getMaxAllocHeap()) - long(firstLargest));
    --cyclesLeft; cyclePhase = 0; cycleAt = millis() + 3000;
    if (!cyclesLeft) Serial.println("pwr: btcycle done");
  } else if (now - cycleWaitStart > 30000) {
    Serial.println("pwr: btcycle stopped: stack did not settle within 30 s");
    cyclesLeft = 0; cyclePhase = 0;
  } else cycleAt = now + 500;
}
}  // namespace

bool power_lever(PowerLever lever) { return lever < PWR_LEVER_COUNT && levers[lever]; }

uint32_t power_loopDelayMs() { return levers[PWR_LOOP] && sleep_screenOff() ? 20 : 2; }

bool power_command(const char* line) {
  if (strncmp(line, "pwr", 3) != 0 || (line[3] != 0 && line[3] != ' ')) return false;
  char verb[12] = {0}, arg[16] = {0};
  sscanf(line + 3, "%11s %15s", verb, arg);
  if (!verb[0]) { printStatus(millis()); return true; }
  if (strcmp(verb, "on") == 0 || strcmp(verb, "off") == 0) {
    const int i = leverIndex(arg);
    if (i < 0) { Serial.println("pwr: levers are panel touchint touchsleep loop a2dp sleep backstop"); return true; }
    levers[i] = strcmp(verb, "on") == 0;
    Serial.printf("pwr: %s %s\n", NAMES[i], levers[i] ? "on" : "off");
    return true;
  }
  if (strcmp(verb, "btcycle") == 0) {
    const int n = atoi(arg);
    if (!bt_stackSettled() && cyclesLeft == 0) { Serial.println("pwr: btcycle needs Bluetooth up and settled"); return true; }
    cyclesLeft = constrain(n, 0, 50); cycleNo = 0; cyclePhase = 0; cycleAt = millis();
    Serial.printf("pwr: btcycle %d cycles\n", cyclesLeft);
    return true;
  }
  if (strcmp(verb, "sleepnow") == 0) {
    forceUntil = (millis() + 10000) | 1;
    Serial.println(levers[PWR_SLEEP] ? "pwr: sleep requested (10 s window)" : "pwr: sleep lever is off");
    return true;
  }
  Serial.println("pwr: commands are pwr | pwr on|off <lever> | pwr btcycle <n> | pwr sleepnow");
  return true;
}

void power_tick(uint32_t now, bool bluetoothStarted) {
  if (!started) { started = true; lastPlayAt = lastStat = now; }
  if (audio_isPlaying()) lastPlayAt = now;
  const bool off = sleep_screenOff();
  touch_setIdle(off && levers[PWR_TOUCHINT], off && levers[PWR_TOUCHSLEEP]);
  if (!levers[PWR_PANEL] && display_panelAsleep()) display_panelSleep(false);
  tickCycle(now);
  if (sleepAllowed(now, bluetoothStarted)) {
    forceUntil = 0;
    enterSleep(now, bluetoothStarted);
  } else if (forceUntil && !forced(now)) {
    forceUntil = 0;
    Serial.println("pwr: sleep request expired (playing, busy or Bluetooth not settled)");
  }
  if (now - lastStat >= 10000 && !touch_isDown()) { lastStat = now; printStatus(now); }
}
#endif  // !CYD_UPLOADER
