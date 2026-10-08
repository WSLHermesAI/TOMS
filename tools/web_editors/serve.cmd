@echo off
rem serve.cmd -- serve the TOMS folder on http://localhost:8000 and open the editors (event + stage, switch with the ⇄ button).
rem Over http://localhost, Chrome and Edge allow "Open project folder", so the stage editor's Save writes straight into assets/data.
cd /d "%~dp0..\.."
start "" "http://localhost:8000/tools/web_editors/index.html"
python -m http.server 8000
