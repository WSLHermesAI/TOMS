@echo off
rem build_web.cmd [release|debug] -- build the browser version of toms_game on Windows.
rem   Output: toms_next\build-web-<release|debug>-windows\bin\toms_game.html (+ .js .wasm .data)
rem   Then run tools\serve_web.cmd to play it in the browser.
rem Needs: emsdk (Emscripten), Visual Studio's CMake + Ninja (or both on PATH), and a host
rem shaderc (built automatically from the desktop shipping preset if there is none yet).
setlocal EnableExtensions
set "KIND=%~1"
if "%KIND%"=="" set "KIND=release"
if /i not "%KIND%"=="release" if /i not "%KIND%"=="debug" (
  echo usage: build_web.cmd [release^|debug]
  exit /b 2
)
set "PRESET=web-%KIND%-windows"
cd /d "%~dp0.."

rem ---- 1. Emscripten SDK ----
set "EMSDK_DIR="
if defined EMSDK if exist "%EMSDK%\emsdk_env.bat" set "EMSDK_DIR=%EMSDK%"
if not defined EMSDK_DIR if exist "%~dp0..\..\..\..\emsdk\emsdk_env.bat" set "EMSDK_DIR=%~dp0..\..\..\..\emsdk"
if not defined EMSDK_DIR if exist "%USERPROFILE%\emsdk\emsdk_env.bat" set "EMSDK_DIR=%USERPROFILE%\emsdk"
if not defined EMSDK_DIR if exist "C:\emsdk\emsdk_env.bat" set "EMSDK_DIR=C:\emsdk"
if not defined EMSDK_DIR if exist "D:\emsdk\emsdk_env.bat" set "EMSDK_DIR=D:\emsdk"
if not defined EMSDK_DIR goto :noemsdk
call "%EMSDK_DIR%\emsdk_env.bat" >nul 2>&1
where emcc >nul 2>&1 || goto :noemsdk
echo [toms] Emscripten: %EMSDK_DIR%

rem ---- 2. CMake + Ninja (Visual Studio's copies, else PATH) ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath`) do set "VSDIR=%%i"
if defined VSDIR set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
where cmake >nul 2>&1 || goto :nocmake
where ninja >nul 2>&1 || goto :nocmake

rem ---- 3. Host shaderc (the web build compiles shaders on this machine) ----
set "HOST_SHADERC="
for /f "delims=" %%f in ('dir /b /s "out\build\shaderc.exe" 2^>nul') do if not defined HOST_SHADERC set "HOST_SHADERC=%%f"
if not defined HOST_SHADERC (
  echo [toms] No host shaderc yet: building the desktop shipping preset once to get one...
  call "%~dp0build.cmd" windows-shipping || exit /b 1
  for /f "delims=" %%f in ('dir /b /s "out\build\shaderc.exe" 2^>nul') do if not defined HOST_SHADERC set "HOST_SHADERC=%%f"
)
if not defined HOST_SHADERC goto :noshaderc
echo [toms] host shaderc: %HOST_SHADERC%

rem ---- 4. Configure + build ----
set "POPUPS=ON"
if "%TOMS_NO_POPUPS%"=="1" set "POPUPS=OFF"
cmake --preset "%PRESET%" -DTOMS_HOST_SHADERC="%HOST_SHADERC%" -DTOMS_POPUP_WARNINGS=%POPUPS%
if errorlevel 1 exit /b 1
cmake --build --preset "%PRESET%"
if errorlevel 1 exit /b 1
echo.
echo [toms] done: %CD%\build-web-%KIND%-windows\bin\toms_game.html
echo [toms] play it: tools\serve_web.cmd %KIND%
exit /b 0

:noemsdk
echo [toms] Emscripten SDK (emsdk) not found. Install it, or set EMSDK to its folder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0show_message.ps1" -Title "TOMS: Emscripten SDK not found" -Message "The web build needs the Emscripten SDK (emsdk). Install: git clone https://github.com/emscripten-core/emsdk.git, then in that folder run: emsdk install latest, then: emsdk activate latest. Then set EMSDK to that folder (or put it at %USERPROFILE%\emsdk or C:\emsdk)." -Links "https://emscripten.org/docs/getting_started/downloads.html"
exit /b 1
:nocmake
echo [toms] CMake/Ninja not found. Install Visual Studio's "C++ CMake tools for Windows".
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0show_message.ps1" -Title "TOMS: CMake or Ninja not found" -Message "Install the Visual Studio component 'C++ CMake tools for Windows' (it contains CMake and Ninja), or put cmake and ninja on PATH." -Links "https://learn.microsoft.com/cpp/build/cmake-projects-in-visual-studio"
exit /b 1
:noshaderc
echo [toms] Could not get a host shaderc. Build a desktop preset first: tools\build.cmd windows-shipping
exit /b 1
