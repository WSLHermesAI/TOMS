@echo off
rem release_editors.cmd -- build every editor and refresh the editor release in Editor\bin.
rem   Result: Editor\bin\*.exe (all editors + atlaspack, fontpack, Qt and runtime DLLs)  and  Editor\BUILD_INFO.md
rem   Then double-click any launcher in Editor\ (TomsEditor.cmd, AnimEditor.cmd, ...).
rem Double-click this file, or run it from a command prompt. Missing tools pop up with download links.
rem   release_editors.cmd nobuild  copy the last build only (no compile)
setlocal EnableExtensions
cd /d "%~dp0.."
set "ARGS="
if /i "%~1"=="nobuild" set "ARGS=--no-build"
if /i "%~2"=="nobuild" set "ARGS=--no-build"
if defined ARGS (echo [toms] copying the last windows-release build into Editor\bin...) else (echo [toms] building the editors ^(preset windows-release^), then copying them into Editor\bin...)
python tools\package_editors.py %ARGS%
if errorlevel 1 goto :failed
echo.
echo [toms] done. The editors in Editor\bin are up to date (see Editor\BUILD_INFO.md).
if /i not "%~1"=="nopause" if /i not "%~2"=="nopause" pause
exit /b 0

:failed
echo.
echo [toms] FAILED -- see the messages above (and tools\check_env.cmd for missing tools).
if /i not "%~1"=="nopause" if /i not "%~2"=="nopause" pause
exit /b 1
