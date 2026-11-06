# CY-118: "Unable to change output" / "Pair new does nothing" (v34, no speaker saved)

Branch `cydpod/output-pair-fix` (from 2f49747). Fix 7dc88ce. No device operations.

## 1. Output switch: cause found and fixed
`output_switch_run` (`output_switch.h:14-24`) fails at `quiesce`, `park` or `save`.

| Rank | Stage | Evidence | Fires on every attempt with no speaker? |
|---|---|---|---|
| 1 | `quiesce` | `pending()` (`output_switch.cpp:15-18`) includes `c.opening`, which `audio_commands()` filled from `starting`. `starting` is set by `openTrack` (`audio.cpp:198`) and cleared only by `audio_pull` consuming frames while `playing && !seeking` (`audio.cpp:94,106`). `capture()` pauses first (`output_switch.cpp:21`), so nothing can clear it, and it waits 5 s then fails (`:24-25`). | Yes. Fresh boot: resume track opened, paused, and with no speaker nothing ever pulls. Same for a connected speaker that has not played since boot. |
| 2 | `park` | `speaker.cpp:166-185`: only if a DAC/wired writer stays unparked 3 s; BT has none. | No |
| 3 | `preferences` / `save` | `settings.cpp:105-147,177-201`: only on an NVS open/put error; empty strings and zero `btAddr` take checked paths. | No |

Owner datum fits: once audio plays, `audio_pull` clears `starting`. These files and the `starting` logic are identical in `v1`: **not a CY-114 regression**, a latent bug the erased, never-played state exposes. It also fails right after a track change or seek (`audio.cpp:172,198`).

**Fix** (14 lines): new `opening` flag, set inside the same critical section that consumes the request (`audio.cpp:254`), cleared when it ends (`:266`), and reported by `audio_commands()` (`:557`). `starting` keeps its underrun-counting role.

**Reproduction**: production `audio.cpp` host test `tests/audio_seek_test.cpp:131-132`. Before the fix: `Assertion '!audio_commands().opening' failed` after an opened, paused, un-pulled track. After: passes 3 of 3 runs.

## 2. Pair new: no defect found by reading; not fixed
The path is `screen_bt.cpp:221-223` -> `bt_requestPairing` (`bt.cpp:570-582`) -> `CydSource::Pair` -> `PairingSession::begin`. No guard reads `btAddr`, audio state, `startupComplete` or the CY-114 policy. `pairResumeWanted` is only set when audio is playing. Silent exits:
1. `bluetoothReady` false -> serial `cmd: pair ignored (Bluetooth not ready)`, toast "Bluetooth is not ready. Try again." Only if `bt_begin` has not run (`app.ino:222-224,293`).
2. **Most plausible (not proven):** `post()` fails (`cmd: pair ignored (queue busy)`, same toast). The library's stack-up blocks BtAppT for 10 s (`BluetoothA2DPSource.cpp:548`); `bt_tick` posts a Tick every 250 ms (`bt.cpp:408-409`) into a 20-slot queue (`BluetoothA2DPCommon.h:409`), so it is full after about 5 s and each post waits 10 ms. With no saved speaker the app opens the Bluetooth screen at boot (`needSpeaker`, `app.ino:237`), so the owner taps within that window. A saved-speaker owner is on the player screen. The same code is in `v1`.
3. The session starts and then fails: `bt: live pairing failed: <op>` shows "Connect failed" in the status line (`screen_bt.cpp:70-73`).

The pure session copes with zero `former` and a running boot inquiry (new block in `tests/pairing_session_test.cpp`, PASS), so the state machine is not the cause.

## 3. Common cause?
Not supported: output switch is the `starting` flag; Pair new has no audio dependency.

## Serial lines for the owner (reboot, tap Pair new after 15 s, then Output)
- `output-switch: failed stage=quiesce` (confirms 1); stats line `commands` showing `opening=1`.
- `cmd: pair queued` (path OK) vs `cmd: pair ignored (...)` (cases 1/2) vs `bt: live pairing failed: ...` (case 3).
- `heap: after audio startup ... ready 1` and `bt: started (live pairing)` at boot.
If Pair new still fails after a 15 s wait, send all `bt:` lines.

## Checks (final tip 7dc88ce)
- `tools/check.ps1 -SkipProbe` -> exit 0, PASS.
- `-SkipProbe -SkipHostMedia` (CYD) -> exit 0, PASS; `-Board jc2432w328c ...` -> exit 0, PASS.
- `sh tests/build_bt_policy.sh` -> PASS reconnect policy, PASS live pairing. `audio_test` -> exit 0 (3 runs).
