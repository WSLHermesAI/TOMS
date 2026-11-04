@echo off
rem publish_web.cmd -- build the web game and publish it to GitHub Pages (the gh-pages branch).
rem   Site: https://wslhermesai.github.io/TOMS/
rem   It asks before pushing. Options:
rem     publish_web.cmd nobuild   publish the existing Build\dist\TOMS-web without rebuilding
rem     publish_web.cmd dryrun    build + prepare, show what would change, push nothing
rem The version on the title screen (assets\data\version.json, tools\bump_version.ps1): each real publish adds 1. A dry run,
rem "no" at the question or a failure puts it back. nobuild adds nothing: that package already has its number inside.
rem Details: docs\project\07_PUBLISH_GITHUB_PAGES.md
setlocal EnableExtensions
cd /d "%~dp0.."
set "PUBARGS="
if /i "%~1"=="dryrun" set "PUBARGS=-DryRun"
set "BUILT="
if /i "%~1"=="nobuild" goto :publish
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bump_version.ps1 -Action bump
if errorlevel 1 goto :failed
set "BUILT=1"
rem The web build packs the data files only when it links: without this the new number would not reach the game.
del /q "Build\web-release-windows\bin\toms_game.html" "Build\web-release-mt-windows\bin\toms_game.html" 2>nul
call tools\release_web.cmd nopause
if errorlevel 1 goto :failed

:publish
del /q "Build\dist\published.flag" 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_pages.ps1 %PUBARGS%
if errorlevel 1 goto :failed
if exist "Build\dist\published.flag" goto :published
if defined BUILT powershell -NoProfile -ExecutionPolicy Bypass -File tools\bump_version.ps1 -Action undo
pause
exit /b 0

:published
if not defined BUILT powershell -NoProfile -ExecutionPolicy Bypass -File tools\bump_version.ps1 -Action adopt
pause
exit /b 0

:failed
if defined BUILT powershell -NoProfile -ExecutionPolicy Bypass -File tools\bump_version.ps1 -Action undo
echo.
echo [toms] FAILED -- see the messages above.
pause
exit /b 1
