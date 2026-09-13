# TOMS 原始碼佈局（Source Layout）

> **這份文件是什麼：** 2026-09-13 重構之後，`src/game/` 與 `src/engine/` 每個檔案的職責邊界、
> 相機類別的介面，以及「新程式碼該放哪裡」的規則。目的是讓任何一個檔案都能單獨讀懂，
> 不需要先讀完一個三千行的檔案才能改一行。
>
> 相關文件：`docs/PROGRESS_REPORT.md`（進度與下一步）、
> `docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md`（系統架構）、
> `docs/IMPLEMENTATION_ROADMAP.md`（里程碑）。

---

## 1. 為什麼要重構

重構前 `src/game/game.cpp` 是 **3,104 行**的單一翻譯單元，裡面同時有：資產載入、文字/血條繪製、
整個場景繪製、輸入路由、背包 UI、商店 + 關卡選擇 + 暫停選單、戰鬥結算、劇情/任務、
標題畫面與存檔黏合、除錯 overlay……任何一個小改動都要在三千行裡找位置，而且相機的狀態和數學
（`camX_`/`camY_`/`camTargetX_`/`camTargetY_`/`viewCols_`/`cameraMode_`）散在 `Game` 裡面，
跟遊戲邏輯混在一起。

重構後：`game.cpp` **268 行**，最大檔案 **629 行**，全部低於「單檔 800 行」的門檻；
相機成為獨立類別 `toms::Camera`，有 30 項單元測試。

---

## 2. 檔案職責表（src/game/）

| 檔案 | 行數 | 職責 |
|---|---|---|
| `game.h` / `game.cpp` | 591 / 268 | `Game` 類別宣告與核心：初始化、`update()`、每幀提交、存檔黏合、TextNode 繪製轉接、F1 debug overlay 的入口。**新增跨系統邏輯前先問：這是不是某個既有單元的職責？** |
| `camera.h` / `camera.cpp` | 85 / 57 | **`toms::Camera`：迷宮相機（獨立類別）**。模式（Follow/Rooms）、縮放（`viewCols`）、目前位置與目標、視野換算、邊界夾制、平滑移動。 |
| `camera_test.cpp` | 142 | 相機 30 項純數學測試（縮放換算、邊界夾制、小迷宮置中、Rooms 分區對齊、ease 收斂不超調、與 dt 切割無關）。 |
| `game_assets.cpp` | 224 | 圖集/材質載入、sprite id 查表、關卡 JSON → `Stage` 格線與實體放置。**STB 實作的唯一定義處。** |
| `game_text_draw.cpp` | 171 | 文字與長條基本元件：UTF-8 解碼、字符繪製、寬度量測、HP 條、Power Bar、開場 splash。 |
| `game_scene_draw.cpp` | 430 | 每幀場景：`draw()`（走路/戰鬥/對話/背包的場景切換與走路場景本體）、虛擬手把 overlay、通知、關卡選擇預覽、styling spike。 |
| `game_input.cpp` | 277 | 輸入路由：觸控/滑鼠/虛擬手把命中測試、按住方向移動、世界互動、撞怪進戰鬥。 |
| `game_inventory.cpp` | 274 | 道具：拾取效果、背包 UI 與游標/使用/丟棄、名稱與說明查表。 |
| `game_store.cpp` | 629 | 商店與暫停選單：商店資料/UI/購買、關卡選擇、樓梯確認、遊戲中選單（存檔/設定/回標題）。 |
| `game_combat.cpp` | 150 | 戰鬥結算：開始/結束、Battle v2 的攻擊/防禦/超必殺判定、敵人時鐘。 |
| `game_story.cpp` | 162 | 劇情黏合：對話節點/選項/動作、任務、通知、遊戲狀態查詢。 |
| `game_title_glue.cpp` | 411 | 標題階段與存檔格黏合：標題選單狀態、存檔摘要、新遊戲/繼續、自動存檔、語言切換、標題畫面繪製。 |
| `game_helpers.h` | 139 | 多個單元共用的**inline** 小工具：`C4`、`trParam`、`readJsonFile`、`cellSprite`、`entSprite`、sprite 順序表、虛擬手把表 `GP`、`g_textGame`。 |
| `game_condition.h` | 78 | `GameConditionContext`：把 `Player` + `MetaSaveData` 接上 `toms::ConditionContext`，讓門/鑰匙與對話 `requires` 共用同一套條件求值器。 |
| `game_internal.h` | 50 | 拆分後各單元共用的 include 集合（後端選擇 + 引擎/遊戲標頭），避免每個檔案各自猜 include 順序。 |
| `main.cpp` | 333 | 桌面進入點與輸入轉換（含 device→design 座標）。 |
| `title_screen.cpp` / `save_slots.cpp` / `game_settings.cpp` / `localization.cpp` | 288 / 137 / 53 / 132 | 標題畫面資料與繪製、存檔格 I/O、設定、六語言文字表。 |
| `*_test.cpp` | — | 純邏輯測試（不開視窗）：`camera_test`、`title_screen_test`、`equipment_test`、`mission_test`、`save_test`、`condition_eval_test`、`entity_status_test`、`story_controller_test`、`encounter_resolve_test`。 |

### 邊界規則（新增程式碼時照這個判斷）

1. **相機只認數學，不認引擎**：`Camera` 不知道 `Renderer`、`Stage` 或 `Player`。呼叫端把
   「視野像素尺寸、格線大小、跟隨格」餵進去，這讓它能被單元測試，也保證「繪製用的偏移」
   與「ease 的目標」永遠來自同一份狀態（以前這兩件事分別寫在 `draw()` 與 `update()` 裡）。
2. **共用小工具放 `game_helpers.h`，不要複製到兩個 .cpp**：以 `inline` 定義，各單元
   `using namespace toms::game_detail;` 之後照舊直接呼叫，呼叫點不用改。
3. **`Game` 類別沒有拆**：`game.h` 仍宣告全部成員；拆的是**定義所在的檔案**。這樣既拿到
   「一檔一職責」的好處，又不用做高風險的類別介面重設計。
4. **一個定義只能有一個家**：例如 STB 實作只在 `game_assets.cpp` 定義一次
   （`texture.cpp` 自己那份只服務 `texture_test` 目標）。

---

## 3. `toms::Camera` 介面

```cpp
namespace toms {
class Camera {
public:
    enum class Mode { Follow = 0, Rooms = 1 };
    static Mode modeFromIndex(int index);     // 設定檔/存檔相容：0=Follow, 1=Rooms

    Mode mode() const;  void setMode(Mode);
    int  modeIndex() const;

    int  viewCols() const;                    // 視野橫向格數 = 縮放
    void setViewCols(int cols);               // 夾制到 >= kMinViewCols (4)

    // 視野換算：tile 尺寸 + 視野格數（格線比視野小的迷宮會置中）
    void viewportTiles(float viewportW, float viewportH, float topMargin, float bottomMargin,
                       float& tileSize, int& cols, int& rows) const;

    // 依跟隨格（玩家）與格線大小算出目標，並夾制在邊界內
    void retarget(int focusTileX, int focusTileY, int gridW, int gridH,
                  float viewportW, float viewportH, float topMargin, float bottomMargin);

    void update(float dtSeconds);             // 指數式平滑趨近（與影格率無關）
    void snap();                              // 立刻到位（載入關卡/切換模式）

    float x() const; float y() const;         // 目前左上角（格為單位）
    float targetX() const; float targetY() const;
    void  setPosition(float x, float y);
};
}
```

常數：`kMinViewCols = 4`、`kSmoothingMs = 120`、`kSettleEps = 0.01`、
`kTopMarginPx = 60`（HUD 文字帶）、`kBottomMarginPx = 50`（底部劇情提示列）。

**呼叫點：** `Game::cameraViewportTiles()` 是唯一把 `ren->width()/height()` 餵給相機的轉接函式
（只有 `Game` 知道 renderer）；`Game::update()` 呼叫 `retarget` + `update`；
`Game::draw()` 用 `cam_.x()/y()` 算繪製偏移；`Game::loadStage()` 呼叫 `cam_.snap()`；
F1 debug overlay 的「Visible cells」滑桿 → `setViewCols()`。

---

## 4. 驗證方式（重構的證據）

| 項目 | 結果 |
|---|---|
| 原生建置 | `tower_vulkan` 建置成功（2026-09-13） |
| 單元測試 | **17/17 全過**，含新增 `camera_test: ALL PASS`（30 項）與修好的 `texture_test: ALL PASS` |
| 原生實機 | Xvfb 上真鍵盤：標題 → 新遊戲 → 迷宮；按住方向鍵走動時畫面 **17.2%** 像素改變（相機滾動），HUD/虛擬手把/底部劇情列文字完整 |
| 網頁建置 | `./build_web.sh webgl`（Emscripten 目標也吃同一份 CMake 來源清單） |

> 行為不變是這次重構的驗收標準：搬移定義時沒有改動任何數學或繪製順序，
> 畫面與改動前逐格相同（只有相機類別多了一層純函式邊界）。
