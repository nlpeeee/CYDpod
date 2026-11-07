# Power and sleep plan (JC2432W328C)

Written 2026-11-07, revision 2. Replaces PLAN-POWER (2026-11-06).
Revision 2 applies the owner's answers:
- the CPU stays at 240 MHz (160 MHz is not enough);
- Sleep starts after 5 minutes without playback;
- no restart on wake;
- BOOT can't be reached, so touch is the only way to wake.

Labels: S = sourced (code or document), M = measured, I = inferred.
Code base: `main` 3a2287e (release 0.5.0, firmware 1cc1b4d), plus `cydpod/bt-a`, which is still pending (ad3f72b).
Abbreviations:
- A = `firmware/cydpod_player/app`.
- TRM = `docs/esp32_technical_reference_manual_en.pdf` v5.9.
- Lib = `.deps/sketchbook/libraries/ESP32-A2DP/src`, version 1.8.12.
- SDK = `.deps/arduino/packages/esp32/tools/esp32-libs/3.3.10`.

## Implementation status (2026-11-07)

P0, P1, P2, P3a and P3b are implemented on `cydpod/power` (firmware commit 12ffb37), as runtime levers. Every owner gate (the touch INT gate, the 20-cycle Bluetooth heap gate, the S0-S6 readings) is still pending on hardware. See `docs/research/power-candidate.md`.

Deviation from the original chunk plan: the gates were turned into runtime self-checks so one candidate can be measured.
- Sleep requires either the 1 s backstop, or a screen-off INT wake already seen this boot.
- `pwr btcycle 20` runs the Bluetooth heap gate on the device.

## Goal

The board has a lithium battery charge and discharge circuit (S, `releases/0.5.0/hardware/JC2432W328-Specifications-EN.pdf`), so the target is battery runtime.

Today the owner reads 0.5-0.6 W, about 100-120 mA at 5 V, with the screen off, Bluetooth disconnected and nothing playing (M, owner). The vendor's figure is "about 115 mA" (S, spec PDF).

1. **Idle**: the screen is off and the player is still usable at once. Cut current without losing the Bluetooth link.
2. **Sleep**: after 5 minutes without playback, the chip enters light sleep with Bluetooth off. A touch wakes it **in place**: no restart, the same screen, the same track and position.

## Findings

| # | Power sink | Today | Lever | Est. saving (I) |
|---|---|---|---|---|
| F1 | CPU clock | 240 MHz fixed (S, boards.txt `f_cpu`; Arduino `main.cpp:97`) | None at runtime with Bluetooth up (C1). In Sleep the CPU is stalled anyway. | — |
| F2 | ST7789 panel | Backlight PWM set to 0 only; no SLPIN (S, A/sleep_mode.cpp `light()`, A/display.cpp) | DISPOFF + SLPIN when the screen is off | 3-8 mA |
| F3 | CST820 touch | Auto-sleep disabled (`0xFE=0xFF`); polled over I²C on every 2 ms loop pass (S, A/touch_cst820.cpp, A/app.ino `delay(2)`) | When the screen is off, enable auto-sleep and wake on the INT pin | 1-3 mA |
| F4 | A2DP stream while paused | The library starts media on connect and sends SUSPEND only on its way to a disconnect (S, Lib/BluetoothA2DPSource.cpp:926-983). The player fills the gap with silence (S, A/audio.cpp). The radio streams at full rate while paused. | Suspend media after a short pause; restart it on play | 10-30 mA in S3 |
| F5 | Bluetooth controller | Always enabled. With the main crystal as its low-power clock, it holds `ESP_PM_NO_LIGHT_SLEEP` from enable to disable (S, IDF `bt/controller/esp32/bt.c`). The board has no free pins for a 32 kHz crystal (S, A/board.h: GPIO32/33 carry touch I²C). | Disable it in Sleep (C3) | Bluetooth's whole budget, in Sleep |
| F6 | Page scan and paging | Connectable at all times; paging backs off, then gives up (S, A/bt_reconnect_policy.h) | None for Idle; Sleep turns Bluetooth off | ~1 mA steady |
| F7 | Main loop | `delay(2)`. FreeRTOS ticks at 1 kHz with no tickless idle. The decoder waits in 10 ms slices while paused (S, A/audio.cpp) | `delay(20)` while the screen is off | ≤2 mA |
| F8 | Board floor | USB-serial bridge, linear regulator, SD card, speaker amplifier. The capacitive board has no amplifier-enable GPIO (S, A/board.h:271-272), plus the charger. | None (SD: CS high) | Floor; to be measured |

### Constraints

**C1. The CPU stays at 240 MHz while Bluetooth is up.**
- 240 MHz runs from the 480 MHz PLL, while 80 and 160 MHz run from the 320 MHz PLL (S, TRM Table 7.2-2).
- Changing the PLL requires Bluetooth to be in its low-power mode first (S, TRM §7.2.5).
- The Arduino switch falls back to reconfiguring the PLL in this case (S, SDK `soc/rtc.h:333-335`).
- IDF refuses a 240↔80/160 switch in its own frequency scaling (S, IDF v5.5 `esp_pm/pm_impl.c`: "We can't switch between 240 and 80/160 without disabling PLL").
- With 160 MHz ruled out by the owner, there is no runtime CPU lever. See Deferred D-SDK for the only remaining route.

**C2. Light sleep is the only usable sleep mode.**
- Light sleep keeps RAM and peripheral state: the digital core's clock is gated and the CPUs are stalled. The chip draws 0.8 mA and wakes in under 1 ms (S, TRM §9.3.9).
- Any GPIO can wake it (S, TRM Table 9.3-2).
- Deep sleep needs an RTC GPIO to wake (S, TRM Table 9.3-2, note 4). The touch INT pin, GPIO21, is not one, and BOOT can't be reached (owner). So deep sleep is ruled out.
- Light sleep also makes resuming in place possible: the display, SD card, decoder and UI state all survive.

**C3. Bluetooth can be stopped and restarted without a reboot.**
- `a2dp.end(false)` deinitialises AVRCP, A2DP and the library's app task, and leaves Bluedroid and the controller alone. The library's own comment says this is meant "to allow reinitialization on next start()" (S, Lib/BluetoothA2DPSource.cpp `end`; Lib/BluetoothA2DPCommon.cpp:134-237).
- After that, the player calls `esp_bluedroid_disable()` and `esp_bt_controller_disable()`. Neither frees memory; the controller goes back to INITED (S, IDF bt.c).
- `a2dp.start()` handles exactly these states:
  - `btStartMode()` re-enables a controller in the INITED state (S, Arduino `esp32-hal-bt.c:88-147`);
  - `start()` re-enables Bluedroid if it is INITIALIZED (S, Lib/BluetoothA2DPSource.cpp:149-166);
  - then it re-creates the app task and re-runs the stack-up profile initialisation (S, :182-186, :545-570).
- Never call `end(true)`: it releases the controller's memory, and restarting becomes impossible (S, Lib/BluetoothA2DPCommon.cpp:220).
- **Risk (I): heap fragmentation from the per-cycle free and re-allocation.** It is gated by measurement in P3a.

**C4. Touch wake.**
- The CST816S family has `IrqPluseWidth` (`0xED`): the INT low pulse, in 0.1 ms units, range 1-200, default 10 (1 ms). `AutoSleepTime` (`0xF9`) is in seconds, default 2. `DisAutoSleep` is `0xFE` (S, [CST816S register declaration](https://files.waveshare.com/wiki/common/CST816S_register_declaration.pdf)).
- No CST820 register datasheet was found. ESPHome treats the CST820 (ID 0xB7) as using the same IRQ registers (S, [ESPHome cst816 driver](https://api-docs.esphome.io/cst816__touchscreen_8cpp_source)). Whether they behave the same on the CST820 is unverified (I).
- Light-sleep GPIO wake is level-triggered. A 20 ms low pulse leaves plenty of margin (I).

**C5. No automatic power management in the SDK.** The prebuilt SDK has no `CONFIG_PM_ENABLE` (S, sdkconfig.h). `esp_light_sleep_start()` called directly doesn't need it (I, IDF API).
- `millis()` comes from esp_timer, which IDF corrects after light sleep (I).
- The FreeRTOS tick count is not advanced without tickless idle (I), so any timeout based on `xTaskGetTickCount` sees the sleep as zero time.

**C6. ST7789 sleep timing.** In sleep-in, the MCU interface and frame memory keep working (I, ST7789V datasheet; confirm in P1).
- Wait 5 ms after SLPIN before the next command.
- Wait 120 ms after SLPOUT before sending SLPIN.
- Wait 120 ms after SLPIN before sending SLPOUT.

The ILI9341 that can be selected at runtime (S, A/display.cpp) uses the same opcodes.

## Power states

| State | Entry | What's on | Wake |
|---|---|---|---|
| Active | Screen on | As today (240 MHz) | — |
| Idle | Screen off (existing timeout) | Panel in sleep-in; touch on INT with auto-sleep; loop at 20 ms; A2DP suspended 10 s after pausing; Bluetooth connectable; CPU at 240 | Touch: about 120 ms to visible |
| Sleep | **Not playing for 5 minutes and no touch for 5 minutes (D1)**, with none of the Sleep exclusions active | Light sleep; Bluetooth disabled (memory kept); audio output parked; panel in sleep-in; backlight pin held low; touch on INT | Touch only: about 120 ms to visible; Bluetooth back in about 1-2 s (I, measure), then one page to the saved speaker |

**Sleep exclusions**: pairing; USB upload mode; firmware update; catalogue scan or write; any pending open/seek in `audio_commands()`.

A connected speaker that has been paused for 5 minutes is disconnected by Sleep. While asleep, a speaker can't connect and AVRCP play is unavailable. Both follow from D1.

## Measurement protocol (owner only; agents never touch the device)

- Use the same USB meter every time. Wait 30 s for the reading to settle, then take a 60 s average.
- After each chunk, paste the `stat:` heap lines (free/min/largest) and the `stat: pwr` line.

| State | Description |
|---|---|
| S0 | 0.5.0 wired firmware on the wired route, screen off, paused. Bluetooth never starts (S, A/app.ino:148), so S2 − S0 gives Bluetooth's share. |
| S1 | Screen on, Bluetooth disconnected, paused |
| S2 | Screen off, Bluetooth disconnected, paused |
| S3 | Screen off, AirPods Pro 2 connected, paused for more than 30 s |
| S4 | Screen off, connected, playing AAC-LC 44.1 kHz stereo |
| S5 | Screen off, wired I2S output, playing |
| S6 | Sleep |

Every lever has a runtime toggle (P0), so one flash covers all the A/B readings.

## Chunks

Rules for every chunk:
- About 20 minutes and at most 300 lines.
- Edit only the listed files, plus `docs/research/power-<chunk>.md`. Don't touch `vendor/`, `.deps/` or `releases/`.
- Run the check once, on the final commit: `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`. Keep the PASS/FAIL line and the log tail. A compile is not hardware evidence.

### W0: baseline (owner, no code)

Read S0-S5 on the 0.5.0 Bluetooth and wired firmware (`releases/0.5.0/firmware/`).

### P0 `cydpod/power-diag`: power module and toggles

Files: new `A/power.cpp` and `A/power.h`; `A/app.ino` (route lines starting `pwr` to `power_command()` before `classic_command`; call `power_tick()`); `docs/research/power-diag.md`.
- Levers: `panel`, `touch`, `loop`, `a2dp`, `sleep`, each on by default once its chunk lands. Toggles live in RAM only.
- Commands: `pwr`, `pwr on|off <lever>`.
- Every 10 s, print `stat: pwr state=<Active|Idle> levers=<…> sleepIn=<s>`.

### P1 `cydpod/power-panel-touch`: panel, touch and loop (after P0)

Files: `A/display.cpp`, `A/display.h`, `A/sleep_mode.cpp`, `A/touch_cst820.cpp`, `A/touch_cst820.h`, `A/app.ino`, `A/power.cpp`.

**Panel**
- Add `display_panelSleep(bool)`.
  - Off: backlight 0, then 0x28, then 0x10, then wait 5 ms.
  - On: 0x11, wait 120 ms, then 0x29, then the backlight.
- Enforce the C6 spacing with a timestamp.
- Hook it only at `light(0)` and at the first non-zero `light()` after it. The dim stage keeps the panel awake.
- Confirm against the datasheet that RAM writes work in sleep-in. If they don't, suppress redraws while asleep.
- The touch swallow lasts until the panel is visible.

**Touch**
- At init, write `0xED=200` (a 20 ms INT pulse). Read it back and log the value.
- While the screen is off:
  - Write `0xFE=0x00` to enable auto-sleep.
  - Stop I²C polling.
  - `attachInterrupt(GPIO21, FALLING)` sets a flag.
- On the flag: write `0xFE=0xFF` (the existing reset/recovery path is the fallback), resume polling, and swallow the wake press.
- I²C NACKs while the controller is asleep are expected; don't count them as errors.
- Log `touch: int edges=<n> pulse=<0xED readback>`.

**Touch gate (blocks P3)**
- Owner: after 60 s with the screen off, a tap must produce an INT edge.
- If auto-sleep kills INT: keep auto-sleep disabled while the screen is off and use INT edges from the controller while it is active.
- If neither mode produces INT edges, Sleep is impossible. Stop and report.

**Loop.** `delay(20)` while the screen is off. Record `pollGapMax` with the screen on to show it's unchanged.

**Acceptance**
- The check passes.
- S1-S3 readings with each lever on and off.
- The first touch wakes the screen and is swallowed.
- No underruns in `stat: audio`.

### P2 `cydpod/power-a2dp`: suspend the A2DP stream while paused (after bt-a lands)

Files: `A/bt.cpp`, `A/bt.h`, new `A/bt_media_idle.h` (pure logic) and `tests/bt_media_idle_test.cpp`, `A/power.cpp`, `docs/research/power-a2dp.md`.
- `s_media_state` is protected in the library (S, Lib/BluetoothA2DPSource.h:311).
- After the player has been paused for 10 s with media STARTED, send `esp_a2d_media_ctrl(SUSPEND)`. On the ACK, set the state to IDLE. Don't enter the library's STOPPING path, which disconnects.
- On play, send `CHECK_SRC_RDY`. The library's IDLE→STARTING→STARTED path sends START.
- Hold the decoder's first pull until STARTED.
- Cover these cases: seek or track change while suspended; remote AVRCP play; pairing; the peer refusing the suspend or disconnecting (log it, don't fight it).
- Run the host test once.
- **Acceptance**
  - S3 falls towards S2.
  - Play after a 1-minute pause starts within 500 ms (`bt: media ack` log).
  - No regression in the bt-a reconnect and volume scenarios, or in FLAC over Bluetooth.

### P3a `cydpod/power-btcycle`: stop and restart Bluetooth in place (after bt-a; can run in parallel with P2's review)

Files: `A/bt.cpp`, `A/bt.h`, `A/power.cpp`, `docs/research/power-btcycle.md`.

`bt_suspendStack()`:
1. Disconnect if connected, and wait for DISCONNECTED (the existing path).
2. `a2dp.end(false)`.
3. `esp_bluedroid_disable()`.
4. `esp_bt_controller_disable()`, then wait for INITED.
5. Put bt.cpp's state machines (reconnect policy, volume sync, pairing session) into a `Suspended` state that they leave untouched.

`bt_resumeStack()`:
1. Call `a2dp.start()` with the same names and callbacks as `bt_begin()`. Factor out the shared part; don't duplicate it.
2. Restore connectable, non-discoverable.
3. Arm one page through the reconnect policy (`FIRST_PAGE_DELAY_MS`).

Logging and test command:
- Log heap free/min/largest before the suspend, after it, and after the resume.
- `pwr btcycle <n>` runs n cycles 10 s apart, without light sleep.

**Gate (blocks P3b)**
- 20 cycles with no ratchet: after cycle 20, the largest free block and free heap are within 1 KiB of cycle 1.
- Minimum free heap stays above the 10,240 B margin.
- The saved speaker reconnects after each resume, and AAC plays afterwards.

**If the gate fails**: try the lower-level variant: keep the library's profiles registered and only disable and enable Bluedroid and the controller. Record both results, then stop for an owner decision. Sleep can't ship without in-place Bluetooth.

### P3b `cydpod/power-sleep`: Sleep state (after the P1 touch gate and the P3a gate)

Files: `A/power.cpp`, `A/power.h`, `A/sleep_mode.cpp`, `A/sleep_mode.h`, `A/app.ino`, `A/display.cpp`, `A/audio.cpp`, `A/audio.h`, `docs/research/power-sleep.md`.

**Entry** (D1: not playing and no touch for 300 s, and no exclusion active):
1. `settings_saveResume()`. This protects against a later power loss; it isn't used on wake.
2. Make sure the wired I2S and speaker DAC outputs are parked, with no DMA running (S, A/wired.cpp `parked`).
3. `bt_suspendStack()`.
4. SD card: no open writes; CS high.
5. `display_panelSleep(true)`. Then `ledcDetach(BL)`, drive the pin low, `gpio_hold_en(BL)`.
6. Touch in its INT mode (from P1).
7. If GPIO21 is already low, abort and return to Idle.
8. `gpio_wakeup_enable(GPIO21, GPIO_INTR_LOW_LEVEL)`, `esp_sleep_enable_gpio_wakeup()`.
9. `Serial.flush()`, then `esp_light_sleep_start()`.

**Wake** (in place):
1. `gpio_hold_dis(BL)`, then re-attach LEDC.
2. `display_panelSleep(false)` (120 ms). The wake press stays swallowed.
3. Touch returns to polling.
4. `bt_resumeStack()`.
5. Reset the idle timers.
6. Log `pwr: sleep <s> s, woke cause=<gpio>`.
7. Check every timeout that uses the FreeRTOS tick count (C5), and fix any that would misbehave after a sleep.

**Debug lever**: `pwr on sleeptimer` also arms a 60 s RTC timer wake, so a failed INT wake can't strand a test unit. It's off in release.

**Acceptance**
- The check passes.
- S6 reading.
- 20 touch wake cycles, each returning to the same screen, track and position.
- The time from wake to visible, and the time from wake to the speaker reconnecting, are recorded.
- No heap ratchet over the 20 cycles.
- 5 minutes without playback enters Sleep; playing, pairing or upload mode never does.

### Deferred

- **D-SDK: frequency scaling via a custom SDK, the only remaining CPU lever.**
  - With `CONFIG_PM_ENABLE` and `{max 240, min 40}`, the CPU would drop to the 40 MHz crystal only while the Bluetooth controller releases its APB lock during ORIG modem sleep (S, IDF bt.c phase 2/3 hooks). Suspending the PLL in that state is what the TRM allows (S, TRM §7.2.5).
  - Every return to 240 MHz relocks the PLL (I: latency and audio-clock effects unknown).
  - It costs rebuilt esp32-libs and provenance work.
  - Revisit only if S2 and S3 after P1 and P2 are still a large share of the budget.
- **Bluetooth connectable hold.** Saves about 1 mA (F6), and Sleep covers it. Dropped.
- **Deep sleep.** Ruled out (C2).

## Order and ownership

1. W0 (owner).
2. P0, then P1. The P1 touch gate is owner-measured.
3. `cydpod/bt-a` lands.
4. P2 and P3a both edit bt.cpp, so run them one after the other: P3a first, because Sleep is the larger win.
5. P3b goes after the P1 and P3a gates.
6. P1, P3a and P3b share power.cpp, app.ino and sleep_mode.cpp, so serialize them.

## Owner decisions (answered 2026-11-07)

- 160 MHz: rejected. The CPU stays at 240 MHz.
- D1: Sleep after 5 minutes without playback. This plan also requires 5 minutes without a touch, so the player doesn't sleep while someone is browsing.
- D2: no restart on wake. Bluetooth restarts in place (C3, P3a).
- D3: BOOT can't be reached. Touch (GPIO21) is the only wake source (C4, P1 gate).
