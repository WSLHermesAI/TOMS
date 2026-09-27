# 07 — Android：建置與檔案存取設計（design, not yet built）

> 本文件是設計稿，尚未實作。目的：讓 Android 版能編譯、能讀到 `assets/`、能把存檔寫進 App 私有目錄，
> 並且**不必解壓一份資產到裝置上**（見 §2 的取捨）。
> 相關：`06_BUILD_WEB.md`（web 版建置，已可用）、`04_MIGRATION_PLAN.md`（階段規劃）。

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
