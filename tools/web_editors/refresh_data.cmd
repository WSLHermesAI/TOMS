@echo off
rem refresh_data.cmd -- rebuild both editors' data files (data.js, stage_bundle.js) from the current assets/data.
rem serve.cmd does this by itself; this is for opening the HTML files without the server.
cd /d "%~dp0"
python make_data.py
python make_bundle.py
pause
