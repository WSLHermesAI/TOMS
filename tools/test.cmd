@echo off
rem test.cmd [preset] [all | --since REF | --list] [ctest options] -- run the tests (docs/09_TESTS.md).
rem   test.cmd                        only the tests the uncommitted changes can break (tools\test_select.py)
rem   test.cmd all                    every test of windows-release, web included
rem   test.cmd --since main           the tests for what changed since a commit or branch
rem   test.cmd --list                 print which tests would run and why; run nothing
rem   test.cmd ci-windows -L unit     a ctest filter (-L -LE -R -E -I) runs as given, without the selection
rem Build first (tools\build.cmd). Uses the ctest that ships with Visual Studio. Exit code: 0 when every test passed.
setlocal EnableExtensions
rem (the root first: shift below also shifts %0)
set "ROOT=%~dp0.."
set "PRESET=windows-release"
set "A1=%~1"
if not "%A1%"=="" if not "%A1%"=="all" if not "%A1%"=="changed" if not "%A1:~0,1%"=="-" (
  set "PRESET=%A1%"
  shift
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" goto :novs
set "CTEST=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if not exist "%CTEST%" goto :novs

set "ARGS="
:args
if "%~1"=="" goto :run
set "ARGS=%ARGS% %1"
shift
goto :args

:run
cd /d "%ROOT%"
if not exist "Build\%PRESET%\CTestTestfile.cmake" (
  echo [toms] Build\%PRESET% has no tests yet: run tools\build.cmd %PRESET% first.
  exit /b 1
)
where python >nul 2>nul
if errorlevel 1 (
  echo [toms] Python was not found, so the changed-only selection cannot run: running every test.
  "%CTEST%" --preset "%PRESET%" -j 4%ARGS%
  exit /b %ERRORLEVEL%
)
python "%ROOT%\tools\test_select.py" --ctest "%CTEST%" --preset "%PRESET%"%ARGS%
exit /b %ERRORLEVEL%

:novs
echo [toms] Visual Studio's ctest was not found. Run tools\check_env.cmd for details.
exit /b 1
