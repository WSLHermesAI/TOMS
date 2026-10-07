@echo off
rem serve.cmd -- serve the TOMS folder on http://localhost:8000 and open the stage editor.
rem Over http://localhost, Chrome and Edge allow "Open project folder", so Save writes straight into assets/data.
cd /d "%~dp0..\..\.."
start "" "http://localhost:8000/tools/stage_editor/web/stage_editor.html"
python -m http.server 8000
