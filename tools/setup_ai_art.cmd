@echo off
rem Double-click me: set up the optional AI art tools (ComfyUI) for TOMS --
rem either install ComfyUI + models on this PC, or use a ComfyUI server on another machine.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_ai_art.ps1" %*
set RC=%ERRORLEVEL%
pause
exit /b %RC%
