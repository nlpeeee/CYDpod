# CY-80: Home as six tiles, flatter navigation, swipe-back (UI-4)

Branch `cydpod/ui-home`, worktree `E:/dev/.worktrees/CYDpod/cy80-home`, exact base `cydpod/ui-mini` at `664702b` (CY-79). Firmware code is commit **80a** `8632627`; **80b** `11d2164` holds the tests; the geometry golden is its own commit **80c** (message states the reason). Owned files only: `classic_screen.cpp`, `classic_layout.h`, `classic_navigation.h`, `tests/classic_ui_test.cpp`, `tests/golden/classic_layout.txt` and this report. No touch classifier, storage, audio, Bluetooth, I2S, vendor or sibling worktree changes.

## What changed

- **Home is six tiles**: Songs, Artists, Albums / Playlists, Search, Settings (row-major), 3 x 2 on landscape and 2 x 3 on portrait, above the unchanged CY-79 mini-player. The old five Home rows (Music, Now Playing, Shuffle Songs, Settings, Search Songs) are gone; Now Playing and Shuffle are reached through the mini-player (text opens Now Playing, an empty queue shows **Shuffle all**).
- **Tiles**: rounded face in the band colour with an outline, icon, centred label. Touch-down paints the theme `pressed` face; the action fires on tolerant release (CY-62 rule, `SlopNear` 8 px, leaving by more than `SlopFar` 24 px cancels for good). A press that lands in a gap has no target and never reaches a tile; slop only ever counts the captured tile.
- **Icons**: Songs = `ICON_NOTE`, Playlists = `ICON_STAR`, Settings = `ICON_GEAR` from `ui_icon()`. `ui_icon()` has no person, album or magnifier shape and `ui.cpp` is not in this chunk's ownership, so Artists, Albums and Search are drawn in `classic_screen.cpp` from plain `fillCircle/fillRoundRect/drawRect/drawLine` calls (`tileGlyph`), the same approach as the CY-79 deep-Home glyph. Sizes are `TileIconSize` etc. in `classic_layout.h`. No new `Icon` enum values.
- **Flatter navigation**: the intermediate Music page (`Mode::Music`) is removed. Tiles open Songs, Artists and Albums directly (depth 2), Playlists (depth 2) or Settings (depth 2); Search keeps the existing Search results + Keyboard pair (depth 3; the CY-64 Search folding belongs to INT-1). The mode slot is now `Mode::Playlists`: rows **Favourites**, **Folders**, then the user's M3U playlists. Favourites opens the existing favourites preview, Folders the existing folder list (`Mode::Sources`, depth 3), so the old Folders/Favourites routes keep working until CY-81. The legacy `SCR_LIBRARY`/`SCR_SOURCES` entry points (`classic_prepareLibrary`) now land on Home.
- **Swipe-back**: a release at least `SwipeBackPx` = 40 px to the right of the touch-down point with horizontal travel greater than vertical (`dx > |dy|`) acts as Back. It is a shortcut only; the Back button and the CY-62 release rules are unchanged. It applies on browse pages at navigation depth > 1 (not Player, not Keyboard), only when the touch started in the body (below the 36 px header, above the 44 px mini-player, left of the 30 px paging strip). Arbitration:
  - `Gesture` gains a `vlead` latch (CY-62 vertical-lead rule: `dy >= 10 && dy >= dx`). Once a press has led vertically it belongs to list scrolling for good and can never become a swipe.
  - Crossing 40 px claims the press, so the classifier's tap event is swallowed and no row (or tile) activates. A list drag that claims earlier (dx >= 16) already does the same.
  - Slider and seek targets return before the check (the seek bar is on Player, which is excluded); mini-player and header touches are excluded by their start position.
  - On Home (depth 1) there is no parent, so swiping a tile does nothing and opens nothing. The only tile page, Home, therefore has no swipe action; the shortcut works from every list page below it.

## Geometry (`x,y,w,h`)

All numbers are in `classic_layout.h` (`TileMargin 8`, `TileGap 4`, `TileTop 8`, `TileBottom 4`, `TileRadius 8`, `tile<R>(w,h,i)`). Left/top margins follow proposal C; the right margin is one gap. Rotations 1 and 3 share the landscape rectangles, 0 and 2 the portrait ones. Header 36 px and mini-player 44 px are unchanged, so Home body height is 160 px landscape / 240 px portrait.

| Tile | Landscape 320 x 240 (rot 1, 3) | Portrait 240 x 320 (rot 0, 2) |
| --- | --- | --- |
| 0 Songs | 8,44,100,72 | 8,44,112,73 |
| 1 Artists | 112,44,100,72 | 124,44,112,73 |
| 2 Albums | 216,44,100,72 | 8,121,112,73 |
| 3 Playlists | 8,120,100,72 | 124,121,112,73 |
| 4 Search | 112,120,100,72 | 8,198,112,73 |
| 5 Settings | 216,120,100,72 | 124,198,112,73 |
| Gaps | 4 px between columns and rows | 4 px between columns and rows |
| Margin to mini | 4 px (192 -> 196) | 5 px (271 -> 276) |
| Mini-player | 0,196,320,44 | 0,276,240,44 |

Every tile is at least 100 x 72 (landscape) or 112 x 73 (portrait), above the 80 x 64 minimum. The golden diff is only the Home mode line and the `rects=` line of each rotation (8 lines); Playlists renders the same hit map as the old Music slot, and every other mode, the Player, mini-player, keyboard and strip rectangles are byte-identical to CY-79.

## Tests (`tests/classic_ui_test.cpp`, run in both the default and the wired host variants)

`CY80_HOME_TILES_OK`, for rotations 0/1/2/3:

- Exact tile rectangles (landscape and portrait), minimum size, inside header/mini-player, gaps kept, no overlap.
- Hit map for every tile: centre, four outer corners, one pixel outside each side is *not* that tile, every pixel of every column/row gap returns no target, the right tile column is not swallowed by the paging strip, the mini-player stays below.
- Navigation depth after each tile (2, 2, 2, 2, 3 for Search, 2), header names the tile, no "Music" label ever, Back from each tile (repeated for Search) returns to Home at depth 1. Playlists lists Favourites and Folders; Playlists > Folders and Playlists > Favourites reach depth 3 and Back returns. The legacy Library/Sources routes land on Home.
- Tile press: pressed ink appears on touch-down with no navigation, release 7 px beyond the edge acts, 14 px outside cancels, a far excursion cancels even after returning, a press landing in a gap never reaches a tile, slop from one tile into the next still opens the captured tile, jittered finger inside a tile opens it exactly once (real classifier harness).
- Swipe-back: 39 px does nothing (and does not play the row), exactly 40 px goes Back and swallows the row tap, 100 x 30 diagonal goes Back, 45 degree and leftward drags do not, a vertical lead followed by 60 px right does not (latch), vertical drag still scrolls, 9 px vertical jitter does not block, harness runs at 10 ms and 80 ms polling (90 px travel with 5 px jitter goes Back once without playing; vertical scroll with sideways jitter stays; small jitter is still a row tap), non-scrolling Playback page swipes back without cycling Shuffle, header and mini-player starts are not swipes, the Back button still works, a horizontal drag on the Player seek bar seeks and is not Back, swiping a Home tile opens nothing.
- Existing tests were retargeted to the new Home through the real tile/mini hit targets (`tapTile`, `tapMini`): CY-43, CY-50, CY-61, CY-62, CY-71, CY-78 and CY-79 assertions all still pass unchanged in intent. The Music-based "scrolling list" cases now use Songs (10 rows).

## Validation

Both required checks ran once, sequentially, on final code (`8632627`, tests/golden afterwards) and exited **0**:

```text
powershell -NoProfile -File tools/check.ps1
  PASS: compile-only checks completed; no device commands were run.
  DEFAULT_CHECK_EXIT=0 (7 min 36 s)
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia
  PASS: compile-only checks completed; no device commands were run.
  JC_CHECK_EXIT=0
```

Output: `.tools/logs/cy80-default-final-check.txt` and `.tools/logs/cy80-jc2432w328c-final-check.txt` (both contain `CY80_HOME_TILES_OK`, `CY79_MINI_PLAYER_OK`, `CY61_NOWPLAYING_OK`, `CLASSIC_GEOMETRY_GOLDEN`). The golden is compared, not regenerated, by the final checks. Development host runs (one regenerating the golden for commit 80c) used the same classic UI test build.

Rendered snapshots (host Arial placement evidence, not the device bitmap font): `tests/out/cy80-home-landscape.svg`, `cy80-home-landscape3.svg`, `cy80-home-portrait.svg`, `cy80-home-portrait2.svg`; inspected as `.tools/cy80-render/cy80-home.png`. They show a 3 x 2 grid with 4 px gaps, centred labels clear of the icons, the Bluetooth route icon in the header and the CY-79 mini-player (pause, clipped title/artist, Next, 2 px progress) under the tiles in every rotation.

## Resource budget

| Normal player | CY-79 | CY-80 | Delta |
| --- | ---: | ---: | ---: |
| Default program storage (B) | 1,476,765 | 1,477,661 | +896 |
| Default app image (B) | 1,476,912 | 1,477,808 | +896 |
| Default static RAM (B) | 91,352 | 91,352 | **0** |
| W328C program storage (B) | 1,513,617 | 1,514,589 | +972 |
| W328C app image (B) | 1,513,760 | 1,514,736 | +976 |
| W328C static RAM (B) | 92,928 | 92,928 | **0** |
| Codec pool, both targets (B) | 40,960 | 40,960 | **0** |

Linked static RAM is unchanged (two new small statics, `tilePressed` and the `vlead` flag, fit existing alignment padding). No persistent heap, no metadata cache, no codec allocation. Home paints six tiles on entry and on press/release of one tile; it adds no periodic drawing, and the mini-player progress remains the CY-79 1 Hz two-rectangle update. Home has no list rows, so there is no per-tick row loading there. This supports unchanged audio scheduling by inspection only; no `stat: buf`/underrun measurement was made.

## Not verified on hardware

Everything above is host evidence. Not yet seen on the W328C: real touch feel of the 40 px swipe with the CST820 (the finger path is smoothed by the classifier, so a swipe that ends exactly at 40 px can read one pixel short), tile text/glyph rendering with the device bitmap font, tile pressed-ink visibility on the real panel, and audio health while paging. AAC-LC 44.1 kHz stereo, actual AirPods Pro 2 and Headphones playback remain hardware acceptance requirements.

## Image

Normal player (saved output route), reserved tag **codec-pool-v17-jc2432w328c**: `E:/dev/.worktrees/CYDpod/cy80-home/.tools/images/codec-pool-v17-jc2432w328c/app.ino.bin`, **1,514,736 B**, SHA256 `d3e5b7b0706baf9da92f750705bc901a0bfec24d89a2d9fb86586a079eca12aa`. The check script writes its fixed intermediate `codec-pool-v12-jc2432w328c`; the seven files were copied to v17, `MANIFEST.json` regenerated with `imageTag`, source `86326278bfcd9a8c0d9c5616f3d6b93763903830` (80a, last firmware change) and base `664702b`, and every file re-verified for bytes and SHA256 (`.tools/logs/cy80-manifest-verification.txt`, v17 `MANIFEST.txt`).

| Flash region | File | Bytes |
| --- | --- | ---: |
| 0x1000 | `app.ino.bootloader.bin` | 25,024 |
| 0x8000 | `app.ino.partitions.bin` | 3,072 |
| 0xe000 | `boot_app0.bin` | 8,192 |
| 0x10000 | `app.ino.bin` | 1,514,736 |

## Visual checklist for the board (later integrated run)

- **Home, landscape**: header "CYDpod" and route icon; six tiles in two rows of three (Songs, Artists, Albums / Playlists, Search, Settings), each with an icon above its label, 4 px gaps, mini-player strip directly below. Rotate: portrait shows two columns by three rows.
- **Press feel**: touch a tile and hold: the face darkens (pressed colour) immediately; nothing opens until lift. Slide off the tile and lift: nothing opens. Lift in the gap just beside the tile you pressed: that tile still opens.
- **Straight through**: Songs, Artists and Albums open their list directly with Back naming "Home". There is no "Music" page anywhere. Playlists shows Favourites, Folders, then your playlists; Folders and Favourites still work. Search opens the keyboard.
- **Swipe-back**: on any list (Songs, Settings, Playback) drag right about 1 cm along a row: it goes Back and does not play/open that row. Drag up/down: the list scrolls and never goes Back, even if the finger drifts right afterwards. On Home a rightward swipe does nothing.
- **Exempt**: dragging along the Now Playing seek bar seeks as before; the mini-player and header are not swipe starts; the Back button still works.
- Mini-player on Home: play/pause and Next act on touch-down, text opens Now Playing, empty queue shows **Shuffle all**.

## Flash entry (documentation only; not executed)

Factory backup already exists per `BUILD-CYDPOD.md`; retain it. COM7 is the documented W328C port and the user must identify the port before the integrated run. Manual download mode (hold BOOT, tap RESET, release BOOT), `--before no-reset --after no-reset`, four regions, no erase. Restore image `codec-pool-v8-jc2432w328c` (`E:/dev/.worktrees/CYDpod/cy67-jc2432/.tools/images/codec-pool-v8-jc2432w328c`), same four-region commands. Neither operation formats the SD card or clears NVS. No intermediate flash is requested; INT-1 follows.

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy80-home
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v17-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
# Press RESET after verification. For rollback set $img to the v8 directory above.
```

## Notes

No COM port, serial, flash or SD operation was performed. The workspace-comment update is not available for this externally created worktree (as in CY-79); checkpoints went through heartbeats.
