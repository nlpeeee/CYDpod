# CY-78: compact chrome (UI-1)

Branch `cydpod/ui-chrome`, worktree `E:/dev/.worktrees/CYDpod/cy78-chrome`, exact dispatch base `df24eae` on `cydpod/nowplaying` (final CY-61 code `7c6d43e`). Implementation `66f6c9b`, native-strip spacing/guard `574fbf1`; intentional geometry golden `cb70ea5` is a separate commit with the reason. This chunk changes only shared Classic chrome and its tests; the accepted CY-61 Now Playing page and controls are preserved.

## Result and geometry

Browse headers use a flat theme band with a bottom rule, a Back icon and parent page name, and a Bluetooth or Headphones icon at the right. Bluetooth uses `text` ink when connected and `dim` ink while disconnected/reconnecting; the existing status tick refreshes it. Headphones uses its existing active/dim state without calling Bluetooth. Parent names come from the existing navigation stack, including `Home`, `Music`, `Settings`, and named catalogue/source pages; long names retain the existing clipped text path. The title fits the remaining interval between the parent label and route icon. No new route, Quick panel or battery API was added: no battery reading exists in the maintained player, so there is no battery indicator.

Rectangles are `x,y,w,h`. Rows use offsets 12/30 for title/detail centres so both native 18 px non-ASCII strips remain disjoint inside 40 px, above the bottom rule. The strip is the existing 7 px scrollbar; no alphabet UI is included.

| Shared browse geometry | CY-61 base | CY-78 |
| --- | --- | --- |
| Landscape header | 0,0,320,44 | 0,0,320,36 |
| Landscape row starts / height | 44,88,132 / 44 | 36,76,116,156 / 40 |
| Landscape row width | 312 | 312 |
| Landscape scrollbar | 313,44,7,132 | 313,36,7,160 |
| Landscape footer | 0,196,320,44 | unchanged |
| Portrait header | 0,0,240,44 | 0,0,240,36 |
| Portrait row starts / height | 44,88,132,176,220 / 44 | 36,76,116,156,196,236 / 40 |
| Portrait row width | 232 | 232 |
| Portrait scrollbar | 233,44,7,220 | 233,36,7,240 |
| Portrait footer | 0,276,240,44 | unchanged |
| Back hit / painted parent | 0,0,64,44 / generic Back | 0,0,100,36 / icon x=16, name x=28 |
| Route icon centre / size | Headphones only, y=22 / 24 | x=width-20, y=18 / 24, both routes |

`36 + 4 * 40 = 196 = footerY` in both landscape rotations; `36 + 6 * 40 = 276 = footerY` in both portrait rotations. Footer Home, Up, Down and Playing rectangles/behaviour are retained. More rows naturally change paging/top clamps; host navigation expectations were updated for the new capacity. The Back hit is 36 px tall as a documented edge control; it does not extend into the first list row.

The CY-61 Player mode target maps in the new golden are byte-for-byte identical to the accepted base for rotations 0/1/3; its separate 36 px band, seek, transport, volume, output chip, favourite and Options geometry remain unchanged. Rotation 2 is additionally covered by the shared geometry dump.

## Validation

Required final checks ran sequentially against final code `574fbf1` and both exited **0**:

```text
powershell -NoProfile -File tools/check.ps1
  PASS: compile-only checks completed; no device commands were run.
  DEFAULT_CHECK_EXIT=0
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia
  PASS: compile-only checks completed; no device commands were run.
  JC_CHECK_EXIT=0
```

Complete output: `.tools/logs/cy78-default-final-check.txt` and `.tools/logs/cy78-jc2432w328c-check.txt`; summaries: `build-results.json` and `build-results-jc2432w328c.json` alongside them. The W328C check also compiled the forced-Headphones, two tone and diagnostic variants. The initial default run also exited 0, but its firmware preceded the final one-pixel native-strip spacing fix, so the full default check was repeated; the initial output is retained as `cy78-default-before-strip-fix-check.txt`. Development host output is `cy78-host-development3.txt`. Final output contains `CY78_COMPACT_CHROME_OK` and `CY61_NOWPLAYING_OK` twice, `HEADPHONES_UI_OK`, and both CY-62 180-case matrices.

The geometry harness asserts row/strip bounds, 40 px row alignment, contiguous non-overlapping rows ending at the footer, and adjacent footer cells for all 17 page modes, four state variants and all four rotations. It dumps the full production hit map to `tests/out/classic_layout.actual.txt` and compares against the separately committed golden without regeneration during final checks. Existing keyboard, dialog and Player targets remain in the table.

New UI checks exercise nested Songs -> Music and Playback -> Settings parent labels, Back right-edge bounds and tolerant release, all row outer edges, exact Bluetooth/Headphones icon drawing, disconnected dimming and live reconnection. The existing CY-62 classifier-to-command 180-case matrix, jitter/slop, cancellation, list drag and swallowed wake checks pass alongside the CY-61 seek/volume/transport tests. Both default and wired UI binaries run from the default repository check; no independent extra final host run is needed.

Header contrast uses linear Rec.709 luminance after RGB565 expansion, normalized to 0..1. Every theme must have absolute `text`-to-`band` difference > **0.25**, and dimmed route ink difference > **0.08**. All nine palettes pass: Dark 0.993383, Light 0.773637, Neon 0.990510, Retro Amber 0.476474 (minimum), Ocean 0.846685, Phosphor 0.684994, Paper 0.703246, Pop 0.769682, Classic Touch 0.669547. There is no palette named High-contrast in this base; no theme colour fix was required and `theme.cpp` is unchanged.

Host SVGs: `tests/out/cy78-songs-portrait.svg`, `cy78-songs-portrait2.svg`, `cy78-songs-landscape.svg`, `cy78-songs-landscape3.svg`, `cy78-dark-home.svg`, `cy78-headphones-home.svg`. Inspected composite PNG: `.tools/logs/cy78-visual.png` (four/six rows, parent name, route swap, legible flat Dark band). Host text uses Arial and is placement evidence, not device font raster or hardware evidence. Build/test artifacts are ignored and stay out of Git.

## Resource and scheduling budget

| Normal player budget | CY-61 base | CY-78 | Delta |
| --- | ---: | ---: | ---: |
| Default program storage (B) | 1,475,061 | 1,474,913 | -148 |
| Default app image (B) | 1,475,216 | 1,475,056 | -160 |
| Default static RAM (B) | 91,264 | 91,264 | **0** |
| W328C program storage (B) | 1,512,041 | 1,511,913 | -128 |
| W328C app image (B) | 1,512,192 | 1,512,064 | -128 |
| W328C static RAM (B) | 92,840 | 92,840 | **0** |
| Codec pool, both targets (B) | 40,960 | 40,960 | **0** |

No new persistent heap or production globals are introduced. The existing six-entry row cache and navigation stack hold the additional visible rows and parent names; strings use the existing transient UI text path. The codec pool stays 40,960 B, verified by the check from each ELF.

Dirty-row scheduling remains one row per tick; the extra visible row does not create a multi-row repaint burst. Header drawing changes from 44 blended horizontal lines to one 36 px fill plus a rule, existing text and icons. The existing one-second status comparison only redraws on a route/connection change; no per-second full-screen repaint, audio work, SD reads or new timer is added. This supports unchanged audio scheduling by inspection; actual `stat: buf`, underruns, latency and free/peak heap require the integrated board run.

## Image and visual checklist

Primary image: `E:/dev/.worktrees/CYDpod/cy78-chrome/.tools/images/codec-pool-v15-jc2432w328c/app.ino.bin`, **1,512,064 B**, SHA256 `bdcaae2f7d911544bc9823566de1a6027166e5269f01aa43fb2f1be209a9b0b9`. It is the normal saved-route player, not the forced-Headphones test variant. The unchanged check script exports an intermediate v12 name in this private worktree; its completed normal-player files were copied into reserved v15, the manifest regenerated with final code provenance, and **all seven exported files** reverified for byte count and SHA256. Evidence: `.tools/logs/cy78-manifest-verification.txt`; v13/v14 were not used.

| Flash region | File | Bytes |
| --- | --- | ---: |
| 0x10000 | `app.ino.bin` | 1,512,064 |
| 0x1000 | `app.ino.bootloader.bin` | 25,024 |
| 0x8000 | `app.ino.partitions.bin` | 3,072 |
| 0xe000 | `boot_app0.bin` | 8,192 |

No device, serial, COM, SD write or flash operation ran. AAC-LC 44.1 kHz stereo, actual AirPods Pro 2 and stereo headphone output remain hardware acceptance requirements; compile/host evidence does not establish them. No flash is requested until INT-1/UI-DEMO.

For that later integrated visual check:

- Landscape Home now shows Music, Now Playing, Shuffle Songs and Settings together; Search remains reachable by Down/scroll. Music shows Artists, Albums, Songs and Playlists together.
- Landscape Songs, populated Artist/Album child lists, source-song lists and Search Results show four rows when at least four entries exist. Settings shows Audio Output, Bluetooth, Playback and Display together; Display, Playback and Options can also show four rows. Short lists retain their actual entry count.
- Portrait populated lists show six rows. Check rotations 0/2 and landscape 1/3; the last row should end exactly above the original footer.
- Songs has the Back icon and `Music` at the left; Playback has `Settings`. Named nested catalogue pages show their actual parent name in the same place; long text is clipped within the label interval. Back must return to that parent on release.
- Bluetooth is a glyph at the top right, dim while reconnecting and bright when connected. Headphones swaps in the headphone glyph. Read the header on Dark and Classic Touch; there should be no white gradient.
- Now Playing retains all CY-61 controls and geometry. Check normal audio buffer health during browsing and repeated Back/paging on the required real outputs.

## Later flash entry (documentation only; not executed)

Factory backup already exists per `BUILD-CYDPOD.md`; retain it. COM7 is the documented JC2432W328C port and must be confirmed by the user at the later integrated demo. Enter manual download mode (hold BOOT, tap RESET, release BOOT); use `--before no-reset --after no-reset`, all four regions and no erase. Restore image: `E:/dev/.worktrees/CYDpod/cy67-jc2432/.tools/images/codec-pool-v8-jc2432w328c`, using the same four-region commands. Neither operation formats SD or clears NVS.

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy78-chrome
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v15-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
# Press RESET after verification. Rollback: set $img to the v8 directory above.
```

The required Orca workspace-comment checkpoint was attempted, but `worktree set --worktree active` returned `selector_not_found` for this external Git worktree. Progress/heartbeat checkpoints were sent through the live dispatch instead.
