# CY-11c: flash (no erase) + verify the four probe regions to COM10 only. Usage: -ImageDir .tools/images/aac-probe -Tag committed; the player image: -ImageDir .tools/images/cydpod-player -Tag player -Base app.ino -BootApp .tools/build/cydpod-player/boot_app0.bin
param([Parameter(Mandatory)][string]$ImageDir,[Parameter(Mandatory)][string]$Tag,[string]$Base='cydpod_probe.ino',[string]$BootApp='.tools/build/aac-probe/boot_app0.bin')
$ErrorActionPreference = 'Stop'
$esp = '.\.deps\arduino\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$f = @('0x1000',"$ImageDir/$Base.bootloader.bin",'0x8000',"$ImageDir/$Base.partitions.bin",
 '0xe000',$BootApp,'0x10000',"$ImageDir/$Base.bin")
foreach ($i in 1,3,5,7) { if (!(Test-Path -LiteralPath $f[$i])) { throw "Missing $($f[$i])" }
  "$($f[$i]) bytes=$((Get-Item $f[$i]).Length) sha256=$((Get-FileHash $f[$i]).Hash.ToLower())" }
& $esp --chip esp32 --port COM10 --baud 460800 write-flash --no-progress --flash-mode dio --flash-freq 80m --flash-size 4MB @f 2>&1 | Tee-Object ".tools/logs/cy11c-flash-$Tag.txt"
if ($LASTEXITCODE -ne 0) { throw 'Flash failed' }
& $esp --chip esp32 --port COM10 --baud 460800 verify-flash --flash-mode dio --flash-freq 80m --flash-size 4MB @f 2>&1 | Tee-Object ".tools/logs/cy11c-verify-$Tag.txt"
if ($LASTEXITCODE -ne 0) { throw 'Verify failed' }
'FLASH_VERIFY_OK'
