# 07 — Future gameplay plan

This plan is built from the owner's design notes, [魔塔.txt](魔塔.txt), written in 2024 (dated entries Feb 2, May 10
and June 2). It sorts them into one plan and adds what the notes asked for:
- three new weapons (the notes: "think of three new weapons and ways to fight");
- the open questions the notes leave.

Where this document adds its own proposal, it says **(proposal)**. Everything else comes from the notes. The
owner's answers of 2026-10-20 are in section 11 and are written into the sections they change, marked
**Owner (2026-10-20)**.

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

- **Already in the game:** 70 floors in 10 chapters ([06_FLOOR_REDESIGN.md](06_FLOOR_REDESIGN.md)), gold, EXP and
  levels, a store, a forge, skills (功法), equipment actives, rebirth, a hub and stage select.

The notes' combat ideas grow **out of Battle v2**: its bars become each weapon's own way to play.

## 2. The pillars

1. **Weapon = a way to play.** Each weapon type has its own touch mini-game, not only different numbers.
2. **Mastery shows.** The more a player uses a weapon:
   - the longer its combos;
   - the stronger the attack;
   - the bigger the animation and effects ("連招越多動畫越誇張攻擊力也越強").
   - Players should want the next level to see the new move.
3. **Cards.** For now, a **collection**: monsters drop their card when defeated (section 3). Buying and merging
   cards in a run, with gems raising how far merging can go, is a later idea.
4. **Mobile time.** Early weapon levels come fast; after advancing a weapon, levels come slowly.

## 3. Cards

**Owner (2026-10-20): for now, cards are only a collection system.**

| Rule | Value |
|---|---|
| What a card records | per monster: how many times the player has defeated it, and how many times it has defeated the player |
| Getting the card | a chance each time the monster is defeated: the **first** copy 100 %, a **second** (same) copy 50 % |
| Where the chances are set | **in the monster's data** (`enemies.json`), so each monster can have its own |

The chances in the monster's data **(proposal: the field names)**:

```json
"slime": { "hp": 24, "atk": 8, ..., "card": { "chance": 1.0, "repeatChance": 0.5 } }
```

- `chance`: the chance to get the card when the player has none yet. Missing: 1.0.
- `repeatChance`: the chance to get another copy. Missing: 0.5.
- The counts (copies, defeated, defeated by) are kept in the meta save, so they survive runs and rebirths
  **(proposal)**. A Cards page in the player menu shows them **(proposal)**.

### 3.1 Later: merging cards (from the notes, not now)

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

How merged cards would act in battle (companions beside the hero, or the hero's skills and moves) is decided when
this part is built. The collection above comes first, and its copies are what merging would use later.

## 4. Combat: two bars

From the notes (the newest combat entry):

- **A vertical bar: charge (集氣).**
  - The first tap charges power; the longer the charge, the stronger the attack.
  - The more power, the **faster** the horizontal marker moves.
- **A horizontal bar: hits (攻擊幾次).**
  - Each tap inside the accepted range lands a hit and continues the combo.
  - The weapon's level and the attributes decide how many hits a combo can have.
- **Attributes:**
  - **AGI** shortens the **waiting time** between attacks, so with higher AGI the player attacks more often in the
    same time. **Owner (2026-10-20)**; this is what AGI does today (shorter bar cooldown). The marker's speed comes
    from the charge and the weapon, not from AGI;
  - **DEX** sets the accepted range (DEX already widens the hit zone today).
- The marker moves by a **tween** ("use tween to move the lighting indicator").
- **Attacks pause the clocks. Owner (2026-10-20):** while the player or the enemy is attacking (its attack and
  animation), **both** attack clocks pause until that attack is finished. Neither side can be hit in the middle of
  its own combo, and no time is lost while an animation plays.

The trade-off is the core: more charge means more damage, but a faster marker and so a harder combo.

## 5. The weapons

**Owner (2026-10-20):** the battle's play depends on the weapon; each weapon type has its own way to play.
The owner's newer ideas for the bow, the sword and the staff (2026-10-30) are in [14](14_WEAPON_PLAY_AND_LOOT.md) §2.

### 5.1 Unarmed (空手): a moving bar, five points

```
1---2---3---4---5        the marker runs along; points light up as the level allows (up to five)
```

- Tap when the marker is on a point. Each hit adds a combo step; level ups add points, up to five.
- **The marker turns back**, so a missed point can still be saved, and the order is free. Example: tap 1, miss 2
  and 3, tap 4 and 5; the marker turns back to 3, and the combo goes on from there.
- Two misses in a row, or random tapping, is a **fail**: one plain attack, and the turn ends.
- How far a miss sets the combo back depends on the weapon's level (section 5.6).

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

### 5.6 A miss sets the combo back (all weapons)

**Owner (2026-10-20):** a miss sends the combo back to an earlier step, and how far depends on the weapon and its
level. The owner's example:

| Weapon level | A miss sends the combo |
|---|---|
| Lv 5 | back to the start |
| Lv 10 | back 5 points |
| Lv 15 | back 4 points |
| … | less and less |

It is set per weapon in a **settings file**, so each weapon's values can be changed without code. Proposal:
`assets/data/weapon_play.json`:

```json
{
  "unarmed": { "missBack": [ { "lv": 1, "back": "start" }, { "lv": 10, "back": 5 }, { "lv": 15, "back": 4 } ] },
  "sword":   { "missBack": [ { "lv": 1, "back": "start" }, { "lv": 10, "back": 3 } ], "missWaitMs": 1200 }
}
```

- The entry with the highest `lv` at or below the weapon's level applies.
- `back` is a number of points, or `"start"`.
- The same file holds each weapon's other play settings (points per level, wait after a miss, …).

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
- Weapons decide the attack mode (武器跟攻擊模式有關). **Owner (2026-10-20):** the battle's play depends on the weapon.
- **Owner (2026-10-21):** the difficulty is **not** tied to this choice and **can be changed during the game**.
  The chosen style is the main one; other weapons can still be picked up and used.

## 8. Other modes (for later)

A third one, the loot grind (farm monsters, open chests, go deeper; 2026-10-30), is in [14](14_WEAPON_PLAY_AND_LOOT.md) §3.

**Owner (2026-10-20):** these are **part of TOMS**, but each is built as an **independent module** that another
game can reuse: its own code folder and data, talking to TOMS only through a small interface (start, the result,
the player's stats in), not through `Game` internals.

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
| 5 | The card collection (section 3): drop chances in `enemies.json`, counts in the meta save, a Cards page | decided; small, and it can come earlier |
| 6 | The other modes (section 8), as reusable modules | part of TOMS, built to be reused |

Each phase ends playable in the weapon test phase, and gets tests by the project's rules ([../project/08_TESTS.md](../project/08_TESTS.md)).
**Every scene a phase adds or changes gets a test scenario** ([08_TEST_SCENARIOS.md](08_TEST_SCENARIOS.md)): a folder in
`tests/scenarios/` that starts the game straight in that scene from setting files (a save, the player, the events),
so each phase can be tested alone.

## 11. The owner's decisions (2026-10-20)

The six open questions (Q16–Q21 in [../project/progress_report/PROGRESS_REPORT.md](../project/progress_report/PROGRESS_REPORT.md)) were
answered on 2026-10-20:

| # | Question | Decision |
|---|---|---|
| Q16 | Cards | For now **a collection only**: per monster, how many times it was defeated and how many times it defeated the player. The first card drops 100 %, a second copy 50 %, set **in each monster's data** (section 3) |
| Q17 | AGI | AGI **reduces the waiting time**: with higher AGI the player attacks more often in the same time (section 4) |
| Q18 | Style choice | The battle's play **depends on the weapon**; each weapon plays differently. 2026-10-21: the difficulty is not tied to the choice and **can be changed during the game** (section 7) |
| Q19 | Misses | A miss sends the combo **back to an earlier step**, by the weapon and its level (Lv 5: to the start; Lv 10: back 5; Lv 15: back 4 …), set per weapon **in a settings file** (section 5.6) |
| Q20 | The enemy's turn | While the player **or** the enemy is attacking, **both** attack clocks pause until the attack is finished (section 4) |
| Q21 | Other modes | **Part of TOMS**, but built as **independent modules** that other games can reuse (section 8) |

**The proposals are accepted (owner, 2026-10-21):** the three new weapons (5.5), the level curve (section 6), and the
names `card.chance` / `card.repeatChance` (section 3) and `assets/data/weapon_play.json` (5.6).

The questions as first asked:

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

## 12. Phase 0: done (2026-10-21)

| Piece | Where | Tested by |
|---|---|---|
| **Safe timers**: a delayed call never runs after its owner is gone (`TimerOwner`; `reset()` cancels a battle's calls when the next battle starts) | `src/core/engine/safe_timer.h` | `unit.safe_timer_test` |
| **Tweens**: a value over time with easing; once, repeat or back and forth; the speed can change in place (a marker that speeds up as power charges) | `src/core/engine/tween.h` | `unit.tween_test` |
| **Timed hits**: points due at given times, a tap graded Perfect / Good / Miss by its distance, ordered (sword, staff) or free (unarmed) tracks, and each point's growth from "appears" to "due" (the growing lines) | `src/core/engine/timed_hits.h` | `unit.timed_hits_test` |
| **Weapon play settings**: `assets/data/weapon_play.json`, a miss setting the combo back by weapon level (the owner's example is the `default` entry), the wait after a miss | `src/core/game/systems/weapon_play.h` | `unit.weapon_play_test` (also checks every key is a weapon of `items.json`) |
| **Attacks pause both clocks**: while the player or the enemy attacks, the bars stop moving and cooling and the enemy's clock waits (`battle.json` `attack_pause_ms`, 450) | `Game::update`, `startAttackPause` | the arena scenarios |
| **Damage numbers**: they rise over the fighter that was hit, when the swing lands (`hit_number_delay_ms`, 180, through a safe timer owned by the battle); a guarded hit shows "Guard!" | `Game::popDamage`, `battle.rml` | seen in the arena |
| **AGI**: shortens the wait between attacks (already so: `stats.json`, cooldown −15 ms a point) | `stats.json` | — |
| **The weapon test arena**: fight after fight against chosen enemies with a chosen weapon, full HP each time, no rewards, no story effects | scene `arena`, `toms_game --arena=slime,bat:wand` ([08](08_TEST_SCENARIOS.md)) | `scenario.arena_unarmed`, `arena_wand`, `arena_restart` |

The engine pieces have no game dependency, so other games (and the other modes, section 8) can reuse them. Battle v3
(phase 1) builds the two-bar battle and the unarmed play on them.

**Done after phase 0 (2026-10-21):**
- **The card collection** (section 3): `systems/card_collection.h` (`unit.card_collection_test`). A win counts and may
  drop the card ("New card: Slime!"); a loss counts. The records are in the meta save (`cards`, kept over rebirth). The
  player menu has a **Cards** tab (between Events and System): the monsters met, easiest first, `Cards: n / all`,
  each card's copies, wins and losses. The arena gives no cards. Scenarios `cards_first_win`, `menu_cards`.
- **Multi-event stair locks** (W38): see [event_editor/05](event_editor/05_FLOOR_LINKS.md) §3.

## 13. Phase 1: Battle v3's point play and weapon levels (2026-10-22)

The owner (2026-10-22): the points (up to five) come from the **weapon's level**, and each weapon has its own data per
level (for example Lv 1: STR +10, 3 points; values made up for now).

| Piece | Where | Tested by |
|---|---|---|
| **Weapon levels**: `assets/data/weapon_play.json` per weapon: `levels` (per level the points, 1-5, and the attribute bonuses, e.g. bow Lv 1 STR +10 / 3 points ... Lv 5 STR +20, AGI +15 / 5 points), `usesToLevel` (wins per level: 3, 5, 8, 12). A win with the equipped weapon counts (not in the arena); a level-up shows "Bow reached Lv 2: 3 points!". The level's bonuses add to the attributes (`Game::attrValue`). Saved in the run (`player.weaponUses`); a new game and a rebirth start every weapon at Lv 1 | `weapon_play.h`, `Game::weaponLevel` | `unit.combo_play_test`, `scenario.battle_weapon_level` |
| **The point play** (§4, §5.1): while the attack waits, **charge** builds up (`chargeMs`); the first tap starts the marker over the level's points, faster with more charge (`chargeSpeedUp`); a tap near a point hits it, in any order; a tap on nothing is a **miss** (the combo goes back by `missBack`, then `missWaitMs`); two misses in a row are a **fail** (a plain attack). The combo ends when every point is hit, when the sweeps run out (`maxSweeps`; `turnBack: false` is one sweep), or on a fail | `systems/combo_play.h` (no game dependency) | `unit.combo_play_test` |
| **The damage**: the share of points hit is the power (0-100) of the existing damage formula, times the charge (`chargeDamage`: a full charge doubles it). A fail is power 0 (still a plain hit). The enemies' clocks keep running while a combo runs (the owner 2026-10-26: they keep charging; before, they waited, §4): an attack lands in the middle of the combo, and the combo goes on (quick jabs cut it short). `battle.json` `combo_holds_monsters: true` brings back the wait. Since 2026-10-30 (the owner) the combo's marker also keeps moving through the attack's pause (`attack_pause_ms`); `combo_runs_through_attacks: false` stops it, as before. All points hit shows a big number | `Game::resolveCombo` | the battle and arena scenarios |
| **The screen**: the attack column shows the charge (a yellow column), the track with its points (green when hit) and the marker, and "Bow Lv 1 0/3" (the level and the wins toward the next); the defence bar is unchanged | `battle.rml` / `.rcss`, `UiCombo` | screenshots |
| **Scenarios**: `player.weaponUses` sets wins per weapon; `expect.weaponLevel` checks levels; `battle_bow` taps the bow's three points | [08](08_TEST_SCENARIOS.md) | `battle_bow`, `battle_bow_idle`, `arena_restart`, `battle_weapon_level` |

Every weapon uses the point play for now; the sword and staff of §5.2 / §5.3 get their own plays later.

## 14. Phases 2 and 4: the sword and the staff, their clips by status (2026-10-22)

| Piece | Where | Tested by |
|---|---|---|
| **Two new weapon types** in `items.json`: `sword` (Iron Sword) and `staff` (Oak Staff), names in six languages. They use the point play with their own settings (`weapon_play.json`): the sword's marker **does not turn back** (one sweep) and a miss costs a 600 ms "put back"; the staff **charges longer** (3 s) for more damage (x2.6 at full charge) and a slower marker. Their own plays (§5.2 polygon, §5.3 circles) come later | `items.json`, `weapon_play.json` | `unit.atlas_sprites_test`, `scenario.battle_sword`, `battle_staff` |
| **Clips by status**: `"battleAnim": {"character": ..., "held": ..., "clips": {"idle": "ready", "charge": "raise", "attack": "slash", "miss": "fumble"}}` names the clip each battle status plays (in both files); a status not listed plays the clip of its own name (the bow) | `EquipmentDefinition::battleClips`, `Game::setBattleRigState` | `scenario.battle_sword` (`expect.battleClip`) |
| **The animations** (phase 4): `tools/make_weapon_anims.py` writes `weapon_sword.anim` + `fighter_sword.anim` (ready, raise, slash, fumble) and `weapon_staff.anim` + `caster_staff.anim` (hold, focus, cast, fizzle), weapons built from the fx shapes like the bow; the charge clips keep a looping tremble / glow while the attack waits. Any character plays them (`body` / `hand`, fitted at run time) | `tools/make_weapon_anims.py`, `assets/media/anim/` | `anim_editor --headless check` (0 errors) |
| **The sprites** (phase 4): `item_sword`, `item_staff` (32 x 32, `tools/make_weapon_icons.py`) in the game atlas (and dark16, which falls back to them) | `art_raw/sprites/`, `assets/media/atlas/` | `unit.atlas_sprites_test` |

## 15. Phase 3: the start story, trying the weapons (2026-10-22)

The owner (2026-10-22): a simple start story lets the player pick up a weapon and try it, and try another until
satisfied; then the run begins on stage 1 (section 7's "choose a style at the start").

| Piece | Where | Tested by |
|---|---|---|
| **The weapon master** (`assets/data/dialogue/weapon_master.json`, zh_TW + en): at the village edge he offers the bow, the sword and the staff ("Tell me about them" explains each one's play). The title's **New Game** opens it, after the new run is set up | `Game::startWeaponTrial` | `scenario.trial_start` |
| **A trial fight**: dialogue action `{"type": "tryWeapon", "weapon": "sword"}` wears that weapon and starts one fight in the weapon test arena against a training slime (40 HP, weak, slow), no rewards and no weapon win counted | `Game::tryWeapon`, `Arena::trial` | `scenario.trial_try_sword` |
| **Back to the master**: when the trial fight is over (a win dismissed, or a loss shown), his `after` node asks: set out with this one, or try another | `Game::endWeaponTrialFight` | `scenario.trial_choose_staff` |
| **The choice**: `{"type": "chooseWeapon"}` keeps the worn weapon as the only gear (the starting bow goes if another was chosen), sets the run flag `start.weapon.<id>`, and the player walks on stage 1 | `Game::chooseWeapon` | `scenario.trial_choose_staff` |

Test scenarios start without it (they call `newGame` directly); scene `weaponTrial` opens it ([08](08_TEST_SCENARIOS.md)).

## 16. Phase 6: a reusable reward event, gold or crystals (2026-10-22)

The owner (2026-10-22): a simple, reusable event that lets the player earn money or crystals, one of them in
Traditional Chinese (to see Chinese working). Phase 5 (the card collection) was already done (§12).

| Piece | Where | Tested by |
|---|---|---|
| **Crystals**: a second currency beside gold, in the HUD (`GOLD 0  CRYSTALS 0`), saved with the run (`player.crystals`), reset by a new game and a rebirth | `Player::crystals`, `PlayerSaveData::crystals` | `unit.save_test`, `scenario.minigame_crystal` |
| **The reward event module**: `assets/data/minigames.json` defines each event (title and text per language, the currency, `perHit`, `bonusAll`, `limit` plays a run, and the point play: points, sweep, turning back, hit window). Any dialogue choice starts one: `{"type": "miniGame", "id": "chest_lockpick"}`. Its screen (`minigame.rml`) shows the text, then the point track: Enter / a tap starts, then hits the points; each hit earns `perHit`, all of them `bonusAll` more; two misses in a row end it with nothing | `systems/mini_game.h` (no game dependency), `Game::openMiniGame` | `unit.mini_game_test` |
| **Two on stage 1**: the **Peddler with a Chest** (`P`, `chest_lockpick`: 4 pins, 15 gold each, +30 for all) and the **crystal shrine's priest** (`O`, `crystal_shrine`: 3 lights, 1 crystal each, +2 for all). The shrine's dialogue, event text and the priest's name are **Traditional Chinese only**: a language missing falls back, so it shows Chinese in every language | `stages/stage01.json`, `npcs/chest_peddler.json`, `npcs/crystal_oracle.json`, `dialogue/` | `scenario.minigame_chest`, `minigame_crystal` |

**Next:** the phases the owner asked for on 2026-10-22 (1, 2, 3, 4, 6) are built; phase 5 was already. Later: the sword's and
the staff's own plays (§5.2, §5.3), named moves (§5.4), mastery and advancement (§6), the other modes (§8); or the
download steps that need no decision
([09](09_DOWNLOADS_AND_DLC.md) §6, steps 1-2).

## 17. Timing help on the attack track (2026-10-24)

The owner (2026-10-24): the timing for the points is hard to get right; add a particle and a sound as a hint, or make
the accepted range bigger. Both were done.

**Why it was hard.** A point took a tap within 0.07 of the track on each side. With the bow (1.7 s a sweep) that is
about 240 ms, and with a full charge, which doubles the marker's speed, only about 120 ms. Touch and display delay on
a phone use up 50–80 ms of that.

**The window** (`combo_play.h`, `weapon_play.json`):
- `hitWindowMs` (default 140): the window is never shorter than this in time, so a charged marker keeps it;
- `lateMs` (default 80): a tap this long after the marker passed a point still counts (only late taps, not early ones);
- never more than 45% of the gap between two points, so a tap still has to aim at one;
- `hitWindow` stays the smallest window in track units. Each weapon can set its own values.

The bow at Lv 1 (3 points): about 360 ms with no charge (was 240), about 270 ms with a full charge (was 120). With five points
the gap caps it: about 210 ms with a full charge.

**The hint** (`Game::comboHint`):
- when the marker enters a point's window: the sound `combo_ready` (`tools/make_combo_sfx.py`), a gold ring and star
  on the point (`combo_ready` in `media/fx/battle.particle`), and the point grows and turns gold (`.pt.near` in
  `battle.rcss`) while a tap would hit it. The highlight comes from the same window the tap uses;
- when a point is hit: a green burst on it (`combo_point_hit`).

The particles are drawn **over the UI**: a new sprite layer (`IRenderer::setLayer(1)`, bgfx view `kViewTop` = 3,
ImGui moved to view 4). The UI tells the game where it drew the points (`GameUi::sync` reads the layout,
`Game::setComboPointsOnScreen`), so the particles sit on them whatever the screen size.

Tests: `combo_play_test` (the time window, the cap, late but not early taps, the hint); the battle, arena, trial and
reward-event scenarios pass. `arena_restart` taps twice to miss; its taps moved earlier, before the first window.
