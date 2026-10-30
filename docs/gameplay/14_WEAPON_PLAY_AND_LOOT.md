# 14. Weapon plays v2 and the loot grind (the owner's ideas, 2026-10-30)

The owner's new gameplay ideas from 2026-10-30, put in order: a play of its own for each of the bow, the sword and the
staff (§2), and a monster-farming, treasure-hunting mode (§3). It builds on the gameplay plan
([07](07_GAMEPLAY_PLAN.md) §5 weapons, §6 growth, §8 other modes) and the battle editor ([13](13_BATTLE_EDITOR.md)).
Nothing here is built yet. The open questions are in §5.

## 1. The owner's notes (as written)

> 弓箭：拉弓射中目標。一個方塊，模擬拉弓，等級低只能射一箭，用技能能射兩箭，絕招時靶會出現在不同地方，都射中會出會心招式。
>
> 劍：類似砍水果，一刀兩斷，等級一只能砍一刀，等級提升可以砍多刀，用絕招要指定方向砍，後面連招考記憶跟方向。
>
> 魔法杖：類似應援團，等級低只會貌1-2個圈圈，等級高要瘋狂抓時間點指定的地方跟畫魔法陣。

> 刷怪打寶 diablo：無聊開起來刷怪抽寶。打倒怪物能夠收集賺來抽寶箱。特殊怪物才會指定寶箱有特別道具。
> 一直往最深處刷怪刷寶。整裝道具的能力加成。

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

| # | Question | Proposal if no answer |
|---|---|---|
| Q27 | Do the new plays **replace** the shared point track for the bow, sword and staff, or are they **added** (for example the point track for normal attacks, the new play for skills and the ultimate)? | replace it, weapon by weapon, with the old play kept as a setting until the new one is tuned |
| Q28 | **Bow:** is the box dragged (pull back from the box and let go, like a slingshot), or held (hold, a gauge fills, let go)? Do targets move? | drag back and let go; still targets at low level, moving targets at higher levels |
| Q29 | **Sword:** in the memory combo, are the directions shown first and then hidden, or shown one at a time? How many at most? | shown all at once for 1.5 s, then hidden; 3 at first, up to 6 |
| Q30 | **Staff:** magic-circle drawing was given to the war hammer in 07 §5.5. Does it move to the staff, and does the hammer get something else? | the staff draws magic circles (round seals); the hammer keeps straight-line shapes (lines, V, zigzag) |
| Q31 | **Loot grind:** a mode of its own (from the title or the hub), or the tower itself going below its last floor? Can it play itself (idle, as 07 §8's side-scroller) when there is "nothing to do"? | a mode of its own from the hub, with auto-battle on/off |
| Q32 | What do monsters drop to open chests: a new currency (chest tokens / keys), gold, or crystals? | a new item, "chest key", so gold and crystals keep their uses |
| Q33 | "整裝道具的能力加成": the bonuses of each piece of gear, or **set bonuses** for wearing a full set? | both: each piece has its bonuses, and a full set adds one more |
