# CY-108: estimator + Coast wired into the list (v30)
`cydpod/flick-wire` on c306436. No device ops/hardware claim. `scroll_core.h` untouched.

## Changes
- Raw sample: `PressTracker::raw()`, `touch_rawPosition()`. List `Tracker` gets raw+filtered y (move and tick); claim/velocity raw, paint filtered. Release judged at `lastSeen` + `restNowMs`; `Coast` replaces `Fling`: forward from PRESS_UP, end clamp, any down stops it and drags, no new SD reads.
- `tauFast` 18 s (screen default). Rows by release px/s (600/1200/4800): 4 s 5/24/351; 12 s 7/56/1031; 18 s 9/80/1541 (25 s). 12 s gives only ~1,030. Slow flicks keep `tauSlow` (0.3 s).
- `scroll [gap|gain|tau|tauf|cap|minv|mintravel] <v>` (`scroll_cmd.h`, RAM only, bare prints). No command reader existed: `app.ino` got a 48 B line reader.
- `stat: scroll`: one line per list release, printed idle after the coast ends/stops (~25 ms serial block). `reject`/`resets` approximate the estimator.
- Test edits: CY-99 reversal 10 to 20 px (CY-107 rule); flings +7 to +5.

## Size
W328C RAM +344 B (93808); app.bin +1808 B (1540544); pool 40960 B.

## Checks (final tip)
- UI host, exit 0: `PASS CY-103 late release`, `PASS CY-98a viewport`, `PASS CY-98b drag`, `PASS CY-99 fling`, `PASS CY-108 flick wiring: raw vs filtered routing, coast start/stop/no backdating, tap-to-stop, end clamp, per-release line, knob parsing`.
- `PASS all` (scroll_core); `PASS CY-107 replay: matrix, rest, direction, monotone distance, 20/30 fps`.
- check.ps1 default + W328C: `PASS: compile-only checks completed`; `PASS V30_EXPORT`.

## Image `.tools/images/codec-pool-v30-jc2432w328c`
```
0x10000 app.ino.bin 1540544 aa384b644ad7ba30fbaad823b986cf9f405dedd22f8289bf9bfcea7cd4371550
0x1000  bootloader  25024   f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29
0x8000  partitions  3072    0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7
0xe000  boot_app0   8192    f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0
```
Owner only (confirm COM7; BOOT+RESET):
```powershell
$s='E:\dev\.worktrees\CYDpod\cy108-wire\.tools\images\codec-pool-v30-jc2432w328c'
New-Item -ItemType Directory -Force E:\flash\v30|Out-Null
'app.ino.bootloader.bin','app.ino.partitions.bin','boot_app0.bin','app.ino.bin','MANIFEST.json'|%{Copy-Item "$s\$_" E:\flash\v30 -Force}
Copy-Item E:\dev\CYDpod\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe E:\flash\v30 -Force
cd E:\flash\v30
foreach($op in 'write-flash','verify-flash'){.\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin}
```
## 10-flick test
Long Songs list, monitor COM7. 10 numbered flicks (3 slow, 4 medium, 3 hard); await each `stat: scroll` line. `reject none` = coasted; `coastRows` planned, `remRows` left if stopped (`aborted 1`). A miss shows `reject rest|stale|travel|slow|few|edge`, `stillMs`, `pollGapMax`/`changedGapMax` (>100 = sparse), `resets`, `rawDy`/`filtDy`, `vEst`. Tune: `scroll`, `scroll tauf 12000`, `scroll minv 60`.
