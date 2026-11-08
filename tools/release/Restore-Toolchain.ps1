param([Parameter(Mandatory)][string]$Source)

$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $Source).Path
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'dependencies.lock.json') -PathType Leaf)) {
    throw 'Pass the root of the extracted CYDpod source or Git checkout.'
}
foreach ($name in '.tools', '.deps') {
    if (Test-Path -LiteralPath (Join-Path $sourceRoot $name)) {
        throw "Use a fresh source checkout. $name already exists."
    }
}

$kit = Join-Path $PSScriptRoot 'windows-w328c-toolchain.zip'
if ((Get-FileHash -LiteralPath $kit -Algorithm SHA256).Hash -ne '774e51c9bf1617de894a30b3e4542d4c81fdc108f7beda512267d7e67f4cd453') {
    throw 'Base W328C toolchain checksum mismatch.'
}
& python -I -m zipfile -e $kit $sourceRoot
if ($LASTEXITCODE -ne 0) { throw 'Could not extract the W328C toolchain archive.' }
& python -I (Join-Path $PSScriptRoot 'Restore-A2DP.py') $sourceRoot
if ($LASTEXITCODE -ne 0) { throw 'Could not restore the pinned A2DP library overlay.' }
& python -I (Join-Path $PSScriptRoot 'Restore-Bluedroid.py') $sourceRoot
if ($LASTEXITCODE -ne 0) { throw 'Could not apply the pinned trimmed-Bluedroid overlay.' }

$root = $sourceRoot.Replace('\', '/')
$arduinoData = "$root/.deps/arduino" | ConvertTo-Json -Compress
$arduinoDownloads = "$root/.tools/downloads/arduino" | ConvertTo-Json -Compress
$arduinoUser = "$root/.deps/sketchbook" | ConvertTo-Json -Compress
$arduinoCache = "$root/.tools/build-cache" | ConvertTo-Json -Compress
$config = @"
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: $arduinoData
  downloads: $arduinoDownloads
  user: $arduinoUser
build_cache:
  path: $arduinoCache
"@
[IO.File]::WriteAllText((Join-Path $sourceRoot '.tools/arduino-cli.yaml'), $config + "`n")
Write-Output 'PASS W328C 0.5.3 toolchain restored (trimmed Bluedroid).'
