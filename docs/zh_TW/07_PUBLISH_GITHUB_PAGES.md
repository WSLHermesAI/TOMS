# 07 — 把 web 版發佈到 GitHub Pages

> 英文原文：[07_PUBLISH_GITHUB_PAGES.md](../07_PUBLISH_GITHUB_PAGES.md)

Web 版遊戲以靜態檔案的形式發佈在本 repository 的 **`gh-pages` 分支**上。
GitHub Pages 在以下網址提供這個分支：

**https://wslhermesai.github.io/TOMS/**

（網址是 `https://<owner>.github.io/<repo>/`；如果是 fork，就跟著 fork 的擁有者和名稱。）

## 1. 一次性設定

在 GitHub 上：**Settings → Pages → Build and deployment**：

- **Source：** *Deploy from a branch*
- **Branch：** `gh-pages`，資料夾 **`/ (root)`**

本 repository 已經設定好了（舊專案就部署在那裡）。如果是新的 fork 或 repository，
請在第一次發佈建立了 `gh-pages` 分支之後再設定。

你需要對 repository 有 **push 權限**，而且 git 必須能從這台機器 push（Git for Windows 附帶的
Git Credential Manager 會請你登入一次）。

## 2. 發佈（一般做法）

雙擊 repository 根目錄的 **`publish_web.bat`**，或在命令提示字元中執行：

| 指令 | 做什麼 |
|---|---|
| `publish_web.bat` | 建置 web release（`build_web.bat`）、準備網站、**詢問**，然後 push |
| `publish_web.bat dryrun` | 建置並準備，顯示會改變什麼，**不** push 任何東西 |
| `publish_web.bat nobuild` | 不重新建置，直接發佈現有的 `Build\dist\TOMS-web` |

會發生什麼：

1. `build_web.bat` 建置 web release，並以**加上版本戳記的檔名**打包到 `Build\dist\TOMS-web\`：
   `toms_game.<commit>-<time>.js/.wasm/.data`（戳記也寫在
   `version.txt`）。只有 `index.html` 保留原檔名。
2. `tools\publish_pages.ps1` 把 `gh-pages` checkout 到 `%TEMP%` 底下的暫時 git worktree，
   所以你的工作副本和目前分支完全不會被動到。
3. 在那個 worktree 中，它刪除舊網站，**但保留上一版加了戳記的檔案**，然後
   複製進新的套件和一個 `.nojekyll` 檔（讓 GitHub 原樣提供檔案）。
4. 它列出結果網站和變更檔案的數量，然後詢問：
   `Publish <stamp> to https://wslhermesai.github.io/TOMS/ now? (y/N)`。
5. 回答 **y**：它在 `gh-pages` 上提交 `Deploy web build <stamp>` 並 push。回答其他任何東西
   則停止，不會 push 任何東西。暫時的 worktree 無論如何都會被移除。
6. GitHub 大約 **1–2 分鐘**內重建網站（GitHub → **Actions** 會顯示
   *pages build and deployment* 的執行）。然後開啟網址。

直接使用腳本：`powershell -File tools\publish_pages.ps1 [-DryRun] [-Yes] [-Keep 1]`
（`-Yes` 為自動化略過詢問；`-Keep` 設定要在線上保留幾個舊版本）。

## 3. 為什麼要加版本戳記的檔名

GitHub Pages 送出檔案時的快取時間大約 10 分鐘，瀏覽器也會保留它們。如果用
固定的檔名（`toms_game.data`），回訪的玩家可能拿到**新**的頁面，搭配快取中**舊**的
`.data` 或 `.wasm`，結果在強制重新載入之前都是黑畫面。用加了戳記的檔名，每個
版本都是新的網址，所以同一版本的檔案永遠互相匹配。

`index.html` 本身仍可能被快取幾分鐘。那時它指向的是*上一個*
版本的檔案，這就是發佈時要在線上保留上一版戳記檔案的原因：
快取的頁面在重新整理之前都能繼續運作。更舊的版本會在下一次發佈時移除。

## 4. 檢查

- 開啟 https://wslhermesai.github.io/TOMS/（1–2 分鐘後）。會出現標題畫面。
- F12 → Console：`bgfx ... ready: renderer=OpenGL ES 3.0` 和 `TOMS on bgfx`。
- 存檔在重新載入頁面後仍然存在（存在瀏覽器的 IndexedDB 中，每個玩家、每個瀏覽器各自一份）。
- 執行緒：GitHub Pages 無法送出 COOP/COEP 標頭，所以第一次造訪時 `coi-serviceworker.js`
  會自行安裝，頁面重新載入一次；之後主控台會顯示 `[jobs] 4 worker thread(s)`，且
  `crossOriginIsolated` 為 `true`。`index.html?nothreads` 會載入單執行緒版（[10](10_THREADS.md)）。
- 自動測試也可以對線上網站執行：
  `node tools\web_smoke_test.mjs https://wslhermesai.github.io/TOMS/ out\pages_smoke`

## 5. 手動發佈（不用腳本）

```bat
build_web.bat
git fetch origin gh-pages
git worktree add --detach ..\TOMS-pages FETCH_HEAD
cd ..\TOMS-pages
:: delete the old files (keep the previous toms_game.<stamp>.* and toms_game_mt.<stamp>.* if you want cached pages to keep working)
git rm -r -q .
xcopy /e /y /q ..\TOMS\dist\TOMS-web\* .
type nul > .nojekyll
git add -A
git commit -m "Deploy web build"
git push origin HEAD:refs/heads/gh-pages
cd ..\TOMS
git worktree remove --force ..\TOMS-pages
```

（刪除舊檔案時，如果想讓快取的頁面繼續運作，請保留上一版的 `toms_game.<stamp>.*` 和 `toms_game_mt.<stamp>.*`。）

## 6. 撤回有問題的版本

網站就只是 `gh-pages` 分支，所以舊版本就在前一個 commit：

```bat
git fetch origin gh-pages
git log --oneline FETCH_HEAD -5            :: find the good "Deploy web build ..." commit
git push --force origin <good-commit>:refs/heads/gh-pages
```

（`--force` 會改寫已發佈的分支；只有在要把網站退回時才這樣做。）

## 7. 問題排除

| 症狀 | 原因 | 修正 |
|---|---|---|
| 網址出現 404 | 沒有為 `gh-pages` 開啟 Pages，或第一次部署還在執行 | §1；等 Actions 執行完 |
| Push 被拒 / 要求輸入密碼 | 沒有 push 權限，或 git 還沒登入 | 透過 Git Credential Manager 登入；檢查 repository 權限 |
| 仍然顯示舊版 | 頁面被快取（最多約 10 分鐘） | 等一下，或 Ctrl+F5 |
| 黑頁面 | 見 [06 §7](../06_BUILD_WEB.md#7-troubleshooting)；在主控台檢查 `canvas.width` | |
| `No packaged web build in Build\dist\TOMS-web` | 還沒建置任何東西 | 執行 `build_web.bat`（或不加 `nobuild` 的 `publish_web.bat`） |
| 打包時出現 `Versioning: expected '...' exactly once` | 新版 Emscripten 改了載入器命名檔案的方式 | 更新 `tools\package.ps1` 中的三處取代 |
