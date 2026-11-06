# Flash v34 (owner only; confirm the board is on COM7, then BOOT+RESET download mode)

SHA256 of `cydpod-v34-w328c.bin` (4,194,304 B): 3366e983e6ac6d421857cd8a990e6e8f28f7c9896f6ad906bd64a9217c8b13ad

## Download mode and port
1. Plug in the CYD by USB. Hold **BOOT**, tap **RESET**, release **BOOT**. The screen stays blank or frozen.
2. Find the port: `Get-CimInstance Win32_SerialPort | Select DeviceID,Description` (or Device Manager > Ports). Unplug/replug and see which entry appears; it must be COM7. If it is not, change `--port` below. Close any serial monitor first.
3. After flashing, tap **RESET** once to boot (`--after no-reset` does not reboot it).

## Verify the files first
```powershell
cd E:\flash\v34
Get-FileHash .\cydpod-v34-w328c.bin -Algorithm SHA256   # must equal 3366e983e6ac6d421857cd8a990e6e8f28f7c9896f6ad906bd64a9217c8b13ad
Get-FileHash .\app.ino.bin,.\app.ino.bootloader.bin,.\app.ino.partitions.bin,.\boot_app0.bin -Algorithm SHA256   # compare with MANIFEST.json
```

## (a) Single file at 0x0: **ERASES settings** (speaker bond, volume, theme, Wi-Fi, last track). SD card untouched.
```powershell
cd E:\flash\v34
foreach($op in 'write-flash','verify-flash'){.\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 cydpod-v34-w328c.bin}
```

## (b) Four regions: keeps saved settings
```powershell
cd E:\flash\v34
foreach($op in 'write-flash','verify-flash'){.\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin}
```
Each run prints `Hash of data verified` for write and `Verify OK` for verify.
