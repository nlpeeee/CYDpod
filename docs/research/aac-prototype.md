# AAC minimal prototype: implementation and evidence

Date: 2026-10-23. Scope: C2/C4 **software probe**, not completion of the AirPods or wired hardware gates. No board was flashed, serial port opened, user media decoded, or hardware playback measured in this task.

## Implemented contract

`firmware/cydpod_probe/cydpod_probe.ino` targets the original Bluetooth Classic ESP32 with PSRAM disabled. SD uses VSPI SCLK18/MISO19/MOSI23/CS5 at 10 MHz. Display, touch and Wi-Fi are absent. One producer task owns one SD file and one Helix decoder; an 8,192-byte SPSC PCM ring feeds either the ESP32-A2DP source byte callback or a separate I2S consumer task. A2DP returns full zero-padded requests, reads only the ring and applies gain: it never waits on SD, decoding or a mutex. The I2S writer has a 20 ms driver timeout. Control uses a four-entry FreeRTOS queue; serial lines are limited to 127 bytes, oversized lines are discarded entirely, and queue overflow rejects a command with a metric.

The active I2S channel uses the existing IDF standard driver, Philips stereo, 16-bit, 44,100 Hz, on **provisional GPIO22 BCLK / GPIO27 WS / GPIO17 DOUT**. GPIO17 loading/accessibility and the actual CYD revision still need identification. `line` and `hp` select independent digital levels on the same external I2S pins; they do not control an analog jack selector or configure an amplifier. A verified stereo line DAC and headphone amplifier are still required.

Output changes cancel the current producer generation, mute, quiesce both ring endpoints, clear the ring, free the decoder and stop the previous source/I2S channel before starting the new one. The last track restarts from byte zero; position retention/seek/resume are not implemented. `end(false)` retains the Bluetooth controller allocation so it can be started again; it deinitializes the A2DP source. Discovery is cancelled first; upstream shutdown still depends on Bluetooth events and must be timed on hardware.

Boot, output initialization, and every Bluetooth connection-state event force mute. Each output has a saved digital level, clamped to **10% on boot**, with headphone default 5%; explicit session commands permit 0–25%. Unmute ramps digitally, while mute drops the target immediately. There is no analog mute or calibrated line reference, and already queued I2S DMA samples can still emerge after software mute. These bounds do not establish safe acoustic loudness, pop suppression, load suitability or electrical performance.

## Supported media

| Input | Current probe behavior |
| --- | --- |
| AAC-LC ADTS `.aac`, 44,100 Hz, two channels | Implemented; host synthetic 128/192/256 kbit/s fixtures verified |
| ADTS frame larger than 2,048 bytes, multiple raw blocks, other advertised profile/rate/channel config | Rejected before decode |
| Decoded output other than LC / 44,100 Hz core and output / stereo / 16 bit / 1,024 samples per channel | Rejected |
| Truncated or malformed ADTS | Stops track with an error; no arbitrary resynchronization |
| `.m4a` / MP4, including faststart | **Unsupported and explicitly rejected**; generated variants are negative tests |
| ADIF, ID3-prefixed AAC, DRM, HE-AAC/SBR, other codecs | Unsupported; no support inference from file suffix |

ADTS headers identify the advertised core profile, not every possible implicit extension. SBR is compiled out; this is an LC-only contract, not a complete detector for maliciously mislabeled HE-AAC. Decoder CRC verification and exhaustive hostile-input fuzzing are not claimed. SD's zero-byte read at a frame boundary is treated as EOF, so a transport-level read fault can look like clean EOF; an exact filesystem error report remains a hardware integration improvement.

## Reproduction and runbook

The build worker owns `dependencies.lock.json`, setup/check scripts, local dependencies and the upstream import. Required pins are Arduino-ESP32 **3.3.10**, ESP32-A2DP **v1.8.11**, commit `b6da4744286ca15b6f1dee607c077a4747294dd4`, and arduino-libhelix **v0.9.4**, commit `6183305de92c6c8e445aa95116319b64963a6ad3`. The documented local Helix config patch gates its default SBR enable behind global `CYDPOD_AAC_LC_ONLY`; that flag must apply to both C and C++ library compilation. The sketch refuses a different target or missing flag. The raw C decoder API is used; the public wrapper include only enables Arduino library discovery.

```powershell
powershell -File tools/setup.ps1
powershell -File tools/check.ps1
python tools/media_check.py
# Optional: regenerate the original legal synthetic inputs, then verify:
python tools/media_check.py --generate
```

Setup and firmware check use workspace-local dependency/config/build directories. The media command installs nothing and discovers already installed Visual Studio x86 C++ tools via `vswhere`; this run used VS 18 BuildTools/MSVC 14.50.35717 and FFmpeg 8.1.1. x86 is intentional because the pinned decoder supplies MSVC x86 inline assembly. Missing tools fail explicitly. Host build/PCM outputs are in `tests/out/`; fixture hashes and results are in `fixtures/verification.json`. The fixture signal is original low-level stereo tones with separate impulses, dedicated to CC0; codec/library notices remain separate.

After a separate authorized flash and confirmed hardware setup, copy a fixture to `/stereo-192.aac` on a backed-up FAT32 SD card and open the confirmed board's serial port at 115200. Commands are newline-terminated and exact:

```text
pair Exact Bluetooth Device Name
play /stereo-192.aac
stats
volume 5
unmute
mute
output line
play /stereo-192.aac
unmute
output hp
volume 5
unmute
stop
```

Wait for `OUTPUT ready ... muted=1` and a connected metric before deliberately unmuting. Each `play`, `pair` or `output` request mutes; an output change restarts the selected track. Pairing matches a target name using the existing library; the library handles bonding and at most three reconnect attempts, while the probe cancels an unconnected output after 30 seconds. Name matching can be ambiguous; address-based selection, explicit bond-forget controls and verified boot reconnection are future work. A saved peer name is loaded at boot, but output/play starts only on a command.

`stats` and the five-second metric line expose current and minimum internal free heap, current largest internal block, sampled minimum largest block, producer/I2S/loop stack high-water marks (ESP-IDF bytes), ring occupancy, decoded frame count, underrun frames/calls, rejected queue commands, connection, mute and level. Largest-block sampling also runs after decoder allocation and every 16 frames. These samples do not capture every allocation transient. Underrun counts are cumulative and can include EOF/transition edges; judge sustained playback intervals from captured logs. Bluetooth library task stacks are not separately instrumented.

## Evidence obtained

`python tools/media_check.py` passes:

```text
PASS ring capacity/wrap/concurrency, ADTS rejection, gain, saved levels, command overflow
PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=20656 live=0
PASS Helix AAC-LC 44100 stereo frames=131 samples_per_channel=134144 peak_decoder_heap=20656 SNR=59.58dB peak_error=190
PASS Helix AAC-LC 44100 stereo frames=131 samples_per_channel=134144 peak_decoder_heap=20656 SNR=59.81dB peak_error=188
PASS Helix AAC-LC 44100 stereo frames=131 samples_per_channel=134144 peak_decoder_heap=20656 SNR=71.67dB peak_error=54
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
```

The three rows are 128/192/256 kbit/s targets. Independent channel projections exceed 35 dB separation; exact values and SHA256 inputs are in the JSON. The accepted comparison threshold is >55 dB overall SNR and <256/32768 peak full-scale error, allowing fixed-point/PNS differences around impulses rather than requiring bit identity. Sample counts match FFmpeg's ADTS reference decode, including AAC delay/padding; no gapless duration claim is made. Host decoder-only heap is **20,656 bytes**, excluding the 10,240 bytes of input/output buffers, 8,192-byte ring, tasks, filesystem, allocator overhead and Bluetooth. It is not an ESP32 peak-memory measurement.

The ring test transfers one million ordered stereo frames between independent host threads, including full/empty and wrap behavior. The actual firmware gain ramp is shared with host tests for signed full-scale samples, mute/reset and rising gain. Error tests cover wrong profile/rate/channel fields, truncated/corrupt input, both unsupported M4A layouts, allocation failures on each of the two LC decoder allocations and repeated cleanup. The checker requires all ten named nonempty fixtures and has automated empty/partial/zero-byte inventory tests. It does not model Bluetooth timing, SD electrical errors or hardware output switching.

Firmware compile evidence: `tools/check.ps1` compiles the probe with global `-DCYDPOD_AAC_LC_ONLY` to 1,189,936 bytes (60% of the 1,966,080-byte slot) with 61,224 bytes of static RAM, SHA256 `35ec4065ba349757d39d9eee7f662249976ba281a9dc5e1765153f8ac7f233e4`. A rerun on 2026-10-23 reproduced the same digest. Compile success says nothing about runtime heap, timing or Bluetooth on the target.

## Exact M4A follow-up

Use the **existing** AudioTools `M4AAudioDemuxer` at commit **`e42af0513a2273730431e8c45ac17a5fd9f38487`** (resolved with read-only `git ls-remote`, then the exact commit's sources were retrieved). No MP4 parser is to be reimplemented. The existing callback supplies AAC as ADTS and can feed the same Helix contract; `setReference` must precede `setCallback`. Its existing parser requires moov before mdat. Sources: [demux callback](https://github.com/pschatzmann/arduino-audio-tools/blob/e42af0513a2273730431e8c45ac17a5fd9f38487/src/AudioTools/AudioCodecs/M4AAudioDemuxer.h), [common reader and allocations](https://github.com/pschatzmann/arduino-audio-tools/blob/e42af0513a2273730431e8c45ac17a5fd9f38487/src/AudioTools/AudioCodecs/M4ACommonDemuxer.h), [MP4 parser](https://github.com/pschatzmann/arduino-audio-tools/blob/e42af0513a2273730431e8c45ac17a5fd9f38487/src/AudioTools/AudioCodecs/MP4Parser.h).

This is a specific memory/correctness integration task, beyond adding an include: `onStsd` resizes from the declared box size, `onStsz` resizes from sample count and buffers chunks, and sample extraction resizes for frame size and an ADTS copy. Its default 16-bit sample-size table alone costs about **303 KiB (310 kB) for one hour at 44.1 kHz / 1,024 samples**, before all other state. The per-function static cache in `currentSampleSize` also deserves repeated-track validation because it outlives a reader reset. A fixed-size substitute buffer must fail cleanly when the existing reader asks it to resize; its current assertions cannot constitute recoverable error handling.

Follow-up ownership: build worker imports this exact commit and notices/config; audio owner adapts the existing reader with explicit error propagation and caps **before** each allocation (suggested first probe: 4 KiB metadata box, 2 KiB AAC frame including ADTS, 2,048 samples/table entries), uses a bounded/file-backed existing `BaseBuffer` for longer tables, validates object type/rate/channels before any PCM, and routes callbacks solely on the producer task. Reject oversized metadata, unsupported tracks/codecs, moov-last and bad tables cleanly until an existing seekable reader path is integrated. Do not silently rewrite or remux original user files.

Acceptance: demux both synthetic variants on host with declared supported/rejected outcomes; faststart AAC PCM must meet the same sample/channel/error thresholds, repeated mixed tracks must not retain table state, malformed/huge counts must stop within measured memory caps, and target compile plus largest-block/min-heap capture must pass. Add a real seekable existing reader for moov-last if user media requires it; disabling the existing moov-order error does not implement moov-last support. No `.m4a` playback is supported by this deliverable.

## Hardware proof still required

All C2/C4 hardware gates remain open: identify the actual original ESP32 CYD/flash/PSRAM/SD wiring; verify accessible and unloaded I2S 22/27/17; measure format/clocks and L/R with the real DAC; confirm its supply/straps, line load and headphone amplifier. With dummy loads first, measure DC, noise, gain, clipping, software/analog mute latency and switching pops before low-volume listening.

Run the actual user's original AAC containers without conversion on both wired modes and actual AirPods Pro 2. Record one-hour output/container runs, at least 100 track transitions and 20 output switches, zero steady-state underruns, stack margin, minimum heap/largest block and restart/reconnect behavior; test pairing, case close/open, range loss, competing phone and repeated reboots. The probe's restart-on-switch behavior needs explicit acceptance or replacement with a reused seek/resume path. Source shutdown/restart and Bluetooth memory retention need measurement. Compile success and synthetic host decode do not establish AirPods compatibility, real-time throughput, no-PSRAM heap fit or wired safety.
