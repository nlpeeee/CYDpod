# CY-17: LC PCM output bound

2026-10-25, compile and WSL Debian host tests only; no device access. The only firmware change is the decoder output capacity and compile-time guards. Ring, stacks, reader, admission constants and playback margin are unchanged.

## Bound before the post-decode check

Paths below are relative to `.deps/sketchbook/libraries/arduino-libhelix/src/` and refer to the pinned local dependency, not a current upstream version.

1. `firmware/cydpod_probe/audio_core.h:60` validates ADTS sync/layer, LC profile, rate index 4 (44,100 Hz), channel configuration 2, and exactly one raw data block. `adts_decoder.h:46-48` applies it before reading the payload or calling `AACDecode`; the encoded frame is also capped at 2,048 bytes. ADIF, raw blocks and channel configuration 0 cannot enter this decoder.
2. `ConfigHelix.h:64-67` enables `HELIX_FEATURE_AUDIO_CODEC_AAC_SBR` only without `CYDPOD_AAC_LC_ONLY`. `libhelix-aac/aaccommon.h:51-56` derives `AAC_ENABLE_SBR` from that feature. `tools/check.ps1:86` selects LC-only and `118-119` applies flags to both C and C++ compilation. The new header guard requires LC-only and rejects the SBR feature; the static assertions require two maximum channels, 1,024 samples/channel and 16-bit shorts. Pinned config integrity is checked by `tools/check.ps1:54-55`. No SBR upsampling or PS output path is compiled.
3. `libhelix-aac/aacdec.h:93-97` defines `AAC_MAX_NCHANS=2`, `AAC_MAX_NSAMPS=1024`. `filefmt.c:115-140` validates LC, maps header configuration 2 to two channels (`aactabs.c:60`), copies the count to decoder state and resets `sbrEnabled=0`. The implicit mapping path (`aacdec.c:336-343`, `filefmt.c:172-202`) is unreachable because configuration 0 was rejected, including on later frames: this object never receives another header configuration or uses the raw-block API.
4. `aacdec.c:359-360` rejects an invalid total channel count before the element loop. Crucially, `aacdec.c:380-382` rejects **baseChan + elementChans > AAC_MAX_NCHANS before noiseless decode, dequantization or IMDCT for that element**. `baseChan` starts at zero (367), only increases by each element's channel count (457), and the table (`aactabs.c:67`) has nonnegative counts. Thus each output call has `chOut=baseChan+ch` in 0..1, even if the bitstream contains more channels than its header claims. Earlier valid elements may already have written within the buffer before a later excess element is rejected.
5. `dequant.c:294-350` only transforms coefficients; PCM output is through `IMDCT` (`aacdec.c:428`). `imdct.c:535` offsets the output pointer by `chOut`; its LC branch (`570-579`) selects a fixed window routine. Long/start/stop routines (`76-143`, `163-231`, `250-318`) cover 1,024 output positions per channel; the short routine (`337-453`) covers 448 + 128 + 3*128 + 64 = 1,024 positions (the rest updates overlap only). Each uses stride `nChans=2`. The maximum short index written is therefore **1 + (1024-1)*2 = 2047**, including error-return frames. `AACGetLastFrameInfo` (`aacdec.c:187`) reports 2*1024=2048; the wrapper checks that same named capacity after decode. That check alone would have been too late as a memory guard; the element guard above supplies the prior bound.

A PCE is parsed into `psi->pce` (`decelmnt.c:407-409`, parser at 218-282); it does **not** replace `aacDecInfo->nChans` or `psi->nChans` in this ADTS path. PCE itself has zero output channels. A six-channel PCE followed by a stereo CPE remains a two-channel output; additional audio elements are rejected by the same accumulated-channel guard. An SCE instead of a CPE can leave one output lane unwritten, but cannot enlarge output. This preserves the existing acceptance behavior; no new requirement for a particular first element is necessary. Malformed syntax can return earlier errors, never bypass the output-channel guard. This is a PCM output bound, not a general security audit of Helix's bitstream parser.

## Reproducible host evidence

`wsl.exe -d Debian -- sh tests/build_pcm_bound.sh` builds every Helix C source with the same LC-only flag, then runs exact 2,048-short outputs followed immediately by sixteen canary words. It covers three accepted AAC fixtures, the guarded faststart M4A fixture and every frame of all 25 real files, read-only in the authorized folder. It also synthesizes a stereo header with two CPEs (four channels), three SCEs, and a PCE declaring six channels followed by one CPE: the excess elements return `ERR_AAC_NCHANS_TOO_HIGH`, the PCE case decodes, and all canaries survive. Mono/implicit/3/6/7-channel, Main, 48 kHz and multi-block headers stop after seven input bytes with `unsupported`, no codec call and unchanged PCM.

Before modification the three fixtures each produced 131 frames. Their full PCM SHA256s were captured from the 4,096-short decoder and are checked in the script for both plain and sanitized builds. All are byte-identical. Reject fixtures retain `unsupported` (48k/Main/mono), `malformed` (invalid header), and `truncated` after 130 frames; executable exit code 4 is asserted. The existing M4A host suite reports `pcm bytes IDENTICAL`; audio-core's ring/concurrency/header/gain/command tests pass.

Sanitizer scope is explicit: an initial full `-fsanitize=address,undefined` run completes with intact canaries but reports dependency signed-shift UB in Helix fixed-point arithmetic (`bitstream.c`, `huffman.c`, `assembly.h`, `dct4.c`, `stproc.c`, `tns.c`). Retained log: `.tools/logs/cy17-pcm-full-ubsan.txt`. The repeatable default excludes **only shift UBSan in Helix C objects**; test/wrapper/reader retain full ASan/UBSan, all other codec UBSan and ASan remain enabled, and undefined errors halt execution. Set `CY17_FULL_UBSAN=1` to reproduce the full dependency diagnostics. This chunk does not claim the unmodified codec is fully UB-clean.

Final runner exit 0 (`.tools/logs/cy17-pcm.txt`), both modes:

```text
PASS PCM canaries: 26 M4A files (25 real + 1 accepted fixture), 269675 decoded frames; header and element mismatch checks
stereo-128.aac.pcm: OK
stereo-192.aac.pcm: OK
stereo-256.aac.pcm: OK
PASS ring capacity/wrap/concurrency, ADTS rejection, gain, saved levels, command overflow
```

Session runners `cy11c_session.ps1` and `cyd_probe_session.ps1` pass `-DryRun` with `-Port COM999 -Command status -LogPath .tools/logs/cy17-unused.txt` in Windows PowerShell 5.1 and `pwsh`; all four print `no port opened` (`.tools/logs/cy17-dryruns.txt`). The placeholder port is never opened or enumerated.

All nine existing suites pass with exit 0: `tests/build_m4a_{host,caps,reject,sequence,r1r5,filetable,real,hook,arena}.sh` (combined output `.tools/logs/cy17-suites.txt`). Real-media inventory: `PASS accepted=25 rejected=0 expected=25 mismatches=0`; M4A comparison: `pcm bytes IDENTICAL`; arena runs both plain and ASan/UBSan, with its default 1,500 randomized streams, followed by caps/reject in arena mode. The unaffected allocation-failure sweep was skipped as requested.

## Build and admission

`powershell -File tools/check.ps1` passes all three builds, inventories and host media checks (output `.tools/logs/cy17-check.txt`). Probe binary: **1,226,960 B**, SHA256 **`5b2b0b555020b6377c3aff0ee9b57e5734ad2a463b2a382f7d231de9d0b4697e`**, static RAM **57,464 B**. Prior probe: 1,226,960 B, `8da289bc871781f3ac4bebee37fea63a0dbc175d0229b87172e942b880690071`, 61,560 B RAM. Image size is unchanged; static RAM and the global decoder's `.bss` decrease by exactly **4,096 B**.

Xtensa ELF inspection (`xtensa-esp32-elf-size -A`, `xtensa-esp32-elf-nm -S -C`): `.dram0.bss=34344`, `decoder` symbol size `0x182c` = 6,188 B, versus its previous layout's 10,284 B. The buffer's 2,048-short reduction is exactly 4,096 B; the added constant/assertions have no resident storage.

Predicted post-open free heap is 45,488 + 4,096 = **49,584 B**, against unchanged `need_free=47,840`: **1,744 B slack** beyond the 2,048 B playback margin. The boot saving enlarges the first free region. Its effect on the largest block **after Bluetooth and file open is unmeasured**, since that heap is fragmented: best case 45,044 + 4,096 = **49,140 B**, giving **3,468 B slack** against `need_largest=45,672`; conservative worst case remains **45,044 B**, **628 B short**, so the admission guard still rejects. Host and compile checks do not establish hardware admission or playback.
