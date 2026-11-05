# CY-81: retire the legacy UI

Branch `cydpod/ui-retire`, based on `cydpod/integration-ui` `482a0b9` (INT-1, v13). User decision 2026-11-04: retire the old Setup screen and its Style, Layout, Background GIF, Invert and Panel rows, and the unused legacy player, library and sources screens. This chunk deletes code; every deletion below was preceded by a reachability proof.

**Result:** the legacy UI is gone; nothing still reachable was removed. Commits (oldest first): `84fcbed` Settings rework (nothing leads to Setup any more), `2cde4ed` legacy deletion, `e2fed29` unreachable Volume page, `b5d4729` geometry golden. Both required checks and the host suite pass; W328C program storage -26,160 B, static RAM -336 B; image `codec-pool-v18-jc2432w328c` (app SHA-256 `31fb67e9d996e66036d1abb14c556de4e2f577ee072472f44bccfd4a1979e106`). Nothing was flashed or run on hardware.

## 1. Reachability list (built before any deletion)

Method. (a) Code search for every `ui_go(SCR_*)` caller and every table entry. (b) A host navigation-graph test (`navigationGraph()` in `tests/classic_ui_test.cpp`): breadth-first search from Home over every hit target the real hit map exposes (`classic_target` scanned on a 4 px grid, every scroll position of a long list; the Bluetooth page, which has its own touch handler, on a 16 px grid), in all four rotations. Each probe replays its tap path from Home and asserts the replay lands on the same page, so no probe sees leftover state. Page class = screen + Classic mode.

**Before (base `482a0b9`, the same test run against the unmodified tree, `.tools/tmp/nav-before-final.log`):** 17 page classes in every rotation, one of them `Legacy.Setup`:

- `Classic.Settings -> Legacy.Setup` through the row "Upload / Advanced" (`external(SCR_SETUP)`).
- `Classic.Display -> Legacy.Setup` through the row "Panel / Advanced".
- `Legacy.Setup -> Bluetooth, Classic.Settings`.
- `Legacy.Player`, `Legacy.Library`, `Legacy.Sources` were never current: `ui_go()` redirected `SCR_PLAYER`/`SCR_LIBRARY`/`SCR_SOURCES` into Classic before entering them.
- `Classic.Volume` (the old Volume page) was never reached: nothing in `targetAt()` returns the `Volume` target.

| Item | Who could reach it | Verdict |
| --- | --- | --- |
| Setup screen (`screen_setup.cpp`, `SCREEN_SETUP`) | Settings "Upload / Advanced", Display "Panel / Advanced" (the BFS above), its own Back | **Reachable at base, made unreachable first** (commit `84fcbed`: both rows replaced, no `ui_go(SCR_SETUP)` left), then deleted |
| Setup rows Style, Layout, Background GIF, Invert, Panel | Setup only | Rows deleted with the screen (see section 2 for what happens to the stored values) |
| Setup rows Output, Bluetooth, Brightness, Orientation, Calibrate, About | Setup; the new Settings already had Output, Bluetooth, Brightness, About | Output/Bluetooth/About/Brightness exist in Settings; Orientation and Calibrate moved to Display (below) |
| Setup rows Upload music, Web passcode, Language | Setup only | **Kept**, moved to Settings > Library & System (they are working features with no other entry point) |
| Setup row Theme | Setup only | Removed with the other palettes: one palette (Classic Touch) remains; saved value ignored |
| Legacy player (`screen_player.cpp`, `SCREEN_PLAYER`) | `ui_go(SCR_PLAYER)` redirected to Classic; callers were `screen_bt.cpp` (3 returns) and Setup Back | Never entered. Deleted. The three Bluetooth returns now call `classic_return()` (same behaviour, tested) |
| Legacy library, sources (`screen_library.cpp`, `screen_sources.cpp`) | `ui_go(SCR_LIBRARY/SCR_SOURCES)`: zero callers outside tests | Never entered. Deleted |
| `layouts.cpp/.h` (skins), `tracklist.cpp/.h`, `ui_row`/`ui_pager`/`ui_chip`/`ui_switch`/`ui_bar`/`ui_numericFont` | Only the four legacy screens (grep of every symbol outside them) | Deleted |
| `background.cpp/.h` (GIF converter and player) | `bg_start` only from the legacy player; `bg_stop` calls from Classic, `ui_confirm`, `output_switch`; stats in `app.ino`. The boot-time `bg_convertAll` call was already gone | Deleted with its call sites |
| Styles 0-6, palettes 0-7 | Setup Style/Theme rows only | Deleted; the Classic Touch style and palette are constants |
| `Mode::Volume` (Classic page) | Nothing returns the `Volume` target; BFS never reaches it | Unreachable; removed in its own commit with the golden table update |
| Bluetooth page (`screen_bt.cpp`) | Settings > Bluetooth, boot when no speaker is known | **Kept** (reachable) |
| Calibration (`calibrate.cpp`) | Display > Calibrate touch (resistive only), 4 s hold | **Kept** |
| Uploader boot (`firmware_bootUploader`) | Was Setup > Upload | **Kept**, Library & System > Upload music |
| Panel/invert stored values, 8 s boot panel switch (`display.cpp`) | Hardware configuration | **Kept** (see section 2) |

After (tip): the same BFS reaches exactly 17 page classes in all four rotations: `Bluetooth` and `Classic.{Home, Catalog, Playlists, Sources, SourceSongs, Player, Options, Output, Search, Keyboard, Settings, Playback, Display, System, Refresh, About}` (`Classic.Message` is an error page and is not reached with the test card; `Classic.Volume` is gone). The test fails if any other page is reachable, any of these is not, or any screen id other than Classic and Bluetooth ever becomes current.

## 2. Settings after the retirement, and what happens to old saved values

Settings (landscape shows four rows, the list scrolls):

| Page | Rows | Notes |
| --- | --- | --- |
| Settings | Audio Output, Bluetooth, Playback, Display, Library & System, About | unchanged rows plus Library & System; "Upload / Advanced" is gone |
| Audio Output | Bluetooth, Headphones | CY-71/72, unchanged |
| Playback | Shuffle, Repeat, Screen timeout, Sleep timer | unchanged |
| Display | Brightness -, Brightness +, **Orientation**, Calibrate touch (resistive board only) | Orientation cycles all four rotations (landscape -> landscape flipped -> portrait -> portrait flipped) and shows its name. It replaces "Flip portrait", which could not return a W328C to landscape once Setup was gone. "Panel / Advanced" is gone. On the CST820 board there is no calibration row (it never had a working calibration) |
| Library & System | Refresh Library, Upload music, Web passcode (shown in full, tap = reset to 000000 after a confirm), Language (English / Chinese) | all four were reachable only through Setup |
| Folders / Favourites | under Playlists (CY-80) | unchanged |
| About | version, songs, free heap | unchanged |

Settings deleted: `style`, `skin` (Layout), `bgIndex` (Background GIF), `theme`, `timeMode` (the time readout mode of the legacy player). Their NVS keys (`style`, `skin`, `bg`, `theme`, `timeMode`) are **never read, written or erased**: whatever an older release left there is ignored without error and stays in flash so an older image flashed back finds its own values. `uiRev` migration is unchanged except that it no longer assigns theme/style/bg.

**Decision flagged (agreed by the coordinator): `panel` and `invert` are kept.** They describe the glass, not a look. Only their Setup rows are gone; the stored values still apply (`display.cpp`), the capacitive board still forces ST7789, and the 8 s boot-hold panel switch is untouched. Ignoring them would have changed colours or blanked the screen for an owner who had set them, with no UI left to undo it.

Not removed on purpose: `lang` (the Bluetooth, calibration and uploader pages and the uploader's web page still honour it), the web passcode, and the uploader image's `/bg` GIF handling in `uploadmode.cpp`/`upload_page.h` (see section 6).

Migration test (`tests/settings_route_test.cpp`, built by `tests/build_wired_fade.sh` in the wired-board, forced-route and no-module configurations, all `SETTINGS_ROUTE_OK`): a blob in the previous release's layout (retired keys with values, `invert`, `panel`, a 7-byte `known` blob of the wrong size, an unknown future key) loads cleanly; live values load; saving does not touch the retired keys or the unknown key; a second boot reads the same values; a blob that never ran the Classic migration still gets it.

## 3. Deletions

Files deleted (lines): `screen_setup.cpp` 323, `screen_player.cpp` 417, `screen_library.cpp` 184, `screen_sources.cpp` 122, `layouts.cpp/.h` 147, `tracklist.cpp/.h` 82, `background.cpp/.h` 513 (GIF converter, player, their buffers), `style.cpp` 80. Pruned: `ui.cpp` (292 lines out: row/pager/chip/switch/bar/numeric-font helpers, six of seven button shapes, shadow/bevel branches, `ICON_LIST`, `ICON_FOLDER`, `ICON_VOL_*`, `ui_slider`), `ui.h`, `style.h` (now three constants: radius 4, inset 3, border 1 = the Classic Touch row), `theme.cpp/.h` (one palette; the other eight and the name fields removed), `tokens.h` (five constants left), `settings.h/.cpp`, `i18n.h/.cpp` (90 -> 48 strings; 42 had no user left), `classic_layout.h` (slider/step rectangles), `classic_screen.cpp` (Volume page).

Necessary glue outside the owned UI files (each minimal): `app.ino` (background include, the unused `conversionProgress`, the `bg` fields of the `diag:`/`stat:` serial lines — the `stat:` line loses `bg %d %u.%u fps %u ms`, everything else on it is unchanged); `output_switch.cpp` (the `bg_stop()` call and include; restart ordering untouched); `screen_bt.cpp` (the three `ui_go(SCR_PLAYER)` returns became `classic_return()`; pairing and cancel logic untouched); `titles.cpp` (stops pre-rendering theme and style names; the Chinese-strip marker changes once, so a card with Chinese strips re-checks them at the next boot).

Retained on purpose, not dead: `src/AnimatedGIF/*` (third-party, still compiled into the sketch; nothing references it now and the linker drops it, per the coordinator's instruction not to touch third-party source), and the uploader's `/bg` GIF upload/list/delete page. That page now uploads files nothing plays; removing it means editing `uploadmode.cpp`/`upload_page.h` (not in this chunk's ownership). Recommended follow-up.


## 4. Flash and static RAM saved

Both numbers are the compiler's own lines from the final `check.ps1` runs (tip `b5d4729`), compared with INT-1's v13 table.

| | v13 (base) | v18 (this) | delta |
| --- | ---: | ---: | ---: |
| W328C program storage ("Sketch uses") | 1,515,265 B | **1,489,105 B** | **-26,160 B** |
| W328C app image `app.ino.bin` | 1,515,408 B | **1,489,248 B** | -26,160 B |
| W328C static RAM ("Global variables") | 92,928 B | **92,592 B** | **-336 B** |
| Default (original board) program storage | 1,478,301 B | **1,451,997 B** | **-26,304 B** |
| Default static RAM | 91,352 B | **91,016 B** | **-336 B** |
| Codec pool | 40,960 B (`codecPool` 0xa000) | 40,960 B (0xa000) | 0 |
| Slot limit 1,966,080 B | 77 % | **76 %** | |

Positive on both counts. Persistent heap: nothing added and nothing removed (the GIF player only allocated while a background played, which no screen started any more). Audio health: no audio, I2S, Bluetooth, catalogue or touch-classifier code changed; the Classic tick (one dirty row per pass, row loads gated on `audio_bufferLow()`, the 1 s mini-player progress, the 250 ms Now Playing tick) is the same code, and the new Settings pages only draw when opened, so `stat: buf` health is unchanged by reasoning. The only change on the `stat:` serial line is that the dead `bg ...` fields are gone.

## 5. Tests

Final host suite (`tests/build_catalog.cmd`, default and wired variants), `tests/build_wired_fade.sh` (includes the settings suites) and both `check.ps1` runs pass (exit 0, section 7).

Added: `navigationGraph()` (BFS, 4 rotations, 17 pages; fails on any unexpected or missing page, any retired screen id, or a non-deterministic replay); `settingsRegression()` (live rows present and retired rows absent in all rotations, Orientation visits all four rotations and returns, brightness, Library & System rows including passcode reset and cancel, language cycle and save, Upload without the uploader says so, Refresh opens from Library & System); `bluetoothReturns()` (Back, successful pairing, refused and accepted cancel, the boot-time page and the Bluetooth-off page, all rotations; the pairing and cancel shims are now controllable); previous-release settings blob migration (three configurations).

Removed or changed because they only covered deleted code:

- `ui_go(SCR_LIBRARY)` / `ui_go(SCR_SOURCES)` "legacy route reaches Home" checks (screens and ids deleted).
- The "advanced settings bridge" check and the legacy-Back-returns-to-scrolled-Settings check (Setup deleted; replaced by "Library & System opens from Settings / Back returns to Settings").
- The radio-free check over `SCREEN_SETUP` and all three `SCREEN_PLAYER` skins (replaced by the same zero-`bt_*` check over Playback, Display, Library & System and About on Headphones).
- The nine-theme header-contrast loop (one theme now; same assertions, one pass; the Dark snapshot `cy78-dark-home.svg` is no longer produced).
- F-12 "background .anim handle released when Search opens" (no background exists).
- Shims for `bg_*`, `SCREEN_LIBRARY`, `SCREEN_SOURCES`; the host build lists no longer compile `screen_player`, `screen_setup`, `layouts`, `tracklist`, `style`.
- Render inventory: the four `setup-legacy-advanced-bridge` captures are replaced by four `system-menu` captures; the count stays 160.

Golden tables: `classic_dialog.txt` unchanged. `classic_layout.txt` changed only where the deleted Volume page had entries (own commit `b5d4729`, reason in its message).

Visual regression against INT-1's inventory: all 160 captures were re-rendered and compared with the base build's as a normalised draw-op multiset (clip ids normalised, order ignored, because the paint order of the row waterfall depends on test timing): **136 identical**; 20 differ and all 20 are expected (Settings x4, Display x4, Refresh Library confirm x4, and the two confirm dialogs x8, which paint the Settings list behind them with the new 6-row scroll thumb); 4 `setup-legacy` captures are gone and 4 `system-menu` are new. Of the other 32 SVG snapshots, 31 match; `ui-settings.svg` differs as expected. This is a layout check on the host drawing recorder (host Arial), not a pixel compare on the panel.

## 6. Follow-ups found, not done (no added scope)

1. The uploader's web page (`upload_page.h`, `uploadmode.cpp`) still offers GIF upload/list/delete under `/bg`; nothing plays those files now. Remove together with the retained third-party `src/AnimatedGIF`.
2. `Classic.Message` (error page) is not reached by the BFS fixture because the test card has favourites; the existing Favourites-error render covers it.
3. The Bluetooth page still uses the 30 px legacy header, 56 x 30 Back and 28 px rows (CY-76 small targets); its return paths are now tested.
4. The `theme` NVS key is left in flash. CY-76 G (black and high-contrast themes) should use a new key name rather than reading the old byte, whose values meant the retired palettes.
5. `ICON_NONE` has no user; the Display page has no brightness slider (CY-76 F).
6. INT-1's documented follow-ups (empty-list draw order, long notice) are untouched.

## 7. Checks

Run sequentially in this worktree at tip `b5d4729`:

- `powershell -NoProfile -File tools/check.ps1` -> exit 0, ends `PASS: compile-only checks completed; no device commands were run.` (log `.tools/check-default.txt`; includes `build_wired_fade.sh`, the settings migration suites and the host media suites).
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` -> exit 0, same final line (log `.tools/check-jc.txt`).
- `tests\build_catalog.cmd` -> exit 0 (`RENDER_INVENTORY_OK: 160 SVG renders`, `HEADPHONES_UI_OK`, `PASS CY-81 Settings`, `PASS CY-81 Bluetooth returns`, `NAV_GRAPH` x4).
- Tree clean after the checks apart from this report.

## 8. Image `codec-pool-v18-jc2432w328c`

Normal image (reads the saved output route) at `E:\dev\.worktrees\CYDpod\cy81-retire\.tools\images\codec-pool-v18-jc2432w328c\`. The check script writes the fixed name `codec-pool-v12-jc2432w328c`; its seven files and `MANIFEST.json` were copied unchanged to v18, and every file's size and SHA-256 was re-verified against the manifest (7 of 7). The forced-Headphones companion `codec-pool-v18-wired-jc2432w328c` was copied the same way (not re-verified, not the one to flash). INT-1's `codec-pool-v13-jc2432w328c` artifacts were not touched.

| Flash region | File | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| 0x1000 | `app.ino.bootloader.bin` | 25,024 | `f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29` |
| 0x8000 | `app.ino.partitions.bin` | 3,072 | `0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7` |
| 0xe000 | `boot_app0.bin` | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| 0x10000 | **`app.ino.bin`** | **1,489,248** | **`31fb67e9d996e66036d1abb14c556de4e2f577ee072472f44bccfd4a1979e106`** |

Bootloader, partitions and `boot_app0.bin` are byte-identical to v13. Not flashed by this worker (no COM port, no esptool).

**For the user only; workers never run these.** The factory backup exists and the restore image is `codec-pool-v8-jc2432w328c`. Manual download mode (hold BOOT, tap RESET, release BOOT), then:

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy81-retire
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v18-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
```

Press RESET afterwards. NVS and the SD card are untouched. Restore: the same two commands with `$img = 'E:/dev/.worktrees/CYDpod/cy71-wired-route/.tools/images/codec-pool-v8-jc2432w328c'` (or INT-1's v13 for the previous UI; v13 and v18 read the same NVS: v18 ignores the retired keys and v13 still uses them).

## 9. Visual checklist on the board (landscape W328C)

1. Boot: Home shows six tiles; no background preparation screen can appear any more.
2. Home > Settings: rows **Audio Output, Bluetooth, Playback, Display, Library & System, About** (four visible, scroll for the rest). No "Upload / Advanced".
3. Settings > Display: **Brightness -, Brightness +, Orientation** (detail shows "Landscape") and **no** Calibrate touch row on this capacitive board. Tap Orientation four times: landscape flipped, portrait, portrait flipped, back to landscape; each saves. Nothing says Flip portrait or Panel.
4. Settings > Library & System: **Refresh Library** (opens the rebuild confirm), **Upload music** (restarts into the uploader, or says "Uploader not installed"), **Web passcode** (the six digits in the detail; tap, Cancel keeps it, Reset sets 000000), **Language** (English / the Chinese name; switches the Bluetooth and calibration pages).
5. Settings > Bluetooth: opens the pairing page; Back returns to Settings (not Now Playing). Pair a speaker from there: the page closes by itself back to Settings.
6. Settings > Playback, Audio Output, About, and Playlists > Folders / Favourites: as in v13.
7. Anything you used from the old Setup that is not listed above: Style, Layout, Background, Invert, Panel and Theme are intentionally gone. If colours look inverted or wrong after the update, say so: the stored invert and panel values are still applied.

