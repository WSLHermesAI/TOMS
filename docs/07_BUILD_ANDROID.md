# 07 — Android: building, running on the emulator, file-access design

> 中文版：[zh_TW/07_BUILD_ANDROID.md](zh_TW/07_BUILD_ANDROID.md)

> **Status (2026-09-30): builds and runs on the emulator.** `tools\build_android.cmd` makes the APK;
> `tools\run_android.cmd` installs it, starts it and shows the log. Checked on the Android Studio
> emulator (Pixel Tablet, API 35, x86_64): title screen, new game, map, touch pad and tap-to-walk,
> battle, saving, saves surviving a force-stop, loading, background → foreground (three times), the
> Back key. **Not tried on a real phone yet** (the arm64 build compiles; see appendix A5).
> Quick start: appendix **A0**. §1–§6 are the original design discussion (implemented as option B),
> kept as background. Related: `06_BUILD_WEB.md` (web build), `04_MIGRATION_PLAN.md` (phases).

---

## 1. The problem: an Android APK is not a file system

Desktop and web can both "open a file by relative path", for different reasons: desktop has real
files, and on the web `--preload-file` packs them into Emscripten's virtual file system. Android is
neither: in the APK, `assets/` are **entries**, readable only through `AAssetManager`, and
`std::ifstream` always fails.

Today: `src/core`, `src/engine` and `src/game` have **58 places** that access files (`ifstream` /
`ofstream` / `fopen` / `std::filesystem`), nearly all opening `assets/data/…`, `assets/media/…` and
the CJK font `.ttc`. So the problem is not "Android must unpack", but **these 58 places need one entry
point that serves desktop / web / APK alike**.

Writing (saves) is a separate matter, unrelated to unpacking: an APK is always read-only, so saves
must go to the app's private folder (`SDL_GetPrefPath()`). That **has to be done anyway**, whichever
read route is chosen (§3.3).

---

## 2. Two read routes (one must be chosen)

| | A: unpack on first start | B: an I/O shim reading directly (recommended) |
|---|---|---|
| How | on first start, copy the APK's `assets/` into `getFilesDir()`, then use relative paths as before | add a thin file entry point (`vfsReadAll` etc.); Android goes through `AAssetManager`, desktop/web stay as they are |
| Changes | the 58 places **untouched**; an unpack step added | the 58 places changed **mechanically** to call the shim (can be done in batches, step by step) |
| Space on the device | **two copies** (in the APK + unpacked); painful if media is large | **one copy** |
| Updates | needs a version stamp; changed assets must be unpacked again (real complexity) | no such problem (always reads the latest in the APK) |
| RmlUi's `.rml`/`.rcss` | readable after unpacking as before | through the shim in the FileInterface |
| Risk | low (existing code untouched) | low (on desktop/web the shim behaves **exactly as today**, so it can be finished before Android) |
| Failure modes | the user clears data, permissions, no space | a missed place → that file cannot be read (tests catch it) |

**B is recommended**, not because it is fashionable but because A's double storage and version
stamps are a long-term debt; B's 58 changes are mechanical, and **behave the same on desktop and
web**, so the work can start now, with the existing golden-PNG smoke tests proving nothing regressed.

---

## 3. Code to change (ordered by risk)

### 3.1 The I/O shim (a new file, the only new interface)
```cpp
// src/core/engine/vfs.h
namespace toms {
    void vfsInit();                                        // Android: get the AAssetManager; no-op elsewhere
    bool vfsReadAll(const std::string& path, std::vector<unsigned char>& out);
    bool vfsExists(const std::string& path);
    std::string vfsWritablePath(const std::string& name);   // -> a path under SDL_GetPrefPath
}
```
- Platform implementations in separate files: `vfs_android.cpp` (`AAssetManager_open` /
  `AAsset_read`), `vfs_desktop.cpp` (`ifstream`).
- Callers change from `std::ifstream f(path)` to `toms::vfsReadAll(path, buf)`; **behaviour on
  desktop/web is exactly the same**.
- Migrate in batches: data JSON loading (`assets/data`) first, then media, then fonts.

### 3.2 RmlUi's file interface
`src/engine/src/rml_ui.cpp` already implements RmlUi's render/system interfaces; add a file
interface whose `Open()` reads `.rml` / `.rcss` / fonts through the shim of §3.1.

### 3.3 Saves and settings (independent of the read route; must be done)
`src/core/game/save/save_slots.cpp` already routes everything through `slotPath(saveDir, slot)`:
pointing `saveDir` at `SDL_GetPrefPath()` is enough, **not a rewrite**. Same for `game_settings`.

### 3.4 Touch → RmlUi input
RmlUi expects mouse/keyboard events; gameplay touch (the pad, `setPadScale`/`setUiScale`) already
exists, and the UI layer needs a touch→mouse mapping.

### 3.5 Small items
- `src/core/engine/log.h`: stdout → `__android_log_print` (cosmetic).
- `src/engine/src/bgfx_host.h`, `src/game/src/main_sdl.cpp`: add an Android branch (SDL3 already
  handles the lifecycle).
- `src/core/game/core/game_helpers.h`: its platform branches need review (not read line by line
  yet; not assumed to be simple).

---

## 4. Build setup (no game code changes)

- New presets `android-debug` / `android-release`:
  `-DCMAKE_TOOLCHAIN_FILE=$ENV{ANDROID_NDK}/build/cmake/android.toolchain.cmake`
  `-DANDROID_ABI=arm64-v8a` (add `armeabi-v7a` if needed) `-DANDROID_PLATFORM=android-24` `-DANDROID_STL=c++_shared`
- **Host shaderc:** Android, like the web, is cross-compiled, so shaderc must be a **host** tool →
  reuse the existing `-DTOMS_HOST_SHADERC=<path>` (the same mechanism, already done for the web).
- Shaders: `TOMS_SHADER_PROFILES=100_es;300_es` + `--platform android` (the CMake variables exist).
- Packaging: SDL3's Gradle / `SDLActivity` as the APK shell; the Qt6 editor excluded (as on the web).
- Android 15+: `-Wl,-z,max-page-size=16384`; ship only `arm64-v8a` (Play requires 64-bit).
- Audio: miniaudio supports Android (OpenSL ES / AAudio); to be verified in the build.

---

## 5. Verification plan (reusing what exists)

1. `gradlew assembleDebug` → `adb install` → start.
2. `adb logcat` for the load messages (the same as checking the web build's console).
3. `adb exec-out screencap` for a screenshot, compared with the **golden PNG** (the CTest smoke-test
   idea of phase 2 step 2).
4. Saves: save in game → kill the app → restart → the save is still there (the same as the web's
   "the save survives a reload").
5. On a real phone, as a player: text is readable, buttons are tappable (the same bar as W9).

---

## 6. Decisions needed from you

1. **Read route A or B** (§2; I recommend B).
2. **ABI:** only `arm64-v8a`, or `armeabi-v7a` too.
3. **The real size of `assets/media`** (decides how much A hurts; also whether Play Asset Delivery
   is needed).
4. **Whether this design goes into implementation now**, or waits for phase 2 step 2 (CTest).


---

# Appendix: building and running (Windows)

## A0. Quick start

First have one desktop build (it provides the host `shaderc.exe`; see A3), and start an emulator in
Android Studio:

```bat
tools\build_android.cmd            :: x86_64, for the emulator (default)
tools\run_android.cmd              :: install on the running emulator / phone, start, show the log (Ctrl+C ends)
tools\build_android.cmd debug      :: arm64-v8a, for a real phone
tools\build_android.cmd release    :: arm64-v8a Release (the APK is unsigned)
```

`build_android.cmd` finds the SDK (`%LOCALAPPDATA%\Android\Sdk`), NDK 27, Android Studio's JBR
(Java), the desktop build's `shaderc.exe` and Visual Studio's CMake/Ninja by itself; any of them can
be overridden with `ANDROID_HOME`, `ANDROID_NDK`, `JAVA_HOME`, `TOMS_HOST_SHADERC`. Steps: the CMake
preset `android-<kind>` builds `libmain.so` → copies `libmain.so` + the NDK's `libc++_shared.so` to
`android\app\libs\<abi>\` → `android\gradlew assemble…`.
Output: `android\app\build\outputs\apk\debug\app-debug.apk`.

## A1. Installing the tools

| Need | Get it | Check |
|---|---|---|
| Android Studio | the official installer (includes the SDK, JBR = Java 21, the emulator) | it starts |
| NDK 27 | Android Studio → SDK Manager → SDK Tools → **NDK (Side by side)** | `%LOCALAPPDATA%\Android\Sdk\ndk\27.*` exists |
| Emulator | Android Studio → Device Manager → create one (an x86_64 system image) | it boots |
| CMake + Ninja | Visual Studio's "C++ CMake tools for Windows" (already there) | — |

Gradle 8.7 and Android Gradle Plugin 8.6.1 are downloaded by `android\gradlew.bat` automatically
(the cache is used once downloaded).

## A2. Project layout

| Where | What |
|---|---|
| `android/` | the Gradle project (from SDL3's `android-project` template). `app/build.gradle`: `jniLibs` = `app/libs` (made at build time, not version-controlled), `assets` = the repo's `assets/` (entries in the APK are `media/…`, `data/…`) |
| `android/app/src/main/java/org/libsdl/app/` | SDL 3.4.8's Java files (zlib licence, copied unchanged; replace them together when upgrading SDL) |
| `android/app/src/main/java/org/toms/game/TomsActivity.java` | extends `SDLActivity` and only loads `c++_shared` and `main` (SDL is linked statically into `libmain.so`) |
| `CMakePresets.json` | `android-x86_64-debug` (emulator), `android-debug` / `android-release` (arm64) |
| `src/game/CMakeLists.txt` | on Android `toms_game` is a SHARED library named `main`; links `EGL android OpenSLES log` |

## A3. The host shaderc

Android, like the web, is **cross-compiled**, so shaderc must run on this Windows machine. Any
desktop build makes it (`tools\build.cmd windows-shipping` → `Build\windows-shipping\bin\shaderc.exe`),
and `build_android.cmd` finds it. Without it CMake stops with an error (Android cannot fall back to
a wasm shaderc the way the web can).

Shader profiles: `300_es` (GLES 3) + `spirv` (Vulkan). This bgfx's shaderc no longer has GLES 2's
`100_es`, and today's Android devices all have GLES 3.

## A4. Code changes (all done on 2026-09-30 and checked on the emulator)

| # | Item | Notes |
|---|---|---|
| 1 | Asset shim | `src/core/engine/vfs.h`: `vfsReadAll` / `vfsExists` / `vfsListDir`. Android goes through `AAssetManager` (strips the leading `assets/`, resolves `..`); desktop/web unchanged |
| 2 | Every read goes through vfs | JSON (`readJsonFile`), sprite PNGs and UI images (`stbi_load_from_memory`), stage folder scans (`game_assets` / `game_store` / `floor_table`), `equipment_actives`, `game_session`'s start-up checks, the UI font check |
| 3 | RmlUi file interface | the `FileInterface` in `rml_ui.cpp` (.rml/.rcss/fonts) |
| 4 | Entry point | `SDL_MAIN_HANDLED` is not defined on Android: `main()` becomes `SDL_main`, called by `SDLActivity` |
| 5 | AAssetManager | `main_sdl.cpp` gets it from the activity's `getAssets()` (signature `()Landroid/content/res/AssetManager;`) |
| 6 | Window | bgfx's window handle = `SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER` (ANativeWindow) |
| 7 | Background/foreground | nothing is drawn in the background; back in the foreground the surface is new → `bgfxHostSetWindow()` makes bgfx rebuild the EGL surface on it. bgfx itself has a bug here (`GlContext::resize` reuses the old window), patched in `glcontext_egl.cpp` at configure time by `cmake/TomsDependencies.cmake` (applied once; configure can be repeated) |
| 8 | Touch and keys | a quick tap (press and release between two frames) still counts as a press; a touch "hovers" for one frame before pressing (the same order as a mouse for the UI); `hasMouse` is always true on phones; Back = Esc |
| 9 | Log | `log.h` writes to logcat (tag `toms`); `stderr` (`[jobs]`, `[rmlui]`, …) goes to logcat too |
| 10 | Saves | the app's private folder (`SDL_GetPrefPath` → `/data/data/org.toms.game/files/`) |
| 11 | Audio | miniaudio outputs through AAudio; sound files are read from the APK through vfs |

## A5. Results

Emulator (Pixel Tablet API 35, x86_64, 2560×1600, GLES 3.0 through the host GPU):

- [x] Title screen (Chinese font, RmlUi layout): bgfx picked OpenGL ES 3.0; the job system has 3 workers
- [x] New game → map, HUD, touch pad; tapping the pad moves one cell; tapping the map = tap-to-walk (including battles)
- [x] Save → `am force-stop` → restart → Continue lists three saves → loading restores the state (HP/gold)
- [x] Home → back (three times in a row): the screen is fine and play continues
- [x] The Back key opens the in-game menu (= Esc)
- [x] arm64-v8a (`tools\build_android.cmd debug`) compiles and packages; **not run on a real phone**
- [ ] Real phone: check as a player that buttons are big enough and text is readable (the W9 bar)

Small known things:
- Each return to the foreground logs **one** `EGL_BAD_SURFACE` (bgfx submits the frame queued before
  the pause), then all is normal.
- Very fast taps such as `adb shell input tap` are handled; real fingers are usually slower and
  unaffected.
- APK size: Debug (x86_64) 22 MB, Release (arm64) 11.6 MB. `app/build.gradle` sets `ndkVersion`, so
  Gradle strips the debug info from `libmain.so` when packaging (the NDK adds `-g` even in Release:
  unstripped it was 99 MB Debug, 91 MB Release); the unstripped originals stay in
  `android/app/libs/<abi>/` for `ndk-stack` to decode crash stacks. The game data itself is under 1 MB.

## A6. Common commands

```bat
adb logcat -s toms SDL                     &rem game log
adb exec-out screencap -p > shot.png       &rem screenshot (in PowerShell go through cmd /c, or the binary gets altered)
adb shell run-as org.toms.game ls files/   &rem list the saves
adb shell am force-stop org.toms.game
```

## A7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| CMake: `An Android build needs a shaderc for this machine` | no desktop build | `tools\build.cmd windows-shipping`, or set `TOMS_HOST_SHADERC` |
| The emulator will not install it (`INSTALL_FAILED_NO_MATCHING_ABIS`) | the emulator is x86_64, the APK arm64 | use `tools\build_android.cmd` (x86_64 by default) for the emulator |
| Crashes at start, logcat shows `JNI DETECTED ERROR` | a Java exception was not cleared | read the exception name in the `F/org.toms.game` lines |
| `graphics could not start: no native window handle` | the window handle was not obtained | see A4 item 6 |
| Black screen after returning to the foreground, `EGL_BAD_SURFACE` repeating | the bgfx patch was not applied | the configure output should show `[toms] bgfx: applied the Android resume fix`; a `bgfx changed` warning means bgfx's version changed and the patch needs another look |
| Play rejects the upload (16 KB pages) | Android 15+ requires it | `-Wl,-z,max-page-size=16384` is already linked |
