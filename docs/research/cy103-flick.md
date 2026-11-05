# CY-103 flick / v28

Code: b36e484 + 9050c07.
Cause: `touch_gesture.h:41,75` delays release >=60 ms; old `classic_screen.cpp:889` used loop-pass `inputNow()` (:82, millis), but `scroll_core.h:286` rejects >40 ms since changed-y. Real PressTracker -> ui_touch/syncPress -> PRESS_UP repro: `period=10 delay=60 rested=0 fling=0`; `FAIL firmware UI: CY-103 late release must fling unless finger really rested` (exit 1, cy103-before.log).

Fix: `touch_lastSeenMs()` in both drivers supplies Classic down/move/up (:829,865,889); true unchanged-y rest >40 ms still suppresses fling. Threshold 45 px/s and re-touch debounce 120 ms unchanged; actual CST820 report rate unmeasured. `stat: scroll`: releaseV (px/s), releaseGapMs (delivery lag), releasePauseMs (true stationary time), releaseSamples (100 ms window); Tracker API/physics unchanged.

Smoke logs: `.tools/logs/cy103-{ui,core,default,jc2432w328c,export}.log`.
- WSL `sh .tools/cy98-ui.sh`, exit 0: `PASS CY-103 late release: 60/100 Hz, 60/90 ms grace, true rest suppresses fling`; CY-98/99 PASS; pre-existing dialog gate excluded.
- WSL g++ tests/scroll_core_test.cpp (-std=c++17 -O2 -Wall -Wextra -Ifirmware/cydpod_player/app), run, exit 0: `PASS all`.
- `tools/check.ps1 -SkipProbe -SkipHostMedia`, then `-Board jc2432w328c`: both exit 0: `PASS: compile-only checks completed; no device commands were run.`
- `.tools/cy103-export.ps1`, exit 0: `PASS V28_EXPORT: all manifest bytes/SHA256 and four flash offsets verified`.
Initial W328C link failure fixed (driver accessor); only affected W328C rerun. UI/core/default: b36e484; W328C: 9050c07.
Flash/static bytes: default 1497397/91872; W328C 1538593/93464. RAM +16 B; `PASS single pool`: 40960 B unchanged.

Image: `.tools/images/codec-pool-v28-jc2432w328c/`; app file 1538736 B.
- app.ino.bin / 0x10000 / `c6a8bdbb9b4476ffa5f831339d460f4743aa00bd28a6d9d18c1d11da3f644490`
- app.ino.bootloader.bin / 0x1000 / `f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29`
- app.ino.partitions.bin / 0x8000 / `0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7`
- boot_app0.bin / 0xe000 / `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0`

Owner only; identify W328C as COM7; BOOT held, tap RESET, release BOOT; retain backup/rollback:
```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy98-viewport
$img = '.tools/images/codec-pool-v28-jc2432w328c'
$tool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
foreach ($op in @('write-flash','verify-flash')) {
  & $tool --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
}
```
Press RESET. Retest flick distance, rest-then-lift, drag, tap-to-stop/drag, both edges, placeholders and audio underruns; collect stat: scroll on misses. No hardware success claimed.
