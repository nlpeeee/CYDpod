# CY-9 Bluetooth probe hardware procedure

CY-8 prepares this procedure only: no port, device, SD or esptool access was performed. Run commands below only during the separately authorised CY-9 hardware session, from `E:\dev\CYDpod`. Wired DAC, line/headphone and switching gates remain deferred, not passed.

## 1. Identify and back up (CY-0b), before flashing

The dispatch identifies COM10 as ESP32-D0WD-V3, 4 MB, no PSRAM. `docs/research/cyd-device-id.md` was absent from this checkout: attach that identification evidence and reconfirm the physical CYD/COM10 association before proceeding. Never substitute COM8 or choose a port from enumeration alone. Close all serial monitors before esptool; use COM10 only throughout this procedure.

Use the locked esptool 5.3.0; these are instructions, not commands run by CY-8:

```powershell
$esp = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
New-Item -ItemType Directory -Force .tools/backup | Out-Null
$backup = '.tools/backup/cyd-com10-before-cy9.bin'
if (Test-Path $backup) { throw 'Preserve existing backup; choose a new evidence filename.' }
& $esp --chip esp32 --port COM10 --baud 115200 read-flash 0x0 0x400000 $backup
if ($LASTEXITCODE -ne 0 -or (Get-Item $backup).Length -ne 4194304) { throw 'Backup failed; do not flash.' }
Get-FileHash $backup -Algorithm SHA256 | Format-List | Out-File .tools/backup/cyd-com10-before-cy9.sha256.txt
```

Copy the backup and checksum to safe host storage and record the SHA256 in the session evidence. Exact recovery command (close serial session first; verify saved checksum before restore):

```powershell
& $esp --chip esp32 --port COM10 --baud 115200 --after no-reset write-flash 0x0 .tools/backup/cyd-com10-before-cy9.bin
if ($LASTEXITCODE -ne 0) { throw 'Restore failed; preserve logs and stop.' }
& $esp --chip esp32 --port COM10 --baud 115200 verify-flash 0x0 .tools/backup/cyd-com10-before-cy9.bin
if ($LASTEXITCODE -ne 0) { throw 'Restore verification failed.' }
```

## 2. Flash and verify the probe

Check `.tools/logs/build-results.json`: aac-probe must be 1,222,320 bytes, SHA256 `90b74d03c7b273dbcb394b5f3ebe3c19d11c01434d640ac31dac6ed6dd3d02f3`; confirm the actual `.ino.bin` hash too. Locked FQBN is `esp32:esp32:esp32:PartitionScheme=min_spiffs,PSRAM=disabled,FlashSize=4M`. Offsets/flash settings below come from `.tools/build/aac-probe/flash_args`; boot_app0 is in the build directory, other binaries in images. No erase-all command is needed. NVS remains but OTA selection at 0xe000 is overwritten; full backup is the recovery authority.

```powershell
$flashFiles = @('0x1000','.tools/images/aac-probe/cydpod_probe.ino.bootloader.bin',
 '0x8000','.tools/images/aac-probe/cydpod_probe.ino.partitions.bin',
 '0xe000','.tools/build/aac-probe/boot_app0.bin',
 '0x10000','.tools/images/aac-probe/cydpod_probe.ino.bin')
& $esp --chip esp32 --port COM10 --baud 115200 write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
if ($LASTEXITCODE -ne 0) { throw 'Flash failed; stop and recover.' }
& $esp --chip esp32 --port COM10 --baud 115200 verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
if ($LASTEXITCODE -ne 0) { throw 'Verification failed; stop and recover.' }
```

Retain esptool output and exit codes. [Esptool v5 verifies writes automatically](https://docs.espressif.com/projects/esptool/en/latest/esp32/migration-guide.html); the explicit verification above is additional evidence. Do not execute the session runner concurrently with esptool.

## 3. SD and safe serial bring-up

Use the user's existing FAT32 card; probe SD uses VSPI 18/19/23, CS5 at 10 MHz and opens tracks FILE_READ. Never format, repair or write the card. Probe has **no listing command**: obtain exact existing `/Music` names from the user or a read-only host directory listing; do not assume the sample names exist. Missing names give `ERR track open`; `FATAL SD mount` means stop and check the card, not format it. Record filename/checksum and existing media metadata: AAC, LC, 44100, 2 channels (A0). The probe does not expose decoded PCM/codec negotiation telemetry: A0/A1 rate/channel/reference validation needs separate evidence and cannot pass from `m4a=ok` alone.

Open COM10 at 115200 8N1 only after identification. Start `mute`, `volume 0`; after every `pair`/`output` send `volume 3` again because saved output levels are restored. Confirm `METRIC ... muted=1 level=3`. Start listening at 0-5 percent; never exceed 25 (probe command limit). Every play/output/connect/disconnect mutes, so explicitly `unmute` after priming/connection. If repeated resets occur: stop commands, close the serial port, preserve logs, restore and verify the full backup from step 1; do not continue retrying playback.

## 4. Pair actual AirPods Pro 2

With both buds in their case and lid open, hold the rear setup button about five seconds until the light blinks white ([Apple instructions](https://support.apple.com/en-by/guide/airpods/dev499c9718b/27/web/27)). Prevent a nearby phone taking the connection for the initial test; record the exact advertised AirPods name from its existing Bluetooth settings. Send `pair <exact name>`, wait for completion, then `output bt`, `volume 3`, `stats`. `OUTPUT ready mode=0 muted=1; switching restarts track` means initialisation, **not connection**; require `METRIC ... connected=1 muted=1 level=3` within 30 seconds. Then `play /Music/<exact-name>.m4a`, wait five seconds, `stats`, `unmute`.

Expected: `TRACK ev=start name=... m4a=ok`, periodic `METRIC m4a=ok ... connected=1`, and at natural completion `TRACK ev=end ... m4a=ok` then `EOF`. Stop is explicit, and need not emit an end line. There is no automatic next track. On `ERR Bluetooth timeout; pair again` or `ERR Bluetooth connect/reconnect timeout; pair again`, send `mute`, `stop`, return buds to pairing mode, resend `pair <name>`, `output bt`, `volume 3`; wait for connected=1 before restarting/unmuting. Preserve the failed interval; recovery does not turn it into a successful uninterrupted run. No forget-bond command exists.

## 5. Capture the five-track run, then lifecycle and soak

Copy `tools/cyd_probe_session.sample.txt` to a host file under `.tools/`, replace all five placeholders with different existing `.m4a` names. Pair first as above; the sample assumes a known peer. `WAIT n` is a host delay, with a default one-second delay per command and two-second initial settling time. Paths may contain spaces; do not quote serial paths. Choose tracks longer than 70 seconds for the sample's 60-second steady intervals, or adjust waits to their recorded durations. Its 400-second default covers all commands. Session ending closes the port but does not stop playback automatically; retain the final mute/stop commands and avoid truncating the schedule.

```powershell
powershell -NoProfile -File tools/cyd_probe_session.ps1 -Port COM10 -Script .tools/cy9-five.txt -LogPath .tools/logs/cy9-five.txt -DurationSeconds 400 -DryRun
# Only during CY-9, remove -DryRun to execute the reviewed schedule.
powershell -NoProfile -File tools/cyd_probe_session.ps1 -SummaryLog .tools/logs/cy9-five.txt
```

Runner also accepts `-Command 'mute','stats','stop'`; `-WhatIf` and `-DryRun` never open a port or create logs. LogPath must be new, with an existing parent directory. UTC RX/TX timestamps preserve sequence; counters are since boot, not per-track. Summary gives observed min/max/first/last, not lifetime totals; inspect reset boundaries and steady intervals in raw logs. Automatic metrics every five seconds provide time samples even during WAIT.

| Gate | CY-9 numeric / yes-no rule |
| --- | --- |
| Required Bluetooth playback (R2/R3, A0/A2) | Actual AirPods Pro 2, five distinct verified AAC-LC/44100/stereo originals; all five audible, clean, correct speed and stereo yes; no distortion/dropout yes. Record duration and listener observations; reference/digital A1 remains separate. |
| Sequence / EOF (A7) | Five starts with `m4a=ok`; four successful stop-to-play or EOF-to-next-play transitions, no reset/hang. Also run at least one natural EOF followed by another play; `ev=end` plus EOF and next start required. |
| Rejection (A7) | If an existing unsupported/malformed file is present: one `ev=skip m4a=<non-ok>` and `ERR m4a ...`, followed by successful good-file playback without reset. If none exists, mark not tested; never create a file on the card. |
| Underruns / stability (A2) | Exclude labelled first/last five seconds of each track and connection gaps; in every remaining interval delta `underrun_frames=0` and `underrun_calls=0`; decoded count increases. Zero resets/watchdogs/allocation errors/queue rejections. Counter decrease invalidates interval and flags reset. |
| Memory / stacks (section 6) | Every `heap` and `min_heap` >=20480 bytes; every producer/sink/loop stack high-water mark >=1024 bytes (ESP32 reports bytes). `largest` and `min_sampled_largest` must exceed largest remaining allocation +4096 bytes; record that allocation bound from the locked implementation, otherwise mark gate unverified. Compare equivalent post-stop states across five tracks: no progressive heap/largest decline; log ranges and explain any decrease. |
| Reconnect (A3 subset) | Put buds in case and close until `connected=0`; reopen within 30 seconds, require `connected=1` within 30 seconds, still muted yes; set volume 3 and explicitly unmute/replay, clean audio yes without board reset. Repeat after >30 seconds closed to exercise timeout and explicit re-pair recovery. |
| Extended A2/A3 | After smoke pass: >=3600 seconds required-file playback, >=100 transitions with same underrun/memory rules; ten reboot reconnects, range return and competing phone recovery all 10/10 or yes. Final 8-hour soak remains release work. Probe lacks screen activity; final UI qualification remains deferred. |

Record raw session logs, exact firmware commit/hash, board identity, SD/media manifest, AirPods model/firmware/name, time ranges and each gate's pass/fail/not-tested in `docs/research/cydpod-validation.md` during CY-9. Source Bluetooth path is decoded PCM to SBC; don't claim AAC radio transport. No wired A4/A5/A6 gate passes in this procedure.

## CY-8 offline verification

Fabricated `tests/fixtures-log/probe-session.txt` expectations: heap 60000..80000, largest 32000..50000, min_heap 54000..70000; underrun_frames 0..12, underrun_calls 0..2 (both first/last 0); statuses na=2, ok=2, moov_after_mdat=1; exactly two ERR lines. Run SummaryLog under both PowerShell 5.1 and 7. This fixture deliberately includes a reset; summary ranges alone cannot pass the steady-state gate.

Verified 2026-10-25 on both `powershell -NoProfile -File tools/cyd_probe_session.ps1` and `pwsh -NoProfile -File tools/cyd_probe_session.ps1`: sample DryRun and WhatIf exit 0, print five plays (last command at 356 s), and create no session log; missing Port and invalid `/dev/ttyUSB0` exit 1. SummaryLog matches every fixture range/status above and prints both ERR lines. Local output retained in `.tools/logs/cy8-offline-check.txt`; no serial object is constructed on dry-run paths.

`powershell -NoProfile -File tools/check.ps1` exited 0: `PASS synthetic fixture matrix: 3 positive, 7 negative` and `PASS: compile-only checks completed; no device commands were run.` Full output retained in `.tools/logs/cy8-check.txt`. Build results and actual binary hash confirm unchanged aac-probe 1,222,320 bytes / `90b74d03c7b273dbcb394b5f3ebe3c19d11c01434d640ac31dac6ed6dd3d02f3`; player/uploader hashes also unchanged. These are offline checks, not hardware results.
