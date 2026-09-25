@echo off
rem Double-click me: checks every prerequisite of toms_next and pops up download links for
rem anything missing. Pass -NoGui for console output only.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check_env.ps1" %*
set RC=%ERRORLEVEL%
if "%~1"=="" pause
exit /b %RC%
