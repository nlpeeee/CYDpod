# CY-98a renderer/cache seam

Pixel Page offset replaces row top; row-step input remains aligned. Seven absolute-row cache slots support clipped opaque partial rows and title-less placeholders; the existing one-row loader/audio gate remains. ASCII paint uses fixed text scratch without String construction; UTF-8 reuses a reserved String and the existing strip loader. Same-query Search reloads retain a pixel bookmark through temporary zero-hit counts. Paging strip/input stay for part 2; no Tracker, settings, scroll_core or codec changes.

Checks on final firmware/test source (12bc3c5), exit 0:
- `.tools/cy98-ui.sh` (standalone Classic host build, CYDPOD_VIEWPORT_ONLY=1): `PASS CY-98a viewport: offsets/clamp, partial-edge clipping, cache-only placeholders/recovery, mini return, invalidation, row steps; 4/6 rows`. Includes same-query Search return.
- `tools/check.ps1 -SkipProbe -SkipHostMedia`: `PASS: compile-only checks completed; no device commands were run.`
- `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`: same PASS line.
Logs: `.tools/logs/cy98-ui.log`, `cy98-default.log`, `cy98-jc2432w328c.log`.

Sketch flash / static RAM (bytes; delta vs 8924c3b, privately rebuilt with identical flags):
- Default: 1490457 (+1164) / 91392 (+312).
- JC2432W328C: 1531629 (+1076) / 92984 (+312).
Baseline logs: `.tools/logs/cy98-baseline-{default,jc2432w328c}.log`. Additional text storage: 128-character String reservation and 132-byte stack scratch. Both checks report `PASS single pool` with size `0000a000` (40960 B).

Unchanged full geometry gate initially reported `FAIL firmware UI: Confirmation dialog geometry differs` under WSL; not investigated or rebaselined. Focused renderer mode excludes that broader gate; no geometry golden changed. Part 2 input/thumb and hardware feel validation remain. No device operations or push.
