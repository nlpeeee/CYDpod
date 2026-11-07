# Bluetooth candidate A

Deliver one W328C candidate based on v0.5.0, upgrading ESP32-A2DP to 1.8.12 at 7da8b6ec508597dc8727756526e7e1beadce6825, then improving the connection menu. Use the existing ESP32 core, SBC callback, compact FLAC, 4096-frame ring and core assignments. Managed codec frameworks are disabled.

The menu reads one fixed snapshot and repaints changed row bands at most five times per second. Identical results are ignored. RSSI requires a 3 dB change and one second between paints. A settled menu and a dark display perform no further paints.

Device actions are bound to the address captured on touch-down. Saved devices offer Connect or Disconnect plus confirmed Forget. An accepted Disconnect pauses playback, retains the bond, cancels pending connection/resume work and holds incoming and outgoing automatic reconnection for this session. Explicit Connect or Pair re-arms the session. A restart follows the saved startup policy. Queue rejection, radio failure and timeout must stay visible and permit retry.

Bluetooth control ticks run only while a session has pending transition or timeout work. Explicit Pair New retains the existing 60-second discovery bound. Normal Pause retains the existing media behaviour.

Run the existing pairing, reconnect, media-policy and Classic UI suites and the W328C compile check with -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel bt-a. Package four-region firmware, manifests and checksums under releases/candidates/bt-a. The owner performs the JBL and Xiaomi board comparison, captures heap/stack and missing-frame counters, then repeats overnight shuffle before promotion.
