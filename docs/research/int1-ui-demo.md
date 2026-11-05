# INT-1 / UI-DEMO: lane A (UI chain) + lane B (Search folding), one demo image

Branch `cydpod/integration-ui` (worktree `E:\dev\.worktrees\CYDpod\int1-demo`), created from `cydpod/ui-home` `eae8150` (CY-80, final code `8632627`).

## Merge

- **Merge commit `1ea7b7c48d85915561b790f4a681316656cd676c`**: `git merge --no-ff cydpod/search-fold` (`77ea4e52fd72d73a4e29c6dbe724e0be3237b446`, CY-64, base `c3ba8aa`). Merge base of the two lanes: `c3ba8aa`.
- **Conflicts: none.** The merge was clean (ort): `search_fold.h` (new, 64 lines), `storage.cpp` (+17/-9) and `tests/catalog_test.cpp` (+49) came from lane B; no lane A file was touched by it. No integration fix was needed: no include-order, duplicate-symbol or fixture breakage. CY-83's alpha backend is not merged.
- Later commits on the branch (tests/tools/docs only, no production code): `3f1c00b` and `31fc9b3` (render inventory + `tools/render_contact_sheet.py`), then this report.

## Checks (both exit 0, run sequentially on `31fc9b3`)

- `powershell -NoProfile -File tools/check.ps1` -> exit 0, ends `PASS: compile-only checks completed; no device commands were run.` Log `.tools/check-default.txt`. It includes `PASS CY-64 folding: Latin accents/case/ligatures, combining marks, CJK/Cyrillic exact, 35/25/31 boundaries, malformed UTF-8, CYDSRCH1 rejected/rebuilt`, the search/catalogue suites, `CY78_COMPACT_CHROME_OK`, `CY61_NOWPLAYING_OK`, `CY79_MINI_PLAYER_OK`, `CY80_HOME_TILES_OK`, the golden geometry table (`CLASSIC_GEOMETRY_GOLDEN`, unchanged: lane A's final `tests/golden/classic_layout.txt`) and `PASS firmware UI`, in both the Bluetooth-only and wired builds, plus `RENDER_INVENTORY_OK` (148 and 160 renders).
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` -> exit 0, ends `PASS: compile-only checks completed`. Log `.tools/check-jc.txt`.
- The tree was clean (`git status --short` empty) after the checks.

## Resource totals

Static RAM is "Global variables" (the linked total); flash is "Sketch uses". W328C = the image to flash.

| W328C player | v8 | v11 (CY-72) | v17 (CY-80 lane A tip) | **v13 (this)** | vs v17 | vs v8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Program storage (B) | 1,485,749 | 1,506,793 | 1,514,589 | **1,515,265** | **+676** | +29,516 |
| App image `app.ino.bin` (B) | 1,485,904 | 1,506,944 | 1,514,736 | **1,515,408** | **+672** | +29,504 |
| Static RAM (B) | 92,640 | 92,840 | 92,928 | **92,928** | **0** | +288 |
| Codec pool (B) | 40,960 | 40,960 | 40,960 | **40,960** (`codecPool` 0xa000) | 0 | 0 |

Default (original board) player: program storage 1,478,301 B (v17: 1,477,661, +640), static RAM 91,352 B (unchanged), app image 1,478,448 B. Slot limit 1,966,080 B: the W328C image uses 77 %. The merge adds no persistent heap; the CY-64 fold is a code/table change in the existing search key path (see `cy64-search-fold.md` on `cydpod/search-fold` for its own scratch accounting). Hardware heap and `stat: buf` audio health are unmeasured here; no audio, Bluetooth, I2S or touch-classifier code changed in this task.

## Image

Reserved tag **`codec-pool-v13-jc2432w328c`** (normal image, reads the saved output route): `E:\dev\.worktrees\CYDpod\int1-demo\.tools\images\codec-pool-v13-jc2432w328c\`. The check script writes the fixed name `codec-pool-v12-jc2432w328c`; the seven files were copied unchanged to v13 (read-only), and every file's size and SHA-256 in `MANIFEST.json` was re-checked against the actual file. The forced-Headphones companion is `codec-pool-v13-wired-jc2432w328c` (same four-region layout; app SHA-256 below). The `v12` directories in this worktree are build scratch: do not flash them.

| Flash region | File | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| 0x1000 | `app.ino.bootloader.bin` | 25,024 | `f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29` |
| 0x8000 | `app.ino.partitions.bin` | 3,072 | `0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7` |
| 0xe000 | `boot_app0.bin` | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| 0x10000 | **`app.ino.bin`** | **1,515,408** | **`c6ac111f26ea8cf0ebdfc70c0b7399b1ce5b4367682f7969f4eca8780e98f990`** |

Wired companion `app.ino.bin`: 1,515,552 B, `da44cb70ede05d803cad4b01f82182aa52538fa7f1fa805312f4bffe7d53d169` (bootloader, partitions and `boot_app0.bin` identical to the table). The bootloader/partitions/boot_app0 hashes are identical to the v8/v11 images. The image also holds `app.ino.elf`, `.map` and `.merged.bin` (4,194,304 B; do not write it: it would overwrite NVS).

## Flash and restore (for the user only; no device operation was run here)

The factory backup already exists; retain it. Identify the board's port yourself first (COM7 is the documented W328C port). Put the board in manual download mode (hold BOOT, tap RESET, release BOOT). Four regions, no erase, NVS and SD untouched.

```powershell
Set-Location E:\dev\.worktrees\CYDpod\int1-demo
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v13-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
```

Press RESET afterwards. Restore the known v8 player (manual download mode again, RESET afterwards; v8 `app.ino.bin` SHA-256 `ca47ab595aa12134e31c2475efb13f9170f1cff828c87de15645814eb98bce8b`, 1,485,904 B). A Headphones route saved by v13 reads as the old speaker route in v8 (saved 2 = `2 % 2 = 0`); select Bluetooth after rollback.

```powershell
$img = 'E:/dev/.worktrees/CYDpod/cy71-wired-route/.tools/images/codec-pool-v8-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
```

## Visual checklist for the board

Landscape 320 x 240 is the primary layout; rotate once to confirm portrait. Theme is the light Classic Touch default.

1. **Home tiles**: header "CYDpod" with the Bluetooth route icon; six tiles in two rows of three (Songs, Artists, Albums / Playlists, Search, Settings), icon above label, 4 px gaps; the mini-player strip underneath (empty queue: "Shuffle all"). Touch a tile and hold: it darkens, and opens only on lift.
2. **A list with 4 rows and the mini-player**: Songs shows exactly four 40 px rows (portrait: six), the page-up/down strip on the right, and the mini-player (pause, title/artist, Next, thin progress line) below. Swipe right about 1 cm along a row: Back, without playing. Drag up/down scrolls.
3. **Now Playing**: tap a song, then the mini-player text. Title/artist/album, seek bar with 1:24 / -2:36 style times, shuffle / previous / play-pause / next / repeat, **volume slider** (speaker icon, value) and the **output chip** at bottom right. No artwork. Drag the volume slider and the seek bar; the "..." button opens Options (Favourite, Shuffle, Repeat, Audio Output).
4. **Output setting**: Settings > Audio Output offers exactly **Bluetooth** and **Headphones** (one "Selected"), with the hint "Switch back to Bluetooth if there is no sound" under Headphones. Selecting the other restarts the player paused at the same position; the header icon becomes headphones and Bluetooth is not started.
5. **Search folding**: Home > Search; the keyboard shows `a..p` with `Q-Z` and `123` keys in landscape. Type `beyonce` against a track titled `Beyonc\é` (the "e" with an acute accent): the result list shows it, "1 results". The first search after flashing rebuilds the old `CYDSRCH1` cache once (the rebuild takes a moment; do it paused).
6. **Settings tree**: Settings > Playback, Display, Refresh Library, About, and Bluetooth (Pair new). "Upload / Advanced" still opens the legacy Setup page (retired later in CY-81).

## Render inventory (host layout evidence)

Produced by the existing host harness (`tests/classic_ui_test.cpp`, new `renderInventory()`; no new dependency) and `tools/render_contact_sheet.py` (Edge headless). Host SVGs use Arial and the host recorder, so they show **layout only**, not device bitmap-font quality or audio health. The recorder only knows width and height, so rotation 2 renders the same as 0 and rotation 3 the same as 1; touch geometry for all four rotations is covered by the golden table and the existing per-rotation tests.

- **Artifacts directory** (next to the image): `E:\dev\.worktrees\CYDpod\int1-demo\.tools\images\codec-pool-v13-jc2432w328c-renders\` with `svg\` (160 labelled SVGs, one per state and rotation, `<page>-<state>-r<rotation>.svg`), `inventory.tsv` (file, rotation, size, page, state, first on-screen labels) and the **inspected contact sheets** `sheet-r0.png` (portrait 240 x 320), `sheet-r1.png` (landscape 320 x 240), `sheet-r2.png` (portrait flipped), `sheet-r3.png` (landscape flipped) plus the matching `.html`.
- **Committed copies**: `docs/research/int1-render/inventory.tsv` and `sheet-r0..r3.png`.
- 40 states per rotation: Home (idle, playing mini-player, empty catalogue); Songs (populated, with mini-player, empty); Artists, Artist albums (child), Artist > Album songs (child), Albums, Album tracks (child), Playlists menu, Folders, Folder songs (each also empty where reachable); Search keyboard (empty, typed `beyonce`, landscape Q-Z page, digits), Search results (accented match `Beyonc\é`, "No matches"); Now Playing (volume slider, output chip); Options; Settings menu, Audio Output (Bluetooth selected, Headphones selected, from Options), Playback, Display, Refresh Library, About, Bluetooth (Pair new; "Bluetooth is off (output: Headphones)"); two confirmation dialogs; the "Unable to change output" notice; the legacy Setup page. Every capture asserts a marker label before it is saved. Existing route/navigation/touch tests are unchanged and still pass.
- **Excluded, unreachable**: `Mode::Volume` (the old standalone volume page). Its target `Volume` is never returned by the hit map (Now Playing owns the slider since CY-61), so no touch path reaches it; it is dead code that CY-81 can retire. Nothing else in the Classic mode list is excluded.
- **Favourites caveat**: the "Favourites" capture (`favourites-read-or-allocation-error-notice`) is *not* a normal empty list: opening Favourites with no favourites reaches the existing message page "Unable to read list or allocate memory. Try again." This is pre-existing behaviour (lane A tip) and was not changed. The normal empty browse states are the `empty-catalogue` captures (a card with no music).

## Findings (pre-existing, not fixed here)

1. **Empty-list text is hidden**: on a catalogue with no tracks, Songs / Artists / Albums / Folders record the label "No entries" but the first row's white fill is painted after it, so the host SVG shows a blank list (`songs-empty-catalogue-r*.svg`: text at y 92..108, then a `width=290 height=40` white rect over it). Search "No matches" is not affected. Worth a one-line draw-order fix in a later chunk; the real device will do the same.
2. **Long notice lines are clipped** in the host render ("Unable to read list or allocate memory. Try ..." in landscape): Arial is wider than the device font, so check these on the board.
3. Legacy Setup page rows are visibly truncated in portrait ("Bluetooth s", "Upload mu"); that page is slated for CY-81.

## Unverified on hardware

Everything above is host and compile evidence. Not yet seen on the W328C: the device bitmap-font text and the UTF-8 strip rendering of `Beyonc\é`; touch feel (swipe-back, tile press, slider); the one-time `CYDSRCH1` rebuild time on a real library; heap and `stat: buf` audio health with the merged build; Headphones audio and the actual AirPods Pro 2. AAC-LC 44.1 kHz stereo, AirPods Pro 2 and stereo line/headphone paths remain open acceptance requirements. No COM port, serial, flash or SD operation was performed.
