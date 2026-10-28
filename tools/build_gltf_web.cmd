@echo off
rem build_gltf_web.cmd [release|debug] -- build the glTF viewer for the browser (tools\gltf_viewer, WebGL2, single-threaded).
rem   Output: Build\web-<release|debug>-windows\bin\gltf_viewer.html (+ .js .wasm .data: the test models of tests\gltf)
rem   It shares the game's web build folder and setup (tools\build_web.cmd), but builds only gltf_viewer.
rem   Then run tools\serve_web.cmd gltf (or gltf-debug) to open it in the browser.
setlocal EnableExtensions
set "KIND=%~1"
if "%KIND%"=="" set "KIND=release"
if /i not "%KIND%"=="release" if /i not "%KIND%"=="debug" (
  echo usage: build_gltf_web.cmd [release^|debug]
  exit /b 2
)
call "%~dp0build_web.cmd" %KIND% gltf_viewer || exit /b 1
if /i "%KIND%"=="release" (echo [toms] open it: tools\serve_web.cmd gltf) else (echo [toms] open it: tools\serve_web.cmd gltf-debug)
exit /b 0
