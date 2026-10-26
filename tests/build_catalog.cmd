@echo off
setlocal
set "CYDPOD_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CYDPOD_VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%I in (`"%CYDPOD_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CYDPOD_VS_ROOT=%%I"
if not defined CYDPOD_VS_ROOT exit /b 1
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%CYDPOD_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cd out
ffmpeg -v error -y -i ..\..\fixtures\stereo-faststart.m4a -c copy -metadata title="Actual tagged fixture" -metadata artist="Fixture Artist" -metadata album="Fixture Album" -metadata track="2/3" metadata-real.m4a
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /W4 /std:c++17 /Fe:pairing_session_test.exe ..\pairing_session_test.cpp
if errorlevel 1 exit /b 1
.\pairing_session_test.exe
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /W4 /std:c++17 /I..\speaker_shim /Fe:speaker_lifecycle_test.exe ..\speaker_lifecycle_test.cpp
if errorlevel 1 exit /b 1
.\speaker_lifecycle_test.exe
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /W4 /std:c++17 /DCYDPOD_CATALOG_HOST /I..\catalog_shim /I..\..\firmware\cydpod_player\app /Fe:catalog_test.exe ..\catalog_test.cpp ..\..\firmware\cydpod_player\app\storage.cpp ..\..\firmware\cydpod_player\app\playlist.cpp
if errorlevel 1 exit /b 1
.\catalog_test.exe
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /W4 /std:c++17 /DCYDPOD_CATALOG_HOST /I..\catalog_shim /I..\..\firmware\cydpod_player\app /Fe:classic_ui_test.exe ..\classic_ui_test.cpp ..\..\firmware\cydpod_player\app\storage.cpp ..\..\firmware\cydpod_player\app\playlist.cpp ..\..\firmware\cydpod_player\app\ui.cpp ..\..\firmware\cydpod_player\app\classic_screen.cpp ..\..\firmware\cydpod_player\app\sleep_mode.cpp ..\..\firmware\cydpod_player\app\theme.cpp ..\..\firmware\cydpod_player\app\style.cpp ..\..\firmware\cydpod_player\app\i18n.cpp
if errorlevel 1 exit /b 1
.\classic_ui_test.exe
exit /b %errorlevel%
