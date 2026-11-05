# CY-85 Readable, faithful text on the physical display (v20/v22 -> v23)

Branch `cydpod/text-quality` from CY-84 `399dfab`. Commits: `3fb0a98` renderer, `1f2f89c` step-down/chip/tests, `4326cea` **geometry golden only**, `f28b3fa` before/after sheet + PNG packer, plus this report. No audio, output-switch, Bluetooth, storage/catalogue, touch, vendor or lock code changed.

## Cause (confirmed on real glyph data)
`playerText` in v20 nearest-neighbour sampled TFT font 2 (16 px bitmaps) to 22/14/12/11/10/8 px. Dropping source rows and columns changes what a glyph *is*: measured on the real `widtbl_f16`/`chrtbl_f16` tables, the v20 arithmetic changes the topology (ink components or enclosed counters) of **37 / 67 / 62 of 94 glyphs at 14 / 11 / 8 px**. That is the owner photo: "Britney" reads "Dritney", "Headphones" collides, the 8 px count and 10 px chip are noise. The host build had hidden it by drawing Arial through `classicText`. General captions (`TYPE_CAPTION`) were a separate problem: TFT font 1 is a 5x7 glyph set, 7 px tall and light.

Before/after on one sheet (real Font16 data, v20 arithmetic verbatim, then the new faces): `docs/research/cy85-text-quality/before-after-sampler.png` (native) and `-x3.png`.

## Fix
- New `classic_text.h/.cpp` (small, used by production): ASCII text is drawn **1:1, unscaled, from flash** with the Adafruit GFX FreeSans bitmaps that ship with the pinned TFT_eSPI 2.5.43 (`Fonts/GFXFF`): `Title` = FreeSansBold 12 pt (cap 18 px), `TitleCompact` = FreeSansBold 9 pt, `Label` = FreeSans 9 pt (cap 13 px). They are designed for a ~141 ppi 2.8 in panel, which is this one. No heap, no cache, no file access, no scaling; glyph runs go through `drawFastHLine` as before.
- Title steps down Title -> TitleCompact before any ellipsis (portrait fits "...Baby One More Time" whole). Artist, album, elapsed/remaining, queue count, volume number, mini-player title/artist and the output chip use `Label`.
- Measurement and truncation are one function (`fit`): longest whole-glyph prefix plus `...`, trailing space trimmed, empty if even the dots do not fit. `ui_text` (font 2) now also truncates by whole characters (UTF-8 safe) instead of clipping half a glyph in a viewport, and `TYPE_CAPTION` uses font 2 instead of font 1.
- Queue count degrades "24 / 85" -> "24/85" -> "24" rather than cutting a number. Unknown duration shows `--:--` (was `---:--`). Portrait header titles may borrow room from a short parent label.
- Non-ASCII (Latin accents, Chinese) still goes through the existing UTF-8 strip path (`titles_*`) with its native strip height; unchanged. Title-strip release (`titles_release()`) is kept on every ASCII draw.
- Output chip: no icon; text from the chip's left edge. Provenance/licence: the three fonts are unmodified `FreeSans9pt7b.h` (sha256 `3547eea7...2969`), `FreeSansBold9pt7b.h` (`cc48bca6...a3f4`) and `FreeSansBold12pt7b.h` (`584ef1f6...c38f`) from the already installed, locked TFT_eSPI 2.5.43 (`dependencies.lock.json`, BSD-2-Clause, per-file font notices retained in `Fonts/GFXFF/license.txt`; the outlines derive from GNU FreeFont via Adafruit GFX). Nothing was added to the repository or downloaded; `LOAD_GFXFF` is already defined in both User_Setup files.

## Geometry (only what legibility required; separate golden commit `4326cea`)
| Rectangle | v22 landscape / portrait | CY-85 landscape / portrait |
| --- | --- | --- |
| Seek bar `progress` | y 118 / 198 | y 116 / 196 (13 px time labels clear the knob) |
| Output chip | 238,205 76x30 / 158,285 76x30 | 190,205 124x30 / 120,285 114x30 |
| Output hit target | 232..320 (88) / 152..240 | 184..320 (136) / 114..240 (126) |
| Volume track | x44 w150 / x44 w70 | x44 w102 / x28 w48 |
| Volume hit target | 0..232 / 0..152 | 0..184 / 0..114 |
Text rows (centre of capitals, `classic_layout.h`): title 52, artist 76, album 97, time 135, text block cleared 40..110 (+portrait offset), time band 128..146. No browse, Home, keyboard or dialog rectangle moved. The transport row, favourite, options, back hit targets are unchanged. Portrait trade-off: its volume track is 48 px (was 70) because "Headphones" is 102 px wide at 9 pt; the speaker glyph moves to x14.

## Tests (host, production glyph path, framebuffer PNGs)
`tests/text_quality_test.h` (included by `classic_ui_test.cpp`); the host `TFT_eSPI` recorder now keeps an RGB565 framebuffer fed by the real Font16/glcd tables and the helper's GFX bitmaps, with a dependency-free PNG writer. Round rects, circles, lines and triangles are host approximations; **glyph pixels are the production data**. Retained output:
- `CY85_GLYPHS_OK: 282 glyphs pixel-exact with their font bitmaps; v20 sampler negative control changes topology of 37/67/62 of 94 glyphs at 14/11/8 px` (every printable glyph of all three faces, drawn through `cydtext::draw`, equals its font bitmap and baseline offset; the v20 sampler is retained only as a negative control).
- `CY85_MEASURE_OK: 585 fit/draw cases (415 truncated)` against an independent brute-force longest-prefix oracle; painted ink never exceeds the measured box; alignment edges; controls measure as space; `ui_ellipsize` never splits a UTF-8 sequence.
- `CY85_SCREENS_OK: 27 faithful PNG previews`: five scenes (Britney/Bluetooth `AirPods Pro 2`, Headphones with a 1285-track queue and 1:02:05, overflow and punctuation, unknown duration, UTF-8 fallback) x rotations 1/3/0/2 through the real Now Playing screen; every label's ink box must lie in its band, never overlap another label or a control (bar/knob, transport, toggles, volume knob, back/favourite/options), and must be >= 9 px tall. Owner labels must be whole: `...Baby One More Time`, `Britney Spears`, times, volume, `Headphones` in all rotations; `AirPods Pro 2` and `n / m` counts on the 320 px panels (portrait degrades per the ladder).
- `CY85_REPAINT_OK: a 1 Hz tick makes 2 fills, largest 5760 px (time band 5760 px)`; mini-player lines stay in their cell (`CY85_BROWSE_OK`).
- Mutation checks: moving the album line to y 108 and an off-by-one in `fit` each fail these tests. The checks also caught a real defect during development (volume number overlapping the knob at 100 %).
Existing navigation, QWERTY, Search session, touch, output-switch and geometry-golden tests are kept; five expectations were updated for the new numbers (bar y, chip, volume track, one tap x, truncated portrait header titles: a title is now "whole or whole glyphs + ...").

**PNG paths** (committed, repacked losslessly by `tests/pack_previews.py`): `docs/research/cy85-text-quality/<scene>-r<rotation>.png` (native 320x240 or 240x320) and `<scene>-r<rotation>-x3.png` (3x nearest-neighbour). Scenes: `britney-bluetooth`, `britney-headphones`, `overflow-punctuation`, `unknown-duration`, `utf8-fallback`, `songs-mini`, `audio-output`, `before-after-sampler`. Rotation 1 is the primary landscape, 3 the flipped landscape, 0/2 portrait. Each is drawn by the production helper (`cydtext::draw`) or font 2 table through the real screens, not a smooth host font. Not shown on host: the anti-aliased CJK/Latin-accent strips (`titles_*` reads the SD font); the `utf8-fallback` PNGs show the existing no-strip fallback (non-ASCII becomes `?`). Strips are unchanged and unverified here.

Inspected faithful previews: no missing strokes, no overlap or clipping in any rotation; remaining visible compromises: long titles/albums/peer names end in `...` by design; `JC2432W328C` and `AirPods Pro 2` fit only at 124 px (landscape; portrait shows `JC2432W3...`/`AirPods P...`); the 5x7 font-1 hint "Switch back to Bluetooth if there is no sound" (Output page) is unchanged because an earlier accepted spec demands one line.

## Resources (W328C v22 -> v23, default board)
| Metric | v22 | v23 | Delta |
| --- | ---: | ---: | ---: |
| W328C program storage | 1,490,381 | 1,497,669 | +7,288 (+4 KB fonts, ~3 KB code) |
| W328C static RAM | 92,632 | 92,632 | 0 |
| W328C exported app | 1,490,528 | 1,497,824 | +7,296 |
| Codec pool symbol | 0xa000 (40,960) | 0xa000 (40,960) | 0 |
| Default `cydpod-player` program / static | 1,453,149 / 91,056 | 1,460,393 / 91,056 | +7,244 / 0 |
No new persistent heap (the helper has no allocation; `ui_ellipsize` uses transient `String`s on the UI task as `ui_text` already did). Per-frame work is a few hundred `drawFastHLine` calls only when a label changes (track change, 1 Hz time) exactly as before; text redraw timing and the one-dirty-row/ring-health logic are untouched, so audio health (`stat: buf`) is unchanged by reasoning, not measured.

## Final checks (sequential)
- `powershell -NoProfile -File tools/check.ps1` -> **exit 0** (`.tools/logs/cy85-check-default.log`, ends `PASS: compile-only checks completed`, includes both `classic_ui_test` runs with `CY85_*` PASS lines).
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` -> **exit 0** (`.tools/logs/cy85-check-w328c.log`, `PASS single pool: 3ffc4a40 0000a000`).
- Reserved `codec-pool-v23-jc2432w328c` (normal) and `codec-pool-v23-wired-jc2432w328c`, copied from the unchanged v12-named build output. Normal: 7/7 manifest files match size and SHA-256; the four regions equal their offsets in the merged 4 MiB image: `app.ino.bootloader.bin` 0x1000 (25,024 B), `app.ino.partitions.bin` 0x8000 (3,072 B), `boot_app0.bin` 0xe000 (8,192 B), `app.ino.bin` 0x10000 (1,497,824 B, sha256 `783e368e062b0a5f4f9b5543efe3b822a5390a97018e6822e5d18b08e5c62fad`); manifest sha256 `225396abcc5cae30cfaebd4189cd8b595b0312d287b284fe92d4d1a3e2db36c2`. Wired companion app 1,497,920 B `1adcc40afc2f79dfe635644bbee498bbdb31d696cd5862343125395d472da987`, 7/7 and 4/4. v20/v22/v21 reservations untouched.
- No device, serial, flash or SD operation was performed.

## Software evidence vs owner-only panel acceptance
Software evidence: the glyph/measurement/band tests and PNGs above. **Not verified on glass**: that the unscaled 9/12 pt faces look as sharp on the real panel as in the PNGs (colour, gamma, backlight, viewing angle); camera moire on the photo cannot be used to judge pixels.

Owner-only flash (not run by workers): identify the actual W328C first, manual download mode, then as in `cy66-qwerty-search.md`:
```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy85-text-quality
$jcPort = 'REPLACE_WITH_CONFIRMED_W328C_PORT'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v23-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
```
Roll back with the v22 image from the CY-84 worktree the same way. Then look at, on the board, in landscape and portrait: Now Playing (title bold and whole or compact-bold, artist and album readable with no touching letters, `n / m` in the header, elapsed left and remaining right, volume number, chip reading `Bluetooth` peer name or `Headphones` whole), the mini-player under Songs, list captions (artist under a song), Settings > Audio Output, header parent label. Report anything that still reads ragged, a clipped chip name, or a time label touching the seek knob.
