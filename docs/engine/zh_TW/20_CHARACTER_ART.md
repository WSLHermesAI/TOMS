> 英文原文：[20_CHARACTER_ART.md](../20_CHARACTER_ART.md)

# 20 — 戰鬥角色美術：手繪關鍵姿勢（B），以及備案 3D（C）

擁有者（2026-11-02，[../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) Q58）：
- **玩家的外觀：** hero_m 概念圖 606。
- **做法：** 戰鬥角色要看起來有動感，只用一張圖拆成零件再在動畫編輯器組合不夠。先試 **B（手繪關鍵姿勢）**；不夠好再試
  **C（3D glTF 模型）**。
- **新電腦：** 需要的東西都要能在新電腦上安裝（本頁與 `tools\setup_ai_art.cmd`）。

## 1. 三種做法比較

| | A. 一張圖拆成零件 | B. 手繪關鍵姿勢 | C. 3D glTF 模型 |
|---|---|---|---|
| 怎麼做 | 概念圖 → 零件 → 動畫編輯器（brawler，[18](18_NODE_EFFECTS.md) §5） | 每個姿勢是同一個角色的一張畫，以 sprite key 加上補間與特效播放 | 概念圖 → 3D 網格 → 骨架 → 動畫 → 戰鬥中用 glTF 繪製器 |
| 動態方向 | 只有一個朝向 | 畫出來的任何姿勢 | 完全自由；攝影機特寫 |
| 符合手繪風格 | 是 | 最好：每一格都是畫 | 需要手繪感或卡通著色器 |
| 主要風險 | 平面、紙娃娃感 | 畫與畫之間細節會飄移（IP-Adapter 維持臉與服裝） | AI 網格粗糙（手、臉、背面）；本機無法上貼圖；骨架與動畫需要新工具 |
| 新工具 | 無 | 無 | Hunyuan3D-2 模型、Blender（§4） |

## 2. B：需要什麼

全部由 **`tools\setup_ai_art.cmd`** 安裝（新電腦：`setup_new_pc.bat` 會詢問；見
[../../project/zh_TW/09_NEW_MACHINE_SETUP.md](../../project/zh_TW/09_NEW_MACHINE_SETUP.md)）。

| 部分 | 內容 | 放在哪裡 |
|---|---|---|
| GPU | 8 GB 以上的 NVIDIA 顯示卡（本機：RTX 4070，12 GB；一張圖約 20 秒） | — |
| ComfyUI | 伺服器，由 `AIGamestyle\_pipeline\tools\start_comfyui_with_flags.bat` 啟動（設定 `COMFY_EXTRA_ARGS=--disable-auto-launch` 可略過它的提問） | `AIGamestyle\_comfyui\ComfyUI`，連接埠 8188 |
| 模型資料夾 | ComfyUI 從 `ComfyUI\extra_model_paths.yaml` 設定的資料夾讀取模型（本機：`D:\Work\AI\models`） | — |
| 主模型 | **Juggernaut XL v9** `Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors`（7.1 GB，Hugging Face RunDiffusion/Juggernaut-XL-v9） | `checkpoints` |
| ControlNet | ControlNet Union SDXL ProMax `controlnet-union-sdxl-promax.safetensors`（2.5 GB）：OpenPose 骨架讓身體擺出姿勢 | `controlnet` |
| IP-Adapter | `ip-adapter-plus_sdxl_vit-h.safetensors`（0.8 GB）：維持同一個人與同一套服裝 | `ipadapter` |
| CLIP Vision | `CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors`（2.5 GB），IP-Adapter 需要 | `clip_vision` |
| 自訂節點 | ComfyUI_IPAdapter_plus、comfyui_controlnet_aux、ComfyUI-Inspyrenet-Rembg（去背） | `ComfyUI\custom_nodes` |
| Python | 腳本需要 Pillow 與 numpy（`pip install pillow numpy`） | — |

所有模型（含 sprite 工作流程的）共約 20 GB。依專案規則，**下載**模型沒問題；不會上傳任何專案內容。線上 3D 服務（ComfyUI 中的
Meshy、Tripo、Rodin 節點）**不使用**。

## 3. B：怎麼做

1. **選外觀：** `python tools/art/make_brawler_art.py concept --char hero_m --seeds 505,606,707,808`。圖片在
   `Build/art_out/hero_m/`。擁有者選了 606。
2. **畫姿勢：** `python tools/art/make_hero_poses.py --weapon bow --seeds 33,44`。武器的每個姿勢（腳本中的 `POSES`）用為該姿勢
   畫的骨架，加上選定的概念圖作為 IP-Adapter 參考，每個 seed 畫一張。輸出是 `Build/art_out/hero_bow/<姿勢>_<seed>.png`、
   去背的 `_cut.png`，以及 `sheet.png`。
3. **每個姿勢挑最好的 seed**，畫得不好的姿勢用新的 seed 重畫（`--pose attack --seeds 55,66`）。
4. **做出片段：** `python tools/art/make_hero_anim.py --weapon bow --pick idle=55 --pick charge=44 --pick miss=44
   --pick attack_big=44`。每個選用的姿勢用同一個框裁切（腳底保持在同一條線上），打包進 `assets/media/atlas/hero_bow.atlas`，
   並在 `assets/media/anim/hero_bow.anim` 中以 sprite key 加上短補間（擠壓、前衝、後座）播放：idle、charge、attack、miss、
   attack_big、hurt。`items.json` 中弓的 `battleAnim` 是 `{"character": "anim/hero_bow.anim"}`（沒有手持武器：武器是畫上去的）。
   片段可在動畫編輯器中微調；武器特效與受擊閃光照舊由戰鬥提供。
   - 畫不好的姿勢從好的姿勢做出來：放箭 = 滿弓加上彈出與後座；受傷 = 準備姿勢往後傾。
   - 畫的節點**不能叫 `body`**：戰鬥會把玩家的地圖 sprite 放到名為 `body` 的節點上（`Game::fitClipToCharacter`，給任何角色
     都能用的片段），那樣會蓋掉畫。
   - 片段長度依戰鬥的計時，不依美術：`attack_big` 與其他弓的片段一樣是 0.8 秒（`tests/scenarios/battle_bow_idle` 要求那時弓已放下）。

**目前結果（弓，2026-11-02）：**

| 姿勢 | 畫得好 | 選用 |
|---|---|---|
| idle（準備） | 是 | 55 或 66 |
| charge（滿弓） | 是 | 44 |
| miss（踉蹌） | 是 | 44 |
| attack_big（跳躍強力射擊） | 是 | 44 |
| attack（放箭） | 否：三輪後弓仍在錯的手上 | 由 charge 44 做出（步驟 4） |
| hurt（後仰） | 否：角色一直在瞄準 | 由 idle 做出（步驟 4） |

每個姿勢都是同一個人。有些姿勢（放箭、後仰）很難只靠骨架與文字提示畫出來。
在實際戰鬥中（2026-11-02），英雄在弓的每個狀態都會畫出來，battle_bow、battle_bow_charge 與 battle_bow_idle 用這些片段都通過。

## 4. C：需要什麼（如果 B 不夠好）

擁有者判斷 B 不夠好（2026-11-02：「看起來是一張靜態圖加上簡單晃動」），選擇 C。

| 部分 | 內容 | 說明 |
|---|---|---|
| 圖轉 3D | **Hunyuan3D-2** 形狀模型 `hunyuan3d-dit-v2-0.fp16.safetensors`（4.9 GB，Hugging Face tencent/Hunyuan3D-2，騰訊混元社群授權），放在模型資料夾的 `checkpoints`，用 ComfyUI 內建的 Hunyuan3D 節點執行；由 `tools\setup_ai_art.cmd` 安裝 | 從一張圖做出網格；內建節點不產生貼圖 |
| 貼圖 | Hunyuan3D-2 Paint，或把畫烘焙到網格上 | 上色模型需要更多 GPU 記憶體；先在 12 GB 上確認 |
| 骨架與動畫 | **Blender**（免費；`winget install BlenderFoundation.Blender`） | 自動綁骨外掛，然後手作片段或用有授權的本機動畫集 |
| 遊戲中 | 戰鬥用現有的 glTF 繪製器（蒙皮、PBR）畫 glTF 戰鬥者，位於背景與 UI 之間，加上手繪感或卡通著色器 | 標題畫面已經在畫 glTF |

**試做（2026-11-02）：**

```
python tools/art/make_hero_3d.py --seed 1
Build\windows-release\bin\gltf_viewer.exe Build\art_out\hero_3d\hero_1.glb
```

腳本把概念圖 606 的人物置中放在白色正方形上，由 ComfyUI 做出形狀（RTX 4070 約 40 秒：14.2 萬頂點、38.3 萬三角形），
並以從正面看到的概念圖顏色為每個頂點上色（COLOR_0）。

- **形狀：** 好。身體、頭、頭髮、披風、腰帶、腰包與靴子都有厚度；披風是身體後面獨立的一片。
- **顏色（第一次）：** 只有正面正確；側面與背面是條紋。

**每一面都有顏色（2026-11-02，擁有者：「ok do it」）。** 腳本現在用已安裝的模型（不需新下載）畫出其他面：從背面、兩側與左後方
（`VIEWS`）看到的網格深度，讓 ControlNet Union（depth 模式）在同一個身體上畫出那個視角，IP-Adapter（概念圖 606）維持人物與服裝。
每個頂點混合看得到它的視角（正面用概念圖），依各視角看它的正對程度加權；沒有視角看到的頂點取最近鄰的顏色；顏色再與鄰點平均兩次。
畫是 `Build/art_out/hero_3d/view_<名稱>_<seed>.png`；四面的模型圖是 `Build/art_out/hero_3d/views.png`。

過程中解決的問題：
- **披風的顏色：** 畫出的視角會把概念圖的藍色上衣畫在披風上，直到提示詞加權（`CAPE`，`(…brown leather cape…:1.5)`）並把
  IP-Adapter 降到 0.6。畫出棕色披風的 seed 設為預設（`--view-seeds 9,9,9,8`）；右後方視角畫出了臉，所以不用。
- **披風的斑點：** 披風比形狀的網格還薄，surface net 留下兩種方向的面與被 3-4 個面共用的邊。法線不看面的方向（取各面法線的主軸，
  朝外），面再重新排列方向；一個視角看到的薄片兩面都取那個視角的顏色。
- **表面：** 10 輪 Taubin 平滑（`--smooth`）。

仍粗糙的地方：額頭有一塊淺色（正面投影造成）、背面細節較柔（多張畫混合）、38.3 萬個三角形。

**骨架與片段（2026-11-02，擁有者：「yes」）。** 以 `winget install BlenderFoundation.Blender` 安裝 Blender 5.2 LTS
（腳本在 Program Files 下尋找，或設定 `TOMS_BLENDER`）。

```
python tools/art/make_hero_rig.py            （約 15 秒）
Build\windows-release\bin\gltf_viewer.exe Build\art_out\hero_3d\hero_rigged.glb --anim=draw
```

`make_hero_rig.py` 把概念圖的 OpenPose 關節（`make_brawler_art.JOINTS`：概念圖就是照它畫的）放到網格上，x 與 y 取自圖，
深度取自網格，另加一條沿披風背面往下的鏈（`joints.json`）。接著 Blender（`tools/art/hero_rig_blender.py`，無介面）：

1. **清理並減少網格：** voxel remesh（surface net 的網格有被 3-4 個面共用的邊，無法減面）、把顏色複製回來、減到約 3.8 萬面、
   刪除小的分離碎片（有它們時自動權重會失敗）。
2. **把顏色烘焙成貼圖**（2048²，`hero_rigged_colour.png`，打包進 GLB）：減面後的網格展 UV，把完整網格的顏色烘焙上去（Cycles）。
   2 萬個頂點的頂點色會讓臉變糊。
3. **建立骨架**（24 根骨頭：臀、脊椎、胸、頸、頭、手臂、腿、3 根披風骨、弓弦）並以 Blender 的自動權重**蒙皮**。
4. **加上弓**在左手（剛體）與弓弦，拉弓時弦的中段跟著右手。
5. **擺出片段**：以瞄準骨頭（手臂用簡單的兩骨 IK）擺姿勢、烘焙，每個片段匯出為一個 glTF 動畫：`idle`（2 秒，呼吸）、
   `draw`（1.8 秒：舉弓、滿弓、停住、放箭、回到 idle）、`charge`（1.2 秒，停在滿弓並微微顫動）。

弓手胸口朝向攝影機，朝 +x（畫面右方，戰鬥中敵人的位置）射擊。片段的 GIF：`Build/art_out/hero_3d/draw.gif`、`idle.gif`、
`charge.gif`。

仍粗糙的地方：臉偏糊（顏色來自 832 × 1216 的概念圖，臉約 150 像素寬）、滿弓時轉過去的頭多半只看到頭髮、片段由程式擺出
（可以在 Blender 中手動調整：開啟 GLB）。

**尚未做：** 在戰鬥中繪製 glTF 戰鬥者（用現有的 glTF 繪製器，位於背景與 UI 之間）、劍與法杖的片段、更清楚的臉（把概念圖直接
投影到貼圖，或另畫一張臉部特寫）。
