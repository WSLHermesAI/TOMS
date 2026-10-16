@echo off
rem package_editors.cmd -- build windows-release and refresh the editor release in Editor\bin (Editor\README.md).
rem   package_editors.cmd             build, then copy
rem   package_editors.cmd --no-build  copy the last build
rem   package_editors.cmd --check     only report differences
python "%~dp0package_editors.py" %*
exit /b %errorlevel%
