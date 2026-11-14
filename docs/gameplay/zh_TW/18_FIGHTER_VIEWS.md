> 英文原文：[18_FIGHTER_VIEWS.md](../18_FIGHTER_VIEWS.md)

# 18 — 戰鬥者的顯示：同一個介面下的 2D 或 3D，以及錄製畫面

擁有者（2026-11-02）：3D 英雄看起來很差，下一步再試 2D，做法像 Spine（各部位畫出不同方向，在動畫編輯器中組合）。之後可能再試
3D，所以遊戲需要一個介面，讓玩家與怪物在 2D 與 3D 之間切換，程式改動要少。另外：擷取畫面時不能讓視窗在桌面上閃爍；遊戲把畫面
錄到資料夾，再合併成 GIF。

**3D 戰鬥者 view 延後**（擁有者 2026-11-14）：戰鬥者維持 2D（在 3D 舞台上當 3D 物件受光，見 docs/gameplay/21）；設成 `"3d"` 的外觀仍退回 2D。

## 1. 介面：FighterView

`src/core/game/core/fighter_view.h`。戰鬥依名稱向戰鬥者的 view 要一個片段，並畫在該戰鬥者的框內；怎麼動、怎麼畫由 view 決定。

| 呼叫 | 作用 |
|---|---|
| `f_Has(clip)` | 外觀是否有那個片段 |
| `f_Play(clip, restart)` | 播放（`restart` 時從頭）；沒有該片段時回傳 false |
| `f_Stop()`、`f_Clip()`、`f_Finished()` | 停止；正在播放的片段；單次片段已播完 |
| `f_Update(dtMs)` | 推進 |
| `f_Draw(ren, box)` | 畫在框內（螢幕像素），腳在框的底部中央 |

片段名稱就是戰鬥狀態：`idle`、`charge`、`attack`、`attack_big`、`miss`、`hurt`、`die`、怪物自己的 `<狀態>_<敵人 id>`，
以及武器或外觀改名的片段。

| View | 畫什麼 | 狀態 |
|---|---|---|
| `AnimFighterView` | 2D：.anim 檔（動畫編輯器的）的角色片段，加上畫在手上的手持片段 | 已完成；玩家與所有怪物都用它 |
| glTF view | 3D：.glb 中同名的動畫，用 glTF 繪製器 | 尚未做：設為 3D 的外觀會提示一次並改用它的 2D 檔 |

遊戲以 `Game::makeFighterView(look, fit, unitsTall, boxSide)` 建立 view：
- 玩家的來自裝備武器的 `battleAnim`（`loadBattleRig`）；
- 每隻怪物的來自牠的外觀（`monsterLook`），怪物改變時（`transformInto`）重新建立。

## 2. 切換它的資料

玩家（items.json，一把武器）：

```json
"battleAnim": {"render": "2d", "character": "anim/hero_bow.anim", "held": "", "model": "", "clips": {}}
```

怪物（looks.json，一個外觀；`base` 補上外觀沒寫的部分）：

```json
"slime": {"base": "monster", "render": "2d", "character": "anim/slime_battle.anim", "model": "", "states": {}}
```

| 欄位 | 意義 |
|---|---|
| `render` | `"2d"`（預設）或 `"3d"` |
| `character`、`held` | 2D：.anim 檔（`held` 畫在角色的 `hand` 節點）。沒有寫的怪物用 `anim/monster_battle.anim` |
| `model` | 3D：.glb |

讓戰鬥者在 2D 與 3D 之間切換只需改資料；戰鬥程式不變。

## 3. 程式的改變

- `Game::battleRig_`、`battleCharacterAnim_`、`battleHeldAnim_` 與 `battleFitted_` 合併為 `playerView_`。
- 每個 `FoeRig` 的 rig 與 fitted 片段合併為它的 `view`。
- 移除 `monsterAnim_`：每隻怪物的 view 載入牠外觀的檔案（資源管理器會共用）。
- 原本向 .anim 檔問片段的地方（大攻擊、特殊招式的片段）改問 view（`f_Has`）。

手動檢查（2026-11-02，直接以遊戲執行情境）：`battle_bow`、`battle_bow_idle`、`battle_enemy_hurt`、`battle_enemy_die` 與
`battle_transform` 都通過，畫面與之前相同。

## 4. 不開視窗錄製畫面

```
toms_game.exe --scenario=tests/scenarios/battle_bow --scenario-test --fixed-dt=33 --design=1024x768 ^
              --frames=150 --record=<資料夾> --record-from=20 --record-step=2
python tools/frames_to_gif.py <資料夾> out.gif --fps 15 [--crop x,y,w,h] [--width 640] [--keep]
```

- `--record=<資料夾>` 存成 `frame_00000.png`、`frame_00001.png`……（從 `--record-from` 起每一格，或每 `--record-step` 格，
  直到 `--frames`）。
- 帶 `--screenshot` 或 `--record` 的執行畫在**隱藏的視窗**：桌面上不會閃爍。`gltf_viewer --screenshot` 也一樣。
- `tools/frames_to_gif.py` 合併畫面（之後刪除，除非 `--keep`）。用 `--fixed-dt=33` 與 `--record-step=2` 時 GIF 為 15 fps。
