@echo off
rem run_android.cmd [release] -- install the APK of tools\build_android.cmd on the running emulator or
rem the connected phone, start it, and show the game's log (adb logcat, tag "toms"). Ctrl+C stops the log.
rem Start an emulator first: Android Studio, Device Manager, the play button (docs/07_BUILD_ANDROID.md).
setlocal EnableExtensions
set "ROOT=%~dp0.."
if "%ANDROID_HOME%"=="" set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "ADB=%ANDROID_HOME%\platform-tools\adb.exe"
if not exist "%ADB%" (
  echo [toms] adb not found at %ADB%. Install Android Studio, or set ANDROID_HOME.
  exit /b 1
)
set "APK=%ROOT%\android\app\build\outputs\apk\debug\app-debug.apk"
if /i "%~1"=="release" set "APK=%ROOT%\android\app\build\outputs\apk\release\app-release-unsigned.apk"
if not exist "%APK%" (
  echo [toms] %APK% not found. Build it first: tools\build_android.cmd
  exit /b 1
)
"%ADB%" get-state >nul 2>&1
if errorlevel 1 (
  echo [toms] No device. Start an emulator ^(Android Studio, Device Manager^) or plug in a phone with USB debugging.
  exit /b 1
)
echo [toms] installing %APK%
"%ADB%" install -r "%APK%"
if errorlevel 1 exit /b 1
"%ADB%" logcat -c
"%ADB%" shell am start -n org.toms.game/.TomsActivity
echo [toms] log (Ctrl+C to stop):
"%ADB%" logcat -v time toms:V SDL:V SDL/APP:V AndroidRuntime:E DEBUG:V *:S
