# PL-BACK

Base 96e95b9 (v35); branch cydpod/pl-back; implementation d755cd4 + 93bd767.

## Change
Retain the existing 256-ID preview cursor through pushed Player and mini-player return. Key: source ID/kind + catalogue CRC/count; successful UI favourite changes discard it (no revision accessor). Source/page changes, Home, read failures and failed staging also discard it. Only the ID window and keyed row labels refill on return; selection still copies into playlist.cpp's separate queue cursor/files. No scroll, thumb or viewport-cache changes. Loading... precedes a required re-entry build.

Serial, once per Back to SourceSongs:
`cmd: back list rebuild ms=<elapsed> rebuilt=<0|1> source=<id>`

Playlist path/stat metadata is not exposed by the playlist API. PC playlist edits require Home and re-entry; no favourites revision API exists and the only production toggle caller is Classic UI.

## Resources
PreviewKey is 12 B, replacing one dirty flag; no heap buffer/task added. Cursor holds zero File handles. During playback: audio 1 + catalogue 1 + retained preview 0 + persistent queue 0 = 2/4 slots; transient ID refill = 3/4, selection's queue output plus preview refill = 4/4, unchanged. Host high-water 3/4 (audio is stubbed).

## Checks
Final implementation 93bd767:
- WSL `sh .tools/pl-back-ui.sh` (focused `CYDPOD_PL_BACK_ONLY=1`): exit 0, log `.tools/logs/pl-back-ui.log`.
- PASS PL-BACK: unchanged playlist Back reuses preview; repeated select keeps queue independent; no retained file slot
- PASS PL-BACK: changed source and catalogue CRC rebuild
- PASS PL-BACK: Favourites toggle rebuilds with updated IDs
- PASS PL-BACK: SD failure discards preview, rebuild failure/recovery preserves live queue; Home refreshes PC edits
- Host test also checks mini-player return and live callback context/IDs after preview replacement/free.
- `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`: exit 0; `PASS: compile-only checks completed; no device commands were run.` Log `.tools/logs/pl-back-check-w328c.log`. Both single-pool checks PASS.
- Existing INT-7 baseline logs: player `Global variables use 95176 bytes`, wired `Global variables use 95184 bytes` (copied to `.tools/logs/pl-back-before-w328c*.log`). Final: player 95168 B, wired 95176 B; each -8 B, under the 64 B growth limit. Images 1582597 / 1582733 B.

Setup: reused existing generated metadata-real.m4a (WSL has no ffmpeg); normalized ignored runner LF. Stopped initial compile when copied Arduino YAML retained main-checkout paths, then isolated paths before final compile. Orca checkpoint update returned selector_not_found for this mandated Git-created worktree.

No hardware/audio performance claim; device acceptance remains owner work.
