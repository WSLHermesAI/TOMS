# 02 — 安裝所有東西（Windows）

> 英文原文：[02_INSTALL_WINDOWS.md](../02_INSTALL_WINDOWS.md)

本頁列出 TOMS 需要的所有東西、去哪裡取得，以及如何檢查。標示**必要**的項目
是遊戲（`toms_game.exe`）需要的。**Qt** 只有編輯器（`toms_editor.exe`）需要。

**要設定一台沒用過的電腦？** 請先讀
[11 — 新電腦上從零開始（AI 執行版）](11_NEW_MACHINE_SETUP.md)：重要的 git 設定
（`git clone --recurse-submodules`、`git config core.longpaths true`）、哪些*不*需要
（不需要 Git LFS、不需要設定 symlink）、可以直接貼給 AI 代理的指令區塊，以及驗收
清單。SSH 金鑰（以及如何從第二台電腦 push）有自己的頁面：[12](12_SSH_KEY_SETUP.md)。

## 0. 檢查你已經有什麼

雙擊 **`tools\check_env.cmd`**（或在終端機中執行）。

- 它會檢查本頁的每一項，並印出 `[ OK ]`、`[MISS]`（必要）或 `[WARN]`（選用）。
- 如果有缺少的，會跳出**對話框**列出它、修正方法和下載連結，並可以
  在瀏覽器中打開下載頁面。
- `tools\check_env.cmd -NoGui` 印出同樣的報告，但不跳對話框（給 CI 用）。
- 結束碼 1 表示缺少某個*必要*項目。

CMake configure 專案時（在 Visual Studio 中或
從 `tools\build.cmd`）會自動再跑一次同樣的檢查。缺少**必要**項目會以對話框停止 configure；缺少
**選用**項目（Qt）只會跳一次對話框，並只略過需要它的部分。

## 1. 必要

| # | 項目 | 原因 | 取得方式 | 檢查如何找到它 |
|---|---|---|---|---|
| 1 | **Windows 10 或 11，64 位元** | 目標平台 | — | 作業系統版本 |
| 2 | **Visual Studio 2022 或更新版**（Community 免費），含工作負載 **「使用 C++ 的桌面開發」** | MSVC 編譯器、除錯器 | https://visualstudio.microsoft.com/downloads/ | `vswhere` 元件 `VC.Tools.x86.x64` |
| 3 | VS 元件 **「適用於 Windows 的 C++ CMake 工具」** | VS 內建的 CMake + Ninja、*開啟資料夾* 支援 | VS Installer → 修改 → 個別元件 | `vswhere` 元件 `VC.CMake.Project` |
| 4 | **Windows 11 SDK**（任何 10.0.2xxxx） | Windows 標頭；編譯 shader 用的 `d3dcompiler_47.dll` | VS Installer → 個別元件，或 https://developer.microsoft.com/windows/downloads/windows-sdk/ | 登錄檔 `Windows Kits\Installed Roots` |
| 5 | **CMake 3.24+** | 建置系統 | 隨第 3 項附帶；或 https://cmake.org/download/ | 先找 VS 的副本，再找 `PATH` |
| 6 | **Ninja** | preset 使用的建置工具 | 隨第 3 項附帶；或 https://github.com/ninja-build/ninja/releases | 先找 VS 的副本，再找 `PATH` |
| 7 | **Git for Windows** | 第一次 configure 會 clone bgfx、SDL3、Dear ImGui 和 glm | https://git-scm.com/download/win | `PATH` 上的 `git` |
| 8 | **能連到 github.com**（只有第一次 configure 需要） | 同第 7 項 | — | 對 github.com 的 HTTPS 請求 |
| 9 | 本 repository 的 **`src/` 和 `assets/`** | 遊戲程式碼（`src/core`、`src/third_party`、`src/editor/stage`）會被編譯；`assets/media` + `assets/data` 會被載入 / 打包 | clone 時就有：`git clone --recurse-submodules https://github.com/WSLHermesAI/TOMS` | 檔案存在 |
| 10 | **約 5 GB 可用磁碟空間** | bgfx + 工具 + 兩種組態 | — | 磁碟可用空間 |

### Visual Studio：要勾選什麼

在 **Visual Studio Installer** → *修改*：

- 工作負載分頁：**使用 C++ 的桌面開發**
- 右側 *安裝詳細資料* 底下，保持勾選以下項目（預設就是勾選的）：
  - **MSVC v143（或更新版）– VS 2022 C++ x64/x86 建置工具**
  - **Windows 11 SDK**
  - **適用於 Windows 的 C++ CMake 工具**

Visual Studio 2026（v18）也可以：會自動使用它附帶的 CMake 4.x 和 Ninja。

## 2. 選用

| 項目 | 用途 | 取得方式 | 備註 |
|---|---|---|---|
| **Qt 6.5+ — 套件「MSVC 2022 64-bit」**（建議 6.8 LTS） | 只有 `toms_editor.exe` | Qt Online Installer：https://www.qt.io/download-qt-installer-oss | 需要免費的 Qt 帳號。見下方。 |
| **Python 3 + fontTools**（`pip install fonttools`） | 只有在加入含新字元的文字後，重建 UI 字型時需要 | https://www.python.org/downloads/ | 字型本身在 repo 中（`assets/media/fonts/NotoSansCJKtc-TOMS.otf`）；見 [08](../08_RMLUI.md#editing-the-ui)。 |
| **Emscripten SDK（emsdk）** | 只有 web 版需要（[06](06_BUILD_WEB.md)） | https://emscripten.org/docs/getting_started/downloads.html | 先 `git clone`，再 `emsdk install latest` + `emsdk activate latest`。會自動偵測 TOMS 旁邊（`D:\Work\emsdk`）、`%USERPROFILE%\emsdk`、`C:\emsdk`，或透過 `EMSDK`。 |
| **Vulkan 執行環境** | `--renderer=vulkan` | 隨目前的 NVIDIA/AMD/Intel 驅動程式附帶 | Direct3D 11/12 不需要它。 |
| **RenderDoc** | bgfx 的 GPU 畫面擷取 | https://renderdoc.org/ | |

### Qt：一步一步

1. 下載並執行 **Qt Online Installer**（https://www.qt.io/download-qt-installer-oss）。
   登入或建立免費帳號；選擇 **open-source use**（LGPLv3）。
2. *安裝資料夾*：保持 **`C:\Qt`**。專案也會自動偵測 `D:\Qt` 和
   `%USERPROFILE%\Qt`。
3. *Select components* → **Qt → Qt 6.8.x**（或更新版）→ 勾選 **MSVC 2022 64-bit**。
   其他都不需要（不需要 MinGW、Android、原始碼）。
4. 如果你把 Qt 裝在其他地方，請設定環境變數 **`QTDIR`** 指向套件資料夾，
   例如 `E:\Tools\Qt\6.8.3\msvc2022_64`，然後重新啟動 Visual Studio。
5. 再執行一次 `tools\check_env.cmd`：Qt 那一行應該會顯示套件路徑。

**MinGW 套件不能用。** 它們無法和 MSVC 連結。如果只找到 MinGW 套件，檢查和 CMake configure 都會
發出警告。

Qt 授權說明：編輯器以 LGPLv3 動態連結 Qt。它是內部工具，
永遠不會發給玩家，所以發行的遊戲完全沒有 Qt 的義務。

## 3. 不需要安裝的東西

這些會在第一次 configure 時由 CMake 下載並建置（固定版本，見
`cmake/TomsDependencies.cmake`）：

| 函式庫 | 版本 | 用途 |
|---|---|---|
| bgfx（+ bx、bimg、shaderc），透過 bgfx.cmake | v1.161.9510-579 | 渲染；shaderc 編譯 `src/engine/shaders/*.sc` |
| SDL3 | release-3.4.8 | 遊戲視窗、輸入、主迴圈 |
| Dear ImGui | v1.90.9 | 遊戲的開發者視窗（和舊版同一版本） |
| glm | 1.0.1 | `node.h` 中的數學（舊版需要透過 `GLM_DIR` 手動安裝一份） |

**不需要** Vulkan SDK（bgfx 在執行時載入 Vulkan）。只有想要 Vulkan 驗證層時
才需要安裝。

## 4. 安裝之後

1. `tools\check_env.cmd` → 所有必要項目都顯示 `[ OK ]`。
2. 在 Visual Studio 中開啟資料夾：[03_VISUAL_STUDIO.md](03_VISUAL_STUDIO.md)。
3. 或從終端機建置：`tools\build.cmd windows-release`。
