# Now Playing right-edge erasure

Firmware fix: `60807f97ea8c135b718807859da2157cde8c1be0`, on `cydpod/bt-a`.

Shuffle and Repeat changes call `invalidate()` to discard the browse-list cache. That function also calls `paintThumb()`, which cleared a 30-pixel scrollbar strip down the right side even on the Player page. The strip overlaps most of the Repeat button. The transport renderer caches button states, so an unchanged Repeat button stayed erased after Shuffle changed. Other content in the strip could also disappear.

`paintThumb()` now returns on the Player page. List invalidation still discards cached rows, and list scrollbar behavior is unchanged. This uses no additional persistent RAM.

The new regression first failed against the previous renderer with `FAIL firmware UI: list cache invalidation must not erase the player right edge`. It checks framebuffer preservation through direct cache invalidation and Shuffle, and compares incremental Repeat painting with a full redraw. It covers four rotations and all repeat modes.

Final code checks:

- PASS player right edge: cache invalidation, Shuffle and Repeat preserve pixels in four rotations and all repeat modes. Passed in both Bluetooth and wired host configurations.
- PASS player redraw full Classic UI checks (Bluetooth and wired). Log `.tools/logs/player-redraw-host-final.log`.
- PASS: compile-only checks completed; no device commands were run. W328C compact FLAC with 4096-frame ring, both Bluetooth and wired. Log `.tools/logs/player-redraw-w328c-final.log`.

Bluetooth sketch: 1,571,037 bytes, static RAM 87,192 bytes. Wired sketch: 1,571,181 bytes, static RAM 87,200 bytes. Each image grew 24 bytes, with no static RAM change from the preceding candidate.

Updated package: `releases/candidates/bt-a-player`, with manifests, hashes, flash instructions, source revisions and check evidence. The earlier `bt-a` package remains available. On-board display confirmation and the Bluetooth candidate playback gate remain pending. After flashing, toggle Shuffle and Repeat repeatedly in portrait and landscape, then change tracks and navigate away and back. The right side and Repeat control should remain visible.
