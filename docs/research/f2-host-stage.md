# F2 host stage (v1 a0806cc)

## Changes

Restored scroll include/source/executable/data separators in `build_catalog.cmd`; CRLF and no stray control bytes verified (hex: `.tools/logs/f2-catalog-bytes.log`). Added default `cydpod-uploader` with `-DCYD_UPLOADER` and exclusive uploader/player image markers; it compiled to 1,149,033 bytes and passed the marker check. Compiler failures name the executable and exit non-zero; runtime failures accumulate so independent suites continue and check.ps1 throws a final failure summary.

REV-7 #7: vendor enumeration includes hidden files; nm version comes from the dependency lock; WSL uses `wslpath` and `--cd`; optional real samples use local `testsamples` or `CYDPOD_TESTSAMPLES` (WSL path), with explicit SKIP messages. Copied dependencies/tools and rewrote the ignored CLI config/cache paths to this worktree. No product code or device operations.

## Checks

Full run ONCE: `powershell -NoProfile -File tools/check.ps1 -SkipProbe`, exit **1**. Log: `.tools/logs/f2-full-host.log`; final message: `Host checks failed: catalogue/navigation. See output above.` All four firmware jobs compiled before the host stage. **10 newly reachable entrypoints attempted; 14 total** (rows below; nested checks grouped under their script).

| Suite | Result | First failure / evidence |
|---|---|---|
| pairing_session | PASS | PASS live pairing: 20 cycles |
| speaker_lifecycle | PASS | PASS speaker lifecycle |
| catalog | PASS | PASS catalogue: real/synthetic M4A tags |
| touch_gesture | PASS | PASS touch gesture classifier |
| scroll_core | PASS | PASS goldens (818 rows); PASS all |
| cst820_report | PASS | PASS CST820 real decode -> shared classifier |
| classic_ui | FAIL | FAIL firmware UI: select track (log:450) |
| classic_ui wired | FAIL | FAIL firmware UI: select track (log:600) |
| icon verify | FAIL | FileNotFoundError: [Errno 2] No such file or directory: 'icon-preview\\catalogue-12.png' (log:622) |
| build_codec_pool.sh | PASS | MiniAAC seek, codec/MP3/FLAC pool, FIR/resampler, production audio/seek; PASS host stage |
| build_i2s_tone.sh | PASS | PASS host stage (normal + sanitizer) |
| build_wired_fade.sh | PASS | fade/pump, route, settings, boot plan, output switch variants; PASS host stage |
| build_i2s_diag.sh | PASS | PASS host stage (normal + sanitizer) |
| media_check.py | PASS | PASS synthetic fixture matrix: 3 positive, 7 negative |

Final sequential compile-only commands (each run once, both exit **0**):
- `powershell -NoProfile -File tools/check.ps1 -SkipProbe -SkipHostMedia` -> `.tools/logs/f2-default-compile.log`: PASS.
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia` -> `.tools/logs/f2-jc2432w328c-compile.log`: PASS.

Both logs end `PASS: compile-only checks completed; no device commands were run.`

## Follow-up

UI owner: investigate both `select track` assertions (expected Song 02/playing after drag); likely stale gesture expectation versus current selection behavior, not a build/path failure. Icon verification depends on `iconq::run()` near the end of classic_ui_test, which the earlier UI assertion prevents from generating PNGs; fix that upstream failure or isolate icon generation in a follow-up chunk. No rebaseline performed: known WSL `Confirmation dialog geometry differs` since 8924c3b remains recorded; this Windows UI run passed geometry and failed later. Optional real samples were explicitly skipped (absent in this worktree). Orca checkpoint metadata was unavailable (`selector_not_found` for the mandated Git-created worktree).
