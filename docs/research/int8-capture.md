# CYDpod v36: matched decoder capture (owner only)

Flash **cydpod-v36-w328c.bin (compact) first**, then run A-H (I if available). Flash **cydpod-v36-legacy-w328c.bin** and repeat C, D and F, recording a fresh A header. Use FLASH.md. Keep the same SD card, track, speaker/headphones, volume, output and scrolling pace; restore settings after a single-file flash. Keep separate labelled serial logs (115200 baud), starting before RESET. No hardware results are claimed yet.

**Shared capture for C-F:** retain consecutive 10-second groups of `stat: cpu`, `stat: flac`, `stat: pull`, `stat: sched`, `stat: audio`, `stat: heap` and every `diag:` line. Include `stat: tasks` whenever emitted. Report missing lines rather than substituting values: `stat: flac` is absent for non-FLAC. Retain underruns at start/end (cumulative; compare the delta). Discard a partial interval after changing track/output; capture six complete groups for each 60-second run and three for F. Say whether audio breaks up, and when.

A. **Boot:** label the exact flashed filename/decoder. Neither build prints the decoder name in its boot banner: note it manually. Paste boot lines through `heap: after init`, plus `cmd: cpustats ok` and `sd: clock ...` (retain later lines if needed).

B. **Idle, 60 s:** no playback or touch. Paste `stat: cpu`, `stat: tasks` and `stat: heap`. Tasks print at the first stat interval after 20 s, then every 60 s; leave the log running until one appears.

C. **FLAC / Bluetooth, 60 s:** with a card reader copy `E:\dev\CYDpod\testsamples\04 - CAKE - Short Skirt - Long Jacket.flac` to `/music` on the card. Play CAKE - Short Skirt Long Jacket over Bluetooth and collect six shared groups. Note speaker name and whether it breaks up.

D. **Same FLAC / wired, 60 s:** select the wired/headphone output, use the same file/position and collect six shared groups. State output and whether it breaks up.

E. **Control / Bluetooth, 60 s:** play an MP3 or M4A, naming it; collect six shared groups and report breakup. FLAC-specific lines may be absent.

F. **FLAC / Bluetooth + Songs scroll, 30 s:** same CAKE file and start point; scroll the Songs list continuously at a repeatable pace. Collect three shared groups and report breakup.

G. **Takeover (compact alone is fine):** while the player is connected, connect another phone to the speaker and play from it. Press Play on the player twice; record timing of both presses. Paste all `bt: evt`, `bt: media`, `bt: play intent` lines from 10 s before takeover to 10 s after the second Play; say what each press did. Stream policy is present but unwired: automatic recovery is not promised.

H. **Playlist Back:** open `01 - Britpop and 90s Indie`, play a track, press Back to the list; return to the player and Back again. Paste `cmd: playlist ...`, `cmd: source ...` and `cmd: back list rebuild ms=...`; say if audio dropped on either Back.

I. **Optional 4608-block FLAC:** if available, try one on compact; expect `flac: reject block=4608 max=4096`. This limit applies to compact; legacy retains the old limit.

**Coordinator decision:** compact stays default only if its `nextMaxUs` / `nonIoMaxUs` and underruns in C are no worse than legacy. Otherwise compact ships only together with the speed chunks.
