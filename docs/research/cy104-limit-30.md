# CY-104: -30 dBFS / v29

`wired_fade.h`: `kLimitedCeilingQ15=1036` (-30.0018 dBFS), was 4125. Off=16423 (-6 dBFS); BT, curve, writer slew (1 Q15/frame), `hpLimit` and label unchanged. Slew: 15387 frames/348.91 ms; scroll unchanged. W328C static RAM 93464 B (v28 +0).

Full-scale PCM via Fade, sleep gain 100:

|Slider|10|25|50|75|100|
|---|---:|---:|---:|---:|---:|
|Peak dBFS|-70.309|-54.051|-42.043|-34.996|-30.002|

Requirement gap: 0/1/2 output zero; 3 outputs 1 LSB (-90.309 dBFS). Steps 3..100 are distinct. Coordinator retained curve; knee deferred.

Checks on source commit 4b92c9e (report added afterward), exits 0:
- `sh .tools/cy104-host.sh`: `WIRED_FADE_OK`, `WIRED_PUMP_OK`, 3x `SETTINGS_ROUTE_OK`, `PASS CY-92 UI` (focused).
- `tools/check.ps1 -SkipProbe -SkipHostMedia`, default then `-Board jc2432w328c`: both `PASS: compile-only checks completed`.
- `PASS V29_EXPORT`: 7 hashes/sizes, 4 offsets. Logs `.tools/logs/cy104-*`.

Image `.tools/images/codec-pool-v29-jc2432w328c`; SHA256 by offset (full names in MANIFEST.json):
```
0x10000 700ea010591e17a3fa30c3d13f9e2fb5281d184500209d83002a700982868271
0x1000 f508dfe30f34c2490ec08caaa96f20dc2853f66a0a92f6fb759b205e82924f29
0x8000 0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7
0xe000 f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0
```
Owner only: identify COM7, BOOT+RESET download mode:
```powershell
cd E:\dev\.worktrees\CYDpod\cy98-viewport
$img='.tools/images/codec-pool-v29-jc2432w328c'
$tool='.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
foreach($op in @('write-flash','verify-flash')){
& $tool --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 "$img/app.ino.bootloader.bin" 0x8000 "$img/app.ino.partitions.bin" 0xe000 "$img/boot_app0.bin" 0x10000 "$img/app.ino.bin"
}
```
RESET; retest loudness/low steps, toggle, reboot On/Off, BT, pause/seek/transitions. No device operations.