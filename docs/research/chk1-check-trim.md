# CHK-1 check trim (2026-11-06)

Stock builds require `-IncludeStock`; W328C diagnostics require `-IncludeDiagnostics`.
W328C keeps normal/wired players and v34/v33 exports/manifests.
`-ListJobs` lists jobs. Timings cover staging/validation, host suites, WSL start/path,
and media check; tests unchanged.
Latest table: `.tools/logs/check-timings-<board>.txt`.

Commands: `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`, then the same plus `-IncludeDiagnostics`.
Both exit 0: `PASS: compile-only checks completed; no device commands were run.`
After: 2 images; before-equivalent with diagnostics: 5 images.

Fast path:
```
stage | seconds
dependency/inventory | 1.74
job selection/probe staging | 0.02
compile cydpod-player | 147.65
compile cydpod-player-wired | 136.44
results export | 0.00
total | 285.85
```
With diagnostics:
```
stage | seconds
dependency/inventory | 1.05
job selection/probe staging | 0.01
compile cydpod-player | 35.23
compile cydpod-player-wired | 29.05
compile i2s-tone | 32.71
compile i2s-tone-slot32 | 31.38
compile i2s-diag | 32.03
results export | 0.00
total | 161.46
```
Diagnostic work avoided: 96.12 s. Raw five-minus-two total is -124.39 s:
the second run reused warmed build caches, so totals do not establish a speedup.
Slowest three measured stages: normal 147.65 s, wired 136.44 s, warm normal 35.23 s.

Stock audit: results JSON iterates jobs; codec-pool checks use each
maintained player ELF; manifests/image export use each job's own output directory;
host scripts/media_check build their own binaries and use no stock image/results.
No stock prerequisite found. Probe selection/SkipProbe and W328C filtering unchanged.
Default-board `-ListJobs -SkipProbe -SkipHostMedia` exit 0: cydpod-player,
cydpod-uploader; plus `-IncludeStock` exit 0: player, uploader, cydpod-player,
cydpod-uploader (2 vs 4). Stock compile skipped per coordinator.
Host suites/device operations not run. Logs: `.tools/logs/chk1-*.log`.
