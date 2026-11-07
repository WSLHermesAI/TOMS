@echo off
rem Double-click me: downloads ffmpeg into tools\ffmpeg\ (about 170 MB; not in git), for the tools that write OGG sound.
rem   get_ffmpeg.cmd -NoGui   for scripts: no message box, exit 0 when ffmpeg is ready, 1 when not
rem   get_ffmpeg.cmd -Force   downloads again
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0get_ffmpeg.ps1" %*
exit /b %ERRORLEVEL%
