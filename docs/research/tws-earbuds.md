# True-wireless earbuds on CYDpod (AirPods Pro 3, Huawei FreeBuds Pro)

Owner board runs on 8 November 2026 used the diagnostic build (`docs/research/airpods-diag.md`). The research below was gathered the same day. This note led to `docs/specs/bt-rate-and-earbuds.md`.

## What the logs show

| | JBL / Xiaomi | AirPods Pro 3 at 328 kbps | AirPods at 229 kbps | FreeBuds Pro |
|---|---|---|---|---|
| Peer bitpool range | (not logged) | 2..53 | 2..53 | **2..35** |
| Bitpool in use | 53 | 53 | 35 | 35 |
| Packets per 10 s (`writes`) | — | irregular | ~333 (MTU 1008) | ~445 (MTU 883) |
| Queue | clear | full: heap 60 → 19 KB, pull gaps 0.5–8.7 s | usually clear; one session had 21–132 L2CAP stalls/10 s and heap dips to 22 KB | clear |
| `rssiDelta` | — | — | −18…0 at +9 dBm | −12…0 |

- **Throughput.** The FreeBuds cap the bitpool at 35, so they never see 328 kbps. The AirPods accept 53 but can't carry it.
- **Display load.** In the AirPods session with stalls, the stalls were present while the panel was awake (marquee, list scrolling) and gone once `panel=asleep`. This matches the Fonix One report, which found Apple-earbud dropouts only under display load. It is not proven: `btd ui quiet` is there for an A/B test.
- **Stream lifecycle.** Both earbuds end the link with reason "normal" 3–15 s after CYDpod's 10 s idle SUSPEND. The FreeBuds also suspended a stream themselves 3 s after START and then left. CYDpod then held (`HeldPeer`) and Play did nothing.
- **Inbound connections.** After the FreeBuds left, the AirPods opened the link themselves and became the saved speaker. The owner wants to keep that.
- **Pairing messages.** `bt: paired with` printed on every authentication, including with a stored key, so the logs could not show whether the FreeBuds re-paired.
- **MP3 seeks.** Seeks into MP3s took 13–23 s. That is a separate bug: `codec_track.cpp` replays the file from frame 0.

## Packet counts

In the owner logs, the AirPods took about 668 packets per 10 s at 328 kbps and about 333 per 10 s at 229 kbps, both with MTU 1008. With frames of 119 B and 83 B, that comes to about 614 B and 857 B per packet. Bluedroid's 30 ms tick holds about 10.3 frames, but only 8 fit a 328 kbps packet, so every tick also sent a 2–3 frame leftover packet. Patch 0003 sends whole packets only (`docs/specs/bt-rate-and-earbuds.md`).

## Background

- **How TWS earbuds share one link.** True-wireless sets keep one ACL link to the source. The primary bud either relays audio to the secondary, or the secondary sniffs the primary's link and only the primary acknowledges. Roles can swap when one bud is in a worse position, in the case, or low on battery. That costs airtime on the one link and can suspend or drop the stream ([Qualcomm TWS whitepaper](https://assets.qualcomm.com/rs/385-TWS-803/images/whitepaper-the-evolution-of-true-wireless-technology-2020.pdf), [Google seamless role-switch patent](https://patents.google.com/patent/US12108493)).
- **Bitpool.** Most sinks cap the SBC bitpool at 53, and some lower. AirPods advertise higher, but sources usually stay at or below 53 ([Habr: SBC on headphones](https://habr.com/en/articles/456476)). Lowering the bitpool is the common cure for stutter on congested links ([A2DP streaming notes](https://applied-ee.github.io/embedded/docs/audio-projects/bluetooth-audio/a2dp-audio-streaming/)).
- **Other ESP32 projects.** Some report AirPods connecting only from pairing mode, while other brands reconnect fine. There is no confirmed fix ([esp32.com thread](https://esp32.com/viewtopic.php?p=141208)).
- **Android interop lists.** Android's stack keeps per-device workarounds, including one that disables AVDTP SUSPEND for some sinks ([AOSP Bluetooth change mirror](https://gitlab.e.foundation/e/os/android_packages_modules_bluetooth/-/commit/3d4db3aa836dd998094c9a5c7bc2aae1084ae7e3)).
- **Fonix One.** Their player saw dropouts with Apple earbuds only when the display was busy. Their fix detects an Apple sink and lightens display load ([Fonix One write-up](https://fonix.one/news/behind-the-scenes-airpods-fix)).
- **Bluedroid's source.** It encodes SBC towards a fixed 328 kbps target (`DEFAULT_SBC_BITRATE`), lowered only for non-EDR peers. It has no congestion feedback: a backed-up L2CAP channel just fills the 27-packet TX queue and drops.

## Plausible on-chip mechanism (unproven)

Core 1 does the display work: SPI pushes and flash-font reads. Bluedroid's BTC/BTU tasks run on core 0. The two share the flash cache and bus, so heavy UI work adds latency to the host tasks just when the earbud link has the least margin. A speaker with margin absorbs it, and earbuds near their limit do not.
