# INT-8 integration v36

`cydpod/integration-v6`, private `int8`; base TIP-A `96e95b9`. Checked firmware source `3a6eab6` (later commits are documentation only). No push, device or serial operations.

No-ff merge order (branch tips): diag-cpu `57c4649` -> flacx-pool `5b35dff` (includes diag-flac/FLACX host) -> diag-bt `f986a2b` -> pl-tel `5250727` -> pl-back `ec27a9a` -> btstream-policy `93c6847` (unwired). One merge commit each.
Conflict: only `tools/check.ps1` host-suite list; retained every diagnostic/FLAC suite plus BT stream policy. app.ino merged automatically. Export tags: `codec-pool-v36-jc2432w328c` and `codec-pool-v36-legacy-jc2432w328c`. Excluded core0-ui, F5, uploader export, F3-F23, BTSTREAM-WIRE, TRM and RAM-1.

## Checks
- Full default: `powershell -NoProfile -File tools/check.ps1 -SkipProbe`: exit 0; all 17 host stages PASS (catalogue/UI, 15 WSL scripts, media), 0 FAIL; new CPU/audio diagnostics, FLACX and BT stream-policy suites included.
- Initial full run failed on missing catalogue `audio_isPlaying()` stub; focused rerun exposed low simulated heap refusing the favourite invalidation before the SD-failure test. Added paused stub and restored simulated heap before mutation; no firmware change. Focused catalogue/UI then passed (54 PASS, 0 FAIL + CY90_PIXELS_OK); failed logs retained. Full retry verifies final corrected harness.
- W328C: `-Board jc2432w328c -SkipProbe -SkipHostMedia`, default and then `-FlacLegacy`: exit 0 each; `PASS: compile-only checks completed; no device commands were run.` Both normal/wired: `PASS single pool`, compact `00007868`, legacy `0000a000`; opposite decoder excluded by nm checks.

## W328C player bytes
| Reference / variant | Static RAM | Sketch flash |
|---|---:|---:|
| v35 task reference | 95176 | 1582501 |
| flacx-pool | 85328 | 1562049 |
| v36 compact | 85384 | 1564593 |
| v36 legacy | 95520 | 1588105 |
Compact vs v35: -9792 RAM / -17908 flash; vs flacx-pool: +56 / +2544. Compact saves 10136 RAM / 23512 flash vs legacy. Wired companions: compact 85392 / 1564737; legacy 95528 / 1588229.

## E:\flash\v36 delivery
Both 4194304 B; SHA256:
- `cydpod-v36-w328c.bin`: `3d5e580711973e11e451b56cddb6707411c75c7919f7d9cb67cc184fc752f4e2`
- `cydpod-v36-legacy-w328c.bin`: `ff553f22ae5d1bbd8471a313ce1343eec763539e9c2e3f559d163bb39484335a`
`PASS merged layout` each: regions/hash equality at 0x1000/0x8000/0xe000/0x10000, all other bytes FF. Root compact regions, legacy/ old-decoder regions; manifests and esptool copied. FLASH/CAPTURE/CHECKLIST match committed int8-* copies. Temp site: RELEASE_OK version=36; PASS site links, anchors and manifest (41 refs); no binary committed.

Logs: `.tools/logs/int8-*.log`. Owner A-H then legacy C/D/F captures remain; compact default is conditional on CAPTURE.md's timing/underrun decision. No hardware acceptance claim.
