# Robust speaker volume sync

Date: 2026-11-07. Branch `cydpod/volume-sync` from 0.5.2, firmware commit 291c62d. This follows [the initial-sync work](bt-volume-initial.md).

## Report

The owner reported that on 0.5.2 the CYDpod volume still frequently goes out of step with the speaker. No serial capture of a desync exists yet, so the causes below come from the code. The new `stat: vol` line is there to confirm which of them happens on the board.

## What AVRCP allows

The source cannot read the speaker's volume. It learns the level from only two things:
- a `CHANGED` volume notification, which needs re-registering after each one;
- the reply to a set-absolute-volume command.

The pinned ESP-IDF build gives no sign of delivering the `INTERIM` response that a registration returns. The earlier observation agrees: no report arrived until a physical button press. The design therefore cannot rely on asking the speaker for its level.

## Failure modes found

1. **Late replies dropped.** If a reply took longer than 1 s, `response()` discarded it because the command was no longer in flight. The timeout then set the shown level back to the last report. The speaker was at the new level while CYDpod showed the old one.
2. **One-shot handshake.** One initial command that timed out (some speakers are slow just after connecting) left CYDpod in software-volume mode for the whole link. It then attenuated the PCM while the speaker kept its own gain, so the two controls compounded until a speaker button was pressed.
3. **The link depended on event order.** Volume sync started only if the AVRCP connection event matched the A2DP peer that had already been recorded, and a capability reply arriving before that was lost. If A2DP reconnected while AVRCP stayed up, no new AVRCP event arrived at all. Any of these left the whole connection without sync.
4. **Sleep.** The stack stopped without a disconnect event; fixed in 0.5.2 (52bf3b7).

## Change

- **`BtVolumeSync` (`bt_volume_sync.h`):**
  - A reply is applied whenever the link is up and a command has been sent, including after the timeout, and is counted as `late`.
  - On timeout, the requested level is kept as the presumed speaker level (the command was accepted) rather than reverting.
  - The initial handshake is tried 3 times, 1 s apart. If none is answered, a speaker that advertised volume notifications is treated as absolute anyway. A slider move during the retries is what gets sent next.
- **The link is decided in `bt_tick`:**
  - The link is up when AVRCP is up, A2DP is connected, and both are the same peer. This is checked on every pass.
  - A capability reply is remembered even if it arrives before the link is up. If none arrives within 1.5 s of linking, the capabilities are requested again, up to 3 times.
  - Sleep clears the AVRCP state.
- **Diagnostics:** every 10 s while connected:
  - `stat: vol link= avrc= caps= abs= init=state/tries inflight= pending= speaker=% shown=% remote= notes= replies= late= timeouts= retries= sends=`;
  - plus `bt: remote control up|down`, `bt: speaker volume control supported|not supported` and `bt: asking speaker for volume support again`.
- **Unchanged:** labels, coalescing, the 100 ms minimum interval, no notification echoes, PCM gain ownership, and the wired route.

## Remaining limit

If the speaker changes level twice in quick succession, the second `CHANGED` can land before CYDpod has re-registered. It is then missed until the next change. AVRCP offers no read-back to close this gap. The next speaker press, or slider move, realigns the two.

## Checks (once, on 291c62d)

- **Volume host suite:** `wsl -e sh tests/build_bt_volume.sh` (g++, plus the audio PCM-gain and diagnostics tests) PASS, log `.tools/logs/bt-volume-robust-host.log`. The new cases cover: a late reply after the timeout is applied; a timeout keeps the requested level; a retried handshake; an unanswered handshake handed over to the speaker; a slow first reply; a slider move during the retries; and replies with no command ignored.
- **Firmware compile-only:** `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel volsync` PASS, log `.tools/logs/volsync-check.log`. Sizes: Bluetooth 1,603,241 / 88,440 B, wired 1,603,369 / 88,448 B; static RAM +40 B over 0.5.2.

None of this is hardware evidence.

## Board checks (owner)

Use the JBL Flip 3 and the Xiaomi Sound Pocket. Keep the serial log open.
1. Connect, move the slider, then press the speaker's own buttons several times quickly. The shown volume should follow both. Paste the `stat: vol` lines.
2. Disconnect and reconnect from the speaker side and from CYDpod, then Sleep and wake. Each link should log `bt: remote control up`, `bt: speaker volume control supported` and `link=1 abs=1`.
3. Whenever the two disagree, note the time and paste the surrounding `stat: vol` and `bt:` lines. Rising `late`, `timeouts` or `retries`, or `link=0` while connected, show which path caused it.
