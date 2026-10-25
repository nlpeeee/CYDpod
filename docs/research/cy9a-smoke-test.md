# CY-9a COM10 probe smoke test: boot panic, stock restored

Date: 2026-10-25. Result: **FAIL**. The pinned probe flashed and verified on the user-confirmed CYD at COM10, but panicked in startup telemetry before a directory listing or idle metrics could be returned. The single permitted re-flash reproduced the fault; stock recovery is recorded below. No firmware source changes or push were made.

## Identity and compile evidence

Branch `cydpod/cy-9`; firmware source HEAD `02453732a5f3e19ced82af997588229eb739fb49`. The pre-existing modification to `docs/research/build-baseline.md` was preserved and excluded from staging. Board identification matched the existing evidence: ESP32-D0WD-V3 revision v3.1, MAC `ec:64:c9:89:d9:a4`, 4 MB flash, user-confirmed COM10.

Command: `powershell -NoProfile -File tools/check.ps1`, captured in `.tools/logs/cy9a-check.txt`; exit 0:

```text
Sketch uses 1222730 bytes (62%) of program storage space. Maximum is 1966080 bytes.
Global variables use 61528 bytes (18%) of dynamic memory, leaving 266152 bytes for local variables. Maximum is 327680 bytes.
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

Actual `.tools/images/aac-probe/cydpod_probe.ino.bin`: **1,222,880 bytes**, SHA256 `9a57cb67d323030de1e4e796c0b9ff440f06981393a66ad320de258d05154b4c`, matching `.tools/logs/build-results.json` and the dispatch. Actual `.tools/build/aac-probe/partitions.csv` matched `dependencies.lock.json`'s pinned min_spiffs checksum. FQBN: `esp32:esp32:esp32:PartitionScheme=min_spiffs,PSRAM=disabled,FlashSize=4M`.

All four files existed before opening COM10:

| Offset | File | Bytes |
| --- | --- | ---: |
| 0x1000 | .tools/images/aac-probe/cydpod_probe.ino.bootloader.bin | 25,024 |
| 0x8000 | .tools/images/aac-probe/cydpod_probe.ino.partitions.bin | 3,072 |
| 0xe000 | .tools/build/aac-probe/boot_app0.bin | 8,192 |
| 0x10000 | .tools/images/aac-probe/cydpod_probe.ino.bin | 1,222,880 |

The resulting min_spiffs layout has nvs at 0x9000/0x5000, otadata at 0xe000/0x2000, app0 at 0x10000/0x1e0000, app1 at 0x1f0000/0x1e0000, spiffs at 0x3d0000/0x20000, and coredump at 0x3f0000/0x10000. Stock backup identity was checked before flash and again before recovery: 4,194,304 bytes, SHA256 `28cc552e78e33db41fa58816cc28e2d04b01cb1458366f021a2e6750809acb5f`.

## Flash and explicit verification

Pinned esptool 5.3.0; commands below ran from the repository root (PowerShell array expansion):

```powershell
$esp = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$flashFiles = @('0x1000','.tools/images/aac-probe/cydpod_probe.ino.bootloader.bin',
 '0x8000','.tools/images/aac-probe/cydpod_probe.ino.partitions.bin',
 '0xe000','.tools/build/aac-probe/boot_app0.bin',
 '0x10000','.tools/images/aac-probe/cydpod_probe.ino.bin')
& $esp --chip esp32 --port COM10 --baud 460800 write-flash --no-progress --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
& $esp --chip esp32 --port COM10 --baud 460800 verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
```

Both exited 0 on the initial flash and the one retry. Each write printed `Hash of data verified.` for all four regions; each explicit verify printed:

```text
Verification successful (digest matched).
Verification successful (digest matched).
Verification successful (digest matched).
Verification successful (digest matched).
Hard resetting via RTS pin...
```

Raw evidence: `.tools/logs/cy9a-flash.txt`, `cy9a-verify.txt`, `cy9a-reflash.txt`, `cy9a-reverify.txt`. Esptool adjusted the bootloader header/digest for the requested flash settings and verified against the correspondingly adjusted image. Only the write regions were sector-erased as part of write-flash; no full-chip erase command was used.

An earlier invocation put `--no-progress` before the subcommand. Esptool rejected that argument placement before connecting; Windows PowerShell also treated its native stderr as a terminating error. The corrected command above ran under `pwsh`; this invocation error did not flash the board.

## Serial session and observed failure

The verify command hard-reset the board, then immediately invoked the existing runner in the same PowerShell process, capturing the startup banner. The reviewed schedule, stored locally in `.tools/cy9a-smoke.txt`, was:

```text
mute
ls /Music
stats
WAIT 20
stats
ls /Music
```

Command: `tools/cyd_probe_session.ps1 -Port COM10 -Script .tools/cy9a-smoke.txt -LogPath .tools/logs/cy9a-smoke.txt -DurationSeconds 32` (invoked by `.tools/cy9a-flash-smoke.ps1`). Full timestamped session: `.tools/logs/cy9a-smoke.txt`. Trimmed real output:

```text
2026-10-25T12:14:19.7131130Z RX CYDpod AAC probe chip=ESP32-D0WD-V3 psram=0; provisional I2S 22/27/17; outputs unverified
2026-10-25T12:14:19.9370763Z RX Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
2026-10-25T12:14:19.9989882Z RX EXCVADDR: 0x00023630  LBEG    : 0x40091a9d  LEND    : 0x40091aad  LCOUNT  : 0xffffffff
2026-10-25T12:14:19.9993875Z RX Backtrace: 0x40091a9a:0x3ffd1200 0x401970de:0x3ffd1210 0x40191ecb:0x3ffd1530 0x40191f11:0x3ffd15c0 0x400decbe:0x3ffd1600 0x400ded45:0x3ffd1690 0x400d4559:0x3ffd16e0 0x400d4898:0x3ffd1750 0x400e22de:0x3ffd1790 0x40095ae9:0x3ffd17b0
2026-10-25T12:14:19.9999376Z RX ELF file SHA256: d4d76764a
2026-10-25T12:14:20.0614250Z RX Rebooting...
2026-10-25T12:14:20.0618584Z RX rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
2026-10-25T12:14:21.3461525Z TX mute
2026-10-25T12:14:22.3358590Z TX ls /Music
2026-10-25T12:14:23.3428148Z TX stats
2026-10-25T12:14:44.3544468Z TX stats
2026-10-25T12:14:45.3484050Z TX ls /Music
```

The first session logged 34 banners and 34 panics, zero METRIC and zero LS lines. By the time the failure output was inspected, the short scheduled commands had been transmitted; the session process was terminated to close COM10 and stop further commands. The retry used the same flash/verify commands, then a six-second runner session with only `mute`, captured in `.tools/logs/cy9a-retry.txt` and `cy9a-retry-console.txt`; it logged seven banners and seven identical panics, zero METRIC and zero LS. Its process exited 0, which is only runner completion, not hardware success. Both serial sessions were closed before subsequent esptool operations.

No `FATAL` or `ERR` response was observed. The SD mount appears to have succeeded: setup reached its help banner and `metrics()` after `SD.begin` and task allocation checks; this is source-order inference, not an SD listing or media-integrity test. **Exact /Music names and sizes: unavailable (no LS response). Idle heap, largest block, and min_heap: unavailable (no METRIC response). Twenty-second stability: failed, repeated resets.** The probe has no UI and does not initialise the screen; physical screen state was not observed and cannot be inferred from serial.

No Bluetooth pairing, playback, output selection, unmute or volume command was sent. Only COM10 was opened; no SD write/format command was issued.

## Decoded cause and scope boundary

The coordinator identified the cause and requested immediate restore; its message arrived after the permitted retry had already completed and stock restoration had started. Offline decoding independently confirmed it:

```powershell
& '.\.deps\arduino\packages\esp32\tools\esp-x32\2601\bin\xtensa-esp32-elf-addr2line.exe' -pfiaC -e .tools/build/aac-probe/cydpod_probe.ino.elf 0x40091a9a 0x401970de 0x40191ecb 0x40191f11 0x400decbe 0x400ded45 0x400d4559 0x400d4898 0x400e22de 0x40095ae9
```

Exit 0; full output `.tools/logs/cy9a-backtrace.txt`. Relevant chain: `strlen` -> `_svfprintf_r` -> `_vsnprintf_r` -> `vsnprintf` -> `Print::vprintf` -> `Print::printf` -> `metrics()` at probe line 249 -> `setup()` at line 312 -> `loopTask` -> `vPortTaskWrapper`.

The format starts `METRIC m4a=%s heap=%u ...`, but the argument list starts with `heap_caps_get_free_size(...)` and omits `m4aState`. The integer heap value is interpreted as a string pointer, matching EXCVADDR `0x23630`; that is not a valid telemetry reading. Fix and rebuilt-image qualification belong to the next chunk; no source correction was attempted here.

## Stock recovery and final state

Restored the complete 4 MB backup with the documented command, then explicitly verified the full image:

```powershell
$esp = 'E:\dev\CYDpod\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$backup = 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin'
& $esp --chip esp32 --port COM10 --baud 115200 --after hard-reset write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0 $backup
& $esp --chip esp32 --port COM10 --baud 115200 verify-flash 0x0 $backup
```

Both exited 0. Real output:

```text
Wrote 4194304 bytes (1249319 compressed) at 0x00000000 in 113.5 seconds (295.6 kbit/s).
Verifying written data...
Hash of data verified.
Hard resetting via RTS pin...
restore_exit=0
Verifying 0x400000 (4194304) bytes at 0x00000000 in flash against 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin'...
Verification successful (digest matched).
Hard resetting via RTS pin...
restore_verify_exit=0
```

Full recovery logs: `.tools/logs/cy9a-restore.txt`, `cy9a-restore-verify.txt`, `cy9a-restore-console.txt`; local command script `.tools/cy9a-restore.ps1`. The board was left hard-reset into the verified stock image with COM10 closed. The stock demo's physical screen/boot behaviour was not observed; full-image verification is the recovery evidence. No further device operations were performed.

Pre-commit `git status --short` showed only:

```text
 M docs/research/build-baseline.md
?? docs/research/cy9a-smoke-test.md
```

Only this report is staged. All build, backup, flash and serial artifacts remain ignored under `.tools/`; no binaries or logs are committed.

CY-9b is blocked pending a corrected probe and a fresh COM10 smoke test. `tools/cyd_probe_session.cy9b.txt` was not created because no real track names were obtained; the coordinator explicitly redirected this failure branch to a report-only commit. No placeholder or guessed media names were substituted. Bluetooth, audible output, AAC compatibility, and all line/headphone acceptance gates remain untested.
