# Compile-only. There is deliberately no upload, monitor, port or erase option.
[CmdletBinding()]
param([switch]$SkipProbe, [switch]$SkipHostMedia)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $root '.tools/arduino-cli/arduino-cli.exe'
$config = Join-Path $root '.tools/arduino-cli.yaml'
if (!(Test-Path $cli) -or !(Test-Path $config)) { throw 'Run powershell -File tools/setup.ps1 first.' }
$lock = Get-Content (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$env:ARDUINO_DIRECTORIES_DATA = Join-Path $root '.deps/arduino'
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $root '.tools/downloads/arduino'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $root '.deps/sketchbook'
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
    if ((Get-FileHash $cli).Hash.ToLowerInvariant() -ne $lock.cli.executableSha256) { throw 'Arduino CLI executable differs from lock.' }
    if ((Get-FileHash .deps/sketchbook/libraries/arduino-libhelix/src/ConfigHelix.h).Hash.ToLowerInvariant() -ne $lock.patches[0].sha256) {
        throw 'Helix config differs from lock; run setup.ps1.'
    }
    if ((Get-FileHash .deps/sketchbook/libraries/TFT_eSPI/User_Setup.h).Hash.ToLowerInvariant() -ne $lock.tft.sha256) {
        throw 'TFT configuration differs from lock; run setup.ps1.'
    }
    $jobs = @(
        @{ Name = 'player'; Sketch = 'vendor/CYD-MP3-Player/app'; Flags = '' },
        @{ Name = 'uploader'; Sketch = 'vendor/CYD-MP3-Player/app'; Flags = '-DCYD_UPLOADER' }
    )
    if (!$SkipProbe) {
        if (!(Test-Path firmware/cydpod_probe/cydpod_probe.ino)) {
            throw 'Required AAC probe is missing. Use -SkipProbe only for an explicit stock-baseline check.'
        }
        $jobs += @{ Name = 'aac-probe'; Sketch = 'firmware/cydpod_probe'; Flags = '-DCYDPOD_AAC_LC_ONLY' }
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
        $arguments = @('--config-file', $config, 'compile', '--fqbn', $lock.core.fqbn,
            '--warnings', 'none', '--build-path', (Join-Path $root ".tools/build/$($job.Name)"),
            '--output-dir', (Join-Path $root ".tools/images/$($job.Name)"))
        if ($job.Flags) {
            $arguments += @('--build-property', "compiler.cpp.extra_flags=$($job.Flags)",
                '--build-property', "compiler.c.extra_flags=$($job.Flags)")
        }
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
            if (($job.Name -eq 'player' -and (!$isPlayer -or $isUploader)) -or
                ($job.Name -eq 'uploader' -and (!$isUploader -or $isPlayer))) {
                throw "Wrong program in $($job.Name) image (upstream marker check)."
            }
        }
        $results += [pscustomobject]@{ name=$job.Name; bytes=$binary.Length;
            sha256=(Get-FileHash $binary.FullName).Hash.ToLowerInvariant(); log=$log }
    }
    $results | ConvertTo-Json | Set-Content .tools/logs/build-results.json
    if (!$SkipHostMedia) {
        if (!(Test-Path tools/media_check.py)) { throw 'Required host media check is missing.' }
        & python tools/media_check.py
        if ($LASTEXITCODE -ne 0) { throw 'Host media checks failed. See output above.' }
    }
    Write-Host 'PASS: compile-only checks completed; no device commands were run.'
} finally { Pop-Location }
