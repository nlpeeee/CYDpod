# CY-96: scroll_core.h (CY-95 chunk C1)

`firmware/cydpod_player/app/scroll_core.h` (178 lines, header-only, float+int32, no heap): `ScrollCfg`, `Fling`, `Edge`.

## Constants (ScrollCfg defaults, 143 ppi)
friction 0.015, ppi 143, minFling 45, maxFling 7150 px/s, overflingPx 5, gravity 2000 px/s^2 (BALLISTIC/CUBIC),
spring stiffness 24.657 / damping 0.98, edgeRangePx 60 (my choice, dampen asymptote), settlePx 0.5.
Fixed in code: DECEL 2.3582, INFLEXION 0.35, P1 0.175, P2 0.35. physicalCoeff = 9.80665*39.37*ppi*0.84.

## Design
- Closed form in wall-clock ms: position/velocity/finished are pure functions of `now`. The AOSP 101-entry table is replaced by a 24-step bisection of the Bezier (no table RAM).
- Hitting a bound: spline cut at the crossing time, BALLISTIC to min(v^2/2g, overfling), then CUBIC springback `over*(1-(3u^2-2u^3))`, duration `sqrt(2*over/g)`. Start outside bounds = springback only. `flywheel()` adds same-sign velocity.
- The ballistic/cubic/flywheel details follow the report summary of OverScroller (not a fresh re-read of AOSP source); `Edge::dampen` (rubber band) is my formula, the spring is a closed-form underdamped oscillator (unit mass).

## Goldens and tolerances
`tests/data/aosp_fling.csv` (818 rows, v = 100..7000 px/s, 8 ms steps), made by `tools/gen_aosp_fling.py` (independent reproduction of AOSP table + linear interpolation, **not** captured from a real Android device, so the 5.3 "authoritative" goldens are still open).
Tolerance: position < max(1 px, 0.5% of total); velocity 2% of v + 5 px/s; duration within 33 ms; final within max(1 px, 0.5%). Measured worst: 0.536 px, 0.00 ms, 0.007 px.

## Test
`wsl.exe -d Debian -- sh -c "cd /mnt/e/dev/.worktrees/CYDpod/cy96-scroll-core; g++ -std=c++17 -O2 -Wall -Wextra -Ifirmware/cydpod_player/app -o /tmp/scroll_core_test tests/scroll_core_test.cpp; /tmp/scroll_core_test"`
(also registered in `tests/build_catalog.cmd` for MSVC; not run there).
