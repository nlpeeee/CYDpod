# CY-114: stop Bluetooth stealing the speaker (v31)
Branch `cydpod/bt-grab` from v1 (a0806cc). Host and compile checks only; nothing was run on a device.

## Causes (v1 line numbers)
1. **Page forever** `bt.cpp:438-444`: while disconnected and a saved speaker exists, `connectNow(btAddr)` every fixed 15 s, no cap or backoff. `disc_rsn` was ignored (`bt.cpp:88-107`, `262-288`), so a speaker that dropped us on purpose (phone took it) was paged again 15 s later and switched back, kicking the phone. Nothing recorded a deliberate disconnect. Failed, busy or refused pages were not counted.
2. **Boot double page** `bt.cpp:362` (library) plus `bt_tick` (nextReconnect=0). The library's own retry and fallback inquiry also ran.
3. **Always discoverable** `bt.cpp:453-459`: library default is GENERAL_DISCOVERABLE and `set_connectable(true)` re-asserted it every 5 s; nothing set NON_DISCOVERABLE. Answers the CY-36 hypothesis: yes, "CYD-MP3" stayed findable after pairing and SSP auto-confirms.
4. **Deaf inward** `pairing_session.h:20-24`: background pages used `Select`, so `accepts()` rejected every peer except the paged one (and all in Failed). A Normal session accepted any peer, known or not.
5. `pairing_session.h:27-52`: Failed kept `pairing=true` (paging off, discoverable, music paused). `bt.cpp:410-414,520`: stale `pairResumeWanted` auto-played later. `cancel()` after connect dropped the new device. `pendingKnown` was a list index.

Other paths: boot library page; STACK_UP inquiry only if no saved speaker (not removed, see below); owner Connect/Pair/Cancel/Forget; session 20 s/60 s deadlines. Wired: `app.ino:152` never starts BT, so nothing pages while wired; switching output restarts the board.

## Fix
New `bt_reconnect_policy.h` (pure, all constants in one struct), used by `bt.cpp`:

| Event | Result |
|---|---|
| Boot with saved speaker | library pages once; bt_tick waits 30 s; library retry/inquiry disabled at first connection event |
| Page refused / busy / no answer (25 s) | failure #n, wait 15/30/60/120 s; 5th failure stops |
| Peer ended link, DISC_RSN_NORMAL | hold, never chase |
| Signal loss, DISC_RSN_ABNORMAL | first page after 5 s, then the same backoff and cap |
| Owner Connect / Pair / Cancel / choose | clears hold and count |
| Owner connect fails | stop (screen shows Connect failed) |
| Forget the connected speaker | hold |
| Link up (either side) | count cleared |
| Connected, pairing, owner connect, switch pending, session busy | no page |

Also: background pages use new `Page` command (`PairingSession::page`): no pairing flag, never drops a link, accepts known devices (btKnown/btAddr), ends in Normal. Normal/Failed accept known devices only; strangers only during an explicit pair. Discoverable only while a pair session runs (`hide()` at begin and when the session ends). Failed clears `pairing`, real failure code (`BT_FAIL_RADIO`); Back in Failed runs Cancel (reconnects former). `cancel()` is a no-op when nothing is pending; `former` cleared on connect. `pairResumeWanted` dropped after 30 s idle with no link. `pendingSwitch` holds an address.

## Not done
- C1 (races on btAddr/names), C4 (device list cap 8). 
- "Disconnect" action: not added; needs a row/button design. Policy supports it (`userStop`). Forget of the connected speaker already holds.
- Status line says "Looking for X" after auto-reconnect gave up (tap the speaker to reconnect).
- With no saved speaker the library still runs one 13 s boot inquiry (never connects).
- Tradeoff: a speaker switched off cleanly is not chased; most reconnect to us on power-on (we accept known devices).

## Checks
- `wsl sh tests/build_bt_policy.sh`:
  `PASS bt reconnect policy: refused-connect cap with 15/30/60/120 s backoff, ...`
  `PASS live pairing: 20 cycles, ...` (now also page/accepts/cancel cases)
- `tools/check.ps1 -SkipProbe -SkipHostMedia` exit 0: `PASS: compile-only checks completed; no device commands were run.` (player 76%, `PASS single pool: ... codecPool`, 0xa000 = 40,960 B)
- `-Board jc2432w328c ...` exit 0: same PASS lines (`PASS single pool: 3ffc4ee0 0000a000 ...`).
- `build_bt_policy.sh` is also wired into check.ps1's host-media block. `check.ps1` image names bumped v30 to v31.

## Image
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
1. Play to the speaker, then connect a phone to that speaker. The player must not take it back (serial: `bt: link ended (was up, normal)`, no `bt: paging saved device`).
2. Power the speaker off and on: it should reconnect to the player by itself, or tap it in the list.
3. Walk out of range and back: up to 5 pages over about 4 minutes, then none until you tap the speaker.
4. From a phone, "CYD-MP3" must not be in the Bluetooth list unless the Pair screen is open.
