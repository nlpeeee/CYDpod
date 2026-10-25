# CY-10d final probe COM10 smoke test: PASS

2026-10-25, source HEAD `111efe636e2f843150568876b9cbaca4d9b59dec`, branch
`cydpod/cy-10`. The user identified the CYD as COM10 and authorised this flash
and no-Bluetooth serial test. Esptool confirmed ESP32-D0WD-V3 rev v3.1,
MAC `ec:64:c9:89:d9:a4`, matching the recorded device identity.

The final probe was flashed once and all four regions explicitly verified.
The boot banner was captured, SD mounted, both `/Music` listings matched,
and two requested stats readings 21.011 seconds apart were stable. No FATAL,
ERR, panic, watchdog or reset appeared in the 32-second session. COM10 was
closed normally; the board was left running the final probe, idle and muted,
with no Bluetooth connection or playback. Stock recovery was not needed.

## Build and recovery identity

Ran `powershell -NoProfile -File tools/check.ps1`, retaining full output in
`.tools/logs/cy10d-check.txt`; exit **0**:

```text
Sketch uses 1223566 bytes (62%) of program storage space. Maximum is 1966080 bytes.
Global variables use 61536 bytes (18%) of dynamic memory, leaving 266144 bytes for local variables. Maximum is 327680 bytes.
PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=20656 live=0
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

Player and uploader also compiled successfully. Refreshed
`.tools/logs/build-results.json` and independent file hashing agree: probe
**1,223,712 bytes**, SHA256
`0e0f02ceb782ef9e958dc34f60e9dd0de7724c206320eb3924f9094a9b097aff`.
The built `partitions.csv` matches locked min_spiffs SHA256
`fe66cdf53d5216316f59ec446e68820b32de1de31d751799d48a8df2452f72fa`.
Its app0/app1 slots are 0x1e0000 bytes at 0x10000/0x1f0000;
NVS 0x9000/0x5000, otadata 0xe000/0x2000, SPIFFS 0x3d0000/0x20000,
coredump 0x3f0000/0x10000.

Before writing, confirmed the recovery backup exists with 4,194,304 bytes
and SHA256 `28cc552e78e33db41fa58816cc28e2d04b01cb1458366f021a2e6750809acb5f`:
`.tools/backup/stock-esp32-2432s028-2026-10-25.bin`.
Recovery instructions remain in `cy0b-backup.md`.

## Flash commands and verification

Local runner `.tools/cy10d-flash-smoke.ps1` checks every file exists, the
app/backup sizes and hashes, and locked partition CSV before executing:

```powershell
$esp = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$flashFiles = @('0x1000','.tools/images/aac-probe/cydpod_probe.ino.bootloader.bin',
 '0x8000','.tools/images/aac-probe/cydpod_probe.ino.partitions.bin',
 '0xe000','.tools/build/aac-probe/boot_app0.bin',
 '0x10000','.tools/images/aac-probe/cydpod_probe.ino.bin')
& $esp --chip esp32 --port COM10 --baud 460800 write-flash --no-progress --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
& $esp --chip esp32 --port COM10 --baud 460800 verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB @flashFiles
```

Both exited **0**. Bootloader, partitions and boot_app0 lengths were 25,024,
3,072 and 8,192 bytes respectively. Each write reported `Hash of data verified`.
Explicit verification output:

```text
Verifying 0x61c0 (25024) bytes at 0x00001000 in flash against '.tools/images/aac-probe/cydpod_probe.ino.bootloader.bin'...
Verification successful (digest matched).
Verifying 0xc00 (3072) bytes at 0x00008000 in flash against '.tools/images/aac-probe/cydpod_probe.ino.partitions.bin'...
Verification successful (digest matched).
Verifying 0x2000 (8192) bytes at 0x0000e000 in flash against '.tools/build/aac-probe/boot_app0.bin'...
Verification successful (digest matched).
Verifying 0x12ac20 (1223712) bytes at 0x00010000 in flash against '.tools/images/aac-probe/cydpod_probe.ino.bin'...
Verification successful (digest matched).
Hard resetting via RTS pin...
verify_exit=0
```

Esptool adjusted the bootloader header/digest for the specified flash settings.
Write-flash erased only affected sectors; no full-chip erase was requested.
Raw logs: `.tools/logs/cy10d-flash.txt`, `cy10d-verify.txt`, and combined
`cy10d-flash-smoke-console.txt` (also contains hashes, exit codes and serial).

## Serial session and idle measurements

Immediately after verification's reset, the same runner invoked:

```powershell
& ./tools/cyd_probe_session.ps1 -Port COM10 -Script .tools/cy10d-smoke.txt -LogPath .tools/logs/cy10d-smoke.txt -DurationSeconds 32
```

The surrounding pipeline aborts on panic/backtrace/reboot/reset evidence so
the serial runner's finally block closes COM10 before any recovery. Session
exit was **0**. The entire host schedule was:

```text
mute
ls /Music
stats
WAIT 20
stats
ls /Music
```

Trimmed verbatim session (full UTC RX/TX log retained at the path above):

```text
2026-10-25T13:25:23.4434868Z RX CYDpod AAC probe chip=ESP32-D0WD-V3 psram=0; provisional I2S 22/27/17; outputs unverified
2026-10-25T13:25:25.1163912Z TX mute
2026-10-25T13:25:26.1176326Z TX ls /Music
2026-10-25T13:25:26.1808126Z RX LS done count=6 truncated=0
2026-10-25T13:25:27.1197634Z TX stats
2026-10-25T13:25:27.1515575Z RX METRIC m4a=na heap=144936 min_heap=144684 largest=110580 min_sampled_largest=110580 producer_stack=5812 sink_stack=2028 loop_stack=6560 ring=0 decoded=0 underrun_frames=0 underrun_calls=0 queue_rejected=0 connected=0 muted=1 level=10
2026-10-25T13:25:48.1319302Z TX stats
2026-10-25T13:25:48.1621788Z RX METRIC m4a=na heap=144936 min_heap=144684 largest=110580 min_sampled_largest=110580 producer_stack=5812 sink_stack=2028 loop_stack=6544 ring=0 decoded=0 underrun_frames=0 underrun_calls=0 queue_rejected=0 connected=0 muted=1 level=10
2026-10-25T13:25:49.1360211Z TX ls /Music
2026-10-25T13:25:49.2002473Z RX LS done count=6 truncated=0
2026-10-25T13:25:53.1406381Z RX METRIC m4a=na heap=144936 min_heap=144684 largest=110580 min_sampled_largest=110580 producer_stack=5812 sink_stack=2028 loop_stack=6544 ring=0 decoded=0 underrun_frames=0 underrun_calls=0 queue_rejected=0 connected=0 muted=1 level=10
```

Summary command `powershell -NoProfile -File tools/cyd_probe_session.ps1
-SummaryLog .tools/logs/cy10d-smoke.txt` exited 0; output retained as
`.tools/logs/cy10d-summary.txt`. Nine METRIC samples: heap always **144936**,
largest always **110580**, min_heap initially **144936** then **144684**
after first listing, stable thereafter. Lowest stack high-water marks were
producer **5812**, sink **2028**, loop **6544** bytes. All samples had
ring/decoded/underrun frames/underrun calls/queue rejected/connected **0**,
muted **1**, level **10**, m4a **na**. The loop stack's 16-byte change between
requested stats is a high-water mark, not a declining free-heap measurement.
One boot banner, ongoing five-second telemetry through 13:25:53 UTC, and
no reset/error lines support the idle stability pass; no playback claim follows.

## Exact read-only /Music manifest

Both listings returned these exact names and byte sizes, `kind=file`, with
`LS done count=6 truncated=0`. Successful listings establish the SD mount.

| Exact name | Bytes |
| --- | ---: |
| 07-20 - Daft Punk - Get Lucky.m4a | 9978043 |
| 07-21 - Cassius - Cassius 1999.m4a | 8966024 |
| 07-22 - Basement Jaxx - Red Alert.m4a | 10387612 |
| 07-23 - Basement Jaxx - Romeo.m4a | 8791060 |
| 07-24 - The Supermen Lovers - Starlight.m4a | 9435002 |
| 07-25 - Justice - D.A.N.C.E.m4a | 8565459 |

Prepared `tools/cyd_probe_session.cy9b.txt` from the sample using the first
five exact names and volume 3, with no pairing command. It is for the later
authorised CY-9b session with the user listening; **it was not executed**.
Its dry-run exited 0, five plays, final mute/stop at 361/362 seconds within
the default 400 seconds; output `.tools/logs/cy10d-cy9b-dryrun.txt`:

```powershell
powershell -NoProfile -File tools/cyd_probe_session.ps1 -Port COM10 -Script tools/cyd_probe_session.cy9b.txt -LogPath .tools/logs/cy10d-cy9b-never-opened.txt -DryRun
```

File listing alone does not verify codec, duration, media checksum, or audio.
Actual AirPods Pro 2 playback, R2 allocation recovery, and wired stereo gates
remain outstanding. No play/pair/output/unmute/volume command was sent to
the board; no other port or SD write/format operation was used.

The screen was not physically observed. Source setup has no display/UI
initialisation, so no probe UI is expected; panel/backlight appearance cannot
be inferred from serial. Initial sandbox Git/Orca access restrictions were
resolved with approved execution; they were not board failures.

Pre-commit `git status --short` contained only the two new deliverables and
the pre-existing `docs/research/build-baseline.md` modification. That existing
change was left untouched and excluded; only explicit report/script paths
were staged. No push was performed.
