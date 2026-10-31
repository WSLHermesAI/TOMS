@echo off
rem TOMS_Launcher.bat -- one window for every build, deploy, test and setup action (tools\launcher.ps1).
rem   Double-click it, pick an action on the left, read what it does and where the result goes, press Run.
rem   Each action runs in its own console window; the launcher lists what ran and whether it passed.
start "" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0tools\launcher.ps1" %*
