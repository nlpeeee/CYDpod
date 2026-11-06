# BTSTREAM-POLICY (base 96e95b9)

Header only; no firmware wiring, heap, reconnect or serial formats. Only explicit Play reclaims. Direct START avoids library CHECK_SRC_RDY issuing an untracked START.

| State/input | Transition/output |
|---|---|
| Any / SUSPEND | Record audio; no media reset/send |
| Terminal + fresh eligible Play | START -> Waiting |
| Waiting/Retry + Play | Consume/coalesce serial |
| Audio and media STARTED + Play | Done, no send |
| Waiting + ACK OK and audio STARTED | Done (either order) |
| Failed/busy ACK or send rejection | One retry next tick, then Failed |
| Waiting + deadline | Failed without retry |
| Pending + pause/track/route/pair/link drop | Cancelled |
| Wrong-epoch ACK | Ignore |

ACK_DEADLINE_MS=3000 also bounds audio completion; MAX_RETRIES=1. Deadlines handle wrap. Clock::nowMs()/tick share a clock. initialSerial seeds boot; rejected/coalesced intents are consumed. No connection API; HeldPeer is untouched.

## BTSTREAM-WIRE hooks

Own policy on BtAppT; queue loop observations independently of pairing generations.

- bt_begin (loop): seed audio_playIntentSerial(); register a new onAudio callback with set_on_audio_state_changed. Initial snapshot is not Play.
- bt_tick (loop): observe audio_playIntentSerial(), audio_isPlaying(), audio_trackSerial(), speaker_route(), pairingActive; post ordered snapshots through CydSource::post/dispatch. Recheck intent/route/peer before send.
- CydSource::command (BtAppT): fresh explicit Play -> onPlayIntent; pause -> onPause; changed track serial -> onNewTrack; route -> onRouteChange; pairing entry/exit -> onPairing(bool); tick(millis()). Track selection cancels rather than replaying its intent as Play.
- CydSource::bt_app_av_sm_hdlr (BtAppT): accepted connection events -> onLinkUp/Down(peerEpoch); audio events/new onAudio -> onAudioState; snapshot s_media_state after base processing -> onMediaState; media ACK -> onMediaAck(cmd,status,capturedEpoch). Do not call policy from app_a2d_callback (stack callback).
- Injected Sender::request(START,epoch,reconcile) (BtAppT): return acceptance; never callback inline. Set library media STARTING before sending esp_a2d_media_ctrl(START); reconcile=true for non-IDLE mirror, including stale STARTED/SUSPEND. Never set IDLE on suspend. Intercept recovery ACK/heartbeat handling so failure-to-IDLE cannot auto-reclaim after failure/cancellation; serialize library STARTs with policy. ACKs have no attempt token; do not stamp old queued events with a new peer epoch.

## Evidence

`sh tests/build_bt_stream_policy.sh`: 20 PASS / 0 FAIL, including `PASS: missing ACK deadline fails without retry`.
`powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia`: `PASS: compile-only checks completed; no device commands were run.`
RAM: BT 95,160 B; wired 95,168 B (`Global variables use`); before inferred identical (unwired header), not separately rebuilt. Log: .tools/logs/btstream-policy-check.log. Hardware/capture/wiring remain open.
