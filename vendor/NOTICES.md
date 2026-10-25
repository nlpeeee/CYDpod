# Pinned third-party source and build dependencies

`CYD-MP3-Player/` is an unmodified source snapshot of
[Garionhk/CYD-MP3-Player](https://github.com/Garionhk/CYD-MP3-Player), commit
`404332e93974f4f70392b613c9f3dfb25e1b89b8` (upstream version 1.2.0).
Archive SHA256: `b5e37def64bf1b71d243b8d270908c65343439bec03e5da08656db8f1cd7550a`.
The snapshot contains no nested Git metadata. Original notices and licenses are
retained, including `LICENSE-2.0.txt` and BitBank's AnimatedGIF Apache-2.0
license in each vendored copy. AnimatedGIF was already modified by upstream
(width limit / include filename); CYDpod does not replace it with a new version.
`tools/upstream-files.sha256.json` inventories every original archive file.

The full reproducibility lock is `../dependencies.lock.json`; package-index
extracts in `../tools/package*_index.json` preserve the official download URLs,
versions and SHA256 values of every Windows core/tool artifact. Archives and
installed tools are local ignored build inputs, not part of this source import.

| Component | Version/provenance | Notices and scope |
| --- | --- | --- |
| CYD-MP3-Player | Commit above | Apache-2.0; separate bundled notices retained |
| AnimatedGIF | Exact files in pinned shell | BitBank Software Apache-2.0, retained in source |
| ESP32-A2DP | 1.8.11, `b6da4744286ca15b6f1dee607c077a4747294dd4` | Library Apache-2.0; examples can have GPL headers; stock build uses library sources |
| arduino-libhelix | 0.9.4, `6183305de92c6c8e445aa95116319b64963a6ad3` | GPLv3 wrapper and Helix codec RPSL/RCSL terms; retain codec file headers |
| TFT_eSPI | Arduino library archive 2.5.43 | FreeBSD/BSD two-clause text, MIT ancestry, individual font notices retained |
| arduino-audio-tools | 1.2.6, `e42af0513a2273730431e8c45ac17a5fd9f38487`, archive SHA256 `1187a17837211722f4996dda53579f018e19cd4a5fab74eec383b74b1ea8c7fe` | GPLv3 (`notices/AudioTools-GPL-3.0.txt`); unmodified `src/`, `library.properties`, `License.txt` in `arduino-audio-tools/`; not wired into any build; per-file hashes in `tools/audiotools-files.sha256.json` |
| MiniAAC (MinimalAAC) | 1.0.0 hardware-tested archive, SHA256 `deead1c51dfbcf9c9e4675a20aa9dbec3cc2e016841ab5541b7be6cb510e1540`; Helix core from codec-helix `f6e55a307c7577c5e3dfe1bf424a61075235fa08` with the local changes in `miniaac/UPSTREAM.md` and `miniaac/patches/helix-minimal.patch` | Wrapper, M4A/ADTS parser, examples and tools: MIT (`notices/MiniAAC-LICENSE-MIT.txt`). Helix AAC core: codec-helix GPL-3.0 text (`notices/MiniAAC-codec-helix-GPL-3.0.txt`) with RealNetworks RPSL/RCSL notices retained in every source header; MIT does not relicense Helix and the combined probe binary is not labelled MIT. Imported unmodified; per-file hashes in `tools/miniaac-files.sha256.json`; compiled into the probe as an Arduino `--library` (CY-31) |
| Arduino-ESP32 | Core archive 3.3.10 | Package declares LGPL-2.1-or-later; individual files and bundled components have other terms |
| ESP-IDF binary SDK | 5.5.4, `735507283d`, ESP32 libs archive 3.3.10 | Espressif/third-party SDK notices apply; archive hash pinned; not represented as wholly LGPL |
| Xtensa/RISC-V toolchains | esp-x32/esp-rv32 2601, GCC 14.2.0, build 20260121 | Toolchain GPL and runtime/component exceptions/notices retained in installed packages |
| Arduino CLI | 1.5.1, executable commit `01f3d4f2b` | GPLv3; license copied to `notices/` |
| Package tools | Exact lock/index versions (esptool, GDB, OpenOCD, filesystem tools, dfu-util, Arduino builtins) | Separate upstream licenses in installed packages; pinning is not a relicensing claim |

Copies of the principal library and Helix license texts are in `notices/` so
they remain available without the ignored dependency directories. The codec
source retains its RealNetworks copyright and license headers. This inventory
does not label the combined firmware Apache/MIT, and is not a redistribution
clearance or an exhaustive SDK/toolchain legal audit.

Local build configuration changes are explicit: `tools/tft/User_Setup.h` copies
the upstream ST7789 template into the isolated TFT library; the Helix
`ConfigHelix.h` patch adds the opt-in `CYDPOD_AAC_LC_ONLY` guard to disable SBR
for the AAC probe while preserving stock behavior when the flag is absent.
`tools/bootstrap.py` contains the exact replacement and the lock records its
resulting file hash. No other third-party source changes are intended.

No generated CJK font or user media is supplied. Hardware compatibility,
AAC patents, codec licensing obligations for a distribution, and the actual
AirPods/DAC requirements remain separate from compile-only reproduction.

## Maintained player copy (CY-33)

`firmware/cydpod_player/app/` is a modified copy of the `vendor/CYD-MP3-Player/app` sources (Garionhk/CYD-MP3-Player, Apache-2.0; `firmware/cydpod_player/LICENSE-2.0.txt` and the bundled `app/src/AnimatedGIF/LICENSE` are kept). The pristine snapshot in `vendor/` is untouched. The changes (MiniAAC M4A decoder instead of libhelix MP3, `.m4a` library scan, no seeking) are listed in `firmware/cydpod_player/CYDPOD-CHANGES.md`. It is built with MiniAAC as an Arduino `--library` (same notices as the probe row above), so the same GPL-3.0/Helix notices apply to its binary.
