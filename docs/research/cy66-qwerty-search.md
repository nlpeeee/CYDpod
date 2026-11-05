# CY-66: QWERTY Search keyboard

Branch `cydpod/search-qwerty`, worktree `E:\dev\.worktrees\CYDpod\cy66-qwerty`, exact verified base `cydpod/search-session` `06c83f43400efc914581f9591b29423216389cc9`. Production and tests: `6d48dd5`; intentional keyboard golden: `6cbd621`. Changes are limited to `classic_screen.cpp`, `classic_layout.h`, the existing UI harness/golden and this report. CY-65 audio-aware I/O admission and CY-68 slide lock remain separate.

## Behaviour and geometry

Search now has the same three QWERTY rows in every rotation: `qwertyuiop`, `asdfghjkl`, `zxcvbnm`. The 123 page has ten digits plus the 17 basic ASCII punctuation characters `- / : ; ( ) & @ . , ? ! ' " _ + =`. Delete fills the remainder of row three; 123/ABC, Space, Clear and Done fill the bottom row, with Space twice the width of its neighbours. The nine-key A-L row uses the entire width rather than shrinking to ten-key cells.

All rectangles and gap dimensions live in `classic_layout.h`. Hit cells use half-open bounds, and each owns its inset drawing gap; a point on a shared boundary belongs only to the next cell. Character keys fire once on admitted down, claim the press and swallow moves/release/long-hold events, so adjacent jitter cannot retarget or repeat. Control keys retain release activation with captured-target slop. No touch-classifier code changed.

| Region | CY-63 portrait 240 x 320 | CY-66 portrait 240 x 320 | CY-63 landscape 320 x 240 | CY-66 landscape 320 x 240 (rotations 1/3) |
| --- | --- | --- | --- | --- |
| Header / query strip | y=0..36 / 36..68 | unchanged | y=0..36 / 36..68 | unchanged |
| Character rows | 6 columns, 40 x 40 at y=76; 26 letters over 5 rows | y=68..137 / 137..206 / 206..276; 24 px ten-key width, 26/27 px A-L width | 8 columns, 40 x 40 at y=76; A-P / Q-Z pages | y=68..110 / 110..153 / 153..196; 32 px ten-key width, 35/36 px A-L width |
| Delete | (80,236,80,40) | (168,206,72,70) | (0,156,106,40) | (224,153,96,43) |
| 123/ABC | (160,236,80,40) | (0,276,48,44) | (212,156,108,40) | (0,196,64,44) |
| Space | (0,276,80,44) | (48,276,96,44) | (0,196,80,44) | (64,196,128,44) |
| Clear | (80,276,80,44) | (144,276,48,44) | (80,196,80,44) | (192,196,64,44) |
| Done hit rectangle | (160,276,80,44) | (192,276,48,44) | (160,196,80,44) | (256,196,64,44) |

Visible key faces are inset one pixel on every side: the ten-key top-row portrait face is 22 x 67 px inside a 24 x 69 px hit cell; landscape is 30 x 40 inside 32 x 42. Row heights distribute all available space, including the final remainder pixel. All Home, Player, mini-player, Search result and other non-keyboard hit maps remain byte-for-byte identical in the approved golden; independent comparison found exactly four changed lines, all `mode=15` (Keyboard) across rotations 0/1/2/3. The separate golden commit includes the intentional geometry-change reason.

CY-63's 40-byte fixed session, 150 ms debounce, immediate query echo, tick-only deferred apply/Done, same-query cursor/count retention, query bounds, cancellation/reentry/error semantics and obsolete-hit/queue isolation remain intact. The existing 32-byte edited text buffer still admits at most 31 bytes. No storage/index/matching, folding, audio, Bluetooth, I2S, vendor or per-track data changed. Tables are bounded read-only constants; the obsolete second-letter-page flag is removed; no new persistent heap, task or timer exists.

Per-character drawing still repaints only the existing query/status strip. Page switching paints the bounded keyboard once; punctuation increases the old digit-page key count from 10 to 27, but adds no storage work and no recurring full-screen repaint. The one-dirty-row-per-tick and existing audio-buffer/120 ms scheduling rules are untouched. This is source-level audio-health reasoning, not a measured `stat: buf`, underrun, physical latency or drop-free playback claim.

## Host acceptance and visual inspection

`cmd.exe /d /c tests\build_catalog.cmd` exited 0 in `.tools/cy66-host.txt`. The normal and wired harness each pass 1,908 actual character centre/edge/gap/down/move/hold/release cases across all four rotations, every letter/digit/punctuation target, control centres/edges/gaps, outside/query-strip rejection, captured page-key release slop across Space, Delete/Space/Clear/Done and ABC return, page/query retention, 31-byte bounds, and real classifier 1/650/4500 ms holds with no repeats or recalibration from a character hold. Every rotation also checks one warm query open for the final burst at 150 ms, no I/O at 149 ms or in key handlers, and retained progress/no reopen through Done/redraw/Edit.

The entire retained CY-63 lifecycle suite passes, including cold partial-index build cancellation/resume, wrapped deadlines, busy/completed session retention, error retry, obsolete-hit isolation and exact queue selection. Existing Home/Settings/Player/mini-player/navigation/touch tests remain in the host check. While extending the test, the expected full-query toast needed explicit dismissal before classifier input, and Edit needed the real dirty-row scheduler to paint its row before it was tapped; these were harness setup corrections, not production behaviour changes.

Inspected the host recorder's QWERTY and 123 snapshots for all rotations. Labels and controls fit without overlap or clipping; the coordinator independently reviewed the contact sheet with no visual correctness finding. Host Arial recorder output is geometry evidence, not device bitmap-font or physical touch qualification.

All paths below are relative to this worktree, whose absolute path is above:

- `.tools/cy66-keyboard-review.png` and `.tools/cy66-keyboard-review.html`: all four rotations, QWERTY and 123 contact sheet.
- `.tools/cy66-portrait-preview.png` and `.tools/cy66-portrait-preview.html`: portrait QWERTY/123 pair.
- `.tools/cy66-landscape-preview.png` and `.tools/cy66-landscape-preview.html`: landscape QWERTY/123 pair.
- `tests/out/render/search-keyboard-letters-empty-r0.svg`, `tests/out/render/search-keyboard-letters-empty-r1.svg`, `tests/out/render/search-keyboard-letters-empty-r2.svg`, `tests/out/render/search-keyboard-letters-empty-r3.svg`: empty QWERTY snapshots.
- `tests/out/render/search-keyboard-qwerty-all-rows-r0.svg`, `tests/out/render/search-keyboard-qwerty-all-rows-r1.svg`, `tests/out/render/search-keyboard-qwerty-all-rows-r2.svg`, `tests/out/render/search-keyboard-qwerty-all-rows-r3.svg`: typed QWERTY snapshots.
- `tests/out/render/search-keyboard-digits-r0.svg`, `tests/out/render/search-keyboard-digits-r1.svg`, `tests/out/render/search-keyboard-digits-r2.svg`, `tests/out/render/search-keyboard-digits-r3.svg`: 123 snapshots.
- `tests/out/render/inventory.tsv`: full render inventory; `tests/out/classic_layout.actual.txt`: emitted geometry/hit map.

## Device limits and owner checklist

No device, port, serial, flash or user-media operation was performed. AAC-LC 44,100 Hz stereo, actual AirPods Pro 2 and stereo line/headphone hardware acceptance remain open. CY-65 still owns audio-aware synchronous index/result-label I/O admission and subsequent owner-run latency/underrun/heap/stack measurements.

Owner visual checklist: Home > Search shows all QWERTY letters together; tap Q/P/A/L/Z/M at centres and boundaries, hold a character and drift across neighbours; confirm only the down character appears once. Switch 123, enter digits/punctuation, return with ABC, use Delete/Space/Clear, type a burst then Done, and revisit Edit without losing progress. Repeat portrait and both landscapes, checking that Home tiles, mini-player and Player controls still map correctly.

## Linked RAM and flash

Baseline is accepted CY-63 v19 (`docs/research/cy63-search-session.md`); these values come from the final commands' compiler/linker output and actual files.

| Target / metric | CY-63 v19 | CY-66 v20 | Delta |
| --- | ---: | ---: | ---: |
| Default program storage | 1,452,649 B | 1,452,377 B | -272 B |
| Default static RAM | 91,056 B | 91,056 B | 0 B |
| Default exported app | 1,452,800 B | 1,452,528 B | -272 B |
| W328C program storage | 1,489,825 B | 1,489,533 B | -292 B |
| W328C static RAM | 92,632 B | 92,632 B | 0 B |
| W328C exported app | 1,489,968 B | 1,489,680 B | -288 B |
| Codec pool (both) | 40,960 B | 40,960 B | 0 B |
| Search session (both) | 40 B | 40 B | 0 B |

`xtensa-esp32-elf-nm -S --defined-only` confirms `searchSession` size `00000028` and `codecPool` size `0000a000` in both ELFs. The two key tables are 27 B and 28 B including terminators (55 B total in flash/DROM). Linked symbols are retained at `.tools/cy66-default-linked-symbols.txt` and `.tools/cy66-w328c-linked-symbols.txt`. Static growth is exactly 0 B on both targets, under the 100 B chunk budget. This is source/linker evidence for zero new persistent heap/state, not a runtime free-heap or peak-stack measurement.

The maintained source and each staged final player source match SHA-256 `49fbca94aab77b9dd0dc97806715ef5ca251a549c295cfae8fa29c78430b6ae4`. The default app image SHA-256 is `0c5866fab66583dbe39c70bffd0f26245781338e1e938221009cb95e84c4eeed`.

## Reserved v20 artifacts and independent verification

Normal saved-route image: `E:\dev\.worktrees\CYDpod\cy66-qwerty\.tools\images\codec-pool-v20-jc2432w328c`. The unchanged build script emits a v12-named directory; all seven exported files and its manifest were copied into the previously absent reserved v20 directory without overwriting an existing image. Each actual file was independently remeasured and hashed against its manifest, every region offset checked, and each of the four region byte sequences compared with its bytes at that offset in the merged image: **7/7 files and 4/4 offsets/merged regions PASS**. Verification script/result: `.tools/cy66_verify_artifacts.py`, `.tools/cy66-v20-verified.json`. Build/ELF/log source names retain v12 inside this private worktree.

| File | Individual flash offset | Actual bytes | Verified SHA-256 |
| --- | --- | ---: | --- |
| `app.ino.bin` | 0x10000 | 1,489,680 | `2b1b6cb90b424110f40449a6fffc4487c2128889ff57fd035bb90e9be7f1623b` |
| `app.ino.bootloader.bin` | 0x1000 | 25,024 | `f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29` |
| `app.ino.elf` | not an individual region | 15,610,320 | `d816b27524823a54f197729d24f4be926f0607289ba58d3ef5efbffcaeebcc0b` |
| `app.ino.map` | not an individual region | 19,462,401 | `6ac1ee12b85eac1c7811dcc6f300aa5042c11f023489b7644284082d2ae4a2a4` |
| `app.ino.merged.bin` | not an individual region | 4,194,304 | `5e4998a593a9d499f2c2a59838029df6c50d1dd5420bffc25b462cff34226e9a` |
| `app.ino.partitions.bin` | 0x8000 | 3,072 | `0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7` |
| `boot_app0.bin` | 0xe000 | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |

Normal `MANIFEST.json` SHA-256: `a6cad41ef88659245226b135ef48f7b6f07049e343b81e2fb6616a378b53afbb`. Bootloader, partitions and boot_app0 hashes match the accepted v19 files. ELF/map are debugging artifacts; the merged 4 MiB file is not an extra flash region. Downloads, caches and build/image/preview artifacts remain ignored and uncommitted.

The separate forced-Headphones companion is `E:\dev\.worktrees\CYDpod\cy66-qwerty\.tools\images\codec-pool-v20-wired-jc2432w328c`; it also passes 7/7 actual file checks plus all four offset/merged-region checks against its own manifest. Companion app SHA-256: `78205f41a7db4e8ade12b1242a3995be477443a5bac899adac0ba68f14bcdff0`. The normal saved-route v20 image above is the primary deliverable.

**Owner-only flash entry; not executed by this worker.** Prior CY-71/CY-81 evidence records that the W328C factory backup exists; retain it. Identify the actual board before using the saved COM7 entry. Enter manual download mode (hold BOOT, tap RESET, release BOOT), use `--before no-reset --after no-reset`, then write/verify only the four regions below (DIO, 80 MHz, 4MB, 460800 baud); press RESET afterward. The restore player is `codec-pool-v8-jc2432w328c` under `E:\dev\.worktrees\CYDpod\cy71-wired-route\.tools\images`, with the same four offsets. No erase, NVS or user-media operation is part of this entry.

```powershell
$jcPort = 'COM7'  # owner must first identify this as the actual W328C
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v20-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
```

## Final checks and retained PASS evidence

Final checks ran **sequentially**, default first, on code/tests `6d48dd5` plus golden `6cbd621`; both exited 0. Only this report was changed afterward. Existing compiler/shim warnings are retained in the full logs.

| Command | Result and log | Complete log SHA-256 |
| --- | --- | --- |
| `powershell -NoProfile -File tools/check.ps1` | exit 0; `.tools/check-default.txt` | `8222c5968f1e7ee2c66bf41cb92bde4d82a58a3ca0feb0c584766ffde65df99b` |
| `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` | exit 0; `.tools/check-jc.txt` | `9115248e6268e86457c2dcc6de2480eaba28cfa542f01eb900c45aee15fa05fe` |

Selected final output (both normal and wired UI invocations include the CY-66 and retained CY-63 PASS):

```text
RENDER_INVENTORY_OK: 148 SVG renders, rotations 0/1/2/3, see render/inventory.tsv
PASS CY-66 QWERTY: 1908 centres/edges/gaps; all letters/digits/punctuation, captured adjacent jitter, held/released keys, classifier 1/650/4500 ms, controls/slop, page retention, 31-byte bound, one query per burst, retained progress and no per-key storage; all rotations
PASS CY-63 Search session: immediate echo, 150 ms burst/rollover, tick-only Done, retained busy/completed cursor/count/no opens, empty no-I/O, cancel/resume, error retry, obsolete-hit and full queue isolation
NAV_GRAPH rotation 0: 17 pages, 18 page/scroll states
NAV_GRAPH rotation 1: 17 pages, 20 page/scroll states
NAV_GRAPH rotation 2: 17 pages, 18 page/scroll states
NAV_GRAPH rotation 3: 17 pages, 21 page/scroll states
HEADPHONES_UI_OK: zero bt_* calls across Bluetooth page, Classic and the Settings tree
RENDER_INVENTORY_OK: 160 SVG renders, rotations 0/1/2/3, see render/inventory.tsv
PASS: compile-only checks completed; no device commands were run.
```

Final W328C normal player compiler/linker evidence:

```text
Sketch uses 1489533 bytes (75%) of program storage space. Maximum is 1966080 bytes.
Global variables use 92632 bytes (28%) of dynamic memory, leaving 235048 bytes for local variables. Maximum is 327680 bytes.
PASS single pool: 3ffc4a40 0000a000 b _ZN12_GLOBAL__N_1L9codecPoolE
PASS: compile-only checks completed; no device commands were run.
```

The default command also passed the existing codec pool, MP3/AAC/WAV, I2S tone/diagnostic, wired fade, Settings/migration and synthetic-media suites. The W328C command built the normal player, forced-Headphones player, both tone variants and diagnostic image. These are compile/host results only; device acceptance remains open as above.
