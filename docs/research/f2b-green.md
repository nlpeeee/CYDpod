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

## F2d: enumeration and one-pass fix
Non-aborting mode: `CYDPOD_UI_CONTINUE=1` makes `check` print `FAIL classic_ui_test.cpp:<line>: <msg>`
and continue; exit stays 1 (`FAIL firmware UI: N assertion(s) failed`). Default remains fail-fast.
Enumeration (normal and wired, identical): **5 failing assertions**, not a long tail.

| Assertion (line before fix) | Class | Evidence | Fix |
|---|---|---|---|
| 266 true vertical drag scrolls the list instead of activating a row | stale test | Old: `top==2`. Now pixel drag (CY-98b, 7 px slop) 113 px, then CY-99 release coast: offset 160 = 4 rows of RowH 40, depth 2, no row call | expect `classic_offset()==160`, `top==4`, `calls==songCalls` |
| 461 swipe, 90 px travel, 5 px jitter (x4, one per rotation) | stale test | List claims at |dy|>7 slop and |dy|>=|dx| (`classic_screen.cpp` PRESS_MOVE). At 10 ms polling the 10 px p-p jitter reaches dy=10 with dx=4.5: list owns the press, depth 2, no play, offset 0. At 80 ms dx=36: Back | 10 ms: expect list ownership (no Back, no play, offset 0); 80 ms: Back as before; added 2 px jitter case (below slop) expecting Back at both periods |

Line 461 fired four times (one per rotation, 10 ms period only). No firmware bug found; no
firmware or golden change. Real-hardware CST820 jitter is still unmeasured (cy97-tracker.md).

## Final host check (F2d tip)
`powershell -NoProfile -File tools/check.ps1 -SkipProbe`, **exit 0**, log `.tools/logs/f2d-full-host.log`.
`PASS: compile-only checks completed; no device commands were run.`
Suites: pairing_session, speaker_lifecycle, catalog, touch_gesture, scroll_core (818 rows),
cst820_report, classic_ui, classic_ui wired, icon verify (CY90_PIXELS_OK), build_codec_pool,
i2s_tone, wired_fade, i2s_diag, bt_policy, catalog_sort, playlist_queue (N=10,000), audio_order,
catalog_validation, media_check: **all PASS**. Optional real samples SKIP (absent).
