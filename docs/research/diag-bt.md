# DIAG-BT / BTSTREAM-OBS

Base: 96e95b9 (v35); branch: cydpod/diag-bt. Logging only: registered the audio callback (enables the library cache), added accepted-peer epochs and diagnostic snapshots; no recovery, reconnect/pairing policy or data-path change. Diff review confirms onData/audio_pull are unchanged; no device operations or hardware claims.

## Serial formats
- `bt: evt ms=<millis> ev=<CONNECTION|AUDIO|MEDIA_ACK> conn=<state> av=<state> media=<state> audio=<SUSPEND|STARTED> epoch=<n>`: BtAppT, after library handling each connection/audio/media-ACK event; rejected connection events show retained state before rejection. Symbolic states; epoch increments on accepted CONNECTED transitions. Maximum 20 event-prefix lines per 1000 ms window, including summaries.
- `bt: evt dropped=<n>`: next BtAppT event/tick after a saturated window; counts omitted event snapshots.
- `bt: media cmd=<CHECK|START|SUSPEND> rc=-1`: inferred sends from the pinned library dispatch branches (connecting recovery/heartbeat, connected idle heartbeat, successful CHECK ACK, STOPPING retry) and its disconnect wrapper. No media-send hook exists: rc=-1 is unavailable, NOT a measured API failure. No library patch.
- `bt: media ack cmd=<n> status=<n> ms=<delay>`: every ACK on BtAppT; cmd/status are direct event fields. Delay is derived from the latest inferred send for that command; -1 means no matching timestamp. Cleared after ACK and link transitions; cannot disambiguate delayed ACKs or multiple same-command sends.
- `bt: play intent=<serial> link=<0|1> audio=<s> media=<s> playing=<0|1> route=BT pulls=<delta> frames=<delta>`: loop task, once on a changed explicit intent serial while BT-routed (includes pause/selection intents). Baseline seeded at begin and refreshed on every observed intent, including non-BT intents. Pull/frame deltas use existing counters since the previous observed intent; snapshots are observational, not atomic across tasks.

The audio callback records state only; its event line prints after library processing. Existing link-ended and saved-device paging lines are unchanged. No heap allocation, new task or hot-path logging/counters.

## Check
Final code eb79635: `powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`.
`PASS: compile-only checks completed; no device commands were run.` Both images and single-pool checks PASS; logs: `.tools/logs/diag-bt-check.log` and build logs.
Player static RAM: 95,176 -> 95,208 B (+32 B, under 100 B); wired final 95,216 B. No host test required (no pure header factored).
First compile failed: heartbeat macro private to library cpp; fixed with sourced constant 0xff00 and reran only the affected check. Original logs retained as diag-bt-first-*.log.
Baseline: retained int7 v35 build log copied to `.tools/logs/diag-bt-baseline.log`: `Global variables use 95176 bytes`.

Open: owner takeover/Play serial capture; actual media API return codes require a library hook/overlay in a separate authorized task.
