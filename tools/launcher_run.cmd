@echo off
rem launcher_run.cmd -- runs one action of tools\launcher.ps1 (TOMS_Launcher.bat) in its own console window.
rem   The launcher sets TOMS_RUN_TITLE (the action's name), TOMS_RUN_CMD (the command line, from the repo root)
rem   and TOMS_RUN_PAUSE: 1 pause at the end, 0 never (the command pauses on its own), fail only when it failed (an
rem   action of a chain, so the next one starts without a key press). Exit code: the command's.
title TOMS - %TOMS_RUN_TITLE%
cd /d "%~dp0.."
echo [toms] %TOMS_RUN_TITLE%
echo [toms] ^> %TOMS_RUN_CMD%
echo.
call %TOMS_RUN_CMD%
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo [toms] %TOMS_RUN_TITLE%: finished OK.) else (echo [toms] %TOMS_RUN_TITLE%: FAILED, exit code %RC%.)
if "%TOMS_RUN_PAUSE%"=="1" pause
if "%TOMS_RUN_PAUSE%"=="fail" if not "%RC%"=="0" pause
exit /b %RC%
