#pragma once
// Idle power and the Sleep state (docs/specs/power-plan.md).
//
// Every saving is a runtime lever, on by default, so one flash serves every
// A/B current reading: `pwr`, `pwr on <lever>`, `pwr off <lever>`. Levers are
// RAM only; a reset restores the defaults.
//
//   panel     ST7789/ILI9341 DISPOFF + SLPIN while the screen is off
//   touchint  screen off: CST820 is read only after its INT edge (250 ms backstop poll)
//   touchsleep screen off: let the CST820 auto-sleep (off by default: unverified)
//   loop      screen off: 20 ms loop cadence instead of 2 ms
//   a2dp      suspend the A2DP stream 10 s after playback stops
//   sleep     light sleep with Bluetooth stopped after 5 min without playback
//   backstop  while asleep, also wake every second to look for a held touch
//   btfast    Bluetooth: 0.5 s stack-up wait (not 10 s) at boot and after Sleep, and prompt
//             media-ready retries after any connection
//   bttx      Bluetooth transmit power up to +9 dBm (controller default is lower)
#include <Arduino.h>

enum PowerLever : uint8_t {
  PWR_PANEL, PWR_TOUCHINT, PWR_TOUCHSLEEP, PWR_LOOP, PWR_A2DP, PWR_SLEEP, PWR_BACKSTOP, PWR_BTFAST, PWR_BTTX, PWR_LEVER_COUNT
};

bool     power_lever(PowerLever lever);
// True when the line was a `pwr` command (handled here).
bool     power_command(const char* line);
// After ui_tick(). bluetoothStarted: bt_begin() ran this boot.
void     power_tick(uint32_t now, bool bluetoothStarted);
uint32_t power_loopDelayMs();
// The Sleep timeout (D1): no playback and no touch for this long.
static const uint32_t POWER_SLEEP_AFTER_MS = 5UL * 60UL * 1000UL;
