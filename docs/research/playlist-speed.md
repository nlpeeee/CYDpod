# Playlist speed, playlists inside /music, and volume sync after Sleep

Date: 2026-11-07. Branch `cydpod/power`, firmware commits 52bf3b7 (volume) and 8c2ed33 (playlists).

## Evidence before the change (owner log, power candidate 33656bf)

- Every list open logged `scan≈3950-4120 ms records=1477`, with total time about 4.1-4.3 s. Touch went unread for about 4.2 s (`stat: touch pollGapMax 4215 ms`). The loop task used 45-68 % CPU in those 10 s windows.
- Cause:
  - `build()` → `storage_forEach()` read the path and title of every 712 B catalogue record, at about 2.7 ms per record.
  - It ran on the UI task while the decoder was reading the same card.
  - A preview, and then playing from it, built the list twice. Boot paid the same pass in `scanSources()`.
- `06 - Ska Soul and Retro Pop n=25 found=0 missing=25`: probably tracks one folder deeper than the scan reached (`scanDir` recursed only at depth < 2). Unconfirmed until the next board log.
- No reboot appeared in that log. One audio dropout (`underruns 9`, `ringLow 0/4096`, missingFrames 2432) fell in the window with `play track 77` and `shuffle off`, not during a list open. The lowest free heap was `min 6412`.

## Change

- **`/.sys/paths.keys`:**
  - Header: `CYDKEYS1`, library CRC, count. Then 12 B per track in record order: the CRC of the lower-cased full path, of `/Dir/file` and of `/file`.
  - Written in the boot pass that already collects folders, only when the sources signature changes. It is read 32 tracks (384 B) at a time.
  - If it is missing or invalid, builds compute the same keys from `storage_forEach`, so the old path remains as a fallback.
- **Favourites:** match the full-path key.
- **Folders:**
  - The scan is depth-first, so a folder's tracks form one run in record order. `sources.txt` stores `first`/`count` for each folder, and the build writes that run with no scan.
  - A folder whose tracks are not one run stores `count` 0 and scans as before.
- **M3U:**
  - Each entry's resolved-path key, its `/Dir/file` or `/file` key and a has-folder flag go into the existing on-card match records.
  - The 4 KB sorted index is unchanged.
  - The playlist file is closed before matching, so the build holds one fewer file open.
  - Scores are now 7 exact, 2 last folder and file, 1 bare name, which is a behaviour change: telling apart two copies whose last two folders are identical no longer uses a deeper match. As before, an ambiguous tail plays the first copy, and an ambiguous bare name stays missing.
- **Playlists inside /music:**
  - `scanDir`'s boot pass collects up to 200 `.m3u`/`.m3u8` paths. They are not part of the library signature.
  - They are merged with `/playlists`, sorted by name, and two lists with the same name get ` (Folder)` added.
  - Relative entries resolve against the list's own folder, so a list beside `Tracks/` matches exactly.
- **Sources reuse:**
  - The first line of `sources.txt` is `#sig <crc> <namesCrc> <folders> <lists>`. The signature covers the library CRC, the track count and the sorted list paths.
  - While it matches, and `sources.idx` and the keys are valid, boot makes no library pass.
- **Depth:** `scanDir` recurses at depth < 3 (`/music/a/b/c/song`). Deeper folders are counted and logged as `catalog: N folders below /music/a/b/c/ not indexed`.
- **Volume after Sleep (52bf3b7):**
  - `bt_suspendStack` stops the stack without a guaranteed AVRCP/A2DP disconnect event. `BtVolumeSync` stayed `linked`, so `connection(true)` after wake returned early.
  - As a result the capability handshake (`bt: initial volume sync`) was skipped and the speaker was left unsynced.
  - The stack stop now drops the volume link and the remote-volume flag, as a real disconnect would.

## Checks (run once on 8c2ed33)

- Playlist-queue host suite (WSL g++ 14.2, `tests/build_playlist_queue.sh`): PASS (`.tools/logs/playlist-queue.log`).
  - The existing M3U fixtures keep their counts: 25/25, 175/175, missing, ambiguous-first-copy, exact over tail, normalisation and UTF-8, bare-name uniqueness, overlong, truncation.
  - New assertions in the same suite: a list in `/music` resolves 25/25 exactly; a list build makes no library pass; an unchanged library reuses sources and keys with no pass; a partial catalogue pass at boot still makes the next build refuse.
  - Open-file high water is 4, the same as before the change; the test itself holds two files open to stand in for audio and the catalogue.
- `tests/build_catalog.cmd` (MSVC: catalogue, Classic UI Bluetooth and wired, and the other host suites): exit 0, no FAIL (`.tools/logs/playlist-host.log`).
- Firmware compile-only `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel power`: PASS for both images (`.tools/logs/power-check-playlist.log`).
  - Bluetooth: `Sketch uses 1602185 bytes`, `Global variables use 88400 bytes`.
  - Wired: 1602329 / 88408.
  - That is 440 B of static RAM above 78a11e7, mostly the 384 B key buffer.

None of this is hardware evidence.

## Owner checks

1. **First boot after flashing:** expect one library pass (`playlist: … folders, … playlists (N in /music)`) and, if any folders are too deep, a `not indexed` line. If the deeper scan finds new tracks, the catalogue rebuilds once.
2. **Second boot:** expect `… unchanged since last boot`, with no "Preparing playlists" pause.
3. **Opening lists:** open 01, 02, 07 and 2010-12. Expect `found=` 20, 20, 30, 18 as before; `scan=` and `total=` well under 200 ms; and `stat: touch pollGapMax` staying near 100 ms. List 06 should now find its tracks.
4. **A list in /music:** copy a pack with an `.m3u8` beside `Tracks/`. It appears as a playlist, and the log shows `found=n missing=0`.
5. **Volume after Sleep:** connect the speaker, `pwr sleepnow`, then wake. Expect `bt: initial volume sync target=…%`. The slider and the speaker's buttons should both move the speaker's volume.
6. Check the `stat: tasks loop` stack margin after the boot pass (one more `scanDir` frame).
