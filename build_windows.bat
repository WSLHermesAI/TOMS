@echo off
rem build_windows.bat -- build the Windows game and package it.
rem   Result: Build\dist\TOMS-windows\toms_game.exe (+ assets\, runtime DLLs)  and  Build\dist\TOMS-windows.zip
rem   Copy Build\dist\TOMS-windows anywhere and double-click toms_game.exe.
rem Double-click this file, or run it from a command prompt. Missing tools pop up with download links.
setlocal EnableExtensions
cd /d "%~dp0"
echo [toms] building the Windows game (preset windows-shipping: Release, no editor, no Qt)...
call tools\build.cmd windows-shipping
if errorlevel 1 goto :failed
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1 -Target windows
if errorlevel 1 goto :failed
echo.
echo [toms] done. Double-click Build\dist\TOMS-windows\toms_game.exe to play.
if /i not "%~1"=="nopause" pause
exit /b 0

:failed
echo.
echo [toms] FAILED -- see the messages above (and tools\check_env.cmd for missing tools).
if /i not "%~1"=="nopause" pause
exit /b 1
