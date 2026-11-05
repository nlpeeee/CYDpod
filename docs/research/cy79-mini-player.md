# CY-79: browse mini-player (UI-2)

Branch `cydpod/ui-mini`, worktree `E:/dev/.worktrees/CYDpod/cy79-mini`, exact base `cydpod/ui-chrome` at `966a154`. The implementation was split into **79a** (`0cf3fbc`, mini-player and new hit zones above the existing footer) and **79b** (`a4f7d9d`, footer removal and paging migration), without an intermediate image/flash request. The geometry golden is its own commit `172a6a4` with the intentional rectangle-change reason. Only the assigned production files, UI test, existing drawing shim, golden and this report changed.

## Behaviour and geometry

The 44 px bottom band shows play/pause at the left, clipped title/artist text in the middle and Next at the right, with a 2 px accent progress line along its top. Play and Next act once on touch-down, use the theme pressed ink immediately and swallow release/hold events. A short press retains its highlight until the next tick after the 80 ms deadline; moving outside the captured target clears it. Transport commands, queue construction and metadata drawing reuse existing helpers; no audio or touch-classifier changes were made.

Text opens Now Playing as a temporary replacement of the current page, preserving one fixed-size Page value and keeping navigation depth unchanged, even at the stack limit. Back restores the same page and exact top row; source previews continue through the existing release/rebuild path. If Now Playing already exists below an Options page, text returns to that existing entry. No new persistent heap is used.

With an empty queue the entire band says **Shuffle all**, waits for tolerant release and starts the existing full Songs shuffle queue. With no songs it is disabled. The dispatch/approved plan's 44 px empty state takes precedence over the older SVG footnote proposing a fifth landscape row.

The right strip is now 30 px wide: its upper/lower halves page up/down by `rows - 1`, clamped at both ends. The thumb and up/down glyphs remain visible; it is not an alphabet scrubber (CY-83 owns that later chunk). At navigation depth >=4 a Home glyph appears in its own header target beside Back, and the page title uses the remaining interval. Header parent labels, route contrast, 40 px row caption spacing and native UTF-8 drawing remain from CY-78. All pixel constants remain in `classic_layout.h`.

Rectangles are `x,y,w,h`:

| Rectangle | CY-78 landscape | CY-79 landscape | CY-79 portrait |
| --- | --- | --- | --- |
| Header | 0,0,320,36 | unchanged | 0,0,240,36 |
| Rows | four x 312x40, y=36/76/116/156 | four x 290x40, same y | six x 210x40, y=36/76/116/156/196/236 |
| Scroll strip | 313,36,7,160 | 290,36,30,160 | 210,36,30,240 |
| Page Up / Down | footer cells 64,196,48,44 / 112,196,48,44 | 290,36,30,80 / 290,116,30,80 | 210,36,30,120 / 210,156,30,120 |
| Bottom band | 0,196,320,44, Home/Up/Down/Playing | 0,196,320,44, mini-player | 0,276,240,44, mini-player |
| Play / Text / Next | absent | 0,196,52,44 / 52,196,216,44 / 268,196,52,44 | 0,276,52,44 / 52,276,136,44 / 188,276,52,44 |
| Progress | absent | 0,196,320,2 | 0,276,240,2 |
| Deep Home | absent | 100,0,40,36 | 100,0,40,36 |

Thus `36 + 4*40 + 44 = 240` and `36 + 6*40 + 44 = 320`. Rotations 1/3 share the landscape rectangles and 0/2 the portrait rectangles. CY-61 Now Playing keeps its independent header, seek, favourite, Options, transport, shuffle/repeat, volume and output bottom row. The keyboard retains its existing action row.

## Validation and scheduling

Both required checks ran sequentially on final firmware code `172a6a4` and exited **0**:

```text
powershell -NoProfile -File tools/check.ps1
  PASS: compile-only checks completed; no device commands were run.
  DEFAULT_CHECK_EXIT=0
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia
  PASS: compile-only checks completed; no device commands were run.
  JC_CHECK_EXIT=0
```

Complete output is retained in `.tools/logs/cy79-default-final-check.txt` and `.tools/logs/cy79-jc2432w328c-final-check.txt`, with `build-results.json` and `build-results-jc2432w328c.json`. The second check also compiled the forced-Headphones player, both tone variants and the diagnostic image. Development host output is `cy79-host-development3.txt`; an initial missing Home icon enum was resolved using local primitives, and navigation assertions were updated to use stack depth because the new mini artist label is also visible on list pages. Final checks compare the committed golden without regeneration.

The default check includes both normal and Headphones-enabled host UI variants. New regressions cover mini transport outer edges, exactly one command on down with no release/hold repeat, pressed paint calls, title edges and repeated open/Back without depth growth, return through Options, exact list-position restoration, 30 px strip halves and paging clamps, deep Home edges and captured-target release slop, and empty-queue Shuffle all. An independent comparison against `966a154` also confirms the full Player and Keyboard hit-map lines are byte-for-byte identical for all four rotations and state variants. The geometry golden covers 17 modes x four state variants x all four rotations; queued/deep and empty states are explicit. CY-61, CY-62, CY-78, keyboard, source-preview and output regressions pass with the production UI code.

The host drawing shim counts actual fill calls and row fills. After settling the page, a one-second progress tick issues **two rectangle fills** confined to the 2 px line and **zero row fills / text calls**; a tick at +999 ms draws nothing, the next millisecond draws the line again. Full mini text only repaints on entry, track/play-state change or pressed-feedback restoration; paging and unchanged connection polls do not repaint it. Metadata reuses the existing `playingInfo` record. List loading remains one dirty row per tick with the existing low-buffer gate, and the codec pool remains 40,960 B. Added periodic TFT work is at most 2*width*2 pixels per second (1,280 pixels in landscape), with no periodic catalogue read; this supports unchanged audio scheduling by inspection, not a measured `stat: buf`/underrun result.

Host SVGs: `tests/out/cy79-mini-landscape.svg`, `cy79-mini-landscape3.svg`, `cy79-mini-portrait.svg`, `cy79-mini-portrait2.svg`, `cy79-mini-empty.svg`. Inspected render: `.tools/logs/cy79-visual.png` (and its HTML). They show 4/6 rows, thin progress, separate transport/text targets, empty-queue band and scroll strip. Host Arial text is placement evidence rather than the device bitmap font or hardware evidence.

## Resource budget

| Normal player budget | CY-78 base | CY-79 | Delta |
| --- | ---: | ---: | ---: |
| Default program storage (B) | 1,474,913 | 1,476,765 | +1,852 |
| Default app image (B) | 1,475,056 | 1,476,912 | +1,856 |
| Default static RAM (B) | 91,264 | 91,352 | **+88** |
| W328C program storage (B) | 1,511,913 | 1,513,617 | +1,704 |
| W328C app image (B) | 1,512,064 | 1,513,760 | +1,696 |
| W328C static RAM (B) | 92,840 | 92,928 | **+88** |
| Codec pool, both targets (B) | 40,960 | 40,960 | **0** |

The linked static RAM delta is 88 B on each board, below the 100 B chunk limit. ELF symbols show the saved Page is 76 B, the deadline 4 B and the two state fields 1 B each; alignment accounts for the remainder of the linked delta. There is no new persistent heap, metadata cache or codec allocation. Default ELF pool symbol: `0000a000 b codecPool`.

## Image and visual checklist

Primary image: `E:/dev/.worktrees/CYDpod/cy79-mini/.tools/images/codec-pool-v16-jc2432w328c/app.ino.bin`, **1,513,760 B**, SHA256 `49bb856164726b41aabc657ec6161de897424a3fd8e89f3fd1c27bcf7439a4ff`. This is the normal player using the saved output route, not the forced-Headphones variant. The unchanged check script exports a private intermediate v12 directory; its final normal-player artifacts were copied into reserved **v16**, the manifest regenerated with source commit `172a6a41b288a984f694c3343093ee096046347c` and base `966a154a28c2bb553cbc33f1eeeb1848324cfa54`, and all seven exported files independently reverified for byte count and SHA256. Evidence: `.tools/logs/cy79-manifest-verification.txt`; destination `MANIFEST.json` and `MANIFEST.txt`. v13/v14/v15 were not used.

| Flash region | File | Bytes |
| --- | --- | ---: |
| 0x1000 | `app.ino.bootloader.bin` | 25,024 |
| 0x8000 | `app.ino.partitions.bin` | 3,072 |
| 0xe000 | `boot_app0.bin` | 8,192 |
| 0x10000 | `app.ino.bin` | 1,513,760 |

No device, COM, serial, flash or user-media operation was performed. AAC-LC 44.1 kHz stereo, actual AirPods Pro 2 and stereo Headphones playback remain hardware acceptance requirements. The next screen-chain owner is CY-80; no intermediate flash is requested.

For the later integrated board run:

- Landscape Songs and Search Results: four 40 px rows, 30 px paging strip at right, 44 px mini-player below; portrait has six rows. Repeat in rotations 0/1/2/3.
- Tap play/pause and Next, hold, then lift: one action on initial contact, immediate pressed ink, no repeat on lift. The title/artist must stay clipped between the two buttons.
- While playing, watch the thin line advance once per second without list text flicker. Check normal buffer health and underruns while paging on the required real outputs.
- Scroll a long Songs/source list, tap the mini text, then Back: return to precisely the same first visible row; repeat through Options without accumulating pages.
- At depth four (Home > Music > Artists > artist albums), Home should be beside Back; the title and route icon must remain legible. Home returns directly to the root.
- With no queue, see Shuffle all and start it on release. Page-up/down taps advance three rows in landscape or five in portrait and stop at the ends.
- Now Playing must retain every CY-61 control and bottom-row target. Keyboard Space/Clear/Done remains separate.

## Later flash entry (documentation only; not executed)

Factory backup already exists per `BUILD-CYDPOD.md`; retain it. COM7 is the documented W328C port and must be identified by the user for the later integrated demo. Enter manual download mode (hold BOOT, tap RESET, release BOOT), use `--before no-reset --after no-reset`, four firmware regions and no erase. Restore image: `E:/dev/.worktrees/CYDpod/cy67-jc2432/.tools/images/codec-pool-v8-jc2432w328c`, using the same four-region commands. Neither operation formats SD or clears NVS.

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy79-mini
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v16-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
# Press RESET after verification. For rollback set $img to the v8 directory above.
```

The required active workspace-comment update was attempted and returned `selector_not_found`, because this externally assigned Git worktree is not Orca-managed. Checkpoints/heartbeats were sent through the live dispatch instead.
