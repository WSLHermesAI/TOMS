@echo off
rem publish_web.bat -- build the web game and publish it to GitHub Pages (the gh-pages branch).
rem   Site: https://wslhermesai.github.io/TOMS/
rem   It asks before pushing. Options:
rem     publish_web.bat nobuild   publish the existing Build\dist\TOMS-web without rebuilding
rem     publish_web.bat dryrun    build + prepare, show what would change, push nothing
rem Details: docs\07_PUBLISH_GITHUB_PAGES.md
setlocal EnableExtensions
cd /d "%~dp0"
set "PUBARGS="
if /i "%~1"=="dryrun" set "PUBARGS=-DryRun"
if /i "%~1"=="nobuild" goto :publish
call build_web.bat nopause
if errorlevel 1 goto :failed

:publish
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_pages.ps1 %PUBARGS%
if errorlevel 1 goto :failed
pause
exit /b 0

:failed
echo.
echo [toms] FAILED -- see the messages above.
pause
exit /b 1
