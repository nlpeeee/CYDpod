# Portrait Classic Touch implementation

Date: 2026-10-25. Implemented in `cydpod/miniaac`, starting from `d9084b3`.
The user approved implementation while AFK. This is software/host/compile
validation. No device was opened, reset, flashed or formatted during this work.

## Implemented

- Portrait Home/Music menus, grey header, white lists, blue press feedback,
  chevrons, large Back/Home/page/transport targets and an eight-page bounded
  navigation stack. Dragging scrolls by rows; page buttons are an alternative.
  Drags, including holds over four seconds, suppress row activation/calibration.
- Artist -> Albums -> Songs, direct Albums -> Songs and title-sorted Songs.
  Artist pages include All Songs; album tracks follow disc/track order. Back
  restores the parent's previous list position.
- Embedded iTunes-style M4A `ilst` tags: title, artist, album, album artist,
  disc/track numbers and compilation marker. UTF-8/UTF-16BE text is bounded,
  trimmed and sanitized; malformed UTF-8 is replaced. Missing tags fall back
  to filename/Unknown Artist/Unknown Album.
- Albums group by title, album artist (or compilation/track-artist fallback)
  and folder. Numbered Disc/CD subfolders share the parent album. Different
  artists' Greatest Hits remain separate; compilations in one album folder can
  contain multiple track artists.
- Independent playlist/folder/favourite previews. Browsing does not change the
  active queue. Track activation commits the group and chosen song together.
  Failed reads/allocations preserve playback; the active queue owns its buffer
  after a browser preview is released.
- Now Playing with metadata, elapsed/remaining time, read-only progress,
  previous/play/pause/next, volume slider/steps and favourite/shuffle/repeat/output
  options. Bluetooth, calibration and legacy advanced settings remain reachable.
  Unimplemented stereo line/headphone routes are visibly unavailable.
- Boot opens Home with the restored track paused. Saved track paths survive
  catalogue reordering. Catalogued queues restore only with a matching catalogue
  checksum. Decoder/open/allocation/task errors have synchronized UI reporting.
- A one-time migration selects portrait/Classic Touch with GIF backgrounds off,
  retaining calibration, bonds, output, volume and credentials. Old enum IDs are
  unchanged; palette/style entries are appended.

`classic_screen.cpp` uses the existing TFT_eSPI, UI, touch, audio and Bluetooth
services. Changed list rows draw one per loop, backing off when the audio ring
is low. No framebuffer, graphics-framework migration or codec/radio replacement
was introduced. Unicode strip preparation streams labels instead of collecting
all title/artist/album strings in RAM. The existing optional CJK font/cache path
is retained; without the font/cached strips the built-in fallback cannot display
every Unicode character. New core menu labels are English.

## SD format and recovery

The maintained application now uses `/.sys/library-a.cat` and
`/.sys/library-b.cat`; the old text `library.idx` is left alone. Each generation
has a 60-byte header, 712-byte fixed track records, three sorted 16-bit ID arrays
and 10-byte group records. The packed format is little endian. Limits excluding
terminators: path 255, title/album 127 and artist/album-artist 95 UTF-8 bytes.
Long paths reject rebuilding rather than silently addressing another file.

Startup validates bounds, IDs, records and header/body CRCs, then compares a
path/size/modification-time scan signature. Changed media builds the inactive
generation and publishes its header last; interrupted/full-card builds retain
the previous valid generation. FAT rename is not assumed transactional.
Refresh Library writes a request marker and reboots into a forced rescan,
covering same-size retags that signature heuristics can miss. Music files are
never retagged or transcoded. Card-full/corrupt-index states remain recoverable.

The inherited limits remain 2,000 `.m4a` files and two subdirectory levels under
`/music`. Scanning streams records; building sorts at most 4,000 bytes of IDs
against disk records with an iterative heapsort that stops on I/O failure. Shared
record scratch uses 1,424 bytes and avoids records on every recursive scan frame.
Visible entries are read directly; no whole-library name/offset table is added.
Folder/playlist names have an on-card offset index as well.

Existing playback queue buffers remain two bytes per track. A catalogue queue
is staged temporarily (at most 4,000 bytes); a source preview can retain at most
4,000 bytes until its browse branch closes. Admission reserves a 20 KiB heap
floor plus working allowance and a 4 KiB contiguous-block margin, including
existing M3U matching scratch where needed. This is defensive admission, not a
measured device heap result.

## Checks and evidence

Repository command: `powershell -NoProfile -File tools/check.ps1`.
Full final output: `.tools/logs/classic-touch-final-check.txt`.
Image inventory: `.tools/logs/build-results.json`. The check builds stock player,
stock uploader, maintained CYDpod player and AAC probe, then runs the new
catalogue/UI tests and existing synthetic media matrix. Firmware-image identity
and final results are recorded in the completion section below.

`tests/build_catalog.cmd` compiles the actual storage/playlist code against a
host filesystem adapter and checks real FFmpeg-tagged M4A plus synthetic tags,
UTF-8/UTF-16, invalid UTF-8/malformed boxes, grouping, missing/duplicate tags,
multi-disc order, compilations and 1,000 tracks with ten artists/twenty albums.
It covers warm cache, forced refresh, additions, damaged generations, simulated
full-card rebuild, failed reads, low-memory queue admission and live-queue
isolation. It also tests bounded navigation, scroll clamping and I/O-safe sorting.

The UI test compiles actual `classic_screen.cpp`, `ui.cpp`, palette/style and
catalogue/playlist code; hardware/audio/font endpoints are mocked. It exercises
Home/Music/Artist/Album/Song/Now Playing, play/pause/skip hit targets, volume,
Back to scrolled lists, button cancellation and a five-second drag. Drawing
calls generate `tests/out/ui-*.svg`. The chat gallery uses these captures, with
an approximate host font; it was visually reviewed. These are layout/logic
checks, not CYD photographs, audio or latency measurements.

MSVC emits narrowing/CRT warnings and the existing foreign-GCC-pragma warnings;
compile/test failures remain fatal. The existing Helix/ADTS fixture matrix is
separate from MiniAAC M4A/device playback acceptance.

## Remaining qualification

The new image still needs installation on the identified CYD and the full UI
exercised with required AAC, actual AirPods Pro 2 and the external stereo DAC.
No device touch-latency, heap-floor, stack, SD-stall, underrun, soak or required
output success is claimed. The original G1/G2 gates remain open; the later user
instruction authorized software UI implementation ahead of those results.

Artwork is a placeholder. Seeking/position resume remain unsupported; restored
tracks restart from the beginning. Metadata support is common iTunes-style
`ilst`, not every `mdta`/freeform variant. Sorts fold ASCII case and otherwise
use deterministic UTF-8 bytes, without locale collation/transliteration.
Multi-artist tags remain one display value rather than being guessed apart.

The separate stock uploader is unchanged and still filters MP3 filenames; AAC
loading for this build uses the SD card. Matching M4A uploader work remains
separate. Battery reporting is omitted pending verified measurement hardware.

MiniAAC-derived box traversal retains its full MIT notice in `library_core.h`;
pinned vendor sources/inventories are unchanged. Apple's
[metadata structure documentation](https://developer.apple.com/documentation/quicktime-file-format/metadata_atoms_and_types)
was consulted for terminology. No new dependency was downloaded.

The Orca card checkpoint was attempted but returned `runtime_unavailable` while
reading runtime metadata. These documents preserve progress; no worker state
was changed or replacement agent launched. The pre-existing
`docs/research/build-baseline.md` modification was preserved.

## Completion evidence

Final `powershell -NoProfile -File tools/check.ps1` exited **0** with:

```text
PASS catalogue: real/synthetic M4A tags, artists/albums/titles, multidisc, compilations, 1000 tracks, warm cache, full-card recovery, corrupt generation, refresh, queue isolation, gestures and bounded navigation
PASS firmware UI: Home, Artist/Album/Song routes, track selection, queue isolation during drag, 5-second drag suppression, button cancellation, volume and Back position; SVG snapshots recorded
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

The UI test additionally verifies return from a legacy advanced screen to its
Classic Touch settings parent. Final maintained-player binary:
`.tools/images/cydpod-player/app.ino.bin`, **1,364,912 bytes**, SHA256
`d86eea41c9780f23e24b88a50a4ea8514af376651573f7d1f539d5b13b33b519`.
It has 601,168 bytes remaining in the locked 1,966,080-byte slot. Static RAM is
**78,720 bytes**, an increase of **2,088 bytes** over CY-33's 76,632 bytes.
These are compiler/static figures, not free heap measured during playback.

The final chat gallery is `classic-touch-implemented-v2.html`, assembled from
the checked firmware drawing captures. Its Chrome render exited 0; the reviewed
image is `classic-implemented-v2-review.png` in the chat scratch folder.
The pre-existing build-baseline document SHA256 remains
`982ca7992c7176050821a0ad315237fcee41e177829f757b7504ee3efa9089a7`.
No commit/push or hardware operation was performed.
