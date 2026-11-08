# CYDpod 0.5.5: flashing and building

## Flash the release

1. Download `CYDpod-0.5.5-firmware.zip` from the [0.5.5 release](https://github.com/nlpeeee/CYDpod/releases/tag/v0.5.5) and unzip it. It contains the Bluetooth and wired player builds, their manifests and esptool 5.3.0.
2. Close any serial monitor, and identify the board's COM port by unplugging it and plugging it back in.
3. Hold **BOOT**, tap **RESET**, then release **BOOT**.
4. From the unzipped folder, run the block below in PowerShell. It checks every file's hash, then writes and verifies the four flash regions (`0x1000`, `0x8000`, `0xe000`, `0x10000`). Saved settings and the Bluetooth bond survive.

```powershell
Set-Location .\firmware\bluetooth
$manifest = Get-Content .\MANIFEST.json -Raw | ConvertFrom-Json
foreach ($image in $manifest.files) {
    if ((Get-FileHash -LiteralPath $image.file -Algorithm SHA256).Hash -ne $image.sha256) {
        throw "Hash mismatch for $($image.file)"
    }
}
$port = Read-Host 'COM port for the JC2432W328C'
foreach ($op in 'write-flash','verify-flash') {
    & ..\..\flash-tools\esptool.exe --chip esp32 --port $port --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin
    if ($LASTEXITCODE -ne 0) { throw "esptool $op failed" }
}
```

5. Tap **RESET**.
   - Start with the speaker turned down: Bluetooth volume follows the speaker's own scale.
   - The serial log runs at 115200 baud.
   - For the wired build, use `firmware\wired` instead.

## Build from source

You'll need Windows x64, Git, Python 3.9 or later and Windows PowerShell 5.1.

1. Download `CYDpod-0.5.5-build-kit.zip` from the release and unzip it outside the checkout. It contains the pinned toolchain and `Restore-Toolchain.ps1`, so no other downloads are needed.
2. Restore it into a fresh clone, then build:

```powershell
git clone --branch v0.5.5 https://github.com/nlpeeee/CYDpod.git E:\dev\CYDpod
powershell -NoProfile -File E:\dev\CYDpod-build-kit\Restore-Toolchain.ps1 -Source E:\dev\CYDpod
Set-Location E:\dev\CYDpod
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144 -BuildLabel release-055
```

Images and manifests are written to `.tools/images`. Rebuilt application hashes can differ from the released binaries, so flash those when you need an exact match. Dependency pins and third-party inventories are in [dependencies.lock.json](../../dependencies.lock.json).

## Provenance and limits

- **Provenance:** the source is tagged `v0.5.5`. The manifests and the [release notes](../releases/0.5.5.md) record the firmware's provenance, the checks, and what is not yet confirmed on hardware. GitHub Releases holds the binary assets.
- **Legacy tooling:** the older Pages generator in `tools/make_site_release.ps1` describes the historical merged-image delivery. Use the four-region instructions above.
