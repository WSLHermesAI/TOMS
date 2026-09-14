# 美術與能力設計書（ART & ABILITY DESIGN）

> 版本：v1.0　｜　範圍：**50 種敵人**（含 10 位獨特首領）、**20 件裝備**（武器／護甲／飾品）、**36 種狀態**，以及由此推算的**全部美術資產清單**
> 搭配：`docs/story/STORY_BIBLE.md`（70 層／10 幕）、`docs/story/SIDE_STORIES.md`、`docs/story/STORY_DATA_SCHEMA.md`（`enemies.json` / `equipment.json` / 新 `status.json` 欄位）
> 用途：美術照此製作、程式照此接資料、企劃照此配能力值——**這份文件同時是美術規格書與數值表**

---

## 0. 為什麼要部件制（本文件的核心策略）

需求明確：**部分敵人外觀相同、只靠顏色與部位縮放區分，以降低檔案量**。本作因此在敵人美術上採三層策略：

| 層 | 內容 | 檔案成本 |
|---|---|---|
| ① **共用骨架（body rig）** | 9 組骨架，每組含完整動畫（待機／移動／攻擊／受擊／死亡） | 一份動畫給多個敵人共用 |
| ② **部件（part）** | 頭／角／武器／披風等靜態部件，可獨立縮放並掛載 | 每件 1 張（首領 3 張） |
| ③ **著色（shader tint / hue）** | 同一份骨架以 shader 換色（含幕別氛圍、精英光暈） | **0 張** |

**節省計算（實際數字）**

- 天真做法：50 個敵人各做一組動畫 = 50 × 22 = **1,100 幀**
- 本作做法：9 組骨架 × 22 幀 = **198 幀**（其餘 41 個敵人靠部件＋著色＋縮放）
- **省下 82%** 的敵人動畫量；全案動畫總量從 1,746 幀降到 **844 幀（−52%）**

> 唯一例外是**首領**：10 位首領**絕不共用骨架**（見 §3.4），因為「首領必須獨特」是明確要求。

---

## 1. 成品規格與製作管線

### 1.1 尺寸與格線

| 類別 | 來源尺寸 | 遊戲內顯示 | 備註 |
|---|---|---|---|
| 地圖格（tile） | 32×32 | 32×32（設計解析度 1024×768，相機可捲動） | 對齊格線，無透明邊 |
| 一般敵人（現場） | 32×32 | 24–28（tile 內縮） | 與既有 `spriteQuad(..., sSize, sSize, ...)` 一致 |
| 一般敵人（戰鬥） | 32×32 | **96×96**（3× 最近鄰放大） | 放大必須用 nearest，維持像素感 |
| 首領（戰鬥） | **64×64** | 128×128（2×） | 首領另有一組戰鬥專用大圖 |
| 道具／裝備圖示 | 16×16 或 32×32 | 32×32 | 背包卡片用 |
| 狀態圖示（icon） | 32×32 | 20–24 | HUD 與敵人頭上 |
| UI 節點／按鈕 | 32×32 / 48×48 | 依 UI | 技能樹節點、鍛造、樞紐 |
| 結局插圖 | 512×288 | 全寬 | 15 張（可先以漸層＋剪影替代） |

**像素規範**：輪廓線 1px（深色 `#1b1420`）、受光面朝左上、陰影色為基色 ×0.7、禁用抗鋸齒、調色盤上限 24 色／敵人（見 §7）。

### 1.2 部件制規格（敵人）

**骨架分組（9 組，含各自適用的幕）**

| rig id | 骨架 | 適用敵人 | 部件槽位 | 動畫幀數 |
|---|---|---|---|---|
| `rig_slime` | 軟體類 | 史萊姆、井蛙、腐葉蟲、晶石獸、星塵獸 | 表面裝飾 ×2 | 20 |
| `rig_bat` | 飛行類 | 暗影蝙蝠、書頁妖、鐘靈、蝠母下屬 | 翼飾 ×2、尾 | 22 |
| `rig_bone` | 骨類 | 骷髏兵／弓手、亡者衛、執卷骨僧、帝骨衛 | 頭飾、武器 ×2 | 22 |
| `rig_humanoid` | 人形類 | 村犬（獸）、王國士兵、叛軍弓手、鎧甲戰士、神殿守衛、誦經者 | 頭、武器、披風 | 24 |
| `rig_beast` | 獸類 | 荊棘藤、林獵蛛、墓穴爬行者、軍犬、洞窟潛伏者 | 背飾、爪 | 22 |
| `rig_construct` | 造物類 | 石巨人、投石傀儡、鏽甲衛、書架巨像、道兵殘骸 | 肩甲、武器、核心 | 24 |
| `rig_spirit` | 靈體類 | 怨靈、哭喪女、回聲之影、幻影修士、心魔系雜兵 | 面紗、尾焰 | 22 |
| `rig_demon` | 魔類 | 小惡魔、墨無殤之影、王座之影、天劫餘燼 | 角 ×2、翼、爪 | 24 |
| `rig_lord` | 統帥類 | 前世道兵王、軍營叛將、鏡像衛、前室侍從 | 披風、冠、武器 ×2 | 24 |

**部件規則**
1. 部件一律**獨立 PNG**、透明背景、**自身軸心在底部中心**（方便掛載）。
2. 掛載靠 `appearance.parts[]`（部件 id、相對座標、縮放、色相偏移）——**縮放是變體手段之一**：
   - 例：`E-13 骷髏弓手` = `rig_bone` 骨架 ＋ `part_bow`（縮放 1.30，手臂 ×1.15）→ 剪影與骷髏兵明顯不同，但**沒有新動畫**。
3. **剪影必須可辨識**（§7 守則）：同骨架的敵人必須靠部件或色相在 1 秒內分辨。
4. 首領**不使用部件制**（整組獨立）。

### 1.3 著色（shader 變體）

現行 sprite shader 為 `output = tint * texture`，因此：

| 變體類型 | 做法 | 不需新檔 |
|---|---|---|
| 同族不同色（例：史萊姆／毒史萊姆／冰史萊姆） | `tint = hsl2rgb(hue + 45°, s, l)` | ✅ |
| 精英 | 原色 ＋ 金邊光暈（`rimLight=1`）＋ 尺寸 ×1.15 | ✅ |
| 幕別氛圍 | 場景層與敵人同時乘上幕色（一～三幕偏青、六～八幕偏橙、九～十幕偏紫） | ✅ |
| 侵蝕／中毒狀態 | 疊加 `overlay_color` 與 shader 內的水平擾動 | ✅ |
| 首領第二階段 | 同骨架換 tint ＋ 追加 `part_boss_phase2_*`（部件制的唯一例外） | 少量 |

**shader 需要新增的 uniform**：`uTintMode`（0=直接乘色、1=色相旋轉、2=HSL 偏移）、`uRimLight`、`uOverlay`（狀態覆蓋色＋強度）、`uDistort`（靈體／侵蝕用的擾動強度）。四者皆為可選，未使用時行為與現在相同。

### 1.4 動畫規格

| 動作 | 一般敵人 | 首領 | 循環 | 備註 |
|---|---|---|---|---|
| `idle` 待機 | 4 | 6 | ✅ | 呼吸式位移 ≤1px |
| `move` 移動 | 6 | 6 | ✅ | 對齊 tile 移動 |
| `attack` 攻擊 | 5 | 8 | ✕ | **第 2 幀為命中時點**（供 VFX／音效對齊） |
| `hurt` 受擊 | 2 | 3 | ✕ | 白閃由 shader 處理，不畫在圖上 |
| `death` 死亡 | 5 | 8 | ✕ | 最後 2 幀為消散 |
| `phase2_idle` / `phase2_attack` | — | 12 | ✅/✕ | 僅第二階段首領 |
| `cast` 施法（部分敵人） | 5 | 6 | ✕ | 召喚／治療類 |

**播放規格**：12 fps（戰鬥）、8 fps（地圖）；死亡 14 fps。

### 1.5 檔案命名與 atlas 打包

```
assets/sprites/enemy/<rig>/<rig>_<action>_<###>.png        例: enemy/rig_bone/rig_bone_move_003.png
assets/tiles/enemy/<rig>/<rig>_<action>.png                 打包後（橫向排列，atlas 用）
assets/parts/<partId>.png                                  例: parts/part_bow.png
assets/sprites/boss/<bossId>/<bossId>_<action>_<###>.png
assets/icons/enemy/<enemyId>.png                            64×64 圖鑑／戰鬥頭像
assets/icons/status/<statusId>.png                          32×32
assets/equipment/<itemId>_icon.png / <itemId>_hand.png      圖示 / 手持
assets/prompts/<id>.txt                                     ComfyUI prompt（與美術一對一，見 §1.6）
assets/tiles/act<NN>/                                       每幕 tile set
```

**atlas 規則**：單頁上限 **2048×2048**、1px padding、同 rig 同動作不分頁；打包後**只載入當前幕用到的 atlas**（web 體積控管）。目標：**全部美術 PNG ≤ 3 MB**（現行 `toms_web.data` 僅 208 KB，仍有充裕空間；但不可超過 3 MB，否則行動瀏覽器載入變慢）。

### 1.6 ComfyUI 工作流

**通用模板（所有敵人／裝備共用）**

```
POSITIVE:
pixel art sprite, 32x32 game asset, single character centered, full body, transparent background,
crisp 1px dark outline (#1b1420), limited 24-color palette, top-left rim light, hard pixel edges,
no anti-aliasing, JRPG tower-climbing dark fantasy, <SUBJECT>, <SILHOUETTE NOTES>, <COLOR>

NEGATIVE:
blurry, anti-aliased, soft shading, 3d render, photorealistic, text, watermark, signature,
multiple characters, cropped, background scenery, gradient background, motion blur, drop shadow,
extra limbs, modern clothing

PARAMS:
checkpoint: pixel-art XL (SDXL) | sampler: dpmpp_2m + karras | steps: 28 | cfg: 7.0
size: 512x512 (downscale to 32x32 with nearest) | seed: fixed per rig (保持同族一致)
+ ControlNet openpose / silhouette (首領與人形類建議開，確保剪影)
+ 後處理: Pixelate(32) → Quantize(24 colors) → Transparent BG (rembg) → outline pass
```

**後處理 8 步（必須全做，否則無法進遊戲）**
1. `Pixelate` 到 32×32（首領 64×64），用 nearest。
2. 量化到 24 色並鎖定調色盤（同 rig 共用盤）。
3. `rembg` 去背 → 檢查 alpha 邊緣無半透明雜點。
4. 描邊 pass：輪廓 1px `#1b1420`。
5. 對齊格線：腳底落在畫面下緣 ±1px，中心對齊。
6. 匯出 `idle/move/attack/hurt/death` 幀（同 rig 用同一顆 seed 生成基礎幀，再以 img2img 低 denoise 產生動作）。
7. 產生 atlas 與 `manifest.json`（沿用現行 sprite manifest 格式）。
8. 寫入 `assets/prompts/<id>.txt`（把實際使用的 prompt 存檔，便於日後重製）。

**變體生成**：一律**不重跑 ComfyUI**——由後處理腳本以 hue tint ＋ 部件縮放產生（`tools/make_variants.py`，見 §6.4）。

### 1.7 佔格（footprint）規格：1 格／2 格／4 格

> **實作狀態（2026-09-13，S1 完成）**：本節 F1–F8 已進入引擎。
> - 規則與解析：`src/game/footprint.h`（合法尺寸 1x1／2x1／1x2／2x2、覆蓋判定、F5 排序鍵、非法尺寸直接拒絕並回退 1x1）。
> - 角色級佔格表：`data/footprints.json`（F8）；單一樓層可用 stage JSON 的 `footprints` 覆寫單一格（優先序：樓層 > 角色表 > 1x1，統一由 `toms::resolveFootprint()` 決定）。
> - 繪製／碰撞：佔用每一格都會擋路與觸發戰鬥（F6）；大體型敵人「不可踏入」——玩家在相鄰格開打，敵人保有自己整個 cell（F4）；繪製改為**單一 y-sort 通道**且玩家一起排序（F5）；sprite 依佔格放大（F2）；4 格／徘徊者顯示名牌。
> - 徘徊者（`src/game/roamer.h/.cpp`）：遊蕩＋偵測（6 格）→ 追擊（9 格放棄，含滯後），一步僅在**整個佔格**都落在可走格且不出界時才成立（F3／F4，不會穿牆）；種子由關卡 id 決定，重載樓層會重現同樣的遊蕩。
> - 驗證：`footprint_test`（88 項）＋對 11 個既有關卡的資料驗證器（尺寸合法、不得站在牆上、不得重疊（F7）、**樓梯在放大後的阻擋下仍可達**＝防鎖死）。
> - 尚未做：現有 11 關沒有任何徘徊者（王座之影、前世道兵王屬於 S2 產生的樓層事件）；其餘 8 種 2 格、12 種 4 格敵人隨美術（S8）進表。

**現行格線（已實作）**：迷宮的「一格 cell」＝ **2×2 tiles ＝ 4 個 grid**（`tools/gen_mazes.py` 的 `CELL_SCALE_BY_FLOOR = 2`）；cell 之間的通道是**整個 2 tiles 寬的門口**，門也封住整個寬度。相機縮放由 `viewCols_` 決定（`ts = W / viewCols_`），tile 像素大小隨之變化。

敵人因此可有三種佔格，且**只能是 1／2／4 格**（能整除 2×2 的 cell 細分）：

| 佔格 | 尺寸（tile） | 來源像素 | 對齊方式 | 視覺定位 | 數量 |
|---|---|---|---|---|---|
| **1 格** | 1×1 | 32×32 | 任意 tile | 一般敵人 | 30／50 |
| **2 格** | 2×1（寬）或 1×2（高） | 64×32 / 32×64 | 同一 cell 內相鄰兩 tile | 大型・壓迫感 | 8／50 |
| **4 格** | 2×2 | 64×64 | **佔滿一整個 cell** | 首領／特殊事件 | 12／50（10 首領＋2 徘徊者） |

**規則**
- **F1 對齊**：佔格只允許 1／2／4 格，**不允許 3 格**（無法對齊 2×2 細分）。
- **F2 像素密度固定**：每 grid 一律 32×32 px → 2 格＝64×32、4 格＝64×64。**不是把 32×32 放大**，所以大體型敵人是「更多細節」而不是「更模糊的像素」。
- **F3 通行**：因為 cell 間通道是 2 tiles 寬，**4 格（＝整個 cell）敵人也能在迷宮中移動**，不會被困在房間裡——這是現行生成器既有的性質，不需額外工程。
- **F4 閃避空間**：在 4 格的 cell 中，2 格敵人只佔一半，玩家仍有 2 格可繞行；4 格敵人佔滿時玩家**無法與其同格**，只能在外圈繞。
- **F5 繪製順序**：以佔格**最下緣** tile 做 y-sort；4 格敵人自 cell 左上 tile 起繪。
- **F6 命中與狀態**：命中敵人**任一佔格**即成立；狀態（燃燒／中毒…）**只掛一次**，圖示畫在佔格中心上方。
- **F7 配置**：生成器必須保留整個佔格（不得與其他實體重疊）；4 格敵人直接對應一個 cell。
- **F8 變體不變佔格**：色相變體與精英款**不改變佔格**（精英＝原佔格＋金邊＋尺寸 1.15）。佔格是**角色級**設定，不是變體手段。

---

## 2. 狀態系統（36 種）→ 圖示與 VFX 需求

**現況**：`entity_status.h` 的 `EntityStatus` 是**關卡格狀態**（Untouched／Engaged／Defeated／Collected／Opened／Hidden），**不是戰鬥狀態**。本文件因此定義新的戰鬥狀態層（`data/status.json`），並把關卡格狀態與戰鬥狀態分開。

### 2.1 分類與數量

| 類別 | 數量 | 圖示 | VFX | 疊層規則 |
|---|---|---|---|---|
| 增益 Buff | 12 | 12 | 12 | 同 id 取最強，不同 id 可並存（上限 4） |
| 減益 Debuff | 12 | 12 | 12 | 同 id 疊層 = 強度 ×層數（上限 5） |
| 樓層危害 Hazard | 6 | 6 | 6 | 場地效果，不掛在角色上 |
| 架式／關鍵字 Stance | 6 | 6 | 6 | 互斥（同時只 1 個）；id 前綴 `sta_`（避免與支線 `ss_NN` 撞名） |
| **合計** | **36** | **36** | **36（216 幀）** | |

### 2.2 狀態表

**增益（12）**

| id | 名稱 | 效果 | 圖示色 | VFX |
|---|---|---|---|---|
| `st_qi_surge` | 氣盛 | 攻擊條判定帶 +20%，8 秒 | 金 | 身周金氣上升 |
| `st_iron_guard` | 鐵壁 | 護盾效率 +25%，10 秒 | 藍 | 六角護盾 | 
| `st_focus` | 凝神 | 必殺充能速度 +30%，12 秒 | 紫 | 頭頂符紋 |
| `st_swift` | 迅影 | 攻擊條速度 +15%，8 秒 | 青 | 殘影 |
| `st_regen` | 回春 | 每 2 秒回復 2% HP | 綠 | 綠葉粒子 |
| `st_lifesteal` | 汲血 | 命中回復傷害 10% | 暗紅 | 血線回流 |
| `st_barrier_now` | 瞬盾 | 立即獲得等同 15% 最大 HP 的護盾 | 淡藍 | 環形爆散 |
| `st_echo` | 回響 | 下一個主動技不進冷卻 | 白 | 鐘形波紋 |
| `st_memory_clear` | 記憶澄明 | 所有技能效果 +10%（本輪常駐） | 金白 | 碎片環繞 |
| `st_cycle_mark` | 輪迴印記 | 輪迴次數 ≥1 時常駐：EXP +10% | 紫金 | 額外符印 |
| `st_shield_armed` | 護盾已架 | battle v2 既有：抵擋下一次敵擊 | 藍 | 盾面浮現 |
| `st_super_ready` | 必殺待發 | 必殺充能滿 | 橙 | 爆閃 |

**減益（12）**

| id | 名稱 | 效果 | 圖示色 | VFX |
|---|---|---|---|---|
| `st_poison` | 中毒 | 每秒 2% 最大 HP，8 秒 | 綠 | 綠色氣泡 |
| `st_burn` | 燃燒 | 每秒 3% 最大 HP，5 秒 | 橙 | 火焰 |
| `st_bleed` | 流血 | 移動時追加 3 點傷害，10 秒 | 紅 | 血滴 |
| `st_slow` | 遲緩 | 攻擊條速度 −20% | 灰藍 | 腳部冰晶 |
| `st_weaken` | 削弱 | ATK −20% | 灰紫 | 下墜箭頭 |
| `st_shatter_def` | 破防 | DEF −30% | 灰橙 | 甲片碎裂 |
| `st_silence` | 封氣 | 無法使用主動技，6 秒 | 深紫 | 口部封印符 |
| `st_curse` | 咒印 | 受到傷害 +15% | 暗紫 | 符咒繞身 |
| `st_confuse` | 迷亂 | 攻擊條左右隨機反轉，4 秒 | 粉紫 | 螺旋眼 |
| `st_entangle` | 纏繞 | 無法移動，3 秒 | 褐綠 | 藤蔓 |
| `st_fear` | 恐懼 | 強制後退 1 格並失去 5 充能 | 深藍 | 黑霧 |
| `st_drain` | 汲取 | 每次行動被吸取 2% HP（怨靈系） | 紫黑 | 絲線連結 |

**樓層危害（6）**

| id | 名稱 | 效果 | VFX |
|---|---|---|---|
| `hz_dark` | 無光 | 視野半徑 6 格 | 畫面暗角 |
| `hz_poison_mist` | 毒霧 | 每 3 秒 `st_poison` 1 層 | 低層綠霧 |
| `hz_ember_rain` | 火星雨 | 隨機 3 格受傷 | 火星下落 |
| `hz_tremor` | 震動 | 每 6 秒全場 `st_confuse` 1 秒 | 畫面震動 |
| `hz_star_crack` | 星核裂痕 | 裂痕擴大，失敗即 `e_12` | 藍白裂痕 |
| `hz_seal_pulse` | 封印脈動 | 每 10 秒全敵人 `st_iron_guard` 1 層 | 金環擴散 |

**架式（6）**

| id | 名稱 | 效果 | 圖示色 |
|---|---|---|---|
| `sta_blade` | 劍架 | 攻擊 +10%、防禦 −5% | 青 |
| `sta_guard` | 守架 | 防禦 +12%、攻擊 −5% | 藍 |
| `sta_flow` | 流水 | 判定帶 +12%、護盾 −10% | 青綠 |
| `sta_heavy` | 重勢 | 傷害 +20%、條速 −20% | 橙 |
| `sta_star` | 星架 | 必殺充能 +25%、HP −8% | 紫白 |
| `sta_void` | 空心 | 免疫減益、無法回血 | 灰 |

### 2.3 圖示／VFX 統計

| 項目 | 數量 | 單位成本 | 小計 |
|---|---|---|---|
| 狀態圖示 | 36 | 1 張（32×32） | 36 張 |
| 狀態 VFX | 36 | 6 幀（32×32，可加粒子） | **216 幀** |
| 場地危害覆蓋層 | 6 | 1 張（全螢幕素材，可平鋪） | 6 張 |
| 狀態列 UI 底框 | 4 | 1 張 | 4 張 |

---

## 3. 敵人設計（50 種）

### 3.1 分佈

| 幕 | 樓層 | 一般敵人 | 精英 | 首領 | 合計 |
|---|---|---|---|---|---|
| 一 | F1–F7 | 4 | 0 | 1 | 5 |
| 二 | F8–F14 | 4 | 1 | 1 | 5 |
| … | … | 4 | 1 | 1 | 5 |
| 十 | F64–F70 | 4 | 1 | 1 | 5 |
| **合計** | | **40** | **10（菁英，由一般敵人變體）** | **10（獨特）** | **50** |

> 精英**不佔新的美術**：由該幕一般敵人 ＋ 金色 rim light ＋ 尺寸 1.15 倍 ＋ 能力值 ×1.6 HP／×1.35 ATK 產生。

### 3.2 能力值曲線（10 階，對應 10 幕）

| 階（幕） | HP | ATK | DEF | EXP | GOLD | 敵方攻擊間隔 `atkIntervalMs` |
|---|---|---|---|---|---|---|
| 1 | 18–30 | 7–10 | 1–3 | 5–8 | 3–6 | 2600 |
| 2 | 34–52 | 12–17 | 3–5 | 9–14 | 6–10 | 2500 |
| 3 | 58–86 | 19–26 | 5–9 | 15–22 | 10–16 | 2400 |
| 4 | 92–130 | 28–36 | 8–13 | 23–32 | 16–22 | 2300 |
| 5 | 140–190 | 38–48 | 12–18 | 33–44 | 22–30 | 2200 |
| 6 | 200–270 | 50–62 | 17–24 | 45–58 | 30–40 | 2100 |
| 7 | 285–370 | 64–78 | 23–32 | 60–75 | 40–52 | 2000 |
| 8 | 385–490 | 80–96 | 31–41 | 78–95 | 52–66 | 1900 |
| 9 | 510–650 | 98–116 | 40–52 | 98–118 | 66–82 | 1800 |
| 10 | 670–860 | 118–140 | 51–66 | 120–145 | 82–100 | 1700 |

**公式**：`HP(t) = 24 × 1.47^(t−1)`、`ATK(t) = 8 × 1.38^(t−1)`、`DEF(t) = 2 × 1.44^(t−1)`、`EXP(t) = 6 × 1.40^(t−1)`、`GOLD(t) = 4 × 1.42^(t−1)`；傷害沿用 `max(1, atk − def)`。
**定位變體**：坦克（HP ×1.5、ATK ×0.8、DEF ×1.4）、刺客（HP ×0.7、ATK ×1.3、間隔 ×0.8）、法師（HP ×0.8、ATK ×1.1、召喚／狀態）、首領（HP ×6–10、ATK ×1.6、間隔 ×0.6、多階段）。

### 3.3 敵人條目格式說明

每條包含：**外觀／變體（骨架・色相・部位縮放）／動畫張數／能力值／技能／ComfyUI subject**。
共用骨架與模板見 §1.2 與 §1.6，以下 `subject` 直接接進模板的 `<SUBJECT>` 位置。

> 敵人條目見 §3.3（`E-01` … `E-50`），10 位首領另有**完整 prompt** 與獨特性規格（§3.4）。
---

## 3.3 敵人條目（50 種）

> 格式：**外觀／變體（骨架・色相・部位縮放）／動畫／能力值／技能／ComfyUI subject**。
> `subject` 直接填入 §1.6 模板的 `<SUBJECT>`，positive 其餘欄位與 negative 沿用模板。
> 能力值沿用 §3.2 曲線；**精英**＝同款 ＋ 金色 rim light ＋ 尺寸 1.15＋HP×1.6／ATK×1.35。

### 第一幕 F1–F7「邊村與井」（T1）

**E-01 史萊姆 `slime`｜rig_slime**
外觀：半透明水滴狀、內有核心，圓潤剪影。／變體：色相 0°（基準款）＋部件 `part_slime_core`。／動畫 22 幀（待4・移6・攻5・傷2・死5）。
能力：HP 24｜ATK 8｜DEF 2｜EXP 6｜G 4｜間隔 2600ms｜技能：**撞擊**、受擊 15% 分裂出微型史萊姆（HP 6 / ATK 3）。
Prompt subject：`translucent blue slime, visible glowing core, glossy jelly body`

**E-02 井蛙 `well_toad`｜rig_slime**
外觀：肥厚蛙身、喉囊鼓動，眼為雙重瞳孔。／變體：rig_slime｜色相 +95°（黃綠）｜部件 `part_toad_sac`（縮放 1.25）。／動畫 22 幀。
能力：HP 30｜ATK 9｜DEF 3｜EXP 8｜G 5｜間隔 2700ms｜技能：**黏液噴吐**（命中附加 `st_slow`）。
Prompt subject：`fat swamp toad with a bulging throat sac, double-pupil eyes, warty skin`

**E-03 腐葉蟲 `rot_grub`｜rig_beast**
外觀：分節幼蟲、背上覆蓋腐葉，蠕動前進。／變體：rig_beast｜色相 +40°（褐紅）｜部件 `part_leaf_shell`（縮放 1.2）。／動畫 22 幀。
能力：HP 22｜ATK 7｜DEF 5｜EXP 5｜G 3｜間隔 2800ms｜技能：**腐蝕液**（`st_shatter_def` 1 層）。
Prompt subject：`segmented grub with rotting leaves growing on its back, low crawling silhouette`

**E-04 村犬 `village_hound`｜rig_beast**
外觀：瘦骨野犬、頸上有斷裂項圈。／變體：rig_beast｜色相 −25°（冷灰）｜部件 `part_torn_collar`。／動畫 22 幀。
能力：HP 26｜ATK 10｜DEF 1｜EXP 7｜G 6｜間隔 2400ms｜技能：**撲咬**（連續兩次攻擊，第二次 −30% 傷害）。
Prompt subject：`emaciated feral hound with a broken collar, low aggressive stance`

**E-05 🐲首領 井中妖王 `slime_king`｜rig_slime（首領特化）**
外觀：巨型史萊姆，內部懸浮整口井的石磚與人骨，中央有一隻巨大的眼。／變體：色相 −20°（墨綠）＋部件 `part_king_eye`（縮放 1.6）、`part_king_bricks`。／動畫 28 幀（首領版待6・移6・攻8・傷3・死5）。
能力：HP 190｜ATK 14｜DEF 5｜EXP 60｜G 45｜間隔 2000ms｜技能：**吞沒**（玩家 3 秒無法移動）、**井水噴湧**（隨機 3 格受傷）、HP<40% 時**分裂**（召喚 E-01 ×2）。
Prompt（完整）：見 §3.4-B1。

### 第二幕 F8–F14「林道」（T2）

**E-06 暗影蝙蝠 `bat`｜rig_bat**
外觀：暗紫蝠身、翼膜破洞、單眼發紅。／變體：rig_bat｜色相 −35°（深紫）｜部件 `part_bat_wing_torn`（縮放 1.1）。／動畫 22 幀。
能力：HP 18｜ATK 11｜DEF 1｜EXP 7｜G 5｜間隔 2500ms｜技能：**吸血啃咬**（回復造成傷害 30%）。
Prompt subject：`shadowy one-eyed bat with torn wing membranes, dark purple fur`

**E-07 荊棘藤 `thorn_vine`｜rig_beast**｜佔格 **1×2（2 格）**
外觀：無眼，四條帶刺藤蔓自地面挺起。／變體：rig_beast｜色相 +70°（深綠）｜部件 `part_thorn_tip`×2（縮放 1.35）。／動畫 22 幀。
能力：HP 40｜ATK 13｜DEF 5｜EXP 11｜G 7｜間隔 2600ms｜技能：**纏繞**（`st_entangle` 3 秒）、固定不動（免疫位移）。
Prompt subject：`carnivorous thorn vine with four barbed tendrils rising from soil, no eyes`

**E-08 林獵蛛 `wood_spider`｜rig_beast**
外觀：扁身八足、背上覆蓋樹皮，腹部有卵囊。／變體：rig_beast｜色相 +15°（棕黃）｜部件 `part_egg_sac`（縮放 1.2）、`part_spider_legs`（1.4）。／動畫 24 幀。
能力：HP 36｜ATK 15｜DEF 3｜EXP 10｜G 6｜間隔 2300ms｜技能：**蛛網投射**（`st_slow` 2 層）、死亡時爆出 2 隻小蛛（HP 8）。
Prompt subject：`bark-camouflaged spider with an egg sac abdomen and long thin legs`

**E-09 折箭獵手 `arrow_hunter`（精英款）｜rig_humanoid**
外觀：斗篷獵人、背上插著自己折斷的箭；臉埋在陰影裡。／變體：rig_humanoid｜色相 −10°（冷褐）｜部件 `part_broken_arrows`（縮放 1.2）、`part_hunter_bow`。／動畫 24 幀。
能力：HP 62｜ATK 17｜DEF 5｜EXP 18｜G 12｜間隔 2100ms｜技能：**折箭射擊**（無視 25% 護盾）、**後躍**（拉開距離並進入 `st_swift`）。
備註：**首領 E-10 的前置型**，剪影刻意相似但更小、更瘦。
Prompt subject：`hooded hunter with broken arrows stuck in his back, shadowed face, worn cloak`

**E-10 🐲首領 蝠母·獵手首領 `bat_matron`｜rig_bat（首領特化）**
外觀：翼展極寬的巨蝠，翼骨末端長出人手，胸口懸著折斷的獵弓。／變體：色相 −50°（黑紫）＋部件 `part_matron_hands`×2（縮放 1.5）、`part_hunter_bow`。／動畫 30 幀。
能力：HP 300｜ATK 24｜DEF 8｜EXP 120｜G 90｜間隔 1700ms｜技能：**音波俯衝**（全場 `st_confuse` 2 秒）、**召喚蝠群**（E-06 ×3）、HP<50% **翼展**（判定帶視覺變寬，攻擊間隔 ×0.8）。
Prompt（完整）：見 §3.4-B2。

### 第三幕 F15–F21「門樓」（T3）

**E-11 骷髏兵 `skeleton`｜rig_bone**
外觀：鏽劍腐盾、肋骨外露，空洞眼窩有微光。／變體：rig_bone｜色相 0°（基準）｜部件 `part_rusty_sword`、`part_wood_shield`。／動畫 22 幀。
能力：HP 58｜ATK 19｜DEF 6｜EXP 16｜G 10｜間隔 2400ms｜技能：**盾格擋**（30% 機率減傷 50%）。
Prompt subject：`skeleton soldier with rusted sword and cracked wooden shield, faint light in eye sockets`

**E-12 道兵殘骸 `dao_soldier`｜rig_construct**
外觀：石製人形、關節嵌著符文環，頸後刻有編號。／變體：rig_construct｜色相 +10°（石灰）｜部件 `part_rune_ring`×2、`part_dao_core`。／動畫 24 幀。
能力：HP 86｜ATK 21｜DEF 9｜EXP 20｜G 14｜間隔 2500ms｜技能：**符文重擊**（`st_shatter_def`）、**核心過載**（HP<30% 時 ATK +40%）。
Prompt subject：`stone automaton soldier with glowing rune rings at its joints, carved number on neck`

**E-13 骷髏弓手 `skeleton_archer`｜rig_bone**
外觀：細高骨架、箭袋綁在肋骨上，弓比身體寬。／變體：rig_bone＋色相 +18°（冷藍）｜部件 `part_bow`（**縮放 1.30**）、上臂骨架 ×1.15。／動畫 22 幀。
能力：HP 52｜ATK 22｜DEF 4｜EXP 18｜G 11｜間隔 2200ms｜技能：**貫穿箭**（無視 20% 護盾）、**後躍**。
Prompt subject：`tall thin skeleton archer, quiver strapped to its ribs, bow wider than its body`

**E-14 投石傀儡 `stone_lobber`｜rig_construct**｜佔格 **2×1（2 格）**
外觀：矮胖石身、右臂為投石索，背部堆滿石塊。／變體：rig_construct｜色相 +25°（土黃）｜部件 `part_sling_arm`（1.25）、`part_stone_pile`。／動畫 24 幀。
能力：HP 78｜ATK 26｜DEF 11｜EXP 22｜G 16｜間隔 2700ms｜技能：**投石**（遠距離，附帶 `st_confuse` 1 秒）、移動極慢。
Prompt subject：`squat stone golem with a sling arm and a pile of rocks on its back`

**E-15 🐲首領 守門石巨人 `golem`｜rig_construct（首領特化）**
外觀：三層樓高的石巨人，胸口嵌一道閉鎖的門環；頸後有 `道兵 三·七` 刻痕（故事伏筆）。／變體：色相 +5°（岩灰）＋部件 `part_gate_ring`（1.8）、`part_seal_chain`。／動畫 32 幀。
能力：HP 520｜ATK 30｜DEF 16｜EXP 260｜G 200｜間隔 2400ms（重擊型）｜技能：**落石震擊**（全場震動＋`st_confuse`）、**門環鎖鏈**（召喚鎖鏈限制移動 4 秒）、HP<50% **崩解重組**（DEF +50%、速度 −20%）。
Prompt（完整）：見 §3.4-B3。

### 第四幕 F22–F28「藏書閣」（T4）

**E-16 書頁妖 `page_wisp`｜rig_bat**
外觀：一疊飛散書頁組成、中央有一顆墨眼。／變體：rig_bat｜色相 +180°（紙黃灰）｜部件 `part_page_swarm`（1.3）。／動畫 20 幀。
能力：HP 92｜ATK 28｜DEF 8｜EXP 23｜G 16｜間隔 2300ms｜技能：**墨滴**（`hz_dark` 視野縮減 3 秒）、**紙刃迴旋**。
Prompt subject：`swarm of flying book pages with a single ink eye at its center, paper yellow`

**E-17 執卷骨僧 `tome_monk`｜rig_bone**
外觀：盤坐浮空、雙手持巨大典籍，書頁不斷翻動。／變體：rig_bone＋色相 +40°（金褐）｜部件 `part_great_tome`（1.35）、`part_monk_beads`。／動畫 24 幀。
能力：HP 118｜ATK 30｜DEF 13｜EXP 28｜G 20｜間隔 2500ms｜技能：**誦讀**（自身 `st_iron_guard`）、**咒印**（玩家 `st_curse`）。
Prompt subject：`floating skeletal monk holding a giant open tome, prayer beads, pages turning`

**E-18 骷髏弓手·典藏版 `skeleton_archer_archive`｜rig_bone**
外觀：與 E-13 同骨架，但披上圖書館防塵布、箭為書籤狀。／變體：rig_bone＋色相 +18°（冷藍）＋部件 `part_bow`（1.30）、`part_dust_cloth`、`part_bookmark_arrows`。／動畫 22 幀。
能力：HP 104｜ATK 34｜DEF 9｜EXP 30｜G 21｜間隔 2100ms｜技能：**貫穿箭·典藏**（無視 30% 護盾）。
備註：**示範「同骨架不同部件」的省檔案做法**——與 E-13 共用全部動畫。
Prompt subject：`skeleton archer draped in a dusty archive cloth, arrows tipped with bookmarks`

**E-19 書架巨像 `shelf_colossus`｜rig_construct**｜佔格 **1×2（2 格）**
外觀：以書架為軀幹的木石巨人，每層都塞滿書。／變體：rig_construct｜色相 +35°（胡桃木）｜部件 `part_shelf_torso`（1.4）、`part_candle_crown`。／動畫 24 幀。
能力：HP 130｜ATK 32｜DEF 14｜EXP 32｜G 22｜間隔 2600ms｜技能：**落書**（隨機 3 格）、**知識壓制**（`st_silence` 3 秒）。
Prompt subject：`colossal golem whose torso is a bookshelf stuffed with tomes, candles on its head`

**E-20 🐲首領 執卷人之骸·骷髏學者 `skeleton_scholar_boss`｜rig_bone（首領特化）**
外觀：四臂骨僧，背後以鎖鏈懸吊十本巨書，眼眶內是兩盞燭火。／變體：色相 +55°（骨金）＋部件 `part_four_arms`（1.4）、`part_chained_tomes`、`part_candle_eyes`。／動畫 34 幀。
能力：HP 680｜ATK 36｜DEF 15｜EXP 380｜G 260｜間隔 2200ms｜技能：**禁書翻頁**（每頁一種效果：毒／緩／破防）、**知識洪流**（直線高傷）、HP<45% **燭火熄滅**（進入第二階段：全場 `st_curse`、攻擊間隔 ×0.8）。
Prompt（完整）：見 §3.4-B4。

### 第五幕 F29–F35「墓穴」（T5）

**E-21 怨靈 `wraith`｜rig_spirit**
外觀：破爛斗篷漂浮、下半身化為黑霧。／變體：rig_spirit｜色相 −60°（青紫）｜部件 `part_tattered_cloak`（1.15）。／動畫 22 幀。
能力：HP 140｜ATK 38｜DEF 12｜EXP 33｜G 22｜間隔 2200ms｜技能：**汲取**（`st_drain`）、**穿牆**（無視地形 2 秒）。
Prompt subject：`floating wraith in a tattered cloak, lower body dissolving into black mist`

**E-22 墓穴爬行者 `crypt_crawler`｜rig_beast**｜佔格 **2×1（2 格）**
外觀：以四肢倒掛爬行、臉朝上，脊椎外露。／變體：rig_beast｜色相 −15°（骨灰）｜部件 `part_exposed_spine`（1.3）。／動畫 24 幀。
能力：HP 165｜ATK 43｜DEF 15｜EXP 38｜G 26｜間隔 2100ms｜技能：**突襲**（從視野外衝出，首次傷害 ×1.5）。
Prompt subject：`inverted crawling corpse with an exposed spine, face pointing upward, grotesque`

**E-23 亡者衛 `grave_guard`｜rig_bone**
外觀：重甲骷髏、手持鏽蝕長戟，頭盔內無光。／變體：rig_bone＋色相 +5°（鐵灰）｜部件 `part_halbard`（1.4）、`part_heavy_helm`。／動畫 22 幀。
能力：HP 190｜ATK 46｜DEF 18｜EXP 42｜G 29｜間隔 2400ms｜技能：**戟刺**（直線貫穿）、**誓約防守**（HP<50% 時 DEF +60%）。
Prompt subject：`heavily armored skeletal guard with a rusted halberd, dark empty helmet`

**E-24 哭喪女 `banshee`｜rig_spirit**
外觀：長髮掩面、口中流出灰燼，周圍環繞低鳴波紋。／變體：rig_spirit｜色相 −85°（灰白）｜部件 `part_long_hair`（1.3）、`part_ash_veil`。／動畫 24 幀。
能力：HP 150｜ATK 48｜DEF 12｜EXP 44｜G 30｜間隔 2300ms｜技能：**哀鳴**（全場 `st_fear`）、**灰燼吐息**（`st_burn` 2 層）。
Prompt subject：`banshee with long hair covering her face, ash pouring from her mouth, faint ripples around her`

**E-25 🐲首領 亡者合唱 `wraith_choir`｜rig_spirit（首領特化）**
外觀：五具怨靈以鎖鏈連成一圈、中央是它們共同的心臟（懸浮、跳動）。／變體：色相 −70°（幽藍）＋部件 `part_choir_heart`（1.6）、`part_chain_circle`。／動畫 36 幀。
能力：HP 980｜ATK 52｜DEF 18｜EXP 560｜G 380｜間隔 2000ms｜技能：**五重輪唱**（每具怨靈各攻擊一次）、**亡者之名**（讀出玩家已刻名者 → 若完成 `ss_05`，傷害 −30%，**故事與戰鬥掛鉤**）、HP<50% **單體化**（合為一體、ATK ×1.5、失去輪唱）。
Prompt（完整）：見 §3.4-B5。

### 第六幕 F36–F42「洞窟」（T6）

**E-26 小惡魔 `demon`｜rig_demon**
外觀：短小紅魔、單角、尾巴末端為箭頭。／變體：rig_demon｜色相 0°（基準）｜部件 `part_single_horn`、`part_arrow_tail`。／動畫 24 幀。
能力：HP 200｜ATK 50｜DEF 17｜EXP 45｜G 30｜間隔 2100ms｜技能：**火球**（`st_burn`）、**嘲弄**（玩家 `st_confuse` 2 秒）。
Prompt subject：`small imp with a single horn and arrow-tipped tail, mischievous grin`

**E-27 晶石獸 `crystal_beast`｜rig_slime**｜佔格 **1×2（2 格）**
外觀：身軀由晶簇構成、內部折射出光線。／變體：rig_slime｜色相 +150°（青藍）｜部件 `part_crystal_cluster`×2（1.35）。／動畫 22 幀。
能力：HP 240｜ATK 54｜DEF 24｜EXP 52｜G 36｜間隔 2200ms｜技能：**稜光折射**（反彈 20% 傷害）、**結晶護層**（`st_iron_guard`）。
Prompt subject：`beast made of glowing crystal clusters, light refracting inside its body`

**E-28 洞窟潛伏者 `cave_lurker`｜rig_beast**
外觀：四足伏行、頭部為一顆巨大的感光眼，無口。／變體：rig_beast｜色相 −45°（深灰藍）｜部件 `part_giant_eye`（1.4）。／動畫 22 幀。
能力：HP 215｜ATK 58｜DEF 19｜EXP 55｜G 38｜間隔 2000ms｜技能：**無光領域**（場地 `hz_dark` 6 秒）、**突進**。
Prompt subject：`four-legged lurking beast with a single giant light-sensitive eye and no mouth`

**E-29 回聲之影 `echo_shade`｜rig_spirit**
外觀：玩家動作的延遲殘影，輪廓與玩家相同但全黑。／變體：rig_spirit｜色相 −100°（純黑）＋shader `uDistort=0.4`｜部件 `part_echo_trail`。／動畫 22 幀。
能力：HP 225｜ATK 60｜DEF 20｜EXP 58｜G 40｜間隔 1900ms｜技能：**模仿**（複製玩家上一次使用的技能）、**殘響**（死亡時原地產生一次玩家技能的傷害）。
Prompt subject：`monochrome shadow copy of a human silhouette, distorted edges, no facial features`

**E-30 🐲首領 雙首晶獸 `twin_golem`｜rig_construct（首領特化）**
外觀：兩具晶石軀體共用一個核心、以晶橋相連；兩顆頭分別為攻與守。／變體：色相 +150°（青藍）＋部件 `part_twin_core`（1.7）、`part_crystal_bridge`。／動畫 38 幀。
能力：HP 1450｜ATK 62｜DEF 26｜EXP 820｜G 560｜間隔 1900ms｜技能：**雙重夾擊**（兩段攻擊，需連續兩次防禦成功）、**晶化**（DEF ×2 一回合，引導玩家改用必殺）、HP<50% **核心暴露**（DEF −50%、ATK +40%）。
Prompt（完整）：見 §3.4-B6。

### 第七幕 F43–F49「軍營」（T7）

**E-31 王國士兵 `realm_soldier`｜rig_humanoid**
外觀：藍白制服、圓盾短劍，動作整齊劃一。／變體：rig_humanoid｜色相 0°（基準）｜部件 `part_round_shield`、`part_short_sword`。／動畫 24 幀。
能力：HP 285｜ATK 64｜DEF 23｜EXP 60｜G 40｜間隔 2000ms｜技能：**列陣**（與另一名士兵同時在場時 DEF +20%）。
Prompt subject：`kingdom soldier in blue and white uniform with round shield and short sword, rigid posture`

**E-32 叛軍弓手 `rebel_archer`｜rig_humanoid**
外觀：與 E-31 同骨架，紅布蒙面、皮甲破損。／變體：rig_humanoid＋色相 +160°（紅褐）｜部件 `part_rebel_bow`（1.25）、`part_red_mask`、`part_quiver`。／動畫 24 幀。
能力：HP 300｜ATK 74｜DEF 25｜EXP 68｜G 46｜間隔 1850ms｜技能：**三連射**（三支箭各 35% 傷害）。
備註：與 E-31 共用動畫（同骨架示範）。
Prompt subject：`rebel archer in red mask with a worn leather armor and longbow`

**E-33 軍犬 `war_hound`｜rig_beast**
外觀：披掛鎧甲的獵犬、嘴部濺血。／變體：rig_beast｜色相 +20°（鐵褐）｜部件 `part_dog_armor`（1.2）。／動畫 22 幀。
能力：HP 270｜ATK 78｜DEF 26｜EXP 65｜G 43｜間隔 1750ms｜技能：**撕咬**（`st_bleed` 2 層）、**追獵**（每回合向玩家靠近 2 格）。
Prompt subject：`armored war hound with blood on its muzzle, thick iron plates`

**E-34 鎧甲戰士 `armored_knight`｜rig_humanoid**
外觀：全覆式重甲、雙手持巨劍，甲面有裂痕。／變體：rig_humanoid＋色相 −10°（鋼灰）｜部件 `part_great_sword`（1.45）、`part_full_helm`、`part_heavy_pauldron`（1.2）。／動畫 24 幀。
能力：HP 370｜ATK 78｜DEF 32｜EXP 75｜G 52｜間隔 2300ms｜技能：**迴旋斬**（相鄰兩格）、**鋼鐵意志**（免疫 `st_fear`）。
Prompt subject：`fully armored knight with a great sword, cracked steel plates, full helmet`

**E-35 🐲首領 軍營叛將 `traitor_captain`｜rig_lord（首領特化）**
外觀：半邊身軀被巫術結晶取代的將領、披風燒毀一半，手持裂紋軍旗。／變體：色相 −20°（暗紅）＋部件 `part_broken_banner`（1.6）、`part_crystal_arm`（1.3）。／動畫 36 幀。
能力：HP 2200｜ATK 80｜DEF 30｜EXP 1100｜G 720｜間隔 1800ms｜技能：**軍令如山**（召喚 E-31 ×2）、**叛旗拂塵**（全場 `st_weaken` 2 層）、HP<45% **結晶化**（結晶臂 ATK ×1.6、失去召喚）。
Prompt（完整）：見 §3.4-B7。

### 第八幕 F50–F56「神殿」（T8）

**E-36 神殿守衛 `temple_guard`｜rig_construct**
外觀：白瓷甲守衛、胸口嵌齒輪鐘擺。／變體：rig_construct｜色相 −5°（象牙白）｜部件 `part_pendulum_core`（1.25）、`part_ceramic_pauldron`。／動畫 24 幀。
能力：HP 385｜ATK 82｜DEF 33｜EXP 78｜G 52｜間隔 1900ms｜技能：**鐘擺打擊**（每三回合一次重擊 ×2）。
Prompt subject：`porcelain-armored temple guardian with a ticking pendulum core in its chest`

**E-37 鐘靈 `bell_spirit`｜rig_bat**
外觀：由鐘與音波構成的浮游靈體、無臉。／變體：rig_bat｜色相 +45°（金黃）｜部件 `part_bell_core`（1.3）。／動畫 22 幀。
能力：HP 400｜ATK 88｜DEF 31｜EXP 85｜G 58｜間隔 1800ms｜技能：**音爆**（全場 `st_confuse` 2 秒）、**共鳴**（與其他鐘靈同時在場時傷害 +25%）。
Prompt subject：`floating bell-shaped spirit made of sound waves, faceless, brass gold`

**E-38 誦經者 `chanter`｜rig_humanoid**
外觀：蒙眼僧侶、雙手合十，腳不著地。／變體：rig_humanoid＋色相 +55°（赭金）｜部件 `part_blindfold`、`part_prayer_hands`。／動畫 24 幀。
能力：HP 420｜ATK 90｜DEF 35｜EXP 88｜G 60｜間隔 2000ms｜技能：**安寧之聲**（治療其他敵人 15% HP）、**咒縛**（`st_silence` 3 秒）。
Prompt subject：`blindfolded monk floating with hands pressed together, ochre robes, ascetic`

**E-39 幻影修士 `phantom_monk`｜rig_spirit**
外觀：半透明的同款僧侶，攻擊會穿過實體。／變體：rig_spirit｜色相 +55°（淡金）＋shader `uDistort=0.3`｜部件 `part_phantom_halo`。／動畫 22 幀。
能力：HP 395｜ATK 92｜DEF 32｜EXP 90｜G 62｜間隔 1750ms｜技能：**實體迴避**（30% 完全閃避）、**殘像分身**。
Prompt subject：`translucent phantom monk with a faint halo, semi-transparent body`

**E-40 🐲首領 鐘樓守衛 `bell_warden`｜rig_construct（首領特化）**
外觀：軀幹為一口巨鐘、四肢為敲鐘機械臂，鐘面刻著七道裂痕（對應第七次鐘聲）。／變體：色相 +40°（銅金）＋部件 `part_great_bell`（1.8）、`part_seven_cracks`。／動畫 38 幀。
能力：HP 3100｜ATK 96｜DEF 38｜EXP 1600｜G 980｜間隔 1700ms｜技能：**鐘鳴七響**（每響疊加一層 `st_weaken`，第七響為全場大傷）、**鐘壁**（吸收 25% 傷害，需先破壞鐘面裂痕）、HP<40% **第七響**（全場 `st_confuse` + `st_curse`，攻擊間隔 ×0.75）。
Prompt（完整）：見 §3.4-B8。

### 第九幕 F57–F63「王座前室」（T9）

**E-41 鏡像衛 `mirror_guard`｜rig_lord**｜佔格 **2×1（2 格）**
外觀：全身為鏡面甲、映出玩家樣貌。／變體：rig_lord｜色相 −120°（銀鏡）＋shader 反射（`uTintMode=2`）｜部件 `part_mirror_shield`（1.3）。／動畫 24 幀。
能力：HP 520｜ATK 98｜DEF 42｜EXP 100｜G 68｜間隔 1750ms｜技能：**反射**（反彈 25% 傷害）、**鏡像分裂**（召喚 1 個自身 40% HP 的鏡像）。
Prompt subject：`mirror-armored guard reflecting the viewer, polished silver plates`

**E-42 前室侍從 `chamber_attendant`｜rig_lord**
外觀：王座侍從、頸上有咒印項圈，手持燭台。／變體：rig_lord＋色相 +200°（深紫）｜部件 `part_candelabrum`（1.25）、`part_cursed_collar`。／動畫 24 幀。
能力：HP 540｜ATK 102｜DEF 44｜EXP 105｜G 72｜間隔 1800ms｜技能：**燭火引路**（為其他敵人上 `st_swift`）、**項圈引爆**（死亡時自爆）。
Prompt subject：`royal chamber attendant with a cursed collar, holding a candelabrum`

**E-43 墨無殤之影 `wushang_shade`｜rig_demon**｜佔格 **1×2（2 格）**
外觀：巫師輪廓的暗影、只有眼睛發光，斗篷下是星塵。／變體：rig_demon｜色相 −140°（深紫黑）＋`uDistort=0.35`｜部件 `part_shade_eyes`（1.4）、`part_star_cloak`。／動畫 26 幀。
能力：HP 610｜ATK 110｜DEF 46｜EXP 115｜G 78｜間隔 1700ms｜技能：**禁咒彈**、**師弟的試探**（若玩家持有 `qingxiao_blade`，此敵人 ATK −20%，台詞不同）。
Prompt subject：`shadowy sorcerer silhouette with glowing eyes, star dust under its cloak`

**E-44 王座之影 `throne_shadow`｜rig_demon**｜佔格 **2×2（4 格・徘徊者）**
外觀：由王座石雕與陰影構成的巨影、背上長出座椅。／變體：rig_demon＋色相 −160°（黑金）｜部件 `part_throne_back`（1.5）。／動畫 26 幀。
能力：HP 650｜ATK 116｜DEF 50｜EXP 118｜G 82｜間隔 1700ms｜技能：**即位**（自身 `st_iron_guard`）、**陰影壓制**（玩家 3 秒無法使用必殺）。
Prompt subject：`giant shadow figure with a stone throne growing from its back, black and gold`

**E-45 🐲首領 心魔·青霄 `inner_qingxiao`｜rig_lord（首領特化，獨特）**
外觀：**與玩家完全相同的剪影**，但全為暗色並帶金色劍氣裂紋；臉部是鏡面。／變體：色相 −180°（暗金）＋`uRimLight=1`（金）＋部件 `part_inner_blade`（1.4）、`part_cracked_face`。／動畫 44 幀（含 12 幀特殊動作）。
能力：HP 4200｜ATK 118｜DEF 52｜EXP 2200｜G 1300｜間隔 1600ms｜技能：**以彼之道**（複製玩家當前技能樹最高階技能）、**帝骨回憶**（強制播放一段前世記憶，期間玩家無法行動 2 秒）、**劍意對決**（雙方強制進入連續三次判定對決）、HP<35% **心魔顯形**（變為玩家外觀 ＋ 玩家所有裝備 tint 反轉）。
Prompt（完整）：見 §3.4-B9。
**獨特性**：唯一「鏡像玩家」的首領，**骨架雖與 rig_lord 同族，但動畫與部件全為獨立製作**（44 幀 vs 一般 24 幀）。

### 第十幕 F64–F70「帝骨與星」（T10）

**E-46 前世道兵王 `dao_lord`｜rig_construct**｜佔格 **2×2（4 格・徘徊者）**
外觀：七具道兵殘骸以符文鏈聚合成的巨兵、中央是發光的道基。／變體：rig_construct＋色相 +15°（古銅）｜部件 `part_dao_core_big`（1.9）、`part_rune_chains`。／動畫 28 幀。
能力：HP 700｜ATK 120｜DEF 52｜EXP 125｜G 84｜間隔 1800ms｜技能：**道基共鳴**（全場道兵系敵人 ATK +20%）、**符文鏈牽引**（拉近玩家）。
Prompt subject：`colossal warrior assembled from seven broken stone automatons, rune chains binding them`

**E-47 星塵獸 `star_mote`｜rig_slime**
外觀：由星核碎屑凝聚、內部有微型星雲。／變體：rig_slime｜色相 +210°（藍白）＋`uRimLight=1`｜部件 `part_star_nebula`（1.45）。／動畫 22 幀。
能力：HP 670｜ATK 124｜DEF 51｜EXP 120｜G 82｜間隔 1750ms｜技能：**星塵爆**（`st_burn` 3 層）、**重力禁錮**（`st_entangle`）。
Prompt subject：`creature of condensed star dust with a miniature nebula inside, glowing blue-white`

**E-48 帝骨衛 `bonelord_guard`｜rig_bone**｜佔格 **2×1（2 格）**
外觀：以大帝骨為材的巨型骨衛、披著前世戰袍殘片。／變體：rig_bone＋色相 +65°（骨金）｜部件 `part_emperor_bone`（1.5）、`part_old_banner`。／動畫 24 幀。
能力：HP 760｜ATK 130｜DEF 58｜EXP 135｜G 90｜間隔 1800ms｜技能：**骨壁**（吸收 30% 傷害）、**帝威**（`st_fear`）。
Prompt subject：`giant skeletal guard built from an emperor's bones, tattered ancient war banner`

**E-49 天劫餘燼 `tribulation_ember`｜rig_demon**
外觀：前世天劫遺留的火種、形如殘破鳳凰。／變體：rig_demon＋色相 +30°（白金火焰）＋`uRimLight=2`｜部件 `part_ember_wings`×2（1.35）。／動畫 26 幀。
能力：HP 820｜ATK 138｜DEF 60｜EXP 142｜G 96｜間隔 1600ms｜技能：**天劫灼痕**（`st_burn` 5 層 ＋ `st_weaken`）、**餘燼重生**（一次以 25% HP 復活）。
Prompt subject：`broken phoenix made of heavenly tribulation embers, white-gold flames`

**E-50 🐲最終首領 墨無殤（沃卡司）`demonlord_vorkath`｜獨立骨架（非共用）**
外觀：三階段外觀——
- **第一階段**：黑巫師之王（高冠、長袍、星核作杖頭）。
- **第二階段**：卸下皮相 → 前世道體（白袍、斷劍、與主角同款劍意）。
- **第三階段**：帝骨與星核共鳴（半身帝骨裝甲、星核裂痕自胸口延伸）。
／變體：**獨立骨架**，部件 `part_crown_star`（1.4）、`part_broken_sword`、`part_emperor_bone_half`、`part_star_crack_aura`。／動畫 **96 幀**（三階段各 24 幀 ＋ 專屬大絕 24 幀）。
能力：HP 8600（三階段 3200／2600／2800）｜ATK 140｜DEF 62｜EXP 4000｜G 2400｜間隔 1500ms（每階段遞減至 1200ms）。
技能：**星核封印**（第一階段：全場 `st_silence`）、**同門劍意**（第二階段：使用青霄劍意的變體，必須以「純淨之攻」對應）、**師弟的質問**（第二階段：選擇性對話分支 `e_09` 的觸發點）、**帝骨共鳴**（第三階段：場地出現星核裂痕 `hz_star_crack`）、**最後一問**（第三階段：`c_final_blow` 分歧）。
Prompt（完整，三階段各一段）：見 §3.4-B10。

---

## 3.4 十位首領的完整 prompt 與獨特性規格

**首領共同規格**：獨立骨架（不與雜兵共用）｜**佔格 4 格（2×2，佔滿一整個 cell）**｜64×64 來源｜動畫 28–96 幀｜專屬 VFX ≥3 種｜第二階段外觀｜競技場互動（破壞地形／召喚小兵／改變燈光）｜專屬入場演出 12 幀（入場同時顯示名牌橫幅）。

### B1 井中妖王 `slime_king`

```
POSITIVE:
pixel art boss sprite, 64x64 game asset, single creature centered, transparent background,
crisp 1px dark outline (#1b1420), limited 28-color palette, JRPG dark fantasy,
gigantic translucent slime king, an entire stone well and human bones suspended inside its body,
one huge glowing eye at its center, dripping water, mossy green,
imposing but grotesque silhouette, <uniqueness: internal rubble and a floating eye>
NEGATIVE: (模板 negative 全採) + clean/pretty, cute, small, multiple creatures
PARAMS: SDXL pixel-art, steps 32, cfg 7.5, 1024x1024 → 64x64 nearest, seed 1001
         + ControlNet silhouette, 後處理同 §1.6
```
**獨特點**：唯一「身體內部有場景」的首領（井與骨）；眼為獨立部件可縮放，用於第二階段的「睜眼」演出。

### B2 蝠母·獵手首領 `bat_matron`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
colossal bat matron, enormous wingspan with human hands growing from the wing bones,
a broken hunter's bow hanging on her chest, dark purple fur, feral mother-protector aura,
<uniqueness: human hands on wing joints + the broken bow>
PARAMS: seed 1002
```
**獨特點**：翼骨末端的「人手」；第二階段以翅膀包覆全場（畫面遮罩演出）。

### B3 守門石巨人 `golem`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
three-story stone golem, a sealed ring (closed eye motif) embedded in its chest,
carved number "三·七" on the nape, chained seals, cracked rock slabs, grey stone,
<uniqueness: the gate ring + carved dao-soldier number tying to the story>
PARAMS: seed 1003
```
**獨特點**：唯一「身上有故事伏筆」的首領（刻痕與門環直通第三幕支線 `ss_03`）。

### B4 執卷人之骸·骷髏學者 `skeleton_scholar_boss`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
four-armed skeletal scholar, ten giant tomes hanging behind it on chains,
candle flames burning inside both eye sockets, bone gold robes,
<uniqueness: four arms + chained library + candle eyes>
PARAMS: seed 1004
```
**獨特點**：四臂（多段攻擊）與「背後書庫」部件（第二階段逐本燒毀）。

### B5 亡者合唱 `wraith_choir`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
five wraiths chained in a circle, a single floating heart beating at the center,
ghostly blue, chains radiating outward, sorrowful choir mood,
<uniqueness: five bodies one heart -- the heart is the real target>
PARAMS: seed 1005
```
**獨特點**：唯一「多個身體共用一個核心」的首領；對已完成 `ss_05` 的玩家傷害 −30%（**支線直接影響戰鬥**）。

### B6 雙首晶獸 `twin_golem`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
two crystal bodies sharing one core, bridged by a glowing crystal spine,
one head oriented for attack and one for defense, cyan crystal, refracted light,
<uniqueness: two independent heads on a shared core -- dual attack pattern>
PARAMS: seed 1006
```
**獨特點**：兩個頭各自獨立攻擊（需連續兩次防禦成功）；第二階段核心外露。

### B7 軍營叛將 `traitor_captain`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
military captain half-consumed by sorcerous crystal growth, half-burned cape,
holding a cracked war banner, dark red and steel, tragic figure,
<uniqueness: crystal arm replacing half the body + broken banner>
PARAMS: seed 1007
```
**獨特點**：唯一「召喚部下」型首領（軍令如山）；第二階段由「人」變為「結晶」。

### B8 鐘樓守衛 `bell_warden`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
giant warden whose torso is a great bronze bell with seven cracks,
clockwork arms ringing the bell, brass and gold, holy but worn,
<uniqueness: torso IS the bell; the seven cracks are the mechanic>
PARAMS: seed 1008
```
**獨特點**：**機制與美術合一**——鐘面七道裂痕即為「破壞鐘壁」的階段指示器（美術狀態圖 7 張）。

### B9 心魔·青霄 `inner_qingxiao`

```
POSITIVE:
pixel art boss sprite, 64x64, transparent background, 1px dark outline, 28-color palette,
a dark mirror copy of the player character, black silhouette with golden sword-qi cracks,
a mirror face reflecting the viewer, one blade identical to the player's,
<uniqueness: silhouette identical to the player; gold rim light; mirror face>
PARAMS: seed 1009 + img2img on the player sprite (denoise 0.35) 以確保剪影一致
```
**獨特點**：唯一以**玩家自己的 sprite** img2img 生成的首領（剪影必須與玩家一致）；第二階段 tint 反轉。

### B10 最終首領 墨無殤／沃卡司 `demonlord_vorkath`（三階段）

```
[階段一 · 黑巫師之王]
POSITIVE: pixel art boss sprite, 64x64, transparent background, 1px dark outline, 32-color palette,
towering sorcerer-king, tall crown with a miniature sealed star as its jewel, long dark robes,
the Staff of the Sealed Star, cold regal posture, black and gold,
<uniqueness: the sealed star literally sits in his crown>

[階段二 · 前世道體]
POSITIVE: same character revealed as a white-robed cultivator, broken sword,
sword-intent cracks in the air, identical blade style to the protagonist's,
tragic brotherly aura, white and pale gold

[階段三 · 帝骨共鳴]
POSITIVE: half of his body armored with the emperor's bone, star cracks spreading from his chest,
black-and-gold emperor regalia, the tower's night sky behind him,
<uniqueness: the protagonist's stolen bone worn as armor>

PARAMS: seed 1010; 三階段各生成一組，動畫 24/24/24 + 大絕 24 = 96 幀；32 色調色盤
```
**獨特點**：全作唯一三階段首領；第三階段的帝骨裝甲即「主角被奪走之物」，視覺與敘事同時收束。

---

## 3.6 多格敵人清單與「特殊事件徘徊者」

**2 格（8 種）**：E-07 荊棘藤 `1×2`、E-14 投石傀儡 `2×1`、E-19 書架巨像 `1×2`、E-22 墓穴爬行者 `2×1`、E-27 晶石獸 `1×2`、E-41 鏡像衛 `2×1`、E-43 墨無殤之影 `1×2`、E-48 帝骨衛 `2×1`。
這 8 種剛好落在 `rig_beast`／`rig_construct`／`rig_lord`／`rig_demon`／`rig_bone` 五組骨架上，因此**只需多 5 組 2 格動畫（110 幀）**，其餘部位與色相照舊共用。

**4 格（12 種）**：
- **10 位首領**（§3.4）——佔滿一整個 cell，是「壓迫感」的來源；其 4 格佔位同時是戰鬥場景切換的觸發條件（與 4 格敵人同格＝進入戰鬥）。
- **2 種特殊事件徘徊者**：E-44 王座之影、E-46 前世道兵王——**不以首領身分出場**，而是每層低機率出現的遊蕩者。

### 徘徊者（Roamer）規格

| 項目 | 內容 |
|---|---|
| 出現率 | 每層 8%（第五幕之後 14%），同層最多 1 隻 |
| 標示 | 進入視野時顯示名牌橫幅（UI）＋低沉音效；**視覺語言＝佔 4 格＝有事發生** |
| 行為 | 沿通道遊蕩；發現玩家後直線追擊（一般敵人固定站位，徘徊者會移動） |
| 戰鬥 | 與其同格即進入戰鬥；能力值 = 同階首領的 **HP 45% / ATK 80%**，**無第二階段** |
| 獎勵 | 必掉 1 件 T3／T4 裝備或 `mat_*` 素材；EXP ×3 |
| 可迴避 | 不強制戰鬥——玩家可繞開（牠不會穿牆，但會持續追） |

**設計效益**：玩家在 70 層中會自然學到「**佔 4 格＝有事發生**」。首領、徘徊者、特殊事件共用同一種視覺語言，不需要任何 UI 文字提示——這正是「用佔格大小做視覺分級」的核心價值。

## 3.5 變體規則（省檔案的核心）

| 變體 | 手法 | 是否新增檔案 |
|---|---|---|
| 同族不同色（毒／冰／火史萊姆等） | shader 色相偏移（±45°／±90°） | ❌ 0 張 |
| 精英 | 金色 rim light ＋ 尺寸 ×1.15 ＋ 數值 ×1.6HP／×1.35ATK | ❌ 0 張 |
| 重裝／輕裝（如 E-18 vs E-13） | 同骨架 ＋ 換部件（`part_dust_cloth` 等） | ✅ 部件 1–3 張 |
| 幕別氛圍 | 場景＋敵人同乘幕色 | ❌ 0 張 |
| 首領第二階段 | 換 tint ＋ 追加 1–3 個部件（唯一例外） | ✅ 少量 |
| 輪迴變體（前世迴響層） | 骨系／道兵系骨架 ＋ 冷藍 tint ＋ 部件換為 `part_rune_*` | ✅ 部件共用 |

**結論**：新增一個「同骨架敵人」的美術成本 ≈ **1–3 張部件圖**，而非一整套動畫（22–26 幀）。
---

## 4. 裝備設計（20 件：武器 8／護甲 6／飾品 6）

### 4.1 成長線總表

| ID | 名稱 | 槽 | 階 | 來源 | 定位 |
|---|---|---|---|---|---|
| `wand` | 見習者法杖 | weapon | T1 | 第 1 幕起始 | 均衡基準 |
| `twin_daggers` | 雙生短匕 | weapon | T1 | 村莊商店 | 快條・窄判定（高技巧） |
| `war_hammer` | 戰爭巨鎚 | weapon | T1 | 村莊商店 | 慢條・寬判定（高容錯） |
| `dao_arm_blade` | 道兵臂刃 | weapon | T2 | 第三幕道兵殘骸掉落／鍛造 | 穩定中階過渡 |
| `wand_t2_gatewarden` | 守門法杖 | weapon | T2 | 鍛造（`fr_gatewarden`，`ss_03`） | 寬判定・攻擊向 |
| `war_hammer_t3_soldier` | 軍匠重鎚 | weapon | T3 | 鍛造（`fr_soldier_blade`，`ss_07`） | 高傷・慢條 |
| `star_mote_wand` | 星塵杖 | weapon | T4 | 第八幕後鍛造（素材：星塵核心） | 充能特化 |
| `qingxiao_blade` | 青霄 | weapon | T4 | 支線 `ss_09`（復仇線限定） | 最高會心・最慢條 |
| `cloth_robe` | 學者布袍 | armor | T1 | 第 1 幕起始 | 無懲罰基準 |
| `swift_leather` | 疾風皮甲 | armor | T2 | 村莊商店 | 條速向 |
| `guardian_plate` | 守護鎧甲 | armor | T2 | 村莊商店 | 護盾向 |
| `bell_weave_robe` | 鐘紋法衣 | armor | T3 | 第八幕神殿取得／鍛造 | 狀態抗性 |
| `mirror_scale` | 鏡鱗甲 | armor | T4 | 第九幕鏡像衛掉落 | 反傷向 |
| `emperor_bone_armor` | 帝骨鎧 | armor | T5 | 第十幕（F64–F69） | 終盤綜合 |
| `focus_talisman` | 專注護符 | accessory | T2 | 支線 `ss_05`（顯影獎勵） | 充能速度 |
| `berserker_charm` | 狂戰符 | accessory | T2 | 村莊商店 | 攻高防低 |
| `guardian_ring` | 守護者之戒 | accessory | T3 | 村莊商店／鍛造 | 防禦保底 |
| `soul_echo_bell` | 魂返鐘 | accessory | T3 | 支線 `ss_08` | 保命（受致命傷留 1 HP） |
| `memory_shard_focus` | 憶晶飾 | accessory | T4 | 記憶碎片 ≥6 片時於樞紐製作 | 記憶／技能加成 |
| `cycle_mark_ring` | 輪迴印記戒 | accessory | T5 | **輪迴限定**（`cycleIndex ≥ 1` 的樞紐） | 跨輪成長象徵 |

> 既有 9 件（`wand`／`twin_daggers`／`war_hammer`／`cloth_robe`／`guardian_plate`／`swift_leather`／`focus_talisman`／`berserker_charm`／`guardian_ring`）**數值沿用 `data/equipment.json` 現值**，本文件只補其**外觀規格與 prompt**；新增 11 件則連能力值一起定義。

### 4.2 武器（8 件）

**A-01 見習者法杖 `wand`**
外觀：短木杖、頂端一枚未打磨的水晶；杖身有學徒刻痕。／手持部件：`part_hand_wand`（16×16 覆蓋於手部）／圖示：`equipment/wand_icon.png`。
能力值：**沿用現值**；定位＝均衡（條速與判定帶皆中庸）。
Prompt：`apprentice's wooden wand with a rough crystal tip, carved student marks`

**A-02 雙生短匕 `twin_daggers`**
外觀：成對短匕、刀身一分為二、護手纏布。／手持部件：`part_hand_daggers`×2。
能力值：**沿用現值**（ATK −2；條速極快、判定帶窄）。
Prompt：`pair of twin daggers with cloth-wrapped guards, split blades`

**A-03 戰爭巨鎚 `war_hammer`**
外觀：方形鎚頭、木柄包鐵，鎚面有撞擊凹痕。／手持部件：`part_hand_hammer`（縮放 1.2）。
能力值：**沿用現值**（慢條、寬判定、高容錯）。
Prompt：`heavy war hammer with a square iron head and dented striking face, wooden haft`

**A-04 道兵臂刃 `dao_arm_blade`（新）**
外觀：由道兵殘骸的小臂改造、刃面刻符文。／手持部件：`part_hand_dao_blade`。
能力值：ATK **+4**；bar `v0 18 / vmax 240 / rampTime 1.25 / greenHalf 12 / blueOuter 32 / redOuter 62 / maxMult 2.1`；被動：對 `construct` 系敵人 +10% 傷害。
來源：第三幕道兵殘骸掉落（15%）／鍛造（`fr_dao_arm`：素材 `mat_echo_shard`×2）。
Prompt：`blade grafted from a stone automaton's forearm, runes on the edge, ancient`

**A-05 守門法杖 `wand_t2_gatewarden`（新・鍛造）**
外觀：原法杖接上門環殘片、環上眼紋緊閉。／手持部件：`part_hand_wand_gate`。
能力值：ATK **+3**；bar `v0 16 / vmax 230 / rampTime 1.35 / greenHalf 16 / blueOuter 38 / redOuter 68 / maxMult 2.15`；判定帶 **+12%**。
來源：`fr_gatewarden`（`ss_03` 圖譜）。
Prompt：`wooden staff crowned with a broken gate ring, closed-eye motif, rune etched`

**A-06 軍匠重鎚 `war_hammer_t3_soldier`（新・鍛造）**
外觀：軍營匠人重製、鎚頭嵌軍徽與三道箍。／手持部件：`part_hand_hammer_t3`（1.25）。
能力值：ATK **+9**；bar `v0 22 / vmax 280 / rampTime 1.5 / greenHalf 14 / blueOuter 36 / redOuter 66 / maxMult 2.3`；條速 +10%。
來源：`fr_soldier_blade`（`ss_07`）。
Prompt：`military smith's great hammer with regimental emblem and three iron bands`

**A-07 星塵杖 `star_mote_wand`（新）**
外觀：杖頭為懸浮的星核碎屑、周圍有微型星環。／手持部件：`part_hand_star_wand`（含動態 4 幀）。
能力值：ATK **+6**；bar `v0 18 / vmax 250 / rampTime 1.3 / greenHalf 14 / blueOuter 34 / redOuter 64 / maxMult 2.25`；必殺充能 **+25%**。
來源：第八幕後鍛造（素材：`star_core_fragment` 1 ＋ `mat_echo_shard` 6）。
Prompt：`staff crowned with floating star-core fragments and a miniature ring of stars`

**A-08 青霄 `qingxiao_blade`（新・支線限定）**
外觀：前世佩劍、劍身如夜色、劍脊有一線青光；劍鞘以星紋封條纏繞。／手持部件：`part_hand_qingxiao`（含劍氣 6 幀 VFX 分離）。
能力值：ATK **+14**；bar `v0 26 / vmax 300 / rampTime 1.8 / greenHalf 10 / blueOuter 26 / redOuter 52 / maxMult 2.6`（**判定帶最寬、條速最慢**）；會心 +8%。
主動技：`a_qingxiao_edge`（每場一次必會心，傷害 ×1.8）。
來源：`ss_09`（`flag_path_vendetta`）。
Prompt：`ancient cultivator's blade, night-black steel with a single thread of azure light, star-patterned seal on the sheath`

### 4.3 護甲（6 件）

**A-09 學者布袍 `cloth_robe`**：麻布袍、下襬有墨漬。／`part_body_robe`。能力值沿用現值（無懲罰基準）。Prompt：`scholar's linen robe with ink stains, simple and worn`

**A-10 疾風皮甲 `swift_leather`**：輕皮甲、肩甲為鳥羽造型。／`part_body_leather`。沿用現值（條速向）。Prompt：`light leather armor with feathered shoulder guards, agile look`

**A-11 守護鎧甲 `guardian_plate`**：厚板甲、胸甲有盾徽。／`part_body_plate`。沿用現值（護盾向）。Prompt：`heavy plate armor with a shield emblem on the chest`

**A-12 鐘紋法衣 `bell_weave_robe`（新）**
外觀：織入銅線的長袍、袍面有鐘紋與七道細裂。／`part_body_bell_robe`。
能力值：DEF **+6**；狀態抗性：`st_confuse`／`st_fear` 時間 −50%；護盾效率 +8%。
來源：第八幕神殿寶箱／鍛造（`fr_bell_weave`）。
Prompt：`bronze-threaded ceremonial robe with bell patterns and seven fine cracks`

**A-13 鏡鱗甲 `mirror_scale`（新）**
外觀：鏡面鱗片甲、會映出周圍景物（shader 反射）。／`part_body_mirror`。
能力值：DEF **+8**；反傷 **15%**；受擊時 10% 機率使攻擊者 `st_shatter_def`。
來源：第九幕鏡像衛掉落（20%）。
Prompt：`scale armor of polished mirror plates reflecting the surroundings`

**A-14 帝骨鎧 `emperor_bone_armor`（新・終盤）**
外觀：以大帝之骨為內襯、外覆黑金甲片，胸口有心魔裂紋。／`part_body_bone_emperor`。
能力值：DEF **+12**、HP 上限 **+8%**；戰鬥開始時獲得 `st_iron_guard` 1 層；受到致命傷時改為消耗 1 記憶碎片（每輪限 1 次）。
來源：第十幕 F64–F69 寶箱／鍛造（素材：`mat_emperor_bone`）。
Prompt：`black-and-gold armor lined with an emperor's bone, a crack over the heart`

### 4.4 飾品（6 件）

**A-15 專注護符 `focus_talisman`**：木牌上刻封氣紋、以紅繩繫。／小巧，掛於腰間（`part_acc_talisman`）。沿用現值（充能 +20%）。Prompt：`wooden talisman etched with qi-sealing symbols, red cord`

**A-16 狂戰符 `berserker_charm`**：獸牙串成的符、帶著血鏽。／沿用現值（攻高防低）。Prompt：`berserker charm made of strung beast fangs, rusted with dried blood`

**A-17 守護者之戒 `guardian_ring`**：銀戒、戒面為盾形。／沿用現值（防禦保底）。Prompt：`silver ring with a shield-shaped face, well-worn`

**A-18 魂返鐘 `soul_echo_bell`（新）**
外觀：掌心大小的銅鐘、內側刻亡者之名。／`part_acc_bell`；主動技 VFX：鐘形波紋 8 幀。
能力值：必殺充能 +15%；主動技 `a_sanctuary_echo`（受致命傷保留 1 HP，每場 1 次）。
來源：`ss_08`。
Prompt：`palm-sized bronze bell engraved with the names of the dead inside`

**A-19 憶晶飾 `memory_shard_focus`（新）**
外觀：以記憶碎片串成的飾品、每片映出一段模糊畫面（shader 微動）。／`part_acc_shards`（6 幀微動）。
能力值：記憶碎片每 1 片 → 技能效果 +1.5%（上限 +10.5%）；`insight` 相關對話選項顯示「已憶起」標記。
來源：記憶碎片 ≥6 時於樞紐製作。
Prompt：`ornament strung from memory shards, each fragment showing a blurred vision`

**A-20 輪迴印記戒 `cycle_mark_ring`（新・輪迴限定）**
外觀：樸素黑戒、內側刻著輪迴次數的刻痕（每次輪迴 +1 道，最多 9 道）。／`part_acc_cycle_ring`。
能力值：EXP **+10%**；每渡過 1 次輪迴，起始技能點 +1（上限 +5）；死亡時不掉落任何道具（保證）。
來源：`cycleIndex ≥ 1` 後於樞紐領取。
Prompt：`plain black ring with tally marks carved inside, one per rebirth`

### 4.5 裝備外觀規則

| 項目 | 規則 |
|---|---|
| 手持部件 | 每把武器 1 個 `part_hand_*`（16×16），戰鬥中依攻擊動作第 2 幀出現揮擊弧光（可與武器分離成 VFX） |
| 身體部件 | 護甲 1 個 `part_body_*`（32×32），覆蓋在地圖與戰鬥 sprite 上 |
| 飾品 | 1 個 `part_acc_*`（8×8），腰間／手上小圖，地圖上僅顯示 1 個（避免雜亂） |
| 鍛造光效 | 不需新圖：以 shader `uRimLight` ＋ 顏色偏移表示 T2/T3/T4 |
| 圖示 | 每件 1 張 32×32（背包卡片用），同槽位共用邊框（4 種邊框：武器／護甲／飾品／特殊） |

---

## 5. 資產總表（要做幾張圖、幾段動畫）

### 5.1 動畫（844 幀）

| 類別 | 數量 | 單價（幀） | 小計 |
|---|---|---|---|
| 敵人共用骨架（**1 格** 32×32） | 9 rigs | 22 | 198 |
| 敵人共用骨架（**2 格** 64×32 / 32×64） | 5 rigs | 22 | 110 |
| 首領獨立骨架（**4 格** 64×64） | 10 | 43（含第二階段） | 430 |
| 徘徊者 4 格（非首領） | 2 | 26 | 52 |
| 狀態 VFX | 36 | 6 | 216 |
| **合計** | | | **1,006 幀** |

> 若不採部件制：50 敵人各自 22 幀（1,100）＋首領 430＋徘徊者 52＋VFX 216 = **1,798 幀**。**本方案省下 44%**，且多格敵人只多 5 組 2 格動畫。

### 5.2 靜態圖（445 張）

| 類別 | 數量 | 尺寸 |
|---|---|---|
| 敵人部件 | 66（9 rigs × 4 ＋ 30 專屬） | 8–24 px |
| 多格敵人專用（2 格部件 10、2/4 格圖鑑 14） | 24 | 8–128 px |
| 敵人圖鑑／頭像 | 50 | 64×64 |
| 首領圖鑑／頭像 | 10 | 128×128 |
| 裝備（圖示＋手持／身體） | 40 | 32×32 / 16×16 |
| 狀態圖示 | 36 | 32×32 |
| 場地危害覆蓋層 | 6 | 平鋪素材 |
| Tile 與場景物件 | 161（10 幕 × 14 ＋ 21 專用） | 32×32 |
| UI 圖示（技能節點 36／樹 3／鍛造 6／樞紐 8／HUD 12／結局 15／標誌 2） | 82 | 24–512 px |
| **合計** | **469 張** | |

### 5.3 總量與體積

| 指標 | 數值 |
|---|---|
| 動畫幀 | **1,006**（1 格 198／2 格 110／首領 430／徘徊者 52／VFX 216） |
| 靜態圖 | **469** |
| **資產總件數** | **1,475** |
| PNG 原始體積（32×32 ≈1.2 KB、64×64 ≈3.5 KB） | **約 2.8 MB** |
| atlas 合併後（預估） | **約 2.6 MB**（2048×2048 分頁、1px padding） |
| 目標上限 | **≤ 3 MB**（現行 `toms_web.data` 208 KB） |

> ⚠️ 加入多格敵人後體積已接近 3 MB 上限（2.8 MB）。若需再瘦身，建議依序處理：① 結局插圖改為「漸層＋剪影」程序化生成（省 ≈1 MB，15 張中 12 張可用此方式）；② 2 格骨架只做 4 組（合併 `rig_demon` 與 `rig_lord` 的 2 格版）；③ tile 包改為每幕共用 80% 圖塊。三者合計可省約 1.3 MB。

### 5.4 每幕分配（便於分階段製作）

| 幕 | 一般敵人 | 首領 | 新狀態 | 新裝備 | Tile 包 | 該幕資產合計（約） |
|---|---|---|---|---|---|---|
| 一～二 | 8 | 2 | 6 | 6 | 2 包 | 180 |
| 三～四 | 8 | 2 | 6 | 4 | 2 包 | 175 |
| 五～六 | 8 | 2 | 6 | 3 | 2 包 | 170 |
| 七～八 | 8 | 2 | 6 | 4 | 2 包 | 175 |
| 九～十 | 8 | 2 | 6 | 3 | 2 包 | 185 |
| 多格專用（2 格 5 組動畫、徘徊者 2 組、多格圖鑑 14） | — | — | — | — | — | 186 |
| 通用（UI／狀態／圖鑑邊框） | — | — | 6 | — | — | 404 |

### 5.5 人力估算（單人美術）

| 工作 | 件數 | 單件工時（估） | 小計 |
|---|---|---|---|
| 骨架動畫（含生成＋後處理＋修圖） | 9 rigs | 6 h | 54 h |
| 首領（含三階段與獨特 VFX） | 10 | 14 h | 140 h |
| 部件與變體 | 66 | 0.4 h | 26 h |
| 靜態圖示與 tile | 445 | 0.25 h | 111 h |
| 狀態 VFX | 36 | 1 h | 36 h |
| UI 與結局插圖 | 82 | 1.5 h | 123 h |
| **合計** | | | **約 520 h（≈ 13 週全職；含多格敵人）** |

> 以 ComfyUI ＋「生成 → 後處理腳本」流程，可將骨架與圖示類工作壓到 40–50%（AI 生成＋人工修正），實務上約 **6–8 週**。

---

## 6. 與遊戲資料的對接

### 6.1 `data/enemies.json` 追加欄位

```json
{
  "wraith_choir": {
    "footprint": [2, 2],                  // 4 格：佔滿一個 cell（首領）
    "appearance": { "rig": "boss_wraith_choir", "tint": { "mode": "hue", "hueShiftDeg": -70 } },
    "combat": { "phases": 2, "arenaGimmick": "choir_circle" }
  },
  "dao_lord": {
    "footprint": [2, 2],                  // 4 格：徘徊者（非首領）
    "roamer": { "spawnChance": 0.08, "chase": true, "nameBanner": true,
                "rewardTable": ["equip_t3", "mat_echo_shard"], "expMul": 3,
                "statScaleVsBoss": { "hp": 0.45, "atk": 0.80 }, "phases": 1 }
  },
  "thorn_vine": { "footprint": [1, 2] },  // 2 格：1 寬 × 2 高
  "stone_lobber": { "footprint": [2, 1] },// 2 格：2 寬 × 1 高
  "skeleton_archer": {
    "name": { "zh_TW": "骷髏弓手" },
    "sprite": "skeleton_archer.png",
    "hp": 52, "atk": 22, "def": 4, "exp": 18, "gold": 11,
    "atkIntervalMs": 2200,
    "tier": 3, "act": 3, "floors": ["F15", "F21"],
    "role": "ranged",
    "footprint": [1, 1],
    "appearance": {
      "rig": "rig_bone",
      "tint": { "mode": "hue", "hueShiftDeg": 18, "satMul": 0.9 },
      "parts": [
        { "id": "part_bow", "slot": "main_hand", "scale": 1.30, "offset": [6, -4] },
        { "id": "part_upper_arm", "slot": "arm", "scale": 1.15 }
      ],
      "icon": "icons/enemy/skeleton_archer.png",
      "promptFile": "prompts/e13_skeleton_archer.txt"
    },
    "skills": [
      { "id": "sk_pierce_arrow", "nameKey": "skill.sk_pierce_arrow", "kind": "attack",
        "cooldownMs": 6000, "effects": { "shieldIgnorePct": 20, "damageMul": 1.2 },
        "vfx": "vfx_pierce" },
      { "id": "sk_backstep", "kind": "move", "cooldownMs": 9000,
        "effects": { "retreatTiles": 2, "selfStatus": "st_swift" } }
    ],
    "eliteOf": null,
    "statusImmunities": [],
    "roamer": null
  }
}
```

### 6.2 `data/equipment.json` 追加欄位

```json
{
  "qingxiao_blade": {
    "id": "qingxiao_blade", "slot": "weapon", "tier": 4,
    "name": { "zh_TW": "青霄" },
    "sprite": "equipment/qingxiao_icon.png",
    "stat": { "atk": 14, "critChance": 0.08 },
    "bar": { "v0": 26, "vmax": 300, "rampTime": 1.8,
             "greenHalf": 10, "blueOuter": 26, "redOuter": 52, "maxMult": 2.6 },
    "actives": ["a_qingxiao_edge"],
    "appearance": { "handPart": "part_hand_qingxiao", "vfx": "vfx_qingxiao_edge",
                    "iconFrame": "weapon_legendary" },
    "lore": { "zh_TW": "這把劍認得你。" },
    "source": { "sideStory": "ss_09", "requires": { "choiceMade": ["c_final_stance", "opt_vendetta"] } }
  }
}
```

### 6.3 新檔 `data/status.json`

```json
{
  "st_burn": {
    "id": "st_burn", "name": { "zh_TW": "燃燒" }, "kind": "debuff",
    "icon": "icons/status/st_burn.png",
    "vfx": { "id": "vfx_st_burn", "frames": 6, "anchor": "body", "color": "#ff7a2a" },
    "stack": { "max": 5, "mode": "intensity" },
    "effects": { "damagePctPerSec": 3, "durationMs": 5000 },
    "cleanse": ["st_iron_guard"]
  }
}
```

### 6.4 工具與資料夾

| 工具／資料夾 | 用途 |
|---|---|
| `tools/make_variants.py` | 由 base rig ＋ tint ＋ 部件縮放產生變體（**不重跑 ComfyUI**） |
| `tools/pack_atlas.py` | 產生 atlas ＋ `manifest.json`（沿用現行 sprite manifest 格式） |
| `assets/prompts/<id>.txt` | 每個敵人的實際 prompt（可重製） |
| `assets/sprites/enemy/<rig>/` `assets/sprites/boss/<id>/` `assets/parts/` | 美術來源 |
| `assets/icons/{enemy,status,ui}/` | 圖示 |

### 6.5 與既有系統的相容

| 系統 | 需要的小改動 |
|---|---|
| sprite shader | 新增 `uTintMode` / `uRimLight` / `uOverlay` / `uDistort` 四個選用 uniform（未使用時行為不變） |
| `spriteLayer()` | 支援部件圖層（`body` / `main_hand` / `off_hand` / `acc` 分層繪製） |
| `entity_status.h` | **不動**（關卡格狀態）；戰鬥狀態走新的 `status.json` ＋ runtime 容器 |
| 戰鬥數值 | 敵人 `atkIntervalMs` 已存在（battle v2 的敵方計時器）；本表只是填值 |
| `stage.h` 的 `Entity` | 新增 `footprint`（預設 1×1；舊關卡 JSON 未提供時自動視為 1×1，**不破壞相容**） |
| 繪製路徑 | `draw()` 的實體繪製由「固定 1 tile」改為「依 footprint 計算 w/h（tile 為單位）」；y-sort 以最下緣 tile 排序 |
| 碰撞與觸發 | 進入任一佔格即觸發戰鬥；佔格全部視為障礙（玩家不可穿越） |
| 徘徊者 AI | 新增 `RoamerController`：巡邏→察覺→追擊；不穿牆、不跨樓層 |
| UI | 名牌橫幅（首領／徘徊者進入視野時顯示，12 幀進場演出可與橫幅同步） |
| 存檔 | 狀態不進存檔（戰鬥內暫態）；裝備與外觀進 `RunSaveData`；徘徊者的「已擊敗」狀態走 `entityStatus`（`Defeated`） |

---

## 7. 製作守則與驗證（美術必須自檢）

| # | 守則 | 為什麼 |
|---|---|---|
| R1 | **剪影優先**：同骨架的不同敵人在純黑剪影下必須可分辨 | 玩家在戰鬥中只有 0.5 秒判斷 |
| R2 | 變體**不得改變骨架輪廓**的原則，只能靠部件與色相 | 否則無法共用動畫 |
| R3 | 每 rig 共用 24 色調色盤；變體只做色相偏移，不加新色 | 維持系列感與體積 |
| R4 | 色盲可辨：紅／綠不可作為唯一區分（用形狀＋亮度差） | 狀態圖示與敵人職業色 |
| R5 | 輪廓線固定 1px `#1b1420`，禁止抗鋸齒 | 縮放到 96×96 時才不會糊 |
| R6 | 圖示必須在 20×20 顯示時仍可辨識 | HUD 實寸 |
| R7 | 首領的機制必須**看得見**（如鐘面七道裂痕） | 玩家要能讀懂階段 |
| R8 | 每件成品都要寫回 `assets/prompts/<id>.txt` | 可重製、可迭代 |
| R9 | atlas 分頁 ≤2048×2048，且同 rig 不分頁 | 減少 draw call 與載入尖峰 |
| R10 | 全部美術 ≤3 MB；每次新增資產後量測 | web 首頁載入體驗（現在 208 KB） |
| R11 | 佔格只允許 **1／2／4 格**，且每 grid 固定 32×32 px（不做整體放大） | 對齊 2×2 cell 細分；避免模糊 |
| R12 | 同一個 cell 內不得有兩個實體的佔格重疊；4 格敵人獨佔一個 cell | 生成器必須保留佔格 |
| R13 | **佔 4 格 = 重要**：只有首領與徘徊者可用 4 格；2 格僅限上表 8 種 | 維持「佔格大小＝視覺分級」的可信度 |

---

## 8. 製作順序（建議）

| 階段 | 內容 | 產出 |
|---|---|---|
| A1 | shader 四個 uniform ＋ `make_variants.py` ＋ `pack_atlas.py` | 變體管線可跑 |
| A2 | 3 組骨架（`rig_slime`／`rig_bone`／`rig_humanoid`）＋ 12 敵人（一～四幕）＋ 第 1 組 2 格骨架（`rig_beast` 或 `rig_construct`） | 前 28 層可玩的美術，含首個 2 格敵人 |
| A3 | 4 位首領（B1–B4）＋ 6 狀態 VFX | 一～四幕首領戰成立 |
| A4 | 其餘 6 骨架 ＋ **5 組 2 格骨架** ＋ 28 敵人 ＋ 6 首領 ＋ 2 徘徊者 | 全 70 層敵人齊備（含全部多格敵人） |
| A5 | 20 件裝備（圖示＋部件）＋ 鍛造／樞紐 UI | 四大系統可視化 |
| A6 | 36 狀態圖示與 VFX ＋ 6 場地危害 | 狀態系統完整 |
| A7 | 10 幕 tile 包 ＋ 結局插圖 15 張 | 全案美術完成 |

> 每個階段結束時跑一次 `pack_atlas.py` 並量測體積（R10），確保仍在 3 MB 預算內。
