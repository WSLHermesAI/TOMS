> 英文原文：[21_OCEAN_VIEWER.md](../21_OCEAN_VIEWER.md)

# 21. 海洋：即時的海與天空，以及它的檢視器

這是 [17 §12](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) 三個 3D 背景中的第一個。它有自己的程式 `ocean_viewer`，可以試玩與調整外觀。

**遊戲中：只有標題畫面。** 每次啟動輪流使用海與城市模型，先用海（`--title-scene=ocean` 強制用海；[08 §3](08_GLTF.md)）。它用預設外觀，較暗、雲較少以便看清選單，桌面版品質 high，網頁版 low。Android 目前一律用城市。戰鬥還沒有使用它（Q69）。

## 1. 執行

建置 `ocean_viewer` 目標，然後執行 `Build\windows-release\bin\ocean_viewer.exe`。面板上每個設定都有滑桿，並有四個預設：day、sunset、storm、calm。

| 操作 | 作用 |
|---|---|
| 左鍵拖曳 | 轉動視角 |
| 滾輪 | 攝影機上下 |
| H | 隱藏面板 |
| Space | 暫停 |
| Esc | 離開 |

**Save settings** 會寫入 `tools/ocean_viewer/ocean_settings.json`，檢視器啟動時會讀取它。

**在瀏覽器中：** 執行 `tools\build_ocean_web.cmd`，再執行 `tools\serve_web.cmd ocean`。同樣的選項放在網址中：
`ocean_viewer.html?preset=storm&quality=low`。網頁版的面板一開始是收合的。「Save settings」會存在該瀏覽器中。

不開視窗產生圖片（視窗保持隱藏）：`--preset=storm --no-ui --frames=120 --fixed-dt=33 --record=<資料夾>`，再執行
`python tools/frames_to_gif.py <資料夾> out.gif`。其他選項列在 `tools/ocean_viewer/main.cpp` 開頭。

## 2. 運作方式

| 部分 | 內容 | 位置 |
|---|---|---|
| 波浪 | Tessendorf 的 FFT 海洋，用 JONSWAP 波譜與 cos^2s 方向分布，透過 job system 在 CPU 上計算。三塊正方形區塊（700、110、19 公尺），各自負責一段波長，所以看不出重複 | `src/core/engine/ocean_sim.*` |
| 泡沫 | 出現在水面被擠壓處（Jacobian 變低），之後隨時間淡去 | `ocean_sim.cpp`、`fs_ocean_water.sc` |
| 繪製 | 以下全部，以及品質表 | `src/engine/src/ocean_renderer.*`、`src/engine/shaders/*ocean*` |
| 水面網格 | 鋪在畫面上的格子：每個頂點沿視線到平坦海面，再由波浪移動。所以能延伸到地平線，細節在畫面上分布平均 | `vs_ocean_water.sc` |
| 水的顏色 | 用 Fresnel 混合天空反射與水中的光、薄浪頂透出的光、太陽閃光（GGX）、往地平線的霧氣 | `fs_ocean_water.sc` |
| 天空 | Preetham 日光模型、低太陽的暖色、一層平面的雜訊雲、太陽圓盤。不需要圖片 | `ocean_common.sh`、`fs_ocean_sky.sc` |

波浪的圖片是 RGBA16F，含各級 mip，每個畫格上傳。

## 3. 品質：給網頁版與手機

用 `--quality=` 或面板選擇（`ocean_renderer.h` 的 `g_arrQualities` 表）：

| 品質 | 區塊 | 網格列數 | 雲的雜訊：天空／水中 | 波浪模擬 |
|---|---|---|---|---|
| high（桌面版） | 3 塊 256 | 360 | 5 / 3 | 每個畫格 |
| medium（網頁版，電腦） | 3 塊 128 | 220 | 4 / 2 | 每個畫格 |
| low（網頁版，手機） | 2 塊 128 | 140 | 3 / 無 | 每 2 個畫格 |
| lowest | 2 塊 64 | 90 | 2 / 無 | 每 2 個畫格 |

- **自動（網頁版預設）：** 從 medium 開始，手機從 low 開始。如果畫格平均超過 24 毫秒達 2 秒，就降一級。不會自己升回去，所以畫面不會一直變。
- **手機以每個 CSS 像素一個像素繪製，** 而不是 2–3 個裝置像素。這是最大的節省，最多只要九分之一的像素。
- **網頁版只有一個執行緒、沒有 MSAA，** 並在著色器中做 gamma 編碼，因為 WebGL 沒有 sRGB 畫面。

## 4. 與負責人指定的專案比較

[kentril0/WaterSurfaceRendering](https://github.com/kentril0/WaterSurfaceRendering)（MIT）是方法上的參考。沒有複製它的程式碼，因為它只支援
Vulkan，而本專案透過 bgfx 繪製。

| | 該專案 | 這裡 |
|---|---|---|
| 繪圖 | Vulkan | bgfx：Direct3D 11 與 12、Vulkan、OpenGL |
| FFT | FFTW（GPL） | 自己寫的小型 FFT，沒有授權問題 |
| 波譜 | Tessendorf 的 choppy waves | 相同，加上 JONSWAP |
| 區塊 | 一塊 | 三塊 |
| 泡沫 | 列為未來工作 | 有 |
| 天空 | Preetham | Preetham，加上雲與低太陽色調 |
| 水底地形 | 有（value noise） | 沒有：只有深海 |

## 5. 數據

在負責人的電腦上（8 個工作執行緒），1920 x 1080，3 塊 256 x 256，4x MSAA，測量 400 個畫格：

| | 每畫格毫秒 |
|---|---|
| 波浪 | 6.0 |
| mip 圖片與半精度浮點 | 1.4 |
| 整個畫格 | 9.4 |

「整個畫格」是沒有 vsync 時執行的實際時間，包含啟動。

**網頁版**（1.9 MB wasm、216 KB JS，沒有資料檔）在無頭 Chrome、同一台電腦的 GPU 上，單一執行緒，上限 60 fps：

| 執行 | 品質 | fps | 波浪毫秒 |
|---|---|---|---|
| 電腦，1254 x 620 | high | 43 | 16.7 |
| 電腦 | medium（自動） | 60 | 4.2 |
| 電腦 | low | 60 | 2.9 |
| 電腦 | lowest | 60 | 0.6 |
| 電腦，CPU 慢 4 倍 | medium | 35 | 18.0 |
| 模擬手機，412 x 915 | low（自動） | 60 | 2.5 |
| 模擬手機，CPU 慢 4 倍 | low（自動） | 60 | 10.8 |
| 模擬手機，CPU 慢 6 倍 | low（自動） | 53 | 21.6 |

**模擬手機有手機的畫面與 user agent，CPU 也調慢，但 GPU 是這台電腦的。** 在 Chrome 的軟體 GPU（非常慢的最壞情況）上，自動品質從 medium 降到 lowest（1254 x 620 時 22 fps）。**還沒有在真正的手機上執行過。**

## 6. 測試

- `unit.ocean_sim_test`：FFT、高度變異數與波譜積分的比較（撰寫時誤差在 2% 內）、斜率與高度、可重複執行、泡沫。
- `smoke.ocean_viewer`：能啟動、著色器能載入、能繪製。沒有參考圖，因為外觀還在挑選。
- `smoke.title_ocean`：遊戲標題加上 `--title-scene=ocean`；海沒有啟動就會失敗。同樣沒有參考圖。
- `web.ocean_viewer`（只在網頁測試中）：網頁在無頭 Chrome、真實 GPU 上：能繪製，沒有著色器失敗。
  `tools/ocean_viewer/web/ocean_web_test.mjs` 也接受 `--phone`、`--cpu=<慢幾倍>`、`--software`，並印出上面的數字。

手動檢查：四個預設在 Direct3D 11 的圖片；day 在 Direct3D 12、Vulkan 與 OpenGL。
它不在 Android 程式中：手機使用瀏覽器網頁。

## 7. 尚未完成

- 放進戰鬥：主程式要在標題使用的場景 view 中繪製它，並移除戰鬥的黑色背景。待決問題是 [Q69](../../project/zh_TW/13_OPEN_DECISIONS.md)。
- 「Save settings」只屬於檢視器：標題不會讀取 `ocean_settings.json`。
- 水花，以及漂在水上的物體。
- 另外兩個背景（天空、火山）。這裡的天空可以當天空背景的起點。
