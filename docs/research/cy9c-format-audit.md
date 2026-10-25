# CY-9c: probe format audit and compile regression

2026-10-25. Compile and host evidence only; no device, serial, esptool or SD access.

## Cause and correction

CY-9a decoded the panic to `strlen` through `Print::printf` in `metrics()`.
The `METRIC` format has 16 conversions: one string, then 15 unsigned numbers.
Its argument list omitted `m4aState`, shifting the heap integer into `%s` and
leaving the final conversion without an argument. Restored that first argument;
all numeric arguments now have explicit `unsigned` type (including promoted
boolean loads), or already return `unsigned` (`targetLevel.load()`).

## Every format call in the probe

Audit covers all of `cydpod_probe.ino`, `adts_decoder.h`, and `audio_core.h`.
There are eight `Serial.printf` calls and one `snprintf`; the two headers have
no format calls. `Serial.println` takes literal text and is not variadic formatting.

| Location / prefix | Conversions and arguments | Verdict |
| --- | --- | --- |
| `logTrack`: `TRACK` | `%s` ev, track, m4aState; four `%u` heap/free-block/minimum and minLargest | OK after explicit unsigned normalization of size/atomic values; original order correct |
| `listDirectory`: `snprintf` | `0:%s`, path is `const char*`; bounded destination | OK |
| `listDirectory`: `LS name` | `%.96s` entry.fname array; `%llu` explicitly `unsigned long long(entry.fsize)`; `%s` literal kind | OK; existing 64-bit cast retained |
| `listDirectory`: `LS done` | `%u` unsigned count; `%u` unsigned(truncated) | OK |
| `producerTask`: `OUTPUT ready` | `%u` unsigned(output) | OK |
| `producerTask`: `ERR m4a` | `%s` m4aState is `const char*` | OK |
| `producerTask`: `ERR ... helix` | `%s` resultName returns `const char*`; `%d` lastError is int | OK |
| `metrics`: `METRIC` | String plus 15 unsigned values, detailed below | FIXED missing string argument; normalized numeric types |
| `setup`: banner | `%s` getChipModel returns `const char*`; `%u` unsigned(getPsramSize()) | OK after explicit unsigned normalization |

`logTrack` callers supply literal `skip`, `start`, `skip`, and `end`; its
argument is data passed to `%s`, never a format string. No `%u` call consumes
a 64-bit value and no `%d` call consumes unsigned. No other printf-family or
variadic format calls occur in these files.

| Position | Field | Conversion | Argument type after correction |
| ---: | --- | --- | --- |
| 1 | m4a | `%s` | const char* m4aState |
| 2 | heap | `%u` | unsigned(free internal heap size) |
| 3 | min_heap | `%u` | unsigned(minimum internal heap size) |
| 4 | largest | `%u` | unsigned(largest uint32_t) |
| 5 | min_sampled_largest | `%u` | unsigned(minLargest.load()) |
| 6 | producer_stack | `%u` | unsigned(task high-water mark or zero) |
| 7 | sink_stack | `%u` | unsigned(task high-water mark or zero) |
| 8 | loop_stack | `%u` | unsigned(current task high-water mark) |
| 9 | ring | `%u` | unsigned(pcmRing.size()) |
| 10 | decoded | `%u` | unsigned(decodedFrames.load()) |
| 11 | underrun_frames | `%u` | unsigned(underrunFrames.load()) |
| 12 | underrun_calls | `%u` | unsigned(underrunCalls.load()) |
| 13 | queue_rejected | `%u` | unsigned(queueRejected.load()) |
| 14 | connected | `%u` | unsigned(connected.load()) |
| 15 | muted | `%u` | unsigned(muted.load()) |
| 16 | level | `%u` | unsigned targetLevel.load() |

These counters/sizes are bounded by the original ESP32's 32-bit address space;
casts preserve their values. The listing's file size deliberately retains 64 bits.

## Build enforcement

Pinned core `.deps/arduino/packages/esp32/hardware/esp32/3.3.10/cores/esp32/Print.h:71`
declares `size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3)));`.
The implicit member `this` accounts for index 1; no local wrapper is needed.

Each probe file enables `#pragma GCC diagnostic error "-Wformat"` and
`"-Wformat-security"` immediately after dependency includes, inside a diagnostic
push/pop. Headers restore their caller's state. GCC guards preserve MSVC host
builds. Dependencies and `lib/`/`vendor/` are unchanged.

The initial staging-only negative experiment unexpectedly compiled: the existing
`--warnings none` adds GCC `-w`, which suppresses even diagnostic-error pragmas.
`tools/check.ps1` now selects `--warnings default` only for `aac-probe` (the pinned
platform maps it to no warning flag). Player and uploader keep `--warnings none`.
There are no global format-error flags; enforcement is confined to probe source.

## Evidence

The staging-only negative regression removed only the first `m4aState` argument
from the corrected `metrics()` call, compiled with the normal probe options
(`--warnings default`, locked FQBN, LC-only and M4A include flags), and restored
the staging source in a `finally` block. The repository source was never broken.
Local runner: `powershell -NoProfile -File .tools/cy9c-negative.ps1`.
Full command, compiler output and restoration record:
`.tools/logs/cy9c-negative-format.txt` (compile exit **1**):

```text
error: format '%s' expects argument of type 'char*', but argument 3 has type 'unsigned int' [-Werror=format=]
error: format '%u' expects a matching 'unsigned int' argument [-Werror=format=]
compile_exit=1
Staging source restored to fixed version.
```

Positive command, repeated after restoring staging:
`powershell -NoProfile -File tools/check.ps1` (exit **0**).
Output retained in `.tools/logs/cy9c-check.txt`; probe details in
`.tools/logs/aac-probe.log`, metadata in `.tools/logs/build-results.json`:

```text
Sketch uses 1222738 bytes (62%) of program storage space. Maximum is 1966080 bytes.
Global variables use 61528 bytes (18%) of dynamic memory, leaving 266152 bytes for local variables. Maximum is 327680 bytes.
PASS synthetic fixture matrix: 3 positive, 7 negative
PASS: compile-only checks completed; no device commands were run.
```

| Probe | Image bytes | Static RAM bytes | SHA256 |
| --- | ---: | ---: | --- |
| Previous CY-8b / CY-9a | 1,222,880 | 61,528 | `9a57cb67d323030de1e4e796c0b9ff440f06981393a66ad320de258d05154b4c` |
| Corrected CY-9c | 1,222,880 | 61,528 | `480441396eb43594b6a0a25760a081b81080cd3c803a11a172de961b7037a79b` |

Image and static RAM deltas are zero; linker code size increases by 8 bytes.
Player/uploader image sizes and SHA256 values remain unchanged.

All four explicit commands `wsl.exe -d Debian -- sh tests/build_m4a_host.sh`,
`build_m4a_caps.sh`, `build_m4a_reject.sh`, and `build_m4a_sequence.sh` exited **0**.
Their retained output `.tools/logs/cy9c-host-tests.txt` reports identical PCM/counts,
malformed cases within caps, all 105 rejection runs, and every sequence track equal
to its fresh-reader result. An initial shell-loop quoting error is retained at
the beginning of that log; the corrected run invoked each script explicitly.

`powershell -NoProfile -File tools/cyd_probe_session.ps1 -Port COM10
-Script tools/cyd_probe_session.sample.txt -LogPath
.tools/logs/cy9c-dryrun-session.txt -DryRun` exited **0**, scheduled listing at
2 seconds and final stop at 362 seconds, and created no session log or serial
object. Console evidence: `.tools/logs/cy9c-dryrun.txt`. The COM10 string is solely
a dry-run input; no port was opened. A fresh hardware smoke test remains required.
