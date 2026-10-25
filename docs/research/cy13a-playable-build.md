# CY-13a: playable admission margin, flashed to COM10

2026-10-25, branch `cydpod/cy-10`, parent `c0ece7f`. The user authorised flashing COM10 only. No Bluetooth, no audio, no SD writes.

## Change

`kPlaybackMarginBytes` 10240 -> **2048** in `firmware/cydpod_probe/cydpod_probe.ino` (same change as `cy11c-qualification.patch`). Model: need 20,680 + 14,336 + 4,392 + 2,048 = **41,456 B** versus 45,488 B measured free after open (CY-12a): 4,032 B slack, so `.m4a` tracks are admitted. **Provisional:** the true playback heap floor has not been measured (no track was played in CY-11c). If heap runs out during playback the board reboots; the allocation-failure fix (R2) is open. The user stated the hand test of the lower-margin variant succeeded; the logs do not show playback.

## Build identity

`powershell -File tools/check.ps1` exit 0 (`.tools/logs/cy13a-check.txt`); seven host suites pass (real: accepted=25 mismatches=0); session dry-runs pass in PS 5.1 and `pwsh`.

| | bytes | static RAM | sha256 |
|---|---|---|---|
| CY-11b probe (margin 10240) | 1,224,944 | 61,552 | `b25eec1eaf5d35daebbdb03339235c358d45708187e48627d81acbfb48e91121` |
| CY-13a probe (margin 2048) | 1,224,944 | 61,552 (+0) | `002f811ffaedf48cee190f9dfb225ad013f8557a1f1bb687034d620d00a20914` |

62.3% of the 1,966,080 B slot. (The earlier uncommitted scratch build was 1,224,790 B; that is a different compile.)

## Flash and smoke (COM10)

`tools/cy11c_flash.ps1 -ImageDir .tools/images/aac-probe -Tag cy13a`: one `write-flash` of the four regions (no erase), then `verify-flash`: all four "Verification successful"; `FLASH_VERIFY_OK`. Logs: `.tools/logs/cy13a-flash.txt`, `cy13a-verify.txt`.

Smoke: `tools/cyd_probe_session.ps1`-style run via `tools/cy11c_session.ps1 -Port COM10 -Script tools/cy13a_smoke.txt` (`mute`, `ls /Music`, `stats`, wait 20 s, `stats`), log `.tools/logs/cy13a-smoke.txt`. Result: six `/Music` names (`LS done count=6`), METRIC flat across the session (heap 144,788, min_heap 144,512, largest 110,580, underruns 0, connected=0, muted=1), no ERR/FATAL/panic/reset. The boot banner was not in the log (the board booted before the session opened the port). Board left muted and idle; port closed.

## Testing playback (not yet done by an agent; image untested for sound and for the heap floor)

Open `python -m serial.tools.miniterm COM10 115200` (Ctrl+] exits; one program at a time on COM10). Then:

```
ls /Music
output bt
volume 3
pair Xiaomi Sound Pocket     (speaker in pairing mode first)
stats                        (connected=1)
play /Music/07-25 - Justice - D.A.N.C.E.m4a
unmute
```

Watch `TRACK ev=live ... heap=... largest=...` (real free heap while playing), `min_heap` in `stats`, `underrun_frames`, any `ERR m4a low_memory` and any second banner (reboot).
