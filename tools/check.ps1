# Compile-only. There is deliberately no upload, monitor, port or erase option.
[CmdletBinding()]
param([switch]$SkipProbe, [switch]$SkipHostMedia)
$ErrorActionPreference = 'Stop'

function Test-FileInventory {
    param([string]$Label, [string]$SourceRoot, [string]$InventoryPath)
    $expected = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
    $expectedNames = @($expected.PSObject.Properties.Name)
    foreach ($item in ($expected.PSObject.Properties | Sort-Object Name)) {
        $path = Join-Path $SourceRoot $item.Name
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$Label inventory missing file: $($item.Name)"
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.Value) {
            throw "$Label inventory SHA256 mismatch: $($item.Name)"
        }
    }
    $source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd([char]92, [char]47)
    foreach ($file in (Get-ChildItem -LiteralPath $source -Recurse -File -Force | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($source.Length + 1).Replace([char]92, [char]47)
        if ($expectedNames -cnotcontains $relative) {
            throw "$Label inventory unexpected file: $relative"
        }
    }
}

$root = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $root '.tools/arduino-cli/arduino-cli.exe'
$config = Join-Path $root '.tools/arduino-cli.yaml'
if (!(Test-Path $cli) -or !(Test-Path $config)) { throw 'Run powershell -File tools/setup.ps1 first.' }
$lock = Get-Content (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
# Arduino CLI cannot discover packages through a Windows junction parent.
# Keep outputs private but point it at the canonical installed dependency tree.
$dependencyItem = Get-Item -LiteralPath (Join-Path $root '.deps')
$dependencyRoot = $dependencyItem.FullName
if ($dependencyItem.LinkType -and $dependencyItem.Target) {
    $dependencyRoot = @($dependencyItem.Target)[0]
    if (![IO.Path]::IsPathRooted($dependencyRoot)) { $dependencyRoot = Join-Path $root $dependencyRoot }
}
$env:ARDUINO_DIRECTORIES_DATA = Join-Path $dependencyRoot 'arduino'
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $root '.tools/downloads/arduino'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $dependencyRoot 'sketchbook'
$env:TEMP = $env:TMP = Join-Path $root '.tools/tmp'
New-Item -ItemType Directory -Force $env:TEMP,(Join-Path $root '.tools/logs') | Out-Null
Push-Location $root
try {
    $expected = Get-Content tools/upstream-files.sha256.json -Raw | ConvertFrom-Json
    foreach ($item in $expected.PSObject.Properties) {
        $path = Join-Path 'vendor/CYD-MP3-Player' $item.Name
        if (!(Test-Path $path) -or (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.Value) {
            throw "Vendored source changed: $path"
        }
    }
    $sourceFiles = @(Get-ChildItem vendor/CYD-MP3-Player -Recurse -File)
    if ($sourceFiles.Count -ne @($expected.PSObject.Properties).Count) {
        throw 'Unexpected files in vendored source snapshot.'
    }
    Test-FileInventory -Label 'AudioTools' -SourceRoot (Join-Path $root 'vendor/arduino-audio-tools') `
        -InventoryPath (Join-Path $root 'tools/audiotools-files.sha256.json')
    Test-FileInventory -Label 'MiniAAC' -SourceRoot (Join-Path $root 'vendor/miniaac') `
        -InventoryPath (Join-Path $root 'tools/miniaac-files.sha256.json')
    if ((Get-FileHash $cli).Hash.ToLowerInvariant() -ne $lock.cli.executableSha256) { throw 'Arduino CLI executable differs from lock.' }
    if ((Get-FileHash .deps/sketchbook/libraries/arduino-libhelix/src/ConfigHelix.h).Hash.ToLowerInvariant() -ne $lock.patches[0].sha256) {
        throw 'Helix config differs from lock; run setup.ps1.'
    }
    if ((Get-FileHash .deps/sketchbook/libraries/TFT_eSPI/User_Setup.h).Hash.ToLowerInvariant() -ne $lock.tft.sha256) {
        throw 'TFT configuration differs from lock; run setup.ps1.'
    }
    & python -I tools/stage_miniaac.py
    if ($LASTEXITCODE -ne 0) { throw 'MiniAAC staging failed.' }
    $jobs = @(
        @{ Name = 'player'; Sketch = 'vendor/CYD-MP3-Player/app'; Flags = '' },
        @{ Name = 'uploader'; Sketch = 'vendor/CYD-MP3-Player/app'; Flags = '-DCYD_UPLOADER' }
    )
    # CY-33: the maintained touchscreen player copy (M4A via MiniAAC); MiniAAC is passed as a single --library.
    $jobs += @{ Name = 'cydpod-player'; Sketch = 'firmware/cydpod_player/app'; Flags = ''
        Library = (Join-Path $root '.tools/staging/miniaac-patched') }
    if (!$SkipProbe) {
        if (!(Test-Path firmware/cydpod_probe/cydpod_probe.ino)) {
            throw 'Required AAC probe is missing. Use -SkipProbe only for an explicit stock-baseline check.'
        }
        # CY-31: the probe decodes with MiniAAC (vendor/miniaac, inventory verified above), passed to arduino-cli as a single --library.
        # AudioTools and lib/cydpod_m4a are no longer on the probe's build path. The retained AudioTools overlay patches (CY-10b1) are
        # still applied to a staged copy of the pristine tree below so that they keep applying; vendor/ and its inventory are never modified.
        $patchedRoot = Join-Path $root '.tools/staging/audiotools-patched'
        if (Test-Path $patchedRoot) {
            if (!(Resolve-Path $patchedRoot).Path.StartsWith((Join-Path $root '.tools/staging/'), [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Staging cleanup escaped .tools/staging.'
            }
            Remove-Item -LiteralPath $patchedRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Force $patchedRoot | Out-Null
        Copy-Item -LiteralPath (Join-Path $root 'vendor/arduino-audio-tools/src') -Destination $patchedRoot -Recurse
        foreach ($patch in (Get-ChildItem (Join-Path $root 'patches/audio-tools') -Filter *.patch | Sort-Object Name)) {
            Push-Location $patchedRoot
            try { & git apply --whitespace=nowarn $patch.FullName } finally { Pop-Location }
            if ($LASTEXITCODE -ne 0) { throw "AudioTools overlay failed to apply: $($patch.Name)" }
        }
        $jobs += @{ Name = 'aac-probe'; Sketch = 'firmware/cydpod_probe'; Flags = '-DCYDPOD_AAC_LC_ONLY'
            Library = (Join-Path $root '.tools/staging/miniaac-patched') }
    }
    $results = @()
    foreach ($job in $jobs) {
        # ESP32 3.3.10 savehex hooks also export beside the sketch. Compile a
        # local staging copy so those hooks cannot write into source folders.
        $stageParent = Join-Path $root ".tools/staging/$($job.Name)"
        $stage = Join-Path $stageParent (Split-Path $job.Sketch -Leaf)
        if (Test-Path $stageParent) {
            $resolved = (Resolve-Path $stageParent).Path
            if (!$resolved.StartsWith((Join-Path $root '.tools/staging/'), [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Staging cleanup escaped .tools/staging.'
            }
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
        New-Item -ItemType Directory -Force $stage | Out-Null
        $source = (Resolve-Path $job.Sketch).Path
        Get-ChildItem -LiteralPath $source -Recurse -File | ForEach-Object {
            $relative = $_.FullName.Substring($source.Length + 1)
            if ($relative -notmatch '^build[\\/]') {
                $dest = Join-Path $stage $relative
                New-Item -ItemType Directory -Force (Split-Path $dest -Parent) | Out-Null
                Copy-Item -LiteralPath $_.FullName -Destination $dest
            }
        }
        # GCC -w suppresses even local diagnostic-error pragmas. Keep it for
        # stock builds only; the probe enables format errors in its own files.
        $warnings = if ($job.Name -eq 'aac-probe') { 'default' } else { 'none' }
        $arguments = @('--config-file', $config, 'compile', '--fqbn', $lock.core.fqbn,
            '--warnings', $warnings, '--build-path', (Join-Path $root ".tools/build/$($job.Name)"),
            '--output-dir', (Join-Path $root ".tools/images/$($job.Name)"))
        if ($job.Flags) {
            $arguments += @('--build-property', "compiler.cpp.extra_flags=$($job.Flags)",
                '--build-property', "compiler.c.extra_flags=$($job.Flags)")
        }
        if ($job.Library) { $arguments += @('--library', $job.Library) }
        $arguments += $stage
        $log = Join-Path $root ".tools/logs/$($job.Name).log"
        Write-Host "Compiling $($job.Name)..."
        $writer = [System.IO.StreamWriter]::new($log, $false, [System.Text.UTF8Encoding]::new($false))
        $writer.AutoFlush = $true
        $writer.AutoFlush = $true
        $writer.WriteLine('Command: arduino-cli ' + ($arguments -join ' '))
        # Windows PowerShell treats native stderr as an ErrorRecord; capture it
        # without converting harmless compiler warnings into terminating errors.
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & $cli @arguments 2>&1 | ForEach-Object {
                $line = "$_"
                $writer.WriteLine($line)
                Write-Output $line
            }
            $code = $LASTEXITCODE
        } finally {
            $writer.Dispose()
            $ErrorActionPreference = $previous
        }
        if ($code -ne 0) { throw "$($job.Name) failed ($code). See $log" }
        $binary = Get-Item (Join-Path $root ".tools/images/$($job.Name)/*.ino.bin")
        if ($binary.Length -gt 1966080) { throw "$($job.Name) exceeds min_spiffs app slot." }
        if ((Get-FileHash (Join-Path $root ".tools/build/$($job.Name)/partitions.csv")).Hash.ToLowerInvariant() -ne $lock.core.partitionsSha256) {
            throw 'Built partition table differs from the locked min_spiffs layout.'
        }
        if ($job.Name -ne 'aac-probe') {
            $imageText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($binary.FullName))
            $isPlayer = $imageText.Contains('heap before audio + bt')
            $isUploader = $imageText.Contains('upload: hotspot')
            if ((($job.Name -eq 'player' -or $job.Name -eq 'cydpod-player') -and (!$isPlayer -or $isUploader)) -or
                ($job.Name -eq 'uploader' -and (!$isUploader -or $isPlayer))) {
                throw "Wrong program in $($job.Name) image (upstream marker check)."
            }
        }
        $results += [pscustomobject]@{ name=$job.Name; bytes=$binary.Length;
            sha256=(Get-FileHash $binary.FullName).Hash.ToLowerInvariant(); log=$log }
    }
    $results | ConvertTo-Json | Set-Content .tools/logs/build-results.json
    if (!$SkipHostMedia) {
        & cmd.exe /d /c tests\build_catalog.cmd
        if ($LASTEXITCODE -ne 0) { throw 'Catalogue/navigation host checks failed.' }
        if (!(Test-Path tools/media_check.py)) { throw 'Required host media check is missing.' }
        & python tools/media_check.py
        if ($LASTEXITCODE -ne 0) { throw 'Host media checks failed. See output above.' }
    }
    Write-Host 'PASS: compile-only checks completed; no device commands were run.'
} finally { Pop-Location }
