# CY-11: heap budget with Bluetooth running

2026-10-25; analysis only, starting commit 740e5a7, branch cydpod/cy-10. No device access. Work in progress: measured facts and accounting are being appended before conclusions.

## 1. Measured facts and source boundaries

The baseline is post-setup, SD-mounted idle, not raw reset heap. CY-10d records 144,936 B free and largest 110,580 B before Bluetooth (`docs/research/cy10d-smoke-test.md`, 13:25:27 METRIC). CY-9b records Bluetooth discovery without connection at 57,860 B free, largest 53,236 B, lifetime minimum 56,984 B (`docs/research/cy9b-bluetooth-test.md` and raw `.tools/logs/cy9b-pair{1,2}.txt`). Combined A2DP/controller/Bluedroid startup and discovery cost is therefore 87,076 B free-heap reduction; largest-block reduction is 57,344 B, which is fragmentation information, not another allocation to add. No source separates that total into individual library costs. Connection/playback allocations remain unknown: every recorded sample has connected=0 and decoded=0.

Raw anchors: `cy9b-pair1.txt:1` is pre-Bluetooth 144,936/110,580; `cy9b-pair2.txt:8-11` is 57,860/53,236, minimum 56,984. The initial `output bt` actually failed because no peer was set; the subsequent `pair` started Bluetooth (`cy9b-bluetooth-test.md`, Commands). Thus these are after Bluetooth startup/discovery, not proof that the literal first `output bt` succeeded. Timeout cleanup leaves 70,884/57,332 (pair2 final stats), not a usable connected baseline.

Pinned library evidence under `.deps/sketchbook/libraries/ESP32-A2DP/src/`: `BluetoothA2DPSource.cpp:start` starts controller, initializes/enables Bluedroid, creates its application task and dispatches profile setup; `av_hdl_stack_evt` initializes A2DP/AVRCP. `BluetoothA2DPCommon.h:409-411` sets a 20-event queue, 3,072-byte event stack and Classic-only controller mode. `BluetoothA2DPSource.cpp:731-891` changes connected/media state and requests CHECK_SRC_RDY then START; event dispatch at line 248 copies event parameters with malloc. These create transient demands beyond a static disconnected snapshot. Internal SBC encoder/media buffers, ACL/L2CAP transport, authentication and AVRCP activity on actual connection/start have **unknown incremental peak and largest request** in this build; the supplied sources do not expose a complete allocation ledger. Do not assume that delta is zero or that stopping discovery necessarily pays for it.

Core configuration source throughout: `.deps/arduino/packages/esp32/tools/esp32-libs/3.3.10/sdkconfig` (original ESP32, not another chip). Lines 632/636 configure BTC/BTU stacks at 8,192 B each; lines 642-667 enable A2DP internal codec, AVRCP, SPP, HFP and BLE; lines 703-706 allow four host ACL links and dynamic BLE environment memory, with no SPIRAM-first allocation. Controller settings at 724-742 are BTDM-capable, maximum three BLE and two Classic ACL links; these are capacity settings, not measured live bytes. These stacks/controller state are already in the observed startup difference. Host/target allocator overhead and connected peak need target telemetry; no device operation was performed here.

## 2. Per-track commitments and fit

All values below are bytes. KiB means 1,024 B. Source paths are relative to this checkout. Distinguish resident allocations already paid before the METRIC from new track allocations: double-subtracting globals or tasks would incorrectly reject AAC.

| Component | Commitment | Increment after 57,860 baseline | Evidence |
|---|---:|---:|---|
| PCM ring | 8,192 payload + 8 indices | 0 | `firmware/cydpod_probe/cydpod_probe.ino:28`, `audio_core.h:PcmRing` global storage |
| Decoder compressed/output storage | 2,048 encoded + 8,192 PCM, plus info/handle | 0 | global `decoder`; `adts_decoder.h:55-57`; host object sizeof 10,288 |
| Producer stack | 8,192 + TCB | 0 | `cydpod_probe.ino:344`; allocated during setup |
| Sink stack | **3,072 + TCB** | 0 | `cydpod_probe.ino:345`; task brief's 2,048 is not current source |
| Loop stack | 8,192 + TCB | 0 | core sdkconfig:448; TCB/RTOS overhead size unknown, already paid |
| SD mount/card, command queue, preferences | resident; individual sizes not measured | 0 | probe setup before metrics; `sd_diskio.cpp:745` allocates card at mount |
| Helix internal decoder | host peak **20,680** | 20,680 for AAC or M4A | fresh WSL allocation test below; target-layout previous result 20,656 |
| Open-file buffering | 4,096 libc + 4,096 FatFs window | budget 8,192 for either format | core `libraries/FS/src/vfs_api.cpp:23,291`; sdkconfig:1809,1838; `include/fatfs/src/ff.h:225` |
| File metadata/allocator overhead | unknown, call it U | U for either format | `vfs_api.cpp:78,253-274`: shared VFSFileImpl, temporary path, strdup, fopen; FILE/FIL allocations and alignment not host-measured |
| Reader fixed live | 9,160 | M4A only | `docs/research/cy5-state-reset.md`, malloc-wrapped host measurement, includes 3,672 object and initial parser/extractor buffers |
| M4aFeed around reader | 4,376 host-layout increment | M4A only | sizeof measurement below: 4,096 queue + 256 input chunk + counters/alignment |
| Sample-size table | 2 x N | M4A only | `m4a_caps.h`, reader uint16 table; full per-file table below |
| Reader frame/temporary growth | planning allowance 4,096 | M4A only | `M4ACommonDemuxer.h:129,256,316`: initial 1,024 accumulation buffer already in fixed part; cap requires up to 2,041 accumulation and 2,048 ADTS temp, or +3,065 payload; allowance rounds up for growth |
| Connected/audio-start change | unknown, call it C | C for either format | section 1; not measured by disconnected logs |

The 8,192 file allowance uses the configured static 4,096-byte FatFs window (`ffconf.h`: FF_MAX_SS, FF_FS_TINY; sdkconfig:1848 disables dynamic buffers), regardless of actual sector size; target FILE/FIL overhead beyond that window is unknown. It is not a measured 8,192-byte allocation. libc buffer allocation may be lazy. The probe opens the file **before** its M4A guard (`ino:214-220`), so already charged bytes must be excluded from remaining demand there, while lazy buffering must still be reserved. Avoid a second file handle for a cached table: it can duplicate this expense. U also covers allocator rounding, host-to-ESP32 layout differences and temporary realloc overlap not bounded here; it is not assumed zero in acceptance.

Fresh host command (2026-10-25, exit 0; no installs): `wsl.exe -d Debian -- sh .tools/cy11-host.sh`. Script compiles each `.deps/sketchbook/libraries/arduino-libhelix/src/libhelix-aac/*.c` with `gcc -O2 -w -DUSE_DEFAULT_STDLIB -DCYDPOD_AAC_LC_ONLY`, then links `tests/helix_decode.cpp` and those objects with `g++ -O2 -std=c++17 -DCYDPOD_AAC_LC_ONLY` and the Helix include directory; binary is `.tools/cy11-host/helix_decode`. Output:

```text
PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=20680 live=0
```

This is requested payload, not target heap metadata. `tests/helix_decode.cpp:7-16` counts the two Helix requests; `libhelix-aac/buffers.c:102-107` allocates AACDecInfo and PSInfoBase. Earlier x86 Windows measurement is 20,656 (`aac-prototype.md:70`, reproduced by repository check); WSL 64-bit pointers change struct layout. Use the larger host value for planning, not a claim of an ESP32 measurement.

Small host sizing program `.tools/cy11-sizes.cpp` copies only the M4aFeed member declaration and includes the actual reader/decoder headers plus `Arduino.h`. Built with g++ C++17, LC-only, `-Itests/host_shim -Ivendor/arduino-audio-tools/src -I.deps/sketchbook/libraries/arduino-libhelix/src`; output:

```text
sizeof_reader=3672 sizeof_feed=8048 feed_extra=4376 sizeof_decoder=10288
```

Arithmetic against the disconnected baseline, **excluding U and C**:

- AAC new demand = 20,680 + 8,192 = **28,872**; free remainder **28,988**. It fits the known allocation budget, with **18,748** left beyond a proposed 10 KiB margin. Actual connected playback fits only if U+C <=18,748 and allocation fragmentation permits; no hardware fit is established.
- M4A new demand = 20,680 + 8,192 + 9,160 + 4,376 + 4,096 + 2N = **46,504 + 2N**. Free remainder = **11,356 - 2N**, before U+C. Even N=7,833 gives **-4,310**; cap N=16,384 gives **-21,412**; N=18,100 gives **-24,844**, and is rejected by the current cap anyway. Thus current M4A does not fit the conservative budget for any of the real files. Removing the file allowance altogether yields shortest-file remainder 3,882, still **6,358 below** the 10 KiB margin.
- Largest 53,236 exceeds every individual present track request (table <=32,768, feed object host 8,048, decoder aggregate <=20,680). That alone cannot make the total fit: the decoder/feed allocate before table parsing and split the block. The reported reader peak 44,312 at the cap (`cy3-caps.md`) is **not the entire playback budget**; it excludes Helix, feed and file state and comes from a cap-table case with no audio frames (`tests/m4a_caps_test.cpp:91`). Do not add its peak to the fixed/table/frame ledger a second time.

Per-file source: `tests/real_media_expected.tsv` gives counts for the 23 accepted files; its rejected rows have emitted count zero, **not zero table entries**. Counts 16,963 and 18,100 come from `cy3-caps.md` measurement. Last column applies the common conservative formula 11,356-2N, excluding U+C; all table costs are desired allocations even for rejected files.

| File | N entries | Table B | Free remainder B |
|---|---:|---:|---:|
| 07-01 - Deee-Lite - Groove Is in the Heart.m4a | 9985 | 19970 | -8614 |
| 07-02 - Moloko - Sing It Back.m4a | 11975 | 23950 | -12594 |
| 07-03 - Moloko - The Time Is Now.m4a | 9573 | 19146 | -7790 |
| 07-04 - Spiller - Groovejet (If This Ain't Love).m4a | 9805 | 19610 | -8254 |
| 07-05 - Madison Avenue - Don't Call Me Baby.m4a | 9828 | 19656 | -8300 |
| 07-06 - Room 5 - Make Luv.m4a | 9150 | 18300 | -6944 |
| 07-07 - Junior Senior - Move Your Feet.m4a | 7833 | 15666 | -4310 |
| 07-08 - Jamiroquai - Cosmic Girl.m4a | 10517 | 21034 | -9678 |
| 07-09 - Jamiroquai - Canned Heat.m4a | 8580 | 17160 | -5804 |
| 07-10 - Groove Armada - Superstylin'.m4a | 15525 | 31050 | -19694 |
| 07-11 - The Chemical Brothers - Hey Boy Hey Girl.m4a | 9146 | 18292 | -6936 |
| 07-12 - The Chemical Brothers - Galvanize.m4a | 16963 | 33926 | -22570 |
| 07-13 - The Prodigy - Out of Space.m4a | 13013 | 26026 | -14670 |
| 07-14 - The Prodigy - Everybody in the Place.m4a | 9174 | 18348 | -6992 |
| 07-15 - Primal Scream - Loaded.m4a | 18100 | 36200 | -24844 |
| 07-16 - Primal Scream - Movin' on Up.m4a | 9833 | 19666 | -8310 |
| 07-17 - Cornershop - Brimful of Asha.m4a | 10355 | 20710 | -9354 |
| 07-18 - Fatboy Slim - Weapon of Choice.m4a | 9493 | 18986 | -7630 |
| 07-19 - Daft Punk - Around the World.m4a | 10402 | 20804 | -9448 |
| 07-20 - Daft Punk - Get Lucky.m4a | 10667 | 21334 | -9978 |
| 07-21 - Cassius - Cassius 1999.m4a | 9552 | 19104 | -7748 |
| 07-22 - Basement Jaxx - Red Alert.m4a | 11088 | 22176 | -10820 |
| 07-23 - Basement Jaxx - Romeo.m4a | 9369 | 18738 | -7382 |
| 07-24 - The Supermen Lovers - Starlight.m4a | 10097 | 20194 | -8838 |
| 07-25 - Justice - D.A.N.C.E.m4a | 9127 | 18254 | -6898 |

## 3. Ranked options with numbers

1. **Replace the retained table with a 1,024-byte SD window (256 raw stsz entries).** This is a proposed bound, not a measurement. Savings are 2N-1,024: **14,642..35,176 B** across the real set, **31,744 B** at the current cap. Model demand becomes 47,528 and remainder **10,332-U-C**, independent of duration. Alone this has only **92 B** above 10 KiB before unknowns; combine with resident-buffer savings below.

   The vendored interface supports this: `M4ACommonDemuxer.h:setSampleSizesBuffer(BaseBuffer<uint16_t>&)` and `onStsz` call resize(N), write(each size), clear and later sequential read; `currentSampleSize()` caches repeats. An ordinary small SingleBuffer cannot work: the complete stsz is parsed before mdat, so a rolling FIFO loses entries before consumption. `M4AFileSampleSizeBuffer.h` already demonstrates a file-backed BaseBuffer with ignored writes, sequential reads and save/restore file position, using `M4AAudioFileDemuxer::beginSampleSizeAccess`. Its AudioPlayer/ContainerM4A integration is not the probe's M4aFeed, so it is a design/source to adapt, not a drop-in fix.

   Current CYDpod guard hard-wires `M4aCappedSizes`, reconstructs it on begin, reinstalls it, and rejects count >16,384. Calling public `reader().setSampleSizesBuffer` externally would bypass that ownership/reset design and leave its cap/overflow state inconsistent. A CYDpod-side change must explicitly own/inject a seekable BaseBuffer, retain the stsz offset/count after validating every original 32-bit entry, and remove only the RAM-table count limit. Use the same File on the producer: seek to refill <=1,024 B, decode big-endian entries, restore mdat position, and keep bounded count/index arithmetic. Preserve validation, moov-first restriction, unsupported fixed-size rejection, truncation behavior and CY-5 cache reset. No vendor edit or custom codec is required. File I/O stays outside Bluetooth callbacks; latency/underruns remain unknown until measured.

2. **Reclaim resident buffers conservatively, then measure active stacks.** Raw CY-9b METRIC minima (`pair1:11`, `pair2:8`): producer free **5,812/8,192**, sink **1,996/3,072**, loop **6,448/8,192**. Observed used values are therefore 2,380 / 1,076 / 1,744. These are idle/discovery high-water marks, not decode/I2S maxima. Proposed producer 6,144 saves **2,048** and leaves 3,764 beyond observed use; do not cut directly to 4,096 based on idle telemetry. Proposed sink 2,048 saves **1,024**, leaving only 972 beyond observed use (below the existing 1,024 floor), so defer that cut until active measurement; alternatively create the I2S sink only in wired mode, potentially saving **3,072 + unknown TCB** during Bluetooth. Loop 4,096 could save **4,096**, but serial/path/printf peak is unmeasured; defer.

   Ring 2,048->1,024 stereo frames saves **4,096** resident bytes; latency capacity falls from **46.44 to 23.22 ms** (N/44,100). Probe readiness already needs 1,024 frames, so a 512-frame ring would require readiness/backpressure changes. Helix LC-only `AdtsDecoder::pcm_[4096]` reserves defensive SBR capacity although validated LC emits 2,048 int16 samples: changing to 2,048 could save **4,096** resident bytes only after verifying pinned Helix LC writes/negative-path bounds. These three proposed savings (ring 4,096 + producer 2,048 + LC PCM 4,096) total **10,240**. Combined with option 1, projected playback free is **20,572-U-C**, allowing **10,332** for unknown costs while retaining 10 KiB. Global .bss savings increase usable heap on the next build/boot; they are not current track frees. Feed queue 4,096 could be bounded closer to the 2,048 ADTS cap, saving another **2,048** per M4A track, but multiple callback emission/queue-drain behavior must first be proved.

3. **Smaller RAM cap with explicit rejection is a compatibility fallback, not the solution.** With the current allowance, keeping 10 KiB requires 2N <=1,116, or **N<=558** before U+C: every one of the 25 files is rejected. A practical round cap 512 consumes 1,024 B and has the same 10,332-U-C remainder as option 1, but supports only ~11.89 seconds (512*1024/44,100). A seemingly generous cap **8,192** uses 16,384 B, leaves **-5,028-U-C**, and accepts only **07-07 Junior Senior**; it rejects **07-01 through 07-06 and 07-08 through 07-25** (all other 24 rows). Current cap 16,384 rejects only **07-12 Galvanize and 07-15 Loaded**; raising it to 18,432 (36,864 B) accepts all 25 but worsens the shortage. Counts and names are in section 2; no truncation is acceptable.

4. **Bluetooth runtime/config changes: no bankable extra bytes yet.** Classic-only is already the library default (`BluetoothA2DPCommon.h:411`), and Arduino 3.3.10 `cores/esp32/esp32-hal-bt.c:btStartMode` calls `btMemRelease(BT_MODE_BLE)` for Classic, which uses `esp_bt_mem_release` (host + controller). Therefore do not credit another BLE-release saving on top of the observed baseline. Runtime API exists without rebuilding the core, but savings from any unused/unreleased regions are **unknown**, potentially already exhausted. `BluetoothA2DPCommon::end(true)` releases Classic and prevents a subsequent restart: unusable while Bluetooth playback is required; probe uses end(false).

   Disabling compiled BLE, HFP, SPP, AVRCP cover-art, reducing BTC/BTU stacks or host ACL capacity via sdkconfig requires rebuilding the precompiled ESP32 core/IDF archives; sketch defines cannot change those libraries. No supported runtime switch here removes compiled host BLE support while keeping the running Classic source. A2DP exposes `set_event_stack_size`/`set_event_queue_size`; its current 3,072-byte event stack is already paid, and no Bluetooth event-task high-water measurement was supplied, so budget **zero proven saving**. Reuse/configure the existing stack before considering a different transport implementation.

5. **Avoid duplicate file buffers and unnecessary copying.** The Arduino File API offers setBufferSize (vfs_api.cpp:437); a smaller/explicitly unbuffered libc buffer might save up to **4,096 B**, at a cost of more SD reads, without eliminating FatFs's file window. Actual setvbuf semantics and latency require measurement, so no saving is included above. Preallocating/reusing reader/table/frame objects can reduce fragmentation and transition peaks; it does not eliminate their live byte cost. No PSRAM credit is available (`cy10d` boot psram=0).

## 4. Recommended admission thresholds and queued work

**Do not lower the present 96/64 KiB guard to admit current M4A at 57,860 B.** A guard cannot repair this budget. The current thresholds (`ino:51-52`) are 98,304 heap / 65,536 largest and therefore reject Bluetooth-state starts. A more explicit interim policy for the unchanged RAM-table implementation is **84 KiB heap (86,016), 74 KiB largest (75,776)** at the post-open guard, *only after file allocations are made eager/accounted*. Reasoning: remaining cap-track modeled demand excluding the already-paid 8,192 file allowance is 38,312+32,768=71,080; plus 10,240 playback margin and a proposed 4,096 uncertainty reserve =85,416, rounded to 86,016. Largest conservatively reserves the entire remaining allocation sequence +4,096 =75,176, rounded to 75,776, rather than assuming that a 32 KiB table fits after earlier allocations. If any file buffering is still lazy, add its outstanding bytes to both checks. Keep the existing guard until this accounting is implemented; neither threshold pair admits the current Bluetooth baseline. These are policy bounds, not a measured connected guarantee.

After a 1 KiB file-backed table and the resident savings in option 2 are implemented and measured, recommend **53 KiB heap (54,272), 44 KiB largest (45,056)** at an eager-file post-open guard. Remaining modeled new demand is 20,680+9,160+4,376+4,096+1,024=39,336; +10,240 margin +4,096 uncertainty reserve =53,672 rounded up. Largest uses 39,336+4,096=43,432 rounded up. Require connected state before admission, and separately reserve any measured future audio-start peak; if that exceeds the 4,096 allowance, raise thresholds by the excess. If File allocation is deferred, add the outstanding file allowance instead of silently assuming it paid. Apply a corresponding AAC guard of **35 KiB heap (35,840), 25 KiB largest (25,600)** after eager file allocation: 20,680+10,240+4,096=35,016 and 20,680+4,096=24,776 rounded up. No guard can promise 10 KiB throughout playback until C/U are measured and bounded; largest is an independent fragmentation gate, not a substitute for total free heap.

Queue these bounded chunks, each with its own acceptance evidence:

- **CY-11a: file-backed stsz buffer and host bounds.** Adapt existing BaseBuffer seam in `lib/cydpod_m4a`, using a <=1 KiB cache and one seekable file; retain all guard/state-reset protections and accept all 25 counts without retaining the table.
  Check actual emitted frames/PCM against fresh-reader references, mixed/aborted sequences, malformed entries, truncated windows and seek failures; measure peak/largest allocation with max frames and all real-file counts.
- **CY-11b: resident buffer recovery and probe admission.** Reduce ring to 1,024 frames and verify LC-only output bound before halving PCM storage; trial producer 6,144 only with stack instrumentation, targeting 10,240 resident bytes reclaimed.
  Integrate file-backed feed, eager/bounded file allocation and thresholds above, record target-layout sizes and compile-only checks; preserve mute/cancel/output behavior and keep stack cuts provisional pending active high-water evidence.
- **CY-11c: authorized connected memory qualification.** Separate device chunk after pairing is resolved: measure pre/post connection, source-ready, audio start, sustained AAC/M4A and stop/transition peaks, with actual AirPods and both heap/largest telemetry.
  Accept only if free internal heap stays >=10,240, active stacks >=1,024 and largest exceeds the next request +4,096; otherwise revise reserves/buffers before listening qualification, with at most two correction cycles.

The proposed plan predicts 20,572 B free before U+C, not a hardware pass. At least 10 KiB margin requires U+C<=10,332 after all proposed changes, or further measured savings such as avoiding the inactive I2S task/extra File buffering. Hardware approval and pairing repair are outside this analysis dispatch.

Validation (2026-10-25): `powershell -NoProfile -File tools/check.ps1` exited **0**; retained full output `.tools/logs/cy11-check.txt`. Tail evidence:

```text
PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=20656 live=0
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.
```

Pre-commit `git status --short` contains only `?? docs/research/cy11-heap-budget.md` and the pre-existing ` M docs/research/build-baseline.md`; the latter was not edited or staged. All host helper sources/binaries and check logs are ignored under `.tools/`. No firmware/library/vendor/tools source changed, no installs, serial, flash or SD access, and no push. This check and host allocation evidence do not establish successful hardware playback.
