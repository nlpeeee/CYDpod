# Classic touchscreen UI planning evidence

Date: 2026-10-25. Checkout: `E:\dev\CYDpod`, branch `cydpod/miniaac`, HEAD `d9084b3`.

## Scope

Created [the implementation plan](../specs/classic-touch-ui-plan.md) and
[interactive HTML concept](classic-touch-ui-prototype.html). Firmware, dependency
inventories, settings, original music and device state were not changed. No
agents were launched, commits made, pushes performed, or serial/device commands
run. The pre-existing modification to `docs/research/build-baseline.md` was
preserved; its SHA256 after the check was
`982ca7992c7176050821a0ad315237fcee41e177829f757b7504ee3efa9089a7`.

The recommendation follows inspection of the maintained player's display,
touch, UI lifecycle, layout, theme/style, library, playlist, storage, audio and
application-loop interfaces, the governing specification, older implementation
checkpoint, and newer CY-33/CY-13c evidence. In particular:

- The application already supports direct drawing and down/move/up input.
- Library screens currently page; list dragging is proposed work.
- `playlist_select()` can change playback; browsing needs separate state.
- M4A seeking is a no-op. Metadata, artist/album grouping and cover art need
  implementation rather than just different labels.
- Player free-heap numbers in CY-33 are estimates. CY-13c concerns another image
  and establishes muted idle, not integrated playback qualification.

## Repository check

Command: `powershell -NoProfile -File tools/check.ps1`. Exit code **0**.
Full retained output: `.tools/logs/classic-touch-plan-check.txt`.
Build inventory: `.tools/logs/build-results.json`.

| Image | File bytes | Static RAM bytes |
| --- | ---: | ---: |
| Stock player | 1,336,576 | 56,176 |
| Uploader | 1,147,600 | 48,900 |
| Maintained CYDpod player | 1,347,600 | 76,632 |
| AAC probe | 1,197,216 | 77,344 |

All images fit the locked 1,966,080-byte slot. Maintained player SHA256:
`a1b177b2927f44b87175130bb8b03cb5d1abe8a6868e6b2f768bd0050c74391e`.

Selected output:

```text
Compiling cydpod-player...
Sketch uses 1347453 bytes (68%) of program storage space. Maximum is 1966080 bytes.
Global variables use 76632 bytes (23%) of dynamic memory, leaving 251048 bytes for local variables. Maximum is 327680 bytes.
PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=20656 live=0
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

The host fixture matrix uses Helix/ADTS and deliberately rejects its M4A
fixtures; this matrix is not evidence for the maintained player's MiniAAC M4A
runtime path. Existing upstream GCC-pragma warnings occur in the MSVC host build;
the check exits successfully. No hardware acceptance is inferred.

## Concept verification

The HTML has inline assets and no runtime network dependency. Node's
`vm.Script` syntax check passed. A temporary smoke variant in the chat scratch
folder exercised the concept in hidden headless Chrome using an isolated
profile, without opening the user's normal browser profile.

The first launch under the filesystem/process sandbox failed to start Chrome's
GPU process. The approved sandbox escalation succeeded with browser exit 0.
The resulting DOM reported:

```text
PASS: navigation, track, back offset, pause, volume, landscape and portrait
```

Checks covered Home -> Music -> Songs -> track -> Now Playing, returning to a
scrolled list, pause, volume step adjustment, 320x240 and 240x320 dimensions, and
footer fit in both orientations. The landscape Now Playing screenshot was
visually inspected. Screenshot and smoke DOM remain in this chat's scratch
folder as `classic-touch-ui-review.png` and `concept-smoke-dom.html`; the
reviewable concept is retained in the repository and copied into chat scratch
as `classic-touch-ui-concept.html` for inline display.

These checks validate the browser concept's basic flow/layout only. They do
not validate resistive-touch jitter, drag cancellation on hardware, AAC/Bluetooth
timing, memory budgets, seek, real output selection or audio quality. Those are
explicit future implementation gates in the plan.

## Workspace checkpoint

Attempted the required checkpoint with the version-matched Orca CLI:

```text
orca worktree set --worktree active --comment "Reviewing iPod-inspired touch UI; retain player framework and audio services" --workspace-status in-progress --json
```

The CLI returned `runtime_unavailable`: it could not read
`C:\Users\np\AppData\Roaming\orca\orca-runtime.json`.
No successful workspace-card update is claimed; no app restart or worker-state
mutation was attempted. The repository documents preserve this checkpoint.

## User revision: portrait and required metadata browsing

The user confirmed portrait and Artist/Album/Song selection. Updated the plan
to make 240x320 portrait the product target, require all three metadata views
in the first usable UI, and separate optional artwork from the required
catalogue milestone. Added proposed on-card record/index structure, build and
refresh lifecycle, bounded paging, grouping/sorting rules, and interruption
recovery. Estimates now include the required metadata work.

Source inspection confirmed `storage.cpp` writes `CYDLIB3` at
`/.sys/library.idx`, with empty title/artist fields and filename fallback for
M4A. Its current invalidation is path+size CRC; this cannot detect every
same-size retag. `id3.*` is retained MP3 ID3 code, not M4A music-tag support.
MiniAAC's current adapter exposes audio properties, not artist/album tags.

The revised concept defaults to portrait and supports sample Artist -> Album
-> Song, direct Album -> Song and alphabetically sorted Songs. Starting a song
sets the sample group's queue; browsing another group leaves playback alone.
The browser smoke run exited 0 and reported:

```text
PASS: portrait, Artists/Albums/Songs, group membership, queue isolation, next, title sorting, back offset and footer fit
```

The portrait Music-screen screenshot was visually inspected. Scratch artifacts:
`classic-touch-portrait-v2.html`, `portrait-v2-review.png` and
`portrait-v2-smoke-dom.html`. JavaScript syntax validation passed. This revision
only changes the plan/concept/evidence; the earlier successful firmware check
above remains the baseline and was not rerun for these document-only changes.
No real SD catalogue or firmware feature is claimed as implemented.
