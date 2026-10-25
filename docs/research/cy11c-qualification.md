# CY-11c: hardware qualification of M4A playback (BLOCKED at pairing; no playback)

**Outcome: failed.** The Xiaomi Sound Pocket did not pair in the two permitted attempts, so no track was played and the heap floor is **not measured**. Board ended on the committed image, muted, idle. Logs (not in Git): `.tools/logs/cy11c-*.txt`.

## Build identity
- `tools/check.ps1` exit 0; `build-results.json` aac-probe 1,224,944 B, sha256 `b25eec1e...e91121` = CY-11b `3a3ec0b`.
- Scratch qualification image (`kPlaybackMarginBytes` 10240 -> 2048, patch in `cy11c-qualification.patch`, built via a scratch copy of check.ps1's staging/compile path, `.tools/cy11c-scratch-build.ps1`): 1,224,944 B, sha256 `b6dc643fe0cf383f55f3da13b3ce6ed9ddfa9889ff9011266e0ae88ba57373a4`, static RAM 61,552.

## Flash and smoke (COM10 only, no erase)
- Committed image flashed (4 regions at 0x1000/0x8000/0xe000/0x10000), verify-flash matched. Smoke 20 s (`cy11c-smoke.txt`): six `/Music` names, `LS done count=6 truncated=0`, no resets; idle heap 144,788, largest 110,580.
- Scratch image flashed and verified, booted clean (same idle heap).
- Per coordinator trim, the committed-image `play` (model rejection) was skipped; CY-12a's 45,488/45,044 stands.
- Final: committed image re-flashed from `.tools/images/cy11c-committed` (copy of check.ps1 output), verified, muted, idle (`cy11c-final-idle.txt`, no reset).

## Pairing (scratch image)
Both attempts: `mute`, `output bt`, `volume 3`, USER ACTION cue sent to coordinator, 20 s wait, `pair Xiaomi Sound Pocket`, 60 s window.
- Attempt 1 (14:20): `ERR Bluetooth timeout; pair again` at 14:21:16, `connected=0` throughout.
- Attempt 2 (14:52, new cue): `ERR Bluetooth timeout; pair again` at 14:53:47, `connected=0` throughout.
- During pairing heap 56-65 K, largest 51,188-55,284, min_heap 55,512; no panic or reset. Limit of two attempts reached, so I stopped.
- Cannot tell from logs whether the speaker was actually in pairing mode, already connected to another source, or if cues reached the user in time (attempt 1 cue to pair gap ~21 s, attempt 2 ~21 s). CY-12a paired first time with the same procedure on the earlier image.

## Pass rules
All unevaluated: connected throughout, resets, underruns, playback heap floor, stacks, EOF, R4 `stop` with pending `ls`. Model-versus-measured margin and a `kPlaybackMarginBytes` recommendation are **not available**; per-track table empty. User audio verdict not gathered.

## Next
Re-run the matrix once the speaker is confirmed discoverable (check it is not connected to a phone, fresh pairing mode immediately before `pair`, possibly a longer pair window). Scripts ready: `tools/cy11c_session.ps1` (HOST cue to coordinator, emergency mute file `.tools/cy11c-emergency-mute`), `tools/cy11c_flash.ps1`, `tools/cy11c_pair*.txt`; the scratch image is rebuildable with `.tools/cy11c-scratch-build.ps1` (not committed) from the patch.
