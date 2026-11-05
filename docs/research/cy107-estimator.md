# CY-107: flick estimator + long coast (host only)
Base 9ca4e92, `cydpod/flick-estimator`. Host only; no firmware wiring, no hardware claim. `tests/cy105_flick_sim.cpp` is now the CY-107 replay (CY-105 original: 28bc23d).

## Constants (`ScrollCfg`)
Estimator: `gapMs 100`, `tauSmooth 0.050`, `aggMs 20`, `minTravelPx 12`, `minVelPxS 80`, `reverseMinPx 2`, `restMs 100`, `restMovePx 2`. Coast: `rowPx 40`, `capRowsPerSec 120`, `slowRows 10`, `fastRows 60`, `tauSlow 0.32 s`, `tauFast 4 s`, `stopPxS 40`, `minAnimMs 33`.
- `FlickEstimator`: raw y, `v += (1-exp(-dt/tau))(s-v)`, reset on reversal (>=2 px against v) or gap >100 ms, stale/duplicate timestamps ignored, dt<20 ms aggregated. Fling needs net travel since reset >=12 px, |v|>=80.
- Rest is separate: timer restarts only on >2 px from the last reference, so 1 px jitter never restarts it. `release()` refuses when rest >100 ms or the newest sample is >100 ms old. **Known ambiguity:** a rest of ~80-100 ms is indistinguishable from a sparse sample gap and may still coast; with 92-100 ms polls only ~68 ms of a 200 ms rest is seen, so `up()` takes an optional `restNowMs` (wiring: lift-detect poll time minus the 60 ms grace).
- `Coast` (closed form, no integrator): tau rises linearly in speed between 400 and 2400 px/s; time/distance per region are logs and polynomials, speed at time t by 24-step bisection (needs no state). Starts at release time (no backdating), hard clamp at list ends, ends at 40 px/s, never finished sooner than 33 ms.
- Tracker: `move(ms, filteredY, rawY)`; claim and velocity use raw, `delta()` uses filtered with no jump at the claim; `move(ms,y)` = both equal. `up()` takes `Fling&` (firmware call shapes unchanged) or `Coast&`; `Strategy` is ignored.
- Coast at defaults: 300 px/s 2.1 rows 0.64 s; 1200 px/s 24 rows 1.75 s; cap 4800 px/s 351 rows 6.4 s.

## Replay matrix (fling count/8, median coast rows; 8 phases x poll jitter +-3 ms)
Columns are Hz+stall ms (stall added to every second interval). Strokes last max(120 ms, 20 px)+0..28 ms.
| px/s | 15+0..30 | 25+0..30 | 30+0..30 | 60+0..30 |
|---|---|---|---|---|
| 50 | 0/8 all | 0/8 all | 0/8 all | 0/8 all |
| 100 | 8/8, 0.5 | 8/8, 0.5 | 8/8, 0.5 | 8/8, 0.5 |
| 300 | 8/8, 2.1 | 8/8, 2.1 | 8/8, 2.1 | 8/8, 2.1 |
| 800 | 8/8, 9.7-9.9 | 8/8, 9.7-9.8 | 8/8, 9.7-9.8 | 8/8, 9.7-9.8 |
| 2000 | 8/8, 74.6 | 8/8, 74.6 | 8/8, 74.6 | 8/8, 74.6 |
(All 12 columns: `.tools/logs/cy107-sim.log`, uncommitted.) Spread across rates: 0.06/0.05/0.17/0.00 rows. 200 ms rest: 0/288 flings. Reversal: long turn-back flings only in the new direction, 8 px turn-back does not. Coast positions at 20 fps vs 30 fps instants: <=1 px (pure function of time).
**Stroke length deviation:** CY-105 strokes (120 ms) are 12 px at 100 px/s; the last poll sees 11 px, under the 12 px minimum, so 100 px/s cannot fling with those. Matrix strokes are therefore at least 20 px. A 100 px/s stroke shorter than ~13 px will not fling.

## Checks (once, final tip, WSL g++ 14 -std=c++17 -O2 -Wall -Wextra)
- `PASS all` (tests/scroll_core_test.cpp, incl. `PASS estimator and coast`), no warnings.
- `PASS CY-107 replay: matrix, rest, direction, monotone distance, 20/30 fps` (tests/cy105_flick_sim.cpp).
Not run: firmware builds, `classic_ui_test` (`classic_screen.cpp` call shapes compile-checked with a stub).

## Changed existing tests
1. Synthetic-drag test: `Velocity` fed directly instead of `Tracker` (Tracker no longer uses LSQ2/Impulse).
2. "up 41 ms after the last sample: no fling" now expects a fling (gap allowance 100 ms); added "101 ms rest: no fling".
3. Tracker test title adds "late up". Everything else unchanged; the 39 ms and hold-then-lift cases still pass as written.

## Open questions
- "Almost infinite": with the default tau 4 s the cap throw covers ~351 rows (6.4 s). Larger `tauFast`/`capRowsPerSec` for more; owner decision.
- Firmware wiring: pass raw and filtered y, the `restNowMs` clock and use `Coast` in `classic_screen.cpp`; CY-105's `stat: scroll` still gives real incidence.
- Piecewise-linear tau is C0 not smooth at 10/60 rows/s; smoothstep tau also has a closed form.
