# Installs only local, hash-checked build dependencies. Never connects to a board.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $python = Get-Command python -ErrorAction Stop
    & $python.Source (Join-Path $PSScriptRoot 'bootstrap.py')
    if ($LASTEXITCODE -ne 0) { throw "Dependency setup failed ($LASTEXITCODE)." }
} finally { Pop-Location }
