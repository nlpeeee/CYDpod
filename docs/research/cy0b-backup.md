# CY-0b stock CYD flash backup

Date: 2026-10-25. PASS: two independent full 4 MB reads completed with identical SHA256; the temporary second image was deleted after comparison.

Scope: user-confirmed CYD on COM10 only, pinned esptool 5.3.0, two read-only 4 MB flash reads. No write, erase, eFuse command, serial-monitor session, other port, or SD access is authorised in this chunk. Esptool uploads a RAM stub and resets the ESP32; this does not write flash.

Branch preparation succeeded: `git switch -c cydpod/cy-9 cydpod/cy-8b`, then `git merge --no-ff --no-edit cydpod/cy-0a`, producing merge `d54c238`. The pre-existing modification to `docs/research/build-baseline.md` is preserved and excluded from staging.

The existing build record matches the CY-8b probe: 1,222,880 bytes, SHA256 `9a57cb67d323030de1e4e796c0b9ff440f06981393a66ad320de258d05154b4c`. Read syntax was checked using local esptool `read-flash --help`; v5.3.0 supports the plan's hyphenated syntax. Baud 460800 is used for the initial reads, with at most one lower-baud retry at 115200 if needed; `--no-progress` suppresses progress only. The backup name follows this chunk's dated convention.

Both flash reads exited 0 without retries, with normal hard reset afterwards. Raw logs, parser and check output are retained locally under `.tools/backup/`.

## Offline inventory

The partition table at `0x8000` has a matching MD5 and all partitions fit the 4 MB image. Type `0x00` is app; type `0x01` is data. No encrypted partition flags are set.

| Name | Type | Subtype | Offset | Size |
| --- | --- | --- | --- | --- |
| nvs | data (0x01) | NVS (0x02) | 0x9000 | 0x5000 |
| otadata | data (0x01) | OTA selection (0x00) | 0xe000 | 0x2000 |
| app0 | app (0x00) | OTA slot 0 (0x10) | 0x10000 | 0x140000 |
| app1 | app (0x00) | OTA slot 1 (0x11) | 0x150000 | 0x140000 |
| spiffs | data (0x01) | SPIFFS (0x82) | 0x290000 | 0x170000 |

The first app at `0x10000` has image magic `0xe9`, six segments, DIO mode (`0x02`), flash size/frequency byte `0x2f` (4 MB / 80 MHz), entry `0x4008216c`, ESP32 chip ID 0 and an appended image hash. Its descriptor identifies `esp32-arduino-lib-builder`, version `9036908-dirty`, built Mar 26 2021 at 06:09:28, IDF `v3.3.5-1-g85c43024c`. These header facts do not independently establish the GUI identity or verify playback.

Read-only parser command: `python .tools/backup/cy0b-inventory.py`; this opens the saved backup only, never a port. Full parser retained here for reproducibility:

```python
from pathlib import Path
import hashlib, struct
p = Path(r'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin')
b = p.read_bytes()
print(f'backup={p}\nsize={len(b)}\nsha256={hashlib.sha256(b).hexdigest()}')
apps = []
print('name,type,subtype,offset,size,flags')
for pos in range(0x8000, 0x8c00, 32):
    row = b[pos:pos+32]
    if row == b'\xff'*32:
        print(f'partition terminator at 0x{pos:x}')
        break
    if row[:2] == b'\xeb\xeb':
        actual = hashlib.md5(b[0x8000:pos]).digest()
        print(f'partition MD5 stored={row[16:].hex()} calculated={actual.hex()} match={actual == row[16:]}')
        assert actual == row[16:]
        continue
    magic, typ, sub, off, size, name, flags = struct.unpack('<HBBII16sI', row)
    assert magic == 0x50aa, f'Bad partition magic at {pos:x}'
    print(f'{name.split(bytes([0]))[0].decode()},{typ:#04x},{sub:#04x},{off:#010x},{size:#010x},{flags:#x}')
    assert off+size <= len(b)
    if typ == 0:
        apps.append((off,size))
off, size = min(apps)
h = b[off:off+24]
magic, segments, mode, speed_size, entry = struct.unpack('<BBBBI', h[:8])
print(f'first_app_offset={off:#x}\napp_header_hex={h.hex()}')
print(f'magic={magic:#x} segments={segments} flash_mode={mode:#x} flash_speed_size={speed_size:#x} entry={entry:#x}')
print(f'wp_pin={h[8]:#x} spi_pin_drive={h[9:12].hex()} chip_id={int.from_bytes(h[12:14], "little")} min_chip_rev={h[14]} min_chip_rev_full={int.from_bytes(h[15:17], "little")} max_chip_rev_full={int.from_bytes(h[17:19], "little")} hash_appended={h[23]}')
assert magic == 0xe9
load, seglen = struct.unpack('<II', b[off+24:off+32])
print(f'first_segment_load={load:#x} length={seglen:#x}')
d = b[off+32:off+288]
print(f'app_descriptor_magic={int.from_bytes(d[:4], "little"):#x}')
if d[:4] == b'\x32\x54\xcd\xab':
    for name, start, length in [('version',16,32),('project',48,32),('time',80,16),('date',96,16),('idf',112,32)]:
        print(f'{name}={d[start:start+length].split(bytes([0]))[0].decode(errors="replace")}')

```

Output (exit 0):

```text
backup=E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin
size=4194304
sha256=28cc552e78e33db41fa58816cc28e2d04b01cb1458366f021a2e6750809acb5f
name,type,subtype,offset,size,flags
nvs,0x01,0x02,0x00009000,0x00005000,0x0
otadata,0x01,0x00,0x0000e000,0x00002000,0x0
app0,0x00,0x10,0x00010000,0x00140000,0x0
app1,0x00,0x11,0x00150000,0x00140000,0x0
spiffs,0x01,0x82,0x00290000,0x00170000,0x0
partition MD5 stored=28f14c0945017760a107065db97a2507 calculated=28f14c0945017760a107065db97a2507 match=True
partition terminator at 0x80c0
first_app_offset=0x10000
app_header_hex=e906022f6c210840ee000000000000000000000000000001
magic=0xe9 segments=6 flash_mode=0x2 flash_speed_size=0x2f entry=0x4008216c
wp_pin=0xee spi_pin_drive=000000 chip_id=0 min_chip_rev=0 min_chip_rev_full=0 max_chip_rev_full=0 hash_appended=1
first_segment_load=0x3f400020 length=0x2b830
app_descriptor_magic=0xabcd5432
version=9036908-dirty
project=esp32-arduino-lib-builder
time=06:09:28
date=Mar 26 2021
idf=v3.3.5-1-g85c43024c

```

## Restore procedure (documented only, not executed)

The complete image restores the saved stock LVGL demo, its original partition layout, NVS, OTA selection and SPIFFS contents. GUI identity is the session's stock-demo description, not an app-header inference; restoration itself was not tested in this read-only chunk. Close any future serial session and first check that the saved file is exactly 4,194,304 bytes with SHA256 `28cc552e78e33db41fa58816cc28e2d04b01cb1458366f021a2e6750809acb5f`.

```powershell
& 'E:\dev\CYDpod\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe' --chip esp32 --port COM10 --baud 115200 --after hard-reset write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin'
if ($LASTEXITCODE -ne 0) { throw 'Restore failed; preserve logs and stop.' }
```

The `keep` options preserve the backed-up header settings. The restore is a future, separately authorised flash write, COM10 only; it was not run here. No SD data is included in the SPI flash backup. No binary, logs, parser or build artifacts are committed; `.tools/` is ignored.

## Exact read commands and real output

Run from `E:\dev\CYDpod`. Created the directory with `New-Item -ItemType Directory -Force .tools/backup | Out-Null`; checked each destination did not already exist before reading. No retry was required. `read-flash --help` and `write-flash --help` were local help-only invocations, with no device access.

```powershell
& '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe' --chip esp32 --port COM10 --baud 460800 read-flash --no-progress 0x0 0x400000 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin' 2>&1 | Tee-Object -FilePath .tools/backup/cy0b-read1.txt
$readExit = $LASTEXITCODE
"exit=$readExit" | Tee-Object -FilePath .tools/backup/cy0b-read1.txt -Append
```

```text
esptool v5.3.0
Serial port COM10:
Connecting....
Connected to ESP32 on COM10:
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Features:           Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
Crystal frequency:  40MHz
MAC:                ec:64:c9:89:d9:a4

Uploading stub flasher...
Running stub flasher...
Stub flasher running.
Changing baud rate to 460800...
Changed.

Configuring flash size...
Read 4194304 bytes from 0x00000000 in 100.3 seconds (334.5 kbit/s) to 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin'.

Hard resetting via RTS pin...
exit=0

```

```powershell
& '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe' --chip esp32 --port COM10 --baud 460800 read-flash --no-progress 0x0 0x400000 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.verify.tmp.bin' 2>&1 | Tee-Object -FilePath .tools/backup/cy0b-read2.txt
$readExit = $LASTEXITCODE
"exit=$readExit" | Tee-Object -FilePath .tools/backup/cy0b-read2.txt -Append
```

```text
esptool v5.3.0
Serial port COM10:
Connecting....
Connected to ESP32 on COM10:
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Features:           Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
Crystal frequency:  40MHz
MAC:                ec:64:c9:89:d9:a4

Uploading stub flasher...
Running stub flasher...
Stub flasher running.
Changing baud rate to 460800...
Changed.

Configuring flash size...
Read 4194304 bytes from 0x00000000 in 100.2 seconds (334.8 kbit/s) to 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.verify.tmp.bin'.

Hard resetting via RTS pin...
exit=0

```

Verification commands and output:

```powershell
$backup = 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.bin'
$second = 'E:\dev\CYDpod\.tools\backup\stock-esp32-2432s028-2026-10-25.verify.tmp.bin'
$size1 = (Get-Item -LiteralPath $backup).Length
$size2 = (Get-Item -LiteralPath $second).Length
$hash1 = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash
$hash2 = (Get-FileHash -LiteralPath $second -Algorithm SHA256).Hash
@("read1_size=$size1", "read1_sha256=$hash1", "read2_size=$size2", "read2_sha256=$hash2", "hashes_identical=$($hash1 -eq $hash2)") | Tee-Object -FilePath .tools/backup/cy0b-verification.txt
if ($size1 -ne 4194304 -or $size2 -ne 4194304 -or $hash1 -ne $hash2) { throw 'Read verification failed; preserve both images and stop.' }
Remove-Item -LiteralPath $second
"temporary_file_exists=$(Test-Path -LiteralPath $second)" | Tee-Object -FilePath .tools/backup/cy0b-verification.txt -Append
```

```text
read1_size=4194304
read1_sha256=28CC552E78E33DB41FA58816CC28E2D04B01CB1458366F021A2E6750809ACB5F
read2_size=4194304
read2_sha256=28CC552E78E33DB41FA58816CC28E2D04B01CB1458366F021A2E6750809ACB5F
hashes_identical=True
temporary_file_exists=False

```

## Compile-only check and Git evidence

Command: `powershell -NoProfile -File tools/check.ps1 *> .tools/backup/cy0b-check.txt`; exit 0. This is separate from device operations; it does not establish AirPods, SD, display or wired-output acceptance. Full output:

```text
Compiling player...
Sketch uses 1336425 bytes (67%) of program storage space. Maximum is 1966080 bytes.
Global variables use 56176 bytes (17%) of dynamic memory, leaving 271504 bytes for local variables. Maximum is 327680 bytes.
Compiling uploader...
Sketch uses 1147449 bytes (58%) of program storage space. Maximum is 1966080 bytes.
Global variables use 48900 bytes (14%) of dynamic memory, leaving 278780 bytes for local variables. Maximum is 327680 bytes.
Compiling aac-probe...
Sketch uses 1222730 bytes (62%) of program storage space. Maximum is 1966080 bytes.
Global variables use 61528 bytes (18%) of dynamic memory, leaving 266152 bytes for local variables. Maximum is 327680 bytes.
audio_core_test.cpp
PASS ring capacity/wrap/concurrency, ADTS rejection, gain, saved levels, command overflow
aacdec.c
aactabs.c
bitstream.c
buffers.c
dct4.c
decelmnt.c
dequant.c
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\dequant.c(114): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\dequant.c(115): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\dequant.c(118): warning C4068: unknown pragma 'GCC'
fft.c
filefmt.c
huffman.c
hufftabs.c
imdct.c
noiseless.c
pns.c
sbr.c
sbrfft.c
sbrfreq.c
sbrhfadj.c
sbrhfgen.c
sbrhuff.c
Generating Code...
Compiling...
sbrimdct.c
sbrmath.c
sbrqmf.c
sbrside.c
sbrtabs.c
stproc.c
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\stproc.c(84): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\stproc.c(85): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\stproc.c(90): warning C4068: unknown pragma 'GCC'
tns.c
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\tns.c(52): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\tns.c(53): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\tns.c(69): warning C4068: unknown pragma 'GCC'
trigtabs.c
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\trigtabs.c(48): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\trigtabs.c(49): warning C4068: unknown pragma 'GCC'
..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\trigtabs.c(1004): warning C4068: unknown pragma 'GCC'
Generating Code...
helix_decode.cpp
PASS AAC allocation failures 1/2 and 100 begin/end cycles, peak_decoder_heap=20656 live=0
PASS Helix AAC-LC 44100 stereo frames=131 samples_per_channel=134144 peak_decoder_heap=20656 SNR=59.58dB peak_error=190
PASS Helix AAC-LC 44100 stereo frames=131 samples_per_channel=134144 peak_decoder_heap=20656 SNR=59.81dB peak_error=188
PASS Helix AAC-LC 44100 stereo frames=131 samples_per_channel=134144 peak_decoder_heap=20656 SNR=71.67dB peak_error=54
PASS expected rejection reject-48k.aac
PASS expected rejection reject-main.aac
PASS expected rejection reject-malformed.aac
PASS expected rejection reject-mono.aac
PASS expected rejection reject-truncated.aac
PASS expected rejection stereo-faststart.m4a
PASS expected rejection stereo-moov-last.m4a
PASS synthetic fixture matrix: 3 positive, 7 negative; report fixtures/verification.json
PASS: compile-only checks completed; no device commands were run.

exit=0
```

Pre-commit `git status --short`:

```text
 M docs/research/build-baseline.md
?? docs/research/cy0b-backup.md
```

Pre-commit `git log --oneline -3`:

```text
d54c238 Merge branch 'cydpod/cy-0a' into cydpod/cy-9
70c5e29 Add read-only ls command to the probe
093202f Add Bluetooth hardware test plan and serial session script
```

Only this report is staged and committed. `git check-ignore .tools/backup/stock-esp32-2432s028-2026-10-25.bin` prints that path, confirming the backup remains ignored. No push was performed. The backup exists on this host only; an off-host copy and actual restore validation are outside this chunk.
