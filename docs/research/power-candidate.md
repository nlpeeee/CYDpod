# Power candidate: idle levers and in-place Sleep

Date: 2026-11-07. Branch `cydpod/power`, firmware commit 54a7f1c, built from `main` 3abebbe (v0.5.1).
Specification: `docs/specs/power-plan.md`, revision 2.

## What changed

Each item is a runtime lever (`pwr on|off <lever>`; levers live in RAM, a reset restores the defaults).

| Lever | Default | Behaviour |
|---|---|---|
| `panel` | on | `light(0)` sends DISPOFF + SLPIN after the backlight goes dark. The first lit level sends SLPOUT, waits 120 ms, sends DISPON, then lights the backlight. SLPIN/SLPOUT are spaced at least 120 ms apart. |
| `touchint` | on | Screen off: the CST820 is read only for 500 ms after an INT falling edge, or by a 250 ms backstop poll. Once a press is down, every pass reads. |
| `touchsleep` | **off** | Screen off: `0xFE=0x00` lets the CST820 auto-sleep (unverified on the CST820). `0xFE=0xFF` is written again when the screen lights. |
| `loop` | on | `delay(20)` instead of `delay(2)` while the screen is off. |
| `a2dp` | on | Connected, the stream started by us, and nothing for Bluetooth to carry for 10 s (paused, or the route is not Bluetooth): AVDTP SUSPEND. The library's media state stays STARTED, so its 10 s heartbeat does not restart it. Play on Bluetooth: media goes to STARTING, then START. If START fails, the media state is left IDLE and the library's heartbeat retries. |
| `sleep` | on | Sleep conditions: 5 min without playback **and** without touch; screen dark; screen timeout not "Off"; no pending open/seek; Bluetooth settled and not pairing, connecting or choosing a device. On entry: save the resume point, stop the Bluetooth stack in place, hold the backlight pin low, arm GPIO21 LOW + UART0 (+ the 1 s timer), then `esp_light_sleep_start()`. On wake, resume in place (no restart). |
| (sleep timer) | — | When the sleep timer expires it fades and pauses playback, then requests Sleep at once (10 s window, same checks as `pwr sleepnow`) instead of waiting another 5 minutes. |
| `backstop` | on | While asleep, wake and read the CST820 once: every 1 s once INT has woken the dark screen, every 50 ms until then, so a quick tap is still caught. |

Other changes:
- `touch_begin` writes `0xED=200` (CST816-family IrqPluseWidth, 20 ms) and logs the read-back.
- The ISR on GPIO21 counts INT edges. GPIO21 uses the internal pull-up.
- `touch_begin` writes `0xFA=0x60` (CST816-family IrqCtl: EnTouch | EnChange) and logs the read-back and the INT level.
- Until INT has produced an edge this boot, the dark screen reads the CST820 on every loop pass (20 ms) instead of every 250 ms.

### Bluetooth stop and restart in place

`bt_suspendStack()`:
1. Marks the coming disconnect as ours and turns off library auto-reconnect, so `clean_last_connection()` does not zero the saved peer in NVS.
2. Calls `a2dp.end(false)`, which deinitialises AVRCP, A2DP, the managed encoder, the heartbeat timer and the BtAppT task.
3. Calls `esp_bluedroid_disable()` and `esp_bt_controller_disable()`. Nothing is deinitialised or freed.

`bt_resumeStack()`:
1. Calls `set_auto_reconnect(true,1)`.
2. Calls `policy.boot()`.
3. Calls `a2dp.start()`. That re-enables the controller (INITED → ENABLED, Arduino `btStartMode`) and Bluedroid (INITIALIZED → enabled), recreates BtAppT and re-runs the library's stack-up. Stack-up waits **10 s** and then pages the saved speaker once, exactly as at boot.

`bt_stackSettled()` waits for the library's heartbeat timer to exist. That timer is created last in stack-up, so Sleep and `pwr btcycle` never stop a stack that is still starting.

### Wake handling

| Cause | Handling |
|---|---|
| GPIO21 low | A touch only if a CST820 read shows contact within 300 ms; otherwise it is counted as spurious and the board sleeps again (giving up after 20 in a row). |
| Timer | Counted (`backstopWakes`). A touch only if a single read shows contact. |
| UART | `serial`: any 3 characters on the serial monitor wake the board. |

After a touch wake, the press is swallowed (1.5 s window), as for any dark-screen wake.

**Safety gate**: Sleep is only entered if the backstop can read an awake CST820 (`backstop` on, `touchsleep` off, the defaults), or if INT has already woken the dark screen this boot (`stat: pwr verified=1`). Resistive boards never sleep.

## Software checks

Run once, on the code of commit 54a7f1c (built immediately before committing, with no edits in between):

```
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel power
PASS pristine MiniAAC + verified seek overlay staged
PASS pinned Helix MP3-only core staged unchanged; heap buffers.c excluded
PASS compact FLAC staged; legacy core/header excluded
Sketch uses 1591913 bytes (80%) ... Global variables use 87928 bytes (26%)
PASS single pool: 3ffc5788 00007868 b _ZN12_GLOBAL__N_1L9codecPoolE
PASS compact FLAC staged; legacy core/header excluded
Sketch uses 1592057 bytes (80%) ... Global variables use 87928 bytes (26%)
PASS single pool: 3ffc5788 00007868 b _ZN12_GLOBAL__N_1L9codecPoolE
PASS: compile-only checks completed; no device commands were run.
SUMMARY board=jc2432w328c images built=2 total seconds=68.40
```

Static RAM is 56 bytes above 0.5.1: the codec pool moved from 0x3ffc5750 to 0x3ffc5788.

No host tests were added. The new logic is hardware-facing: radio, light sleep and the controller's INT. **None of this is hardware evidence.** Every gate below is owner-measured.

## Owner gates and expected log lines

1. **Baseline**: S0-S5 on 0.5.1, then S1-S5 on this candidate with `pwr` levers toggled one at a time (`stat: pwr` every 10 s).
2. **Touch INT** (P1 gate):
   - With the screen dark, tap. Expect `touch: screen-off press seen by INT`, and `verified=1` in `stat: pwr`.
   - `backstop poll` instead means INT edges are not arriving.
   - Note the `pulse=` read-back (200 means the register exists).
   - Optional: `pwr on touchsleep`, wait 60 s dark, then tap.
3. **Bluetooth cycle** (P3a gate):
   - Run `pwr btcycle 20` with the saved speaker in range.
   - Each cycle logs `pwr: btcycle N drift free … largest …` relative to cycle 1. Pass: within ±1024 B after cycle 20, and `min` stays above 10,240.
   - The speaker must reconnect after each restart (about 10-15 s, because of the library's stack-up wait), and AAC must play afterwards.
4. **A2DP suspend** (P2):
   - Connected and paused for more than 10 s, expect `bt: media cmd=SUSPEND rc=0 (idle stream)` and an AUDIO SUSPEND event.
   - Play: `bt: idle stream resume queued`, then `bt: media cmd=START`. Audio should start within about 500 ms.
   - Watch for speakers that disconnect themselves while suspended.
5. **Sleep** (P3b):
   - `pwr sleepnow` (10 s window), or wait 5 min without playback or touch.
   - Expect `pwr: sleep entering` and `bt: stack stopped … status=1`.
   - Touch: `pwr: woke from sleep after Ns cause=touch` (or `backstop`), the same screen and track, then `bt: stack restarted`, and the speaker reconnecting.
   - S6 reading with `pwr off backstop` and with it on.
   - Run 20 touch wakes, and compare the `pwr: sleep after heap` lines.

## First board run (2026-11-07, firmware f70ccde)

- Dark-screen idle drew about 0.3 W, the owner's lowest reading so far. Sleep was not entered in that run (`sleeps=0`).
- `int edges=0` for the whole log, including touches with the screen lit: INT never reached the ISR. Dark-screen taps were found only by the 250 ms backstop, and quick taps were missed (about 3 presses to wake). Commit 54a7f1c addresses this; INT itself is still unverified.
- Bluetooth: both speakers ended each page with a normal remote disconnect, including the library's first page at boot, before any power code ran. After a restart with the speaker in pairing mode it reconnected. This points to speaker-side bond or busy state, not the power changes.

## Known limits

- Every wake from Sleep makes the speaker wait for the library's 10 s stack-up delay before its single page.
- If the speaker was connected when Sleep began, Sleep disconnects it. The reconnect after wake is the library's boot page, then the normal policy.
- On the wired route, the I2S clocks stop during light sleep. Listen for a pop at sleep and wake.
- `touchsleep` and the 0xED pulse register are unverified on the CST820. The INT pin uses the board's own pull-up; `pinMode` is unchanged.
- The CPU stays at 240 MHz while awake (owner decision). Light sleep is the only CPU saving.
