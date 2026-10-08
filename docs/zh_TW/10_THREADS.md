# 10 — 執行緒：工作系統，以及 web 上的執行緒

> 英文原文：[10_THREADS.md](../10_THREADS.md)

遊戲的每個畫面在一個執行緒上執行：輸入 → 更新 → UI → 送出 bgfx。bgfx 也在那個執行緒上渲染（見
`bgfx_host.cpp`）；一個畫面大約 0.4 ms，在這個示範中用渲染執行緒沒有任何好處。為了更重的
工作（之後的 3D：剔除、蒙皮、載入），有一個**工作系統**在每個平台上執行同一份程式碼，
只要有執行緒的地方就會用多執行緒。

## 工作系統

`src/core/engine/job_system.h`：

```cpp
toms::JobSystem::parallelFor(count, [&](int i) {   // blocks until all count calls have returned
    results[i] = work(inputs[i]);                  // each i runs once, on a worker or the caller
});
```

（`parallelFor` 會等到全部 `count` 次呼叫都回傳才繼續；每個 `i` 只執行一次，在某個 worker 或呼叫端上。）

- `GameSession::start` 啟動它一次：硬體執行緒數 − 1 個 worker（最多 8 個）。桌機的 log 會顯示 `[jobs] 8
  worker thread(s)`。
- **零個 worker 是正常模式。** 單執行緒的 web 版永遠是零個，這時 `parallelFor` 就只是
  在呼叫端跑迴圈。程式碼永遠不需要為「有沒有執行緒」寫 `#ifdef`。
- **規則：**
  - 函式主體必須能同時在多個執行緒上安全執行：只寫入你自己的第 `i` 格，不要碰
    共用的容器。
  - 一次只執行一個 `parallelFor`；主體內的 `parallelFor` 會直接就地執行。
  - bgfx 和 RmlUi 的呼叫留在主執行緒。
- **目前用在** `Game::loadAssets`：sprite PNG 平行解碼。`stb_image` 的錯誤狀態是每個
  執行緒各自一份。
- **測試** 由 `unit.job_system_test` 負責：每個索引剛好執行一次、連續 2,000 個工作、巢狀工作、零個
  worker，以及工作真的分散到多個執行緒上。

## Web 上的執行緒

WebAssembly 的執行緒是共用一塊記憶體（`SharedArrayBuffer`）的 Web Worker。瀏覽器只在
**跨來源隔離（cross-origin isolated）** 的頁面上允許這樣做：伺服器必須送出

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

沒有這兩個標頭，多執行緒版根本無法啟動：「SharedArrayBuffer transfer requires
self.crossOriginIsolated」。所以有兩個 web 版，由頁面挑選其中一個：

| 版本 | Preset / 指令 | 輸出 |
|---|---|---|
| 單執行緒 | `tools\build_web.cmd`（`web-release-windows`） | `Build\web-release-windows\bin` |
| 多執行緒 | `tools\build_web.cmd mt`（`web-release-mt-windows`：`-pthread`、預先啟動 5 個 worker，工作系統用其中 4 個） | `Build\web-release-mt-windows\bin` |

`build_web.bat` 兩個都建置，`tools\package.ps1` 把兩個都放進 `Build\dist\TOMS-web`（`toms_game.<stamp>.*` 和
`toms_game_mt.<stamp>.*`）。玩家只會下載其中一個。`index.html` 在載入時決定：

```js
threaded = !nothreads && self.crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'
```

| 頁面由…提供 | 玩家拿到 |
|---|---|
| 會送出這兩個標頭的主機（itch.io、Netlify、Cloudflare Pages、自己的伺服器） | 直接拿到多執行緒版 |
| 無法送出標頭的主機，例如 **GitHub Pages** | `coi-serviceworker.js`（在套件中）會安裝一個加上標頭的 service worker；第一次造訪時頁面會自己重新載入一次，之後拿到多執行緒版 |
| 封鎖 service worker 的瀏覽器（某些私密模式、某些 App 內建瀏覽器） | 單執行緒版：同樣的遊戲，只是沒有 worker |
| `index.html?nothreads` | 刻意使用單執行緒版（用於比較或排除問題） |

第一次造訪沒有標頭的主機時，頁面最多會等 4 秒等那次重新載入，然後才開始
下載單執行緒版。這樣就不會有人下載用不到的版本。

在瀏覽器中，工作系統只使用預先啟動的 worker：瀏覽器的主執行緒無法等待新的
worker 被建立，因為 worker 要等主執行緒返回之後才會啟動。基於同樣的原因，除了 `parallelFor` 之外，
不要讓主執行緒阻塞在任何東西上。

## 試試看

```bat
tools\build_web.cmd mt          :: the threaded build
tools\serve_web.cmd mt          :: serves it cross-origin isolated (tools\serve_web.py) and opens it
build_web.bat                   :: both builds + the package
tools\serve_web.cmd dist        :: the package, as a player gets it
```

在瀏覽器主控台中：`crossOriginIsolated`（true = 允許執行緒）、`tomsThreaded`（頁面選了哪個版本），
以及 `Module.ccall('jsWorkerCount','number',[],[])`（多執行緒版是 4，否則是 0）。

`tools\test.cmd` 會檢查以上全部（`-L web`；[09](09_TESTS.md)）：

| 測試 | 檢查什麼 |
|---|---|
| `web.smoke` | 單執行緒版：0 個 worker |
| `web.smoke_mt` | 有標頭的多執行緒版：4 個 worker |
| `web.page_coi` | 在**沒有**標頭的伺服器上（像 GitHub Pages）的套件：service worker 重新載入後變成多執行緒 |
| `web.page_nothreads` | 加上 `?nothreads` 的套件：0 個 worker |

## 成本和限制

- **兩個版本：** `build_web.bat` 大約多花一倍時間。上傳量加倍（兩個加起來約 10 MB），但
  下載量不變：每個玩家只抓一個。
- 多執行緒版會警告：`-pthread` 加上可成長的記憶體，會讓 JavaScript 存取 wasm 記憶體稍微
  慢一點。在這裡沒有影響。
- bgfx 在 web 上維持單執行緒：它的 WebGL context 屬於頁面的主執行緒。執行緒是給
  工作系統用的。
- 還沒在真正的手機或 Safari 上試過。Safari 從 15.2 起支援跨來源隔離。
