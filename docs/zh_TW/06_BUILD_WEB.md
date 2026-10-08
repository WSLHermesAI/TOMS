# 06 — 建置 web（瀏覽器）版

> 英文原文：[06_BUILD_WEB.md](../06_BUILD_WEB.md)

Web 版和桌機版是同一個 `toms_game`：SDL3 + bgfx + 未修改的 TOMS 遊戲
程式碼，用 Emscripten 編譯成 WebAssembly。在瀏覽器中，bgfx 用 **WebGL2** 繪製。不需要
外掛或安裝；在目前的 Chrome、Edge、Firefox 和 Safari 中都能執行。

## 發行套件

雙擊 repository 根目錄的 **`build_web.bat`**。它會執行 `tools\build_web.cmd release` 和
`tools\build_web.cmd mt`（多執行緒版，[10](10_THREADS.md)），然後執行 `tools\package.ps1 -Target web`，
寫出 **`Build\dist\TOMS-web\`**（以及 `Build\dist\TOMS-web.zip`）：
`index.html`（頁面；瀏覽器允許執行緒時載入多執行緒版，否則載入單執行緒版）、
`toms_game.<stamp>.js/.wasm/.data` 和 `toms_game_mt.<stamp>.js/.wasm/.data`（加上版本戳記，
讓快取永遠不會混用不同版本；戳記寫在 `version.txt`）、`coi-serviceworker.js`（讓 GitHub Pages 這類主機
提供執行緒需要的標頭），再加上 `.htaccess`
（Apache）和 `web.config`（IIS），讓 `.wasm` 以 `application/wasm` 提供。把這些檔案上傳到任何靜態網頁伺服器的
同一個資料夾，再開啟該資料夾的網址即可。GitHub Pages 請用 `publish_web.bat`
（[07](07_PUBLISH_GITHUB_PAGES.md)）。2026-09-27 已驗證：用一般的靜態伺服器提供這個資料夾的副本，
再對 `/` 執行 `tools/web_smoke_test.mjs`。

## 快速開始（Windows）

```bat
cd TOMS
tools\build_web.cmd            :: builds Build\web-release-windows\bin\toms_game.html
tools\serve_web.cmd            :: serves it and opens http://localhost:8099/toms_game.html
```

兩個都要從 **cmd、PowerShell 或檔案總管**（雙擊）執行。不要從 Git Bash：從那裡啟動時，
emsdk 的設定不會生效，腳本會以「emsdk not found」停止。

第一次 `build_web.cmd` 可能要一段時間（見 §2 第 3 步）。之後的建置是增量的。

## 1. 你需要什麼

| 項目 | 原因 | 取得方式 | 由誰檢查 |
|---|---|---|---|
| 桌機版需要的所有東西（[02](02_INSTALL_WINDOWS.md) §1） | Visual Studio 的 CMake + Ninja；桌機版建置會提供主機上的 `shaderc.exe` | 見 02 | `tools\check_env.cmd` |
| **Emscripten SDK（emsdk）** | C++ → WebAssembly 編譯器 | https://emscripten.org/docs/getting_started/downloads.html | `check_env.cmd`（「Emscripten SDK」）、`build_web.cmd` |
| 一個瀏覽器 | 用來玩 | Chrome / Edge / Firefox | — |

**安裝 emsdk**（一次）：

```bat
cd D:\Work
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
emsdk install latest
emsdk activate latest
```

`build_web.cmd` 和 `check_env.cmd` 依以下順序尋找 emsdk：環境變數 `EMSDK`、
TOMS checkout 旁邊的 `..\..\emsdk`（在這台機器上是 `D:\Work\emsdk`）、
`%USERPROFILE%\emsdk`、`C:\emsdk`、`D:\emsdk`。如果你的放在別處，把 `EMSDK` 設成它的資料夾。
如果都找不到，會跳出對話框說明如何安裝，並附上下載頁面的連結。

以 **Emscripten 6.0.9** 測試過。Emscripten 舊於 3.1.60 時 configure 會發出警告。

你**不需要**另外安裝 Node.js 或 Python：emsdk 兩者都有，`serve_web.cmd` 也用
emsdk 的 Python 跑本機網頁伺服器。

## 2. `build_web.cmd` 做了什麼

1. **找到並載入 emsdk**（`emsdk_env.bat`），讓 `emcc` 和 emsdk 的 `node` 出現在 PATH 上。
2. **找到 CMake 和 Ninja**：Visual Studio 的副本（透過 `vswhere`），否則用 PATH。
3. **在 `Build\*\bin\` 中找主機上的 `shaderc.exe`**。Shader 是在建置時於*這台*機器上
   編譯的，所以 web 版需要一個 Windows 的 `shaderc.exe`，就是桌機版用的那個。
   **如果還沒有，它會先建置 `windows-shipping` 桌機 preset** 來取得一個。
   這是第一次執行時最慢的部分。
4. **Configure 並建置** preset `web-release-windows`（或 `web-debug-windows`），並傳入
   shaderc 的路徑。

建置選項：

| 指令 | Preset | 輸出資料夾 | 用途 |
|---|---|---|---|
| `tools\build_web.cmd` 或 `tools\build_web.cmd release` | `web-release-windows`（Release） | `Build\web-release-windows\bin\` | 要發佈的版本（約 5.3 MB，gzip 下載約 1.7 MB） |
| `tools\build_web.cmd debug` | `web-debug-windows`（Debug，DWARF） | `Build\web-debug-windows\bin\` | 在 Chrome DevTools 中逐步執行 C++ |

執行前設定 `TOMS_NO_POPUPS=1` 可以關掉前置需求對話框（建置伺服器用）。

### 輸出

| 檔案 | 大小（release） | 內容 |
|---|---|---|
| `toms_game.html` | 3 KB | 頁面（來自 `src/game/web/shell.html`） |
| `toms_game.js` | 0.25 MB | Emscripten 的載入器 |
| `toms_game.wasm` | 4.2 MB（gzip 1.45 MB） | 遊戲，包含 RmlUi |
| `toms_game.data` | 0.75 MB（gzip 0.18 MB） | `assets/media`（sprite、音效、UI 文件；沒有字型）+ `assets/data`，掛載在 `/assets` 和 `/data` |

這四個檔案就是整個遊戲。複製到任何靜態網頁主機即可。

## 3. 玩它

```bat
tools\serve_web.cmd              :: release build, port 8099
tools\serve_web.cmd debug 8100   :: debug build on another port
tools\serve_web.cmd mt           :: the multithreaded build (tools\build_web.cmd mt)
tools\serve_web.cmd dist         :: the package of build_web.bat, as a player gets it
```

伺服器（`tools\serve_web.py`）會送出兩個跨來源隔離標頭，所以在本機也能使用執行緒。

瀏覽器拒絕從 `file://` 執行 WebAssembly，所以雙擊開啟 `toms_game.html`
是不行的。請用伺服器。用 Ctrl+C 停止它。

網址選項（web 版對應桌機命令列的東西）：

| 網址 | 作用 |
|---|---|
| `toms_game.html?stage=stage03` | 從另一個關卡開始 |
| `toms_game.html?stats` | bgfx 畫面統計 |
| `toms_game.html?keys=enter@60` | 在第 60 個畫面按 Enter（自動測試） |

操作和桌機相同（方向鍵/WASD、Enter、I、B、Tab、F1、Esc）。觸控和滑鼠透過
畫面上的方向盤操作；按住方向鍵會持續走。頁面還有
全螢幕按鈕和背包按鈕。

## 4. Web 版如何運作

| 主題 | Web 上的行為 | 位置 |
|---|---|---|
| 進入點 | 同一個 `main_sdl.cpp`；瀏覽器每次螢幕更新呼叫一個畫面（`emscripten_set_main_loop_arg`），而不是 while 迴圈 | `src/game/src/main_sdl.cpp` |
| 視窗 | `shell.html` 用 CSS 設定 canvas 大小（整個 viewport）；SDL 跟著它，並把繪圖緩衝區設為 CSS 大小 × `devicePixelRatio`；遊戲每個畫面檢查大小，改變時重設 bgfx；遊戲在裡面以黑邊方式放 1024×768。（不用 `SDL_WINDOW_FILL_DOCUMENT`：在非整數顯示縮放時它的偵測會失敗，canvas 停在 1×1。） | `src/game/web/shell.html`、`main_sdl.cpp` |
| 渲染器 | bgfx `OpenGL ES 3.0` = WebGL2；canvas 選擇器（`#canvas`）作為視窗 handle 傳入 | `main_sdl.cpp`、`src/engine/src/bgfx_host.cpp` |
| Shader | 只編譯 ESSL（WebGL2）profile，由主機的 `shaderc.exe` 編譯並嵌入 | `src/engine/CMakeLists.txt`、`cmake/TomsDependencies.cmake` |
| 顏色 | 沒有 sRGB（WebGL 沒有 sRGB backbuffer）；外觀和舊的 WebGL 版相同，比桌機暗 | `bgfx_renderer.cpp`、`bgfx_host.cpp` |
| 文字和 UI | RmlUi，和桌機用同樣的文件（`assets/media/ui`）；瀏覽器用裝置的字型繪製文字（`text.json` 中各語言的 `web_font`），所以不發行任何字型檔 | `src/engine/src/rml_canvas_font.*`、[08](../08_RMLUI.md#fonts) |
| 渲染器物件 | 核心程式碼的 `new Renderer()` 在 web 上也建立 bgfx 渲染器（`compat/renderer.h`） | `src/game/compat/` |
| 存檔 | IndexedDB 掛載在 `/save`（IDBFS）；核心存檔程式碼在每次寫入後同步；初始載入完成時重新讀取存檔欄 | `main_sdl.cpp`（`jsRefreshSlots`） |
| 手機 | viewport < 900×560 css px：方向鍵 ×1.2、UI ×1.5（遊戲解析度不變）；直向時顯示「請旋轉裝置」 | `main_sdl.cpp`、`src/game/web/shell.html` |
| JS hook | `jsRefreshSlots`、`jsInventory`、`jsFrameCount`、`jsTitleOpen`（可用 `Module.ccall` 呼叫） | `main_sdl.cpp` |

為什麼建置要用主機的 `shaderc.exe`：否則 bgfx.cmake 會自己把 glslang、tint、
spirv-cross 和 shaderc 編譯成 WebAssembly，再在 node 下執行。要編譯的東西大約是三
倍，在全速平行時會記憶體不足（WSL，2026-09-25），而且會拉進
`bimg_encode`，它的 etcpak 程式碼無法編譯成 wasm。用主機的 shaderc，web 版建置
只有 348 步。`bimg_encode` 也從預設建置中排除了，因為我們發行的東西都不會
編碼貼圖。

## 5. 測試和除錯

**自動 smoke 測試**（無頭 Chrome 或 Edge，不需點擊）：先啟動伺服器，然後

```bat
tools\serve_web.cmd
D:\Work\emsdk\node\24.19.0_64bit\node.exe tools\web_smoke_test.mjs http://127.0.0.1:8099/toms_game.html out\web_smoke
```

它會載入頁面、按 Enter（新遊戲）、打開遊戲內選單並存檔、重新載入頁面，
並檢查存檔是否從 IndexedDB 回來了。它會在每一步之後印出遊戲狀態，並把
截圖寫到指定的資料夾。如果遊戲沒有啟動，結束碼為 1。選填的第三個
參數設定視窗大小，例如 `844,390` 代表橫向的手機。頁面程式碼和測試必須
先等 `window.tomsReady` 再呼叫 `Module.ccall`：debug 版如果在執行環境初始化之前
呼叫 C++ 會中止。

- **在瀏覽器中設 C++ 中斷點：** 建置 `tools\build_web.cmd debug`，用
  `tools\serve_web.cmd debug` 提供，並安裝 Chrome 擴充功能 *C/C++ DevTools Support (DWARF)*。
  C++ 原始碼就會出現在 DevTools → Sources 底下。
- **主控台輸出**（`printf`、遊戲 log、bgfx 訊息）會送到瀏覽器主控台（F12）。
  致命錯誤也會顯示在頁面上。
- **無害的主控台雜訊：** `WebGL: INVALID_ENUM: getInternalformatParameter`（bgfx 在啟動時探測
  貼圖格式）、`ScriptProcessorNode is deprecated` 和「AudioContext was not
  allowed to start」（如瀏覽器所要求，音效在第一次點擊或按鍵後才開始）。
- 幾乎所有東西都可以改在 Visual Studio 的桌機版中除錯。只有
  瀏覽器特有的問題（IndexedDB、觸控、canvas 大小）需要 DevTools。

## 6. Linux / WSL

Preset `web-debug` / `web-release` 是 Linux 的對應版本（`tools/check_env.sh` 檢查
前置需求）。Linux 上通常沒有主機的 `shaderc`，所以建置會退回成把
shaderc 編譯成 wasm 再用 node 執行：在 16 GB 的機器上請用少量工作數（`--parallel 2`）建置。
傳入 `-DTOMS_HOST_SHADERC=<path>` 可改用原生的 Linux shaderc。**`bimg_encode` 修正之後
這條路徑還沒重新跑過**；Windows 路徑才是測試過的。

## 7. 問題排除

| 症狀 | 原因 | 修正 |
|---|---|---|
| 對話框 / 主控台「Emscripten SDK (emsdk) not found」 | 沒裝 emsdk、不在搜尋的資料夾中，或腳本是從 Git Bash 啟動的 | 安裝 emsdk（§1）；設定 `EMSDK`；從 cmd/PowerShell/檔案總管執行 |
| `build_web.cmd` 跳到錯的標籤或出錯 | `.cmd` 檔失去了 CRLF 換行（cmd.exe 在 `call` 之後會誤解只有 LF 的批次檔） | 根目錄的 `.gitattributes` 讓 `*.cmd` 保持 CRLF；重新 checkout 該檔案 |
| 第一次建置很久 | 還沒有主機的 `shaderc.exe`，所以先建置桌機的 shipping preset | 只會發生一次，正常 |
| Configure 警告「No host shaderc found」 | 沒有桌機版就建置 | 執行一次 `tools\build.cmd windows-shipping`，或傳入 `-DTOMS_HOST_SHADERC=` |
| 頁面停在「Loading…」或是黑的 | 從 `file://` 開啟，或瀏覽器快取中的 `.data` 檔過舊 | 用 `serve_web.cmd`；強制重新載入（Ctrl+F5） |
| 頁面是黑的，但主控台顯示 `TOMS on bgfx (OpenGL ES 3.0)` | canvas 沒有大小（在主控台檢查：`canvas.width, canvas.height`）。2026-09-26 已修正：之前的版本用 SDL 的 fill-document 模式，在非整數顯示縮放（例如 240%）時 canvas 會停在 1×1 | 用目前的 `shell.html` 重新建置；強制重新載入 |
| 重新載入後「繼續」沒有存檔 | 私密瀏覽（沒有 IndexedDB）：存檔只在本次工作階段有效 | 使用一般視窗 |
| 沒有聲音 | 瀏覽器在使用者第一次輸入前會封鎖音效 | 點一下或按個鍵 |
| 警告 `This version of cmake does not support emscripten shared libraries` | Emscripten 的工具鏈檔在講共用函式庫，我們沒有用到 | 忽略 |
