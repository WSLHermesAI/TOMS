@echo off
rem build_ocean_web.cmd [release|debug] -- build the ocean viewer for the browser (tools\ocean_viewer, WebGL2, single-threaded).
rem   Output: Build\web-<release|debug>-windows\bin\ocean_viewer.html (+ .js .wasm)
rem   It shares the game's web build folder and setup (tools\build_web.cmd), but builds only ocean_viewer.
rem   Then run tools\serve_web.cmd ocean (or ocean-debug) to open it in the browser.
setlocal EnableExtensions
set "KIND=%~1"
if "%KIND%"=="" set "KIND=release"
if /i not "%KIND%"=="release" if /i not "%KIND%"=="debug" (
  echo usage: build_ocean_web.cmd [release^|debug]
  exit /b 2
)
call "%~dp0build_web.cmd" %KIND% ocean_viewer || exit /b 1
if /i "%KIND%"=="release" (echo [toms] open it: tools\serve_web.cmd ocean) else (echo [toms] open it: tools\serve_web.cmd ocean-debug)
exit /b 0
