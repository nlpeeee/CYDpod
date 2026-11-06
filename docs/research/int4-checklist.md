# CYDpod v33 owner checklist (W328C)

Compared with what you last tested (v1/v30). Serial monitor on COM7 for every step. Use a COPY of the card for section 6.

## 1. Flash
- [ ] Flash ONE way (commands in `FLASH.md`). **Single file `cydpod-v33-w328c.bin` at 0x0 WIPES settings** (saved speaker, volume, theme, Wi-Fi, last track). The four-region flash keeps them.
- [ ] After a four-region flash the volume and saved speaker are still there; after the single file they are defaults (re-pair).

## 2. Boot and play
- [ ] Boots to home. Log has `=== CYD MP3 player` and `heap: after init`.
- [ ] One track plays; `stat: heap ... underruns` stays 0 or near 0.

## 3. Flick (Long Songs list)
Flick 10 times, wait for each `stat: scroll` line, fill in:

| # | Speed | `reject` | `coastRows` | Felt right? |
|---|---|---|---|---|
| 1-3 | slow | | | |
| 4-7 | medium | | | |
| 8-10 | hard | | | |

- [ ] A touch during a coast stops it (`aborted 1`). Dragging the scroll thumb moves the list; the list stops cleanly at both ends.

## 4. Bluetooth
- [ ] Pair and play to your speaker.
- [ ] Connect another phone to the speaker: the player does NOT take it back (`bt: link ended (was up, normal)`, no `bt: paging saved device`).
- [ ] "CYD-MP3" shows in a phone's Bluetooth list only while the Pair screen is open.
- [ ] Power-cycle the player with the speaker on: it reconnects by itself.

## 5. Wired headphones (limit -30 dBFS)
- [ ] Limit On: slider steps 1-2 silent, step 3 barely audible, 100 still quiet.
- [ ] Limit Off: much louder (-6 dBFS); turn the volume down first.
- [ ] Toggle the limit and switch wired/Bluetooth/wired: no pops.

## 6. 10,000 tracks (card COPY)
- [ ] First scan: note the time and that progress shows. Log: `catalog: ... tracks`, `catalog: ... sorted`.
- [ ] Reboot: note seconds to home (`heap: after init`).
- [ ] Shuffle plays; scroll Songs and the queue to both ends; Search finds a track.

## 7. Playlists
Copy the `Alternative Wedding - UK Nostalgia` folder to `/music/` and the 10 `.m3u8` files to `/playlists/`.
- [ ] Open each playlist; each prints `cmd: playlist <name> n= found= missing=` (found should be close to n).
- [ ] One plays in order; shuffle works.

## 8. Upload page
- [ ] Upload a file named `it's a test.mp3`: it lists, no script runs, delete works.

Not in this build: F5 boot-loop fix, sleep timer and F3-F23.

## Paste back
1. All `stat: scroll` lines  2. All `cmd: playlist` lines  3. Boot log up to `heap: after init`  4. One `stat: heap ... underruns` line
