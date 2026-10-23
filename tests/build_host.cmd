@echo off
setlocal
set "CYDPOD_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CYDPOD_VSWHERE%" (
  echo ERROR: installed Visual Studio C++ tools and vswhere.exe required; no compiler is installed by this check.
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%CYDPOD_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CYDPOD_VS_ROOT=%%I"
if not defined CYDPOD_VS_ROOT (
  echo ERROR: no installed Visual Studio x86/x64 C++ toolchain found.
  exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%CYDPOD_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if not "%errorlevel%"=="0" exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cd out
cl /nologo /O2 /EHsc /std:c++17 /Fe:audio_core_test.exe ..\audio_core_test.cpp
if not "%errorlevel%"=="0" exit /b 1
.\audio_core_test.exe
if not "%errorlevel%"=="0" exit /b 1
if not exist codec mkdir codec
cl /nologo /O2 /TC /c /Focodec\ /DUSE_DEFAULT_STDLIB /DCYDPOD_AAC_LC_ONLY /I..\..\.deps\sketchbook\libraries\arduino-libhelix\src ..\..\.deps\sketchbook\libraries\arduino-libhelix\src\libhelix-aac\*.c
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /DCYDPOD_AAC_LC_ONLY /I..\..\.deps\sketchbook\libraries\arduino-libhelix\src /Fe:helix_decode.exe ..\helix_decode.cpp codec\*.obj
exit /b %errorlevel%
