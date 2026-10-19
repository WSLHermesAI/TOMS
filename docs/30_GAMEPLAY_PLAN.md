# 30 — Future gameplay plan

This plan is built from the owner's design notes, [魔塔.txt](魔塔.txt), written in 2024 (dated entries Feb 2, May 10
and June 2). It sorts them into one plan and adds what the notes asked for:
- three new weapons (the notes: "think of three new weapons and ways to fight");
- the open questions the notes leave.

Where this document adds its own proposal, it says **(proposal)**. Everything else comes from the notes.

Nothing here is built yet. Today's battle is described in section 1, so each idea can be checked against it.

## 1. What the game has today

- **Battle v2** (`src/core/game/core/game_combat.cpp`, `assets/data/battle.json`):
  - an **Attack bar** and a **Defense bar** move on their own; a tap freezes one;
  - a marker's position decides the power (green / blue / red zones, the weapon's `maxMult`);
  - the enemy attacks on its own clock;
  - landed hits charge a **super attack**.
- **Weapons** (`items.json`) each carry their own `bar`: start speed, top speed, ramp time, zone widths, `maxMult`.
  There are 4 weapons, among them the wand and twin daggers.
- **Attributes** (`stats.json`), and what each point gives today:

  | Attribute | Effect |
  |---|---|
  | STR | ATK |
  | DEX | Attack bar hit zone (`hitZone`) |
  | AGI | Shorter bar cooldown |
  | VIT | Max HP |
  | INT | Skill power |
  | LUK | Gold gain |

- **Already in the game:** 70 floors in 10 chapters ([29_FLOOR_REDESIGN.md](29_FLOOR_REDESIGN.md)), gold, EXP and
  levels, a store, a forge, skills (功法), equipment actives, rebirth, a hub and stage select.

The notes' combat ideas grow **out of Battle v2**: its bars become each weapon's own way to play.

## 2. The pillars

1. **Weapon = a way to play.** Each weapon type has its own touch mini-game, not only different numbers.
2. **Mastery shows.** The more a player uses a weapon:
   - the longer its combos;
   - the stronger the attack;
   - the bigger the animation and effects ("連招越多動畫越誇張攻擊力也越強").
   - Players should want the next level to see the new move.
3. **Merge cards to grow.** Inside a run, cards are bought and merged to make the hero (or the party) stronger.
   Between runs, gems raise how far merging can go.
4. **Mobile time.** Early weapon levels come fast; after advancing a weapon, levels come slowly.

## 3. The run and the meta (card merging)

From the notes:

| Step | Rule |
|---|---|
| In a run | killing monsters gives **gold**, usable only in this run |
| Clearing a stage | gives **gems** (kept) |
| Starting a stage | gems the player holds can be changed into gold, **up to a cap per run** |
| Gold | buys **cards** |
| Cards | **merge** to raise a character's stars in this run |
| World map | characters are upgraded with gems; this raises the **highest star** a character can be merged to |
| Deploy slots | each battle allows a fixed number of characters. Later, a character with a low star cap takes a slot without being able to grow, so the player must raise star caps on the world map |

The notes name Clash Royale as the model ("學皇室戰爭中的回合制機制").

**How it fits TOMS (proposal):** TOMS has one hero. Two ways to read "characters":
- **(A) Companion cards:** cards are companions or summons that fight beside the hero in Battle v2. Each takes a
  deploy slot and acts on its own clock, like the enemy does.
- **(B) Skill and weapon cards:** cards are the hero's skills or weapon moves; merging one raises its star (one more
  combo step, a stronger effect).

(B) changes less of the current game. (A) is closer to Clash Royale. See the open questions.

## 4. Combat: two bars

From the notes (the newest combat entry):

- **A vertical bar: charge (集氣).**
  - The first tap charges power; the longer the charge, the stronger the attack.
  - The more power, the **faster** the horizontal marker moves.
- **A horizontal bar: hits (攻擊幾次).**
  - Each tap inside the accepted range lands a hit and continues the combo.
  - The weapon's level and the attributes decide how many hits a combo can have.
- **Attributes:**
  - **AGI** sets the marker's speed;
  - **DEX** sets the accepted range (DEX already widens the hit zone today).
- The marker moves by a **tween** ("use tween to move the lighting indicator").

The trade-off is the core: more charge means more damage, but a faster marker and so a harder combo.

## 5. The weapons

### 5.1 Unarmed (空手): a moving bar, five points

```
1---2---3---4---5        the marker runs along; points light up as the level allows (up to five)
```

- Tap when the marker is on a point. Each hit adds a combo step; level ups add points, up to five.
- **The marker turns back**, so a missed point can still be saved, and the order is free. Example: tap 1, miss 2
  and 3, tap 4 and 5; the marker turns back to 3, and the combo goes on from there.
- Two misses in a row, or random tapping, is a **fail**: one plain attack, and the turn ends.
- (An older line: a miss in the middle sends the combo back to the start.)

### 5.2 Sword (劍): a polygon of points, no second chance

- The hit points sit where the lines of a polygon cross.
- The marker **does not turn back**: a missed point is a miss.
- After a miss, the hero **puts the sword back**: a longer wait.
- A sword combo is **one straight chain that can keep going**. The higher the weapon level, the more moves the
  chain has.
- The weapon level and the hero's **mastery** of the sword change the effect, the animation and the attack.

### 5.3 Magic staff (魔杖): circles in order (like Osu! Tatakae! Ouendan, 應援團)

- Circles appear in order; tap each in time.
  - The score depends on how many were completed.
  - The attack and its animation follow the score.
- **It needs to charge (聚氣)**, so the staff is slower to attack than the other weapons.
- **The chain grows:** the first attack has 3 circles. If all 3 hit, the next has 4; if all 4 hit, it has 5.
- At a high level, or with high mastery, the player can tap a **cast button** early, so the whole chain is not
  needed every time.
- **Lines that grow over time:** for example, four lines grow out one after another, and their hit marks play in
  the same order. Timing their collisions and the "magic numbers" shown at the right moment comes first (section 9).

### 5.4 Named moves (a first-person boxing view)

From the June 2024 entry:
- The view is like a first-person boxing game, pulled back a little, and plays like Ouendan.
- A combo can show **a choice of moves**:
  - played well, "虎虎生風四連擊" (Tiger Wind, four hits);
  - played badly, it becomes "虎頭蛇尾" (tiger's head, snake's tail): a weak attack.
- Applies to every weapon (proposal): name each combo result, so a player sees both what they did and what they
  missed.

### 5.5 Three new weapons (proposal, as the notes asked)

| Weapon | How you fight | What mastery and level add |
|---|---|---|
| **Bow (弓): hold and release** | Hold to draw; a sight swings over the enemy. Let go when the sight crosses the weak point. A longer draw hits harder, but the sight swings faster (the same trade-off as section 4) | More arrows per release (2, then 3); a "perfect release" window that grows with mastery; advanced: charged piercing shots that hit a whole group |
| **War hammer (戰鎚): draw the shape** | Swipe a shape shown on the enemy (a line, then a V, then a zigzag) within a time limit. Accuracy is the power. This is the Feb 2024 note "swipe special shape paths" | Harder shapes unlock bigger smashes; mastery widens the path's tolerance; advanced: a ground-quake that stuns |
| **Twin daggers (雙刃): rhythm taps** | Alternate left and right taps to a beat that speeds up. Each beat on time is a hit; an off-beat breaks the chain | More beats per chain; a finisher at the end of a full chain; advanced: poison on each hit (damage over time). Twin daggers already exist as an item, so this gives them their own play |

Each weapon in this table also gets:
- a fail result (a plain attack, as for unarmed);
- a recovery cost for a miss (the sword's "put back" wait is the model);
- named moves (5.4).

## 6. Weapon growth

- Two separate numbers:
  - **Weapon level:** the weapon itself; it rises with how often it is used.
  - **Mastery (熟練度):** the hero's skill with that weapon type.
- Both raise the effects. Mastery unlocks shortcuts, such as the staff's early cast.
- **Advancement (進階):** some weapons can advance into a stronger form.
- **Pacing for mobile:**
  - early levels are fast (a few battles each);
  - after advancing, levels come slowly.

  Example curve **(proposal)**:

  | Stage | Uses to the next level |
  |---|---|
  | Basic, Lv 1–5 | 5, 8, 12, 16, 20 |
  | Advanced, Lv 1–10 | 40, then +15 a level |

- **The level-up moment:** a combo of two hits becomes three. Show the new move right away, so the player feels it.

## 7. Choosing a style at the start

- A story event at the start lets the player **choose an attack style**, which equals the starting weapon type.
- The notes ask two things:
  - Is the choice tied to difficulty?
  - Is it fixed for the rest of the game ("之後不可改")?
- Weapons decide the attack mode (武器跟攻擊模式有關).

## 8. Other modes (separate ideas, for later)

- **May 2024: an idle side-scroller.**
  - The hero walks one way; the top half keeps the little map where an enemy (one or a group) was met.
  - The bottom half opens the battle.
  - If the player gives no command, the hero fights on their own when time runs out, using their stats.
  - Commanding by hand is faster.
- **June 2024: a maze chase (迷宮鬼抓人).**
  - The whole maze map is shown first, and each road has a way there and a way back.
  - The player heads for the goal and decides whether to detour for treasure chests.
  - Later, more and more ghosts chase the player.
  - Some walls can be blown up for a short cut.

## 9. Tools and engine work the notes ask for

- **A weapon test phase:** a new phase, outside the story, where the player fights any chosen monster to test how a
  weapon feels. This is also the developer's tuning room.
- **A tutorial** each time a new weapon is got, to practise its mini-game.
- **Animations for every state:** charge, hit, miss, fail, combo up, put the weapon back, cast.
- **Timed hits first:** the time-offset collisions and the numbers shown at those times are solved before the
  weapons are built (section 5.3's growing lines).
- **A safe `setTimeout`:** a delayed call must not run after its object is destroyed. The notes suggest a
  `shared_ptr` with a `std::map<pointer, function>`, and removing the entry when the call is done. **(proposal)** An
  owner token works: the delayed call keeps a `weak_ptr` (or an id checked against a live-objects table) and does
  nothing if the owner has gone.

## 10. Order of work (proposal)

| Phase | What | Why first |
|---|---|---|
| 0 | Engine: safe timers, tween-driven markers, the timed-hit display, the **weapon test phase** | every weapon needs them; the test phase is how the feel is tuned |
| 1 | The two-bar battle (section 4) with **unarmed** (5.1), as Battle v3 | the smallest weapon, and it replaces today's bars |
| 2 | **Sword** (5.2) and **staff** (5.3); named moves (5.4) | the two weapons the notes describe fully |
| 3 | Weapon level, mastery, advancement, pacing (section 6); the new-weapon tutorial; the start-of-story choice (section 7) | needs the weapons to exist |
| 4 | The three new weapons (5.5) | the same framework, new mini-games |
| 5 | The card, gold and gem meta (section 3) | needs the owner's choice of (A) or (B) |
| 6 | Prototypes of the other modes (section 8) | separate games; only if wanted |

Each phase ends playable in the weapon test phase, and gets tests by the project's rules ([09_TESTS.md](09_TESTS.md)).

## 11. Open questions for the owner

Tracked as **Q16–Q21** (waiting for the owner's decision) in
[progress_report/PROGRESS_REPORT.md](progress_report/PROGRESS_REPORT.md), Open Questions, each with a default.
Phase 0 (section 10) does not need these answers.

1. **Cards:** companions in battle (A) or the hero's skills and moves (B)? Do the gold, gem and card systems replace
   today's store and forge, or sit beside them?
2. **AGI:** today it shortens the bar cooldown; the notes make it set the marker's speed. A faster marker is
   harder. Should high AGI make it slower, or should it let the player afford more charge?
3. **The style choice:** is it tied to difficulty, and is it fixed for the whole game? Can other weapon types still
   be picked up later?
4. **Unarmed misses:** does a miss let the marker turn back (the newer note), or send the combo back to the start
   (the older line)?
5. **The enemy's turn:** do enemies keep their own clock (as today) while the player is in a combo, or wait?
6. **The other modes** (section 8): part of TOMS (an event floor, a mini-game), or separate games?
