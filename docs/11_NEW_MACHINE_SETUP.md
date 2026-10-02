# 11 — 在新電腦上從零開始（AI 執行版）

> 這份文件的目的：把這份**交給 AI**，它就能在一台新電腦上把 TOMS clone 下來、裝好、建置、跑起來。
> 工具鏈的英文細節在 [`02_INSTALL_WINDOWS.md`](02_INSTALL_WINDOWS.md)；本文件補的是**「換一台電腦」會漏掉的事**，
> 尤其是 git 相關設定（已在本機實測，不是通則）。
>
> 繁體中文敘述、指令與檔名保持英文。

---

## 0. 一句話總結

**最省事：雙擊 [`setup_new_pc.bat`](../setup_new_pc.bat)**（可以單獨下載這一個檔到新電腦：
`https://raw.githubusercontent.com/WSLHermesAI/TOMS/main/setup_new_pc.bat`）。
它會用 winget 安裝缺少的 Git / Visual Studio 2026（winget `Microsoft.VisualStudio.Community`；C++ 工作負載、CMake tools、Windows SDK）、開啟長路徑、
clone、（可選）產生 SSH 金鑰並引導貼到 GitHub、建置與測試。
**需要管理員權限的步驟會先跳出對話框提醒**，看到 Windows「是否允許此應用程式變更您的裝置？」時按「是」。
所有管理員工作集中在**一次** UAC 內完成；clone 與金鑰則以一般使用者身分執行（避免 git 的 *dubious ownership*）。
紀錄檔：`%TEMP%\TOMS_setup.log`、`%TEMP%\TOMS_setup_admin.log`。

途中會問 **AI 美術工具（ComfyUI，可略過）**：(1) 在本機安裝 ComfyUI＋下載模型（約 13 GB，需 8 GB 以上 VRAM 的 NVIDIA 顯卡）、
(2) 使用另一台機器上的 ComfyUI 伺服器（輸入網址，會檢查連線與伺服器缺哪些模型）、(3) 略過。
之後可隨時重跑 `tools\setup_ai_art.cmd`。設定存在使用者環境變數 `COMFYUI_URL`／`COMFYUI_SERVER`（AIGamestyle 的腳本讀後者）。
不需要管理員權限。

手動做的話：

```bat
git clone --recurse-submodules git@github.com:WSLHermesAI/TOMS.git
cd TOMS
git config core.longpaths true
tools\check_env.cmd
tools\build.cmd windows-release
tools\test.cmd
```

跑 `Build\dist\TOMS-windows\toms_game.exe`（或 `Build\windows-release\bin\`）就算成功。
**不需要** Git LFS、不需要處理 symlink、不怕大小寫衝突、換行已經由 `.gitattributes` 處理好。

---

## 1. 給 AI 的指令稿（直接複製貼上）

把下面整段貼給新電腦上的 AI：

```
你要在一台全新的 Windows 電腦上把 TOMS 建置起來並回報結果。

專案：git@github.com:WSLHermesAI/TOMS.git（若無 SSH key，改用 https://github.com/WSLHermesAI/TOMS.git）
參考文件：docs/02_INSTALL_WINDOWS.md（工具鏈）、docs/11_NEW_MACHINE_SETUP.md（本流程）、docs/05_TROUBLESHOOTING.md

請照這個順序做，每一步都要真的執行並貼出真實輸出，不要用「應該可以」代替：

1. clone（含 submodule）
   git clone --recurse-submodules <repo-url> TOMS
   → 驗收：git -C TOMS submodule status 有輸出；git -C TOMS status -sb 乾淨（沒有 Modified/Untracked）
2. git 設定
   git -C TOMS config core.longpaths true
   → 理由：建置目錄 Build\...\_deps\... 很深，Windows 未開長路徑會在建置時失敗
3. 環境檢查
   tools\check_env.cmd          （會開彈窗列出缺少的項目與下載連結）
   → 驗收：必需要素全 [ OK ]；缺 Visual Studio 2026 + Desktop development with C++、CMake、Ninja、Git for Windows
4. 設定 + 建置（第一次會下載並編譯 bgfx / SDL3 / Dear ImGui / glm，數分鐘）
   tools\build.cmd windows-release
   → 驗收：exit code 0，且 Build\windows-release\bin\ 內有 toms_game.exe
5. 測試
   tools\test.cmd                     （全部，約 45 秒）
   tools\test.cmd windows-release -L unit   （只跑單元測試，不到 1 秒）
   → 驗收：貼出 ctest 的 pass/fail 統計
6. 執行遊戲
   跑 Build\windows-release\bin\toms_game.exe（或 build_windows.bat 產出的 Build\dist\TOMS-windows\toms_game.exe）
   → 驗收：視窗開啟、看得到標題畫面。若失敗，貼出完整錯誤，不要猜。

回報時請明確區分三件事，不要混在一起講：
  (a) 建置成功（編譯通過）  (b) 程式可執行（真的開起來）  (c) 玩家可見的行為（畫面/操作/存檔）已經驗證
沒做到的項目就直接說「尚未驗證」。
```

---

## 2. Git 相關設定（本文件重點）

### 2.1 clone 與 submodule

```bash
git clone --recurse-submodules git@github.com:WSLHermesAI/TOMS.git
# 已經 clone 過、忘了加參數：
git submodule update --init --recursive
```

- `.gitmodules` 內有一個 submodule：`AIGamestyle` →
  `https://github.com/fatmingwang/AIGamestyle.git`。
- 一般 clone **不會**下載它，`AIGamestyle/` 會是**空目錄**。
- **但它不是建置必要**：已實測 `CMakeLists.txt`、`cmake/`、各 `src/*/CMakeLists.txt`
  **完全沒有引用**它；本機目前也未初始化，引擎照樣建置成功。
  → 加 `--recurse-submodules` 是最保險的做法，漏掉也不影響建置。

### 2.2 不需要做的事（已實測，避免白花時間）

| 項目 | 結論 | 依據 |
|---|---|---|
| **Git LFS** | 不需要 | `.gitattributes` 沒有 lfs 規則；最大的 tracked 檔案是 `src/third_party/miniaudio/miniaudio.h`（3.9 MB） |
| **symlink 設定**（`core.symlinks`） | 不需要 | 索引中 symlink 數 = **0** |
| **大小寫衝突** | 沒有 | 大小寫不敏感的重複路徑 = **0**（Windows/Linux 之間安全） |
| **換行** | 已由 `.gitattributes` 處理 | `*.cmd`/`*.bat`/`*.ps1` → `eol=crlf`；`*.sh` → `eol=lf`（所以 `.cmd` 腳本跨平台 clone 也不會被 LF 弄壞） |

> 提醒：**不要**把 `core.autocrlf` 設成 `false`/`input` 後期待它覆蓋 `.gitattributes`——
> attributes 優先，反過來才對。Git for Windows 的預設值可以直接用。

### 2.3 建議的設定

```bash
git config --global core.longpaths true   # Windows 必設；建置樹很深
```

其他若需要：

```bash
git config --global user.name  "你的名字"
git config --global user.email "你的信箱"
```

### 2.4 認證方式（本機實測）

> 金鑰的原理、在別台電腦上的三種做法、Windows/WSL 兩套 `~/.ssh` 的差異與排錯：
> 見 [`12_SSH_KEY_SETUP.md`](12_SSH_KEY_SETUP.md)。

**本機用的是 SSH 金鑰，不是密碼也不是 token：**

| 項目 | 實測結果 |
|---|---|
| remote | `git@github.com:WSLHermesAI/TOMS.git`（SSH） |
| 金鑰 | `~/.ssh/id_ed25519`（ED25519，權限 `600`）＋ `~/.ssh/id_ed25519.pub`（`644`） |
| 指紋 | `SHA256:0AhSQKxzlEc4C+1+uP8z8wmeWOmh3SKnVAjubwNbFvg` |
| 對應的 GitHub 帳號 | **WSLHermesAI**（`ssh -T git@github.com` 會回 `Hi WSLHermesAI!`） |
| credential helper | 未設定；`~/.git-credentials` 不存在 → **沒有儲存任何密碼或 PAT** |
| `~/.ssh/config` | 沒有（用預設 identity 與預設主機設定） |

驗證方式（**注意**：GitHub 不提供 shell，成功時 SSH 仍回傳非 0；要看輸出文字）：

```bash
ssh -T git@github.com
# 期望：「Hi WSLHermesAI! You've successfully authenticated, but GitHub does not provide shell access.」
```

**重要：這個 repo 是公開的**（匿名 HTTPS 讀取測試通過）。
所以在新電腦上：

- **clone 完全不需要認證**（用 HTTPS 匿名即可）：
  `git clone --recurse-submodules https://github.com/WSLHermesAI/TOMS.git`
- **只有要 push 時才需要認證**。三種做法：
  1. **在該機器產生新金鑰**（建議，一機一鑰）：
     `ssh-keygen -t ed25519 -C "<你的信箱>"`，把 `~/.ssh/id_ed25519.pub` 的內容貼到
     GitHub → Settings → SSH and GPG keys → New SSH key；remote 用 SSH URL。
  2. **複製既有金鑰**（最快，但等於共用同一身分；私鑰權限必須 `600`）。
  3. **HTTPS + Personal Access Token**：`git remote set-url origin https://github.com/WSLHermesAI/TOMS.git`，
     設定 `git config --global credential.helper manager`（Windows）後首次 push 輸入 PAT 當密碼。

本機的金鑰在無互動（`BatchMode`、沒有密碼提示）下就能通過認證，**請把 `~/.ssh/id_ed25519` 當成機密**：
不要提交進版控、不要貼給 AI 或聊天視窗、不要放到共用資料夾。

### 2.5 大小與完整性（實測）

- `size-pack` 約 **20.12 MiB** → clone 很快（本機 `.git` 顯示 205 MB 是舊的鬆散物件，不是下載量；
  想瘦身可 `git gc`）。
- tracked 檔案數 **479**；其中 `assets/` **243 個檔案都有進 git**，
  唯一沒進版控的是 `assets/wqy-zenhei.ttc`，而**沒有任何程式引用它**（只有舊進度報告提到）。
  → **新電腦 clone 下來就具備建置與執行所需的全部原始碼與素材。**

---

## 3. 工具鏈（不在 git 裡，需要另外安裝）

| 用途 | 需要 | 說明 |
|---|---|---|
| 遊戲（必備） | Windows 10/11 x64、Visual Studio 2026 + *Desktop development with C++*、CMake 3.24+、Ninja、Git for Windows | 見 `docs/02_INSTALL_WINDOWS.md` §1 |
| 編輯器 `toms_editor` | **Qt 6.5+（建議 6.8 LTS，MSVC 2022 64-bit kit）** | 沒裝 Qt 也能建置遊戲，只是不會建編輯器 |
| 網頁版 | Emscripten SDK | 見 `docs/06_BUILD_WEB.md` |
| Android | JDK 17 + Android SDK/NDK 27 | 見 `docs/07_BUILD_ANDROID.md` |
| renderer | Vulkan runtime 或 D3D11/12 | 預設即可；`--renderer=vulkan` 需要驅動支援 |

磁碟：約 **5 GB** 可用空間。

---

## 4. 建置與執行（Windows）

| 指令 | 產出 |
|---|---|
| `tools\check_env.cmd` | 環境檢查（`-NoGui` 為純文字） |
| `tools\build.cmd windows-release` | `Build\windows-release\bin\`（`toms_game.exe`） |
| `tools\build.cmd windows-debug` | 同上，Debug |
| `build_windows.bat` | 出貨包 `Build\dist\TOMS-windows\`（含 `assets\` 與 MSVC runtime，可整個資料夾複製走） |
| `build_web.bat` / `publish_web.bat` | `Build\dist\TOMS-web\` / 發佈到 GitHub Pages |
| `tools\test.cmd [preset] [-L unit]` | CTest（VS 的 Test Explorer 也看得到同一批測試） |

CMake preset 名稱（`CMakePresets.json`）：
`windows-debug`、`windows-release`、`windows-shipping`、`ci-windows`、
`web-debug`/`web-release`（Linux/WSL2）、`web-*-windows`（Windows）、
`android-debug`、`android-x86_64-debug`、`android-release`。

用 Visual Studio 直接開資料夾也可以：VS 會讀 `CMakePresets.json`。

---

## 5. 驗收清單（怎樣算「成功了」）

依序確認，**沒過的就說沒過**：

1. `git status` 乾淨（不會因為建置而變髒——所有產出都在已 ignore 的 `Build/` 底下）。
2. `tools\check_env.cmd` 必需要素全 OK。
3. `tools\build.cmd windows-release` → `exit 0` 且存在 `Build\windows-release\bin\toms_game.exe`。
4. `tools\test.cmd` → CTest 全綠。
5. 執行 `toms_game.exe` → 標題畫面出現（**玩家可見**才算完成）。
6. 隨便動一下（點擊移動、開背包）→ 確認輸入有反應，不是靜態畫面。

---

## 6. 常見陷阱（含本次實測踩到的）

### Windows
- **路徑太長** → `core.longpaths true`（見 2.3）。建置樹 `Build\*\_deps\*` 很深。
- **Qt 找不到** → 編輯器不會被建置（其餘照常）。確認 Qt kit 是 **MSVC 2022 64-bit**。
- **git 不在 PATH** → 第一次 configure 要 clone bgfx/SDL3/ImGui/glm，會直接失敗。
- **第一次 configure 需連 github.com**；離線環境要先在有網路的機器建置過。

### Linux / WSL2（無 root）
- **`ccache` 被設成編譯器**：若 `CMAKE_CXX_COMPILER` 或環境變數 `CC`/`CXX` 指向 `ccache`，
  Qt 的 autogen 會失敗（`ccache: invalid option -- 't'`）。明確指定 `-DCMAKE_CXX_COMPILER=/usr/bin/g++`。
- **headless 執行編輯器會 SIGSEGV**：bgfx 自動選到 Vulkan 時，若系統的 Vulkan ICD 清單裡有不適用的驅動
  （本機是 `asahi_icd.json`）會在 `SwapChainVK::createSurface()` 崩潰。
  解法：`TOMS_RENDERER=opengl`（編輯器支援的 renderer 選擇：`auto|d3d11|d3d12|vulkan|opengl|gles`）。
- **無 root 裝相依**：`apt-get download` + `dpkg-deb -x ~/opt/x11root`，
  但 `.deb` 內的**絕對 symlink 在使用者前綴裡會斷鏈**，且 Ubuntu 24.04 有些套件改名（`libasound2t64`、
  `pkgconf-bin`、`libsource-highlight4t64`…）。判斷方式：`readlink -f <lib>` 看是否真的解析得到。
- **shader 產生需要 host shaderc**；桌面版 shader profile 已依 `WIN32` 分支
  （Linux 不含 `s_5_0`，因為那需要 shaderc 的 D3D4Linux 支援）。

### 一律適用
- **不要**把 `Build/`（建置、打包、AI 美術實驗）、`*.deb`、`.spv` 提交進版控。
- 本專案**沒有** LFS、沒有 symlink、沒有大小寫衝突——遇到相關問題請先懷疑環境，不是 repo。

---

## 7. 這份文件的事實來源

以下數字都是在 `/home/fatming/Desktop/TOMS`（`main`，`7968c16`）實際跑指令得到的：
`git submodule status`、`git count-objects -vH`、`git ls-files`（479 檔）、
`git ls-files -s | awk '$1=="120000"'`（symlink = 0）、
大小寫重複檢查（0）、`git ls-tree -r -l HEAD`（最大 3.9 MB）、
`git ls-files assets | wc -l`（243）對比磁碟 244、
`CMakePresets.json` 的 preset 名稱、以及 `.gitattributes` 內容。
