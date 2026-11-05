# 17. Weapon families, play types, resistances and battle talk (the owner's ideas, 2026-10-31 night)

The owner's notes after playing the battle of [15](15_BATTLE_REVAMP.md) and [16](16_SKILLS_AND_PLAYS.md): issues found,
and a larger plan. In short:
- weapons come in **families** with their own levels and abilities;
- every way of playing an attack is one **play type** from a list, set in data;
- the bow gets **ammo**, the staff a **charge**;
- monsters get **resistances and weaknesses**, and the player **switches weapons** in battle;
- a **battle talk bar** comments on the fight;
- a bigger **battle screen**, **backgrounds**, and a **difficulty curve**.

It changes some of 16's answers (§13). Built so far: the languages (§16), and W1, W2 and W3 (§19). The owner answered Q47–Q56 on 2026-11-02 (§16); new questions are in §18 (Q64–Q67 added 2026-11-04).

## 1. The owner's notes (as written)

> 動畫跟攻擊沒對上、每點一下要播一個攻擊的動畫。劍的三角形跟圈圈都不對，要簡化。要多個0.幾秒不然有點難。事件scroller不能工作（選單中的事件）。
> 圈圈沒很難用出來三角形失敗率超高、Dex可以增加判別成功率。道具或是裝備要顯示劍的能力、能幾連招、還有多久升級。
> 商店買了武器沒出現在道具欄跟不能裝備。敵人會四處走動。怪物的平衡要調整。戰鬥速度可以調整。
> 做個海洋跟skybox當背景、天空當背景、火山爆發當背景。
>
> 劍的絕招，任意方向滑動都算成功沒有等待時間，持續三秒。弓箭絕招，任意地方滑動必定命中沒有等待時間兩秒。魔杖絕招，任意地方連點都算成功沒有等待時間兩秒。
> 劍的普通攻擊增加打勾，兩個方向的連續滑動。
>
> 弓箭：需要拿弓箭放到箭袋才能射擊（點擊弓箭圖案），根據等級提升一次裝備的弓箭數量也不同，沒有弓箭要補弓箭，跟fps射擊遊戲一樣，沒有子彈要填彈才能射擊。
> 然後可以一次裝好幾個箭射出，要做一個左右滑動增加弓箭數量，弓箭等級提升才能裝更多的弓箭一次射出。一個是上子彈的概念，一個是一次射出好幾發子彈提高攻擊力跟命中率的概念，
> 這兩個有點難解釋，需要做出畫面上的提示讓玩家知道。
>
> 某些怪物怕遠攻某些怕近戰，有的怕魔法，所以玩家戰鬥中可以切換武器，要達到第二章後遇到武器商人才能買入，這時候跳個通知讓玩家知道戰鬥中可以切換武器，戰鬥畫面新增可切換的圖示。
>
> 魔杖普通需要做一個集氣才能攻擊的點擊，集氣越久攻擊力越強，但是集氣時間跟技能有關，而且集氣可以點招式要畫出指定集氣方法（類似畫五芒星的操作），不同的集氣會引起點擊時有不同的忽快忽慢節奏，甚至是連點好幾下的操作。
>
> 這邊設定武器是可以買，不同武器有不同的特別招式，吃設定檔。然後玩家的武器熟練度能使用不同武器都可以用的招式，所以寫程式的時候要做一個enum(列舉）eDRAW_SHAPE, eSEQUENCE_CLICK....
> 所有的戰鬥遊玩判斷型態要列舉，並且手滑動指定形狀，滑動分向，連點幾下等類型都可以直接吃指定的設定檔以及對應的動畫或是參數，例如畫形狀會吃position and time，anim editor 中的動畫格式，
> 畫面會先表演一次兩倍速的動畫，然後會出現第一個點發亮提示並且出現一條線指示玩家要怎麼滑動到第二個點，依此類推依照檔案的設定來畫出指定圖案，並且會播放每個點成功或失敗音效及動畫，有的招式可允許失敗有的不允許（可設定）。
> 目前想到的有不同武器的普通攻擊需要不同的操作方式來攻擊。例如一開始的劍，弓箭，魔杖不需要特別的前置動作，攻擊時也沒有特別的指令，依照目前玩法即可。
> 但是換武器後：比較重的劍需要蓄力N秒才能攻擊，蓄力時要有動畫或是特效；雙手劍不能防禦，等待時間較短，但是滑動起跳是兩個方向，適合想快攻的人。
> 劍跟弓箭跟魔杖用上面給的例子麻煩在各產生三種前置以及攻擊時稍微不同的特殊操作，並且在武器說明會寫出遊玩方法，戰鬥場景時也有明顯的提示讓玩家知道怎麼操作。
>
> 每個武器熟練度方面基本都有5招可在戰鬥場景中選擇使用，然後每種武器各有四種不同型態的類型，例如劍的話有長劍，雙手劍，重劍，短劍，然後每種劍有自己的等級，這邊更正一下前面講到的武器特別招式，
> 武器沒有特別招式但是有特別的能力對應劍的等級，例如雙手劍等級提升會降低等待時間以及滑動方向的難易度會降低，可能前面五級都是降低等待時間5%，後面是降低滑動方向的難易度，例如本來是一個打勾兩個方向但是會直接變成一個打勾就好，
> 這邊要先定義好滑動方向的資料有哪些，例如8個方向的滑動是難易度一，任意方向打勾（V)的滑動是難易度二，任意方向M字型滑動是等級三，四點中任意點啟動的四方型滑動等級四。
> 請以此架構撰寫魔杖跟弓的遊玩方法，主要用意是讓玩家除了基本武器招式可以用並且要搭配武器的特殊能力來增加遊戲遊玩的思考，有的怪物不怕魔法有的不怕物理，有的要用弓箭，怪獸的說明可以標記，
> 例如石頭怪不怕物理攻擊，這時候對劍的抗性是50%所以玩家要自己看打下去不痛要自己換武器，打倒怪物後，卡片也會寫，所以這邊怪物要多一個抗性跟弱點特性，普通小怪都是沒抗性跟弱點，第二章可以買武器之後才會有弱點跟抗性。
>
> 戰鬥時要多一個自動更新訊息的自動對話欄，會根據兩方的實力差別說對話，例如遇到等級差很多的會說這怪好弱，好無聊的爛怪。如果有卡片時會說對方的抗性跟弱點，遇到太強的怪會說，我應該打不贏，要跑嗎？或是感覺會死亡要拼命嗎？
> 這邊根據兩方等級差別對話請產生100句對話內容，根據攻擊之後的效果產生50句對話，例如對方有抗性打了不痛會說，是不是要換武器或招式。根據卡片內容會隨機說出對方的能力或抗性，要怎麼打才會痛。
> 然後另外產生一百句無關緊要的對話，說天氣不錯，還有各式冷笑話。所以要有一個戰鬥即時自動對話更新系統，遊戲一開始會隨機跳到某種狀態依據情況分析給出對應的對話。
>
> 畫面跟UI的部分要在大力優化，目前的顯示太簡陋。要像這張圖片（Image），角色大一點，資訊UI不要擋到角色，但是招式UI可以。戰鬥搞定後再來解決關卡的顯示，然後再綁定事件進去。
> 某些招式要特別事件才能取得。每個武器都有史詩任務跟傳說任務取得新招式。
>
> 數值部分：一開始的怪打個3-5下就掛，到第三層之後怪物的血量會提升並且攻速變快一點點，可以每一回合一定打一下玩家的程度，第五章防禦提升血量變更多攻擊力也提升，玩家不能無腦攻擊一定要防禦，
> 但是防禦攻擊就會中斷，這時候玩家應該可以用到五連擊，玩家要自己判斷要以血換血或是防禦不痛打倒敵人。第七層開始有三種怪物的抗性要出現對應各種武器的抗性/弱點大概有50%，所以有一些怪可能玩家現在不想打，
> 打倒第一章的boss之後NPC對話開放購買武器。怪物抗性之後每層怪都約有20%-80%有（根據怪物設定可能只怕一種武器兩種不怕，或是三種都不並且抗性都50%的事件boss)，玩家要自己切換武器跟裝備來過關。

## 2. Issues found, checked against the code

| # | Issue | What the code does (2026-11-02) | Fix |
|---|---|---|---|
| I1 | **The animation does not match the attack**: each tap should play an attack | A sword cut or a staff spell hits at once (`playHit`, game_skills.cpp) but plays no clip; only the play's end (`resolveCombo`) plays `attack` | `playHit` plays a short attack clip per answer (restarted each time); the end keeps the big clip |
| I2 | **The sword's triangle and circle do not work**, too hard | The free-drawn shape check (`matchStroke`, gesture.cpp) wants every corner within 20 % of the stroke's box, in order | Replaced by the swipe tiers of §4.2 (8 ways, V, M, square); shapes stay for moves only |
| I3 | **The answer window is too short** | 0.5 s of each 1 s prompt (battle.json `plays.inputMs`) | + a few tenths: 0.75 s to start (data); DEX adds more (I5) |
| I4 | **The Events tab's list does not scroll** (player menu) | `#events` has `overflow-y: auto` (player.rcss:128); not reproduced yet | Check: the mouse wheel over it, a finger drag (RmlUi does not drag-scroll by touch), and the keyboard selection not scrolling into view |
| I5 | **DEX should make gestures easier to read** | Gestures have fixed tolerances | DEX widens the tolerance (a way's angle, a shape's corner radius) and the window a little (§4.4) |
| I6 | **Items and gear should show the weapon's ability**: how many hits in a combo, the time to the next level | The Equipment tab shows stats only | Its detail adds: the play type and how to play, points / combo length at this level, wins to the next level, the level's ability (§3) |
| I7 | **A weapon bought in the store does not appear and cannot be equipped** | Buying gear owns and wears it at once (`buyStoreItem` → `ownGear(id, true)`), and it is listed in the **Equipment** tab, not the Items tab. **A bought talent never appears**: `gearCells` lists weapons or armor only (game_player_menu.cpp:85) | List talents in the Equipment tab; after buying, a notice "worn; see Equipment"; check the owner's exact steps |
| I8 | **Monsters should walk around** | Only 4-tile roamers move (`roamer.h`) | A `roam` setting per monster: it steps when the player steps (§14) |
| I9 | **Monster balance** | — | The curve of §11 |
| I10 | **Battle speed should be adjustable** | — | A setting: 0.75× / 1× / 1.25× / 1.5× (the battle's own time, `battleFrame`) |

## 3. Weapon families, types and levels

**Three classes, four types each.** Each type is its own item and has its own level (wins with it), as weapons do today.
A weapon type has **no special moves**. It has **abilities** that grow with its level. The moves come from the class's
**mastery** (§6).

| Class | Type | Before the attack (prelude) | During the attack | Guard | Level abilities (examples) |
|---|---|---|---|---|---|
| **Sword** | Long sword (the start) | none | 8-way swipes (tier 1), as today | yes | Lv 1–5: +1 prompt every 2 levels; then the window +5 % a level |
| | Two-handed sword | none | starts at **two-way** swipes (V, tier 2); a short wait | **no guard** | Lv 1–5: wait −5 % each; Lv 6+: gestures one tier easier (V becomes one way) |
| | Heavy sword | **charge N s** (an animation and an effect while charging) | few prompts, each hits hard | yes | charge time −5 % a level; at Lv 5 a full charge breaks a guard |
| | Short sword | none | fast prompts, a short window, many hits | yes (shorter) | +1 prompt a level; at Lv 5 a perfect chain does not reset on one miss |
| **Bow** | Hunting bow (the start) | none | drag back and let go, as today | yes | aim shake −10 % a level |
| | Long bow | **load the quiver** (tap the arrow icon) | a strong pull; the target moves longer | yes | quiver +1 a level; full-pull damage +5 % a level |
| | Crossbow | **load bolts one by one** (tap per bolt, like reloading a gun) | no pull: tap the moving target in time | yes | reload time −8 % a level; at Lv 5 a bolt pierces a guard |
| | Twin bow | **set the volley**: swipe left / right for the arrows per shot | several arrows at once; more arrows, more damage and a wider hit area | yes | volley size +1 every 2 levels |
| **Staff** | Wand (the start) | none | count, hide, tap in order, as today | yes | +1 circle every 2 levels |
| | Rod | **charge by holding**; longer is stronger | a steady beat of taps | yes | charge speed +5 % a level |
| | Orb | **draw the charge sign** (a pentagram or another shape, §4) | the sign sets the rhythm: fast, slow, or bursts of taps | yes | the sign's tier eases; more beats |
| | Tome | **pick a page** (a sign) before the fight's first attack | sequence taps: the shown order, longer chains | yes | the shown time +10 % a level |

- The start weapons (long sword, hunting bow, wand) need no prelude and play as now.
- Each weapon's description says how to play it. The battle screen shows a clear prompt for the prelude and the attack
  (an icon and one line: "Hold to charge", "Tap the arrows to load", "Swipe ↔ to set the volley").
- The two-handed sword also has a memory play, an idea of 2026-11-04 ([14](14_WEAPON_PLAY_AND_LOOT.md) §2.5). Q59 decides
  whether it replaces the V swipes above.

## 4. Every play is a type, set in data

### 4.1 The play types (an enum in code, names in data)

| Type (code) | Data name | What the player does | Used by |
|---|---|---|---|
| `eSWIPE_WAY` | `swipeWay` | swipe one of 8 ways | the sword |
| `eSWIPE_GESTURE` | `swipeGesture` | a gesture from §4.2 (V, M, square) | the two-handed sword; moves |
| `eDRAW_SHAPE` | `drawShape` | trace a shown shape, point by point (§4.3) | moves; the orb's charge sign |
| `eSEQUENCE_CLICK` | `sequenceClick` | tap the shown spots in order | the wand; the tome |
| `eRHYTHM_TAP` | `rhythmTap` | tap on a beat (a pattern of fast and slow) | the rod; the orb after its sign |
| `eHOLD_CHARGE` | `holdCharge` | hold; release when charged | the heavy sword; the rod |
| `ePULL_RELEASE` | `pullRelease` | drag back and let go at a target | the bows |
| `eTAP_TARGET` | `tapTarget` | tap a moving target in time | the crossbow |
| `eRELOAD` | `reload` | tap the ammo icon to load | the long bow, the crossbow |
| `eVOLLEY_SET` | `volleySet` | swipe left / right to set a count | the twin bow |
| `eMASH` | `mash` | tap as fast as possible | special guards |
| `eFREE_SWIPE` | `freeSwipe` | any swipe anywhere counts, for a time | the sword's ultimate |
| `eFREE_TAP` | `freeTap` | any tap anywhere counts, for a time | the staff's ultimate |
| `eFREE_SHOT` | `freeShot` | any swipe shoots and always hits, for a time | the bow's ultimate |

Today's code already has most of these as parts: `touch_play.h` (Bow, Slice, Circles, Shape, Mash), `gesture.h` and
`cue_track.h`. The change is to give each its own type, read every number from data, and add the new ones (gesture
tiers, rhythm, hold, reload, volley, the free modes).

### 4.2 Swipe tiers (how hard a gesture is)

| Tier | Gesture | Note |
|---|---|---|
| 1 | one of **8 ways** | today's sword |
| 2 | **V** (a tick), any direction | two ways in one stroke; the two-handed sword starts here |
| 3 | **M**, any direction | four ways in one stroke |
| 4 | a **square**, starting at any of its 4 corners | four sides |

A weapon's level can move its prompts down a tier (§3). DEX widens every tier's tolerance.

### 4.3 Drawing a shape, from data

A shape move reads:
- **its points:** position and time each;
- **a demo clip:** in the anim editor's `.anim` format;
- **per-point settings:** success and fail sounds and effects;
- **`allowFail`:** whether a missed point ends the move.

The play:
1. The demo plays at **2× speed**.
2. The **first point glows**, and a **line** shows the way to the second.
3. Each point reached plays its success sound and effect; a miss plays its fail ones.
4. Then the next point and its line, until the shape is done.

### 4.4 DEX

DEX adds tolerance to every play (a sketch, all in data):
- a swipe's angle +1.5° a point;
- a shape's corner radius +1.5 %;
- the answer window +10 ms a point.

## 5. The ultimates (the owner's new ones)

| Weapon | Ultimate | Time |
|---|---|---|
| Sword | **any swipe anywhere counts**, no waiting | 3 s |
| Bow | **any swipe anywhere shoots and always hits**, no waiting | 2 s |
| Staff | **any tap anywhere counts**, no waiting | 2 s |

- Each counted swipe or tap is a hit with its own attack clip (I1).
- The sword's **normal** attack also gains the **tick**: a two-way swipe in one stroke (tier 2), as a prompt kind.

## 6. Mastery and moves

- Each weapon class has a **mastery** level, raised by fighting with any weapon of the class.
- Each mastery has **5 moves** that every weapon of that class can use, chosen in the battle screen.
- The moves are `special_moves.json` entries (16 §6), each with its play type (§4).
- Some moves come from **special events**.
- Each class has an **epic quest** and a **legendary quest** that teach a new move (the note says each weapon: Q65).

## 7. The bow's ammo and volley

**The quiver (loading):**
- Arrows must be loaded before the bow shoots: tap the **arrow icon** to load the quiver.
- The quiver's size grows with the bow's level.
- An empty quiver must be reloaded before the next shot, like reloading a gun in a shooter.

**The volley (shots at once):**
- Swipe **left / right** to set how many arrows one shot fires.
- More arrows mean more damage and a wider hit area.
- The most arrows per shot grows with the bow's level.

**Telling them apart on screen** (the owner: hard to explain):
- the quiver shows as **arrows in a holder**, with a "Tap to load" pulse when empty;
- the volley shows as a **fan of arrows** over the bow, with "↔ 3" when set;
- a first-time tutorial shows each once, with a ghost finger (as the bow's pull tutorial does).

## 8. The staff's charge

- The staff's normal attack needs a **charge**. Charging longer makes it stronger, and the charge time depends on skills.
- While charging, the player may **draw a charge sign** (a shape, like the pentagram). The sign sets the attack's
  rhythm: fast, slow, uneven, or bursts of several taps.
- Signs are data: each one is a `drawShape` and the `rhythmTap` pattern it gives.

## 9. The battle talk bar

**What it is:**
- A line on the battle screen that updates by itself. It's the hero thinking out loud.

**When it talks:**
- **At the start:** it jumps at random to one of the states that fit the fight (the level gap, a card hint, idle talk).
  The level gap has five bands, from much weaker to much stronger.
- **After moments:** resisted, weak spot hit, perfect, miss, the player's own HP low, the monster's HP low.
- **With the monster's card:** what the monster resists, what hurts it, how to fight it.
- **Idle talk** (weather, cold jokes) when nothing else is due.
- It doesn't repeat a line soon, and it waits at least a few seconds between lines.

**The lines** (draft: [battle_talk/battle_talk_lines.json](battle_talk/battle_talk_lines.json), zh_TW and English):

| Group | Count |
|---|---|
| level gap | 100 (20 per band) |
| after a hit | 50 |
| card hints | 30 templates with `{name}`, `{type}`, `{pct}` |
| idle | 100, half of them cold jokes |

## 10. Resistances, weaknesses and switching weapons

**Damage types:**
- Each monster gets a resistance or weakness per damage type: melee (sword), ranged (bow), magic (staff).
- For example, a stone monster resists the sword by 50 %.

**What the player sees:**
- The monster's description marks its resistances and weaknesses once its **card** is held, and the card shows them.
- Without the card, the player learns from the damage numbers and the talk bar (Q66).

**Switching weapons in battle:**
- The battle screen shows the owned weapons as **switch icons**.
- Weapon switching unlocks when the **weapon merchant** opens: an NPC opens weapon buying after the chapter 1 boss, and
  the merchant is met in chapter 2. A **notice** says that weapons can now be switched in battle.

**Who has them:**
- Normal small monsters have none.
- From floor 7: three monster kinds, about 50 % per type.
- Later floors: 20–80 % of the monsters, by their data. A monster may fear one type and resist the other two.
- Event bosses may resist all three by 50 %.
- So the player may leave some monsters for later, and gets through by switching weapons and gear (Q67).

## 11. The difficulty curve

| Stage | Target |
|---|---|
| Floors 1–2 | a monster falls in **3–5 hits** |
| From floor 3 | more HP and a slightly faster attack: about **one monster hit per player attack** |
| Chapter 5 | more DEF, HP and ATK: the player **must guard**; guarding stops their attack; they should have **5-hit combos** by then and choose between trading HP and guarding |
| From floor 7 | three monster kinds with 50 % resistances (§10) |
| After the chapter 1 boss | weapon buying opens |
| Later | 20–80 % of monsters with resistances or weaknesses; event bosses resisting everything by 50 % |

The owner's words mix floors and chapters: Q64 asks how to read "Chapter 5" in the bands of Q47.

The battle editor's growth calculator (13 §4.3) and its test setups are the tools to tune this.

## 12. The screen, backgrounds and order

**Battle screen:**
- Bigger fighters.
- The information UI (HP, intents) must not cover the fighters; the moves' UI may.
- The reference picture the owner mentioned was not attached (Q48).

**Backgrounds:**
- an ocean with a skybox;
- a sky;
- a volcano erupting.

  They can use the glTF renderer and the HDRI environment the title scene already has, or painted layers with particles
  (the volcano).

  **The ocean and skybox are being tried in an app of their own** (the owner, 2026-11-04):
  [../engine/21_OCEAN_VIEWER.md](../engine/21_OCEAN_VIEWER.md). They are on the title screen (in turns with the city), not yet in the battle (Q69).

**Order** (the owner): the battle first, then the stage display, then the events bound into it.

## 13. What this changes in 15 and 16

| Before | Now |
|---|---|
| 16 §3.1: sword prompts may be a triangle or a circle | prompts are swipe tiers (§4.2); shapes are for moves |
| 16 §6: the ultimate is Star Seal / Tiger Wind / Falcon Rain | the ultimate is the free mode of §5; the shaped moves become mastery moves (§6, Q49) |
| 16 §3.2: the bow's normal attack is one arrow | the hunting bow stays so; the other bows add ammo and volleys (§7) |
| 16 §3.3: the staff starts by itself | the wand stays so; the rod, orb and tome add a charge first (§8) |
| `touch_play.h` play kinds | the play type enum of §4.1, every number in data |
| `weapon_play.json` per weapon | per weapon **type**, with its class, prelude, play type and level abilities |

## 14. Proposed order

| Step | What |
|---|---|
| W1 | Fixes I1–I7 and I10 (small) |
| W2 | The play type enum and data (§4); swipe tiers; DEX |
| W3 | The new ultimates (§5); the sword's tick |
| W4 | Weapon types and level abilities (§3), with their item text (I6) |
| W5 | Resistances, weapon switching, the merchant unlock (§10) |
| W6 | The talk bar (§9) |
| W7 | Bow ammo and volleys (§7); staff charges and signs (§8) |
| W8 | Mastery and its 5 moves; epic and legendary quests (§6) |
| W9 | The battle screen redesign and backgrounds (§12) |
| W10 | The difficulty curve (§11), then roaming monsters (I8) |

## 15. Questions for the owner

Answered on 2026-11-02: see §16.

| # | Question | Proposal if no answer |
|---|---|---|
| Q47 | **Floors or chapters?** The notes mix 層 (floor) and 章 (chapter): "floor 3", "chapter 5", "floor 7" | 層 = floor (F03, F07), 章 = chapter (act) |
| Q48 | **The reference picture** for the battle screen was not attached. Can you send it again? | — |
| Q49 | Do the new **free-mode ultimates** replace Star Seal / Tiger Wind / Falcon Rain? | Yes; those become mastery moves |
| Q50 | The **bow and staff types**: long bow / crossbow / twin bow, and rod / orb / tome? | as in §3 |
| Q51 | The talk lines' **languages**: zh_TW and English now, the other four later? | yes |
| Q52 | **Damage types**: three (melee, ranged, magic, one per weapon class)? | yes |
| Q53 | **Battle speed** steps: 0.75×, 1×, 1.25×, 1.5×? | yes, in the settings |
| Q54 | **Roaming monsters**: all of them, or some marked in data? Do they move when the player moves, or in real time? | some, marked `roam`; they step when the player steps |
| Q55 | **Switching weapons in battle**: does it cost time (the attack cooldown restarts)? | yes: the cooldown restarts |
| Q56 | The two-handed sword **cannot guard**: does it take full damage, or can it still parry? | full damage, no parry |

## 16. The owner's answers (2026-11-02)

| # | Answer | What it means |
|---|---|---|
| Q47 | Chapters follow the events, but the events are not finished: **divide the 70 floors into 14 bands of 5**; every 5 floors the monsters get stronger | §11 is read by floor band (F01–F05, F06–F10, … F66–F70), 14 steps, in data |
| Q48 | Compare the picture with the current battle screen | §17 |
| Q49 | **Make it data I can bind and switch any time** | each weapon names its ultimate by id in data (`"ultimate": "<move id>"`); a free mode (§5) and a shaped move (Star Seal…) are both moves, so either can be bound |
| Q50 | Yes | the bow and staff types of §3 |
| Q51 | **Traditional Chinese is the default; only Traditional Chinese and English for now; the other languages play in English** | **built** (below) |
| Q52 | Yes, three now; a 4th or more may come | damage types are a list in data, not fixed in code |
| Q53 | Yes, adjustable | a battle speed setting |
| Q54 | Monsters get **walking data**: walk toward the player (bool), how far one walk goes (int), how long between walks, the range from their birth point. Later: monsters talking to or fighting each other as small animations for fun | `"roam": { "toPlayer": false, "step": 2, "everyMs": 1500, "range": 4 }` in enemies.json; the fun part later |
| Q55 | Yes: **switching takes 0.5–1.5 s**, by weapon type and level | a `switchMs` per weapon type and level, in data |
| Q56 | The two-handed sword **cannot guard at all** for now | no guard, no parry |

**Built: the languages (Q51).**
- `assets/data/text.json` has `"default": "zh_TW"` and `"offered": ["zh_TW", "en"]`. The menus show only the offered ones; the
  other four keep their strings for later.
- A new player starts in Traditional Chinese. A settings file from before (version 2) moves to Traditional Chinese once.
- A saved language that is not offered plays in English.
- `TOMS_LANGUAGE=en` picks a language without saving it. The test runners set it, so the tests and their reference pictures
  stay English.

## 17. The reference picture against today's battle screen (Q48)

| Part | The reference | Today | To do |
|---|---|---|---|
| Background | a dungeon: stone walls, torches, a floor in perspective | flat black | a backdrop per area (§12): a painted picture, or 3D |
| Fighters | painted, large: the archer about 70 % of the screen's height, the monster as big | 32-pixel sprites scaled into 134-pixel boxes | painted HD characters on rigs (like the brawler showcase, doc engine/18) and much bigger boxes |
| Title | 戰鬥！苔蘚球, top left, with an ornament line | plain text | a styled title with an ornament |
| HP bars | framed, gem-like bars; the name and HP under them | flat bars | framed bars (9-slice art); the label under |
| Monster timer | a framed panel 攻擊 2.4s above the monster | a thin bar with small text | a framed panel |
| Bow target | an ornate reticle | a plain circle | reticle art |
| Ultimate charge | crystal orbs under each fighter; **the monster has 必殺充能 too** | grey dots, the player only | crystal art; a monster gauge (Q57) |
| Buttons | big framed panels: blue 防禦 with its hint, a glowing gold 攻擊中 | flat rectangles | framed art panels with a glow per state |
| Layout | the fighters fill the screen; the buttons overlap their legs | small fighters, empty space | the fighters fill the space; info UI clear of their bodies (the HP bar in the picture crosses the archer's arm: keep it clear) |

## 18. New questions

| # | Question | Proposal if no answer |
|---|---|---|
| Q57 | The picture gives the **monster an ultimate charge** too (必殺充能 under it). Do monsters get one? What fills it, and what does it unleash? | yes: it fills as the monster lands hits; full, it uses its special move (a special guard play, 16 §4.3) |
| Q58 | **The art**: painted with AI the way the brawler was (ComfyUI), for the player and the monsters? | yes, the player first, then the monsters of floors 1–10 |

**Answered 2026-11-02.** Q57: only a monster whose data has an `ultimateCharge` block gets the gauge (it fills as the monster lands hits; full, it uses its special move); the others have none. Q58: yes, the player first (concept pictures for the owner to pick, then cut into parts on the brawler's skeleton so its rig and battle clips are reused), then the monsters of floors 1–10.

**New, 2026-11-04.** Comparing the owner's full list with this document found four places where it says something else. Answer them in
[../project/13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md).

| # | Question | Proposal if no answer |
|---|---|---|
| Q64 | **The difficulty curve:** the notes go 第三層, 第五章, 第七層. With 14 bands of 5 floors (Q47), 第五章 would be F21–F25, after floor 7. Is it chapter 5, or floor 5? | chapter 5, F21–F25: 5-hit combos need that many levels; 第三層 and 第七層 stay floors |
| Q65 | **Epic and legendary quests:** the note says each weapon (每個武器); §6 wrote each class. That is 6 quests or 24 | each class, because the moves come from the class's mastery |
| Q66 | **Seeing resistances:** the note says the monster's description can mark them, and the card writes them after it is beaten; §10 wrote only once the card is held. Does the description show them before that? | no: until then the player learns from the damage (打下去不痛) |
| Q67 | **Gear against resistances:** the note says the player switches weapons and gear (武器跟裝備); §10 has only weapons. What does gear do here, and can it change in battle? | gear changes between fights only; a piece can carry a bonus against a damage type |

## 19. Built: W1, W2 and W3 (2026-11-05)

**W1, the fixes:**

| # | What the game does now |
|---|---|
| I1 | Every answered cut, spell or move step plays the attack clip, started again each time |
| I2 | The sword's triangle and circle prompts are gone; its prompts are swipe tiers (below). A shape prompt stays possible in data |
| I3 | The answer window is 0.75 s (`battle.json` `plays.inputMs`) |
| I4 | **The wheel was never broken; a drag did nothing.** Every scrolling list now follows a mouse or finger drag, and letting go after a drag selects no row |
| I6 | A weapon's detail shows its level, combo, wins to the next level and how to play it. The level's abilities come with W4 |
| I7 | Talents have their own slot in the Equipment tab. Buying gear says it is worn and where to change it. **The weapon half was not reproduced:** the code lists a bought weapon in both tabs, so the owner's exact steps are still needed |
| I10 | Settings > Battle speed cycles through `battle.json` `battleSpeeds` and is saved. A scenario runs at ×1 unless its battle sets `speed` |

**I1, added the same day (the owner: higher combo counts should look fancier):** the clip now grows with the hits in a row (`weapon_play.json` `comboTiers`, for the sword, bow and staff):

| Hits in a row | Clip | Effect | Feel |
|---|---|---|---|
| 1 | `attack` | `<weapon>_hit_1` | hit |
| 2 | `attack_2`: a lunge | `<weapon>_hit_2` | hit |
| 3 | `attack_3`: a hop, two strikes, a landing | `<weapon>_hit_3` | great |
| 4 | `attack_4`: three rising strikes, a heavy landing | `<weapon>_great` | great |
| 5 or more | `attack_big`: the leap | `<weapon>_perfect` | perfect |

- From the second hit, a counter shows "N Combo!". A miss starts the count again.
- Each attack starts it again, except the bow (`comboKeep`): one arrow is one attack, so its count goes on until a miss. An ultimate builds its own count.
- These effects replace the level-based hit effects for answered hits.
- The three new clips are made by `tools/art/make_hero_rig2d.py`, from the painted poses already there, moved by bones; no new painting.

**The end of a fight (the owner, the same day):** the character now plays an end animation, set in `battle.json` `feel`:

| End | When | Clip |
|---|---|---|
| Big win | no damage taken | `win_big` |
| Win | anything between | `win` |
| Par win | damage taken, and `winParHp` (25 %) of the HP or less | `win_par` |
| Lose | HP gone | `lose`: the hero falls and stays down |

- A win: the last hit's slow motion, the last attack clip plays out, the win clip, then the result. A loss starts the fall at once, and the monsters stop.
- `endMaxMs` (2.5 s) is the longest this may wait. A character without these clips goes on at once.
- The clips use the painted poses already there, moved by bones (`make_hero_rig2d.py`): no new painting.
- The arena and training skip this and go straight to their next fight.

**Weapon levels now come from EXP (the owner, the same day), replacing wins:**
- Each monster has `weaponExp` in `enemies.json`. Beating it gives that much to the weapon used. To start: its `exp` / 6, at least 1 (a slime 1, the toughest normal monsters 7), and 10 for the boss, whose `exp` is 0.
- Each weapon has `levelExp` in `weapon_play.json`: `first` 5, `scale` 2, so 5, 10, 20, 40, 80, 160, 320, 640, 1280: 2555 EXP to Lv 10.
- Every weapon has at least 10 levels. Lv 6 to 10 add +2 to each of the weapon's stats per level; the combo stays at 5 points until changed in the data.
- The training ground gives double weapon EXP (`battle.json` `training.weaponExp`).
- The battle editor sets **weapon level** on the Player tab and a monster's **weapon EXP** on the Monster tab. Scenarios use `weaponLevel` or `weaponExp`.
- A save from before keeps working: its wins count as EXP, so a weapon may read a lower level than it did.

**Each level's detail is edited in the battle editor (the owner, the same day):** the Weapon tab has a **Levels** table: EXP to next (shown), `points` (the prompts or circles of an attack) and each attribute's bonus, for every level; above it, **EXP to Lv 2** and **EXP growth** (`levelExp`). **Save weapons** writes them to `weapon_play.json`. There is no limit on `points` in code any more: the file decides.

**W2, play types (`play_type.h`):**
- Data now names plays with the §4.1 names: `swipeWay`, `swipeGesture`, `drawShape`, `sequenceClick`, `pullRelease`, `mash`, `track`. The older names (`bow`, `slice`, `circles`, `shape`) still read.
- **Not in the enum yet:** `rhythmTap`, `holdCharge`, `tapTarget`, `reload` and `volleySet`. They join it with their plays in W4 and W7.
- Swipe tiers are read from one stroke in any direction: `v`, `m`, `square` (`f_MatchGesture`). Each has extra time (`plays.cueExtraMs`).
- The sword asks for one tick at Lv 3 and two at Lv 5 (`swipeCues`, renamed from `shapeCues`). M and square are ready but no weapon asks for them yet.
- DEX widens the swipe angle, gesture angles, shape corners and the answer window, each with a limit in data (§4.4). The swipe angle stops at 40°, because the eight ways are 45° apart.

**W3, ultimates:**
- `free_swipe` (3 s), `free_shot` (2 s) and `free_tap` (2 s) are in `special_moves.json`. Each swipe or tap is a hit at once.
- Each weapon names its ultimate in `weapon_play.json` (`"ultimate": "<move id>"`, Q49). Star Seal, Tiger Wind and Falcon Rain stay as moves that can be bound. Tiger Wind's triangle became a `v`.
- "Anywhere" includes the battle buttons while a free mode runs.
- **First guesses to tune:** `hitDamage` per hit is 0.6 (sword), 0.8 (bow) and 0.4 (staff).

**Still open:**
- The store's wand, twin daggers and war hammer have no play of their own, so buying one brings back the old point track. W4 replaces them with the weapon types of §3.

**The owner's answers (2026-11-05):**
- **Bow target:** it stays as it is: it waits still until the press, then slides for 0.5 s. Not to be changed.
- **Bow strength:** a slime (48 HP) takes 2 arrows, and that is right. The bow's numbers: STR by level 2, 4, 6, 8, 12; `chargeDamage` 0.4; the `perfect` row's `damage` 1.0; `maxMult` 1.6.
- **Sword and staff:** not lowered.
- **All monsters:** HP and attack doubled in `enemies.json`.

**Test input added:** `--drag`, `--wheel` and the scenario's `test.drags`. Their positions are in design pixels (1024x768), not screen pixels.
