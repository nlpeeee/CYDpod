# CY-114: stop Bluetooth stealing the speaker (v31)
Branch `cydpod/bt-grab` from v1 (a0806cc). Host and compile checks only; no device run.

## Causes (v1 line numbers)
1. `bt.cpp:438-444`: `connectNow(btAddr)` every fixed 15 s while disconnected, forever; no cap or backoff, failed/busy pages not counted. `disc_rsn` ignored (`:88-107`, `:262-288`) and no "deliberate disconnect" state, so a speaker that let us go for a phone was paged again and switched back.
2. Boot double page: `bt.cpp:362` (library, plus its retry and fallback inquiry) and `bt_tick`.
3. `bt.cpp:453-459`: library default GENERAL_DISCOVERABLE, re-asserted every 5 s by `set_connectable(true)`; never NON_DISCOVERABLE. CY-36 hypothesis confirmed.
4. `pairing_session.h:20-24`: background pages used `Select`, so every other peer was rejected (all peers in Failed); Normal accepted any peer.
5. `pairing_session.h:27-52` Failed kept `pairing=true`; `bt.cpp:410-414` stale `pairResumeWanted`; `cancel()` after connect dropped the new device; `pendingKnown` was a list index.
Wired: `app.ino:152` never starts BT, so nothing pages while wired.

## Fix: `bt_reconnect_policy.h` (constants in one struct) used by `bt.cpp`
| Event | Result |
|---|---|
| Boot, saved speaker | library pages once; bt_tick waits 30 s; library retry/inquiry off from first connection event |
| Page refused, busy or silent (25 s) | failure n, wait 15/30/60/120 s; 5th failure stops |
| Peer ended link (DISC_RSN_NORMAL) | hold, never chase |
| Signal loss (ABNORMAL) | page after 5 s, then same backoff and cap |
| Owner Connect, Pair, Cancel | clears hold and count |
| Owner connect fails | stop |
| Forget connected speaker | hold |
| Connected, pairing, own connect, session busy | no page |

Also: `Page` command (`PairingSession::page`) never drops a link, accepts known devices, ends in Normal; Normal/Failed accept known devices only; discoverable only during a pair session; Failed clears `pairing` with real code (`BT_FAIL_RADIO`), Back in Failed runs Cancel; `cancel()` no-op when idle, `former` cleared on connect; `pairResumeWanted` dropped after 30 s without link; `pendingSwitch` holds an address.

## Not done
C1 (races), C4 (list cap). No Disconnect action (needs UI design; policy has `userStop`). Status line still says "Looking for X" after auto-reconnect stops. With no saved speaker the library still runs one 13 s boot inquiry (never connects). A speaker switched off cleanly is not chased; most reconnect on power-on.

## Checks
- `wsl sh tests/build_bt_policy.sh` (also added to check.ps1 host block):
  `PASS bt reconnect policy: refused-connect cap with 15/30/60/120 s backoff, ...`; `PASS live pairing: 20 cycles, ...`
- `check.ps1 -SkipProbe -SkipHostMedia` and `-Board jc2432w328c ...`: exit 0, each `PASS: compile-only checks completed; no device commands were run.` and `PASS single pool: ... 0000a000 ...` (40,960 B).

## Image (names bumped v30 to v31 in check.ps1)
`E:\dev\.worktrees\CYDpod\cy114-bt-grab\.tools\images\codec-pool-v31-jc2432w328c`; `app.ino.bin` 1,542,352 B, SHA256 `EDBE6AB54AC85693AD94D3789A671509ED61F0B306D1DB08656F83138A7D62B8`.
```powershell
$s='E:\dev\.worktrees\CYDpod\cy114-bt-grab\.tools\images\codec-pool-v31-jc2432w328c'
New-Item -ItemType Directory -Force E:\flash\v31|Out-Null
'app.ino.bootloader.bin','app.ino.partitions.bin','boot_app0.bin','app.ino.bin','MANIFEST.json'|%{Copy-Item "$s\$_" E:\flash\v31 -Force}
Copy-Item E:\dev\CYDpod\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe E:\flash\v31 -Force
cd E:\flash\v31
foreach($op in 'write-flash','verify-flash'){.\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin}
```

## Owner test
1. Play to the speaker, connect a phone to it: the player must not take it back (serial `bt: link ended (was up, normal)`, no `bt: paging saved device`).
2. Switch the speaker off and on: it reconnects by itself, or tap it in the list.
3. Out of range and back: at most 5 pages in about 4 minutes, then none until you tap it.
4. "CYD-MP3" must not appear in a phone's Bluetooth list unless the Pair screen is open.
