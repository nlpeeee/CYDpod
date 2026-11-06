<#
.SYNOPSIS
  Cut a CYDpod site release: copy the newest merged flash image into docs/firmware/,
  write docs/flash/manifest.json for ESP Web Tools, and update the version text on
  docs/index.html and README.md. Offline; never opens a serial port or touches a device.

.DESCRIPTION
  Input is ONE merged 4 MB image flashed at offset 0 (cydpod-v<N>-w328c.bin), as produced by
  the INT export step. The four separate region files (bootloader, partitions, boot_app0, app)
  are copied to docs/firmware/regions/ when they sit next to the image, for the manual
  "keep my settings" flash.

  Pages copies are release artefacts: docs/firmware/*.bin is committed at release time only.

.EXAMPLE
  powershell -NoProfile -File tools/make_site_release.ps1
  powershell -NoProfile -File tools/make_site_release.ps1 -Source E:\flash\v34\cydpod-v34-w328c.bin
  powershell -NoProfile -File tools/make_site_release.ps1 -DocsDir $env:TEMP\site-test\docs   # dry run elsewhere
#>
[CmdletBinding()]
param(
  [string]$Source,                 # merged image; default: newest E:\flash\v*\cydpod-v*-w328c.bin
  [string]$Version,                # default: parsed from the file name
  [string]$DocsDir,                # default: <repo>\docs
  [string]$RegionsDir,             # default: folder of -Source
  [string]$FlashRoot = 'E:\flash',
  [long]$ExpectedBytes = 4194304   # 4 MB merged image
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $DocsDir) { $DocsDir = Join-Path $repo 'docs' }

# ---- locate the image ------------------------------------------------------------------------
if (-not $Source) {
  $cands = @(Get-ChildItem -Path $FlashRoot -Recurse -Filter 'cydpod-v*-w328c.bin' -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^cydpod-v(\d+)-w328c\.bin$' } |
    Sort-Object { [int]([regex]::Match($_.Name, '^cydpod-v(\d+)-').Groups[1].Value) } -Descending)
  if ($cands.Count -eq 0) { throw "No cydpod-v*-w328c.bin found under $FlashRoot; pass -Source." }
  $Source = $cands[0].FullName
}
if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Image not found: $Source" }
$srcItem = Get-Item -LiteralPath $Source
$name = $srcItem.Name
if (-not $Version) {
  $m = [regex]::Match($name, '^cydpod-v([0-9A-Za-z.\-]+?)-w328c\.bin$')
  if (-not $m.Success) { throw "Cannot read the version from '$name' (expected cydpod-v<N>-w328c.bin); pass -Version." }
  $Version = $m.Groups[1].Value
}
if (-not $RegionsDir) { $RegionsDir = $srcItem.DirectoryName }

# ---- sanity checks on the image ---------------------------------------------------------------
if ($srcItem.Length -ne $ExpectedBytes) { throw "Image is $($srcItem.Length) bytes, expected $ExpectedBytes (a merged 4 MB image)." }
$fs = [System.IO.File]::OpenRead($srcItem.FullName)
try {
  $b = New-Object byte[] 1
  foreach ($probe in @(@{ At = 0x1000; Want = 0xE9; What = 'bootloader' }, @{ At = 0x10000; Want = 0xE9; What = 'app image' })) {
    [void]$fs.Seek($probe.At, 'Begin'); [void]$fs.Read($b, 0, 1)
    if ($b[0] -ne $probe.Want) { throw ("No ESP image header (0xE9) at 0x{0:x} ({1}); this does not look like a merged image flashed at 0." -f $probe.At, $probe.What) }
  }
} finally { $fs.Dispose() }

$hash = (Get-FileHash -LiteralPath $srcItem.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$sizeMb = [math]::Round($srcItem.Length / 1MB, 1)
$sizeText = if ($sizeMb -eq [math]::Floor($sizeMb)) { '{0:0} MB' -f $sizeMb } else { '{0:0.0} MB' -f $sizeMb }
$date = Get-Date -Format 'yyyy-MM-dd'

# ---- docs/firmware -----------------------------------------------------------------------------
$fwDir = Join-Path $DocsDir 'firmware'
New-Item -ItemType Directory -Force -Path $fwDir | Out-Null
Get-ChildItem -LiteralPath $fwDir -Filter 'cydpod-v*-w328c.bin' -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -ne $name } | Remove-Item -Force
Copy-Item -LiteralPath $srcItem.FullName -Destination (Join-Path $fwDir $name) -Force

$regionNames = 'app.ino.bootloader.bin', 'app.ino.partitions.bin', 'boot_app0.bin', 'app.ino.bin'
$haveRegions = @($regionNames | Where-Object { Test-Path -LiteralPath (Join-Path $RegionsDir $_) }).Count -eq $regionNames.Count
if ($haveRegions) {
  $regDir = Join-Path $fwDir 'regions'
  New-Item -ItemType Directory -Force -Path $regDir | Out-Null
  foreach ($r in $regionNames) { Copy-Item -LiteralPath (Join-Path $RegionsDir $r) -Destination (Join-Path $regDir $r) -Force }
} else {
  Write-Warning "Region files not all found in $RegionsDir; docs/firmware/regions/ not updated (the manual keep-settings flash needs them)."
}

# ---- docs/flash/manifest.json (ESP Web Tools; paths are relative to the manifest) -----------------
$flashDir = Join-Path $DocsDir 'flash'
New-Item -ItemType Directory -Force -Path $flashDir | Out-Null
$manifest = [ordered]@{
  name    = 'CYDpod'
  version = [string]$Version
  builds  = @([ordered]@{
      chipFamily = 'ESP32'
      parts      = @([ordered]@{ path = "../firmware/$name"; offset = 0 })
    })
}
$json = $manifest | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText((Join-Path $flashDir 'manifest.json'), $json + "`n", (New-Object System.Text.UTF8Encoding($false)))

# ---- version text: <!--rel:NAME-->value<!--/rel--> in index.html and README.md ------------------------
$values = @{ version = [string]$Version; file = $name; sha256 = $hash; size = $sizeText; date = $date }
function Set-Release([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $false }
  $text = [System.IO.File]::ReadAllText($path)
  $new = [regex]::Replace($text, '<!--rel:(\w+)-->.*?<!--/rel-->', {
      param($m)
      $k = $m.Groups[1].Value
      if ($values.ContainsKey($k)) { '<!--rel:' + $k + '-->' + $values[$k] + '<!--/rel-->' } else { $m.Value }
    }, 'Singleline')
  if ($new -ne $text) { [System.IO.File]::WriteAllText($path, $new, (New-Object System.Text.UTF8Encoding($false))) }
  return $true
}
$touched = @()
foreach ($p in @((Join-Path $DocsDir 'index.html'), (Join-Path (Split-Path -Parent $DocsDir) 'README.md'))) {
  if (Set-Release $p) { $touched += $p }
}

"RELEASE_OK version=$Version file=$name bytes=$($srcItem.Length) sha256=$hash"
"  manifest : $(Join-Path $flashDir 'manifest.json')"
"  firmware : $(Join-Path $fwDir $name)$(if ($haveRegions) { ' (+ regions/)' })"
"  text     : $(if ($touched.Count) { $touched -join ', ' } else { '(no index.html/README.md found)' })"
"Next: review 'git status', commit docs/firmware, docs/flash, docs/index.html, README.md. Not pushed."
