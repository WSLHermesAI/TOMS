@echo off
rem refresh_data.cmd -- copy the current assets/data into both editors' data files (data.js, stage_bundle.js).
cd /d "%~dp0"
python make_data.py
python make_bundle.py
pause
