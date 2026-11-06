# 19. The plan in three parts: battle, tower events, main screen (the owner's ideas, 2026-11-06)

中文：[zh_TW/19_THREE_PART_PLAN.md](zh_TW/19_THREE_PART_PLAN.md).

The owner's new ideas, sorted into their three priorities. **This is a plan; none of it is built.** The open questions are in
[13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md); the proposals below are not decisions until they are answered. Answered so far: §2.0.

| Part | Goal | Priority |
|---|---|---|
| **1. Battle** | A fight is fun in itself: big monsters, moving out of danger, a real play for each weapon | First |
| **2. Tower events and story** | A floor is more than monsters: things to collect, missions, and reasons to finish the story or the challenge | Second |
| **3. Main screen and growth** | The screen outside the tower: choose a mission or challenge, grow the hero, come back for more | Last |

## 1. The owner's notes (as written)

> 拉長遊戲耐玩度設計：做一個抽獎系統、每隔多久會取得免費水晶、水晶可以抽限定道具、武器
> 某些怪物難度會限定武器有加成、目前三種武器型態、可以買第四種槍類型
> 武器會有最低裝備要求、有的會故意要求agi or int
> 狀態點數能買道具重新洗點重加、藉由狀態跟武器裝備要求讓玩家重新洗點裝備測試武器
>
> 類似吸血鬼倖存者、死亡後能在商店買能力讓下一局開始更強
> 類似ball x pit 可以撿到建造物（卡片？）可以在自己的領地建造擺放物件取得獎勵
>
> 每隔三次通關會有一個大挑戰、會跑出一個超強挑戰玩家攻擊跟防禦操作能力的大魔王、贏了給特殊寶物輸了扣能力點
>
> 弓箭武器等級提升並沒有做出填充弓箭以及將箭放到弓上的遊玩方法,放越多可以瞄準的箭靶越多(只要瞄準到箭靶就符合,此時這個箭靶會暗掉,玩家繼續瞄準下個箭靶、中間只要手放開或是都瞄準到就觸發射擊）可以一次射好幾發
> 做個防禦教學、強調防禦的重要性、沒有防禦被打幾下就掛了
>
> 戰鬥將怪獸放中間，變很大，小怪佔螢幕的20%中怪35%,大怪50%， boss 65%,特殊魔王70%，以上，每隻怪物會有他可以被打的點，弓箭要打弱點才痛，打錯的話攻擊力會降低很多，所以卡到卡片會自動提示
> 魔杖的話，念咒（照順序點球，特效做炫一點），然後發射到要打的地方，會提示要滑到哪邊攻擊
> 劍士的話就是類似彈珠彈射出去，然後彈射出去的過程中要照順序畫指示的圖形，抵達之後會依照畫出的成功率進行連砍，打完不離開可以連點攻擊，離開才能再用畫圖形的方式使出連招，連點的攻擊力點一下只有5%的攻擊力
> 所以要有指示讓玩家知道要滑回玩遠離怪物的地方
>
> 防禦跟躲避變成怪物攻擊前會有提示要打哪邊，可能會近距離也有可能是一直線或是半扇形攻擊，所以玩家要自己移動位置或是遠離怪物
>
> 怪物打過來會有時間差自己要躲好
>
> 明天先把示意圖丟給geok畫一下，把三個人合體的那個圖放中間，然後前面畫幾個格子代表玩家能攻擊的地方，不管是劍，魔杖，都是點任意點然後攻擊，差別在於劍士會跑到怪物前面會拖一些時間，其他兩個武器能一直在遠方偷打我方可以站的地方有六個，最後排三個，中間三個，敵人攻擊時要換格子，有的敵人近戰很痛遠打很弱，這些要幾張示意圖演示一下，再丟給Claude寫功能
>
> 1.先做戰鬥做到好玩
> 2.做塔內事件,讓進塔不只是單純打怪可能要收集什麼過任務
> 3.做最外層的主角狀態升級,讓升級跟進塔過故事或是任務重複性的挑戰,新的DLC故事跟任務讓玩家重複挑戰

The owner also gave a picture of the main screen for part 3 (§4).

## 2. Part 1: make the battle fun

### 2.0 The owner's answers (2026-11-06)

| # | Answer | What it means |
|---|---|---|
| Q70 | **Keep both** moving and guarding; a boss-level monster needs an extra move to defend, something like drawing a 5-point star | Normal hits: move or guard. A boss's big hit: a drawn defence. Already in the game as `guardPlay` in `moves.json` (the lich's seal, `guard_pentagram`) |
| Q71 | **By landscape or portrait: whatever looks right** | The percentages are a guide, not a rule. Each monster's box is set per orientation in `battle.rcss` |
| Q72 | **Three monsters together is set in the stage data.** The stage shows a special icon (or a new symbol), and reaching it starts a fight with the three monsters together | A team has its own mark on the map. The fight is already possible (`party` in `enemies.json`, up to two beside the leader); the icon is new. One hero. The tapping part: next row |
| Q72, tapping | **It will be a new play, perhaps through a skill** | Not the normal attack: the three plays of §2.2 stand. A later idea, not in the steps of §2.4 |
| Background | **Different chapters show different things; not important for now** | The forest stays for all floors for now. `backdrops` in `battle.json` already picks by floor when chapters get their own |
| Map hint | **A small hint icon on the monster; the player must not know how many** | Built: a stage's `objects."x,y".party` (or `party` in `enemies.json`) puts a pulsing star (`anim/map_hint.anim`) on the map monster, the same star for any number. Floor 1's slime at 14,8 has one, for trying |

**The trial fight, built (2026-11-06; B2):** fight `thicketoad` alone to try it. A move with `"shape"` in `moves.json` turns the six cells on for that fight; every other fight is unchanged.
- Tap a cell or use the arrow keys to move. Before a hit, the cells in danger turn red and fill as it comes (`battle.json` `cells.warnMs`, 1.4 s); if the hero is clear when it lands: "Dodged!", no damage. Standing in it, the guard still works.
- Shapes are data (`cells.shapes`): near, line (aimed at the hero's column), fan (only the far middle is clear). The hero's large picture only shifts a little; a dot on the cell shows where they stand.

### 2.1 The new shape of a fight

```
        the monster, large, in the middle (with the spots it can be hit on)
   ----------------------------------------------------------------
   the danger shows before each hit: near, a straight line, or a half circle
   ----------------------------------------------------------------
        [ mid 1 ]    [ mid 2 ]    [ mid 3 ]      the hero stands on one of six
        [ back 1 ]   [ back 2 ]   [ back 3 ]     cells and changes cell to get clear
```

| Idea | What it means | What it replaces or uses |
|---|---|---|
| **Six cells** | Two rows of three. The hero moves between cells during the fight | New. Today the hero stands still |
| **Attacks with a shape** | Before a hit, the cells in danger light up: near, a line or a half circle. The hit lands after a delay, so there is time to move | Beside the guard: the player can move or guard (Q70) |
| **Range matters** | Some monsters hit hard up close and weakly far away, so the row is a choice | New; goes in each monster's `moves.json` |
| **Monster size** | Small 20 %, medium 35 %, large 50 %, boss 65 %, special boss 70 % or more of the screen | A guide; set to look right in each orientation (Q71) |
| **Hit spots** | A monster has spots it can be hit on. The bow hurts on a weak spot and does much less elsewhere. A held card shows the spots | New; needs marking on each monster's picture. Doc 15 §3.5 and doc 17 §10 already planned weaknesses by weapon |
| **Guard tutorial** | Shows that a fight without defending ends after a few hits | The doc 17 §11 curve asks for this too; teaches moving, guarding and a boss's drawn defence |

**The pictures in [new idea screenshoot](new%20idea%20screenshoot/):** `BattleSceneSimulation_Better.jpg` is bright daylight with ruins and a stone floor; the hero is seen from behind, very large, at the bottom left; it adds an MP bar, a small map and Q W E R skill icons. `BattleSceneSimulation_NotGood.jpg` is the dark forest. The stone floor is a natural place for the six cells. **Today's forest backdrop is closer to the "NotGood" picture;** the owner: each chapter gets its own later, not important now.

### 2.2 Each weapon's play

| Weapon | Before the attack | The attack | After | Reuses |
|---|---|---|---|---|
| **Bow** | Load arrows; a higher level loads more | **One pull aims at several targets:** each target the aim touches goes dark, and the shot fires when the finger lets go or all are touched. Several arrows fly at once | Weak spots hurt, others much less | The pull, multi-target `TouchPlay`. **Changes doc 17 §7** (quiver and volley) |
| **Staff** | Chant: tap the orbs in order, with strong effects | Swipe to where the spell should go; a hint shows where | Stays far away, safe | The circles (`sequenceClick`) |
| **Sword** | **Launched at the monster like a pinball**; while it flies, draw the shown shapes in order | On arrival, cuts as many times as the drawing earned | **Stays close: each tap is a hit at only 5 %.** Moving away is the only way to launch and draw again, and a hint shows where to swipe back | The swipe tiers (V, M, square) and combo levels built on 11-05. Takes time to arrive: the risk for its power |

The notes also say any weapon taps a spot to attack. That will be a separate new play, perhaps through a skill (§2.0).

### 2.3 Other battle ideas

- **Monsters that favour a weapon:** some give a bonus to a certain weapon. Doc 17 §10's resistances (W5) already do this.
- **A fourth weapon class, the gun:** bought. Doc 17 has three classes and Q52 allows a fourth damage type (Q73).
- **The big challenge:** every 3 clears, a boss that tests both attack and defence. A win gives a special treasure; a loss takes attribute points (Q79).

### 2.4 Proposed order

Test the fun first, on one monster, before making content for it.

| Step | What | Why this order |
|---|---|---|
| B1 | **Your sketches** of the cells, the attack shapes and the three weapons (your note) | Settles the cells by picture |
| B2 | **A trial fight:** Thicketoad at 50 %, six cells, three attack shapes, a delay, no weapon changes | Answers the main question: is moving out of danger fun? |
| B3 | **The staff** (chant, then swipe) | Closest to what exists |
| B4 | **The bow** (several targets in one pull; hit spots) | Needs spots marked on the picture |
| B5 | **The sword** (launch, draw, cut, 5 % taps, return) | The largest change |
| B6 | **The tutorial,** monster sizes for all, range per monster | Once the rules have stopped moving |
| B7 | **The big challenge boss,** the gun | Needs all of the above |

**What this means for doc 17:** W5 (resistances, switching) and W6 (the talk bar) still fit. W4's 12 weapon types and W7's bow and staff plays should wait,
because B3–B5 change how the weapons play (Q74).

**The new game's tutorial (built 2026-11-06, ahead of B6):** the weapon master's trial fight is now a lesson against 苔蛙 (`thicketoad`),
set in `battle.json` `tutorial`, with its words in `text.json` `tutorial.*`. Each step shows its words in a box at the top and waits until the player does it:

| Step | The player must | Meanwhile |
|---|---|---|
| Attack | Attack twice (a missed bow shot counts) | The toad waits; the cells are hidden; the bow shows its ghost finger |
| Move | Stand clear of the red cells when the hit lands | `toad_lesson_line`, again until dodged |
| Guard | Press Guard on the "!" | `toad_lesson_crush`, which has no cells, so it cannot be dodged; again until guarded |
| Finish | Beat the toad | Its usual moves, then the win clip |

The toad can fall at any step, which ends the lesson early (the owner, 2026-11-06), and the hero cannot fall: the hero stops at 1 HP. Trying another weapon repeats only Attack and Finish (`once`).
So the owner's note "without defending you fall after a few hits" is not shown yet.

**The first danger's moment (built 2026-11-06):** the first time a monster lights the cells in a fight, everything stops for 2.2 s: the screen dims, the camera moves in, and only the monster, its `special` clip, the particles `danger_intro` and a banner are shown. After 1.2 s the hero and the other UI return, the cells and the tutorial's box pop up with a scale effect, and the player has the last second to read them before time runs again. The cells stay hidden until then, in every fight. Set in `battle.json` `cells.intro`.

## 3. Part 2: tower events and story

**The goal:** going up the tower is more than beating monsters. Events give reasons to collect things, finish missions and follow the story.

| What exists | Where |
|---|---|
| 102 events in 10 acts, of 8 kinds (relic, whisper, cache, trap, rescue, merchant echo, memory shard, beat a named monster) | `assets/data/events/` |
| Event and stage editors; dialogue; stair locks that wait for events | `docs/gameplay/event_editor/` |
| 3 missions | `assets/data/missions.json` |
| 2 reward mini-games (the chest, the crystal shrine) | `assets/data/minigames.json` |

**So the tools are there; what is thin is the variety and the reasons.** Proposed kinds of floor goal (Q75 chooses):

| Goal | Example | Why the player cares |
|---|---|---|
| **Collect** | Find 3 seal pieces on this floor to open the stairs | Explores the floor, not just fights |
| **Rescue or escort** | Someone trapped behind monsters | Story and a reward |
| **Hunt** | Beat the marked monster, perhaps with a chosen weapon | Uses part 1's rules |
| **Choose** | Save the village or take the treasure | The story changes |
| **Timed or limited** | Clear with no potion | A reason to replay |

**Proposed first step:** one complete chapter of floor goals joined to its story, in the existing editors, as the pattern for the rest.

## 4. Part 3: main screen and growth (lowest priority)

**The picture shows:** the hero's portrait, level, HP and stats; potions and keys; gems and gold; the tower with a boss at floors 25, 50, 75 and 100; the buttons Ranking, Missions, Bag, Heroes and Shop; a large Challenge button; and two monster companions beside the hero.

| Idea | What it adds | What exists | Question |
|---|---|---|---|
| **Main screen** | The picture above | A village hub, a stage select, the System menu | Q76: the tower has 70 floors today, the picture 100 |
| **Lottery** | Free crystals over time, spent on limited items and weapons | Crystals as a currency | Q77: free only, or paid too? |
| **Weapon requirements** | Minimum stats; some ask for AGI or INT | None | With the next row |
| **Respec item** | Buy an item to place stat points again, to try other weapons | Attribute points; no respec | Proposed together with requirements |
| **Stronger each run** | After a death, buy abilities that make the next run stronger (like a survivor roguelite) | Rebirth, and cards that survive it | Q78: on top of rebirth, or instead? |
| **Territory** | Pick up buildings (cards?) and place them in your own land for rewards (a base-building loop) | The village hub; cards | Q80 |
| **Repeat play and DLC** | Repeat challenges; new story packs | Doc 09's design | Q24, Q25 are already open |

## 5. What I would watch

- **Three different games:** the cell battle, the tower events and base building each pull hard. The order you set keeps them apart: nothing in part 3 is needed to test part 1.
- **The art for size and spots:** at 50–70 % of the screen, every monster needs a painting and marked spots. Only three are made so far.
- **Defence:** moving, guarding and a boss's drawn defence all stay (Q70), so the screen must say clearly which one a hit needs.
- **Taking points on a loss** can feel harsh for a boss meant to be very hard (Q79).
- **To settle before coding:** your sketches.

## 6. Questions

The open ones are in [13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md), each with a default, a recommendation and an empty Answer.

**Waiting on the owner:** the sketches of the note ("把示意圖丟給 grok"): the three-in-one picture in the middle, the cells in front, and how the enemy's attacks reach them.
