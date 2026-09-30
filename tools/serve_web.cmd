@echo off
rem serve_web.cmd [release|debug|mt|dist] [port] -- serve the web build over http and open it.
rem   release/debug  the builds of tools\build_web.cmd     mt    the multithreaded build
rem   dist           the published package of build_web.bat (both builds; the page picks one)
rem Browsers refuse to run WebAssembly from file://, so a local web server is required. This uses
rem the Python that ships inside emsdk (no separate Python install needed) and tools\serve_web.py,
rem which also sends the cross-origin-isolation headers threads need. Ctrl+C stops it.
setlocal EnableExtensions
set "KIND=%~1"
if "%KIND%"=="" set "KIND=release"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=8099"
set "WEBDIR=%~dp0..\build-web-%KIND%-windows\bin"
set "PAGE=toms_game.html"
if /i "%KIND%"=="mt" set "WEBDIR=%~dp0..\build-web-release-mt-windows\bin"
if /i "%KIND%"=="dist" set "WEBDIR=%~dp0..\dist\TOMS-web"
if /i "%KIND%"=="dist" set "PAGE=index.html"
if not exist "%WEBDIR%\%PAGE%" (
  echo [toms] %WEBDIR%\%PAGE% not found. Build it first: tools\build_web.cmd %KIND%  ^(dist: build_web.bat^)
  exit /b 1
)

set "EMSDK_DIR="
if defined EMSDK if exist "%EMSDK%\emsdk_env.bat" set "EMSDK_DIR=%EMSDK%"
if not defined EMSDK_DIR if exist "%~dp0..\..\..\emsdk\emsdk_env.bat" set "EMSDK_DIR=%~dp0..\..\..\emsdk"
if not defined EMSDK_DIR if exist "%USERPROFILE%\emsdk\emsdk_env.bat" set "EMSDK_DIR=%USERPROFILE%\emsdk"
if not defined EMSDK_DIR if exist "C:\emsdk\emsdk_env.bat" set "EMSDK_DIR=C:\emsdk"
if defined EMSDK_DIR call "%EMSDK_DIR%\emsdk_env.bat" >nul 2>&1

set "PY=%EMSDK_PYTHON%"
if not defined PY set "PY=python"
echo [toms] serving %WEBDIR% at http://localhost:%PORT%/%PAGE%  (Ctrl+C to stop)
start "" "http://localhost:%PORT%/%PAGE%"
"%PY%" "%~dp0serve_web.py" "%WEBDIR%" %PORT%
