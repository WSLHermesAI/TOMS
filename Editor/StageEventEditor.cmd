@echo off
rem StageEventEditor.cmd -- the stage and event editors as one Windows app, on the repo's files (Editor\README.md)
start "" "%~dp0bin\stage_event_editor.exe" "--root=%~dp0.." %*
