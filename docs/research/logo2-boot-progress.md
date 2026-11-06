# LOGO-2 boot progress

Code: `96547f7`, branch `cydpod/boot-progress`, base `2f49747` (v34). No push or device operations.

- Yellow wordmark/version stay fixed. Immediately after the splash: `Starting...`, zero bar. Library counts become percentages; phase and title preparation use ink font 2 below a 6 px deep-yellow/ink bar. Layout follows panel dimensions in rotations 0-3. Home retains its existing full theme fill.
- Updates are capped at 125 ms; only bar/text rectangles change. Text redraws only on phase/warning changes. Unknown work moves a block; a percentage without a fresh callback for 250 ms also moves, so merge sorting stays alive through `breathe()`.
- Warnings use ink, an underline and a small instruction line on yellow. No indicator during warning/retry. Calibration and uploader rendering routines are unchanged; the player restores the splash after calibration.
- Added wait is `max(0, 600 - elapsedSinceSplash)` ms (0..600 ms; scheduler rounding possible). Slow boots add 0 ms. No heap/task/timer added for progress.
- Coordinator-authorized `storage.cpp` edit: lines 33-34 only, optional `storage_bootTick` pointer plus its invocation in `breathe()` before the existing delay. Null preserves prior behaviour; StorageProgressFn, sort and validation logic unchanged. Hook is cleared before home.

Checks (once on final code commit; logs `.tools/logs/logo2-*`):
- WSL `sh tests/build_splash.sh`: `PASS splash_test`; `PASS boot_progress: rotations 0-3, 0/37/100% fill +/-1px, fixed wordmark, outside changes=0, animation/throttle/warnings`.
- WSL `catalog_test`: `PASS catalogue`; `build_catalog_sort.sh`: `ALL PASS`; `build_catalog_validation.sh`: `PASS validation writer` (also corruption/byte-count PASS). All exit 0; command script retained in `.tools/logs/logo2-host.sh`.
- `tools/check.ps1 -SkipProbe -SkipHostMedia`: exit 0, `PASS: compile-only checks completed; no device commands were run.`
- Same flags plus `-Board jc2432w328c`: exit 0, `PASS: compile-only checks completed; no device commands were run.` Both preserve `PASS single pool` (40,960 B).

Flash/static RAM: default player 1,525,665 -> 1,525,745 (+80) / 93,464 -> 93,568 (+104) B. W328C normal player 1,566,717 -> 1,566,817 (+100) / 95,056 -> 95,160 (+104) B. Baselines: retained INT-6 compile logs, matching flags; the published v34 report instead lists 1,566,861 B flash. No baseline rebuild.

Owner strips: [portrait](logo2-boot-portrait.png), [landscape](logo2-boot-landscape.png), each 0%, 37%, 100%, two moving-block frames, then no-card/read-only/stale warnings. Expect the logo to remain throughout library work, then disappear fully into home. Host frames include the existing TFT fonts. Device redraw latency (~5 ms target), wall-clock boot duration, physical rotations and flicker are not measured; no hardware acceptance claimed.
