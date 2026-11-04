# CY-61: Now Playing, proposal A

Base `cydpod/ui-layout` / `956c2b5`; branch `cydpod/nowplaying`, worktree `E:/dev/.worktrees/CYDpod/cy61-nowplaying`. No device/serial/flash commands ran, no user media was modified, and no vendor source changed. The required AAC/AirPods and wired hardware acceptance remains open.

Implementation commits: `3ab4d4d` (page, input, tests and v12 tag), `75a762a` (intentional geometry golden, separate commit), `c156187` (painted faces and additional touch evidence), `1ee61f0` (preserve the existing UTF-8 strip-release contract on direct ASCII draws). Parent-label and coherent-volume repaint correctness plus regression: `7c6d43e`. Pixel geometry lives only in `classic_layout.h`; other Classic pages retain their prior geometry.

## Geometry

Rectangles are `x,y,w,h`; text entries give centre y and pixel height. Landscape is identical in rotations 1 and 3.

| Landscape 320 x 240 | Before | After |
| --- | --- | --- |
| Top band | 0,0,320,44 | 0,0,320,36 |
| Back hit | 0,0,64,44 | 0,0,100,40; parent name |
| Favourite hit | none | 230,0,40,40 |
| Options hit | 160,196,160,44 | 272,0,48,40 |
| Title / artist / album | 54/16, 74/16, 90/16 | 54/22, 78/14, 96/12 |
| Seek track | 12,104,296,5 | 14,118,292,8; 16 px knob |
| Seek hit | 8,92,304,44 | 6,100,308,40 |
| Previous | 4,140,98,52 | 56,148,70,48 |
| Play/Pause | 110,140,98,52 | 130,146,80,52 |
| Next | 216,140,98,52 | 214,148,70,48 |
| Shuffle / repeat | only Options | 0,148,52,48 / 284,148,36,48 |
| Volume hit / track | separate page | 0,200,232,40 / 44,216,150,8 |
| Output hit / face | only Options | 232,200,88,40 / 238,205,76,30 |
| Old Player footer | 0,196,320,44 | removed |

The painted transport faces compensate for the shared button helper's style inset using a clipped viewport, so the face dimensions above match the SVG as well as the input cells. Existing icons, theme colours and pressed face colours are retained.

| Portrait 240 x 320 | Before | After |
| --- | --- | --- |
| Title / artist / album centre y | 120 / 140 / 156 | 94 / 118 / 136 |
| Note placeholder | 92,48,56,56 | removed |
| Seek track / hit | 12,184,216,5 / 8,172,224,44 | 14,198,212,8 / 6,180,228,40 |
| Previous / Play / Next | 4,220,72,52 / 84,220,72,52 / 164,220,72,52 | 40,228,36,48 / 80,226,80,52 / 164,228,36,48 |
| Volume track / hit | separate page | 44,296,70,8 / 0,280,152,40 |
| Output face / hit | only Options | 158,285,76,30 / 152,280,88,40 |

The golden records all 17 modes, four variants and rotations 0/1/3; the explicit proposal assertions check the landscape seek, transport, volume and chip rectangles. Portrait remains bounded and non-overlapping. Top controls extend four pixels below the 36 px painted band to provide 40 px favourite/Back/options targets; text begins below that extension.

## Behaviour and resource use

Seek captures duration and track serial at down, previews the existing truthful position while held, maps endpoints to 0 and duration minus 1, and commits once through `audio_seekTo` at release. Preview time updates during dragging; the existing vertical escape of more than 24 px permanently cancels, track changes cancel, and the final release uses CY-62's 8 px tolerant slop without retargeting. Unknown duration/unsupported seek retains the existing failure path.

Volume uses `audio_setVolume` / `audio_volume`, the existing 0..100 gain scale and clamping shared with AVRCP up/down. Down immediately sets the value; moves apply live gain. Only the slider strip/knob and numeric field redraw when the value changes, including externally changed audio volume. Each repaint captures one volume value for the knob, number and shown-value cache, so a concurrent AVRCP change remains detectable on the next tick. A changed drag arms the existing 1500 ms deferred save exactly once at release; no settings write or new save arm occurs per step, and a prior pending save cannot fire during a held slider. Leaving the page flushes pending changes through the existing path. No Bluetooth protocol, audio, I2S or classifier implementation changed; host AVRCP evidence stimulates the same shared audio-volume API used by the unchanged AVRCP callback, not a radio link.

Transport commands run once on down and the release/long hold is swallowed. All five transport buttons pass the existing `pressed` argument; shuffle/repeat stay lit while enabled. The star shares the existing favourite capacity/error handling. Output opens the existing Output page and displays the Bluetooth name (ellipsis when long) or the full Headphones label; name changes refresh without requiring reconnection. Options is in the top bar. Parent name and queue position appear in the band; a Settings-to-Player-to-Back regression checks non-library parent labels as well.

ASCII title/artist/album use pixel heights 22/14/12 by sampling the already pinned TFT_eSPI font-2 bitmap directly from flash; there is no new font file or bitmap allocation. Long ASCII text ends in an ellipsis. Existing non-ASCII cached title strips keep their native height and rendering path; no new strip, font or artwork cache was added. Host SVG text uses Arial, so screenshots establish placement, not device font raster quality.

Artwork removal saves **0 B static RAM and 0 B decoder flash**: the baseline never loaded, decoded or cached artwork; it painted only the portrait note placeholder. That drawing is deleted. There was no artwork-only decoder/buffer to remove and none is retained for another page. Catalogue embedded-picture skipping and all tag parsers are unchanged.

| Normal player budget | CY-77 base | CY-61 | Delta |
| --- | ---: | ---: | ---: |
| Default program storage (B) | 1,469,809 | 1,475,061 | +5,252 |
| Default app image (B) | 1,469,952 | 1,475,216 | +5,264 |
| Default static RAM (B) | 91,264 | 91,264 | +0 |
| W328C program storage (B) | 1,506,849 | 1,512,041 | +5,192 |
| W328C app image (B) | 1,506,992 | 1,512,192 | +5,200 |
| W328C static RAM (B) | 92,840 | 92,840 | +0 |
| Codec pool, both targets (B) | 40,960 | 40,960 | +0 |


No persistent heap is added. The codec pool remains exactly 40,960 B, checked from each ELF by the repository check. The only added live UI state is a pressed target and drag-change flag; removal of the old sleep-footer cache offsets part of this. New text fitting uses a bounded 128 B stack buffer, not heap.

Browse dirty-row scheduling remains one row per tick and is unchanged. Player track metadata/text only redraw on track changes, time text at second changes, progress in its bounded strip at the existing 250 ms cadence, and volume only on a changed value. The new 16 px-high bar erase region adds a bounded few thousand pixels per update; it adds no SD reads per drag step, no per-second full-screen redraw and no new audio-side work. This supports unchanged audio scheduling by inspection, but actual `stat: buf`, underruns, touch latency and stack headroom require the board run below; no synthetic/compile result is a hardware audio-health measurement.

## Validation evidence

Required checks ran sequentially and both exited **0**:

```text
powershell -NoProfile -File tools/check.ps1
  PASS: compile-only checks completed; no device commands were run.
  DEFAULT_CHECK_EXIT=0
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia
  PASS: compile-only checks completed; no device commands were run.
  JC_CHECK_EXIT=0
```

Retained complete output: `.tools/logs/cy61-default-final-check.txt` and `.tools/logs/cy61-jc2432w328c-check.txt`; separate host run `.tools/logs/cy61-host-final.txt` also exited 0. Build summaries are `.tools/logs/build-results.json` and `build-results-jc2432w328c.json`. The W328C command also built its forced-Headphones player, two tone images and diagnostic image. Orca workspace-comment updates returned `selector_not_found` for this external Git worktree; checkpoints were sent through the live Task's orchestration status/heartbeat messages instead.

Host output includes `CY61_NOWPLAYING_OK` twice (default and wired), `HEADPHONES_UI_OK` and the CY-62 180-case classifier-to-command matrix. New checks cover target edges/outside points, endpoint clamps, one-event slider down, monotonic volume and seek drags, live shared volume and numeric repaint, zero writes/arms during drag, one arm and one write at the actual deadline, preview time while seeking, tolerant seek release boundary, favourite and output slop, explicit pressed face colour on down, transport release suppression, shuffle/repeat, favourite toggle and Output/Options navigation. Existing wake, cancellation, queue isolation, playback/keyboard/search and route tests pass.

Golden: `tests/golden/classic_layout.txt`, compared normally without regeneration by the final default check. Actual dump: `tests/out/classic_layout.actual.txt`. Snapshots: `tests/out/cy61-player-portrait.svg`, `cy61-player-landscape.svg`, `cy61-player-landscape3.svg`; visual review PNGs `.tools/logs/cy61-portrait-final.png`, `.tools/logs/cy61-landscape-final.png`. The portrait and landscape PNGs were inspected; both show all controls within the display. No Player artwork reference remains in `classic_screen.cpp`.

Primary image: `E:/dev/.worktrees/CYDpod/cy61-nowplaying/.tools/images/codec-pool-v12-jc2432w328c/app.ino.bin`, **1,512,192 B**, SHA256 `ce68fcb7e5e4f118e9d87cb59896ae2f789b0fa61ae5fc321e15586dc0dbcb6e`. Adjacent `MANIFEST.json` records the four flash regions plus ELF/map/merged image; every exported file in all 5 generated second-board manifests was checked against its byte count and SHA256 (35 files). The forced-Headphones v12 directory is a test variant; the normal v12 directory above is the requested image.

| Primary flash region | File | Bytes |
| --- | --- | ---: |
| 0x1000 | `app.ino.bootloader.bin` | 25,024 |
| 0x8000 | `app.ino.partitions.bin` | 3,072 |
| 0xe000 | `boot_app0.bin` | 8,192 |
| 0x10000 | `app.ino.bin` | 1,512,192 |


## Board visual / audio checklist (user only)

- On landscape Now Playing, verify parent/count/star/options in the 36 px top band; title, artist and album occupy the upper text block with no artwork or empty artwork frame. Check a long title's ellipsis and non-ASCII labels.
- Check seek at x=14 and x=306, its 8 px track and 16 px knob, live elapsed preview and minus-remaining label. Test tap, drag, a small release excursion, a large vertical escape, track change during drag and an unsupported file.
- Tap and hold Previous, Play/Pause and Next; each acts once at down with a visible pressed face, then release does nothing. Toggle shuffle/repeat and favourite; open Options at the top right.
- Drag volume to 0 and 100 while playing; it should change immediately and smoothly without audio glitches, and retain the final value after the deferred save/reboot. Use AVRCP volume keys, then the slider, checking both alter the same displayed gain.
- Check the output name/Headphones chip; tap it to enter Audio Output. Repeat in portrait and the opposite landscape rotation, including the small portrait volume track.
- With real AAC-LC 44.1 kHz stereo through the required outputs, compare `stat: buf` and underrun counters before/after repeated drags and transport touches; hardware evidence remains unverified.

## Flash entry (documentation only; not executed)

Factory backup already exists per `BUILD-CYDPOD.md`; keep it. COM7 is the documented second-board port, to be confirmed by the user before use. Put the JC2432W328C into manual download mode (hold BOOT, tap RESET, release BOOT), then use the four firmware regions below. The restore player is `codec-pool-v8-jc2432w328c` in the CY-67 worktree; use the same four-region commands with that image directory to roll back. Do not format media or erase NVS.

```powershell
Set-Location E:\dev\.worktrees\CYDpod\cy61-nowplaying
$jcPort = 'COM7'
$jcTool = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$img = '.tools/images/codec-pool-v12-jc2432w328c'
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
& $jcTool --chip esp32 --port $jcPort --baud 460800 --before no-reset --after no-reset verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
# Press RESET after verification. Rollback directory:
# E:\dev\.worktrees\CYDpod\cy67-jc2432\.tools\images\codec-pool-v8-jc2432w328c
```
