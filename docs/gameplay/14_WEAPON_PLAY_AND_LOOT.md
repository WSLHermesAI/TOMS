# 14. Weapon plays v2 and the loot grind (the owner's ideas, 2026-10-30)

The owner's new gameplay ideas from 2026-10-30, put in order: a play of its own for each of the bow, the sword and the
staff (§2), and a monster-farming, treasure-hunting mode (§3). It builds on the gameplay plan
([07](07_GAMEPLAY_PLAN.md) §5 weapons, §6 growth, §8 other modes) and the battle editor ([13](13_BATTLE_EDITOR.md)).
The plays of §2.1–§2.3 are built (15 R5, reworked by 16 §10); the two-handed sword (§2.5) and the loot grind (§3) are not.
The open questions are in §5. **§6 lists every idea the owner posted and where it is.**

## 1. The owner's notes (as written)

> 弓箭：拉弓射中目標。一個方塊，模擬拉弓，等級低只能射一箭，用技能能射兩箭，絕招時靶會出現在不同地方，都射中會出會心招式。
>
> 劍：類似砍水果，一刀兩斷，等級一只能砍一刀，等級提升可以砍多刀，用絕招要指定方向砍，後面連招考記憶跟方向。
>
> 魔法杖：類似應援團，等級低只會貌1-2個圈圈，等級高要瘋狂抓時間點指定的地方跟畫魔法陣。

> 刷怪打寶 diablo：無聊開起來刷怪抽寶。打倒怪物能夠收集賺來抽寶箱。特殊怪物才會指定寶箱有特別道具。
> 一直往最深處刷怪刷寶。整裝道具的能力加成。

Added 2026-11-04:

> 雙手劍玩法：連續出現N個提示每個提示顯示X秒(預設0.3)秒方向的指示，之後會有N 乘 X秒的時間,玩家只會知道現在是第幾招(1,2,3...N)的提示不會有方向的提示,這時候玩家要自己記住原本的方向提示,成功的話就會攻擊敵人,每完成一個指令攻擊力會乘上一個倍數(可調整,預設1.2,1.4,1.6...每段加0.2倍)

Also 2026-11-04, the last line of the owner's full list:

> 雙手劍劍，連續延續出現三個每個0.3秒，一秒內要畫完

## 2. Each weapon its own play

Today the bow, the sword and the staff share one play: points on a track, tapped in time (07 §13, `combo_play.h`),
with their own settings in `weapon_play.json`. The ideas below give each weapon a play of its own. They use the
words the game already has:

| The owner's word | In the game | Where it is today |
|---|---|---|
| 等級 (level) | the weapon's level | `weapon_play.json` levels (07 §13) |
| 技能 (skill) | a skill the player unlocks and uses in battle | the skill system (`skill_system`), e.g. "Qi Drawing" |
| 絕招 (ultimate) | the super attack, used when the super charge is full | the super charge bar (5 squares) |
| 會心 (critical) | the best result, a critical move | the "perfect" result row (13 §4.2, §10) |

### 2.1 Bow: draw and hit the target

- **The play:** a box on the screen stands for the bow. The player draws it back (holds and pulls) and lets go to
  shoot at a target.
- **By level and skill:**

  | When | Arrows per shot |
  |---|---|
  | low level | one |
  | with a skill | two |
  | the ultimate | several targets appear in different places; one arrow at each |

- **Critical:** when every target of the ultimate is hit, a critical move plays (the bow's `attack_big` volley,
  13 §10, is a first version of its look).
- Compared with 07 §5.5's bow proposal (a sight that swings over the enemy): the draw stays, the swinging sight
  becomes targets to hit.

### 2.2 Sword: slice it in two

- **The play:** like fruit-slicing games: the player swipes across the enemy, and each swipe is one cut ("一刀兩斷",
  one cut, two halves).
- **By level:**

  | When | Cuts |
  |---|---|
  | Lv 1 | one cut |
  | higher levels | more cuts per attack |
  | the ultimate | the cuts must go in the directions shown |

- **Combos after the ultimate test memory and direction:** a sequence of directions is shown, then hidden, and the
  player swipes it from memory.
- Compared with 07 §5.2 (a polygon of points, no second chance): the swipe replaces the taps, and "no second chance"
  can stay (a wrong direction ends the chain).

### 2.3 Staff: circles in time, then magic circles

- **The play:** like Ouendan (應援團), as 07 §5.3 planned: circles appear and are tapped in time.
- **By level:**

  | When | What appears |
  |---|---|
  | low level | 1–2 circles |
  | high level | many circles at a fast pace, in set places and at set moments, and **magic circles to draw** |

- **Drawing a magic circle:** the player traces a shape (a circle, a star, a seal) shown on the screen. Accuracy and
  speed are the power. (07 §5.5 gave shape drawing to the war hammer; see Q30.)

### 2.4 What the three share

- One result scale for every play: miss, hit, great, perfect / critical, so `weapon_play.json` `results` (13 §4.2) and
  the art made for them (13 §10) keep working.
- Level, skill and ultimate change *how much* there is to do (arrows, cuts, circles), not the rules, so a new player
  learns each weapon once.
- Each play is its own module beside `combo_play.h` (for example `bow_play`, `slice_play`, `circle_play`), chosen by
  the weapon's `weapon_play.json` entry (`"play": "bow"`), so the battle editor ([13](13_BATTLE_EDITOR.md)) can try them.
- All three must work in landscape and portrait, by touch and by mouse ([12](12_UI_LAYOUT.md)).

### 2.5 Two-handed sword: remember the directions (the owner, 2026-11-04)

An idea, not built. Two phases:

1. **Show:** N direction prompts appear one after another, each for X seconds (default 0.3).
2. **Answer:** the player has N × X seconds. The screen shows only which move this is (1, 2, 3 ... N), with no direction, so
   the player swipes the directions from memory.

Each correct answer attacks the enemy, and each completed command multiplies the attack:

| Command | 1 | 2 | 3 | 4 | ... |
|---|---|---|---|---|---|
| Attack × (default) | 1.2 | 1.4 | 1.6 | 1.8 | +0.2 each |

The multipliers are adjustable. With the defaults, N = 3 shows the directions for 0.9 s and gives 0.9 s to answer.

The owner's shorter note says three prompts of 0.3 s each, drawn within one second. It fits N = 3 and X = 0.3, but gives 1 s
to answer and does not say the directions hide (Q63).

This differs from [17](17_WEAPONS_AND_PLAYS.md) §3, where the two-handed sword starts at V swipes, and from the
sword's combos of §2.2. The details still open are Q59–Q63 (§5 and
[../project/13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md)).

## 3. The loot grind: farm monsters, open chests

A mode to open when there is time to kill: fight monster after monster, collect what they drop, open chests for
gear, and go deeper and deeper (in the style of Diablo's loot grind).

### 3.1 The loop

```
 fight monsters ──► collect drops (chest keys / tokens) ──► open chests ──► better gear ──► go deeper
        ▲                                                                                 │
        └─────────────────────────────────────────────────────────────────────────────────┘
```

1. **Fight:** monsters come one group after another; each battle is the normal battle, with the player's weapon play.
2. **Collect:** a beaten monster drops something to collect toward chests (a currency or keys; Q32).
3. **Open chests:** what was collected opens chests, which give gear and items at random.
4. **Special monsters:** only certain monsters drop a **set chest** with a **special item** in it (a named weapon, a
   rare piece of gear). They are the reason to hunt.
5. **Deeper:** each level down has stronger monsters and better chests. The goal is to get as deep as possible.
6. **Gear:** equipping what was found gives stat bonuses (to the six stats, [11](11_BATTLE_DATA.md)); "整裝" may also
   mean bonuses for wearing a full set (Q33).

### 3.2 How it fits what exists

- It is one of the **other modes** (07 §8): part of TOMS but its own module, talking to the game only through start,
  the result, and the player's stats and gear.
- The idle side-scroller of 07 §8 can be its look: the hero walks on, monsters come, and with no command the hero
  fights on their own using their stats (Q31).
- It reuses the battle (Battle v3, the weapon plays of §2), the cards (each kill counts toward the monster's card,
  07 §3), the currencies (gold, crystals, 07 §16) and `items.json` / `equipment.json` for the gear.
- The chests' contents are data: a drop table per chest and per special monster (in `enemies.json` or a new
  `chests.json`), so the battle editor can tune them.

## 4. Proposed order

1. Answer the questions in §5.
2. **One weapon play first, the bow** (§2.1): it is the starting weapon, so every player sees it. Build it beside the
   current play and switch with `weapon_play.json` `"play"`, then try it in the battle editor.
3. The sword's slicing (§2.2), then the staff's circles and magic circles (§2.3).
4. The loot grind (§3): the loop with one level and one chest first, then special monsters, depth and set bonuses.

## 5. Open questions for the owner

| # | Question | Proposal if no answer | My Answer |
|---|---|---|---|
| Q27 | Do the new plays **replace** the shared point track for the bow, sword and staff, or are they **added** (for example the point track for normal attacks, the new play for skills and the ultimate)? | replace it, weapon by weapon, with the old play kept as a setting until the new one is tuned |  |
| Q28 | **Bow:** is the box dragged (pull back from the box and let go, like a slingshot), or held (hold, a gauge fills, let go)? Do targets move? | drag back and let go; still targets at low level, moving targets at higher levels |  |
| Q29 | **Sword:** in the memory combo, are the directions shown first and then hidden, or shown one at a time? How many at most? | shown all at once for 1.5 s, then hidden; 3 at first, up to 6 |  |
| Q30 | **Staff:** magic-circle drawing was given to the war hammer in 07 §5.5. Does it move to the staff, and does the hammer get something else? | the staff draws magic circles (round seals); the hammer keeps straight-line shapes (lines, V, zigzag) |  |
| Q31 | **Loot grind:** a mode of its own (from the title or the hub), or the tower itself going below its last floor? Can it play itself (idle, as 07 §8's side-scroller) when there is "nothing to do"? | a mode of its own from the hub, with auto-battle on/off |  |
| Q32 | What do monsters drop to open chests: a new currency (chest tokens / keys), gold, or crystals? | a new item, "chest key", so gold and crystals keep their uses |  |
| Q33 | "整裝道具的能力加成": the bonuses of each piece of gear, or **set bonuses** for wearing a full set? | both: each piece has its bonuses, and a full set adds one more |  |
| Q59 | **Two-handed sword (14 §2.5):** does this memory play replace its V swipes from doc 17, or is it only for skills and the ultimate? | it replaces them as the normal attack |  |
| Q60 | **Two-handed sword:** does each correct swipe hit at once with its own multiplier, or do all the swipes make one hit at the end with the last multiplier? | each swipe hits at once: ×1.2, then ×1.4, then ×1.6 |  |
| Q61 | **Two-handed sword:** after a wrong swipe or when time runs out, do the hits so far count, or does the whole attack fail? | the hits so far count; the chain ends there |  |
| Q62 | **Two-handed sword:** how big is N at each level, and how many directions (4 or 8)? Is N × X one shared time, or X for each swipe? | N = 3 at Lv 1, up to 6; 4 directions first; one shared time |  |
| Q63 | **Two-handed sword:** Your short note says three prompts, 0.3 s each, drawn within one second. Is it the same play as §2.5, with 1 s to answer instead of N × X (0.9 s)? Do the directions hide while answering? Are they separate swipes, or one stroke (畫完)? | the same play: N = 3 to start, the answer time in data (1 s for N = 3), directions hidden, separate swipes |  |

## 6. Every idea the owner posted, and where it is

The owner's full list of 2026-11-04, checked line by line. The ideas are in three documents, by the day they were posted:
this one, [16](16_SKILLS_AND_PLAYS.md) and [17](17_WEAPONS_AND_PLAYS.md). "I1" and so on are the issues in 17 §2.

| The owner's words | Where | State |
|---|---|---|
| 弓箭：拉弓射中目標；一個方塊模擬拉弓；等級低一箭、技能兩箭、絕招靶在不同地方，都射中出會心招式 | 14 §2.1, 15 R5 | Partly built: the pull, and Falcon Rain's targets. Two arrows from a skill: not built |
| 劍：類似砍水果，等級提升多刀，絕招指定方向，連招考記憶跟方向 | 14 §2.2, 16 §3.1 | Built, reworked by 16 |
| 魔法杖：類似應援團，等級低 1-2 個圈圈，等級高抓時間點跟畫魔法陣 | 14 §2.3, 16 §3.3 | Built, reworked by 16 |
| 刷怪打寶 diablo：刷怪抽寶、特殊怪物指定寶箱、往最深處、整裝加成 | 14 §3 | Not built (Q31–Q33) |
| 雙手劍：N 個提示各 X 秒，憑記憶回答，倍數 1.2、1.4、1.6 | 14 §2.5 | Not built (Q59–Q62) |
| 雙手劍劍，連續延續出現三個每個0.3秒，一秒內要畫完 | 14 §1, §2.5 | **Was missing; added.** Not built (Q63) |
| 劍：每個方向提示一秒，彈一下出現再消失；每個方向攻擊一次 | 16 §3.1 | Built |
| 劍：一秒沒輸入換下一個，只有 0.5 秒（可調整）可輸入 | 16 §3.1, 17 I3 | Built; the longer window (I3) not built |
| 弓：靶移動 0.5 秒後固定，瞄準越久抖動越大 | 16 §3.2 | Built |
| 弓：教學、反方向滑動、蓄力有秒數限制、STR 決定拉多久、DEX 決定震動 | 16 §3.2 | Built |
| 普通拉弓就一下，招式才連續瞄準，要有 particles | 16 §3.2, §6.2 | Built |
| 魔杖：數字順序會慢慢消失？快可以點時驚嘆號 0.5 秒 | 16 §3.3 | Built as: all hide together for 0.5 s (your later note). No slow fade |
| 魔杖：不用點才開始，時間到就數，數完消失 0.5 秒再一起出現；每次點擊放魔法 | 16 §3.3 | Built |
| 防禦中斷玩家攻擊，只有怪物頭上驚嘆號時有效 | 16 §4.2 | Built |
| 怪物攻擊 0.5、0.5 或連續；設定集氣、次數、間隔 | 16 §4.1 | Built |
| 高級怪物進入特殊防禦（畫五角星、連點）；跟怪物綁定 | 16 §4.3 | Built, on the move rather than the monster (Q68) |
| 卡片：特殊練習場指定怪物連戰升級，每天限次，消耗卡片 | 16 §5 | Built on my proposal (Q44) |
| 五角星招式：particles 照畫法、頂點爆炸、暗下倒數、1/5、招式名、10 下或特寫、N 倍（先十） | 16 §6.1 | Built (Star Seal) |
| 動畫跟攻擊沒對上，每點一下要播攻擊動畫 | 17 I1 | Not built |
| 劍的三角形跟圈圈不對要簡化；三角形失敗率高 | 17 I2, §4.2 | Not built |
| 要多個 0.幾秒 | 17 I3 | Not built (still 0.5 s) |
| 事件 scroller 不能工作（選單中的事件） | 17 I4 | Not built |
| Dex 增加判別成功率 | 17 I5, §4.4 | Not built for gestures |
| 道具或裝備顯示劍的能力、幾連招、還有多久升級 | 17 I6 | Not built |
| 商店買了武器沒出現在道具欄、不能裝備 | 17 I7 | Not built |
| 敵人會四處走動 | 17 I8, §16 Q54 | Not built |
| 怪物的平衡要調整 | 17 I9, §11 | Not built |
| 戰鬥速度可以調整 | 17 I10, §16 Q53 | Not built |
| 海洋跟 skybox、天空、火山爆發當背景 | 17 §12 | Not built |
| 絕招：劍任意滑動三秒、弓任意滑動必中兩秒、魔杖任意連點兩秒 | 17 §5, §16 Q49 | Not built |
| 劍的普通攻擊增加打勾 | 17 §5 | Not built |
| 弓：放進箭袋才能射、等級提升裝更多、沒箭要補（填彈） | 17 §7 | Not built |
| 弓：左右滑動一次射多支，提高攻擊力跟命中率；畫面提示解釋兩者 | 17 §7 | Not built |
| 怪物怕遠攻／近戰／魔法；戰鬥中切換武器；第二章武器商人、通知、切換圖示 | 17 §10, §16 Q55 | Not built |
| 魔杖集氣、畫集氣方法、忽快忽慢節奏、連點 | 17 §8 | Not built |
| 戰鬥遊玩判斷型態要列舉（eDRAW_SHAPE, eSEQUENCE_CLICK…），吃設定檔 | 17 §4.1 | Not built |
| 畫形狀：position and time、兩倍速表演、點發亮、指示線、成功失敗音效動畫、可設定允許失敗 | 17 §4.3 | Not built |
| 初始武器無前置；重劍蓄力 N 秒；雙手劍不能防禦、等待短、滑動起跳兩方向 | 17 §3, §16 Q56 | Not built |
| 劍、弓、魔杖各三種前置與攻擊操作；武器說明與戰鬥提示 | 17 §3 | Not built |
| 熟練度 5 招可在戰鬥中選；每種武器四型，各有等級 | 17 §3, §6 | Not built (Q37) |
| 武器沒有特別招式，有對應等級的特別能力（前五級等待 −5%，之後難度降低） | 17 §3 | Not built |
| 滑動難易度：8 方向、V、M、四點四方形 | 17 §4.2 | Not built |
| 抗性與弱點；石頭怪對劍 50%；怪獸說明標記、卡片也寫；小怪沒有；第二章之後才有 | 17 §10 | Not built (Q66) |
| 自動對話欄：依實力差、攻擊效果、卡片內容；開場隨機跳到某狀態 | 17 §9 | Lines drafted; the system is not built |
| 對話 100 句（等級差）、50 句（攻擊效果）、卡片、100 句閒聊與冷笑話 | 17 §9, battle_talk | Draft written: 100, 50, 30 templates, 100 |
| 畫面跟 UI 大力優化，像那張圖片，角色大、資訊 UI 不擋角色、招式 UI 可以 | 17 §12, §17 | Not built (the hero's art is under way) |
| 戰鬥搞定後解決關卡顯示，再綁定事件 | 17 §12 | Order only |
| 某些招式要特別事件才能取得；每個武器有史詩與傳說任務 | 17 §6 | Not built (Q65) |
| 數值：3-5 下、第三層、第五章、第七層 | 17 §11 | Not built (Q64) |
| 打倒第一章 boss 後 NPC 開放買武器 | 17 §10, §11 | Not built |
| 之後每層 20%-80% 有抗性；事件 boss 三種都抗 50%；切換武器跟裝備來過關 | 17 §10, §11 | Not built (Q67) |
