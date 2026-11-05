# CY-108: flick estimator + Coast wired into the list (v30)
Branch `cydpod/flick-wire` on c306436 (CY-107). No device operations; no hardware claim. `scroll_core.h` untouched (no bugs found).

## Changes
- Touch: `PressTracker::raw()` keeps the unfiltered sample; `touch_rawPosition()` in both drivers (poll-time stamp = `touch_lastSeenMs()`).
- `classic_screen.cpp`: `Tracker` gets raw+filtered y (PRESS_MOVE and every tick, so a changed raw y is never lost); claim and velocity use raw, painting the filtered. `Fling` replaced by `Coast`; the estimator judges the release at `lastSeen` with `restNowMs` = lift-detect time minus the 60 ms grace, the coast animates forward from PRESS_UP (no backdating, no jump). Any admitted down stops it and becomes a drag; hard clamp at the ends; no new SD reads (CY-98a policy kept).
- Defaults (screen only; core default stays 4 s): `tauFast 18 s`. Rows (s) by release px/s, cap 4800:

| tauFast | 300 | 600 | 1200 | 2400 | 3600 | 4800 |
|---|---|---|---|---|---|---|
| 4 | 2 (0.6) | 5 (0.9) | 24 (1.8) | 111 (3.7) | 231 (5.3) | 351 (6.4) |
| 12 | 2 | 7 | 56 (3.2) | 311 (8.8) | 671 (13.7) | 1031 (17.1) |
| 18 | 2 (0.6) | 9 (1.2) | 80 (4.3) | 461 (12.7) | 1001 (20) | 1541 (25.1) |
  12 s gives only ~1,030 rows at the cap, so 18 s. Speeds below 400 px/s use `tauSlow` (unchanged: 100 px/s settles in 0.29 s).
- `scroll [gap|gain|tau|tauf|cap|minv|mintravel] <v>` (`scroll_cmd.h`, RAM only, ranges checked, bare = print values). The player had no serial command reader: `app.ino` got a 48-byte non-blocking line reader. Changes reach the tracker/coast between gestures.
- `stat: scroll` is now one line per list release (replaces the periodic line), printed when the coast ends or is stopped by a down (or at once with no coast), only while idle: id, rawDy, filtDy, pollGapMax, changedGapMax, resets, n, spanMs, vEst, reject, stillMs, coastRows, remRows, aborted, plus the old frames/underruns/release fields. A ~330-char line blocks serial ~25 ms at 115200.
- Tests: reversal in the old CY-99 case lengthened 10→20 px (CY-107: turn-back <12 px does not fling); flings counter +7→+5 (a release with no room starts no coast, `reject edge`). `tools/check.ps1`: v30 tag.

## Size
Static RAM W328C 93464→93808 (+344 B), default 91872→92216; app.bin 1538736→1540544 (+1808 B). Codec pool 40960 B unchanged (`PASS single pool`).

## Checks (final tip, quoted)
- `sh .tools/cy108-ui.sh` (VIEWPORT_ONLY), exit 0: `PASS CY-103 late release…`, `PASS CY-98a viewport…`, `PASS CY-98b drag…`, `PASS CY-99 fling…`, `PASS CY-108 flick wiring: raw vs filtered routing, coast start/stop/no backdating, tap-to-stop, end clamp, per-release line, knob parsing`.
- `scroll_core_test`: `PASS all`. `cy105_flick_sim`: `PASS CY-107 replay: matrix, rest, direction, monotone distance, 20/30 fps`.
- `check.ps1 -SkipProbe -SkipHostMedia`, default and `-Board jc2432w328c`: both exit 0 `PASS: compile-only checks completed`. `PASS V30_EXPORT`.
- Approximations: `reject` and `resets` mirror the estimator from outside (reversals judged per sample); `stillMs` uses the same rest rule.

## Image `.tools/images/codec-pool-v30-jc2432w328c` (worktree cy108-wire)
```
0x10000 app.ino.bin 1540544 aa384b644ad7ba30fbaad823b986cf9f405dedd22f8289bf9bfcea7cd4371550
0x1000  bootloader  25024   f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29
0x8000  partitions  3072    0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7
0xe000  boot_app0   8192    f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0
```
Owner only (identify COM7; BOOT held, tap RESET, release BOOT):
```powershell
$s='E:\dev\.worktrees\CYDpod\cy108-wire\.tools\images\codec-pool-v30-jc2432w328c'
New-Item -ItemType Directory -Force E:\flash\v30 | Out-Null
foreach($f in 'app.ino.bootloader.bin','app.ino.partitions.bin','boot_app0.bin','app.ino.bin','MANIFEST.json'){Copy-Item "$s\$f" E:\flash\v30 -Force}
Copy-Item E:\dev\CYDpod\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe E:\flash\v30 -Force
cd E:\flash\v30
foreach($op in 'write-flash','verify-flash'){ .\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin }
```
## 10-flick test
Open a long Songs list, serial monitor on COM7. Do 10 flicks (3 slow, 4 medium, 3 hard), numbering them; after each wait for its `stat: scroll` line (or touch to stop). Note per flick: did it coast, how far, any miss.
Read: `reject none` = coasted; `coastRows` planned, `remRows` left when stopped (`aborted 1`). A miss shows `reject rest|stale|travel|slow|few|edge` with `stillMs`, `changedGapMax`/`pollGapMax` (>100 = sparse polls), `resets` (gap restarts/reversals), `rawDy` vs `filtDy`, `vEst` px/s. Tune live: `scroll`, `scroll tauf 12000`, `scroll minv 60`, `scroll gain 1.3`.
