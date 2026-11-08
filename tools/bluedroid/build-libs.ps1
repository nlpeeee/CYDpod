<#
Rebuild the ESP-IDF libraries that Arduino-ESP32 3.3.10 ships for the esp32,
twice in the same directory: once with Espressif's exact shipped sdkconfig
("stock") and once with that sdkconfig plus defconfig.cydpod-bt ("trim").
Both builds use one path, so build paths embedded in debug information match
and only libraries the Bluetooth options really touch differ. Outputs are
saved to .tools/bt-build/out/<variant> for make-libs.py
(see docs/specs/trimmed-bluedroid.md).

Inputs: ESP-IDF v5.5.4 (735507283d) in .tools/esp-idf-5.5.4, the pinned
xtensa toolchain from .deps, cmake/ninja from Visual Studio Build Tools and the
Python venv in .tools/idf-venv (ESP-IDF requirements.core.txt).
#>
param([string]$Root = (Resolve-Path "$PSScriptRoot\..\..").Path, [string[]]$Variants = @('stock', 'trim'))
$ErrorActionPreference = 'Stop'
$idf = Join-Path $Root '.tools\esp-idf-5.5.4'
$venv = Join-Path $Root '.tools\idf-venv'
$libs = Join-Path $Root '.deps\arduino\packages\esp32\tools\esp32-libs\3.3.10'
if (Test-Path "$libs.stock") { $libs = "$libs.stock" }
$work = Join-Path $Root '.tools\bt-build'
$p = Join-Path $work 'project'
$vs = (& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath)
$cm = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$env:IDF_PATH = $idf
$env:IDF_PYTHON_ENV_PATH = $venv
$env:IDF_COMPONENT_MANAGER = '0'
$env:IDF_TARGET = 'esp32'
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue  # idf.py refuses MSys shells
$env:PATH = "$venv\Scripts;$Root\.deps\arduino\packages\esp32\tools\esp-x32\2601\bin;$cm\CMake\bin;$cm\Ninja;$env:PATH"
foreach ($variant in $Variants) {
    if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    New-Item -ItemType Directory -Force (Join-Path $p 'main') | Out-Null
    Set-Content -Encoding ascii (Join-Path $p 'CMakeLists.txt') "cmake_minimum_required(VERSION 3.16)`ninclude(`$ENV{IDF_PATH}/tools/cmake/project.cmake)`nproject(cydbt)`n"
    Set-Content -Encoding ascii (Join-Path $p 'main\CMakeLists.txt') "idf_component_register(SRCS `"main.c`")`n"
    Set-Content -Encoding ascii (Join-Path $p 'main\main.c') "void app_main(void) {}`n"
    # Fixed version strings: otherwise git describe of this repository and of a
    # patched (dirty) ESP-IDF tree make version-only differences between builds.
    Set-Content -Encoding ascii (Join-Path $p 'version.txt') 'cydbt'
    Set-Content -Encoding ascii (Join-Path $idf 'version.txt') 'v5.5.4'
    $cfg = Get-Content (Join-Path $libs 'sdkconfig')
    if ($variant -eq 'trim') {
        $overlay = Get-Content (Join-Path $PSScriptRoot 'defconfig.cydpod-bt') | Where-Object { $_ -match '^CONFIG_' }
        $keys = $overlay | ForEach-Object { ($_ -split '=')[0] }
        $cfg = ($cfg | Where-Object { $line = $_; -not ($keys | Where-Object { $line -match "^(# )?$_(=| is not set)" }) }) + $overlay
    }
    Set-Content -Encoding ascii (Join-Path $p 'sdkconfig') $cfg
    # The trim build also carries CYDpod's Bluedroid source patches; the stock
    # build stays pristine so make-libs.py sees exactly what they change.
    $patches = @()
    if ($variant -eq 'trim') { $patches = @(Get-ChildItem (Join-Path $PSScriptRoot 'patches\*.patch') | Sort-Object Name) }
    foreach ($patch in $patches) { git -C $idf apply $patch.FullName; if ($LASTEXITCODE -ne 0) { throw "patch failed: $($patch.Name)" } }
    Push-Location $p
    try {
        # ESP-IDF's CMake build directly: idf.py would demand the installer's constraints file.
        cmd /c "cmake -S . -B build -G Ninja -DIDF_TARGET=esp32 -DPYTHON_DEPS_CHECKED=1 -DPYTHON=`"$venv\Scripts\python.exe`" -DSDKCONFIG=`"$p\sdkconfig`" > `"$work\$variant-configure.log`" 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "$variant configure failed" }
        cmd /c "ninja -C build > `"$work\$variant-build.log`" 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "$variant build failed" }
    } finally {
        Pop-Location
        [array]::Reverse($patches)
        foreach ($patch in $patches) { git -C $idf apply -R $patch.FullName }
        Remove-Item -Force (Join-Path $idf 'version.txt')
    }
    $out = Join-Path $work "out\$variant"
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    New-Item -ItemType Directory -Force (Join-Path $out 'lib'), (Join-Path $out 'ld'), (Join-Path $out 'config') | Out-Null
    Get-ChildItem (Join-Path $p 'build\esp-idf') -Recurse -Filter *.a | ForEach-Object { Copy-Item $_.FullName (Join-Path $out 'lib') }
    Copy-Item (Join-Path $p 'build\esp-idf\esp_system\ld\*.ld') (Join-Path $out 'ld')
    Copy-Item (Join-Path $p 'build\config\sdkconfig.h') (Join-Path $out 'config')
    Copy-Item (Join-Path $p 'sdkconfig') $out
}
"PASS ESP-IDF builds complete: $($Variants -join ', ')"
