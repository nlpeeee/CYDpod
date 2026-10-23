# CYD hardware feasibility: SD AAC to AirPods Pro 2 and stereo wired outputs

Research date and source access date: **2026-10-23**. Research only; no hardware was examined, firmware built/flashed, dependencies installed, or playback/runtime measured. All cited external evidence is from component vendors, standards bodies, or the maintainers' own source repositories. Community board definitions are primary evidence of those maintainers' configurations, not manufacturer certification of an unidentified board. Mutable `main` and `stable` URLs describe the versions accessed on this date; ESP-IDF example/configuration references below are pinned to v5.5.1.

## Decision and confidence

**Mandatory baseline:** original **ESP32-2432S028 family**, no PSRAM assumed; local **AAC-LC at 44100 Hz stereo**; Bluetooth specifically **AirPods Pro 2**; and an external DAC connection providing **stereo line output and stereo headphone output**. Container is unknown: provisionally support both unencrypted **`.m4a` (MP4 AAC tracks)** and **ADTS AAC** until clarified. These require container parsing/demux and AAC decoding, not merely matching a filename. PCM/WAV transport runs below are diagnostics only; they cannot satisfy AAC acceptance. MP3 is optional and cannot substitute for the required AAC path. Output is selected between Bluetooth, wired line and wired headphones; simultaneous Bluetooth/wired playback is not required.

**Proceed with a USB-powered proof of concept on the already-owned CYD before buying another processor or integrating a cell.** Original ESP32 supports Bluetooth Classic and Espressif supplies an A2DP **source** example [S8, S9]. That establishes radio/profile capability, not successful simultaneous SD reads, compressed-audio decoding, touch, display, and SBC encoding without PSRAM. That combined workload remains a measured feasibility gate.

The coordinator relayed the user's marking as **`ESP-32-2432SO28`**, provisionally interpreted as the **ESP32-2432S028 family** (`O` versus zero is unresolved). Suffix, PCB revision, module, display driver and touch controller remain unconfirmed. Treat original ESP32 without PSRAM as the requested design baseline, rather than a hardware observation. A Fonix-like device here means an offline SD library, on-device selection and playback controls, and Bluetooth headphone/speaker output; no claim of matching a particular commercial product's internals or feature set is made.

Confirmed in sources: Classic-capable original ESP32; ILI9341 and ST7789 reference configurations; the reference resistive-touch and SD pins; a built-in DAC-to-amplifier path; optional rather than default exFAT. Assumptions: the owned board matches one of those configurations and can meet the intended decoder/UI workload. Unknowns: exact hardware, usable heap, card compatibility, headphone interoperability, current consumption, battery capacity and enclosure dimensions. **There is no runtime estimate or battery-life claim.**

## Identification checklist before selecting a board configuration

- Photograph both PCB faces, module shield text, full model suffix/revision, LCD ribbon and controller markings where accessible without damage. Record screen diagonal, pixel dimensions, PCB dimensions, holes and connector locations.
- Record whether USB is micro-only, USB-C-only, or micro plus USB-C. witnessmenow documents the dual-port version separately and warns that its USB-C connector lacks CC resistors, preventing C-to-C operation [S2]. This is a reference-board observation, not proof that every later clone has the flaw. Use a known-working cable/input for initial tests.
- Read the module name and later record chip identity, flash size and PSRAM detection with an approved diagnostic. Do not infer Classic Bluetooth from the word “ESP32” alone: **ESP32-S3 and ESP32-C3 lack Classic BR/EDR**, despite supporting BLE [S8]. They cannot directly replace the original ESP32 for this A2DP requirement; a separate Classic transmitter would be another architecture.
- Determine display driver using matched revision documentation and a controlled colour/rotation test. Do not use inversion or USB count alone as a controller identifier. The original witnessmenow setup selects `ILI9341_2_DRIVER`; its CYD2USB setup selects `ST7789_DRIVER` [S3, S4].
- Distinguish a resistive overlay/XPT2046 from capacitive touch by suffix, IC/flex identification and schematic/configuration evidence. Record calibration/orientation needs. A “C” model from another size is not evidence that the owned S028 has capacitive touch.
- Identify regulator and amplifier markings, SD wiring, VIN/5 V/3.3 V rails, USB interconnections and any existing charger. Verify connector pitch, polarity and continuity with power disconnected before adding external wiring. No charger or safe battery input has been confirmed for this unit.

## Provisional variant and resource map

These maps apply only after matching the actual board. GPIO numbers are MCU GPIO, not connector pin positions.

| Candidate | Source-confirmed configuration | What remains provisional |
| --- | --- | --- |
| Original ESP32-2432S028R | ILI9341 SPI, 240 × 320, XPT2046 resistive; witnessmenow original configuration [S1, S3, S5] | Matching owned PCB, module and memory |
| S028R USB-C-only, called Rv2 in rzeldent definitions | ILI9341, same listed main display/touch/SD GPIO assignments, custom panel initialization [S6] | Revision naming is maintainer-specific; do not select dual-port ST7789 solely because USB-C exists |
| Dual-port S028R / CYD2USB, called Rv3 in rzeldent definitions | ST7789 configuration; BL21, MOSI13, SCLK14, CS15, DC2; rzeldent leaves MISO unconnected in software and uses SPI mode 3 [S2, S4, S7] | Electrical MISO routing, inversion, colour order and exact initialization on owned panel |
| Capacitive lookalike, example S024C | Maintainer JSON uses ILI9341 and a CST816S-compatible I2C driver: SDA33, SCL32, reset25, IRQ21; BL27 [S15] | This is a **2.4-inch comparison**, not an S028C pinout; controller silicon needs checking |

Capacitive evidence needs care: rzeldent's catalogue labels S024C touch CST820, whereas its actual definition selects the CST816S I2C driver [S15, S16]. Report that distinction rather than choosing a chip identity from either alone. Such a variant could free a hardware SPI host, but it also reassigns pins used by the S028 backlight and expansion connectors.

| Resource on reference S028R | GPIO / constraint | Evidence |
| --- | --- | --- |
| TFT / HSPI (SPI2) | MOSI13, MISO12, SCLK14, CS15, DC2; backlight21; reset configured as -1 in witnessmenow | [S1, S3] |
| XPT2046 touch | MOSI32, MISO39, CLK25, CS33, IRQ36 | [S1, S5] |
| microSD / VSPI (SPI3) | MOSI23, MISO19, SCLK18, CS5; SPI, not a demonstrated four-bit SDMMC wiring | [S1, S10] |
| Local speaker | GPIO26 into amplifier; speaker connector is amplifier output | [S1, S11] |
| RGB / light sensor | Active-low R4/G16/B17; light sensor34 | [S1] |
| Expansion | GPIO22 on P3 and CN1; GPIO27 on CN1; GPIO35 input-only with no internal pull-up; GPIO21 already backlight | [S1] |
| Serial / boot | P1 VIN/GND and tentative TX1/RX3 in source; BOOT0 is also a boot control | [S1] |

PINS.md calls the small connectors 1.25 mm Molex PicoBlade (often sold as JST/mx1.25); verify the actual unit and mating lead rather than assuming a 2 mm battery JST fits [S1]. Original ESP32 has input-only GPIO34–39, boot strapping pins and two 8-bit DACs; check strap behaviour before loading pins at reset [S9]. Battery sensing on GPIO35 is only a proposal: it needs a correctly scaled/filtered divider, ADC calibration and leakage accounting, with voltage always within the confirmed input limits.

### SPI contention and integration choice

**Distinct wires do not guarantee distinct hardware controllers.** witnessmenow's TouchTest creates `SPIClass(VSPI)` on touch pins 25/39/32/33 [S5]; SDCardTest separately uses VSPI with default SD pins [S10]. Original ESP32's general SPI master driver supports SPI2/SPI3 and excludes SPI0/1 [S12]. Simply combining both sketches can remap/reinitialize a host, break transfers or create races. Display+touch success and SD-only success do not prove display+touch+SD success.

Proposed initial allocation: TFT on HSPI, SD on VSPI, and a small, bounded software-SPI XPT2046 transaction on its existing wires. This requires a compatible touch implementation and timing tests; it is not functionality proven by these examples. Poll modestly or use IRQ to schedule reads; avoid filesystem or display work in interrupts. Keep ownership of each hardware host explicit. Avoid repeated pin remapping of a live SD bus.

If software touch fails its latency/CPU gate, assess a deliberately serialized host-remapping implementation only after draining DMA/transactions and proving library behaviour, or a hardware redesign. Ordinary shared-bus chip-select arbitration applies when devices physically share clock/data lines; the CYD's three different pin groups are not that arrangement. Rewiring would require inspection and separate approval/scope. Even with independent display and SD hosts, CPU, DMA-capable memory, interrupt service and scheduler time remain shared [S12].

## A2DP source pipeline and no-PSRAM budget

Proposed path: **SD `.m4a` demux or ADTS parser → AAC-LC decoder → 44100 Hz stereo PCM ring → selected SBC/A2DP source to AirPods Pro 2 OR I2S DAC to wired output**. Bluetooth output does not require the external DAC to be active. A2DP sink examples receive audio and do not establish source capability. AAC files are decoded locally; AirPods AAC capability does not prove AAC passthrough or an AAC encoder in this ESP32 source stack. The provisional Bluetooth path is PCM to SBC; log the actual negotiated codec/rate on hardware [S13].

Espressif's v5.5.1 source example describes SBC encoding of normally 44.1 kHz, two-channel, 16-bit PCM and a single remote sink; source and sink roles cannot run simultaneously in that example's documented limitation [S13]. Its generated audio is random noise: do not use it at headphone listening volume. Use a quiet known signal in a later approved bring-up. The v5.5.1 PCM input callback is explicitly marked deprecated [S14]; pin framework/library versions and verify the chosen API instead of mixing a current source library with an old sketch.

Engineering arithmetic, not benchmark results:

| Item | Derived size / rate | Consequence |
| --- | --- | --- |
| RGB565 full screen | 240 × 320 × 2 = 153,600 bytes; two buffers 307,200 bytes | Avoid full-screen double buffering initially |
| Small display strip | 320 × 20 × 2 = 12,800 bytes per strip | Candidate bounded UI buffer; dimensions depend on orientation |
| 44.1 kHz stereo 16-bit PCM | 176,400 bytes/s | SD WAV test must exceed this rate with latency margin |
| 100 ms / 200 ms PCM | 17,640 / 35,280 bytes | Ring size is a tradeoff, not a promise of stall immunity |
| 320 kbit/s compressed input | 40,000 bytes/s before filesystem overhead | Decoder and SBC still consume CPU/RAM |

ESP32's advertised 520 KB SRAM is not a single free application buffer [S9]. Measure internal free/minimum heap, largest free block, DMA heap and task stack high-water marks after Bluetooth, AAC decoder, demux and UI initialization, then through navigation/reconnects. DMA buffers require suitable capabilities [S17]. Reserve audio buffers first; draw dirty regions, cap art resolution and paginate metadata. PCM WAV isolates transport only. Mandatory acceptance is AAC-LC 44100 Hz stereo in both provisional containers with the full UI and each selected output; benchmark the selected decoder/demux implementation before declaring no-PSRAM feasibility. Cover actual library bitrates, VBR, MP4 metadata/cover art and sample tables, ADTS framing, seeks and malformed/truncated files. Other codecs, resampling and gapless playback need separate scope. Keep SD/decoder work outside the audio callback. Disable unused Wi-Fi for the baseline, then test coexistence only if required.

The witnessmenow local-radio example uses the built-in DAC and forces mono; it explicitly warns of Arduino-ESP32 2.0.17 compatibility rather than 3.x [S11]. It is useful evidence of the local analog route, not a validated library choice for this project. The onboard 8-bit DAC/amplifier path is unsuitable as evidence of high-quality stereo headphone output; no headphone jack or safe direct headphone connection has been confirmed. The repository also reports excessive amplifier gain and a hardware gain modification [S18]. Defer speaker hardware; it adds no value to proving Bluetooth playback.

## External stereo DAC, line and headphone connection proposal

**Preferred provisional path: PCM5102A line DAC plus a documented stereo headphone amplifier.** It needs only three I2S outputs and no I2C. Proposed ESP32-master timing is LRCK **44100 Hz**, BCK **2.8224 MHz** (64 bits/frame: two 32-bit slots containing the decoded 16-bit samples, aligned by the selected driver). This clock arithmetic is a configuration proposal, not a measurement. Confirm framing, channel order and sample alignment with a logic analyser.

| Proposed harness net | CYD pickup | DAC destination / restriction |
| --- | --- | --- |
| BCK | GPIO22, CN1 IO22 (also P3) | PCM5102A BCK; do not allocate this net to I2C too |
| LRCK / WS | GPIO27, CN1 IO27 | PCM5102A LRCK |
| DATA OUT | GPIO17, **new verified solder pickup required** | PCM5102A DIN; sacrifice blue LED function and isolate its load before qualifying this net |
| Ground | CN1 GND | DAC and amplifier ground; short common return |
| Power | CN1 3.3 V only if regulator headroom is measured | Verified 3.3 V-compatible DAC supply input; otherwise separately regulated supply sharing ground |

GPIO22/27 do not conflict with the reference TFT, SD or touch allocations. **GPIO17 is not documented on P1/P3/CN1**; it drives the active-low blue LED [S1]. Thus this is a conflict-free allocation only after reserving GPIO17 exclusively for I2S, disabling all RGB writes to it, tracing its MCU-side connection and isolating the LED branch (matched schematic/continuity first). There is no verified plug-only third-output solution. No soldering was performed. If GPIO17 cannot be safely reached, this map is blocked; do not silently steal SD, touch, backlight or UART pins. GPIO35 is input-only and cannot supply DIN, BCK, LRCK, MCLK or control outputs [S1, S9]. GPIO16/4 LED-pad alternatives also require a separately checked modification and are not free connector pins.

The following **J_AUDIO is a proposed new keyed 6-pin adapter**, not an existing CYD header: pin 1 GND, 2 verified 3.3 V supply, 3 BCK22, 4 LRCK27, 5 DIN17, 6 reserved/NC. Obtain IO22/IO27/GND/3.3 V from a verified CN1 mating lead and DIN17 from the new pickup. Verify orientation/polarity and continuity; source tables give net names, not a safe physical left-to-right view. Do not use a DAC module's unverified VIN label as proof that it accepts 3.3 V or 5 V. Leave pin 6 unused until a documented mute/control design exists.

### PCM5102A and separate headphone amplifier

TI specifies PCM5102A as a **line driver**, 2.1 Vrms full-scale, loads **at least 1 kΩ**; a 16/32 Ω headphone is outside that load specification [S24]. Select a breakout with schematic, exposed OUTL/OUTR/GND, correct decoupling/charge-pump capacitors and documented regulator input. At IC level AVDD/CPVDD are nominal 3.3 V; use 3.3 V DVDD for ESP32 logic. No 5 V logic. Hardware straps: FMT low (I2S), DEMP low (off), FLT low (normal filter); SCK/MCLK grounded for BCK-derived PLL mode; XSMT high for operation, low for mute. No I2C initialization or external MCLK is required; verify breakout strap defaults and power-down behaviour [S25].

Use separate labelled **LINE OUT** and **HEADPHONE** jacks, each stereo tip=L, ring=R, sleeve=signal ground. Route DAC OUTL/R directly to line out and, in headphone mode, to the amplifier input. Select a line destination with documented impedance preferably **≥10 kΩ**, required sensitivity/full-scale level, unbalanced ground compatibility and acceptable clipping limit. Choose fixed calibrated line level (2.1 Vrms maximum, attenuate if destination needs less); headphone volume must not silently alter that setting. Never connect the CYD speaker output or a bridge/class-D speaker output to either jack.

A documented bench amplifier option is **TI TPA6132A2EVM2**; TI links its EVM guide/schematic, including the stereo jack, single-ended input configuration and gain jumpers [S26, S27]. Feed DAC L/R to INL/INR, common ground, JP1/JP2 shunted for single-ended operation; use G1=0/G0=0 (**−6 dB**) initially. The guide specifies a **2.5–5.5 V** bench supply; use regulated 3.3 V with verified current margin. EN/shutdown and gain are hardware controls, no I2C or MCLK. The IC offers −6/0/+3/+6 dB and specifies **25 mW into 16 Ω** [S28]. It is a bench reference, not a compact enclosure recommendation or a verified in-stock purchase.

Gain/load gate: 25 mW into 16 Ω implies about **0.632 Vrms** (sqrt(P×R)); a full-scale 2.1 Vrms DAC through −6 dB would still request about **1.05 Vrms**, so even minimum amp gain needs at least approximately **4.4 dB further attenuation** to stay at that nominal power, plus measured clipping/sensitivity margin. Begin muted and attenuated much further; choose a dual-gang input attenuator or bounded digital headphone volume after documenting the actual headphones' impedance, sensitivity, desired maximum SPL and amplifier output swing at the chosen supply. Test 16/32 Ω dummy loads, distortion, DC offset, channel balance and plug transients before listening; do not extrapolate the 16 Ω rating to every headphone. A generic board with a stereo socket and unknown gain is not an equivalent validated amplifier.

### Practical integrated alternative and its extra pins

**Adafruit TLV320DAC3100 breakout** integrates DAC and stereo headphone drivers; the vendor documents direct 16 Ω headphone use and AC-coupled jack output [S29]. It can derive its clock from BCK, so MCLK is optional, but **I2C configuration is mandatory**, unlike PCM5102A. Board VIN is 3.3–5 V with 3.3 V logic; speaker use requires 5 V and is outside this requirement. SDA/SCL each have 10 kΩ pull-ups; RST needs a low-to-high initialization pulse [S30]. Use its regulators rather than applying board VIN directly to the IC core rails.

This alternative is electrically practical but **not a drop-in on the three-output harness**: retain BCK22/WS27/DIN17, then provisionally reclaim verified green LED GPIO16 for SDA and red LED GPIO4 for SCL, isolating those LED loads and checking reset-time loading on GPIO4; RST requires a documented manual reset circuit for a bench test or another verified output for automated recovery. Those pads, pull-up rails and reset timing remain unresolved. Do not reuse 22/27 simultaneously as I2C. Disable the mono class-D output. The IC's headphone PGA is 0 to +9 dB, with analog attenuation to −78 dB and mute; start at 0 dB and strong attenuation [S31]. Select its explicit **headphone versus line-out mode**, using page 1 register 44 bits D2–D1 = 11 for line mode, and explicitly setting both driver power bits (page 1 register 31), common mode, gain/routing and power sequence; the maintained library exposes `headphoneLineout(left,right)` [S32]. One shared stereo jack selected by mode is sufficient if simultaneous line/headphone use is not required; verify delivered line voltage, output loading, AC-coupling bass response and mute transitions on the actual board. It does not provide two independent simultaneous stereo paths by assumption.

### Output selection and unresolved electrical checks

Provide explicit Bluetooth / wired line / wired headphone selection. Mute and ramp down, stop/drain the active transport, select/initialize the destination with its saved level, prime buffers, then ramp up. For the two-board path, a **break-before-make stereo signal selector** can route DAC L/R to either LINE OUT or amp inputs, avoiding a parallel load; the amplifier enable can follow that selector. Do not tie outputs together. Bluetooth selection stops wired audio; no concurrent BT/I2S fanout is required. If XSMT/EN remain strapped, qualification must prove software ramps, clock stopping and power transitions avoid pops; autonomous hardware mute needs additional verified control circuitry/pins.

Before wiring: identify exact PCB/module; confirm GPIO17 access and LED isolation; confirm connector orientation, module straps/regulator inputs, 3.3 V logic thresholds and rail headroom under radio/display load; measure I2S clock integrity, DAC/amp supply noise, ground loops and transient current. Before output acceptance: record actual headphone gain/load/sensitivity, line receiver impedance/level, output DC, clipping, noise and switching/plug pops. No matched owned-board schematic, electrical measurements or hardware validation is claimed.

## AirPods Pro 2 interoperability gate

Apple explicitly permits non-Apple Bluetooth pairing: with **AirPods Pro 2 in their case and lid open**, hold the **rear setup button about five seconds until the light flashes white**, then select them on the source [S33]. This supports a pairing procedure, not ESP32 compatibility certification. Record actual case/earbud model, firmware and selected ESP-IDF/library versions.

Actual AirPods Pro 2 are mandatory for acceptance: fresh discovery/pairing and stored-bond reconnect; CYD reboot, case close/reopen, out-of-range return and earbud charging cycles; interaction with a nearby previously paired phone; negotiated A2DP codec and 44100 Hz stereo stream; one-hour AAC playback while navigating the UI; repeated Bluetooth/wired switches. Verify low initial volume, both CYD volume and AirPods stem swipe behaviour, AVRCP absolute-volume support/event direction and synchronization, and play/pause/next/previous commands supported by the chosen stack. Log unsupported commands and implement a defined fallback; generic AVRCP support does not establish these interactions. Require no unexpected maximum-volume restoration, resets or unrecoverable reconnect failures. PCM-only sound, another headset, or Apple’s guide alone cannot pass this gate. No pairing/reconnect/volume/AVRCP test was performed.

## SD cards, filesystems and large libraries

SD Association defines SDHC above 2 GB through 32 GB with FAT32 and SDXC above 32 GB through 2 TB with exFAT [S19]. Capacity class, SPI electrical support, partition layout and filesystem support are separate questions. A physical SD slot does not establish support for every capacity or filesystem.

Start with an already-owned, backed-up FAT32 SDHC card of at most 32 GB and a small known library. ESP-IDF v5.5.1 has `FF_FS_EXFAT=0`; enabling it also requires long filenames [S20]. Current stable documentation describes manual configuration/integration rather than a normal exFAT menu option [S21]. Verify the exact chosen Arduino/IDF filesystem build; do not assume `SD.h` mounts factory-formatted SDXC. A larger card reformatted FAT32 is a separate compatibility experiment, not proof of SDXC-standard formatting support. Never format the user's music card automatically on mount failure.

Test candidate large cards individually for mount, full reported capacity, directory traversal, filename encoding, end-of-file seeks, and sustained reads with checksums. FAT32's roughly 4 GiB individual-file ceiling matters for long lossless recordings; whole-library size is a different limit. Validate file-offset/API width for large tracks and bound index memory for thousands of tracks. Card speed labels do not predict worst read stalls in SPI mode. SDCardTest requests 80 MHz and writes/deletes test files [S10]; do not treat that clock as a reliable board/card rating or run it on irreplaceable content. Begin conservatively, then record the stable clock and latency distribution on the actual wiring/card.

## Provisional bill of materials

No quantities below imply a purchase decision, price, or verified compatibility.

| Stage | Item | Selection condition |
| --- | --- | --- |
| First | Already-owned CYD, quantity 1 | Identify actual revision; no replacement initially |
| First | Existing USB supply/data cable and actual AirPods Pro 2 | Known-good cable; actual-device interoperability gate |
| Wired prototype | Documented PCM5102A breakout, TPA6132A2EVM2 or schematic-equivalent stereo headphone amp, separate jacks and stereo selector | Verified gain, loads, input supply, 22/27/17 pickup and mute plan; integrated TLV320DAC3100 alternative needs extra control pins |
| First | Existing backed-up microSD and card reader | FAT32 SDHC baseline; obtain a small card only if none is available |
| Measurement | USB power meter, multimeter; oscilloscope/logic analyser if available | USB meter for energy/average, scope for transient rails and SPI; borrow first |
| Portable baseline | Existing reputable USB power bank, if available | Validate low-load shutoff, button behaviour and any claimed pass-through operation; no enclosure cell wiring needed |
| Integrated battery, deferred | Protected single-cell Li-ion/LiPo, documented charger with system power path, regulated 5 V boost, switch and suitable protection | Specific pack/module datasheets, dimensions, peak current, charge rate, thermal and backfeed checks must pass |
| Mechanical, deferred | Matched leads, insulated mounting/spacers, enclosure, strain relief, cell retention | Measure actual board/pack/connectors and prove RF/thermal clearance first |

TI BQ24074 is an **example power-path charger class**, not a complete CYD battery solution. Its documented system output is 4.4 V under input power and follows the cell during battery operation; it does not itself supply regulated 5 V [S22]. Adafruit's own BQ24074 board documentation confirms load sharing and a load output no higher than 4.4 V, with pack polarity/chemistry restrictions [S23]. Any selected module still needs a compatible regulator stage and verified charge settings. A generic charger attached to BAT with the device also across BAT must not be assumed to provide managed load sharing or correct charge termination.

## Battery and enclosure gates

Proposed architecture: USB input → documented charger/system power path → appropriately rated 5 V conversion → **verified CYD 5 V input**; protected cell → charger BAT. This is a design proposal, not an approved wiring diagram. Never put a raw cell on 3.3 V, or assume its full discharge range meets the board's VIN regulator requirements. Inspect the actual schematic/rails before choosing between a 5 V input and a regulator bypass; the latter is outside this report's implementation scope.

1. **Board gate:** verify regulator input range/dropout, polarity, current peaks, power connector rating, USB/VIN connectivity and backfeed isolation. No direct battery connector or charger capability is assumed. Check supply paths for both USB ports where fitted.
2. **Cell/module gate:** select chemistry, protection, undervoltage cutoff, charge current and temperature limits from the actual pack/module datasheets. Confirm lead polarity, overcurrent protection and a suitable switch; set charging current for the pack, not the charger's maximum rating. Check converter startup, peak output, efficiency and quiescent loss across cell voltage.
3. **Power-path gate:** test USB insertion/removal while playing, dead/low-cell startup, charging while playing, charge termination, off-state drain and absence of USB backfeed. A board that charges is not necessarily capable of seamless play-and-charge operation. Log resets, audible gaps, rail droop and temperature under worst display/radio load.
4. **Mechanical gate:** make a measured drawing; preserve access to SD, reset/boot and connectors. Keep cell insulated from solder joints and fasteners, avoid pressure on the pouch and display/touch layer, provide cable retention and thermal clearance. Keep battery/metal away from the module antenna region and compare RF performance before/after enclosure assembly. Cell swelling allowance and pack mounting follow the chosen pack guidance, not guessed dimensions.
5. **Runtime gate:** measure energy at the battery, ideally integrating voltage/current through a complete discharge to the selected cutoff. Also record regulated output energy to determine conversion loss. USB-input averages alone cannot establish battery life. Repeat with documented brightness, screen timeout, codec/bitrate, sink, distance, volume setting, cell age/capacity, ambient temperature and enclosure. Record playback, pause/screen-off, boot/pairing/reconnect and power-off drain separately. An estimate may later use usable battery Wh divided by measured average battery W; do not mix battery mAh with 5 V load current. Publish measured elapsed hours and conditions only after the run.

## Staged bring-up and performance acceptance

The following are **proposed future tests**, not tests performed during research. Numeric durations are initial engineering targets, subject to the product specification.

| Stage | Method and evidence to retain | Pass / decision gate |
| --- | --- | --- |
| 0: identify | Photos, dimensions, exact markings, rail/connector map and later chip/memory diagnostics | Exact board profile selected; no conflicting pin owners |
| 1: USB display/touch | Matched HelloWorld/TouchTest concepts; colour bars, rotations, touch edges/pressure, brightness sweep | Correct panel geometry/colours; reliable touch; stable rails |
| 2: SD alone | Read-only baseline files with checksums, 4–32 KiB candidate reads, conservative clock; log latency median/p95/p99/max and throughput | No corruption/mount instability; measured worst stalls fit planned buffering |
| 3: radio diagnostics | Quiet PCM to actual AirPods Pro 2; record pairing/reconnect, codec, volume and AVRCP events | Diagnostic transport evidence only; actual-device gate above remains mandatory |
| 4: transport diagnostics | SD WAV 44100 Hz stereo to A2DP and separately I2S; log underruns, read stalls, CPU, heap/stacks | 30-minute diagnostic run with headroom; cannot establish AAC acceptance |
| 5: mandatory AAC + UI + outputs | AAC-LC 44100 Hz stereo in both `.m4a` and ADTS until clarified; actual AirPods Pro 2, wired line and wired headphone separately; scrolling and track changes | One hour per selected output/container with zero underruns/resets, no progressive heap loss, touch target under 100 ms; documented gain/load, line levels, safe switching and actual-device controls/reconnect gate pass |
| 6: adverse conditions | Fragmented/near-full card, large library, malformed file, card removal, sink power cycle, body/pocket obstruction; Wi-Fi only if required | Defined recoverable errors; no crash, unsafe loud output or card autoformat; RF range measured |
| 7: portable power | Bank first, integrated pack only after power gates; scope transitions; charging thermal run, full discharge and enclosure A/B | No backfeed/rail failure; acceptable measured heat; measured runtime and RF satisfy an agreed requirement |

For streaming, use measured maximum producer stall plus scheduling/decoder delay to size the PCM reserve: buffered seconds = available PCM bytes / 176,400 at the baseline format. Average SD throughput alone is insufficient. If the no-PSRAM gate fails, first reduce UI buffers/art, bound metadata, inspect allocations and decoder cost, and change scheduling/buffering based on traces. A replacement board is justified only by measured failure; an S3/C3 alone would sacrifice the Classic radio requirement.

## Source register

All sources below accessed **2026-10-23**. No third-party generated summaries are used as evidence.

- **S1 — witnessmenow, reference pinout:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/PINS.md
- **S2 — witnessmenow, board and dual-USB differences:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/cyd.md
- **S3 — witnessmenow, original display configuration:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/DisplayConfig/User_Setup.h
- **S4 — witnessmenow, CYD2USB display configuration:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/DisplayConfig/CYD2USB/User_Setup.h
- **S5 — witnessmenow, actual TouchTest source:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/Examples/Basics/2-TouchTest/2-TouchTest.ino
- **S6 — rzeldent, Rv2 board definition:** https://raw.githubusercontent.com/rzeldent/platformio-espressif32-sunton/main/esp32-2432S028Rv2.json
- **S7 — rzeldent, Rv3 board definition:** https://raw.githubusercontent.com/rzeldent/platformio-espressif32-sunton/main/esp32-2432S028Rv3.json
- **S8 — Espressif, chip Bluetooth capability matrix:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/bt-architecture/overview.html
- **S9 — Espressif, ESP32 Series Datasheet v5.3:** https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
- **S10 — witnessmenow, actual SDCardTest source:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/Examples/Basics/3-SDCardTest/3-SDCardTest.ino
- **S11 — witnessmenow, actual HelloRadio source:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/Examples/Basics/7-HelloRadio/7-HelloRadio.ino
- **S12 — Espressif, SPI master API:** https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/spi_master.html
- **S13 — Espressif, v5.5.1 A2DP source example and limitations:** https://raw.githubusercontent.com/espressif/esp-idf/v5.5.1/examples/bluetooth/bluedroid/classic_bt/a2dp_source/README.md
- **S14 — Espressif, v5.5.1 legacy PCM source callback:** https://raw.githubusercontent.com/espressif/esp-idf/v5.5.1/components/bt/host/bluedroid/api/include/api/esp_a2dp_legacy_api.h
- **S15 — rzeldent, capacitive S024C board definition:** https://raw.githubusercontent.com/rzeldent/platformio-espressif32-sunton/main/esp32-2432S024C.json
- **S16 — rzeldent, board catalogue:** https://github.com/rzeldent/platformio-espressif32-sunton
- **S17 — Espressif, heap capabilities and DMA allocation:** https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html
- **S18 — witnessmenow, amplifier and PSRAM hardware modifications:** https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/Mods/README.md
- **S19 — SD Association, capacity and filesystem standards:** https://www.sdcard.org/developers/sd-standard-overview/capacity-sd-sdhc-sdxc-sduc/
- **S20 — Espressif, v5.5.1 FatFs configuration:** https://raw.githubusercontent.com/espressif/esp-idf/v5.5.1/components/fatfs/src/ffconf.h
- **S21 — Espressif, current FAT/exFAT integration documentation:** https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/fatfs.html
- **S22 — Texas Instruments, BQ2407x datasheet SLUS810N, revision October 2021:** https://www.ti.com/lit/ds/symlink/bq24074.pdf
- **S23 — Adafruit, its BQ24074 board operating constraints:** https://learn.adafruit.com/adafruit-bq24074-universal-usb-dc-solar-charger-breakout
- **S24 — TI, PCM5102A product specifications (line output, not headphone drive):** https://www.ti.com/product/PCM5102A
- **S25 — TI, PCM510xA datasheet Rev. C, pins, clocks and power:** https://www.ti.com/lit/ds/symlink/pcm5102a.pdf
- **S26 — TI, documented TPA6132A2EVM2 stereo headphone amplifier board:** https://www.ti.com/tool/TPA6132A2EVM2
- **S27 — TI, TPA6132A2 EVM user guide SLOU230, connections, gain jumpers and schematic (linked by EVM2 product page; verify received revision):** https://www.ti.com/lit/pdf/SLOU230
- **S28 — TI, TPA6132A2 gain, supply and 16-ohm power rating:** https://www.ti.com/product/TPA6132A2
- **S29 — Adafruit, integrated TLV320DAC3100 board overview and output coupling:** https://learn.adafruit.com/adafruit-tlv320dac3100-i2s-dac
- **S30 — Adafruit, TLV320DAC3100 board pinout, reset and I2C pull-ups:** https://learn.adafruit.com/adafruit-tlv320dac3100-i2s-dac/pinouts
- **S31 — TI, TLV320DAC3100 datasheet Rev. C, clock sources, headphone/line modes and gain:** https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf
- **S32 — Adafruit, maintained TLV320DAC3100 library API, headphoneLineout:** https://adafruit.github.io/Adafruit_TLV320_I2S/html/_adafruit___t_l_v320_d_a_c3100_8h_source.html
- **S33 — Apple, pairing AirPods (including Pro 2) with a non-Apple device:** https://support.apple.com/en-gb/guide/airpods/dev499c9718b/web

## Research verification and remaining evidence

Source code was inspected for the original/dual-port panel drivers, explicit touch/SD VSPI assignment, SD test side effects, DAC radio example compatibility, the legacy PCM callback and disabled exFAT configuration. The S024C catalogue/configuration discrepancy is retained explicitly. Manufacturer schematics for the owned PCB were **not** verified; power-path, regulator limits and dimensions remain blocked on physical identification and matched documentation.

No repository build/check command was discoverable for this documentation-only task. Hardware acceptance is not claimed. Validation checks required content, unique source definitions, defined references and absence of the superseded MP3-first acceptance text; future implementation must supply actual AAC playback, wired electrical and AirPods Pro 2 evidence for the gates above. Only this report was edited; no firmware, Git operations, installs, flashing or other workers were used.

