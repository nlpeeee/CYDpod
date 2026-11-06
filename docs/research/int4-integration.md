# INT-4 integration v33
Branch `cydpod/integration-v3` from `cydpod/integration-v2` cd8e3b6 (v32). No device operations, no push.

## Merges (no-ff, no conflicts)
- `cydpod/host-green` 26bbb77 (F2d, tests only) -> 9030257
- `cydpod/playlist-match` 98a7b6e (PL-1) -> 236518b (`playlist.cpp/.h` merged cleanly; no `check.ps1` change needed)
- 9ed6331: export tag v32 -> v33 in `tools/check.ps1` (two lines).
Not included: F5, CY-68, F3-F23.

## Checks (once each, final code tip 9ed6331)
- `check.ps1 -SkipProbe`: exit 0, 0 FAIL lines; all 10 host stages PASS (codec_pool, i2s_tone, wired_fade, i2s_diag, bt_policy, catalog_sort, playlist_queue, audio_order, catalog_validation, media_check). Log `.tools/logs/int4-host.log`.
- `-SkipProbe -SkipHostMedia` default and `-Board jc2432w328c`: exit 0, each `PASS: compile-only checks completed; no device commands were run.`; `PASS single pool: ... 0000a000` (40,960 B).

## Size, W328C, v32 (cd8e3b6) -> v33
Static RAM 94,960 -> 95,056 B (+96). Sketch flash 1,548,213 -> 1,551,041 B (+2,828). app.bin 1,548,368 -> 1,551,184 B (+2,816). Slot limit 1,966,080.

## Image `.tools/images/codec-pool-v33-jc2432w328c` (copied to `E:\flash\v33`)
| File | Offset | Bytes | SHA256 |
|---|---|---:|---|
| app.ino.bootloader.bin | 0x1000 | 25024 | f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29 |
| app.ino.partitions.bin | 0x8000 | 3072 | 0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7 |
| boot_app0.bin | 0xe000 | 8192 | f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0 |
| app.ino.bin | 0x10000 | 1551184 | 5f1cf0f7c769fb2ed5e653164a999a5be929483299c39eda4891945e9bdc24a1 |

## Single file `E:\flash\v33\cydpod-v33-w328c.bin`
4,194,304 B (full 4 MB), SHA256 `333b5fc005416167a634adf40588b1e6a51b5a746ecf69fad36a30156b23dc3b` (= `app.ino.merged.bin`).
Read back at the offsets above: all four regions byte-identical to the separate files (hashes match); every other byte (0x0-0xfff, gaps, 0x18ab50-0x3fffff) is 0xFF.

**Flashing it at 0x0 erases NVS and all other data partitions.** The image holds 0xFF over NVS (0x9000-0xdfff), app1/OTA slot (0x1f0000), spiffs (0x3d0000, unused by the firmware) and coredump (0x3f0000), and write-flash erases the whole range it writes. Lost: saved Bluetooth speaker and bond, volume, theme, brightness, screen/sleep settings, Wi-Fi, last track/position, touch settings. Kept: the SD card, including library caches in `/.sys`. (Inferred from the layout and esptool behaviour; not run on a device.)

## Owner commands (confirm COM7; BOOT+RESET)
(a) single file, **erases settings**:
```powershell
cd E:\flash\v33
foreach($op in 'write-flash','verify-flash'){.\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 cydpod-v33-w328c.bin}
```
(b) four regions, keeps NVS:
```powershell
cd E:\flash\v33
foreach($op in 'write-flash','verify-flash'){.\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin}
```
Owner checklist: `docs/research/int4-checklist.md` (copy in `E:\flash\v33\CHECKLIST.md`).
