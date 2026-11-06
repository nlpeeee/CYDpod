# CYDpod v34 owner checklist (W328C)

v34 = v33 + logo splash. Serial monitor on COM7 throughout. Use a COPY of the card for section 6.

## 1. Flash
- [ ] Flash ONE way (`FLASH.md`). Single file `cydpod-v34-w328c.bin` at 0x0 **wipes settings** (speaker, volume, theme, Wi-Fi, last track); the four-region flash keeps them.

## 2. Logo splash (new)
- [ ] Splash background is yellow (0xFE83) with the dark wordmark.
- [ ] Wordmark is centred in portrait and in landscape (rotate in Settings and reboot).
- [ ] Note how long it is visible before the next boot message covers it (may be very brief: later boot messages fill the screen): ____ s.
- [ ] Colours look right on the real panel (no inverted/odd tint, edges clean).

## 3. Boot and play
- [ ] Boots to home. Log has `=== CYD MP3 player` and `heap: after init`.
- [ ] One track plays; `stat: heap ... underruns` stays 0 or near 0.

## 4. Flick (Long Songs list): 10 flicks, wait for each `stat: scroll` line
| # | Speed | `reject` | `coastRows` | Felt right? |
|---|---|---|---|---|
| 1-3 | slow | | | |
| 4-7 | medium | | | |
| 8-10 | hard | | | |
- [ ] Touch during a coast stops it (`aborted 1`); thumb drag works; list stops cleanly at both ends.

## 5. Bluetooth
- [ ] Pair and play to your speaker.
- [ ] Another phone connects to the speaker: player does NOT take it back (`bt: link ended (was up, normal)`, no `bt: paging saved device`).
- [ ] Power-cycle with the speaker on: it reconnects by itself.

## 6. Wired headphones (limit -30 dBFS)
- [ ] Limit On: steps 1-2 silent, 3 barely audible, 100 still quiet. Limit Off: much louder (turn volume down first).
- [ ] Toggle limit and switch wired/Bluetooth/wired: no pops.

## 7. 10,000 tracks (card COPY)
- [ ] First scan shows progress; note time. Log: `catalog: ... tracks`, `catalog: ... sorted`.
- [ ] Reboot: note seconds to home. Shuffle plays; Songs and queue scroll to both ends; Search finds a track.

## 8. Playlists
Copy `Alternative Wedding - UK Nostalgia` to `/music/` and the 10 `.m3u8` files to `/playlists/`.
- [ ] Each playlist prints `cmd: playlist <name> n= found= missing=` (found close to n); one plays in order; shuffle works.

Not in this build: Wi-Fi upload (the uploader image at 0x1f0000 is blank), F5 boot-loop fix, sleep timer and F3-F23.

## Paste back
1. All `stat: scroll` lines  2. All `cmd: playlist` lines  3. Boot log up to `heap: after init`  4. One `stat: heap ... underruns` line
