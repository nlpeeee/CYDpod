# CYDpod: delivery plan

Date: 2026-10-23. Status: research-backed plan; firmware and electrical feasibility have not yet been demonstrated. This document governs scope; research reports supply supporting evidence, not additional requirements.

## 1. Recommendation

Build around the owned **ESP32-2432S028** CYD, with an **AAC-first feasibility milestone** before committing to a full application. Evaluate [Garionhk/CYD-MP3-Player](https://github.com/Garionhk/CYD-MP3-Player) as the application shell. Retain its display/touch, pairing, storage and uploader work where compatible; replace/adapt the MP3-specific audio path using an existing AAC decoder and container reader. Do not write a new codec, Bluetooth stack, graphics library or filesystem.

The preferred experiment is LC-only Helix decoding plus ESP32-A2DP and a bounded PCM buffer. If the shell resists that integration, use AudioTools/Helix/A2DP composition with a small CYD interface. Espressif's decoder components are a second fallback. The decision is conditional on measured AAC, AirPods and wired-output results, not a claim that any existing firmware already satisfies everything.

Supporting research: [reuse options](../research/reuse-options.md), [hardware and wired audio](../research/cyd-hardware.md). Fonix is the [feature reference](https://fonix.one/features); no reusable Fonix source license or available public firmware download was established. Its advertised results are not our acceptance evidence.

## 2. Requirements and assumptions

| ID | Requirement | Completion evidence |
| --- | --- | --- |
| R1 | Use the user's CYD, marked `ESP-32-2432SO28`, provisionally ESP32-2432S028 | Exact module, flash, PSRAM, display and touch profile recorded; board bring-up passes |
| R2 | Play files with `codec_name=aac`, `profile=LC`, `sample_rate=44100`, `channels=2` | Actual original files play at correct speed with stereo preserved through each required output; no required transcoding to MP3 |
| R3 | Bluetooth playback to **AirPods Pro 2** | Actual AirPods discovery, pairing, playback, volume and reconnect tests pass |
| R4 | Provide external DAC connectivity for stereo line/headphone output | Verified I2S harness, stereo DAC, appropriate headphone amplification, electrical and listening evidence |
| R5 | Standalone SD library and touchscreen player resembling the useful Fonix experience | Browse, play/pause, previous/next, seek, volume, output selection and persistent settings work without a phone |
| R6 | Reuse suitable projects; thorough planning before implementation | Source comparison, dependencies, decision gates, chunk ownership and acceptance below |
| R7 | Practical library loading | SD card reader initially; separate Wi-Fi upload mode in first complete release |
| R8 | Portable enclosure and power | Bank-powered prototype, then measured battery/enclosure build; runtime target agreed after measurement |

R1-R4 are explicit user requirements. R5-R8 interpret the original Fonix-like-device request; their details are proposed scope. Assumptions pending answers: **select one audio output at a time**; AAC refers to files, with SBC acceptable as the Bluetooth transport; support both `.m4a` and ADTS `.aac` until the user's actual container is identified. No Apple-specific automatic switching, Siri, microphone, spatial audio or lossless Bluetooth requirement has been requested.

Input codec is distinct from container and output codec. Required path:

```text
SD .m4a or ADTS .aac -> existing demux/parser -> AAC-LC decoder
                                              |
                               44,100 Hz, stereo PCM
                                              |
                             bounded buffer + output selector
                              /                         \
              ESP32 A2DP source/SBC                  I2S transmitter
                       |                                  |
                AirPods Pro 2                       stereo external DAC
                                                        /       \
                                                   line out   headphone amp -> headphones
```

AAC source files are decoded locally for the shared wired path. Bluetooth AAC transmission is a separate optional investigation; it must not delay basic AirPods playback or be silently promised as passthrough. [ESP32-A2DP source documentation](https://github.com/pschatzmann/ESP32-A2DP/wiki/A2DP-Source) documents the normal PCM/SBC path. Newer custom-codec APIs have different framework requirements and do not prove this workload fits the CYD.

## 3. Scope by release

| Area | First usable prototype | First complete release | Later, gated additions |
| --- | --- | --- | --- |
| Audio | Required AAC-LC 44.1 kHz stereo; actual source files | Same, tested across agreed bitrates and containers; robust seek/resume | MP3/WAV, FLAC, Opus, other rates/profiles only after separate tests |
| Outputs | AirPods Pro 2 and external I2S wired prototype | Bluetooth / line / headphone selection with saved safe levels | Simultaneous outputs or AAC radio codec only if explicitly wanted |
| Touch UI | Folder browser, transport, volume, pairing, output picker | Now Playing, queue, shuffle/repeat, playlists, metadata, bounded artwork | Gestures, themes, visualizers, EQ, ReplayGain |
| Storage | Small backed-up FAT32 card, at most 32 GB | Paged library and index; declared tested library limit | exFAT/large SDXC and larger libraries, with actual card testing |
| Management | Card reader | Separate Wi-Fi upload/file/playlist mode | Live browser remote during Bluetooth playback, podcasts |
| Power | USB bench supply | USB power bank portable build | Integrated cell, managed charging and fitted enclosure |
| Updates | Serial installation/recovery | Reproducible versioned images and documented upgrades | OTA only with a proven partition/recovery design |

Fonix advertises AAC/M4A, Bluetooth, exFAT storage and long battery runtime on a no-PSRAM ESP32, with unusually specialised decoding. Those [specifications](https://fonix.one/specs) motivate research; **2 TB, 16+ hours, all codecs, concurrent Wi-Fi and elaborate visuals are not baseline promises**. A development release can be useful well before complete parity.

## 4. Reuse decisions

| Layer | First choice | Reuse boundary / fallback |
| --- | --- | --- |
| Application shell | Garionhk CYD-MP3-Player | Import a pinned snapshot after license/dependency audit. AAC filtering, duration, seeking and output routing need work; no blind decoder swap |
| AAC | Helix via a compatible pinned wrapper, LC-only build | Verify actual SBR configuration and total peak allocations. If unsuitable, evaluate Espressif esp_audio_codec before custom optimisation |
| Containers | Existing AudioTools M4A/ADTS readers or compatible file demuxer | Validate moov-at-end, seek tables and long files. A streaming fast-start parser alone cannot claim general M4A support |
| Bluetooth | ESP32-A2DP `BluetoothA2DPSource` | Preserve working shell version initially; native Espressif source is fallback. Never substitute an A2DP sink project |
| Wired | Existing I2S driver/AudioTools I2SStream | One active PCM sink; use the same framework/I2S API generation throughout |
| Graphics/touch | Keep shell TFT_eSPI and its bounded software-SPI touch approach | Board definitions from witnessmenow. Reconsider LovyanGFX or LVGL only for a demonstrated need; no initial UI framework migration |
| SD | Keep the shell's filesystem first | SdFat is an alternative if needed for the selected demuxer/filesystem; avoid two competing filesystem owners |
| Web | Existing uploader and core HTTP facilities | No new backend framework or app required; separate playback/upload images if the measured memory situation requires it |
| Power/mechanics | Existing documented modules and matching CYD case designs | Reuse a dimensionally verified case; adapt openings for DAC, jack and cell after measuring parts |

Shell documentation names Arduino-ESP32 3.3.10, ESP32-A2DP v1.8.11 and libhelix v0.9.4. These are candidate reproduction versions, not a proven AAC combination. Record exact upstream commit and dependency tags/SHAs in an implementation lock manifest, including TFT_eSPI and vendored code. Current codec-helix may differ from the older wrapper; do not mix current headers with old examples. AudioTools releases have API changes. Reproduce upstream before changing one dependency at a time.

The shell includes Apache license text, but copied examples, wrappers, codecs, fonts and other dependencies have their own terms. Preserve notices and inventory them per pinned artifact. Research found GPL and Helix-specific terms; do not label the eventual combined binary wholly Apache/MIT. Missing-license projects are references rather than automatic copy sources. No redistribution decision is needed to finish planning; a release needs an explicit inventory.

## 5. Hardware and connector proposal

Use the original ESP32 with Bluetooth Classic. An ESP32-S3/C3 replacement alone does not preserve the A2DP requirement. The reported model does not settle ILI9341 versus ST7789, resistive versus capacitive touch, USB wiring or regulator revision; identify those first.

Proposed resource ownership on the reference resistive board: TFT on HSPI, SD on VSPI, touch on bounded software SPI. Generic touch and SD examples can each claim VSPI on different pin groups, so combining them unchanged is unsafe. Preserve a single owner per peripheral and keep large SD artwork reads below audio priority.

**Provisional external DAC harness, not an instruction to wire an unidentified board:**

| CYD signal | Proposed function | Required check |
| --- | --- | --- |
| GPIO22 | I2S BCLK | Header identity and no competing owner |
| GPIO27 | I2S LRCLK/WS | Header identity and no variant backlight conflict |
| GPIO17 | I2S data out to DAC DIN | Shares reference blue LED; verify accessible pickup and load/isolation, potentially soldering |
| GND | DAC signal/power reference | Actual module ground and connector orientation |
| Verified supply rail | DAC/amp module power | Board-specific input voltage, regulation and available peak current |

GPIO35 is input-only and cannot replace an I2S output. GPIO17 is not assumed conveniently exposed on the expansion header. If that pickup is impractical, evaluate a verified LED GPIO/pad alternative with its loading and startup behaviour before publishing a final harness. The onboard mono amplifier is not the headphone path.

**Preferred wired architecture:** a documented PCM5102A breakout for stereo line output, plus an existing stereo headphone amplifier module/reference board such as the TPA6132A2 family. TI specifies the PCM5102A as line output down to a 1 kOhm load; ordinary low-impedance headphones need amplification. Its BCLK-derived PLL avoids a fourth MCLK wire. Verify module straps, regulators, I2S format, mute and supply independently of chip capability. [TI PCM5102A](https://www.ti.com/product/PCM5102A), [TI headphone amplifier](https://www.ti.com/tool/TPA6132A2EVM2).

A TPA6132A2 EVM is a documented bench option, not a cost/size-optimised purchase recommendation. Select the actual affordable module only after checking its schematic, stock, gain, output power at the intended headphone load and enclosure fit. An integrated TLV320DAC3100 breakout is an alternative, but needs extra I2C/reset/control allocation and verified clock setup; it is not a drop-in for the three-signal harness. See the detailed [hardware comparison](../research/cyd-hardware.md).

Provide clear line/headphone labels and separate outputs or a break-before-make selector. Line output uses a defined fixed reference level; headphone output has its own bounded volume. Start muted with a low headphone setting, measure DC/noise/clipping and use dummy loads before listening. Decide analog mute/enable control versus verified soft ramps once the board/module pins are known. Do not parallel two active analog outputs or use a speaker amplifier in place of a headphone amplifier.

Initial BOM: owned CYD, owned AirPods Pro 2, backed-up FAT32 microSD, USB data cable/supply, DAC breakout, stereo headphone amp, suitable jacks/selector and wiring. Borrow multimeter, USB meter and logic analyser/scope where possible. Add power bank first; integrated battery, charger, converter and enclosure come after audio proof. No shopping prices or battery runtime are verified in this plan.

## 6. Firmware architecture and resource gates

Keep architecture close to the chosen base. Logical boundaries can map onto its existing files rather than demand a rewrite:

- **Board profile:** pins, panel/touch configuration, brightness and optional DAC harness.
- **Media reader:** SD ownership, bounded reads, file/container detection and demux state.
- **Decoder:** one active AAC decoder, fixed output contract, clear unsupported-profile/error handling.
- **Audio engine:** producer task, bounded PCM ring, queue/seek/resume state and metrics.
- **Output driver:** A2DP or I2S, with explicit stop/start, volume and disconnect events.
- **UI:** bounded drawing and paged browsing; posts commands to the engine, never decodes in touch callbacks.
- **Library/persistence:** on-card index/playlists/art cache, small NVS settings and bonds, bounded writes.
- **Uploader image:** HTTP file management while playback is stopped; shares declared SD/schema/settings contracts.

For 44.1 kHz stereo 16-bit PCM, throughput is **176,400 bytes/s**. One AAC-LC frame of 1,024 samples per channel produces 4,096 bytes, about 23.2 ms. An 8 KiB ring is about 46.4 ms; 16 KiB about 92.9 ms. These are arithmetic capacities, not tested safety margins. Measure SD, decode and scheduler stalls before choosing the ring. Count total bytes from source expressions, not potentially stale comments.

A full 240 x 320 RGB565 framebuffer costs 153,600 bytes. Start with dirty rectangles/short strips and bounded art decoding, one open track, small index pages and no large animated assets. Precompute thumbnails/indexes before playback or on a host when useful; do not require re-encoding the user's AAC music. Mandatory 44.1 kHz input avoids a resampler in the initial path.

Measure memory after boot, SD, decoder, Bluetooth startup/pairing/streaming, wired startup, track changes, seeking and output switches: current and minimum internal heap, largest block, DMA heap, task stack high-water marks, allocation failures and audio underruns. Candidate conservative gate: minimum free internal heap at least 20 KiB; largest block exceeds the largest remaining runtime allocation plus 4 KiB; each task retains at least 1 KiB stack or a documented measured alternative. If thresholds cannot be met, revise scope/buffers using traces and rerun; no passing by suppressing telemetry.

Audio callbacks consume prepared PCM and provide defined silence on underrun; no SD reads, allocation, UI, decoder work or logging in timing-sensitive callbacks. Record underruns outside the callback. Pause/mute before disconnected output recovery. For output changes: ramp down, stop and drain old sink, save position, initialise chosen sink, prime, ramp up using that output's stored level. An explicit restart between output modes is an acceptable first prototype fallback if seamless switching is unsafe; preserve track/position and make it visible.

Default to two mutually exclusive modes: **Listen** and **Manage music**. Separate player/uploader images are justified by [upstream measurements](https://github.com/Garionhk/CYD-MP3-Player/blob/main/docs/stage0_results.md), but must be reproduced with our selected AAC build. Explain the playback stop in the interface before entering management. Two occupied app slots are not automatically a safe A/B OTA layout: retain serial recovery and defer OTA until image placement, boot selection and interrupted-update recovery are designed.

## 7. Work chunks and dependency order

Effort is focused engineering time, excludes component delivery and user hardware availability, and is an estimate rather than a commitment. Every chunk delivers a small reviewable change/report; firmware work begins only after this planning task.

| Chunk | Owner and scope | Dependencies | Deliverable and acceptance | Estimate |
| --- | --- | --- | --- | --- |
| C0: board and real-file contract | Hardware/media worker | None | Exact PCB/panel/touch/flash profile; verified pin map; sample ffprobe/container/bitrate inventory and checksums; AirPods model confirmed | 0.5 day |
| C1: reproduce reusable base | Build/reuse worker | C0 profile | Pinned source/dependencies/licenses, local toolchain, compile-only check; original player and uploader compile; record sizes and source provenance; source backed up before any later flashing | 0.5-1 day |
| C2: mandatory AAC feasibility | Audio worker | C0; minimal toolchain from C1 | Required AAC files -> PCM -> AirPods Pro 2, Wi-Fi off, tiny UI; one hour, zero underruns/resets and memory gate; codec/container limitations explicit | 1-3 days |
| C3: DAC harness and analog proof | Hardware/audio worker | C0; can overlap C1/C2 | Verified three-signal connector and module wiring, quiet stereo PCM test, line load and headphone dummy-load measurements, no display/SD/touch conflict | 0.5-1.5 days plus parts |
| C4: shared AAC output engine | Audio worker | C2 + C3 | Same required files play through AirPods, line and headphone; safe output selection/levels; one-hour test each, switching and reconnect pass | 1-2 days |
| C5: integrate player UI | UI worker | C4 | Reuse shell browser/pairing/controls; AAC listings, duration, format-aware seek/resume, queue/shuffle/repeat/settings; responsive touch without audio regression | 1-2 days |
| C6: library and art | Storage/UI worker | C5 | On-card bounded index, tags/art or fallback, playlists, 1,000-track proposed test library; filenames/ordering/caches and startup measured | 1-2 days |
| C7: management mode | Web/storage worker | C1 image design + C5 | Wi-Fi loading accepts actual AAC containers, transactional uploads, file/playlist manager, recovery after interruption, clear return to playback | 1-2 days |
| C8: portable power and case | Hardware worker | C4 current measurements; physical DAC parts | Bank-powered pocket prototype; dimensioned enclosure, RF A/B; optional integrated charging/pack only after power-path tests and agreed runtime target | 1-3 days plus fabrication |
| C9: release qualification | Reviewer + integrator | C6 + C7; C8 for portable release | Full acceptance matrix on the final UI/library build, including one hour per wired output/container with artwork/navigation and memory checks; reproducible clean build, versioned binaries/checksums/notices and installation/recovery guide; defects fixed by owning worker | 1-2 days |
| C10: optional Fonix features | Relevant owner per feature | C9 and remaining budget | Individually benchmarked EQ/ReplayGain, visualizers, extra codecs, podcasts, exFAT or live Wi-Fi; each must preserve mandatory tests | Separate estimates |

Critical path: **C0 -> C1/C2 -> C4 -> C5 -> C6/C7 -> C9**. C3 runs alongside the AAC investigation; C8 can overlap later software after current/harness data exists. A mandatory-output bench prototype is approximately **4-8 focused days if the first AAC stack fits**, not an MP3-only milestone. A complete initial player is approximately **2-4 working weeks**, with wide uncertainty until C2 passes. An alternate decoder/container integration can add several days; a hardware architecture change needs re-estimation.

Decision gates:

1. **G0 after C0/C1:** select exact board and reproducible base. No matching display/touch profile or license provenance means resolve it before fork integration.
2. **G1 after C2:** demonstrate required original AAC on actual AirPods. If memory/timing fail, remove optional allocations, disable SBR for LC where supported and compare one alternative decoder using the same fixture. Do not substitute MP3 or declare generic tone output a pass.
3. **G2 after C4:** both wired modes and AirPods pass. If no practical third GPIO/analog design, resolve the harness/module choice before product UI expansion.
4. **G3 after C9:** release only the declared supported matrix. Optional features remain separate work.

After two failed correction cycles on the same blocker, stop that implementation thread, preserve logs and propose a fresh chat with a precise evidence-based prompt as required by the workspace instructions. Do not keep layering guesses. If ordinary reuse paths cannot meet G1, present measured alternatives: a simpler application, separate decoder/radio hardware retaining CYD as UI, or a Classic-capable PSRAM board. Hardware replacement is a user decision; AAC, AirPods and wired output stay required.

## 8. Acceptance and evidence

No item below has passed on hardware yet. Proposed durations and limits become implementation acceptance targets, with any changes recorded explicitly.

| Test | Method | Pass evidence |
| --- | --- | --- |
| A0 real media | `ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,profile,sample_rate,channels,bit_rate:format=format_name,duration -of json <file>` plus checksum; use originals and generated legal fixtures | Required four fields match; container/bitrate/duration recorded. Decoder output rate/channels also observed |
| A1 decoder correctness | Decode known left/right impulses/tones and music; compare digital PCM to reference decode, accounting for documented decoder rounding/delay | No channel swap, rate drift, gross corruption or clipping; supported files identified; no reliance on listening alone |
| A2 AAC-to-AirPods | One continuous hour with required files, screen activity, 100 track transitions; then 8-hour final soak | Zero steady-state underruns, resets, watchdog or allocation failures; memory gate passes; actual AirPods used |
| A3 AirPods lifecycle | Pair from case, remember bond/address, ten CYD reboot reconnects, case close/reopen, range loss/return, competing nearby phone | Clear bounded connection timeout, retry/forget flow and successful recovery without power reset; record codec/rate/channels and remote-control/volume behaviour |
| A4 line out | At least one hour of required AAC per supported container on the final integrated UI/library build, with scrolling, track changes and bounded artwork; high-impedance receiver/analyser for left/right, level, DC, noise, clipping and switching | Zero steady-state underruns/resets/allocation failures and memory gate passes; stable stereo at declared reference level/load; electrical results meet chosen DAC/module design |
| A5 headphones | Dummy load matching selected headphones before low-volume listening; at least one hour of required AAC per supported container on final integrated UI/library build, with scrolling, track changes and bounded artwork; measure gain, noise, DC and volume ramp | Zero steady-state underruns/resets/allocation failures and memory gate passes; adequate unclipped output and low starting level; no loud restoration on reconnect/switch; load/gain/sensitivity recorded |
| A6 switching | At least 20 Bluetooth/line/headphone changes during required AAC playback | Track/position retained within declared tolerance, no hung DMA/tasks, no progressive heap loss or audible uncontrolled pops; restart fallback documented if used |
| A7 containers and seeking | Real `.m4a` and ADTS until clarified; small/long/VBR tracks, moov-first and moov-last M4A, tags/art, EOF, seek and reboot resume | Actual user files work unmodified. Bounded tables/memory, time within proposed 1 s after seeking; unsupported/malformed inputs produce recoverable errors |
| A8 library/UI | 1,000-track proposed library; repeated scrolling, long/Unicode names, missing artwork, stale index and missing playlist entries | No whole-library RAM growth; touch feedback target p95 <150 ms under playback; index safely rebuilds; startup time measured |
| A9 management | PC and phone upload actual AAC; cancel/disconnect/reboot, card-full/path traversal, malformed request and missing card | No overwrite of valid tracks by incomplete uploads; bounded request sizes, temporary file then rename, invalid paths rejected, clear errors; no automatic card format |
| A10 persistence/power loss | Reset at track transition, index rebuild, settings save and mode switch; compare library checksums | No unrecoverable boot loop or original-music corruption; damaged cache rebuilds; settings/bonds preserved where promised |
| A11 portable | Brightness/screen timeout fixed; measure each output's energy, radio peaks, USB insertion/removal, low-cell cutoff and enclosure thermal/RF | Stable rails and defined shutdown; no USB backfeed; measured runtime/heat and RF reported with conditions; no 16-hour inference from pack capacity |
| A12 build/recovery | Clean pinned build of player and uploader, map/size report and checksum; test serial restore; interruption tests before any OTA support | Agreed check command passes, both correct images in declared slots, documented recovery and upgrade preserving/erasing settings explicitly |

Bitrate is not specified by the user's four fields. C0 must inventory it; use representative legal AAC-LC fixtures at 128/192/256 kbit/s plus the actual maximum observed. Do not claim all bitrates/profiles from a single successful track. DRM-protected or other AAC profiles are reported explicitly; they are not silently converted.

First release evidence belongs in `docs/research/cydpod-validation.md` with toolchain manifest, exact firmware commit, board/SD/DAC/headphone/AirPods versions, commands, raw logs, plots where helpful and photos/screenshots. Capture evidence on the correct device; generic upstream measurements are research only. Published passwords/credentials and personal music need not appear in logs or fixtures.

## 9. Work coordination and checks

The coordinator owns this specification, task boundaries, decision gates, integration order and status. Each implementation worker owns a bounded module/branch; only one audio-engine editor at a time. Reviewers compare the diff to this spec and report correctness/spec gaps; they do not rewrite another worker's files. The assigned implementation owner handles fixes. Recheck quota before launching an Orca worker; include heartbeat, report path and exact task/dispatch completion in each live assignment.

At each checkpoint, update the active workspace card. When a Git repository exists, work only in the assigned worktree/feature branch, keep commits small and avoid main pushes, shared rebases, force pushes or merges. The planning folder currently has no Git repository or project check command, so no commit or firmware build is claimed for this task.

Implementation C1 establishes one reproducible compile-only check, preferably wrapping the upstream `tools/build_all.sh` in a Windows-compatible entry point with isolated dependency/config directories. Proposed command: `powershell -File tools/check.ps1`. That file is **not created by this planning task**. CI runs the same pinned compile checks, meaningful pure logic tests for container/seek/queue/index errors, and size limits. Hardware reports remain separate from a passing compile. Do not create tests merely mirroring code or run flash scripts as checks.

Management requests stay local to the device/network; set a per-device credential rather than preserving an open/default uploader configuration for a shared network. Verify authentication on mutation and upload callbacks, bound paths/sizes and ensure partial uploads cannot become playable files. This is part of preserving the music library, not a separate cloud-service project.

## 10. Remaining decisions before implementation

1. Exact CYD suffix/revision/display/touch and flash/PSRAM diagnostics; whether GPIO17 solder pickup is acceptable.
2. AAC container and actual bitrate/duration/tag/art characteristics. Plan both M4A and ADTS until confirmed; a container answer narrows tests, not the required AAC profile.
3. Wired headphone impedance/sensitivity and desired line receiver/load; select an actual documented amp/module and gain accordingly.
4. Whether selectable outputs with a possible short restart meets expectations; simultaneous output is outside baseline.
5. Portable runtime/size target and preferred bank versus integrated battery, after power measurements.
6. Firmware project ownership versus fastest ready-made binary route. Current default is maintainable source reuse; no hardware purchases, dependency installation or flashing was done during planning.

These are implementation inputs, not reasons to leave the plan incomplete. The first executable work is **C0/C1 followed immediately by the mandatory AAC-to-AirPods gate, with the DAC harness investigated in parallel**.
