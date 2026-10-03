@echo off
rem build_atlas_web.cmd [release|debug] -- build the web atlas tool (tools\atlas\web) with Emscripten.
rem   Output: Build\atlas-web\site\ (index.html, atlas_web.js/.wasm, atlaspack.mjs)
rem   Serve it: python tools\serve_web.py Build\atlas-web\site 8098   then open http://localhost:8098/
rem Needs: emsdk (Emscripten) and Visual Studio's CMake + Ninja (or both on PATH).
setlocal EnableExtensions
set "KIND=%~1"
if "%KIND%"=="" set "KIND=release"
set "BTYPE="
if /i "%KIND%"=="release" set "BTYPE=Release"
if /i "%KIND%"=="debug" set "BTYPE=Debug"
if not defined BTYPE (
  echo usage: build_atlas_web.cmd [release^|debug]
  exit /b 2
)
set "OUTDIR=Build\atlas-web"
if /i "%KIND%"=="debug" set "OUTDIR=Build\atlas-web-debug"
cd /d "%~dp0.."

rem ---- 1. Emscripten SDK ----
set "EMSDK_DIR="
if defined EMSDK if exist "%EMSDK%\emsdk_env.bat" set "EMSDK_DIR=%EMSDK%"
if not defined EMSDK_DIR if exist "%~dp0..\..\..\emsdk\emsdk_env.bat" set "EMSDK_DIR=%~dp0..\..\..\emsdk"
if not defined EMSDK_DIR if exist "%USERPROFILE%\emsdk\emsdk_env.bat" set "EMSDK_DIR=%USERPROFILE%\emsdk"
if not defined EMSDK_DIR if exist "C:\emsdk\emsdk_env.bat" set "EMSDK_DIR=C:\emsdk"
if not defined EMSDK_DIR if exist "D:\emsdk\emsdk_env.bat" set "EMSDK_DIR=D:\emsdk"
if not defined EMSDK_DIR goto :noemsdk
call "%EMSDK_DIR%\emsdk_env.bat" >nul 2>&1
where emcc >nul 2>&1 || goto :noemsdk
echo [atlas] Emscripten: %EMSDK_DIR%

rem ---- 2. CMake + Ninja (Visual Studio's copies, else PATH) ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath`) do set "VSDIR=%%i"
if defined VSDIR set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
where cmake >nul 2>&1 || goto :nocmake
where ninja >nul 2>&1 || goto :nocmake

rem ---- 3. Configure + build the standalone tools\atlas project ----
call emcmake cmake -S tools\atlas -B "%OUTDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BTYPE%
if errorlevel 1 exit /b 1
cmake --build "%OUTDIR%" --target atlas_web
if errorlevel 1 exit /b 1
echo.
echo [atlas] done: %CD%\%OUTDIR%\site\index.html
echo [atlas] serve it:  python tools\serve_web.py %OUTDIR%\site 8098   then open http://localhost:8098/
echo [atlas] headless:  node %OUTDIR%\site\atlaspack.mjs pack assets\media\sprites --out Build\atlas-out
exit /b 0

:noemsdk
echo [atlas] Emscripten SDK (emsdk) not found. Install it, or set EMSDK to its folder.
echo [atlas] https://emscripten.org/docs/getting_started/downloads.html
exit /b 1
:nocmake
echo [atlas] CMake/Ninja not found. Install Visual Studio's "C++ CMake tools for Windows".
exit /b 1
