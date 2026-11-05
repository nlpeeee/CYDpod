# CY-97: Velocity + Tracker in scroll_core.h (CY-95 chunk C2)

`scroll_core.h` (305 lines): `Velocity` (ring of 20 `(ms,y)`, horizon 100 ms, gap >40 ms clears, `get()` clamped to +-maxFling), `Strategy {Lsq2, Impulse}`, `Tracker` (`down/move/up`), `Fling::stop()`. `ScrollCfg` gained `slopPx 7`, `horizonMs 100`, `stopMs 40`.

## Default strategy: LSQ2
Synthetic drags (integer px, 60/100 Hz, const/accel/decel/+-1 px jitter, 11 cases; log in the test output):
worst relative error LSQ2 **3.0%**, Impulse **11.5%** (jitter cases: Impulse +110 px/s on 1500, +92 on -800 at 100 Hz; LSQ2 +27 / +24). Impulse is slightly better only on noise-free accel at 100 Hz. LSQ2 is the `Tracker` default (`setStrategy` switches; the debug `scroll strat` knob can call it).
Tolerances in the test: LSQ2 8% + 25 px/s, Impulse 15% + 25 px/s.

## Tracker behaviour
- Claim when `|y - downY| > slopPx`; `delta()` = finger y minus (downY + sign*slop), so 0 at the claim, 1:1 after.
- Samples enter the ring only when y changed. `up(ms, Fling&, y0, minY, maxY, vScale=-1)` flings if claimed, `|v| > minFling` and the last changed sample is <= 40 ms old (hold-then-lift: none).
- `down(ms, y, &running)` on an unfinished Fling calls `stop()` (freezes at the current position) and sets `stoppedFling()`; the touch is claimed at once with no slop; `isTap()` is false.
- Not yet verified against real CST820 data (report rate, jitter): the host tests are synthetic.

## Command
`wsl.exe -d Debian -- sh -c "cd /mnt/e/dev/.worktrees/CYDpod/cy97-tracker; g++ -std=c++17 -O2 -Wall -Wextra -Ifirmware/cydpod_player/app -o /tmp/scroll_core_test tests/scroll_core_test.cpp; /tmp/scroll_core_test"` -> `PASS all`.
