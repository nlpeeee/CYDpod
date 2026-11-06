# CYDpod v36 playback headroom

On the tested JC2432W328C, with a 1,477-track SD library and a JBL Flip 3, compact-4096 gave clean steady Bluetooth playback for M4A and FLAC. The firmware source is a532b5b, based on the original INT-8 v36 at e6d7458. The 4,096-frame ring is a build-time trial. The regular build selects 2,048 frames.

## What changed

| Commit | Change |
|---|---|
| 256fbbd | Added the light audio diagnostics option and a boot line that identifies the ring, FLAC decoder and diagnostic mode. |
| 5d13475 | Batched PCM transfers, moved gain arithmetic outside the ring lock, added decoder wakeups and exposed the larger-ring build option. |
| f7a7a11 | Prepared compact FLAC prediction once per subframe and replaced signed power-of-two division with exact shift-based scaling. |
| a532b5b | Added table-driven FLAC CRC and cached Rice unary reads. |
| f768106 | Recorded the candidate builds and initial board procedure. |

### Shared PCM path

The ring and task changes are in `firmware/cydpod_player/app/audio.cpp`, principally `audio_pull`, `pushFrames` and `decodeTask`.

- The producer previously entered the ring lock once per stereo frame and slept in four-tick intervals when the ring was full. It now copies up to 128 stereo frames per batch, splitting the copy at ring wrap. At 44.1 kHz it copies directly from the codec pool. Resampled audio uses a 512-byte batch on the existing decoder task stack and flushes the final partial batch.
- The consumer copies PCM and advances the ring under the lock. It scales the copied samples outside the lock and pads short pulls with silence. When a pull frees space, it wakes a producer that is waiting on a full ring. A queued track change or seek also wakes the producer. The producer checks the command mailbox under the same lock before publishing, and the decoder flushes the ring when handling the new command.
- MiniAAC still decodes M4A. The decoder task runs on core 1 at priority 5. The board logs identify the Bluetooth PCM consumer as BTC_TASK on core 0 at priority 19.
- The ring grows from 8,192 to 16,384 bytes with -LargePcmRing. At 44.1 kHz that increases nominal PCM capacity from about 46 to 93 ms. The allocation happens at boot. The larger ring absorbs bursts when the decoder can maintain the average sample rate.

### Compact FLAC

The predictor and reader changes are in `firmware/cydpod_player/app/flac_compact.c`.

- Predictor coefficients and sample width determine the safe accumulator size once per subframe. Validated samples populate the history. Prediction uses a 32-bit accumulator where the bound permits it and a 64-bit accumulator otherwise. Scaling preserves round-toward-negative-infinity behavior for negative values. The built Xtensa compact FLAC object has no __divdi3 reference.
- CRC-8 and CRC-16 now use byte lookup tables. Rice unary decoding consumes runs of zero bits directly from the cached byte. The decoder still checks the frame CRC before publishing PCM and retains its bounded resync, seek and cancellation paths.
- The linked codec pool is 30,824 bytes for compact and 40,960 bytes for legacy. Compact accepts FLAC blocks up to 4,096 samples in production. The ring and the FLAC block limit are separate settings.

## Built images

| Folder in the test archive | Configuration | Source |
|---|---|---|
| v36-light-control | Original v36 PCM path and compact decoder with light audio diagnostics | 256fbbd |
| compact-2048 | Batched PCM and optimized compact FLAC with the 2,048-frame ring | a532b5b |
| compact-4096 | Same compact candidate with the 4,096-frame ring | a532b5b |
| legacy-2048 | Batched PCM with legacy FLAC and the 2,048-frame ring | a532b5b |

The light build retains missing-frame counts, underruns, ring low-water, CPU stats and Bluetooth events. It omits detailed pull timing, lock timing and FLAC read/decode timing. The archived control was compiled from the diagnostics-only commit so it can isolate that change from the PCM and decoder changes.

Use the following command for compact-2048.

~~~powershell
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia
~~~

Add -LargePcmRing for compact-4096 or -FlacLegacy for legacy-2048. The current source also accepts -LightAudioDiagnostics. The pinned toolchain builds both Bluetooth and forced-wired variants. The archive contains the four flash regions and verified hashes for each Bluetooth image. Flash the four files from a single directory using the settings-preserving procedure in E:\flash\v36\FLASH.md.

## Verification before flashing

- PASS existing audio diagnostics and production playback checks, including seek, track changes, pause/resume and resampling. The production playback harness also passed with the larger ring and light diagnostics enabled.
- PASS FLAC pool and exactness suites. These covered 71 exact files across the compact profiles, 110 pool seeks and ASan/UBSan checks for malformed, truncated and bad-CRC input. Both owner FLAC tracks decoded byte-for-byte against legacy.
- PASS W328C compile and codec-pool checks for the compact, larger-ring and legacy builds in Bluetooth and forced-wired configurations. The v36 light control compiled and its partition table matched the lock.

Median host microseconds per FLAC frame across three runs on the same files and harness. Timings include file reads and vary with load.

| Track | Original compact | Predictor step | Predictor and reader |
|---|---:|---:|---:|
| CAKE, Short Skirt / Long Jacket | 642 | 569 | 481 |
| Arctic Monkeys, Reckless Serenade | 660 | 592 | 495 |

That is about a 25% drop in median host time on both tracks. The board results below measure playback behavior directly.

## What the board showed

The owner tested the candidates over Bluetooth on a JC2432W328C with a JBL Flip 3 and a 1,477-track library. The compact-4096 capture reports a 20 MHz SD clock.

| Observation | compact-2048 | compact-4096 |
|---|---|---|
| M4A playback | Owner reported near-perfect playback. 68 of 74 pre-FLAC reporting windows showed zero missing frames, including windows around user actions. | Owner reported clean playback in the longer mixed-format run. |
| The National, 90-Mile Water Wall | Whole-block time peaked at about 49 ms. The ring repeatedly hit zero, with roughly 9,000–10,000 missing frames per steady 10-second window. Cumulative underruns rose from 24 to 362. | Whole-block time still peaked at about 49 ms. The ring retained at least 1,408 frames during steady playback of this track and reported zero missing frames in those windows. |
| Heap low-water and lowest sampled largest free block | 18,228 bytes and 15,860 bytes across the 2,048-frame capture. | 9,312 bytes and 6,132 bytes across the longer stress capture. |

Both images recorded Bluetooth pulls in bursts of up to 1,408 frames. A nearly full 2,048-frame ring can lose roughly two-thirds of its contents in one burst, and one 49 ms decode can span the next pull. The 4,096-frame build kept enough PCM available through the same decode time. This comparison isolates the ring size on the tested setup. The individual PCM changes were tested together on the board.

The longer compact-4096 capture included FLAC and M4A track changes, four uncached playlist previews, scrolling and a Bluetooth disconnect/reconnect. The JBL resumed playback after the owner pressed Play. Six of 47 pull windows reported missing frames around transitions. The cumulative underrun count reached 11 early and stayed there through the remaining playback and reconnect. There were no allocation errors or unexpected resets in the capture. Minimum reported free stack was 1,432 bytes for decode and 1,400 bytes for the loop.

The older 10 KiB heap target came from AAC qualification and allowed for transient Bluetooth and SD allocations. The owner run dipped below that target while continuing to play. The compact-4096 result supports using it on this setup. A wider default still needs runtime heap and playback evidence on the larger libraries and other output routes it would cover. The captures compare the two compact candidates. Matched board comparisons against v35, v36-light and legacy remain open.

## Reproducing the board comparison

1. Use the same SD card, receiver, volume and starting point. Test the previously reliable M4A and the same supported FLAC for 60 seconds each after playback settles, first untouched and then while scrolling Songs. Compare compact-2048 and compact-4096. Use legacy-2048 if a decoder comparison is needed. Use the original v35 and v36 images for the historical M4A comparison. Compare original v36 with v36-light-control to isolate diagnostics.
2. Capture serial at 115200 baud from boot. Keep the image configuration, SD clock, track changes, Bluetooth events and complete groups of stat: cpu, stat: pull, stat: audio, stat: flac, stat: pcm and stat: heap. Separate track changes, pause and reconnect from steady playback when comparing counters.
3. Keep an ear out for breakups and note the action and time. For the larger ring, include imported playlist previews and a receiver reconnect with the largest real library available. Check missing-frame and underrun deltas, heap low-water, largest free block, stack headroom and any allocation errors.

Build and host logs are under .tools/logs with the headroom prefix. The diagnostic-only build log is codec-pool-v36-baseline-light-jc2432w328c.log. The board observations come from the owner's serial captures of compact-2048 and compact-4096.
