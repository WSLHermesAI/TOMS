> 英文原文：[20_BATTLE_EFFECTS.md](../20_BATTLE_EFFECTS.md)

# 20. 戰鬥的手感：特效與音效（擁有者的點子，2026-11-09）

擁有者對攻擊外觀與聲音的筆記，整理後**當天已做好**（Windows 桌面版）。數值在 `data/battle.json` 的 `"feelFx"`，特效在
`media/fx/battle.particle`（由 `tools/make_battle_fx.py` 產生），音效在 `media/sfx/`（由 `tools/make_battle_sfx.py` 產生）。仍待確認的是
[../../project/zh_TW/13_OPEN_DECISIONS.md](../../project/zh_TW/13_OPEN_DECISIONS.md) 第 2 節的 Q82–Q87：已照提案做，要改才需回答。

## 1. 擁有者的筆記（原文）

見英文版 §1（原文即為中文，內容相同）。

## 2. 魔杖的提示

| 階段 | 發生什麼 |
|---|---|
| 數數 | 每個圈圈依序亮起數字與特效（`wand_hint_1`..`4`）。**越後面越誇張**（等級較高，且每個再放大 8%），鈴聲（`sfx.wand_hint`）每次**高兩個半音** |
| 消失 | 全部**一口氣**消失，各有一個爆開（`wand_vanish`、`sfx.wand_vanish`） |
| 回來 | **依序、每 0.1 秒**出現一個（`circleReturnMs`），**帶著數字**，而且**已經能點**；還沒回來的不能點 |
| 點擊 | 最後一個回來後，**數字立刻消失**，剩下的憑記憶點 |

原本點擊前的「!」（16 §3.3）已關閉：`circleWarnMs` 為 0。設為大於 0 會恢復（Q85）。

## 3. 命中：連段越多噴越多；魔杖越打越強

- **每一擊都會在怪物身上噴粒子**（`combo_spray_1`..`5`）：連段每 2 下升一級（`sprayComboStep`），連段越長畫得越大。魔杖的高一級。
- **魔杖的魔法沿著連段變強**（Q84）：第 k 個魔法佔整個玩法傷害的 k^1.6（`damageRamp`）。五個圈圈時，第一個約 3%，最後一個約 41%。
  總傷害和以前一樣。每個魔法的音效（`sfx.wand_cast`）也一次比一次高。

## 4. 劍與弓

- **劍：** 每一刀沿著滑動方向留下刀光：會放大縮小的長條，後面跟著兩道殘影，還有火花（`sword_slash`、`sfx.sword_slash`，連段越多音越高）。
- **弓：** 拉弓時每 90 毫秒有粒子**聚集**到箭上，蓄力越滿越多越快（`bow_gather_1`..`3`），伴隨越來越高的嗡聲（`sfx.bow_charge`）。
  箭命中處**爆炸**（`bow_explode`、`sfx.explosion`），拉得越滿越大。

## 5. 怪物的攻擊

現在每次攻擊都有特效，大多也有音效，加在怪物原本的動畫與外觀音效（`looks.json`）之上。依種類（`"feelFx"` `"foeAttack"`）：

| 種類 | 何時 | 特效 |
|---|---|---|
| strike | 一般怪物的普通攻擊 | 主角身上的爪痕（`monster_claw`） |
| big | **大型**怪物的任何攻擊（Q82） | 怪物蓄力，**射出光彈**飛向主角（`monster_bolt`），落點爆炸，發射聲與爆炸聲 |
| spell | 法術 | 施法光芒，**魔法球飛出**，紫色爆炸 |
| special | 重擊，或帶有特殊防禦玩法的攻擊（16 §4.3） | 大型重擊：光環、斬痕、火花、星星 |
| bigSpecial | 大型怪物的同一種 | 更大的光彈，然後重擊 |

- **大型：** boss，或 `enemies.json` 中 `"big": true`。目前只有石巨人有。
- 飛行時間 0.18 秒（`flightMs`），落地時剛好出現傷害數字（`hit_number_delay_ms`）。主角站在危險格以外（19 §2.1）時不會被打中：光彈飛過去後淡出。
- 命中點在主角的胸口。

## 6. 攻擊目標

目標怪物下方的黃色底線已移除。有兩到三隻怪物時，目標名字下方顯示 **目前攻擊目標**（`text.json` 的 `battle.target_now`，Q83）。

## 7. 網頁版的「md5」

遊戲沒有 md5 檢查。避免玩家拿到舊檔案配新網頁的，是每個檔名中的**版本戳記**（docs/project/07 §3）：2026-11-09 檢查線上網站，載入的是
`toms_game.fbb5cad-20261107132554.js`（以及它的 `.wasm`、`.data` 與多執行緒的 `toms_game_mt.*`），`version.json` 是第 4 版。GitHub Pages
也會送出 `ETag`（每個檔案的雜湊）與 10 分鐘的快取時間。把檔名中的 commit 與時間換成雜湊，實際上沒有差別。

## 8. 已完成與檢查項目

| 部分 | 位置 |
|---|---|
| 指定音高與音量的音效 | `Audio::f_Play(key, pitch, volume)`（`src/core/game/audio`） |
| 圈圈依序回來；每個回答的順序與滑動角度 | `touch_play.*`（`f_CirclesShown`、`f_CircleNumbersShown`、`Hit::m_iOrder`、`m_fAngle`） |
| 提示、噴發、刀光、弓、魔杖傷害遞增 | `game_skills.cpp`（`f_WandCues`、`f_HitSpray`、`f_BowGather`、`f_PlayHit`） |
| 怪物攻擊、飛行物 | `game_combat.cpp`（`f_FoeAttackFx`、`f_LaunchBattleFx`、`f_UpdateBattleFx`） |
| 目標文字 | `battle.rml`、`battle.rcss`、`UiBattle::m_strTargetLabel` |
| 25 個特效、9 個音效 | `make_battle_fx.py`（`feel_effects`）、`make_battle_sfx.py`（合成的暫用音效，Q86） |

以隱藏視窗錄製戰鬥檢查：魔杖的提示與回來、三隻怪物時的目標文字、石巨人的光彈，以及每個新特效在主角身上播放（情境的 `"rules"` 可以設定
`"feelFx"`）。**沒有在真正戰鬥中看到：** 劍的刀光、噴發、弓的聚集與爆炸，因為沒有測試情境會讓玩家命中（觸控玩法還沒有鍵盤操作）；只看了特效本身。
**沒有聽到：** 依專案規則，所有測試執行都是靜音，所以音效只確認能解碼。

`unit.touch_play_test` 已為新的圈圈更新，但它被關閉，所以只有編譯，沒有執行。
