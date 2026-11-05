# CY-94: phone-like touch scrolling (research only)
Base 4474418 (v24). S = sourced, I = inferred, U = unmeasured. Paths under `firmware/cydpod_player/app/`.

## 1. Today (S)
- `cydclassic::Gesture` (`classic_navigation.h:10-30`): claim as scroll at 10 px vertical-dominant or 16 px any direction (:11); tap slop 8, cancel 24.
- Offset is whole rows: `top0 + (y0-py)/RowH` (:27), `RowH=40` (`classic_layout.h:6`). Integer division gives a dead zone; the list jumps 40 px. No 1:1 tracking.
- `classic_screen.cpp:721-723`: if claimed and |dy|>=10, `page().top=top; invalidate(); footer()`. `invalidate()` (:255-262) clears all cached rows and marks all dirty; `tick` repaints ONE row per loop pass, and only if `!audio_bufferLow()` or 120 ms elapsed (:556-557). `loadRow` may read the SD. So a step sweeps rows in from the top.
- No flick, inertia or rubber-band. The release is swallowed (:751). Alternative paging: 30 px strip, `rows()-1` per tap (:768). Thumb 3 px (:261).
- `rows()=min(6,(h-80)/40)`: 6 portrait, 4 landscape (`classic_layout.h:8`).

## 2. Input and frame facts (target: JC2432W328C, capacitive only)
- Resistive XPT2046 boards (bit-banged, `touch.cpp`) are out of scope; ignore.
- Controller: CST820, I2C addr 0x15, SDA33 SCL32 RST25 INT21 (`board.h:76-87`, `touch_cst820.cpp:59-72`). Single contact only: count byte must be 0 or 1 (`cst820_report.h:16-17`). S. Hardware gesture byte is decoded as diagnostic only (:8) and is useless for kinetic scroll (no velocity).
- Read: polled, not interrupt-driven. INT is set `INPUT` and never read (`touch_cst820.cpp:60`). Each `touch_readRaw` is one 6-byte burst from reg 0x01 (gesture, count, X, Y) with a repeated start, I2C 400 kHz, bounded 2 ms timeout, no retries (:64-66, :78-93). S. I: about 0.3 ms per read (about 100 bits at 400 kHz); `stat: CST820 readMaxUs` (:114) reports the true worst case. U.
- Poll rate: one read per `loop()` pass (`app.ino:275-328`, `delay(2)` at the end), so up to about 300-400 Hz on a quiet pass, but only as fast as the controller has fresh data. Report rate of the CST820 itself is not in the repo or its sourced docs: I from the CST816 family, about 60-100 Hz (U). Consequence: many polls return the same coordinate; the app sees fresh points at the controller rate, not the poll rate. The tracker only dispatches MOVE when the point changed (`ui.cpp:357`).
- Filtering: no calibration, no pressure (z fixed to 1; `touch_cst820.cpp:91`), out-of-range samples rejected (`cst820_report.h:20`). Same `PressTracker` as resistive: 0.5 EMA (`touch_gesture.h:60-62`), 60 ms release grace, 120 ms re-touch debounce. The EMA adds lag (about one controller period of delay): fine for taps, should be bypassed or lightened for velocity. Capacitive jitter is typically about 1 px (I, U); not measured in repo.
- I2C error recovery: 20 consecutive failures reset the controller (10 ms low + 300 ms settle; :33-37, :40-56). Touch is dead for 300 ms then. Leave as is.
- Orientation: `CYD_ROTATION 1` = 320x240 landscape, 4 list rows (`board.h:81`, `classic_layout.h:8`). Portrait gives 6.
- Display: ST7789 240x320 at 40 MHz (`tools/tft/User_Setup_JC2432W328C.h:28`), no DMA/sprite/viewport use in app. I: 290x40 row about 23 KB = 4.6 ms wire; 4-row viewport about 19 ms + text; 6-row about 28 ms: roughly 20-30 fps (U). So the display, not touch, limits smoothness; a faster touch rate improves velocity estimation and latency, not frame rate.

## 3. Ranking (feel gain / cost)
1. Pixel offset with 1:1 tracking: biggest gain, prerequisite for the rest; medium cost (partial-row paint).
2. Slop handling (keep 10/16 px, subtract it so no jump): trivial.
3. Velocity over the last ~100 ms + exponential fling: big gain, math is free, but needs a repaint per frame.
4. Tap-to-stop: small, required once flinging.
5. Soft edge stop (clamp + short ease): cheap. True rubber-band needs blank-area paint; defer.
6. Scrollbar from pixel offset: cheap, thumb exists.
7. Snap to a row at rest: optional toggle; rows are grid-aligned and a short settle ease is enough.

## 4. Rendering and audio
- Hardware vertical scroll (VSCRSADD) works only on the native 320 px axis. Default landscape rotation 1 (320x240) scrolls across the other axis, and the right strip would move too: not usable (I; `board.h:205`).
- Recommended: repaint only the list viewport at the pixel offset, rows clipped, opaque backgrounds (no clear-then-draw flicker). Header, footer, mini-player and strip untouched. Rows come from `cache[6]` plus 2 prefetched rows; no SD reads during a gesture.
- Tearing: no TE/vsync used, so a mid-sweep tear is possible (I); top-to-bottom order minimises it. Judge on device.
- RAM: no sprite (23 KB one-row sprite is a poor trade; CY-42 only reclaimed 16.5 KB, commit 8e2cb98). New state about 40 B + 2 `Row` (about 0.5 KB). The 40,960 B codec pool (`codec_track.cpp:32-35`) is untouched.
- Cores (S): decode `m4adec` core 1 prio 5 (`audio.cpp:418`); speaker writer core 0 (`speaker.cpp:245`); UI/touch/`bt_tick`/`audio_tick` run in the Arduino loop task (prio 1), preempted by decode.
- Audio stutter today: not evidenced either way in docs. The code defers row paints when the buffer is low (`classic_screen.cpp:556`), so the authors saw it as a risk. A viewport frame holds the loop task 20-30 ms but stays preemptible; the SD read in `loadRow` (shared VSPI) is the real risk, hence no SD in a gesture. U: watch `underruns` in `stat:` while flinging.

## 5. Constants (I, starting values)
- Slop: keep 10 px vertical / 16 px any; start tracking at the claim point with no jump.
- Velocity: ring of 8 (ms, y) pushed ONLY when the coordinate changes (about 6-10 points in 100 ms at 60-100 Hz); least-squares slope over the last 100 ms, using the raw (un-EMA) point; U whether 100 Hz holds.
- Min fling 0.15 px/ms; max clamp 3.0 px/ms.
- Friction: v *= exp(-dt/325 ms), stop below 0.02 px/ms. A 1.5 px/ms flick travels about 490 px (12 rows).
- Frame step: real dt clamped 10-50 ms, one paint per loop pass, target 25-30 fps (U).
- Tap-to-stop: touch while |v|>0.02 halts and swallows that tap. Edge: zero velocity and clamp, optional 8 px ease.
- No LVGL or kinetic library in repo (only TFT_eSPI), so no reference; this is the plain Android-style exponential fling.

## 6. Recommendation
Add header-only `scroll_core.h` (no Arduino): `feed(ms,y)`, `release(ms)`, `step(dt)` -> clamped px offset in `[0,(count-rows)*RowH]`. Host test next to the `touch_gesture` tests: 1:1 equality, 100 ms velocity, decay curve, clamp, slop vs scroll, stop-on-touch.
Firmware: in `classic_screen.cpp` replace `scrollTop`/`invalidate()` at :721-723 with `scroll.feed`; `tick` calls `scroll.step`; new `paintViewport(offsetPx)` draws the 1-2 partial rows via the same palette code as `paintRow`. `Gesture` keeps tap/claim; `page().top = offset/RowH` for the rest. CPU: math negligible, paint 19-28 ms/frame (U). RAM <1 KB.
Chunks (about 20 agent-min each): (1) `scroll_core.h` + host test; (2) `paintViewport` with 1:1 tracking, compile-only both boards; (3) fling, tap-to-stop, edge stop, `stat: scroll` frame-time log; (4) scrollbar from px, snap toggle, constants pass.
Overlap: CY-91 (themes) and CY-92 (headphone limit) also touch `classic_screen.cpp` (palette use in `paintRow` :239, Display/Output rows :306-308) and `settings.cpp/.h`. Chunk 1 is free of overlap; do chunk 2 after CY-91 lands.
Owner decides: keep the paged strip? snap on/off; fling distance (tau); edge style; primary orientation to tune. On hardware: real poll rate (`pollGapMax`), rest jitter, viewport frame time, tearing, audio `underruns` while flinging, controller report rate (count distinct points per second in a drag).
