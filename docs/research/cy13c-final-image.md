# CY-13c: final image flash and muted idle smoke

2026-10-25, 17:30-17:33 UTC. Firmware source: `cydpod/cy-10` at `4ead8a8`
(CY-17), with the reader hardening, file-backed table, allocation-failure hook,
16,384 B arena and 2,048-short PCM buffer. Playback margin remains 2,048 B.
The user authorised COM10 only; the previously identified CYD has ESP32-D0WD-V3
revision v3.1, MAC `ec:64:c9:89:d9:a4`, also confirmed by this flash/verify log.

## Build identity and check

`powershell -NoProfile -File tools/check.ps1` exited **0**. Full output:
`.tools/logs/cy13c-check.txt`; image inventory: `.tools/logs/build-results.json`.
All three builds, pinned inventories and host-media checks passed, including:

```text
Global variables use 57464 bytes (17%) of dynamic memory, leaving 270216 bytes for local variables. Maximum is 327680 bytes.
PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=20656 live=0
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
exit=0
```

Probe image: **1,226,960 B**, SHA256
`5b2b0b555020b6377c3aff0ee9b57e5734ad2a463b2a382f7d231de9d0b4697e`,
static RAM **57,464 B**. All match the dispatched identity. The compiler's
1,226,814-byte storage figure excludes image overhead; the file size above is
the inventory/flash size. CY-13a was 1,224,944 B, static RAM 61,552 B, SHA256
`002f811ffaedf48cee190f9dfb225ad013f8557a1f1bb687034d620d00a20914`.

## Flash and verification

One `write-flash` attempt, followed by `verify-flash`, using a local copy of
`tools/cy11c_flash.ps1` with only log filenames changed to `cy13c-*`:

```powershell
powershell -NoProfile -File .tools/cy13c-flash.ps1 -ImageDir .tools/images/aac-probe -Tag cy13c
```

COM10, 460800 baud, ESP32, DIO / 80 MHz / 4 MB. Written and verified regions:

| Offset | File | Bytes |
| --- | --- | ---: |
| 0x1000 | cydpod_probe.ino.bootloader.bin | 25,024 |
| 0x8000 | cydpod_probe.ino.partitions.bin | 3,072 |
| 0xe000 | boot_app0.bin | 8,192 |
| 0x10000 | cydpod_probe.ino.bin | 1,226,960 |

All four reported `Verification successful (digest matched).`, ending
`FLASH_VERIFY_OK`; exit 0. Evidence: `.tools/logs/cy13c-flash.txt` and
`.tools/logs/cy13c-verify.txt`. No `erase-flash` command was run; esptool's normal
sector erases within the written regions are in its log. No restore was needed.
The stock backup was checked beforehand: 4,194,304 B, SHA256
`28cc552e78e33db41fa58816cc28e2d04b01cb1458366f021a2e6750809acb5f`.

## Thirty-second idle smoke

Used an ignored local copy of `tools/cyd_probe_session.ps1`, adding only an RTS
reset immediately after opening COM10: DTR stays false, RTS true for 100 ms then
false. The port was already open when boot output arrived. This runner does not
send the `stop` cleanup command present in `cy11c_session.ps1`.

```powershell
powershell -NoProfile -File .tools/cy13c-session.ps1 -Port COM10 -Script tools/cy13a_smoke.txt -LogPath .tools/logs/cy13c-smoke.txt -DurationSeconds 30
```

Exit **0**; log `.tools/logs/cy13c-smoke.txt`, console capture
`.tools/logs/cy13c-session-output.txt`. The session captured one ROM reset/banner
from the intentional reset at 17:32:22 UTC and one application banner:

```text
CYDpod AAC probe chip=ESP32-D0WD-V3 psram=0; provisional I2S 22/27/17; outputs unverified
LS done count=6 truncated=0
METRIC m4a=na heap=148876 min_heap=148600 largest=110580 min_sampled_largest=110580 producer_stack=4584 sink_stack=2032 loop_stack=6356 ring=0 decoded=0 underrun_frames=0 underrun_calls=0 queue_rejected=0 connected=0 muted=1 level=3 source_reads=0
```

Only transmitted commands: `mute`, `ls /Music`, `stats`, then after the script's
20-second wait, `stats` (at approximately 2, 3, 4 and 25 seconds). Six names:
Daft Punk/Get Lucky, Cassius/Cassius 1999, Basement Jaxx/Red Alert,
Basement Jaxx/Romeo, The Supermen Lovers/Starlight and Justice/D.A.N.C.E.
Free heap and largest block were flat throughout; minimum heap settled after
listing, and stack high-water values declined slightly as idle paths ran.
No ERR, FATAL, panic, watchdog or unexpected reset appeared. All metrics showed
`connected=0`, `muted=1`, zero decoded frames and zero underruns.
The board was left muted and idle, and the port was disposed/closed.

| Idle metric | CY-13a | CY-13c | Change |
| --- | ---: | ---: | ---: |
| Free heap | 144,788 B | 148,876 B | +4,088 B |
| Minimum heap after listing | 144,512 B | 148,600 B | +4,088 B |
| Largest free block | 110,580 B | 110,580 B | 0 B |

The observed gain is about 4 KB, as expected. Relative to CY-13a the net static
RAM saving is 4,088 B; CY-17 alone saved 4,096 B relative to CY-10b3.

## Remaining measurements and documentation

This establishes boot and idle only. Largest block **after Bluetooth and file
open**, admission on hardware, sound, and the true playback heap floor remain
unmeasured. The CY-17 model predicts 49,584 B free versus `need_free=47,840 B`,
but its largest-block term is not established by this idle test; even the idle
largest block did not grow. No Bluetooth/audio action or SD write was performed.

Updated the `State on ...` paragraph in
`E:\dev\hq\docs\howto\cyd-m4a-bluetooth-test.md` with this source/image identity,
flash verification, idle results, and remaining limitations. That external file
is not included in the CYDpod commit. Firmware, libraries, vendor sources and
`tools/check.ps1` were untouched. The earlier uncommitted
`docs/research/build-baseline.md` change is preserved and excluded from staging;
the check did not modify `docs/research/aac-prototype.md`. No push performed.
