# CY-115: 10,000-track library, part 1 (sort core)

Branch `cydpod/big-library` from v1 a0806cc. Host-tested only; nothing run on a device.

## Design
`catalog_sort.h` (pure C++): external merge sort. Entry = id + 36 B key = 40 B. Runs of N entries (N = arena/40, 921 at 36 KB) are heap-sorted in one arena and spilled to `/.sys/sort-a.tmp`; then a 16-way merge (binary heap of cursors, 8-entry buffer per run) streams the final 2 B ids straight into the catalogue; more than 16 runs use extra passes ping-ponging to `sort-b.tmp`. A library that fits one run uses no temp file. The comparator is the existing `keyCompare` then `compare`, ties by id, so the order is unique and equals the old full sort. `appendOrder`/`buildKeys`/`ids[]` in `storage.cpp` are replaced by one path (`SdSortHost`); keys are built per run (n record reads per order).
- Key widened from 4 to 8 prefix bytes (title/artist/album, 24 -> 36 B). Same ordering; unsettled comparisons (2 record reads each) drop 2-6x. With 4-byte prefixes the model gave 10,000 tracks: title 615 s, artist 276 s, album 404 s (3.5 ms reads).
- Removed phase "Preparing to sort" (keys are built inside each sort); `catalog_test.cpp` expectation updated.
- Heap short: arena = maxAlloc - 24 KB (needs >= 8 KB, else the build fails cleanly and the old catalogue is kept; before, it fell back to slow record compares). Catalogue format and magic unchanged; a 2,000-track catalogue stays valid, and libraries that were capped re-scan and rebuild (signature changes).

## Bounds
- Memory: one `malloc` of at most 36,840 B (test: identical for 2,000 to 65,535 tracks, freed on every exit). No per-track RAM remains in the build (the 2 B/track `ids` array and the 24 B/track key table are gone).
- Open files during a sort: catalogue + 1 temp (normal) or + 2 temp (multi-pass) = 2 or 3 of 4. Temp files are removed before and after (RAII).

## Estimated build time (model, per order; random 712 B read 3.5 ms, range 2-5; run-file I/O 1 ms per transfer)
| tracks | order | old, no keys | new (range) | ties |
|---|---|---|---|---|
| 2,000 | title / artist / album | 264 s each | 38 / 8 / 28 s | 12 / 0.5 / 8% |
| 5,000 | title / artist / album | 754 s each | 132 / 23 / 90 s | 16 / 0.7 / 10% |
| 10,000 | title / artist / album | 1,647 s each | 350 / 59 / 234 s | 21 / 1.5 / 13% |
Totals at 10,000: about 11 min (range 6-16) versus about 82 min. Old with a key table at 2,000: 77/31/48 s (4-byte keys). Tie rates depend on the synthetic fixture (34-word vocabulary, 20% repeated titles); real libraries may differ.

## `MaxTracks` / per-track structures (now 10,000, `library_core.h:28`)
`storage.cpp:68` (validate), `:104` (scan cap, counts `skipped`). Checked and fine at 10,000: `Header`/`Group` uint16 fields, `h.artists<=h.tracks` checks, file offsets (7.1 MB catalogue, uint32), search index (uint16 id, 640 KB). `catalog_test` now builds 10,002 files through the real `storage.cpp` under the 4-file shim: 10,000 kept, 2 skipped.
**Heap risk, needs a decision:** 2 B/track arrays grow by 16 KB each: `playlist.cpp:160` `queue`, `audio.cpp:404` `order`, plus a transient `classic_screen.cpp:491` preview copy (all not edited here). About +32 KB resident, +16 KB transient, against ~45 KB free after Bluetooth connects. Lowering `MaxTracks` is a one-line revert.

## Not measured / next chunk
Not measured: real SD speed (the 3.5 ms and 1 ms are assumptions), scan time with tag reads at 10,000 files (likely longer than the sort), peak heap on device.
Next: on-device scan and sort time; a progress/time-estimate screen; a warning screen when the cap is still exceeded (`skipped`); a 2 B/track memory plan for `queue`/`order`/preview; boot cost at 10,000 (`validate` reads 7.1 MB twice per catalogue, both slots, every boot; `storage_findTrack` is a linear scan of record heads; search index build reads every record once).
