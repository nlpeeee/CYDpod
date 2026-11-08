# Adaptive SBC rate and true-wireless earbuds

Status: implemented on `cydpod/tws-earbuds`. It compiles, and the host tests pass. It has not been tested on the board. Background: `docs/research/tws-earbuds.md`.

## Bluedroid patch 0002 (`tools/bluedroid/patches/0002-a2dp-src-rate-control.patch`)

Patch 0002 is applied with 0001 to the trimmed build. The overlay in `dependencies.lock.json` (`bluedroidOverlay`) pins the result. Only `lib/libbt.a` and `versions.txt` differ from 0.5.4's trimmed package.

**Counters, read by `bt_diag.cpp`:**
- A2DP TX queue drops and its maximum length;
- media ticks that read less PCM than was due;
- L2CAP backlog maximum and stall events;
- AV-list drops;
- AVDTP writes.

**Encoder update:** the bitpool, bitrate, peer bitpool range, MTU and EDR flag.

**Rate control:**
- `cydpod_a2d_src_rate_kbps` sets the SBC target (0 = stock 328).
- Setting `cydpod_a2d_src_apply` makes the media task re-run `btc_a2dp_source_encoder_update()` at its next tick. That is the same update START runs, applied between packets, so the rate changes mid-stream.
- The peer's maximum bitpool still bounds the result.

## Packet packing (patch 0003, `tools/bluedroid/patches/0003-a2dp-src-full-packets.patch`)

Bluedroid's media timer ticks every 30 ms. At 44.1 kHz a tick holds about 10.3 SBC frames of 128 samples. A packet takes at most `floor((MTU-1)/frame length)` frames, capped at 15.

| SBC rate | Frame | Frames per full packet (MTU 1008) | Stock packets/s | Full packets/s |
|---|---|---|---|---|
| 328 kbps (bitpool 53) | 119 B | 8 (952 B) | about 67 (one full and one 2–3 frame packet per tick) | about 43 |
| 229 kbps (bitpool 35) | 83 B | 12 (996 B) | about 33 | about 29 |

How it works:
- `btc_get_num_aa_frame()` rounds each tick's frame count down to whole packets. The remainder stays in the PCM counter and goes out with the next tick, so no audio is lost.
- A count cut below one packet by the TX-queue limit is still sent, so a congested link drains as before.
- Extra latency is at most one packet: about 23 ms at 328 kbps.
- Every sink accepts packets up to its own negotiated MTU.

`btd pack on|off` switches it until reboot. The `stat: a2dlink` line shows `fpp`, the mean frames per packet over the last 10 s, and `pack`.

## Rate policy (`bt_rate_policy.h`, sampled once a second by `bt_diag.cpp`)

- **Steps:** 328, 292, 257, 229, 200, 165 kbps.
- **What counts:** only seconds in which packets were written. A pause is neither congestion nor clean time.
- **Down one step** after a 2 s window with:
  - more than 10 L2CAP stalls;
  - the TX queue at 13 or more of its 27 packets;
  - or any drop.
- **Start** every link at 328 kbps (owner's choice: full quality wherever it fits).
- **Up one step** after 60 s clean in total, back to 328 at most.
- **Memory:** `Settings::btLinks` (NVS key `btLinks`, up to 5 devices, matched by address) stores each device's rate when the link ends, and flags:
  - `EARBUDS`: the device has stepped below 328;
  - `DROPS_WHEN_IDLE`: it ended the link within 30 s of our idle suspend.
- **Manual control:**
  - `btd rate <kbps>` pins a rate until reboot, and `btd rate auto` returns to the policy;
  - `btd forget` clears the link memory.

## Quiet UI

`btlink_quietUi()` is true in either of these cases:
- the policy saw congestion on the current link in the last 10 s;
- `btd ui quiet` is set.

The `EARBUDS` flag is still recorded, but it no longer silences the animations.

`btd ui on` and `btd ui auto` override or restore it.

While it is true:
- the Now Playing marquee holds still on the "..." form;
- a list fling stops where the finger lifts.

Taps, navigation and every other redraw are unchanged.

## Link lifecycle (`bt.cpp`, `bt_reconnect_policy.h`)

- **Play reconnects.** A play intent on the Bluetooth route with no link and a saved device calls `ReconnectPolicy::ownerPlay()`:
  - it pages at once with a fresh budget;
  - it does nothing after a deliberate disconnect (`HeldUser`) or while a page is in flight;
  - the stream starts as the link comes up, as on any connect.
- **Peer suspend.** When the peer suspends a stream we are playing, CYDpod sends START again after 1 s, at most twice in 30 s.
- **Idle suspend.** It waits 10 s, or 60 s for a device flagged `DROPS_WHEN_IDLE`.
- **Inbound connections.** A connection from another paired device is still accepted and becomes the saved device (the owner's choice). It now logs `bt: switched to "X" (it connected)`.
- **Authentication.** A known device that authenticates with its stored key logs `bt: authenticated "X" (already paired)`. `bt: paired with` now means a new bond.

## Logs

- `bt: SBC rate N kbps (stock | remembered for this device | link backed up | link clean | pinned …)`
- `bt: remembered SBC rate N kbps for this device`
- `bt: play with no link: reconnecting "X"`
- `bt: the speaker suspended the stream; starting it again`
- `stat: a2dlink …` (every 10 s): `rate`, `policy`, `down`, `up`, `memo=rate/flags`, `quietUi`, plus the earlier link counters.
