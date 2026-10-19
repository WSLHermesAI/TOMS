@echo off
rem test_switches.cmd [preset] [--list] -- switch tests on / off for tools\test.cmd (tools\test_switches.ps1).
rem   test_switches.cmd            a window with the tests as a tree, a checkbox each; Save writes tests\test_switches.json
rem   test_switches.cmd --list     the same tree as text
setlocal
rem (the folder first: shift below also shifts %0)
set "HERE=%~dp0"
set "ARGS="
:args
if "%~1"=="" goto :run
if /i "%~1"=="--list" (set "ARGS=%ARGS% -List") else (set "ARGS=%ARGS% -Preset %~1")
shift
goto :args
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%test_switches.ps1"%ARGS%
exit /b %ERRORLEVEL%
