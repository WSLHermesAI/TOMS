@echo off
rem serve_web.cmd [release|debug] [port] -- serve the web build over http and open it in the browser.
rem Browsers refuse to run WebAssembly from file://, so a local web server is required. This uses
rem the Python that ships inside emsdk (no separate Python install needed). Ctrl+C stops it.
setlocal EnableExtensions
set "KIND=%~1"
if "%KIND%"=="" set "KIND=release"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=8099"
set "WEBDIR=%~dp0..\build-web-%KIND%-windows\bin"
if not exist "%WEBDIR%\toms_game.html" (
  echo [toms] %WEBDIR%\toms_game.html not found. Build it first: tools\build_web.cmd %KIND%
  exit /b 1
)

set "EMSDK_DIR="
if defined EMSDK if exist "%EMSDK%\emsdk_env.bat" set "EMSDK_DIR=%EMSDK%"
if not defined EMSDK_DIR if exist "%~dp0..\..\..\..\emsdk\emsdk_env.bat" set "EMSDK_DIR=%~dp0..\..\..\..\emsdk"
if not defined EMSDK_DIR if exist "%USERPROFILE%\emsdk\emsdk_env.bat" set "EMSDK_DIR=%USERPROFILE%\emsdk"
if not defined EMSDK_DIR if exist "C:\emsdk\emsdk_env.bat" set "EMSDK_DIR=C:\emsdk"
if defined EMSDK_DIR call "%EMSDK_DIR%\emsdk_env.bat" >nul 2>&1

set "PY=%EMSDK_PYTHON%"
if not defined PY set "PY=python"
echo [toms] serving %WEBDIR% at http://localhost:%PORT%/toms_game.html  (Ctrl+C to stop)
start "" "http://localhost:%PORT%/toms_game.html"
"%PY%" -m http.server %PORT% --bind 127.0.0.1 --directory "%WEBDIR%"
