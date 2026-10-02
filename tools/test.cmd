@echo off
rem test.cmd [preset] [ctest options] -- run the tests (docs/09_TESTS.md) from a plain command prompt.
rem   test.cmd                        all tests of windows-release (build it first: tools\build.cmd)
rem   test.cmd ci-windows -L unit     only the unit tests of ci-windows
rem Uses the ctest that ships with Visual Studio. Exit code: 0 when every test passed.
setlocal EnableExtensions
rem (the root first: shift below also shifts %0)
set "ROOT=%~dp0.."
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-release"
if not "%~1"=="" shift

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
"%CTEST%" --preset "%PRESET%" -j 4%ARGS%
exit /b %ERRORLEVEL%

:novs
echo [toms] Visual Studio's ctest was not found. Run tools\check_env.cmd for details.
exit /b 1
