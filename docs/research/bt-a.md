# Bluetooth candidate A

The dependency upgrade is committed separately as 81fa2bf. Candidate firmware is compiled from cc98f2bc05d1bb1e7f70110ddab141d4edee989a with ESP32-A2DP 1.8.12, compact FLAC, the 4096-frame ring and the internal ESP-IDF SBC callback.

- PASS pairing, reconnect policy, media policy and Classic UI checks, including the candidate menu regressions in both orientations. Log .tools/logs/bt-a-host.log.
- PASS final bounded-inquiry and pending-control tests. Log .tools/logs/bt-a-pairing-final.log.
- PASS final W328C Bluetooth and wired compile checks. Log .tools/logs/bt-a-w328c-final.log.
- Bluetooth static RAM is 87,192 bytes and wired is 87,200 bytes. Both are 504 bytes above the matching 0.5 builds. The new UI has a compile-time 1 KiB persistent-storage budget. Board heap and stack results remain pending.
- Portrait and landscape host screenshots were inspected for the menu and device action view. A saved connected device presents separate Disconnect and Forget controls.

On 7 November 2026 the owner reported clean playback on a Xiaomi Sound Pocket while the JBL Flip 3 exhibited breakup and paused noise. Speaker-side volume changes failed to update the display with both receivers. This is tracked in [issue 1](https://github.com/nlpeeee/CYDpod/issues/1).

The local candidate is packaged under releases/candidates/bt-a. The owner must compare both speakers, verify retained pairing through Disconnect/Connect, capture steady playback missing-frame counters and runtime heap/stack, and repeat overnight shuffle before promotion.
