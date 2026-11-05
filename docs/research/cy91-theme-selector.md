# CY-91: selectable Light, Black and High Contrast themes

Branch `cydpod/theme-selector`, based on CY-90 `4474418`. Code commit `07911d7`, export tags `404beb4`,
this report and the five previews on the final commit. No device, COM, serial, esptool, flash, SD or
user-media operation was performed. Per the owner's 2026-11-05 instruction the validation is focused
(no exhaustive pixel oracle, media matrix or sanitizer sweep).

## What changed

- **Settings > Display** now lists Brightness -, Brightness +, Orientation, **Theme**, then Calibrate
  touch (resistive board only). Theme shows `Light`, `Black` or `High Contrast`; one release cycles
  Light -> Black -> High Contrast -> Light, saves once, and repaints through the existing `enter()`
  path (the same one Orientation uses). A press that leaves the row does nothing. No per-tick repaint
  was added. Display has 4 rows (capacitive) or 5 (resistive), so on landscape the resistive
  Calibrate row sits on the second page, reached with the existing page strip; portrait shows all.
  Paging, Back and every other hit target are unchanged; no layout or golden-geometry change.
- **Persistence:** `Settings::colorTheme` (1 B) in the new NVS key `colorTheme`. Absent, wrong-size or
  out-of-range (>= 3) values select Light and are not rewritten by a read. The retired legacy
  `theme` key is still never read, written or erased; rollback data and unrelated settings are
  untouched.
- **Palettes** (`theme.cpp`): three `const Palette` tables in flash; `theme()` indexes them by
  `g_settings.colorTheme`. Light is v24 byte for byte (asserted against the v24 constants in the
  host test). Black and High Contrast started from the CY-87 illustrative role tables and were
  re-checked against the pairs production code really draws.
- **Three new palette roles** (flash only, Light values equal what the code drew before):
  `onPressed` (ink on a pressed face outside a control: tiles, Player header dots, mini-player
  transport, output chip - these used `text`, which is white on white in High Contrast's pressed
  face), `miniTrack` and `miniFill` (mini-player progress line, which used `outline`/`accent` -
  white track under a yellow fill in High Contrast, and a dark-blue fill on grey in Black).

## Role audit (what the CY-87 mockups did not cover)

| Finding | Fix |
| --- | --- |
| Tiles, Player header Options dots, mini Play/Next and the output chip draw `text` ink on `pressed`; HC pressed is white | `onPressed` role (HC black, Light unchanged `0x1082`) |
| Mini-player line: `outline` track, `accent` fill | `miniTrack`/`miniFill` roles (HC grey/yellow, Black grey/light blue) |
| CY-87 Black accent `(64,148,245)` with white `onAccent` is 3.3:1; the Light-style `pressed` `0x3C5C` with white ink is 3.5:1 | Black accent `0x1BBB` `(24,116,220)` is 4.53:1 with white ink and 4.64:1 on the black page; Black pressed `0x2272` is 8.2:1 with white |
| HC `band` black made key/tile faces and the page identical | HC band `0x3186` (48 grey); outlines stay white |
| Light status colours are dark on white and fail on black | Brighter `good/warn/bad` in both new themes (>= 4.5:1 on bg) |

Palette values (RGB565, field order of `theme.h`): see `theme.cpp`. Black: bg `0000`, band `2104`,
raised `31A7`, text `FFFF`, active/accent `1BBB`, pressed `2272`. High Contrast: bg `0000`, band `3186`,
outline `FFFF`, active/accent `FF00` (yellow) with black ink, pressed `FFFF` with black ink.

## Evidence (focused)

| Check | Result |
| --- | --- |
| `tests/classic_ui_test` (existing host UI target, `.tools/cy91_ui.cmd`, output in `.tools/logs/cy91-ui-final.log`) | exit 0: `PASS CY-91 themes ...`, `PASS firmware UI ...`; geometry golden unchanged |
| `tests/settings_route_test` (wired build) | `settings_route: colorTheme key OK`, `SETTINGS_ROUTE_OK` |
| `powershell -NoProfile -File tools/check.ps1 -SkipProbe -SkipHostMedia` | **exit 0** (`PASS: compile-only checks completed`) |
| `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` | **exit 0**, single codec pool `0xa000` = 40,960 B |
| Seven-file manifest verification (`.tools/cy91_verify_manifest.ps1`; it prints the inherited `CY90_MANIFEST_OK` text) | OK: 7 files, every size and SHA256, exactly four flash offsets |

The theme test asserts: Light == v24 constants; invalid id draws Light; Black/HC production role pairs
(27 pairs, smallest margin 1.006x of the floor for Black and 1.26x for High Contrast); in all four
rotations the row shows the value, a press that leaves the row changes nothing and does not save,
waits for release, one release changes the theme once and saves once, the row cache is rebuilt
(old value label gone), the page, header, page strip, footer and mini-player line sample the new
palette, and no old-palette rectangle or strip is left (fewer than 100 matching pixels per old
role; a few anti-aliasing blends coincide with palette greys); a pressed Home tile shows `pressed`
face and `onPressed` ink in all three themes. The settings test: fresh, valid (all three ids, with
unrelated settings), invalid/wrong-size round trip, and the legacy `theme` key preserved (2 and 8)
and never reinterpreted.

I also looked at pressed tile and pressed row renders for Black and High Contrast (not committed):
HC pressed tile is a white face with black ink; the pressed row is yellow (HC) or blue (Black) with
legible ink. Light was not re-rendered against v24; its palette is asserted identical and the three
code paths that now use roles evaluate to the same values.

## Previews (production renderers, native 1x)

| Screen | Image |
| --- | --- |
| Player, landscape, Light | [player-light-r1.png](cy91-theme-selector/player-light-r1.png) |
| Player, landscape, Black | [player-black-r1.png](cy91-theme-selector/player-black-r1.png) |
| Player, landscape, High Contrast | [player-hc-r1.png](cy91-theme-selector/player-hc-r1.png) |
| Display with the Theme row, landscape, High Contrast | [display-hc-r1.png](cy91-theme-selector/display-hc-r1.png) |
| Display, portrait spot check, Black (resistive host build, shows Calibrate) | [display-black-r0.png](cy91-theme-selector/display-black-r0.png) |

They come from the production `ui.cpp`/`classic_screen.cpp` through the host driver recorder
(`CYDPOD_THEME_PREVIEW=<dir>` while running the UI test), losslessly recompressed with
`tests/pack_previews.py`.

## Resources

| | v24 (CY-90) | v25 | Delta |
| --- | ---: | ---: | ---: |
| Normal image flash (`Sketch uses`) | 1,529,517 B | 1,529,901 B | +384 B |
| Normal image static RAM (`Global variables`) | 92,648 B | 92,656 B | **+8 B** (< 100 B) |
| Forced-wired image flash | 1,529,629 B | 1,530,033 B | +404 B |
| Forced-wired image static RAM | 92,656 B | 92,656 B | 0 |
| Codec pool | 40,960 B | 40,960 B | 0 |

The static RAM is `Settings::colorTheme` plus alignment. No new heap, framebuffer, sprite or cache;
palettes and names are flash. The only added draw work happens once per theme tap (one full repaint
through the existing scheduler, as for Orientation); playback-time drawing is unchanged, so audio
headroom, `stat: buf` and the one-dirty-row-per-tick rule are unaffected.

## Export (reserved v25)

`E:\dev\.worktrees\CYDpod\cy91-theme-selector\.tools\images\codec-pool-v25-jc2432w328c`

**App SHA256:** `7a7454936ae5c7bfd4f6614a095315bca6bcce3991bf1be2e9744499f9a3127a` (forced-wired
companion `6bc3736903ec46f6249f465363c83cfbfef44764f53be1dcd577add9c8a420d1`).

| File | Bytes | Flash offset | SHA256 |
| --- | ---: | --- | --- |
| `app.ino.bin` | 1,530,048 | 0x10000 | `7a7454936ae5c7bfd4f6614a095315bca6bcce3991bf1be2e9744499f9a3127a` |
| `app.ino.bootloader.bin` | 25,024 | 0x1000 | `f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29` |
| `app.ino.partitions.bin` | 3,072 | 0x8000 | `0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7` |
| `boot_app0.bin` | 8,192 | 0xe000 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| `app.ino.elf` | 15,976,932 | none (support) | `331ac786b28d6c159a47f459cae2e8e80daf51c1d18d4a48ef9c9baab8a07cc5` |
| `app.ino.map` | 19,563,880 | none (support) | `0693f988ce45c70382920232b099814fe92ce8e61d1168e69a9bd1aeab36e801` |
| `app.ino.merged.bin` | 4,194,304 | none (support) | `d5bfb06cc8d59ed5c917d2c7ce8de25d4b8c7c4f5811ec8d4495a115aec8b372` |

ESP32 / 4 MB / DIO / 80 MHz / PSRAM disabled.

## Unverified on hardware / what to look at (owner)

Unverified: panel gamma and dark-grey crush on the ST7789 (Black's band `(34,34,36)` and HC's
48-grey band are the values most likely to need a tweak), sunlight readability, touch feel of the new
row, and audio under UI load (the repaint is a one-off on a tap). Look at: Settings > Display >
Theme in portrait and both landscapes; after each tap the whole screen (header, rows, page strip,
mini-player line) is in the new colours with no light rectangles or old-colour text; Home pressed
tile, a pressed list row, Player transport (normal, held, active), favourite on/off, volume and seek
bars, Bluetooth page, in each theme; the setting survives a reset. Volume-cap and slide-lock were not
started (CY-68 stays paused).

**Commands below are for the owner only and were not run.** Identify the JC2432W328C as COM7, enter
manual download mode (hold BOOT, tap RESET, release BOOT). These write and verify only the four
regions and keep NVS and media; keep the factory backup and the v8 rollback image.

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy91-theme-selector
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v25-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
```

Press RESET afterwards. Roll back with the same commands and `$img` set to
`E:/dev/.worktrees/CYDpod/cy90-icons-outline/.tools/images/codec-pool-v24-jc2432w328c` (v24, which
ignores `colorTheme` and leaves the key in place) or the v8 image
`E:/dev/.worktrees/CYDpod/cy72-bt-gating/.tools/images/codec-pool-v8-jc2432w328c`.

Reproduce (sequential): `powershell -NoProfile -File tools/check.ps1 -SkipProbe -SkipHostMedia`, then
`powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`.
