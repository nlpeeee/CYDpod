# MinimalAAC

A small, pull-based AAC-LC/M4A to PCM component for classic ESP32/CYD without
PSRAM. Plug it into your existing project; feed the resulting PCM to your own
Bluetooth producer queue or DAC driver. The component creates no tasks and
performs no heap allocation, including during open/close.

**ESP32 persistent RAM: 26,576 bytes (25.95 KiB), including the 4,096-byte PCM
buffer.** This was measured from the cross-compiled target types, not estimated
from desktop pointer sizes. The examples reserve a rounded **26 KiB** in total.
Task stack, filesystem/SD buffers, and Bluetooth/DAC queues are additional.

## Integration

The public interface is `src/miniaac.h`. Supply:

1. An 8-byte-aligned workspace that stays alive until close.
2. An absolute-offset read callback and the source's byte length.
3. A PCM buffer of 2,048 `int16_t` values (4,096 bytes).

```c
#include "miniaac.h"

static uint64_t arena[MINIAAC_WORKSPACE_BYTES / 8];
static int16_t pcm[MINIAAC_PCM_SAMPLES];

/* Implement against your SD/file abstraction. Return 0 only on an exact read. */
int sd_read_at(void *file, uint64_t offset, void *dst, size_t bytes);

/* file_handle and file_size come from your application. */
miniaac_source source = {sd_read_at, file_handle, file_size};
miniaac *decoder;
miniaac_status rc = miniaac_open(arena, sizeof(arena), &source, &decoder);
if (rc == MINIAAC_OK) {
    miniaac_info info;
    miniaac_get_info(decoder, &info);
    size_t count;
    while ((rc = miniaac_decode_next(decoder, pcm, MINIAAC_PCM_SAMPLES,
                                      &count)) == MINIAAC_OK) {
        /* Consume/copy ALL count int16_t values before decoding again.
           Stereo frame count = count / info.channels. */
        output_pcm(pcm, count, info.sample_rate, info.channels);
    }
    /* rc == MINIAAC_EOF means success; negative values are errors. */
    miniaac_close(decoder);
}
```

The snippet is an application fragment: provide `file_handle`, `file_size`,
`sd_read_at`, and `output_pcm`. Complete buildable examples are included below.

`MINIAAC_WORKSPACE_BYTES` is a convenient 22 KiB upper bound with a compile-time
size check. For exact allocation, call `miniaac_workspace_size()` (22,480 bytes
on the tested ESP32 toolchain). Allocate it once using your application's
allocator, retaining 8-byte alignment and internal RAM. The library itself does
not allocate memory. Workspace, PCM buffer and source backing storage must not
overlap. Each decoder instance has independent state; serialize calls on the
same instance.

PCM is signed 16-bit, native-endian, interleaved left/right for stereo, at the
source sample rate. On ESP32 this is little-endian. There is no resampling,
downmixing, gain, Bluetooth transport, or DAC configuration.

Use a producer task to decode and feed any Bluetooth queue. An audio callback
should drain that queue; SD reads and decoding belong outside that callback.
Partial sink writes must retain their unconsumed PCM before the next decode.
The Arduino example demonstrates this contract. Decoding errors are terminal;
close/reopen to restart. An undersized PCM buffer can be retried without advancing.

## Arduino / ESP-IDF

**Arduino:** copy the entire `miniaac` directory into your sketchbook's
`libraries` directory, or install the supplied ZIP as an Arduino library.
Open `examples/ArduinoSD/ArduinoSD.ino`. Set `TRACK` and SD pins to match your
project. The default pins are SCK=18, MISO=19, MOSI=23, CS=5 for classic CYD.
The example is a decode benchmark with a discard sink; replace `consume_pcm`
when connecting an output. It was compiled for `esp32:esp32:esp32` using
Arduino-ESP32 3.3.10. You can also use PlatformIO `lib_deps` with the local
library directory. Do not install the full codec-helix library alongside it:
the selected AAC sources are already included.

**ESP-IDF:** copy the directory as `components/miniaac`, or add it to
`EXTRA_COMPONENT_DIRS`. Add `REQUIRES miniaac` to your consuming component.
The standalone SD example is in `examples/esp_idf`; run `idf.py set-target esp32`
then `idf.py build` there. It uses ESP-IDF 5.3+ component names. Its source was
cross-compiled against installed SDK headers; a full standalone IDF project
build is pending. The Arduino benchmark has now run successfully on the actual CYD. SPI3 is used for SD in this example;
coordinate shared buses with your existing display setup.

## Supported files and deliberate bounds

- AAC-LC, mono or stereo, 1,024 samples per channel per AAC frame; indexed
  source rates 8â€“96 kHz. Packets may vary in size, up to **2,048 bytes**.
- Ordinary unfragmented M4A/MP4: one audio track, one `mp4a` description and one
  `mdat` box. `moov` can precede or follow `mdat`. Non-audio tracks are skipped.
- Standard `stsz`, `stsc`, `stco`/`co64`, `stts` tables. Table data stays on SD;
  three fixed 256-byte caches serve entries. Artwork and metadata are skipped.
- 32/64-bit box sizes and chunk offsets. The core has 64-bit read offsets;
  the Arduino `File` example adapter is limited to 32-bit positions, and the
  stdio ESP-IDF example to positive `long` offsets (2 GiB on ESP32).
- Basic single-entry, rate-1 edit lists (versions 0/1). Leading AAC samples are
  decoded and discarded, and the final duration is trimmed. Multiple edits,
  empty edits, and arbitrary timeline seeking are unsupported. M4A media
  timescale must equal source sample rate. A shortened final `stts` frame is
  supported; other nonstandard frame durations are rejected.
- ADTS `.aac`: one raw block per frame, optional CRC words are skipped, not
  verified. Stream rate/channels must remain constant. ID3-wrapped AAC and
  ADIF files are outside this interface. ADTS has no gapless duration metadata,
  so `total_frames` is `UINT64_MAX` and encoder padding remains.
- Standard two-byte LC AudioSpecificConfig, or LC plus an explicitly absent
  SBR sync extension. HE-AAC/SBR (including in-band signalling), PS, encryption,
  fragmented MP4, multichannel, and 960-sample AAC are rejected.

Malformed/truncated containers, out-of-range sample offsets, unsupported
formats, oversized packets and read failures return distinct errors. Sample
data is checked lazily, so an error late in a file may occur after earlier
audio was returned. General-purpose MP4 parsing and whole-file validation
are intentionally outside this component.

## Measured footprint and checks

| Item | Classic ESP32 |
|---|---:|
| Decoder core arena | 19,432 bytes |
| Workspace, including input/parser/cache/core | 22,480 bytes |
| Caller PCM buffer | 4,096 bytes |
| Exact persistent component total | **26,576 bytes** |
| Rounded example allocation | 26,624 bytes |
| Code/literals before linker GC | 20,144 bytes |
| Constants before linker GC | 29,228 bytes |
| Additional library `.data` / `.bss` | 0 / 0 bytes |

Code plus constants total **49,372 bytes before linker garbage collection**;
this is a component-object measurement, not the size of a complete firmware.
Allocator symbol inspection found no `malloc`, `calloc`, `realloc` or `free`
dependencies. Flash lookup tables are `const` and are not copied into RAM.

Compiler stack reports show a largest individual frame of **544 bytes** in
`miniaac_open`, and **112 bytes** for `miniaac_decode_next` itself. Nested
decoder calls, your read callback and the filesystem add to those figures;
they are **not** the full task-stack requirement. Provision 4 KiB initially
for a dedicated producer task, then measure its high-water mark under your
application load. On the tested CYD, three complete SD tracks ran on a 4 KiB
producer stack with at least **1,760 bytes free** (maximum observed use:
**2,336 bytes**, including the read callback and benchmark code). The component
buffers plus that task stack reserve **30 KiB** with the rounded arena, before
RTOS task-control data, filesystem and output-queue memory.

The supplied Madness track decoded in full at 44.1 kHz stereo: **7,434,672
PCM frames**, after edit-list and final-duration trimming. Comparison with
FFmpeg over that audio gives **0.294 LSB RMS error**, maximum 4 LSB, and
**88.10 dB** signal-to-error ratio. FFmpeg retains 592 additional final padding
frames for this file. Desktop decoding speed does not establish ESP32 speed.

Evidence and commands are in `docs/research/validation.md` and the JSON reports.
Run all host checks from a compiler-enabled shell:

```sh
python -m pip install numpy
python tools/check.py --build build
# Optionally add: --sample "/path/to/your/track.m4a"
```

CMake, a C compiler, FFmpeg and FFprobe must be on PATH. MSVC users can run
from a Developer PowerShell. Cross-compile the component and obtain target
sizes/static stack frames without a board:

```sh
python tools/esp32_check.py --toolchain /path/to/xtensa/bin --work build/esp32
```

The regression suite generates its own audio and checks PCM agreement, bounded
RAM, arena reuse, output-buffer retry, and read-error handling. It includes
M4A metadata layouts, mono/stereo, a 20-minute file, `co64`, extended box sizes,
ADTS/CRC, and malformed/unsupported input. Test music and generated PCM are not
included in this source package.

The additional collection test decoded **175/175 M4A tracks successfully**
(10.70 hours of audio). All were AAC-LC, 44.1 kHz stereo. Every track was
compared with FFmpeg: worst SNR was **82.00 dB**, worst RMS difference
**0.4074 LSB**, and largest packet read **1,340 bytes**. The host workspace was
identical for every track. See `docs/research/corpus-summary.json` and
`corpus-report.json` for the summary and per-track results. To test a collection:

```sh
python tests/corpus.py --decoder build/miniaac_decode --root /path/to/music --work build/corpus
```

On Windows use `miniaac_decode.exe` (under `build/Release` with multi-config
generators). The corpus check reuses and then removes two scratch PCM files;
it never writes to the source music directory.

## CYD hardware results

Tested on COM10: classic ESP32-D0WD-V3 rev 3.1, 240 MHz, **no PSRAM**, SD over
SPI at 20 MHz. The read-only benchmark decoded Cassius 1999, Get Lucky, and
Red Alert completely: **726.83 seconds of audio** at **4.45–4.57× real time**.
All three PCM rolling checksums and final frame counts matched the desktop
build. Opening the decoder caused **zero measured heap change** on each track.

The slowest single read/decode call was **32.748 ms**, longer than one 44.1 kHz
AAC frame's 23.22 ms. Start with an **8 KiB PCM output queue** (about 46 ms at
44.1 kHz stereo), then check underruns with your Bluetooth/DAC and display
active. These other subsystems were not exercised in this benchmark; the
measurements establish decoder/SD throughput, not an end-to-end playback test.

Use `examples/ArduinoSDBenchmark/ArduinoSDBenchmark.ino` to repeat the test:
mount SD with the normal classic-CYD pins, open Serial at 115200, then send
`G`. It selects up to three distinct AAC/M4A files, uses a dedicated 4 KiB
producer task, hashes/discards PCM, and prints JSON timing/stack results.
It never writes to SD. See `docs/research/hardware-report.json` for measured
results and `hardware-events.json` for serial events. The original complete
4 MiB flash was restored afterward, its data hash verified, and the original
firmware's serial metrics confirmed to have resumed.

## Source provenance

The AAC core is pinned to codec-helix commit
`f6e55a307c7577c5e3dfe1bf424a61075235fa08`. See `UPSTREAM.md` and
`patches/helix-minimal.patch` for the small local changes. SBR and MP3 sources
are omitted. Upstream licence text and source notices are retained under
`licenses` and in the source headers. The new wrapper/parser, examples and
tools use the MIT licence in `LICENSE`; this does not relicense Helix.
