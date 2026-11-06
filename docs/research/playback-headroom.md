# CYDpod v36 playback headroom

The 2,048-frame compact build is the candidate default. The 4,096-frame build is a separate trial with an additional 8,192 bytes of ring allocation. Decode runs on core 1 at priority 5. Bluetooth media runs on core 0. The decoder task uses a 512-byte PCM batch for resampled audio. The 44.1 kHz path copies batches directly from the codec pool.

## Images

Use the four flash regions from one directory at a time. The existing four-region procedure in E:\flash\v36\FLASH.md preserves settings. Verify the CYD port by unplugging and reconnecting it before using the procedure. The images in E:\flash\v35 and E:\flash\v36 are the original references.

| Folder in the test archive | Purpose | Source |
|---|---|---|
| v36-light-control | Original v36 PCM path and compact decoder with detailed audio tracing disabled | 256fbbd |
| compact-2048 | PCM batching and optimized compact FLAC with detailed tracing | a532b5b |
| compact-4096 | Same candidate with the larger ring | a532b5b |
| legacy-2048 | Same PCM changes and 2,048-frame ring with the legacy FLAC decoder | a532b5b |

The boot line 'cmd: audio config' reports the ring size, FLAC decoder, and diagnostic mode. The light control retains missing frames, underruns, and ring low-water. Detailed pull timing, lock timing, and FLAC read/decode timing are suppressed. CPU and Bluetooth event logs remain available.

Build the three candidates with 'powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia', adding '-LargePcmRing' or '-FlacLegacy' for the corresponding row. '-LightAudioDiagnostics' is also available on the candidate source. The archive's control was built from the diagnostic-only commit so the PCM and FLAC changes are isolated from that comparison.

## Board listening pass

1. Play the same previously reliable AAC-LC M4A on original v35 and original v36 compact. Use the same card, receiver, output, volume, and start position. Capture 60 seconds after playback settles, with the screen untouched. If v36 regresses, repeat on the light control. Use a 44.1 kHz stereo WAV as the control track. The existing ff-16b-2c-44100hz.wav in E:\dev\CYDpod\testsamples is suitable.
2. Repeat on compact-2048, then compact-4096. Run M4A and the same supported FLAC with the screen untouched, then while continuously scrolling Songs. Run legacy-2048 with the same FLAC and conditions to isolate the decoder. The reference CAKE FLAC is in E:\dev\CYDpod\testsamples. If wired is available, repeat a failing Bluetooth case on wired with a fresh boot.
3. Capture at 115200 baud from before reset. Label each log with the image, track, receiver, card and route. Keep boot through 'heap: after init', the SD clock and audio configuration, and six complete 10-second groups including 'stat: cpu', 'stat: pull', 'stat: audio', 'stat: flac', 'stat: pcm', and 'stat: heap'. Record any audible break and its time. The original v35 comparison uses underrun deltas because it has no 'missingFrames' field. Treat the light control's absent detailed fields as unavailable.
4. Qualify a candidate only if M4A and FLAC have no audible breaks or new missing frames during steady playback, both idle and scrolling. Use the largest real library for the heap check. Record minimum free heap, largest free block, and stack high-water marks, and look for allocation failures or resets. Keep the larger ring opt-in until it demonstrates a benefit with adequate headroom. A full ring and flat missing-frame counts during audible breaks call for A2DP pull and transmission investigation. Use the original v36 legacy image as the interim fallback only if it passes the same listening gate.

## Verification

The existing audio diagnostics, codec pool, FLAC exactness, and owner sample smoke suites passed. The FLAC tests covered 71 exact files, 110 pool seeks, and sanitizer checks for malformed frames, truncation, and CRC errors. Both owner FLAC files decoded byte-for-byte against legacy. The compact Xtensa object has no reference to the software 64-bit division helper. The production playback harness also passed with the larger ring and light diagnostics. The three W328C Bluetooth candidates passed the compile and codec-pool checks. The v36 light control compiled with the locked partition table.

Median host microseconds per FLAC frame across three runs with the same reader and files. Host timing includes file reads and varies with load.

| Track | Original compact | Predictor step | Predictor and reader |
|---|---:|---:|---:|
| CAKE, Short Skirt / Long Jacket | 642 | 569 | 481 |
| Arctic Monkeys, Reckless Serenade | 660 | 592 | 495 |

Board playback, Bluetooth performance, and runtime heap and stack headroom are pending the listening pass. Logs are under .tools/logs with the headroom prefix. The diagnostic-only build log is codec-pool-v36-baseline-light-jc2432w328c.log.
