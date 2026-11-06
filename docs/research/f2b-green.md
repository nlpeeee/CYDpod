# F2b host-stage correction

Base cd8e3b6 (v32); code commit 74b1ce3; branch cydpod/host-green.
No firmware/golden changes, device operations or push.

## Four requested verdicts
- classic_ui: **stale test**. Pixel drag subtracts 7 px slop: 100 px travel gives offset 93, not a row boundary (`classic_screen.cpp:948`). Corrected the tap to Song 02's visible centre and asserted offset/top (`tests/classic_ui_test.cpp:1400-1404`). The full run passed selection and reached line 1476.
- classic_ui wired: **stale test**, same shared test and evidence as above; it also reached line 1476.
- icon verify: **stale test (upstream cascade)**. `tests/classic_ui_test.cpp:1519` calls `iconq::run()`; `tests/quiet_outline_test.h:93` writes catalogue PNGs. Blocked by the newly exposed line 1476.
- catalog CT-08: **stale test**. CY-116A2 replaced N-ID malloc with a 256-ID stack window (`playlist.cpp:472-479`); `tests/catalog_test.cpp:229-237` now requires success at 10,240 B free and with a 4,000 B largest block, retains the heap-allocation guard boundary checks, and checks I/O failure preserves playback. PASS, including N=10,000 preview/playView coverage.
Firmware references above are under firmware/cydpod_player/app/.

## Checks (once each)
`powershell -NoProfile -File tools/check.ps1 -SkipProbe`: exit 1.
Log `.tools/logs/f2b-full-host.log`; `Host checks failed: catalogue/navigation.`
All four default firmware jobs compiled; `PASS single pool` (40,960 B).
Default compile included above; no repeat.

| Suite | Result |
|---|---|
| pairing_session | PASS |
| speaker_lifecycle | PASS |
| catalog | PASS |
| touch_gesture | PASS |
| scroll_core | PASS (818 golden rows) |
| cst820_report | PASS |
| classic_ui | FAIL: Back rebuild is gated after preview release |
| classic_ui wired | FAIL: same |
| icon verify | FAIL: missing catalogue-12.png |
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

`powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`: exit 0. Log `.tools/logs/f2b-w328c-compile.log`: `PASS: compile-only checks completed; no device commands were run.`

## Remaining blocker
`tests/classic_ui_test.cpp:1475-1478` still expects low simulated heap to refuse the now-bounded preview rebuild. Stopped under CHUNKING's outside-change failure rule; follow up with SD write fault injection, retaining queue/scroll assertions. Full host green is **not achieved**. Optional real samples SKIP (absent); Orca checkpoint metadata: selector_not_found for the Git-created worktree.
