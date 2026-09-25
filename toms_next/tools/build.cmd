@echo off
rem build.cmd [preset] -- configure + build toms_next from a plain command prompt.
rem   Presets: windows-debug, windows-release (default), windows-shipping, ci-windows
rem Finds Visual Studio with vswhere, loads the MSVC x64 environment, and uses the CMake and
rem Ninja that ship with Visual Studio. Inside Visual Studio you do not need this script.
setlocal EnableExtensions
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-release"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" goto :novs

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :novs
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%PATH%"

cd /d "%~dp0.."
echo [toms] Visual Studio: %VSDIR%
echo [toms] preset: %PRESET%
cmake --preset "%PRESET%"
if errorlevel 1 exit /b 1
cmake --build --preset "%PRESET%"
if errorlevel 1 exit /b 1
echo [toms] done. Executables: %CD%\out\build\%PRESET%\bin
exit /b 0

:novs
echo [toms] Visual Studio with "Desktop development with C++" was not found.
echo [toms] Run tools\check_env.cmd for details and download links.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0show_message.ps1" -Title "TOMS: Visual Studio not found" -Message "Visual Studio 2022 or newer with the workload 'Desktop development with C++' is required." -Links "https://visualstudio.microsoft.com/downloads/"
exit /b 1
