# 07 — Android：建置、在模擬器上執行、檔案存取設計

> **現況（2026-09-30）：已可建置並在模擬器上執行。** `tools\build_android.cmd` 產出 APK，
> `tools\run_android.cmd` 安裝、啟動並顯示 log。在 Android Studio 的模擬器（Pixel Tablet, API 35, x86_64）
> 上驗過：標題畫面、新遊戲、地圖、觸控方向盤與點地圖移動、戰鬥、存檔、強制關閉後存檔仍在、讀檔、
> 背景 → 前景（三次）、返回鍵。**尚未在實體手機上試過**（arm64 版可編譯，見附錄 A5）。
> 快速開始：附錄 **A0**。§1–§6 是當初的設計討論（已依建議 B 實作），保留作為背景說明。
> 相關：`06_BUILD_WEB.md`（web 版）、`04_MIGRATION_PLAN.md`（階段規劃）。

---

## 1. 問題：Android 的 APK 不是檔案系統

桌機與 web 都能用「相對路徑開檔」的原因不同：桌機是真的檔案，web 是 `--preload-file` 把它們塞進
Emscripten 的虛擬檔案系統。Android 兩者皆非 —— `assets/` 在 APK 裡是**條目（entry）**，只能透過
`AAssetManager` 讀，`std::ifstream` 一律失敗。

現況：`src/core`、`src/engine`、`src/game` 共有 **58 處**檔案存取（`ifstream` / `ofstream` / `fopen`
/ `std::filesystem`），幾乎都在開 `assets/data/…`、`assets/media/…` 與 CJK 字型 `.ttc`。所以問題不是
「Android 要解壓」，而是**這 58 處要有一個能同時服務桌機 / web / APK 的入口**。

寫入（存檔）是另一件事，跟解壓無關：APK 永遠唯讀，所以存檔必須寫到 App 私有目錄
（`SDL_GetPrefPath()`），這件事**本來就該做**，與選哪條讀取路線無關（§3.3）。

---

## 2. 兩條讀取路線（必須選一條）

| | A：首次啟動解壓 | B：I/O shim 直讀（建議） |
|---|---|---|
| 做法 | 第一次啟動把 APK 內 `assets/` 複製到 `getFilesDir()`，之後照舊用相對路徑 | 新增一個薄檔案入口（`vfsReadAll` 等），Android 走 `AAssetManager`，桌機/web 照舊 |
| 改動量 | 58 處 **不動**；新增解壓流程 | 58 處**機械式**改成呼叫 shim（可分批、可逐步） |
| 裝置佔用 | **兩份**（APK 內 + 解壓後），media 若大就很痛 | **一份** |
| 更新版本 | 需版本戳記，改了資產要重新解壓（真實複雜度） | 無此問題（永遠讀 APK 內最新） |
| RmlUi 的 `.rml`/`.rcss` | 解壓後照舊可讀 | 在 FileInterface 內用 shim |
| 風險 | 低（不碰現有程式） | 低（shim 在桌機/web 行為與現在**完全相同**，可先改完再上 Android） |
| 失敗模式 | 使用者清資料、權限、空間不足 | 忘了改某處 → 該檔讀不到（可由測試抓到） |

**建議 B**，理由不是「比較潮」，而是 A 的兩份佔用與版本戳記是長期負債；B 的 58 處是機械式替換，
而且**在桌機與 web 上行為不變**，所以可以現在就開始改、用現成的 golden PNG 冒煙測試證明沒有回歸。

---

## 3. 需要改的程式（依風險排序）

### 3.1 I/O shim（新檔，唯一的新介面）
```cpp
// src/core/engine/vfs.h
namespace toms {
    void vfsInit();                                        // Android: 取得 AAssetManager；其他平台 no-op
    bool vfsReadAll(const std::string& path, std::vector<unsigned char>& out);
    bool vfsExists(const std::string& path);
    std::string vfsWritablePath(const std::string& name);   // -> SDL_GetPrefPath 之下的路徑
}
```
- 平台實作分檔：`vfs_android.cpp`（`AAssetManager_open` / `AAsset_read`）、`vfs_desktop.cpp`（`ifstream`）。
- 呼叫端從 `std::ifstream f(path)` 改成 `toms::vfsReadAll(path, buf)`，**行為在桌機/web 完全相同**。
- 分批遷移：先改資料 JSON 載入（`assets/data`），再 media，再字型。

### 3.2 RmlUi 的檔案介面
`src/engine/src/rml_ui.cpp` 已自行實作 RmlUi 的 render/system 介面；再加一個 file interface，
其 `Open()` 用 §3.1 的 shim 讀 `.rml` / `.rcss` / 字型。

### 3.3 存檔與設定（與讀取路線無關，必須做）
`src/core/game/save/save_slots.cpp` 已經把一切導向 `slotPath(saveDir, slot)` —— 只要把 `saveDir`
指向 `SDL_GetPrefPath()` 即可，**不是重寫**。`game_settings` 同理。

### 3.4 觸控 → RmlUi 輸入
RmlUi 期待滑鼠/鍵盤事件；玩法觸控（方向盤、`setPadScale`/`setUiScale`）已存在，需補一層
touch→mouse 映射給 UI 層。

### 3.5 小項
- `src/core/engine/log.h`：stdout → `__android_log_print`（純觀感）。
- `src/engine/src/bgfx_host.h`、`src/game/src/main_sdl.cpp`：加 Android 分支（SDL3 已處理生命週期）。
- `src/core/game/core/game_helpers.h`：其中的平台分支需檢視（尚未逐行讀過，不假設它簡單）。

---

## 4. 建置設定（無需改遊戲程式）

- 新 preset `android-debug` / `android-release`：
  `-DCMAKE_TOOLCHAIN_FILE=$ENV{ANDROID_NDK}/build/cmake/android.toolchain.cmake`
  `-DANDROID_ABI=arm64-v8a`（必要時加 `armeabi-v7a`）`-DANDROID_PLATFORM=android-24` `-DANDROID_STL=c++_shared`
- **host shaderc**：Android 與 web 一樣是交叉編譯，shaderc 必須是**主機**工具 → 沿用既有的
  `-DTOMS_HOST_SHADERC=<path>`（同一套機制，已為 web 做好）。
- 著色器：`TOMS_SHADER_PROFILES=100_es;300_es` + `--platform android`（CMake 變數已存在）。
- 打包：SDL3 的 Gradle / `SDLActivity` 作為 APK 外殼；Qt6 編輯器排除（與 web 同）。
- Android 15+：`-Wl,-z,max-page-size=16384`；只出 `arm64-v8a`（Play 要求 64 位）。
- 音訊：miniaudio 支援 Android（OpenSL ES / AAudio），屬建置驗證項。

---

## 5. 驗證計畫（沿用現有做法）

1. `gradlew assembleDebug` → `adb install` → 啟動。
2. `adb logcat` 檢查載入訊息（等同 web 版的 console 檢查）。
3. `adb exec-out screencap` 截圖，與 **golden PNG** 比對（沿用 Phase 2 step 2 的 CTest 冒煙測試構想）。
4. 存檔：進遊戲存檔 → 殺掉 App → 重開 → 存檔仍在（等同 web 的「reload 後存檔還在」）。
5. 在真機上以玩家視角確認：文字可讀、按鍵可點（W9 的同一條標準）。

---

## 6. 需要你決定的事

1. **讀取路線 A 或 B**（§2；我建議 B）。
2. **ABI**：只出 `arm64-v8a`，還是也要 `armeabi-v7a`。
3. **`assets/media` 的實際大小**（決定 A 的痛感；也影響是否該用 Play Asset Delivery）。
4. **這個設計要不要現在就進實作**，或先等 Phase 2 step 2（CTest）完成。


---

# 附錄：建置與執行（Windows）

## A0. 快速開始

先有一次桌機建置（提供主機用的 `shaderc.exe`，見 A3），並在 Android Studio 開一台模擬器：

```bat
tools\build_android.cmd            :: x86_64，給模擬器（預設）
tools\run_android.cmd              :: 安裝到正在跑的模擬器 / 手機、啟動、顯示 log（Ctrl+C 結束）
tools\build_android.cmd debug      :: arm64-v8a，給實體手機
tools\build_android.cmd release    :: arm64-v8a Release（APK 未簽章）
```

`build_android.cmd` 會自己找 SDK（`%LOCALAPPDATA%\Android\Sdk`）、NDK 27、Android Studio 的 JBR（Java）、
桌機建置的 `shaderc.exe` 與 Visual Studio 的 CMake/Ninja；任一項可用 `ANDROID_HOME`、`ANDROID_NDK`、
`JAVA_HOME`、`TOMS_HOST_SHADERC` 覆寫。步驟：CMake preset `android-<kind>` 編出 `libmain.so` →
複製 `libmain.so` + NDK 的 `libc++_shared.so` 到 `android\app\libs\<abi>\` → `android\gradlew assemble…`。
產出：`android\app\build\outputs\apk\debug\app-debug.apk`。

## A1. 安裝工具

| 需要 | 取得方式 | 檢查 |
|---|---|---|
| Android Studio | 官方安裝檔（含 SDK、JBR = Java 21、模擬器） | 開得起來 |
| NDK 27 | Android Studio → SDK Manager → SDK Tools → **NDK (Side by side)** | `%LOCALAPPDATA%\Android\Sdk\ndk\27.*` 存在 |
| 模擬器 | Android Studio → Device Manager → 建一台（x86_64 系統映像） | 能開機 |
| CMake + Ninja | Visual Studio 的「C++ CMake tools for Windows」（已有） | — |

Gradle 8.7 與 Android Gradle Plugin 8.6.1 由 `android\gradlew.bat` 自動下載（已下載過就用快取）。

## A2. 專案結構

| 位置 | 內容 |
|---|---|
| `android/` | Gradle 專案（取自 SDL3 的 `android-project` 範本）。`app/build.gradle`：`jniLibs` = `app/libs`（建置時產生，不進版控），`assets` = 倉庫的 `assets/`（APK 內條目為 `media/…`、`data/…`） |
| `android/app/src/main/java/org/libsdl/app/` | SDL 3.4.8 的 Java 檔（zlib 授權，原樣複製；升級 SDL 時一起換） |
| `android/app/src/main/java/org/toms/game/TomsActivity.java` | 繼承 `SDLActivity`，只載入 `c++_shared` 與 `main`（SDL 是靜態連結進 `libmain.so` 的） |
| `CMakePresets.json` | `android-x86_64-debug`（模擬器）、`android-debug` / `android-release`（arm64） |
| `src/game/CMakeLists.txt` | Android 上 `toms_game` 是 SHARED library，輸出名 `main`；連結 `EGL android OpenSLES log` |

## A3. 主機用的 shaderc

Android 和 web 一樣是**交叉編譯**，shaderc 必須在這台 Windows 上跑。任何一次桌機建置都會產生它
（`tools\build.cmd windows-shipping` → `Build\windows-shipping\bin\shaderc.exe`），`build_android.cmd` 會自己找。
沒有它 CMake 會直接報錯（Android 無法像 web 一樣退回 wasm 版 shaderc）。

著色器 profile：`300_es`（GLES 3）+ `spirv`（Vulkan）。這版 bgfx 的 shaderc 已沒有 GLES 2 的 `100_es`，
而現今 Android 裝置都有 GLES 3。

## A4. 程式改動（2026-09-30 全部完成，並在模擬器上驗證）

| # | 項目 | 說明 |
|---|---|---|
| 1 | 資產 shim | `src/core/engine/vfs.h`：`vfsReadAll` / `vfsExists` / `vfsListDir`。Android 走 `AAssetManager`（路徑去掉開頭的 `assets/`、解析 `..`）；桌機/web 行為不變 |
| 2 | 讀檔點全改走 vfs | JSON（`readJsonFile`）、sprite PNG 與 UI 圖片（`stbi_load_from_memory`）、關卡資料夾掃描（`game_assets` / `game_store` / `floor_table`）、`equipment_actives`、`game_session` 的啟動檢查、UI 字型檢查 |
| 3 | RmlUi 檔案介面 | `rml_ui.cpp` 的 `FileInterface`（.rml/.rcss/字型） |
| 4 | 進入點 | Android 上不定義 `SDL_MAIN_HANDLED`：`main()` 成為 `SDL_main`，由 `SDLActivity` 呼叫 |
| 5 | AAssetManager | `main_sdl.cpp` 由 activity 的 `getAssets()` 取得（簽章 `()Landroid/content/res/AssetManager;`） |
| 6 | 視窗 | bgfx 的視窗 handle = `SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER`（ANativeWindow） |
| 7 | 背景/前景 | 背景時不繪製；回到前景時 surface 是新的 → `bgfxHostSetWindow()` 讓 bgfx 在新 surface 上重建 EGL surface。bgfx 本身在這裡有 bug（`GlContext::resize` 沿用舊視窗），由 `cmake/TomsDependencies.cmake` 在 configure 時修補 `glcontext_egl.cpp`（只改一次、可重複 configure） |
| 8 | 觸控與按鍵 | 快速點擊（按下與放開落在同一格之間）仍算按了一格；觸控會先「懸停」一格再按下（UI 與滑鼠同樣的順序）；手機上 `hasMouse` 恆為 true；返回鍵 = Esc |
| 9 | log | `log.h` 輸出到 logcat（tag `toms`）；`stderr`（`[jobs]`、`[rmlui]` 等）也導到 logcat |
| 10 | 存檔 | App 私有目錄（`SDL_GetPrefPath` → `/data/data/org.toms.game/files/`） |
| 11 | 音效 | miniaudio 經 AAudio 輸出；音檔經 vfs 從 APK 讀 |

## A5. 驗證結果

模擬器（Pixel Tablet API 35，x86_64，2560×1600，GLES 3.0 經主機 GPU）：

- [x] 標題畫面（中文字型、RmlUi 版面）— bgfx 選到 OpenGL ES 3.0；job system 3 個 worker
- [x] 新遊戲 → 地圖、HUD、觸控方向盤；點方向盤移動一格；點地圖 = 點擊移動（含戰鬥）
- [x] 存檔 → `am force-stop` → 重開 → 繼續遊戲列出三個存檔 → 讀檔回到原狀態（HP/金幣）
- [x] Home → 回來（連續三次）畫面正常、可繼續操作
- [x] 返回鍵開啟遊戲選單（= Esc）
- [x] arm64-v8a（`tools\build_android.cmd debug`）可編譯、打包 —— **未在實體手機上執行**
- [ ] 實體手機：玩家視角確認按鍵大小、文字可讀（W9 的標準）

已知小事：
- 每次回到前景，logcat 會出現**一次** `EGL_BAD_SURFACE`（bgfx 先送出暫停前排好的那一格），之後正常。
- `adb shell input tap` 這類極快的點擊已處理；實體手指通常更慢，不受影響。
- APK 大小：Debug（x86_64）22 MB、Release（arm64）11.6 MB。`app/build.gradle` 設了 `ndkVersion`，Gradle 打包時
  會去掉 `libmain.so` 的除錯資訊（NDK 連 Release 也帶 `-g`：未去除時 Debug 99 MB、Release 91 MB）；
  未去除的原檔留在 `android/app/libs/<abi>/`，給 `ndk-stack` 解讀當機堆疊用。遊戲資料本身不到 1 MB。

## A6. 常用指令

```bat
adb logcat -s toms SDL                     &rem 遊戲 log
adb exec-out screencap -p > shot.png       &rem 截圖（在 PowerShell 裡請經 cmd /c，否則二進位會被改掉）
adb shell run-as org.toms.game ls files/   &rem 看存檔
adb shell am force-stop org.toms.game
```

## A7. 疑難排解

| 症狀 | 原因 | 處理 |
|---|---|---|
| CMake：`An Android build needs a shaderc for this machine` | 沒有桌機建置 | `tools\build.cmd windows-shipping`，或設 `TOMS_HOST_SHADERC` |
| 模擬器裝不上（`INSTALL_FAILED_NO_MATCHING_ABIS`） | 模擬器是 x86_64，APK 是 arm64 | 模擬器用 `tools\build_android.cmd`（預設 x86_64） |
| 啟動即閃退，logcat 有 `JNI DETECTED ERROR` | Java 例外沒清掉 | 看 `F/org.toms.game` 那幾行的例外名稱 |
| `graphics could not start: no native window handle` | 視窗 handle 沒取到 | 見 A4 第 6 項 |
| 回到前景後黑畫面、`EGL_BAD_SURFACE` 不斷出現 | bgfx 修補沒套上 | configure 輸出應有 `[toms] bgfx: applied the Android resume fix`；若出現 `bgfx changed` 警告，表示 bgfx 版本變了，要重看修補 |
| Play 上傳被拒（16 KB 分頁） | Android 15+ 要求 | 已連結 `-Wl,-z,max-page-size=16384` |
