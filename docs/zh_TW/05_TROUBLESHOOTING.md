# 05 — 問題排除

> 英文原文：[05_TROUBLESHOOTING.md](../05_TROUBLESHOOTING.md)

先執行 `tools\check_env.cmd`：大部分問題都是缺少某個前置需求，檢查工具會指出
是哪一個並附上下載連結。Web 版的問題在 [06 §7](../06_BUILD_WEB.md#7-troubleshooting)。

## Configure（CMake）問題

| 症狀 | 原因 | 修正 |
|---|---|---|
| 對話框「required tools are missing」 | 缺少 [02 §1](../02_INSTALL_WINDOWS.md#1-required) 中的某一項 | 安裝它，然後 *專案 → 刪除快取並重新設定* |
| `The C++ compiler is not MSVC` | 資料夾是在 VS 開發者環境之外 configure 的 | 在 Visual Studio 中開啟，或用 `tools\build.cmd` |
| `Could not find git` / clone 失敗 | 第一次 configure 時沒有 Git 或沒有網路 | 安裝 Git；檢查 proxy 設定；重試 configure |
| clone bgfx 時出現 `Filename too long` | checkout 路徑太長，加上第三方路徑很深 | 開啟長路徑（見 check_env 的建議）並 `git config --global core.longpaths true`，或把 TOMS 移到靠近磁碟根目錄的地方 |
| 對話框「optional tools are missing: Qt」 | 找不到 Qt 6 MSVC 套件 | 安裝它（[02 §2](../02_INSTALL_WINDOWS.md#qt-step-by-step)）或設定 `QTDIR`；遊戲仍然可以建置 |
| `Qt kit ... is not an MSVC build` | 只裝了 MinGW 套件 | 在 Qt Maintenance Tool 中加入 *MSVC 2022 64-bit* 套件 |
| 我什麼都沒修，對話框卻不再出現 | 每份不同的問題清單只顯示一次 | 文字一定在 *輸出 → CMake* 中；刪除快取就會再看到對話框 |
| 建置伺服器上跳出對話框 | — | 使用 `ci-windows` preset（`TOMS_POPUP_WARNINGS=OFF`） |

## 建置問題

| 症狀 | 原因 | 修正 |
|---|---|---|
| `*.sc` 中的 shaderc 錯誤 | shader 語法 | 訊息會顯示檔案和行號；bgfx shader 規則：https://bkaradzic.github.io/bgfx/tools.html#shader-compiler-shaderc |
| 編譯 shader 時找不到 `d3dcompiler_47.dll` | 缺少 Windows SDK | 安裝 Windows 11 SDK |
| C4819 / CJK 字串亂碼 | 某個目標編譯時沒有 `/utf-8` | 每個 toms 目標都呼叫 `toms_target_defaults()`；新目標也要加上 |
| `renderer.h: vulkan/vulkan.h not found` | 某個 include 核心遊戲標頭的新目標，include 路徑第一順位不是 `src/game/compat` | 連結 `toms_core`（它帶有 include 順序），而不是手動加入 `src/core` 的 include 路徑 |
| 警告 `windeployqt not found` | 找不到 Qt 的 `bin` 資料夾 | 把 `<Qt 套件>\bin` 加進 `PATH`，或把 Qt DLL 複製到 `toms_editor.exe` 旁邊 |

## 執行時問題

| 症狀 | 原因 | 修正 |
|---|---|---|
| 對話框「graphics could not start」 | 選用的 bgfx 後端失敗 | 更新顯示卡驅動程式；試試 `--renderer=d3d11` 或 `--renderer=opengl` |
| 對話框「Game assets were not found」 | exe 找不到 `assets\media` | 還原它（`git checkout -- assets`），或傳入 `--assets=<path>` / 設定 `ASSET_DIR` |
| 對話框「The UI files were not found」 | 缺少 `assets\media\ui` 或 `assets\media\fonts\NotoSansCJKtc-TOMS.otf` | 還原它們：`git checkout -- assets` |
| 對話框「The game UI could not start」+ 一個 `.rml` 名稱 | 某個 RML/RCSS 檔有錯 | 主控台 / `toms.log` 中有 RmlUi 的訊息；修正檔案後重新啟動（或在執行中的遊戲按 F5） |
| UI 中某個字元顯示成空白（一個空隙） | 該字元不在精簡過的 UI 字型中 | `python tools/make_ui_font.py`（[08](../08_RMLUI.md#editing-the-ui)） |
| 遊戲右上角出現黃色「!」 | RmlUi 記錄了一則警告 | 按 F8（RmlUi 除錯器）閱讀它 |
| 編輯器啟動後出現「toms_editor.exe - Qt6Core.dll was not found」 | Qt DLL 不在 exe 旁邊 | 重新建置（每次連結後都會執行 windeployqt），或把 `<Qt 套件>\bin` 加進 `PATH` |
| 編輯器：按鍵沒反應 | 遊戲畫面沒有鍵盤焦點 | 點一下 *Play (bgfx)* 畫面 |
| 編輯器：把畫面拉出停駐或移動後變黑 | 原生視窗被重建了 | 讓 viewport 保持為中央 widget/分頁（[01 §5](../01_ARCHITECTURE.md#5-qt--bgfx-in-the-editor)） |
| 存檔不在舊版放的位置 | 工作目錄不同 | 存檔放在工作目錄的 `save\`（F5 時是 `Build\<preset>\bin`） |
| 截圖或螢幕錄影工具把編輯器的遊戲畫面錄成白色或黑色 | 透過 GDI 擷取視窗的工具讀不到 DXGI flip-model swap chain | 改成擷取螢幕或桌面區域（編輯器自己的 `--screenshot` 就是這樣做），或使用 `toms_game --screenshot` / RenderDoc |
| `--renderer=vulkan` 和 Direct3D 之間文字邊緣差一個像素 | 後端的光柵化規則 | 正常現象；每個後端各自比對基準圖 |
