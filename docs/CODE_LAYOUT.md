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
| `footprint.h` | 100 | **S1：佔格規則（純邏輯）**。合法尺寸（1x1／2x1／1x2／2x2）、覆蓋判定 `footprintCovers()`、F5 排序鍵 `footprintSortKey()`、JSON 解析（非法尺寸拒絕並回退 1x1）、以及唯一決定來源優先序的 `resolveFootprint()`（樓層覆寫 > `data/footprints.json` 角色表 > 1x1）。 |
| `roamer.h` / `roamer.cpp` | 90 / 88 | **S1：樓層徘徊者 `toms::Roamer`（獨立類別）**。遊蕩（保留行進方向）、偵測 6 格／放棄 9 格的滯後、貪婪追擊；可走性透過 `GridQuery` 介面傳入，所以與 Stage／Game／Renderer 完全解耦、可單元測試。硬規則：一步僅在**整個佔格**可站且不出界時成立（不穿牆）。 |
| `footprint_test.cpp` | — | S1 的 88 項檢查：佔格數學、JSON 形式與拒絕、來源優先序、徘徊者（400 回合不穿牆、邊界夾制、偵測/追擊、滯後、卡住的凹槽、同種子可重現）＋**對 11 個關卡的資料驗證器**（尺寸、不可站牆、不可重疊、樓梯仍可達）。 |
| `data/footprints.json` | — | 角色級佔格表（F8）：golem／demon＝2x1、demonlord_vorkath＝2x2（含名牌名稱）。 |
| `tools/gen_floors.py` | — | **S2 產生器**：依樓層表產生 70 層 spec（`data/story/floors/Fnn.json`）＋ 60 層可玩格線（`Fnn.stage.json`）。迷宮沿用 `tools/gen_mazes.py` 的 Wilson＋BFS（同一份實作、同一套慣例：cell＝2x2 tiles、通道 2 tiles 寬），房間＝事件容器，`loops` 額外打通；右／下邊界以牆補齊到表格尺寸（V9 要求精確值）。 |
| `tools/validate_story.py` | — | **S2 驗證器**：V7（樓層↔幕↔nextFloor、首領層對應手工關卡）、V8（連通、樓梯、**鑰匙不得在自己的門後**＝防鎖死）、V9（對照樓層表）、V12（每層 ≥1 relic/whisper＋≥1 cache）＋佔格與重疊檢查。 |
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
| `imgui_layer.cpp` | — | **桌面（Vulkan + GLFW）的 ImGui 後端**：初始化、每幀 NewFrame/渲染（`imgui_impl_glfw` + `imgui_impl_vulkan`）。 |
| `imgui_web.{h,cpp}` | — | **瀏覽器（WebGL2）的 ImGui 後端**（2026-09-13 新增，M2）：ImGui 核心 + `imgui_impl_opengl3`（ES3）+ Emscripten DOM 事件橋（滑鼠/滾輪/觸控/鍵盤/焦點）。此 build 沒有 SDL/GLFW，所以輸入層是自己寫的。 |
| `title_screen.cpp` / `save_slots.cpp` / `game_settings.cpp` / `localization.cpp` | 288 / 137 / 53 / 132 | 標題畫面資料與繪製、存檔格 I/O、設定、六語言文字表。 |
| `run_state.h` / `run_state.cpp` | 110 / 152 | **S3**：`toms::RunStoryState` — 單趟遊玩的劇情狀態（選項、計數器、支線狀態、旗標、記憶碎片、樓層進度、死亡數）。條件的四個新葉子（`choiceMade`／`sideStoryState`／`counterAtLeast`／`cycleIndexAtLeast`）都向它要答案，存檔 v3 由它 `writeInto/readFrom(RunSaveData)`，重生則是 `reset(keepShards=true)`。**不認得** Renderer／Stage／Game——所以可以在測試裡直接跑。 |
| `tools/gen_story_i18n.py` | — | 掃描 story.json／章檔／樓層／事件池所引用的 `story.*`、`ev_*.text` 鍵，補進 `data/text.json`（zh_TW 權威；缺翻譯以 zh_TW 回填並標 `_todo`）。可重複執行。 |
| `*_test.cpp` | — | 純邏輯測試（不開視窗）：`camera_test`、`run_state_test`（S3，45 項）、`title_screen_test`、`equipment_test`、`mission_test`、`save_test`、`condition_eval_test`、`entity_status_test`、`story_controller_test`、`encounter_resolve_test`。 |

### 邊界規則（新增程式碼時照這個判斷）

1. **相機只認數學，不認引擎**：`Camera` 不知道 `Renderer`、`Stage` 或 `Player`。呼叫端把
   「視野像素尺寸、格線大小、跟隨格」餵進去，這讓它能被單元測試，也保證「繪製用的偏移」
   與「ease 的目標」永遠來自同一份狀態（以前這兩件事分別寫在 `draw()` 與 `update()` 裡）。
2. **共用小工具放 `game_helpers.h`，不要複製到兩個 .cpp**：以 `inline` 定義，各單元
   `using namespace toms::game_detail;` 之後照舊直接呼叫，呼叫點不用改。
3. **規則放純邏輯檔、資料放資料檔**（S1 的作法）：佔格與徘徊者 AI 都不碰 Stage／Renderer（`footprint.h`、`roamer.{h,cpp}`），因為它們必須能被 `footprint_test` 用執行期同一份程式碼驗證；而「哪隻敵人多大」屬於資料（`data/footprints.json`），樓層要覆寫單一格時寫在關卡檔的 `footprints`。**新規則請加在這些純邏輯檔裡並補測試，不要寫進 `Game`。**
4. **`Game` 類別沒有拆**：`game.h` 仍宣告全部成員；拆的是**定義所在的檔案**。這樣既拿到
   「一檔一職責」的好處，又不用做高風險的類別介面重設計。
5. **產生出來的資料要能被「執行期的解析器」驗證，不只被 Python 驗證**（S2 的作法）：`footprint_test` 用遊戲自己的 `parseStage` 讀 `data/story/floors/*.stage.json`，所以產生器的輸出若遊戲讀不動（尺寸、佔格、重疊、走不到樓梯）測試會先紅。新產生器的輸出請比照辦理（Python 驗證器＋C++ 執行期驗證器各一份）。
6. **一個定義只能有一個家**：例如 STB 實作只在 `game_assets.cpp` 定義一次
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
| 單元測試 | **19/19 全過**（重構當時 17/17；S1 的 `footprint_test` 88 項、S3 的 `run_state_test` 45 項加入後為 19/19），含新增 `camera_test: ALL PASS`（30 項）與修好的 `texture_test: ALL PASS` |
| 原生實機 | Xvfb 上真鍵盤：標題 → 新遊戲 → 迷宮；按住方向鍵走動時畫面 **17.2%** 像素改變（相機滾動），HUD/虛擬手把/底部劇情列文字完整 |
| 網頁建置 | `./build_web.sh webgl`（Emscripten 目標也吃同一份 CMake 來源清單） |

> 行為不變是這次重構的驗收標準：搬移定義時沒有改動任何數學或繪製順序，
> 畫面與改動前逐格相同（只有相機類別多了一層純函式邊界）。
