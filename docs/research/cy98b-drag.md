# CY-98b drag and scrollbar

Tracker supplies the 7 px slop-subtracted pixel delta. Moves coalesce into one cache-only viewport paint per tick; a claimed drag cannot select. Hard clamps, no fling. Search and UTF-8 SD title loading pause for the press; release repaints native titles and resumes the existing one-row loader. The paged arrows and their actions are removed. The 30 px touch rail drags a proportional thumb, visible during a drag and for 550 ms after release. Row capacity remains derived from orientation.

Paging helpers, mini-player cases, navigation graph actions and the Classic layout golden now use dragging. The dialog golden is unchanged. Production PNGs: `cy98b-drag-{portrait,landscape}.png`, both at offset 25 px (4/6 rows).

## Evidence

Pinned 8924c3b archive, full WSL geometry gate: `FAIL firmware UI: Confirmation dialog geometry differs` (exit 1; `.tools/logs/cy98-baseline-ui.log`). This predates CY-98a. Focused mode retains the migrated Classic layout comparison and excludes that dialog gate.

Final checks pending; logs `.tools/logs/cy98b-{ui,default,jc2432w328c}.log`.

## Owner feel-test

Slow 1:1 tracking across the slop threshold, reversal, both bounds, return-to-origin release without activation, taps on partial rows, thumb grab/travel/timeout, Settings bottom rows, mini-player return. Inspect UTF-8 titles after release and placeholders while dragging; judge CST820 latency/tearing and audio underruns during playback. Hardware, actual AirPods and wired output are untested here. No device operations, push or settings/core edits.
