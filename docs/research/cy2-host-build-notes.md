# CY-2 host build notes: M4AAudioDemuxer on MSVC

Result: **blocked**. The unmodified vendored `M4AAudioDemuxer` (arduino-audio-tools at the CY-1 pin) cannot be compiled with MSVC (VS 18 BuildTools, x86) using a small shim. No test code was committed.

## What was tried
`tests/m4a_host_test.cpp` (demux in 1 KiB writes, callback collects ADTS, decode with `AdtsDecoder`, compare with the `.aac` twin) plus `tests/build_m4a_host.cmd` using `/I vendor/arduino-audio-tools/src`, a force-included Arduino shim (`Print`, `Stream`, `File`, `Serial`, `delay`, `PSTR`) and a stub `AudioTools/CoreAudio/AudioBasic/Str.h` placed ahead on the include path. The shim approach worked for everything except the items below.

## Blockers (vendor code, MSVC)
1. `AudioTools/CoreAudio/AudioBasic/Str.h` (included by `Buffers.h`) uses a variable-length array (`char result[new_size + 1]`, `urlEncode`): C2131. A stub shadowing the header avoids it, but `Buffers.h:1221` then needs `Str`, and `MP4Parser.h` needs `StrView` (`MP4Parser.h:268,654,666`). Stubbing `Str` while keeping `StrView` is possible (small).
2. `AudioTools/AudioCodecs/MP4Parser.h:282` `defaultCallback` declares `char space[box.level * 2 + 1];` (VLA) in an inline static member: C2131/C3863. This is in the file that does the parsing; it cannot be shadowed with a shim without copying the parser, and MSVC has no VLA extension or flag.

No clang, gcc or zig is installed (`wsl.exe` exists, distros/toolchains not checked).

## Proposed split (needs a decision)
- CY-2a: host-compile with a VLA-capable compiler (clang-cl via the VS "C++ Clang tools" component, or g++ in WSL), which needs only the Arduino shim; the test and compare logic are already designed as above.
- or CY-2b: MSVC build over a build-time patched copy of `MP4Parser.h`/`Str.h` in `tests/out/` (replace the two VLAs); this is a modified copy of vendor code and needs explicit approval.
- Do not edit `vendor/` in place.
- Pairing: `stereo-faststart.m4a` is the 192 kbit/s synthetic AAC, so the twin is `stereo-192.aac` (131 frames, 134144 samples per channel). `verification.json` does not claim the twin is bit-identical to the M4A payload (M4A is 73740 B vs 73337 B ADTS); the PCM comparison would show it.
