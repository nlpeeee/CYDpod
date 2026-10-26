# CYDpod touchscreen player: changes against upstream

`app/` is a **modified copy** of `vendor/CYD-MP3-Player/app` (Garionhk/CYD-MP3-Player, Apache-2.0, see `LICENSE-2.0.txt` and `app/src/AnimatedGIF/LICENSE`). `vendor/` itself is unchanged; recorded in `vendor/NOTICES.md`.

Changes (CY-33):
- `audio.cpp`: libhelix MP3 decoder, resampler, ID3/duration/seek code replaced by MiniAAC (`vendor/miniaac`) through `m4a_track.h`. One static `M4aTrack` (workspace + 4 KiB PCM block, no per-track heap) reads the open SD `File`; the decode task pushes frames through the stock abort-aware ring producer. Tracks always start at 0; the position comes from frames the Bluetooth side consumed. Duration comes from the MP4 sample count (0/unknown for ADTS). The decode task is `m4adec`, 6,144 B stack. The `audio_*` interface is unchanged; `audio_seekRelative` is a no-op. MP3 playback is removed from this copy (not switchable); `id3.cpp` stays compiled but unused.
- `m4a_track.h` (new): MiniAAC adapter, adapted from `firmware/cydpod_probe/miniaac_adapter.h`.
- `storage.cpp`: scans `.m4a` (case-insensitive) instead of `.mp3`; index marker `CYDLIB3` rebuilds an old `/.sys/library.idx`; no tag reading (title = file name).
- `screen_player.cpp`: progress-bar seek and long-press +/-10 s removed.
- Bad files (48 kHz, mono, HE-AAC, corrupt, unreadable) print `ERR m4a <reason> <path>` and are skipped; the stock fail-streak stops an all-bad queue. No on-screen note.
- Unchanged: display/touch/calibration, Bluetooth (pairing, saved devices, AVRCP), playlists, settings, speaker route, uploader mode (the uploader still only accepts `.mp3` names; the uploader image is built from `vendor/`, not from this copy).

Build: `powershell -NoProfile -File tools/check.ps1` (job `cydpod-player`, image in `.tools/images/cydpod-player/`). Flash: see `docs/research/cy33-player.md`.

## Classic Touch (2026-10-25)

The maintained player now has a portrait Home/Music/Now Playing UI and
Artist/Album/Song browsing from a rebuildable two-generation SD catalogue.
M4A tag box traversal adapts the pinned MiniAAC MIT wrapper/parser; its full
notice is retained in `app/library_core.h`, and the vendor snapshot is unchanged.
Settings migrate once to the appended Classic Touch palette/style and portrait,
without clearing calibration/bonds/output/volume. Boot restores the track paused.
See [implementation and validation](../../docs/research/classic-touch-ui-implementation.md)
for schema, input/queue behaviour, checks and hardware limitations. The CY-33
list above is historical: metadata/UI are implemented; seeking, artwork,
qualified AirPods and external stereo output remain pending.
