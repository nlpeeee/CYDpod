# F2b/F2c host-stage correction

Branch `cydpod/host-green`; base cd8e3b6 (v32).
F2b code 74b1ce3, evidence 55296a5; F2c code e42d6b3.
No firmware/golden changes, device operations or push.

## Verdicts
- F2b classic_ui and wired: **stale test**. Pixel drag subtracts 7 px slop;
  100 px travel gives offset 93. Selection now taps Song 02's visible centre
  and asserts offset/top. Verified by the F2b run; not repeated separately.
- F2b catalog CT-08: **stale test**. CY-116A2 uses a 256-ID stack window,
  not N-ID malloc. Low-free-heap/fragmented-heap success and I/O-failure queue
  preservation passed in F2b and the F2c full-host run.
- F2c last preview assertion: **stale test**, no firmware fix required.
  `classic_screen.cpp` buildPreview delegates to the bounded playlist_preview;
  there is no free-heap gate. The fixed 528 B ESP32 cursor serves 256-ID windows.
  SD staging may fail during rebuild; a later queueAt refill can return -1,
  which SourceSongs latches as the existing disabled `Track unavailable` row
  until reload. Bounded File/path allocations still exist; this is not a claim
  that all allocation succeeds under real heap exhaustion (see cy-116a2.md).
- Replaced the heap-refusal expectation with low-reported-heap Back success,
  visible Song 04 and exact saved pixel offset, queue IDs/current track/call
  preservation and playback. Added zero SD write budget to assert the actual
  reload error, unchanged live queue, retry, restored scroll and playback.
  **PASS in both UI variants**:
  `PASS CY-43: output failure preserves route; preview release, low-heap Back rebuild, SD failure/recovery, scroll and queue preservation`
- Icon generation/verify: **PASS**; upstream preview assertion no longer blocks it.
  `CY90_PIXELS_OK: 1240320 independently expected production pixels`;
  native/x4 PNG CRCs and exact replications verified.

## Checks (once per code tip)
F2c: `powershell -NoProfile -File tools/check.ps1 -SkipProbe`, **exit 1**
on e42d6b3. Log `.tools/logs/f2c-full-host.log`.
`Host checks failed: catalogue/navigation. See output above.`
All four default firmware jobs compiled; `PASS single pool` (40,960 B).

| Suite | F2c result |
|---|---|
| pairing_session | PASS |
| speaker_lifecycle | PASS |
| catalog | PASS |
| touch_gesture | PASS |
| scroll_core | PASS (818 golden rows) |
| cst820_report | PASS |
| classic_ui | FAIL: a true vertical drag scrolls the list instead of activating a row |
| classic_ui wired | FAIL: same |
| icon verify | PASS (CY90_PIXELS_OK) |
| build_codec_pool.sh | PASS host stage |
| build_i2s_tone.sh | PASS host stage |
| build_wired_fade.sh | PASS host stage |
| build_i2s_diag.sh | PASS host stage |
| build_bt_policy.sh | PASS host stage |
| build_catalog_sort.sh | PASS host stage |
| build_playlist_queue.sh | PASS host stage (N=10,000) |
| build_audio_order.sh | PASS host stage |
| build_catalog_validation.sh | PASS host stage |
| media_check.py | PASS host stage |

F2b history: full-host exit 1, `.tools/logs/f2b-full-host.log`, blocked at
preview heap assertion; icon verify cascaded. W328C compile-only exit 0,
`.tools/logs/f2b-w328c-compile.log`:
`PASS: compile-only checks completed; no device commands were run.`
No second-board rerun requested in F2c.

## Remaining blocker
Both UI variants now reach a later assertion:
`FAIL firmware UI: a true vertical drag scrolls the list instead of activating a row`.
Stopped corrections as instructed; did not investigate or chain fixes.
Full host green is **not achieved**. Optional real samples SKIP (absent).
Orca checkpoint update: `selector_not_found` for this Git-created worktree.
