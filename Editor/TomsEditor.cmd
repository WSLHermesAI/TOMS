@echo off
rem TomsEditor.cmd -- the game in an editor window, with the repo's assets (Editor\README.md)
set "ASSET_DIR=%~dp0..\assets\media"
start "" "%~dp0bin\toms_editor.exe" %*
