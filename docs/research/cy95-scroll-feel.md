# CY-95: touch-scroll "feel", Android (AOSP) as the reference (research only, replaces CY-94 section 6)
Base 48fdb2d. **S** = read in the cited source this session; **I** = inferred/derived (numbers I computed are marked "calc"); **U** = unmeasured on the device. No firmware, device or SD was touched. Target: JC2432W328C (CST820, single touch, 2.8" about 143 ppi per `cy85-text-quality.md`, ST7789 SPI 40 MHz, no PSRAM).

## 0. Verdict
1. **Feel target = stock Android `OverScroller` fling + `VelocityTracker` LSQ2 + `ViewConfiguration` constants**, ported as a pure C++ core (section 5). The spline fling is closed-form in time, so it is evaluated at wall-clock time and survives 20-30 fps.
2. **LVGL (section 6) is adoptable only if one gate passes: heap.** Forecast free heap during playback is about 46 KB (CY-43, arithmetic, unmeasured) against a 10,240 B floor; LVGL's own README says "bare minimum 32 kB RAM" plus buffers. Flash is not a problem (68% used, 618 KB free). LVGL does **not** speed up pixel output or fix the SD problem. Its default throw is *not* Android-like at high speed (calc, section 2.3), so even with LVGL I recommend feeding it the AOSP core (momentum off).
3. Cheapest first move is a **measured LVGL spike (chunk S0)** that decides go/no-go with numbers; the AOSP core (chunks C1-C2) is useful in both branches, so start it in parallel.

## 1. Android reference (primary)
### 1.1 Fling: `OverScroller.SplineOverScroller` (S: [OverScroller.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/widget/OverScroller.java))
- `friction = ViewConfiguration.getScrollFriction() = 0.015` (S: [ViewConfiguration.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/view/ViewConfiguration.java)).
- `physicalCoeff = 9.80665 * 39.37 * ppi * 0.84`, `ppi = density*160` (S). Flutter uses ppi=160 (S: [scroll_simulation.dart](https://github.com/flutter/flutter/blob/master/packages/flutter/lib/src/widgets/scroll_simulation.dart)). **We must use the real ppi (about 143)** so a flick travels the same physical distance; this makes the fling about 9% longer than on a 160-ppi reference (calc).
- `DECEL = ln(0.78)/ln(0.9) = 2.3582`, `INFLEXION = 0.35`, `START_TENSION 0.5`, `END_TENSION 1.0`, `P1 = START_TENSION*INFLEXION = 0.175`, `P2 = 1 - END_TENSION*(1-INFLEXION) = 0.35`, `NB_SAMPLES = 100` (S).
- `l = ln(INFLEXION*|v| / (friction*physicalCoeff))`; **duration_ms = 1000*exp(l/(DECEL-1))**; **distance = friction*physicalCoeff*exp(DECEL/(DECEL-1)*l)** (S). Distance is about v^1.74 (calc), not linear in v.
- Position curve: tables `SPLINE_POSITION[0..100]` built by bisection (cubic Bezier through control values P1/P2 in time and 0.5/1.0 in position, solved to 1e-5). Each sample i: solve x with `tx = 3x(1-x)((1-x)P1 + x P2) + x^3 = i/100`, then `pos = 3x(1-x)((1-x)0.5 + x) + x^3` (S, loop described by the fetched source; reproduced and run in `/tmp`, not committed). Equivalent closed form: position(t) = cubic-Bezier with control points (0.175,0.5),(0.35,1) (I, matches the table; verify in the host test). At t=0 slope is 2.854 (calc), so initial speed equals the release velocity.
- `update()`: `t = elapsed/duration`, `i = (int)(100*t)`, linear interpolation between tables; `position = coef*distance`, `velocity = slope*distance/duration*1000` (S).
- 90% of the distance is covered by t = 0.57 (calc): the fling is front-loaded, then a long soft tail.
- Expected values at ppi 143 (calc, my reproduction of the formulas above): 

| release v px/s | duration | distance px (rows of 40) |
|---|---|---|
| 200 | 184 ms | 13 (0.3) |
| 500 | 362 ms | 63 (1.6) |
| 1000 | 603 ms | 211 (5.3) |
| 2000 | 1005 ms | 703 (17.6) |
| 4000 | 1674 ms | 2343 (59) |
| 7150 (max) | 2567 ms | 6423 (160) |

- Edge: SPLINE reaching a bound goes BALLISTIC (constant deceleration over `OVERFLING_DISTANCE`), then CUBIC springback `distance = sign*over*(3t^2-2t^3)`, duration `1000*sqrt(-2*delta/decel)` (S, summary of fetched source; exact code needs a re-read when porting). Flywheel: a new fling during a running one adds velocity only when same sign (S).
- Stock list views use `ViscousFluid`/quintic interpolators only for programmatic `smoothScrollBy` (RecyclerView `sQuinticInterpolator = t^5+1`, S: [RecyclerView.java](https://github.com/androidx/androidx/blob/androidx-main/recyclerview/recyclerview/src/main/java/androidx/recyclerview/widget/RecyclerView.java)); the finger fling uses the spline.

### 1.2 `ViewConfiguration` constants (S: ViewConfiguration.java above), converted at 143 ppi (density 0.894, calc)
| name | dp value | px here |
|---|---|---|
| TOUCH_SLOP | 8 | 7 (today: 10/16) |
| PAGING_TOUCH_SLOP | 16 | 14 |
| MINIMUM_FLING_VELOCITY | 50 dp/s | 45 px/s |
| MAXIMUM_FLING_VELOCITY | 8000 dp/s | 7150 px/s |
| OVERFLING_DISTANCE | 6 | 5 |
| OVERSCROLL_DISTANCE | 0 | 0 |
| TAP_TIMEOUT / LONG_PRESS | 100 ms / 400 ms | - |
| SCROLL_BAR_DEFAULT_DELAY / FADE | 300 / 250 ms | - |

### 1.3 Velocity: `VelocityTracker` (S: [VelocityTracker.cpp](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/libs/input/VelocityTracker.cpp), [VelocityTracker.h](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/include/input/VelocityTracker.h))
- Default strategy for X/Y = **LSQ2** (2nd-order least squares; the doc calls LSQ2 and Impulse "VERY GOOD", LSQ1 "POOR", LSQ3 "UNUSABLE", S: [VelocityTracker.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/view/VelocityTracker.java)). Caveat: default can change between releases (U). Impulse is the default for scroll-wheel axes.
- **HORIZON = 100 ms**, history 20 samples. Sample x = -age (s), y = position; velocity = linear coefficient of the quadratic at the newest sample; degree is reduced to `size-1` (2 samples = linear, 1 sample = no velocity) (S).
- **ASSUME_POINTER_STOPPED_TIME = 40 ms**: a sample arriving more than 40 ms after the previous one clears the history; on ACTION_UP, if the last event is older than 40 ms the velocity is dropped, so **hold-then-lift gives no fling** (S).
- Impulse: `work += (vcurr - vprev)*|vcurr|`, first term halved, `v = sign*sqrt(|work|)*sqrt(2)` (S). Cheaper (no matrix) and robust at 6-10 samples; pick by host test against recorded drags.
- Result clamped to `[-maxFling, +maxFling]`; fling starts only if `|v| > minFling` (S: ViewConfiguration; the RecyclerView/AbsListView call sites were not read: I).

### 1.4 Drag start and tap-to-stop
- I (RecyclerView/AbsListView, not re-read): scrolling starts after `|dy| > touchSlop` and the slop is subtracted so the content does not jump. Touch-down during a fling aborts the scroller (`abortAnimation`) and the touch becomes a drag (no tap). The CY-94 report found the current UI jumps a full 40 px row and swallows the release (CY-94 section 1); both are to be replaced.

### 1.5 Edge effect (S: [EdgeEffect.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/widget/EdgeEffect.java))
- Android 12+ default is the **stretch** (a GPU `RenderNode.stretch` of the whole list). Constants: `LINEAR_STRETCH_INTENSITY 0.016`, `EXP_STRETCH_INTENSITY 0.016`, `SCROLL_DIST_AFFECTED_BY_EXP_STRETCH 0.33`, `ON_ABSORB_VELOCITY_ADJUSTMENT 13`; release spring (closed-form underdamped) `NATURAL_FREQUENCY 24.657`, `DAMPING_RATIO 0.98`; dampening `sign*(0.016*o + 0.016*(1-exp(-o*e/0.33)))` for pull distance o.
- Pre-12 glow: `RECEDE_TIME 600`, `PULL_TIME 167`, `PULL_DECAY_TIME 2000`, `MAX_ALPHA 0.15`, `MIN/MAX_VELOCITY 100/10000`.
- **Not reproducible here**: stretch resamples the whole list, glow needs alpha blending over existing pixels (no frame buffer). **Approximation (I)**: translate the list by a small damped pixel offset (clamp to about 0.5 row) and settle it with the `EdgeEffect` spring (omega 24.657, zeta 0.98, about 250 ms to rest, calc), plus a scrollbar thumb flash. On a 4-row viewport an 8-20 px translate needs the bar between rows; see 4.

## 2. Comparison (brief)
### 2.1 iOS / Flutter (S: [scroll_physics.dart](https://github.com/flutter/flutter/blob/master/packages/flutter/lib/src/widgets/scroll_physics.dart), [friction_simulation.dart](https://github.com/flutter/flutter/blob/master/packages/flutter/lib/src/physics/friction_simulation.dart); Apple page unreadable, rates from the Flutter comment)
- iOS `decelerationRate` normal 0.998 / fast 0.99 per ms, so v(t) = v0*0.998^t (t in ms); x(t) = v0*(D^t-1)/ln D (Flutter `FrictionSimulation`, drag 0.135 per second). Pure exponential, time constant 500 ms (calc): distance = v0*0.5 s, linear in v. Rubber band (Flutter): friction factor `(1-overscroll/viewport)^2 * 0.52` (fast 0.26), settle spring half-life 0.07 s.
- Flutter Android ("Clamping") = the same spline as AOSP (`_kDecelerationRate = ln0.78/ln0.9`, `_kInflexion 0.35`, `physicalCoeff = 9.80665*39.37*160*0.84`, `friction 0.015`, x(t) = d*(1-(1-t)^2.3582)).
  Note this is an alternative, simple closed form of the spline position: `position(t) = d*(1-(1-t)^DECEL)`, velocity `v0*(1-t)^(DECEL-1)`. **Use it as a cross-check** against the table version in the host test (I: they should agree within a few percent, not verified).

### 2.2 Embedded
- Rockbox touchscreen lists: kinetic scroll since 2010, physics in the code comment "v = a*t + v0, ds = v*dt", hard-coded redraw interval and deceleration, stops at list edges (S: [Rockbox commit](https://git.lenord.me/devs/foxbox/commit/b673ae2c46129d6994b61a351a1567c56f4aef82); search result summary, constants not read). Constant deceleration = distance ~ v^2, closer to AOSP's 1.74 exponent than iOS/LVGL's 1.0 (I).
- ST7789 register facts only: VSCRDEF 0x33 and VSCSAD 0x37 exist ([ESPHome header](https://esphome.io/api/st7789v_8h_source.html)); no source found for a framebuffer-less list implementation. Treat section 4 as engineering inference.

### 2.3 LVGL (S: [lv_indev_scroll.c](https://raw.githubusercontent.com/lvgl/lvgl/master/src/indev/lv_indev_scroll.c), [lv_indev.c](https://raw.githubusercontent.com/lvgl/lvgl/master/src/indev/lv_indev.c), [lv_conf_template.h](https://raw.githubusercontent.com/lvgl/lvgl/master/lv_conf_template.h))
- Defaults: `LV_INDEV_DEF_SCROLL_LIMIT 10` px (slop), `LV_INDEV_DEF_SCROLL_THROW 10` ("slow-down, greater = faster"), `LV_DEF_REFR_PERIOD 33` ms (also the indev read period and animation step), `LV_INDEV_DEF_LONG_PRESS_TIME 400`.
- Velocity = sum of the last pointer vectors in a ring, each decayed linearly by age: `x*(512 - 512*t/99)/512`, zero at t>=99 ms (about the same 100 ms horizon as AOSP, but a weighted sum of per-read vectors, not LSQ). No min/max fling clamp seen.
- Throw: every read tick `v = v*(100-scroll_throw)/100` (integer) until 0 (exponential, tick-based, so it depends on the read period). Elastic: overscroll drag divided by `ELASTIC_SLOWNESS_FACTOR` (default value not in the fetched file: U). Snap: `lv_indev_scroll_get_snap_dist`, animation `ease_out`, `SCROLL_ANIM_TIME_MIN/MAX 200/400 ms`.
- **Calc (my model: v = px per 33 ms tick at release, integer decay)** vs AOSP at 143 ppi:

| release px/s | LVGL thr 10: dist / ms | thr 6 | thr 4 | AOSP |
|---|---|---|---|---|
| 500 | 81 / 429 | 120 / 528 | 120 / 528 | 63 / 362 |
| 1000 | 213 / 627 | 312 / 792 | 412 / 957 | 211 / 603 |
| 2000 | 484 / 825 | 765 / 1122 | 1029 / 1386 | 703 / 1005 |
| 4000 | 1043 / 1023 | 1706 / 1452 | 2428 / 1881 | 2343 / 1674 |

  LVGL's default **matches AOSP at 500-1000 px/s and falls short at flicks** (linear in v vs v^1.74), and no single `scroll_throw` fits both ends. Mapping of the model to the real code is I; confirm with the host harness (chunk S0).

## 3. What creates the feel (ranked) and what survives low fps
Rank 1 is the property people notice first; "survives" = still works at 20-30 fps draw and 60-100 Hz touch.

| # | property | survives low fps? | how |
|---|---|---|---|
| 1 | 1:1 tracking, no slop jump | yes | pixel offset = finger delta from the claim point; draw at the *latest* touch sample (poll right before paint) |
| 2 | Latency touch -> pixels | partly | poll touch immediately before each paint; budget one frame (35-45 ms) (I) |
| 3 | Fling speed continuity at release | yes | LSQ2/impulse over 100 ms of *changed-coordinate* samples; ignore stale (40 ms) |
| 4 | Deceleration curve shape | yes | evaluate `position(now - t0)` at wall clock, never integrate per frame (frame drops cannot change distance/duration) |
| 5 | Tap-to-stop | yes | abort scroller on down; swallow the tap |
| 6 | Edge behaviour | yes | clamp + small damped translate + spring |
| 7 | Frame pacing | no | physics cannot fix 25 fps. Keep spacing even: fixed minimum frame interval (about 40 ms), skip a frame rather than draw late; do not render while the SD read is running |
| 8 | Motion smoothing at low fps | yes | **extrapolate the finger** during drag (draw at `y + v*latency`, latency about one frame, capped at 1 row, I); during fling draw at the position predicted for the *display time* of the frame (now + expected paint time) |

Fixed-step simulation is not needed: the spline is closed-form. Motion-adaptive easing is unnecessary for the same reason; the only per-frame filtering is the optional finger extrapolation (clamp it, drop it at direction reversals).

## 4. Low-fps rendering tricks, judged for ESP32 + ST7789 SPI 40 MHz, no frame buffer
Bus arithmetic (I, 16 bit pixels, 40 MHz): 1 px = 0.4 us; 290x160 (4-row landscape viewport) = 46,400 px = 18.6 ms; 6-row portrait 210x240 = 20.2 ms; matches CY-94. LVGL's own blog measured a full 320x240 frame at about 30 ms transfer (S: [blog.lvgl.io](https://blog.lvgl.io/2019-01-31/esp32)).

| trick | feasible here? |
|---|---|
| Full viewport repaint at pixel offset (current plan CY-94) | **Yes, landscape fallback.** 19 ms bus + text CPU; expect 15-25 fps (U) |
| Dirty rectangle / changed strip only | Partly: when scrolling, every pixel of the list moves, so the dirty area is the viewport; helps only for highlight, thumb, edge settling |
| Memmove of rendered region | **No**: needs the pixels in RAM (4-row viewport = 93 KB). Heap is about 46 KB, pool 40,960 B untouchable |
| **Read back from ST7789 and shift** (RAMRD 0x2E) | **No.** Reads return 3 bytes/pixel at a much slower serial read clock (datasheet read cycle is much longer than the write cycle; exact figure unverified, PDFs not readable here: [ST7789V](https://cdn.shopify.com/s/files/1/0264/7629/files/ST7789V_V1.4.pdf)). Even at an optimistic 10 MHz one 4-row viewport is 46,400*3*8/10e6 = 111 ms (calc). Also the board MISO routing for this panel is unverified (`board.h` defines GPIO12 for the older boards only) |
| **One-row-strip redraw** (draw only rows that become visible) | Needs the shift primitive. Only possible with hardware scroll (next row) |
| **Hardware vertical scroll (VSCRDEF 0x33 + VSCSAD 0x37)** | **Best option, portrait only.** Native scroll axis is the 320-line axis; in landscape (`CYD_ROTATION 1`) the list axis is the 240 axis, so unusable (CY-94 section 4). In portrait: TFA = header 36, BFA = footer/mini 44, VSA = 240 = exactly 6 rows. Per frame, only newly exposed rows are drawn (ring buffer wrap handled by splitting rects) and the shift costs 3 bytes of SPI. At 2000 px/s and 25 fps that is 80 px = 38 KB = 7.7 ms (calc) vs 20 ms repaint, and cost falls with speed. Risks (U): tearing (no TE line, scroll register applies at the next panel refresh), the 30 px strip and thumb scroll with the content (strip is a flat colour: erase/redraw thumb at ring-mapped coordinates), selection highlight must be ring-aware, CJK strips (`titles.cpp`) draw into the ring |
| Line buffer + DMA double buffering | Yes, mechanism for any redraw: 2 x 580 B (290 px) lines render text rows scanline-wise; needs a scanline text composer, biggest code cost; defer (stretch chunk) |
| Reduced detail while flinging | **Yes, cheap.** Above about 3 rows/s skip the detail line, icon and divider; draw title only or a bar placeholder. At that speed content moves a row per frame, nobody reads it |

**Recommendation**: ship the full-viewport repaint (works on both orientations), designed behind a `Scroller` interface so a portrait HW-scroll renderer can replace it. Ask the owner which orientation is primary (Q1).

## 5. Revised plan (replaces CY-94 section 6): port the AOSP core
### 5.1 Core: `scroll_core.h` (no Arduino includes, `float` + `int32`; host-testable). Layout in app folder, test next to the `touch_gesture` tests.
```
struct ScrollCfg { float ppi=143, friction=0.015f, minFling, maxFling, slopPx, horizonMs=100, stopMs=40, overflingPx, ... };
class Tracker  { void down(ms,y); bool move(ms,y)->claimed; void up(ms)->Fling; }   // slop subtract, ring[20] of (ms,y), pushes only on changed y
class Velocity { void add(ms,y); void clear(); float get(strategy);}               // LSQ2 (3x3 normal equations) and Impulse
class Fling    { void start(y0,v,minY,maxY,nowMs); float pos(nowMs); float vel(nowMs); bool done(nowMs); float finalY(); }  // SPLINE table + BALLISTIC + CUBIC springback
class Edge     { float dampen(pull); void settle(distance,vel,nowMs); float offset(nowMs); }  // EdgeEffect spring
```
Constants exactly as section 1 (friction 0.015, DECEL 2.3582, INFLEXION 0.35, P1 0.175, P2 0.35, NB 100, ppi 143, slop 7 px, min 45 px/s, max 7150 px/s, horizon 100 ms, stop 40 ms, overfling 5 px, spring 24.657/0.98). The 101-float position table (404 B, `const`, flash) is generated at start-up with the AOSP bisection loop (about 2 x 100 x 17 iterations, calc) or stored precomputed; no heap, state under 200 B.

### 5.2 Mapping to 20-30 fps and 60-100 Hz CST820 (all I unless stated)
- Touch: one read per `loop()` pass; push (ms,y) to the velocity ring **only when y changes** (CY-94 section 2). At 60-100 Hz the 100 ms window holds 6-10 points, enough for LSQ2 (S: needs at least 3 samples for degree 2). Bypass the 0.5 EMA of `PressTracker` for velocity (it adds about one report period of lag, CY-94); keep it for taps. Pending verification (U): real report rate and rest jitter.
- Draw: fixed order each frame: poll touch, advance state to `now`, draw once, no catch-up. Fling position is `Fling::pos(now)`; frame skips lose nothing.
- Approximations vs AOSP: (a) stretch -> translate (1.5); (b) no glow; (c) the finger-extrapolation term; (d) the display shows steps of 2+ rows per frame above about 1600 px/s (calc: 40 ms/frame), which no physics fixes, hence reduced-detail rendering; (e) ppi is scaled assuming 143 ppi (U for the exact panel).

### 5.3 Verification of feel (host)
1. **Golden curves from real Android** (authoritative): a 30-line Android app (or Robolectric test) that constructs `OverScroller(ctx)`, calls `fling(0,0,0,v,...)` for v = 100, 200, 500, 1000, 2000, 4000, 7000 px/s at known density, and logs `(currTime, getCurrY, getCurrVelocity)` every 8 ms plus `getFinalY()` and the duration to CSV (the app needs a phone; the core computes with ppi as an argument, so compare at the phone's density). Stored under `tests/data/` (small).
2. Fallback golden: the python reproduction of the AOSP formulas (not committed; trivially re-created from section 1.1).
3. Host test `scroll_core_test.cpp`: position error < 1 px or 0.5% vs the CSV, duration within 1 frame, `finalY` equal; LSQ2/impulse velocity on synthetic drags (constant v, ramp, stop-then-lift = 0, direction reversal), recorded real drags once captured by the debug command (`scroll rec`), slop continuity (first claimed offset 0), tap-to-stop, edge clamp/spring monotonicity, flywheel.
4. Feel itself (U) is tuned on the device with the knobs in 5.5.

### 5.4 Chunks (about 20 agent-minutes each; one commit; smoke checks)
- **C1** `scroll_core.h`: Fling (table, BALLISTIC, CUBIC, flywheel) + `Edge` spring + host test against formula goldens (python-computed table checked in as `tests/data/aosp_fling.csv`). Check: host test.
- **C2** `Velocity` (LSQ2 + Impulse, 100/40 ms rules) + `Tracker` (slop subtract, claim, up) + host tests. Check: host test.
- **C3** Firmware seam: `paintViewport(offsetPx)` for the list (1:1 drag, no fling), `Gesture` keeps tap/claim, replaces `invalidate()`+whole-row steps at `classic_screen.cpp:721-723`. Check: compile-only both boards (`-SkipProbe -SkipHostMedia`). Do after CY-91 lands (shared `paintRow` palette code).
- **C4** Fling + tap-to-stop + edge translate/spring in `tick`; `stat: scroll` line (frame ms, max frame ms, claim->first-paint ms, underruns delta). Compile-only.
- **C5** Data policy (section 7): row window cache, placeholders, velocity-gated load, landing-position prefetch. Host test of the policy on a fake slow loader; compile-only.
- **C6** Debug command `scroll` (5.5) + `scroll rec` capture. Compile-only.
- **C7** Optional: reduced-detail fling rendering; scrollbar from pixel offset with fade (300/250 ms).
- **C8** Optional, large (needs split): portrait HW-scroll renderer.
Device work (separate, authorised): flash, feel pass with the owner, underrun check while flinging.

### 5.5 On-device tuning knobs (debug serial command; values live in `ScrollCfg`, RAM only, defaults above)
`scroll show | scroll friction <0.005..0.05> | scroll gain <velocity multiplier 0.5..2.0> | scroll win <ms 60..200> | scroll slop <px 3..14> | scroll maxv <px/s> | scroll edge <px>` and `scroll strat lsq2|impulse`. The five that matter most: **friction** (fling length), **gain** (flick sensitivity), **win** (velocity window), **slop**, **max/min fling**. All are in the core, so no reflash is needed to change them; persisting them belongs to Settings after the owner picks values (Q5).

### 5.6 Proven vs to tune
Proven by source (S): fling/spline formulas and constants, ViewConfiguration values, VelocityTracker horizon/stop rules, EdgeEffect constants. To tune on device (U): ppi scaling and friction, velocity gain on a 100 Hz capacitive panel, jitter, frame time and tearing, edge amplitude, reduced-detail thresholds, audio underruns while flinging.

## 6. LVGL as the default candidate: only blockers (all measurable in S0)
### 6.1 Facts
- Needs per the project's README: typical UI about 100 kB RAM / 200-300 kB flash; "bare minimum 32 kB RAM, 128 kB flash" (S: [README](https://raw.githubusercontent.com/lvgl/lvgl/master/README.md)). `lv_conf_template.h`: `LV_MEM_SIZE 65536` default, minimum 2 kB, `LV_USE_STDLIB_MALLOC` can be `LV_STDLIB_CUSTOM`, one 14 pt Montserrat font by default (S).
- Our budget: flash 1,347,453 of 1,966,080 B (68%), 618 KB free (S: `classic-touch-ui-planning-validation.md`). Static RAM 76,632 B used. **Free heap during playback with Bluetooth connected: predicted 46,408 B (CY-43, arithmetic, not measured; was 29,704 B in CY-31)**, floor 10,240 B (`cy9-test-plan.md`), so the whole UI + LVGL must live in about 36 KB (I).
- Display: v9 ships `lv_tft_espi` (S: [lv_tft_espi.cpp](https://raw.githubusercontent.com/lvgl/lvgl/master/src/drivers/display/tft_espi/lv_tft_espi.cpp)): `flush_cb` = `setAddrWindow` + `pushColors(px, w*h, true)` between `startWrite/endWrite`, then `lv_display_flush_ready`; one buffer, `LV_DISPLAY_RENDER_MODE_PARTIAL`. No touch code there.

### 6.2 Blockers to measure
| concern | estimate (I) | gate to pass |
|---|---|---|
| Heap | pool 12-16 KB (custom allocator `heap_caps_malloc` so it can be freed on leaving the screen) + draw buffer 6.4 KB (320x10 lines x2 B; 1 buffer, blocking flush) + a list of 8 row objects, 2 labels each, about 8 KB; total 24-30 KB | worst-case free heap while playing + BT + search >= 10,240 + 8 KB reserve; `largest` >= next request + 4,096 (the CY-11 rules) |
| Flash | +200-300 KB (README, typical) | image <= 80% of 1,966,080 B |
| Speed | LVGL redraws the whole scrolled viewport each frame, same 19 ms bus as 4 (S: LVGL blog: 30 fps on 320x240 SPI/DMA 40 MHz needs double buffering); SW render of text/gradients adds CPU. **LVGL does not remove the bus limit.** Expect 15-25 fps (U) | list scroll >= 20 fps frame average, max frame <= 60 ms, in 4-row landscape |
| Audio cores | LVGL runs in the Arduino loop task (prio 1); decode (core 1, prio 5) and the speaker writer (core 0) preempt it exactly as today. Display on HSPI, SD on VSPI: no shared bus. Do not call `lv_timer_handler` when `audio_bufferLow()` | `underruns` delta = 0 over a 60 s fling test |
| Indev rate | LVGL reads at `LV_DEF_REFR_PERIOD` = 33 ms by default = 30 Hz sampling: **must set `lv_indev_set_read_period(indev, 8..10)`** (S for default; setter name from memory, check) | velocity from >= 6 samples in 100 ms |
| SD from a list | LVGL gives no data virtualization: the app must update labels from a row cache; reads happen in `lv_timer`/loop, never in a draw callback | policy in section 7 |
| Fonts / CJK | existing text = Adafruit GFX FreeSans 9/12 pt bitmaps (`classic_text.h`) + pre-rendered UTF-8 strips for non-ASCII (`titles.cpp`). LVGL needs `lv_font_t`: convert with `lv_font_conv` (4 bpp, ASCII, about 5-10 KB per size, I) or adapt GFXFont glyphs; CJK strips need a custom draw hook / canvas | glyph parity on screenshots |
| Icons | CY-90 streamed cells -> A8 / I1 `lv_image` or draw-callback calling the existing code | icons render, recolour by theme |

### 6.3 Version
**LVGL 9.x, pinned (record tag + hash and keep the licence notice per AGENTS.md).** Reasons (S/I): 9.x has the built-in `lv_tft_espi` driver, partial render mode, `lv_display_*`/`lv_indev_*` API, `LV_STDLIB_CUSTOM`, and v8 is the legacy branch (the v8 ESP32 drivers repo targets the older API). Do not use 8.x.

### 6.4 Migration path (if S0 passes)
1. **Viewport display, not full screen** (I): `lv_display_create(listW, listH)` for the list rectangle only; `flush_cb` adds the viewport offset `(0, HeaderH)` to `setAddrWindow`. Header, footer, mini-player, strip and all non-list screens stay custom. LVGL owns the 4-6 list rows only: smallest buffer, no change to chrome, trivial coexistence (both use the same `tft`, one task).
2. Skeleton (I):
```cpp
static uint8_t buf[290*10*2] __attribute__((aligned(4)));            // 5.8 KB
static void flush_cb(lv_display_t* d,const lv_area_t* a,uint8_t* px){
  const int w=a->x2-a->x1+1,h=a->y2-a->y1+1; tft.startWrite();
  tft.setAddrWindow(a->x1+listX,a->y1+listY,w,h); tft.pushColors((uint16_t*)px,w*h,true); tft.endWrite();
  lv_display_flush_ready(d);}                                          // as lv_tft_espi.cpp
static void touch_cb(lv_indev_t*,lv_indev_data_t* d){ int x,y; bool p=touch_pressed(&x,&y); d->point.x=x-listX; d->point.y=y-listY; d->state=p?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED; }
// setup: lv_init(); lv_tick_set_cb(millis); disp=lv_display_create(..); lv_display_set_buffers(disp,buf,nullptr,sizeof buf,LV_DISPLAY_RENDER_MODE_PARTIAL);
//        indev=lv_indev_create(); lv_indev_set_type(indev,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(indev,touch_cb); lv_indev_set_read_period(indev,8);
// loop(): if(!audio_bufferLow()) lv_timer_handler();   // after the existing touch tracker, shared coordinates
```
3. Screen order: Songs/Artists/Albums/Playlists lists (one `lv_obj` with 8 recycled row children, labels updated from the window cache) -> Search results -> Settings lists -> Home tiles -> Now Playing last (or never; custom player chrome is not scroll-heavy).
4. Themes (CY-91): map the palette struct to a set of `lv_style_t` (row bg, selected bg, text, divider, scrollbar); on theme change call the setter and `lv_obj_report_style_change(NULL)`. No LVGL theme module needed.
5. Rewrite size (I): list screen logic is `classic_screen.cpp` 852 + `ui.cpp` 408 dense lines; the list part (paintRow/loadRow/invalidate/gesture/strip, about 300 lines) would be replaced by about 250 lines of LVGL glue; chrome unchanged. Whole-UI rewrite: not recommended.

### 6.5 Scroll parameters inside LVGL (Android-like)
- Remove `LV_OBJ_FLAG_SCROLL_MOMENTUM` and `LV_OBJ_FLAG_SCROLL_ELASTIC`, then drive the list with the AOSP core (section 5) through `lv_obj_scroll_to_y(list, y, LV_ANIM_OFF)` each frame (I; the core stays host-tested and exact). This is the only way to get v^1.74 fling distance (calc above) and the edge spring.
- If the owner insists on pure LVGL: `scroll_limit 7`, `scroll_throw 6` (best compromise between 1000 and 2000 px/s in the calc table, undershoots flicks), `lv_indev_set_read_period 8`, elastic on, snap off, `LV_DEF_REFR_PERIOD` 33 -> 25. Expect a "close at moderate speed, weak on flicks" feel (calc). Tune with the same five knobs through `lv_indev_set_scroll_throw` at runtime.

### 6.6 Decision rule
Run **S0** (below). If heap and speed gates pass, adopt per 6.4 and still use the AOSP core inside (C1-C2 are shared). If a gate fails by measurement, continue with section 5 alone (the custom viewport renderer), which needs no new RAM.
- **S0 spike (one chunk, about 20 min, own worktree, device-free)**: add LVGL 9.x pinned under `.deps` with a minimal `lv_conf.h` (`LV_STDLIB_CUSTOM`, only the widgets used, no demos, no extra fonts), a hidden compile flag building one list screen with fake rows; report **compile-only** flash/static-RAM delta for both boards and the predicted heap (pool + buffer). A second device chunk (separate, authorised) measures free heap, largest block, frame ms and underruns during a fling while playing.

## 7. Data loading while scrolling (the SD problem)
### 7.1 Real cost (S, code)
- `loadRow` -> `storage_browseEntry`/`storage_trackSummary` (`classic_screen.cpp:281-300`) take the storage `mutex` and `recordLocked` reads **one 712 B `Record`** (`library_core.h:50`) from the catalogue through a **512 B stdio buffer** (`kCatBuffer`, `storage.cpp:48`) into a one-entry cache (`cacheIndex`); one retry reopens the file. That is 2 SD sector reads plus a seek (I: about 1-4 ms typical at 20 MHz SPI, worst case tens of ms from SD housekeeping, U). `headLocked` can read only the head bytes.
- UI: `cache[6]` rows of about 230 B (label 128 + detail 96 + fields); `invalidate()` clears all; `tick` loads one row per pass if `!audio_bufferLow()` or 120 ms elapsed (`classic_screen.cpp:556-557`).
- **Shared-bus risk** (I): the decoder reads the track file on the same VSPI SD host; FATFS is serialised, so a long UI read can delay a decoder read. The only cushion is the PCM/DAC buffering (speaker DMA about 35 ms, `speaker.cpp:37`). Hence the existing `audio_bufferLow()` gate must stay, and UI SD reads must be single, short and never during a gesture frame.

### 7.2 Policy (I; combines the usual patterns: placeholders, velocity-gated loading, fixed-window cache)
1. **Row window cache**: a ring of 12-16 `Row` entries keyed by absolute row index (3-4 KB, replaces `cache[6]`; shrink `label`/`detail` to what is displayed, 64+48, to hold about 1.8 KB). Visible rows plus 2 behind and 4 ahead in the scroll direction.
2. **Velocity gate**: while `|v| > 3 rows/s` (about 120 px/s) or the finger is dragging faster than that, **no SD reads**; unloaded rows draw as placeholder bars (flat rounded bar at title position, row divider). Loads restart when `|v|` falls below the gate.
3. **Landing prefetch**: at fling start `finalY` is known in closed form (`OverScroller.getFinalY`, S). Prefetch the rows around the landing position (nearest-first) as soon as speed drops below the gate, in direction order; rows between are never loaded.
4. **Budget and cancel**: at most one row per loop pass, only when `!audio_bufferLow()`; a pending load is dropped if its row left the window (loads are synchronous and single, so cancellation = not issuing). Stop all loading while the finger is down and moving.
5. **Sequential reads**: when 2+ adjacent rows are missing, read their records in one `readAt` (2 x 712 B) to save the seek (Songs view is sequential in catalogue order: I).
6. Hit-testing and tap use the cached row only; a tap on a placeholder row loads it synchronously (one row, bounded).

## 8. Large lists (thousands of tracks) (I unless noted)
Music players solve it with (a) accelerated scrolling by speed (the fling above covers 160 rows at max velocity: a 2,000-track list takes about 12 max flings), (b) a fast-scroll handle (Android `FastScroller`/`SectionIndexer`, iOS A-Z index; not fetched), (c) iPod click-wheel acceleration (Rockbox has button-repeat acceleration in `action.c`, S: summary of the Rockbox search). Offer:
1. **Draggable scrollbar thumb** in the existing 30 px strip (touch target 30 px wide): drag maps y to index linearly, with the current first letter as a bubble near the thumb; no SD reads except the final row window on release (velocity-gated policy applies).
2. **A-Z jump**: tap the strip top zone cycles letters or opens a vertical letter strip; needs the first index per letter, computed once from the sorted catalogue at build time (CY-83 "alpha" work: check what it provides) or by binary search (11 record reads for 2,000 tracks, about 30-50 ms, calc).
3. Long-press the Back/header title (or strip arrows): jump to top/bottom.
Keep the paged strip as an option (Q3).

## 9. Open questions for the owner (max 5)
1. **Primary orientation**: portrait (6 rows, enables hardware scroll) or landscape (4 rows, default `CYD_ROTATION 1`)?
2. **LVGL go/no-go thresholds**: accept "adds <= 30 KB heap and still >= 18 KB free while playing + BT"? If S0 shows less headroom, which feature may give up heap (e.g. search build buffers) or is the answer then the custom renderer?
3. Keep the paged strip, replace it by a draggable thumb + letter bubble, or both?
4. Edge style: Android 12 stretch cannot be reproduced; accept "tiny translate + spring + thumb flash" (or the pre-12 glow, which needs blending and is also not available)?
5. Which tuned values to persist in Settings (friction, gain, snap) after the on-device pass, and should snap-to-row at rest exist (stock Android lists do not snap)?

## Sources
- AOSP: [OverScroller.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/widget/OverScroller.java), [ViewConfiguration.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/view/ViewConfiguration.java), [VelocityTracker.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/view/VelocityTracker.java), [VelocityTracker.cpp](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/libs/input/VelocityTracker.cpp), [VelocityTracker.h](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/include/input/VelocityTracker.h), [EdgeEffect.java](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/widget/EdgeEffect.java), [RecyclerView.java](https://github.com/androidx/androidx/blob/androidx-main/recyclerview/recyclerview/src/main/java/androidx/recyclerview/widget/RecyclerView.java) (only constants/interpolator read; fetch tool returned the first 100 KB).
- Flutter: [scroll_physics.dart](https://github.com/flutter/flutter/blob/master/packages/flutter/lib/src/widgets/scroll_physics.dart), [scroll_simulation.dart](https://github.com/flutter/flutter/blob/master/packages/flutter/lib/src/widgets/scroll_simulation.dart), [friction_simulation.dart](https://github.com/flutter/flutter/blob/master/packages/flutter/lib/src/physics/friction_simulation.dart).
- LVGL: [lv_indev_scroll.c](https://github.com/lvgl/lvgl/blob/master/src/indev/lv_indev_scroll.c), [lv_indev.c](https://github.com/lvgl/lvgl/blob/master/src/indev/lv_indev.c), [lv_obj_scroll.c](https://github.com/lvgl/lvgl/blob/master/src/core/lv_obj_scroll.c), [lv_conf_template.h](https://github.com/lvgl/lvgl/blob/master/lv_conf_template.h), [README](https://github.com/lvgl/lvgl/blob/master/README.md), [lv_tft_espi.cpp](https://github.com/lvgl/lvgl/blob/master/src/drivers/display/tft_espi/lv_tft_espi.cpp), [LVGL on ESP32 blog](https://blog.lvgl.io/2019-01-31/esp32), [LVGL ESP32 tutorial](https://lvgl.io/blog/tutorial-esp32-getting-started), [forum: render time](https://forum.lvgl.io/t/render-time-optimization/23291).
- Other: [Rockbox kinetic list commit](https://git.lenord.me/devs/foxbox/commit/b673ae2c46129d6994b61a351a1567c56f4aef82) (search summary only), [ESPHome ST7789V header](https://esphome.io/api/st7789v_8h_source.html), [ST7789V datasheet](https://cdn.shopify.com/s/files/1/0264/7629/files/ST7789V_V1.4.pdf) (not read), [Apple decelerationRate](https://developer.apple.com/documentation/uikit/uiscrollview/decelerationrate) (page content not retrievable; 0.998/0.99 taken from the Flutter source comment).
- Repo (S): `classic_layout.h`, `classic_screen.cpp:36-45,240-300,556-557`, `storage.cpp:48,314-331`, `library_core.h:36-50`, `board.h`, `docs/research/{cy94-touch-scroll,cy11-heap-budget,cy43-heap-implementation,cy85-text-quality,classic-touch-ui-planning-validation}.md`.
