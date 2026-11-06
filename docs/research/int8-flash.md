# Flash CYDpod v36 — JC2432W328C (owner only)

Flash **cydpod-v36-w328c.bin first (compact decoder)**. **cydpod-v36-legacy-w328c.bin is for the decoder A/B comparison**: the old decoder and 40,960-byte pool, with the same diagnostics and other changes. Both are 4,194,304-byte images; compact uses a 30,824-byte pool. No hardware test is claimed.

## Prepare and verify
1. Close serial monitors. Plug in the CYD using a USB data cable. Find its port by unplugging/replugging and comparing `Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Description` (or Device Manager > Ports). Commands use **COM7**; change every `--port COM7` if the identified CYD uses another port.
2. Enter download mode: hold **BOOT**, tap **RESET**, then release **BOOT**. Screen may stay blank/frozen. Repeat before a retry if necessary.
3. Verify these SHA256 values before flashing; do not proceed on a mismatch:

```powershell
cd E:\flash\v36
Get-FileHash .\cydpod-v36-w328c.bin -Algorithm SHA256
# Must equal 3d5e580711973e11e451b56cddb6707411c75c7919f7d9cb67cc184fc752f4e2
Get-FileHash .\cydpod-v36-legacy-w328c.bin -Algorithm SHA256
# Must equal ff553f22ae5d1bbd8471a313ce1343eec763539e9c2e3f559d163bb39484335a
Get-FileHash .\app.ino.bootloader.bin,.\app.ino.partitions.bin,.\boot_app0.bin,.\app.ino.bin -Algorithm SHA256
# Compare each with files[].sha256 in .\MANIFEST.json
Get-FileHash .\legacy\app.ino.bootloader.bin,.\legacy\app.ino.partitions.bin,.\legacy\boot_app0.bin,.\legacy\app.ino.bin -Algorithm SHA256
# Compare each with files[].sha256 in .\legacy\MANIFEST.json
```

## (a) Single file at 0x0 — ERASES saved settings
**Erases speaker bond, volume, theme, Wi-Fi and last track. SD card is untouched.** Use one block only. Each block writes, then verifies; on error stop and re-enter download mode. Tap **RESET** after success to boot (`--after no-reset` leaves the chip in download mode).

v36 (try first):
```powershell
cd E:\flash\v36
foreach ($op in 'write-flash','verify-flash') {
  .\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 cydpod-v36-w328c.bin
  if ($LASTEXITCODE -ne 0) { throw "esptool $op failed; stop and re-enter download mode" }
}
```

v36-legacy (A/B comparison):
```powershell
cd E:\flash\v36
foreach ($op in 'write-flash','verify-flash') {
  .\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 cydpod-v36-legacy-w328c.bin
  if ($LASTEXITCODE -ne 0) { throw "esptool $op failed; stop and re-enter download mode" }
}
```

## (b) Four-region flash — keeps saved settings
Use one block only and keep all four files from the same variant together. These offsets avoid saved NVS settings. Tap **RESET** after success.

v36:
```powershell
cd E:\flash\v36
foreach ($op in 'write-flash','verify-flash') {
  .\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin
  if ($LASTEXITCODE -ne 0) { throw "esptool $op failed; stop and re-enter download mode" }
}
```

v36-legacy (A/B comparison):
```powershell
cd E:\flash\v36
foreach ($op in 'write-flash','verify-flash') {
  .\esptool.exe --chip esp32 --port COM7 --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 legacy/app.ino.bootloader.bin 0x8000 legacy/app.ino.partitions.bin 0xe000 legacy/boot_app0.bin 0x10000 legacy/app.ino.bin
  if ($LASTEXITCODE -ne 0) { throw "esptool $op failed; stop and re-enter download mode" }
}
```

Expected messages: `Hash of data verified` during write and `Verify OK` during verify. Keep the output if either operation fails. After boot, follow CAPTURE.md and CHECKLIST.md. Record the flashed filename manually: the boot banner does not identify the decoder. Compact rejects blocks above 4096 samples; legacy retains the old decoder limit. This build includes diagnostics, playlist Back retention and an unwired stream policy; no stream takeover recovery or core-0 UI move is included.
