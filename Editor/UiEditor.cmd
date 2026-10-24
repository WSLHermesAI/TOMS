@echo off
rem UiEditor.cmd -- the game's UI files (.rml / .rcss) beside the running game, with the repo's assets (Editor\README.md)
set "ASSET_DIR=%~dp0..\assets\media"
start "" "%~dp0bin\ui_editor.exe" %*
