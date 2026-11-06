# PL-1 moved playlists

Resolution: existing resolved `/music` path first; otherwise longest matching
normalised path suffix, bounded to six components. Two or more components are
required when the entry has directories. Bare filenames (including drive-only
paths) match only when unique. Equal best tails select the first library track
and count ambiguous. Exact candidates override earlier weaker matches.

`playlist_lastMatchStats()` returns the last M3U build/preview counts: `entries`,
`exact`, `byTail`, `byName`, `ambiguous`, `missing`, `overlong`, `truncated`.
Found is exact + byTail + byName. Missing includes skipped overlong entries and
entries beyond min(library size, MAX_M3U=1000), preserving the old queue limit.
Ambiguous bare names remain missing. Blank/comment/URL lines are excluded.
Serial opening summary: `cmd: playlist <name> n=<entries> found=<k>
missing=<m> ambiguous=<a>`. CRLF/BOM are accepted; lines are drained with a fixed
259-byte buffer, with at most 255 entry bytes accepted.

RAM: one uint32 key per admitted entry, at most 4000 bytes, same bound as before.
The sorted key packs a 22-bit suffix CRC and a 10-bit original position; binary
search of trailing one/two-component hashes gates candidates, then full path and
up to six suffix hashes verify them.
No per-track storage or additional library scan. Eight-byte per-entry match state
uses the staged queue file (temporary SD file for the legacy RAM preview API).
Only the playlist input handle is added to normal queue building, preserving the
four-file mount limit alongside the catalogue and playing audio. Playlist order
and repeated entries are retained. The test embeds the real 25-entry playlist and
175-entry crate paths; no owner-folder dependency remains at runtime.

Checks (WSL Debian, no device operations):
- `sh tests/build_playlist_queue.sh`: exit 0.
  `PASS M3U real 25/25 and 175/175; exact/tail/name/order/ambiguity/missing/CRLF/BOM/UTF-8/overlong/truncation; one scan; key allocation <=4000 bytes`
  Existing queue/preview checks at N=1000/2000/10000 also PASS; four-file high water.
- `powershell -NoProfile -File tools/check.ps1 -SkipProbe -SkipHostMedia`: exit 0.
  `PASS: compile-only checks completed; no device commands were run.`
- Same with `-Board jc2432w328c`: exit 0.
  `PASS: compile-only checks completed; no device commands were run.`
Logs: `.tools/logs/pl1-host.log`, `pl1-default.log`, `pl1-jc2432w328c.log`.

Follow-ups: row/title found counts need a retained per-source UI descriptor;
this change exposes counts and serial diagnostics only. Importing playlists next
to tracks and exporting favourites remain separate tasks. Hardware playback and
AirPods/stereo acceptance were not exercised.
