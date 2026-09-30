@echo off
rem build_android.cmd [x86_64-debug|debug|release] -- build the Android APK (docs/07_BUILD_ANDROID.md).
rem   x86_64-debug  (default) for the Android Studio emulator     debug / release  arm64 phones
rem Steps: CMake preset android-<kind> (libmain.so, NDK) -> copy libmain.so + libc++_shared.so into
rem android\app\libs\<abi>\ -> android\gradlew assemble. Result: android\app\build\outputs\apk\...
rem Finds on its own (override with the variables): ANDROID_HOME (the SDK), ANDROID_NDK (NDK 27),
rem JAVA_HOME (Android Studio's JBR), TOMS_HOST_SHADERC (a desktop build's shaderc.exe).
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "KIND=%~1"
if "%KIND%"=="" set "KIND=x86_64-debug"
set "PRESET=android-%KIND%"
set "ABI=arm64-v8a"
set "TRIPLE=aarch64-linux-android"
if /i "%KIND%"=="x86_64-debug" set "ABI=x86_64"
if /i "%KIND%"=="x86_64-debug" set "TRIPLE=x86_64-linux-android"
set "GRADLE_TASK=assembleDebug"
if /i "%KIND%"=="release" set "GRADLE_TASK=assembleRelease"

if "%ANDROID_HOME%"=="" set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not exist "%ANDROID_HOME%\platform-tools" (
  echo [toms] Android SDK not found at %ANDROID_HOME%. Install Android Studio, or set ANDROID_HOME.
  exit /b 1
)
if "%ANDROID_NDK%"=="" for /d %%d in ("%ANDROID_HOME%\ndk\27.*") do set "ANDROID_NDK=%%d"
if "%ANDROID_NDK%"=="" (
  echo [toms] NDK 27 not found in %ANDROID_HOME%\ndk. Android Studio: SDK Manager, SDK Tools, NDK ^(Side by side^).
  exit /b 1
)
set "ANDROID_NDK=%ANDROID_NDK:\=/%"
if "%JAVA_HOME%"=="" set "JAVA_HOME=%ProgramFiles%\Android\Android Studio\jbr"
if not exist "%JAVA_HOME%\bin\java.exe" (
  echo [toms] No Java found at %JAVA_HOME%. Install Android Studio, or set JAVA_HOME to a JDK 17+.
  exit /b 1
)
if "%TOMS_HOST_SHADERC%"=="" for %%p in (windows-shipping windows-release ci-windows windows-debug) do (
  if not defined TOMS_HOST_SHADERC if exist "%ROOT%\out\build\%%p\bin\shaderc.exe" set "TOMS_HOST_SHADERC=%ROOT%\out\build\%%p\bin\shaderc.exe"
)
if "%TOMS_HOST_SHADERC%"=="" (
  echo [toms] No shaderc.exe from a desktop build. Build one first: tools\build.cmd windows-shipping
  exit /b 1
)
set "TOMS_HOST_SHADERC=%TOMS_HOST_SHADERC:\=/%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" (
  echo [toms] Visual Studio not found ^(its CMake and Ninja are used^). See docs/02_INSTALL_WINDOWS.md
  exit /b 1
)
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cd /d "%ROOT%"
echo [toms] %PRESET%: NDK %ANDROID_NDK%, shaderc %TOMS_HOST_SHADERC%
cmake --preset %PRESET%
if errorlevel 1 goto :failed
cmake --build --preset %PRESET%
if errorlevel 1 goto :failed

set "LIBS=android\app\libs\%ABI%"
if exist android\app\libs rmdir /s /q android\app\libs
mkdir "%LIBS%"
set "LIBMAIN="
for /r "build-%PRESET%\bin" %%f in (libmain.so) do if exist "%%f" set "LIBMAIN=%%f"
if "%LIBMAIN%"=="" (
  echo [toms] libmain.so not found under build-%PRESET%\bin
  goto :failed
)
copy /y "%LIBMAIN%" "%LIBS%\" >nul
copy /y "%ANDROID_NDK:/=\%\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\%TRIPLE%\libc++_shared.so" "%LIBS%\" >nul
if errorlevel 1 goto :failed

if not exist android\local.properties (echo sdk.dir=%ANDROID_HOME:\=/%) > android\local.properties
cd /d "%ROOT%\android"
call "%ROOT%\android\gradlew.bat" %GRADLE_TASK%
if errorlevel 1 goto :failed
echo [toms] APK: %ROOT%\android\app\build\outputs\apk\
echo [toms] Install + run on the emulator / a phone: tools\run_android.cmd
exit /b 0

:failed
echo [toms] Android build FAILED.
exit /b 1
