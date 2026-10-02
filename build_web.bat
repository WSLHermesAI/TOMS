@echo off
rem build_web.bat -- build the web game and package it for a web server.
rem   Result: Build\dist\TOMS-web\ (index.html, the single- and multithreaded game files,
rem           coi-serviceworker.js, .htaccess, web.config)
rem           and Build\dist\TOMS-web.zip
rem   Copy the files of Build\dist\TOMS-web to any web server and open its URL.
rem Needs the Emscripten SDK (emsdk). Missing tools pop up with download links.
setlocal EnableExtensions
cd /d "%~dp0"
echo [toms] building the web game (preset web-release-windows)...
call tools\build_web.cmd release
if errorlevel 1 goto :failed
rem The multithreaded variant: the page uses it where the browser allows threads (docs\10_THREADS.md).
echo [toms] building the multithreaded web game (preset web-release-mt-windows)...
call tools\build_web.cmd mt
if errorlevel 1 goto :failed
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1 -Target web
if errorlevel 1 goto :failed
echo.
echo [toms] done. Upload the files of Build\dist\TOMS-web to a web server.
echo [toms] To try it locally: tools\serve_web.cmd dist   (serves the package and opens the browser)
if /i not "%~1"=="nopause" pause
exit /b 0

:failed
echo.
echo [toms] FAILED -- see the messages above (and tools\check_env.cmd for missing tools).
if /i not "%~1"=="nopause" pause
exit /b 1
