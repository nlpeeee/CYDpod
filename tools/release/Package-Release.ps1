# Package a CYDpod release from the images tools/check.ps1 compiled. No device commands.
#
# 1. Commit the release (docs/releases/<version>.md) and tag it: git tag -a v<version>
# 2. Compile both boards from that commit, one after the other (both restage MiniAAC):
#      powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144 -BuildLabel release-<digits>
#      powershell -NoProfile -File tools/check.ps1 -Board esp32-2432s028r -SkipProbe -SkipHostMedia -LargePcmRing -PcmRingFrames 6144 -BuildLabel release-<digits>
# 3. powershell -NoProfile -File tools/release/Package-Release.ps1 -Version <version> -Previous <last release>
#
# Writes releases/<version>/ (ignored by Git): firmware/<board>/ with a four-region MANIFEST.json,
# flash-tools, build kit, enclosure, source zip and bundle, evidence, and packages/ with the
# GitHub assets and SHA256SUMS.txt. Unchanged inputs (esptool, build kit, enclosure) come from
# releases/<previous>; the build kit is refused if dependencies.lock.json has changed since.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Previous,
    [string]$ImagePrefix = 'codec-pool-v36-headroom-ring6144',
    [string]$BuildLabel,
    [switch]$Force)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (!$BuildLabel) { $BuildLabel = 'release-' + ($Version -replace '\.', '') }
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$utf8 = [Text.UTF8Encoding]::new($false)

function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Get-TextSha256([string]$Path) {
    # Line endings differ between checkouts and copies; compare content.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = $utf8.GetString($bytes).Replace("`r`n", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    -join ($sha.ComputeHash($utf8.GetBytes($text)) | ForEach-Object { $_.ToString('x2') })
}
function Write-Text([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Force (Split-Path $Path -Parent) | Out-Null
    [IO.File]::WriteAllText($Path, $Text.Replace("`r`n", "`n").Replace("`n", "`r`n"), $utf8)
}
# Entries use forward slashes (Compress-Archive on Windows PowerShell 5.1 writes backslashes).
function New-Zip([string]$ZipPath, [Collections.Specialized.OrderedDictionary]$Entries) {
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    $zip = [IO.Compression.ZipFile]::Open($ZipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in $Entries.Keys) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $Entries[$name], $name.Replace([char]92, [char]47),
                [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $zip.Dispose() }
}
function Add-Tree([Collections.Specialized.OrderedDictionary]$Entries, [string]$Dir, [string]$Prefix) {
    $base = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd([char]92)
    foreach ($f in (Get-ChildItem -LiteralPath $base -Recurse -File | Sort-Object FullName)) {
        $Entries[$Prefix + $f.FullName.Substring($base.Length + 1).Replace([char]92, [char]47)] = $f.FullName
    }
}

Push-Location $root
try {
    $tag = "v$Version"
    $commit = (& git rev-parse HEAD).Trim()
    $tagCommit = (& git rev-parse --verify --quiet "$tag^{commit}")
    if ($LASTEXITCODE -ne 0 -or !$tagCommit -or $tagCommit.Trim() -ne $commit) { throw "Tag $tag must exist and point at HEAD ($commit)." }
    if (& git status --porcelain --untracked-files=no) { throw 'Tracked files have uncommitted changes: package only a committed, tagged tree.' }
    $notes = Join-Path $root "docs/releases/$Version.md"
    if (!(Test-Path $notes)) { throw "Missing release notes docs/releases/$Version.md." }
    $prev = Join-Path $root "releases/$Previous"
    foreach ($need in 'flash-tools/esptool.exe', 'build/dependencies.lock.json') {
        if (!(Test-Path (Join-Path $prev $need))) { throw "releases/$Previous/$need is missing." }
    }
    if ((Get-TextSha256 'dependencies.lock.json') -ne (Get-TextSha256 (Join-Path $prev 'build/dependencies.lock.json'))) {
        throw "dependencies.lock.json changed since $($Previous): rebuild the build kit instead of reusing it."
    }
    $out = Join-Path $root "releases/$Version"
    if (Test-Path $out) {
        if (!$Force) { throw "releases/$Version exists; pass -Force to rebuild it." }
        Remove-Item -LiteralPath $out -Recurse -Force
    }
    New-Item -ItemType Directory -Force $out | Out-Null
    $date = Get-Date -Format 'yyyy-MM-dd'

    $boards = @(
        [pscustomobject]@{ Id = 'jc2432w328c'; Label = 'JC2432W328C'; Zip = "CYDpod-$Version-firmware.zip"
            Name = 'JC2432W328C (2.8", ST7789, CST820 capacitive touch)'
            Notes = @('Plays over Bluetooth, or a wired I2S DAC: Settings > Audio Output > Headphones.') },
        [pscustomobject]@{ Id = 'esp32-2432s028r'; Label = 'ESP32-2432S028R'; Zip = "CYDpod-$Version-firmware-ESP32-2432S028R.zip"
            Name = 'ESP32-2432S028R (2.8", ILI9341 or ST7789, XPT2046 resistive touch)'
            Notes = @('Plays over Bluetooth or the speaker pad. This board has no wired DAC route.',
                'Touch calibration runs on first boot; run it again from Settings > Display.',
                'Blank or garbled screen? Your board has the other panel controller. Hold a finger on the screen from power-up for 8 s: the blue LED blinks from 4 s, then CYDpod switches controller and restarts.') })
    $regions = [ordered]@{ 'app.ino.bootloader.bin' = '0x1000'; 'app.ino.partitions.bin' = '0x8000'; 'boot_app0.bin' = '0xe000'; 'app.ino.bin' = '0x10000' }
    $firmwareSummary = [ordered]@{}

    foreach ($b in $boards) {
        $img = Join-Path $root ".tools/images/$ImagePrefix-$BuildLabel-$($b.Id)"
        if (!(Test-Path (Join-Path $img 'MANIFEST.json'))) { throw "No compiled image for $($b.Id) at $($img): run tools/check.ps1 -Board $($b.Id) -BuildLabel $BuildLabel first." }
        $built = Get-Content (Join-Path $img 'MANIFEST.json') -Raw | ConvertFrom-Json
        if ($built.board -ne $b.Id) { throw "$img MANIFEST names board '$($built.board)', not $($b.Id)." }
        $dest = Join-Path $out "firmware/$($b.Id)"
        New-Item -ItemType Directory -Force $dest | Out-Null
        $files = foreach ($file in $regions.Keys) {
            $entry = @($built.files | Where-Object { $_.file -eq $file })
            if ($entry.Count -ne 1 -or $entry[0].offset -ne $regions[$file]) { throw "$($b.Id): $file missing from MANIFEST or not at $($regions[$file])." }
            $src = Join-Path $img $file
            if ((Get-Sha256 $src) -ne $entry[0].sha256) { throw "$($b.Id): $file does not match its MANIFEST hash." }
            Copy-Item -LiteralPath $src -Destination $dest
            [pscustomobject]@{ file = $file; offset = $regions[$file]; bytes = (Get-Item $src).Length; sha256 = $entry[0].sha256 }
        }
        $manifest = [ordered]@{ release = "CYDpod $Version"; version = $Version; tag = $tag; sourceCommit = $commit; board = $b.Id; boardName = $b.Name
            chip = $built.chip; flashSize = $built.flashSize; flashMode = $built.flashMode; flashFreq = $built.flashFreq; fqbn = $built.fqbn
            pcmRingFrames = 6144; buildLabel = $BuildLabel; files = @($files) }
        Write-Text (Join-Path $dest 'MANIFEST.json') ($manifest | ConvertTo-Json -Depth 5)
        $symbols = Join-Path $out "evidence/debug-symbols/$($b.Id)"
        New-Item -ItemType Directory -Force $symbols | Out-Null
        foreach ($s in 'app.ino.elf', 'app.ino.map') { Copy-Item -LiteralPath (Join-Path $img $s) -Destination $symbols }
        $firmwareSummary[$b.Id] = [ordered]@{ manifest = "firmware/$($b.Id)/MANIFEST.json"; appBytes = ($files | Where-Object file -eq 'app.ino.bin').bytes
            appSha256 = ($files | Where-Object file -eq 'app.ino.bin').sha256 }
    }

    Copy-Item -LiteralPath (Join-Path $prev 'flash-tools') -Destination $out -Recurse
    Copy-Item -LiteralPath $notes -Destination (Join-Path $out 'RELEASE-NOTES.md')
    $build = Join-Path $out 'build'
    New-Item -ItemType Directory -Force $build | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $prev 'build') -File | Where-Object Name -ne 'README.md' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $build }
    $kitReadme = (Get-Content -LiteralPath (Join-Path $prev 'build/README.md') -Raw) -replace "CYDpod $([regex]::Escape($Previous))", "CYDpod $Version"
    Write-Text (Join-Path $build 'README.md') $kitReadme
    $enclosure = @(Get-ChildItem -LiteralPath (Join-Path $prev 'packages') -Filter "CYDpod-$Previous-enclosure-*.zip")
    if ($enclosure.Count -ne 1) { throw "Expected one enclosure package in releases/$Previous/packages." }
    New-Item -ItemType Directory -Force (Join-Path $out 'hardware') | Out-Null
    Copy-Item -LiteralPath $enclosure[0].FullName -Destination (Join-Path $out ('hardware/' + ($enclosure[0].Name -replace "^CYDpod-$([regex]::Escape($Previous))-", 'CYDpod-')))

    $source = Join-Path $out 'source'
    New-Item -ItemType Directory -Force $source | Out-Null
    # Git reports progress on stderr, which Windows PowerShell 5.1 would turn into a terminating error.
    $ErrorActionPreference = 'Continue'
    & git archive --format=zip --prefix=CYDpod/ -o (Join-Path $source 'CYDpod-source.zip') $tag 2>&1 | Out-Null
    $archived = $LASTEXITCODE
    & git bundle create (Join-Path $source 'CYDpod.git.bundle') main --tags 2>&1 | Out-Null
    $bundled = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($archived -ne 0) { throw 'git archive failed.' }
    if ($bundled -ne 0) { throw 'git bundle failed.' }

    $release = [ordered]@{ release = "CYDpod $Version"; version = $Version; date = $date; tag = $tag; repository = 'https://github.com/nlpeeee/CYDpod'
        sourceBranch = 'main'; sourceCommit = $commit; firmwareCommit = $commit; boards = @($boards.Id); firmware = $firmwareSummary
        buildLabel = $BuildLabel; buildKitFrom = $Previous; enclosureFrom = $Previous
        hardware = 'See RELEASE-NOTES.md for what has and has not been run on each board.' }
    Write-Text (Join-Path $out 'RELEASE.json') ($release | ConvertTo-Json -Depth 5)

    foreach ($b in $boards) {
        $lines = @("# CYDpod $Version for the $($b.Name)", '',
            "Firmware for the **$($b.Label)**, from tag ``$tag`` (``$($commit.Substring(0,7))``). What changed and what is confirmed on hardware: ``RELEASE-NOTES.md``.", '')
        $lines += $b.Notes | ForEach-Object { "- $_" }
        $lines += @('', '## Flash', '',
            'Close any serial monitor and plug the board in; find its COM port by unplugging and replugging it. If esptool cannot connect, hold **BOOT**, tap **RESET**, then release **BOOT**. From this folder, in PowerShell:', '',
            '```powershell', "Set-Location .\firmware\$($b.Id)",
            '$manifest = Get-Content .\MANIFEST.json -Raw | ConvertFrom-Json',
            'foreach ($image in $manifest.files) { if ((Get-FileHash -LiteralPath $image.file -Algorithm SHA256).Hash -ne $image.sha256) { throw "Hash mismatch for $($image.file)" } }',
            "`$port = Read-Host 'COM port for the $($b.Label)'",
            "foreach (`$op in 'write-flash','verify-flash') {",
            '    & ..\..\flash-tools\esptool.exe --chip esp32 --port $port --baud 460800 --before no-reset --after no-reset $op --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 app.ino.bootloader.bin 0x8000 app.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 app.ino.bin',
            '    if ($LASTEXITCODE -ne 0) { throw "esptool $op failed" }', '}', '```', '',
            'Then tap **RESET**. Settings and Bluetooth pairings are kept. The serial log runs at 115200 baud.')
        Write-Text (Join-Path $out "README-$($b.Label).md") ($lines -join "`n")
    }
    $folder = @("# CYDpod $Version", '', "Release ``$tag`` at ``$commit``, packaged $date by ``tools/release/Package-Release.ps1``.", '',
        '| Folder | Contents |', '| --- | --- |')
    $folder += $boards | ForEach-Object { "| ``firmware/$($_.Id)`` | $($_.Name): four flash regions and MANIFEST.json |" }
    $folder += @('| `flash-tools` | esptool 5.3.0 and its notice |', "| ``build`` | Windows build kit (unchanged inputs, reused from $Previous) |",
        "| ``hardware`` | Enclosure, unchanged from $Previous |", '| `source` | Source zip at the tag and a Git bundle of main and its tags |',
        '| `evidence` | Debug symbols for both boards |', '| `packages` | The GitHub release assets and SHA256SUMS.txt |', '',
        'Flash instructions per board: ' + (($boards | ForEach-Object { "``README-$($_.Label).md``" }) -join ', ') + '.')
    Write-Text (Join-Path $out 'README.md') ($folder -join "`n")

    $packages = Join-Path $out 'packages'
    New-Item -ItemType Directory -Force $packages | Out-Null
    $notices = [ordered]@{}
    Add-Tree $notices (Join-Path $root 'vendor/notices') 'vendor/notices/'
    $notices['vendor/NOTICES.md'] = (Join-Path $root 'vendor/NOTICES.md')
    foreach ($b in $boards) {
        $e = [ordered]@{}
        Add-Tree $e (Join-Path $out "firmware/$($b.Id)") "firmware/$($b.Id)/"
        Add-Tree $e (Join-Path $out 'flash-tools') 'flash-tools/'
        $e['README.md'] = Join-Path $out "README-$($b.Label).md"
        $e['RELEASE-NOTES.md'] = Join-Path $out 'RELEASE-NOTES.md'
        $e['RELEASE.json'] = Join-Path $out 'RELEASE.json'
        foreach ($k in $notices.Keys) { $e[$k] = $notices[$k] }
        New-Zip (Join-Path $packages $b.Zip) $e
    }
    $e = [ordered]@{}; Add-Tree $e (Join-Path $out 'evidence/debug-symbols') ''
    New-Zip (Join-Path $packages "CYDpod-$Version-debug-symbols.zip") $e
    $e = [ordered]@{}; Add-Tree $e $build ''
    New-Zip (Join-Path $packages "CYDpod-$Version-build-kit.zip") $e
    Copy-Item -LiteralPath (Join-Path $source 'CYDpod-source.zip') -Destination (Join-Path $packages "CYDpod-$Version-source.zip")
    Copy-Item -LiteralPath $enclosure[0].FullName -Destination (Join-Path $packages ($enclosure[0].Name -replace "^CYDpod-$([regex]::Escape($Previous))-", "CYDpod-$Version-"))
    Copy-Item -LiteralPath (Join-Path $out 'RELEASE-NOTES.md'), (Join-Path $out 'RELEASE.json') -Destination $packages

    $sums = Get-ChildItem -LiteralPath $packages -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name |
        ForEach-Object { '{0} *{1}' -f (Get-Sha256 $_.FullName), $_.Name }
    Write-Text (Join-Path $packages 'SHA256SUMS.txt') (($sums -join "`n") + "`n")
    $sums = Get-ChildItem -LiteralPath $out -Recurse -File | Where-Object { $_.FullName -notlike (Join-Path $packages '*') -and $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object FullName |
        ForEach-Object { '{0} *{1}' -f (Get-Sha256 $_.FullName), $_.FullName.Substring($out.Length + 1).Replace([char]92, [char]47) }
    Write-Text (Join-Path $out 'SHA256SUMS.txt') (($sums -join "`n") + "`n")

    Write-Host "PASS packaged CYDpod $Version ($tag at $commit) into releases/$Version"
    Get-ChildItem -LiteralPath $packages -File | Sort-Object Name | ForEach-Object { Write-Host ('  {0,12:N0}  {1}' -f $_.Length, $_.Name) }
} finally { Pop-Location }
