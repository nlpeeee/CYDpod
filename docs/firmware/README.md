# CYDpod 0.5 firmware assets

Download `CYDpod-0.5.0-firmware.zip` from the [0.5 release](https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.0). It contains Bluetooth and wired player builds, manifests, instructions and esptool. Each build has four regions at `0x1000`, `0x8000`, `0xe000` and `0x10000`. Follow the archive README to flash and verify them.

The source is tagged `v0.5.0`. Firmware provenance is recorded in the manifests and [release notes](../releases/0.5.0.md). GitHub Releases holds the binary assets. The local release copy is `E:\dev\CYDpod\releases\0.5.0`.

The older Pages generator in `tools/make_site_release.ps1` describes the historical merged-image delivery. Use the four-region 0.5 release instructions for this release.
