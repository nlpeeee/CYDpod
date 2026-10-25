# Validation evidence â€” 2026-10-25

## Reproducible host check

Run from a C compiler-enabled shell with CMake, FFmpeg and numpy installed:

```text
python tools/check.py --build build --sample "path/to/track.m4a"
```

Final local run (MSVC 19.50, x64, Release):

```text
ninja: no work to do.
Start 1: api
1/1 Test #1: api ... Passed
100% tests passed, 0 tests failed out of 1
PASS: API checks; 11 full audio comparisons; 16 rejection cases.
```

See `validation.json` for all measurements. Positive cases cover stereo at
44.1/48 kHz, mono at 8/32 kHz, a 20-minute track, front/tail `moov`, `co64`,
extended box sizes, fixed-size samples, version-1 movie/media/edit headers,
ADTS with/without CRC words, and the user-supplied Madness track. Each positive
case also checks repeated reuse of the same arena, a too-small output buffer
without advancing playback, idempotent close, and a sticky read error.

Negative cases cover truncated/invalid boxes, a missing sample description,
invalid 32/64-bit offsets, oversized/zero-length packets, invalid chunk
mapping, encrypted sample entries, HE-AAC, 960-sample frames, ADTS Main,
multi-block ADTS, fragmented MP4, and in-band SBR signalling.

The supplied track is AAC-LC, 44.1 kHz stereo, about 320 kb/s, with 7,263 coded
frames. Its largest coded packet is 1,227 bytes. Its sample-size table is
29,052 bytes and its `moov` is 75,006 bytes. MinimalAAC returns 7,434,672 PCM
frames after trimming the simple edit/final media duration. FFmpeg retains
592 additional final padding frames. Over the common audio, RMS error is
0.293684 LSB, maximum error 4 LSB, SNR 88.0973 dB. PCM comparisons allow
fixed-point differences and final AAC-frame padding.

The host workspace is 22,512 bytes; host workspace plus PCM is 26,608 bytes.
The 32-bit ESP32 measurements below are the numbers relevant to the CYD.

## ESP32 cross-compilation

```text
python tools/esp32_check.py --toolchain /path/to/xtensa/bin --work build/esp32
```

Compiler: `xtensa-esp-elf-gcc (esp-14.2.0_20260121) 14.2.0`, classic ESP32,
`-Os -fwrapv -fno-strict-aliasing -ffunction-sections -fdata-sections`.

```text
workspace_bytes:                 22480
codec_arena_bytes:               19432
pcm_bytes:                        4096
total_component_ram_bytes:       26576
text_and_literals_before_gc:     20144
rodata_before_gc:                29228
data / bss:                         0 / 0
heap_allocator_references:      false
```

See `esp32-report.json` for raw evidence and static stack frames. The largest
individual compiler-reported frame is 544 bytes for `miniaac_open`;
`miniaac_decode_next` has 112 bytes. These are individual frames, not summed
call chains or task high-water measurements. Application read callbacks and
filesystem code need additional stack. Runtime hardware measurements appear below.

## Integration compile checks

The complete Arduino SD example compiled for `esp32:esp32:esp32` on installed
Arduino-ESP32 3.3.10:

```text
Sketch uses 393630 bytes (30%) of program storage space. Maximum is 1310720 bytes.
Global variables use 49368 bytes (15%) of dynamic memory, leaving 278312 bytes
for local variables. Maximum is 327680 bytes.
```

These firmware totals include the Arduino runtime, serial, SD, SPI, filesystem
and the example's rounded static buffers. They are not the decoder's footprint.

The ESP-IDF example source was separately cross-compiled against the installed
ESP32 SDK headers. Its full standalone project has not been linked through an
ESP-IDF environment. The main component's CMake and Arduino builds both use
explicit source lists without SBR or MP3 sources.

## CYD hardware validation

After the user made COM10 available, the read-only Arduino benchmark was run
on the actual ESP32-D0WD-V3 rev 3.1 CYD at 240 MHz, with 0 bytes PSRAM and SD
SPI at 20 MHz. The whole original 4 MiB flash was backed up before testing.
The final run used a dedicated 4,096-byte producer-task stack. After testing,
the complete original 4 MiB flash was restored; esptool verified its data hash,
and two original-firmware metric lines confirmed the player was running again.

| Track | Audio duration | Decode/read wall time | Speed | PCM hash | Stack headroom |
|---|---:|---:|---:|---:|---:|
| Cassius — Cassius 1999 | 221.797 s | 49.862 s | 4.448× | 1081732884 | 1776 bytes |
| Daft Punk — Get Lucky | 247.633 s | 54.452 s | 4.548× | 1223142016 | 1776 bytes |
| Basement Jaxx — Red Alert | 257.400 s | 56.263 s | 4.575× | 1626916592 | 1760 bytes |

All tracks completed with `MINIAAC_EOF`, exact declared output-frame counts,
and PCM rolling hashes matching the desktop build. Each open caused zero
measured internal-heap change. The minimum 1,760-byte stack headroom means
2,336 bytes maximum observed producer stack use, including benchmark,
filesystem callback and JSON reporting. Rounded buffers plus the 4 KiB task
stack reserve 30 KiB, before RTOS task-control data, filesystem and downstream queues.

The longest individual read/decode call was 32.748 ms, against a 23.22 ms full
AAC-frame interval. This is compatible with faster-than-real-time production
through a queue, not with an unbuffered sink. Begin with 8 KiB of PCM queue
(46.4 ms at 44.1 kHz stereo), and measure underruns with Bluetooth/DAC and
display tasks enabled. Those subsystems were not tested here.

Reproduce using `examples/ArduinoSDBenchmark`: upload to an appropriate ESP32,
open Serial at 115200, and send `G`. The program discovers up to three distinct
SD tracks, runs a producer task, hashes/discards PCM, then prints `done`.
`hardware-events.json` contains the captured JSON events;
`hardware-report.json` includes desktop comparisons and derived measurements.
The `hardware_tested: false` field in `esp32-report.json` describes only that
separate cross-compilation check, which does not communicate with hardware.

## Full user collection

Command used after the user supplied the wider collection:

```text
python tests/corpus.py --decoder build/miniaac_decode.exe --root "E:\Alternative Wedding - UK Nostalgia\Tracks" --work build/corpus
```

```text
175/175 checked: 175 passed, 0 failed; 122.4s
```

- All 175 files were AAC-LC, 44.1 kHz stereo.
- Total decoded audio: 10.6976 hours.
- Every track was decoded completely and compared to FFmpeg, with edit/final
  padding handled as in the generated-file suite.
- Lowest SNR: 82.0045 dB; highest RMS error: 0.407314 LSB; largest absolute
  error: 6 LSB.
- Largest compressed-frame read: 1,340 bytes, below the 2,048-byte limit.
- Host workspace: exactly 22,512 bytes for every track, independent of length.
- Each track also passed workspace reuse, small-output retry and read-failure
  checks in the host decoder tool.

`corpus-report.json` contains each track's measurements; `corpus-summary.json`
contains the aggregate. The check wrote only to the workspace and removed its
two temporary PCM files after each track. No music is distributed with this
package. This validates format handling and PCM output on the host, not SD
timing on an actual ESP32.
