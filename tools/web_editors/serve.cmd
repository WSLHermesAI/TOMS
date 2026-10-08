@echo off
rem serve.cmd -- refresh the editors' data from assets/data, serve the TOMS folder on http://localhost:8000
rem and open the editors (event + stage, switch with the button at the top).
rem Over http://localhost, Chrome and Edge allow "Open project folder", so the stage editor's Save writes straight into assets/data.
cd /d "%~dp0"
echo Refreshing the editors' data from assets/data ...
python make_data.py || goto :fail
python make_bundle.py || goto :fail
cd /d "%~dp0..\.."
start "" "http://localhost:8000/tools/web_editors/index.html"
python -m http.server 8000
goto :eof
:fail
echo.
echo Could not refresh the data (see the error above). Is Python 3 installed and on PATH?
pause
