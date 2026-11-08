<#
Switch the Arduino core's esp32-libs 3.3.10 between Espressif's stock package
and the trimmed-Bluedroid package built by make-libs.py.
  use-libs.ps1 trim    # .tools/esp32-libs-cydpod-bt becomes esp32-libs/3.3.10
  use-libs.ps1 stock   # Espressif's package (kept as 3.3.10.stock) comes back
#>
param([Parameter(Mandatory)][ValidateSet('stock', 'trim')][string]$Variant, [string]$Root)
$ErrorActionPreference = 'Stop'
if (!$Root) { $Root = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..')).Path }
$dir = Join-Path $Root '.deps\arduino\packages\esp32\tools\esp32-libs'
$live = Join-Path $dir '3.3.10'; $stock = Join-Path $dir '3.3.10.stock'
$trim = Join-Path $Root '.tools\esp32-libs-cydpod-bt'
$isTrim = (Test-Path (Join-Path $live 'versions.txt')) -and (Select-String -Quiet -Path (Join-Path $live 'versions.txt') -Pattern '^cydpod-bt:')
if ($Variant -eq 'trim') {
    if (!(Test-Path $trim)) { throw "Build the trimmed package first: $trim" }
    if (!$isTrim) { if (!(Test-Path $stock)) { Rename-Item $live '3.3.10.stock' } else { Remove-Item -Recurse -Force $live } }
    else { Remove-Item -Recurse -Force $live }
    Copy-Item -Recurse $trim $live
} else {
    if (!(Test-Path $stock)) { if ($isTrim) { throw 'Stock package missing' } else { 'stock already in place'; return } }
    if (Test-Path $live) { Remove-Item -Recurse -Force $live }
    Rename-Item $stock '3.3.10'
}
"esp32-libs 3.3.10 is now $Variant"
