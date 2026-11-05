# CY-90: selected C / Quiet outline icons

2026-11-05. Branch `cydpod/icons-quiet-outline`, worktree
`E:\dev\.worktrees\CYDpod\cy90-icons-outline`, exact base `f05d6b7`
(CY-87 research atop accepted v23 `10416be`). Implementation/assets/tests were
committed as `c24e92a` before the final sequential builds. CY-68 remains paused.
No port, device, serial, flash or user-media operation was performed.

## Implemented design and scope

The selected C shapes and optical origins come unchanged from
[`cy87_icon_sets.py`](../../tools/cy87_icon_sets.py)::`vec/rasterise`:
open play/skip at 16 px and above, filled play/skip at 12/14 px, thin chevrons,
outlined music/speaker, light gear, thicker pause strokes, opposing repeat arrows,
and separate hollow/solid favourite masks. All 17 enum icons plus off-star have
all nine catalogue sizes: 12, 14, 16, 18, 24, 25, 28, 32 and 34 px.

`ui_icons.cpp` contains **30,469 B** row-aligned, high-nibble-first 4-bit coverage,
**1,944 B** mask metadata, **928 B** exact TFT-derived face spans and **20 B** face
metadata: **33,361 B** asset objects before linker padding. The generator retains
its upstream TFT_eSPI primitive-port notice and original-shape provenance; no
icon pack, dependency, download or runtime rasterizer was added. ESP32 reads the
const PROGMEM arrays directly from memory-mapped flash.

Icon transport faces use the page background and a 1 px outline, inset 2 px;
pressed fills use `pressed/onControlActive`, active uses `accent` border/glyph and
a 6 px underline. Press takes precedence over active appearance. Favourite uses
the selected filled capsule when on/pressed, a hollow off-star, and a solid
on-star. The current light palette remains the only shipped palette. Text buttons
and confirmation dialogs retain their original raised treatment. CY-84 output
switching, CY-85 actual glyph typography, CY-63 sessions, QWERTY, output choices,
and touch/transport behavior are retained.

The previous Player wrapper expanded by 3 px and clipped back. It now passes
the original cell and an explicit old nominal glyph field
`min(w+6,h+6)*3/5`: face insets apply once, glyphs retain their selected optical
size, and no expanded face is accidentally resized or clipped. Geometry and hit
rectangles are identical before/after:

| Orientation | Shuffle | Previous | Play | Next | Repeat |
| --- | --- | --- | --- | --- | --- |
| Landscape 1 and 3 | 0,148,52,48 | 56,148,70,48 | 130,146,80,52 | 214,148,70,48 | 284,148,36,48 |
| Portrait | 0,228,36,48 | 40,228,36,48 | 80,226,80,52 | 164,228,36,48 | 204,228,36,48 |

Back, favourite, options, volume, text-only full-width output chip, mini-player,
all lists and keyboard hit maps retain their original geometry. `classic_layout.h`
and `tests/golden/classic_layout.txt` are byte-identical to the base (Git blob IDs
`6bc0c9518bf8c605c1ebbdc2369301ce146fabce` and
`e455d2ef54a79c6228ef195e3b71d973b58a3f2b`). The new mask fingerprint file is
separate from the unchanged geometry golden.

## Streaming, scratch and scheduling

Each standalone glyph opens one clipped tight-bounds address window; each icon
button/favourite opens one clipped **whole cell** window. `iconStream` composes
cleared corners, face, border, coverage-blended glyph and underline before each
`pushColors(row,width,true)` burst, inside one `startWrite/setAddrWindow/endWrite`
transaction. There are no per-pixel SPI draws or per-row address windows. The
installed TFT_eSPI 2.5.43 raw `setAddrWindow` does not clip or add the origin;
`checkViewport` clips first and `getOriginX/Y` supplies the absolute panel window.
The driver's `startWrite` locks the transaction across its row bursts.

Actual largest cell is **80 x 52**, not the old expanded 86 px wrapper; other
transport widths are 36, 52 and 70 px, favourite is 40 px, and the largest tight
glyph width is 34 px. Local scratch is **160 B RGB565 row + 32 B blend table =
192 B maximum**; both default and W328C ELF compositor prologues reserve a **304 B** function
frame including these arrays, locals and ABI/register overhead (not a whole-task
stack watermark). There is no sprite, framebuffer, persistent pixel scratch or new
heap allocation on the device. A five-byte control-state cache suppresses unchanged
transport cells, and full page entry/rotation forces all five cells. No audio,
Bluetooth, decoder, storage, gain, vendor, font or lock code changed.

The dirty-row/audio-headroom scheduler is unchanged. An idle/time tick streams no
icon cells; Play touch-down and release each stream only one 80 x 52 cell. This
reduces transport command work and wire bytes relative to the CY-87 primitive
model, but coverage composition adds CPU work. It is reasoning about bounded
work, not a measurement of buffer health, underruns or Bluetooth drop freedom.

## Validation and production previews

The existing host UI suite now records the production raw driver API calls,
absolute window coordinates, every burst and every RGB565 pixel. It rejects an
unclipped raw window rather than rescuing it with implicit host clipping. These
are executable production-renderer call counts at the driver boundary, not an
on-device logic-analyzer trace or timing measurement.

- 162 mask fingerprints match the independent unpacked source-shape oracle;
  15,326 fractional coverage samples are exercised. Swapped high/low nibbles fail
  all 162 fingerprints. The selected up/down at 14 px peak at 14/15 coverage;
  other masks reach 15/15. Tight metadata, row strides, padding and bounds pass.
- Translated and absolute viewports, negative origins, panel edges and empty/off
  screen rectangles pass; glyph and whole-cell windows stay clipped and complete.
- Normal, pressed, active and repeat-one transport rows: **5 windows, 244 row
  bursts, 15,104 RGB565 pixels**, **30,263 modelled command/pixel bytes** each.
  Glyphs, whole-cell clears, border and underline share those same windows.
- All nine icon-size sheets, four favourite combinations and actual Player
  orientations 0/1/3 are rendered through production `ui.cpp/classic_screen.cpp`
  and the CY-85 glyph/framebuffer paths. Independent CY-87 source rendering
  matches **1,240,320 expected pixels**; an incorrect expanded-wrapper negative
  control differs at **656 pixels**. All 52 PNGs pass CRC/dimension checks and
  exact native-to-4x replication.
- Existing geometry/hit maps, touch/transport, Search/session/QWERTY, Settings,
  output-switch, mini-player, glyph integrity and text fitting regressions pass.
  CY-85 retains 282 bitmap-exact glyph checks and 585 fit/draw cases. The 1 Hz
  time update still makes two fills, largest 5,760 px.

The committed images below are production host renders, not generator mocks.
Their icons/face spans and text use the exact selected masks and installed font
bits; unrelated inherited TFT primitive shapes in the host recorder remain its
existing approximations. No host fonts substitute for the PNG text. The SVG
recorder's Arial labels remain layout evidence only.

All 52 native/4x PNGs and their dimensions/bytes/SHA256 are in the
[preview manifest](cy90-quiet-outline-icons/manifest.json).

| Production screen | Native | 4x |
| --- | --- | --- |
| Landscape 1, normal | [native](cy90-quiet-outline-icons/player-r1-s0.png) | [4x](cy90-quiet-outline-icons/player-r1-s0-x4.png) |
| Landscape 1, pressed | [native](cy90-quiet-outline-icons/player-r1-s1.png) | [4x](cy90-quiet-outline-icons/player-r1-s1-x4.png) |
| Landscape 1, active | [native](cy90-quiet-outline-icons/player-r1-s2.png) | [4x](cy90-quiet-outline-icons/player-r1-s2-x4.png) |
| Landscape 1, repeat one | [native](cy90-quiet-outline-icons/player-r1-s3.png) | [4x](cy90-quiet-outline-icons/player-r1-s3-x4.png) |
| Landscape 3, normal | [native](cy90-quiet-outline-icons/player-r3-s0.png) | [4x](cy90-quiet-outline-icons/player-r3-s0-x4.png) |
| Landscape 3, pressed | [native](cy90-quiet-outline-icons/player-r3-s1.png) | [4x](cy90-quiet-outline-icons/player-r3-s1-x4.png) |
| Landscape 3, active | [native](cy90-quiet-outline-icons/player-r3-s2.png) | [4x](cy90-quiet-outline-icons/player-r3-s2-x4.png) |
| Landscape 3, repeat one | [native](cy90-quiet-outline-icons/player-r3-s3.png) | [4x](cy90-quiet-outline-icons/player-r3-s3-x4.png) |
| Portrait, normal | [native](cy90-quiet-outline-icons/player-r0-s0.png) | [4x](cy90-quiet-outline-icons/player-r0-s0-x4.png) |
| Portrait, pressed | [native](cy90-quiet-outline-icons/player-r0-s1.png) | [4x](cy90-quiet-outline-icons/player-r0-s1-x4.png) |
| Portrait, active | [native](cy90-quiet-outline-icons/player-r0-s2.png) | [4x](cy90-quiet-outline-icons/player-r0-s2-x4.png) |
| Portrait, repeat one | [native](cy90-quiet-outline-icons/player-r0-s3.png) | [4x](cy90-quiet-outline-icons/player-r0-s3-x4.png) |

[Favourite off/on and both pressed states](cy90-quiet-outline-icons/favourite-states.png) / [4x](cy90-quiet-outline-icons/favourite-states-x4.png).

All enum icons in order PREV, PLAY, PAUSE, NEXT, GEAR, BLUETOOTH, BACK, NOTE, SHUFFLE, REPEAT, REPEAT_ONE, UP, DOWN, CHEVRON, SPEAKER, STAR, HEADPHONES; hollow favourite at bottom right:

- 12 px: [native](cy90-quiet-outline-icons/catalogue-12.png) / [4x](cy90-quiet-outline-icons/catalogue-12-x4.png).
- 14 px: [native](cy90-quiet-outline-icons/catalogue-14.png) / [4x](cy90-quiet-outline-icons/catalogue-14-x4.png).
- 16 px: [native](cy90-quiet-outline-icons/catalogue-16.png) / [4x](cy90-quiet-outline-icons/catalogue-16-x4.png).
- 18 px: [native](cy90-quiet-outline-icons/catalogue-18.png) / [4x](cy90-quiet-outline-icons/catalogue-18-x4.png).
- 24 px: [native](cy90-quiet-outline-icons/catalogue-24.png) / [4x](cy90-quiet-outline-icons/catalogue-24-x4.png).
- 25 px: [native](cy90-quiet-outline-icons/catalogue-25.png) / [4x](cy90-quiet-outline-icons/catalogue-25-x4.png).
- 28 px: [native](cy90-quiet-outline-icons/catalogue-28.png) / [4x](cy90-quiet-outline-icons/catalogue-28-x4.png).
- 32 px: [native](cy90-quiet-outline-icons/catalogue-32.png) / [4x](cy90-quiet-outline-icons/catalogue-32-x4.png).
- 34 px: [native](cy90-quiet-outline-icons/catalogue-34.png) / [4x](cy90-quiet-outline-icons/catalogue-34-x4.png).


Visual inspection covered native and enlarged portrait, both landscapes,
pressed/active/repeat-one, hollow/solid/pressed favourite and the small-size
catalogue against C's selected sheets. There is no missing stroke, new overlap or
clipping finding. At 12/14 px, busy gear/note/repeat details retain the selected
preview's small-size limitations; outline play/skip intentionally become filled.
The coordinator also reviewed the production images and source and reported no
correctness/overlap/clipping finding.

## Sequential checks and resources

Both final checks ran **sequentially** and exited **0**:

- `powershell -NoProfile -File tools/check.ps1` — `.tools/logs/cy90-check-default.log`;
  all four targets, both UI variants, geometry/touch/Search/Settings/output-switch,
  codec/media suites and independent selected-C pixel comparison pass.
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`
  — `.tools/logs/cy90-check-w328c.log`; normal/wired v24, both tone variants and
  diagnostic pass. No device commands were run by either check.

| Maintained player | Program storage | Delta from accepted v23/CY-87 | Static RAM | Delta | App binary |
| --- | ---: | ---: | ---: | ---: | ---: |
| Default | 1,488,825 B | +28,432 B (base 1,460,393) | 91,072 B | +16 B (base 91,056) | 1,488,976 B |
| W328C normal | 1,529,517 B | +31,848 B (base 1,497,669) | 92,648 B | +16 B (base 92,632) | 1,529,664 B |
| W328C forced wired diagnostic | 1,529,629 B | diagnostic companion | 92,656 B | diagnostic companion | 1,529,776 B |

ELF section comparison confirms `.dram0.data` grows **16 B**, `.dram0.bss`
**0 B** on both boards; no persistent scratch heap is introduced. All four asset
objects are in `.flash.rodata`, not SRAM. The five-byte state cache plus linked
layout/alignment is included in those exact static totals. The normal image is
below the 1,966,080 B application slot. Both players have one unchanged **40,960 B**
codec pool (`0000a000 b`); W328C normal and wired symbol address `3ffc4a50`.

Exact retained low-level evidence: `.tools/logs/cy90-linked-sections.txt`,
`cy90-flash-objects.txt`, `cy90-compositor-frame.txt`, `cy90-default-symbols.txt`,
`codec-pool-v24-jc2432w328c-codec-pool-map.txt`,
`codec-pool-v24-wired-jc2432w328c-codec-pool-map.txt`, and
`cy90-production-evidence.log`, all under this worktree's `.tools/logs/`.

```text
DEFAULT_CHECK_EXIT=0
CY90_MASKS_OK: 162 source fingerprints; 15326 fractional samples; 162 swapped-nibble negatives; 68824 streamed pixels / 3176 row bursts
CY90_CELLS_OK: five transport cells = 5 windows, 244 bursts, 15104 pixels, 30263 modelled command/pixel bytes per row in all four states; max row 80 px
CY90_DIRTY_OK: idle/time ticks stream 0 icon cells; Play touch-down and release stream exactly 1 cell each
CY90_PIXELS_OK: 1240320 independently expected production pixels; expanded-wrapper negative control differs at 656 pixels
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
W328C_CHECK_EXIT=0
PASS single pool: 3ffc4a50 0000a000 b _ZN12_GLOBAL__N_1L9codecPoolE
PASS: compile-only checks completed; no device commands were run.
CY90_MANIFEST_OK: 7 actual files, every size/SHA256, exact 4 flash offsets, ELF/map/merged carry no flash offset
```

The reserved owner image is
`E:\dev\.worktrees\CYDpod\cy90-icons-outline\.tools\images\codec-pool-v24-jc2432w328c`.
**App SHA256:** `af470ee3bea10fbd681b846e419d5670528b3cef076cab053570777225ad0802`.
Default app SHA256: `fa6b4a6c0587158842a98dd494bf9e00b692b3a5901a1acc85a1893b1196cb6b`.
Forced-wired app SHA256: `375229a1f05c4526b90997971a516d1d04b71f900935f948586bac8f6d2078b6`.

All **seven actual normal-image files** were independently checked against
`MANIFEST.json`, including every byte count and SHA256, and **exactly four** flash
offsets; the complete checked hashes are retained in the
[verified normal-image manifest](cy90-quiet-outline-icons/image-manifest.json):

| Actual exported file | Bytes | Flash offset |
| --- | ---: | --- |
| `app.ino.bin` | 1,529,664 | 0x10000 |
| `app.ino.bootloader.bin` | 25,024 | 0x1000 |
| `app.ino.elf` | 15,968,960 | none (support artifact) |
| `app.ino.map` | 19,549,846 | none (support artifact) |
| `app.ino.merged.bin` | 4,194,304 | none (support artifact) |
| `app.ino.partitions.bin` | 3,072 | 0x8000 |
| `boot_app0.bin` | 8,192 | 0xe000 |

The image manifest identifies ESP32 / 4 MB / DIO / 80 MHz / PSRAM disabled.
ELF, map and merged image are support artifacts; use the four explicit regions
below for the owner flash, and keep the factory backup and v8 rollback image.


Reproduce with the installed dependency tree (read-only `.deps` junction; private
`.tools` CLI config/cache/build/staging). Run checks sequentially:

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy90-icons-outline
python -I tools/cy87_icon_sets.py --export-c
powershell -NoProfile -File tools/check.ps1
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia
# Included by the default host check; can also re-run the independent pixel oracle:
python -I tools/cy87_icon_sets.py --verify-production tests/out/icon-preview
```

## Owner-only hardware follow-up

Hardware remains unverified: physical panel readability/gamma/contrast, actual
AirPods Pro 2 and stereo headphone listening, buffer/underrun behavior under UI
load, and drop-free Bluetooth. Check Home gear/music, header Back/route/favourite,
mini-player and paging icons, Player's five controls (normal, held, active,
repeat-one), and favourite off/on. Test portrait and both landscapes; confirm
thin small details on the panel, one command per transport touch, tolerant
release elsewhere, and the existing Bluetooth/Headphones restart confirmation.
Compare `stat: buf`/underruns during rapid transport taps and sustained playback.
Volume-cap implementation and slide-to-unlock were not started.

**Commands below are for the owner only and were not run.** The factory backup
already exists per the board build/flash record; retain it. After identifying the
JC2432W328C as COM7, enter manual download mode (hold BOOT, tap RESET, release
BOOT). Write/verify all four regions; these operations retain NVS and user media.

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy90-icons-outline
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v24-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
```

Press RESET afterward. Optional owner serial observation, only after closing any
other serial user: `python -I -m serial.tools.miniterm COM7 115200`.
Restore the existing v8 player with the same manual mode and four-region
write/verify commands, changing only `$img` to the verified existing path
`E:/dev/.worktrees/CYDpod/cy72-bt-gating/.tools/images/codec-pool-v8-jc2432w328c`.
Its old output enum interprets a saved Headphones value as its old speaker route;
select Bluetooth after rollback as needed. Full factory recovery uses the
owner's retained full-image backup separately.
