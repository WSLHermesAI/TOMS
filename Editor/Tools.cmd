@echo off
rem Tools.cmd -- a command prompt in the repo folder with atlaspack, fontpack and the editors on PATH
rem (their --headless commands: Editor\docs\ai\README.md).
set "PATH=%~dp0bin;%PATH%"
cd /d "%~dp0.."
echo atlaspack, fontpack and the editors (anim_editor --headless ...) are on PATH. Type "exit" to close.
cmd /k
