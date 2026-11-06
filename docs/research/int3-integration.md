# INT-3 integration v32
Source: `9750ee7`; final commit changes only this report. Branch `cydpod/integration-v2`, worktree `E:\dev\.worktrees\CYDpod\int3`. No push/device operations.

## Merges / conflicts
Base preview `6055cff` (CY-115/A1/A2); no-ff merges in order:
order `03123d4` -> `91f3e6f`; boot `32a492f` -> `9d11861`; BT `a3a92b5` -> `8009be7`; upload `bc63752` -> `c70718b`; host `a9971d6` -> `9750ee7`.
Only conflict: `tools/check.ps1`; kept F2 aggregation/WSL/media repairs and BT/sort registration, added queue/order/validation, removed duplicate runs, bumped export to v32. `storage.cpp` merged cleanly, retaining bounded sort and sampled validation/legacy migration. `build_catalog.cmd` retained F2 repairs. Copied .deps/.tools; private config/cache paths rewritten. Orca checkpoint: `selector_not_found`.

## Checks (once each)
`powershell -NoProfile -File tools/check.ps1` plus:
- `-SkipProbe`: exit 1, `Host checks failed: catalogue/navigation.`
- `-SkipProbe -SkipHostMedia`: exit 0.
- `-Board jc2432w328c -SkipProbe -SkipHostMedia`: exit 0.
Both compile-only logs end `PASS: compile-only checks completed; no device commands were run.`
Logs: `.tools/logs/int3-{full-host,default-compile,w328c-compile}.log`.

19 host entrypoints: 15 PASS, 4 FAIL; no fixes/rebaseline.
| Suites | Result / first failure |
|---|---|
| pairing, speaker, touch, scroll, CST820 | PASS (5) |
| catalog | FAIL catalogue: heap floor refuses staging, reported as memory (CT-08) **additional** |
| classic_ui, classic_ui wired | FAIL firmware UI: select track (both known) |
| icon verify | FileNotFoundError: icon-preview\\catalogue-12.png (known) |
| codec pool, I2S tone, wired fade, I2S diag | PASS (4) |
| BT policy, sort, queue/preview, audio order, validation | PASS (5); includes N=10,000 |
| media_check.py | PASS: 3 positive, 7 negative |
Optional real samples SKIP (absent); validation reads 4,160 B/slot at N=10,000.

## RAM / flash / normal image
W328C v31 -> v32: static RAM 93,832 -> 94,960 B (+1,128); sketch flash 1,542,209 -> 1,548,213 B (+6,004); app binary 1,542,352 -> 1,548,368 B (+6,016). v31 comparator: bt-grab's `codec-pool-v31-jc2432w328c.log`/manifest. Single codec pool PASS: 40,960 B.
Queue+preview+order: 528+528+28 = 1,084 B fixed resident state; expected per-track resident-heap growth **zero**, replacing up to 60,000 B of old ID arrays at 10,000 tracks. Hardware heap not measured.

Export `.tools/images/codec-pool-v32-jc2432w328c`; SHA256:
| Region | Offset | Bytes | Hash |
|---|---|---:|---|
| app.ino.bin | 0x10000 | 1548368 | 35f1421e2ebdcd0d87faa9be81ebc7d81192eae0b16cc81f6e67bb4a03e7d752 |
| app.ino.bootloader.bin | 0x1000 | 25024 | f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29 |
| app.ino.partitions.bin | 0x8000 | 3072 | 0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7 |
| boot_app0.bin | 0xe000 | 8192 | f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0 |

Owner commands (confirm W328C port; COM7 as v31 recipe):
```powershell
$s='E:\dev\.worktrees\CYDpod\int3\.tools\images\codec-pool-v32-jc2432w328c'
New-Item -ItemType Directory -Force E:\flash\v32|Out-Null
'app.ino.bootloader.bin','app.ino.partitions.bin','boot_app0.bin','app.ino.bin','MANIFEST.json'|%{Copy-Item "$s\$_" E:\flash\v32 -Force}
Copy-Item E:\dev\CYDpod\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe E:\flash\v32 -Force
cd E:\flash\v32
foreach($op in 'write-flash','verify-flash'){.\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin}
```
Owner: build a 10,000-track library (scan time/progress); measure boot; test shuffle, queue scrolling/boundaries, BT takeover/reconnect/discoverability, upload page/malicious filenames. Actual AirPods Pro 2 and stereo line/headphone acceptance remain open.