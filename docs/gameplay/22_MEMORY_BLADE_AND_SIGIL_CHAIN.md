# 22. The memory blade and the Sigil Chain: new ways to attack (2026-11-18)

中文：[zh_TW/22_MEMORY_BLADE_AND_SIGIL_CHAIN.md](zh_TW/22_MEMORY_BLADE_AND_SIGIL_CHAIN.md).

The owner asked for more weapon plays and more skill plays (2026-11-18). Two examples:

- **A weapon:** a sword whose prompts show one after another (up 0.5 s, left 0.5 s, down 0.5 s), then hide. The player then
  swipes them back in a time that can be changed (for example 1 s).
- **A skill:** 10 commands shown along the top centre, to be swiped in order in a time that can be changed (for example
  5 s). When all are done, a very striking finish hits the monster.

Both are **built** (§2, §3), and the bow's Cyclone Volley (§4, 2026-11-19). §6 lists more ideas, not built. The questions are in §8 and in
[13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md).

## 1. Where they fit

Both are new **play types** (17 §4.1) in the **swipe family**: every right answer is a cut that hits at once, with the
sword's slash, its combo clips and its damage share (`f_PlayHit`). They do not run on the cue track. Each has its own clock:

| Type (code) | Data name | What the player does | Used by |
|---|---|---|---|
| `eSWIPE_RECALL` | `swipeRecall` | watch the ways shown one by one; when they hide, swipe them back in order from memory, in one answer time | the memory blade (§2) |
| `eSWIPE_CHAIN` | `swipeChain` | a row of commands on a bar at the top; swipe them in order (ways, or gestures and shapes) in one time | the Sigil Chain (§3) |

The same play as the memory blade was proposed for the **two-handed sword** in [14](14_WEAPON_PLAY_AND_LOOT.md) §2.5
(Q59–Q63, still open). It was built here as a **new sword type**, so the two-handed sword keeps its V swipes until those
answers come. Moving the play to the two-handed sword is a data change: `"play": "swipeRecall"` and its `recall` rows.

## 2. The memory blade (憶刃): `swipeRecall`

**The play:**

1. The prompts show **one at a time** over the hero's head, each `showMs` (0.5 s), with a short blank between them
   (`recallGapMs`, 80 ms) so the same way twice would read as two. Two prompts in a row are never the same way.
2. A dot for each prompt fills as it shows, so the player can count them.
3. **"Go!"** shows for `recallReadyMs` (0.25 s).
4. Then the **answer time** starts: `answerMs` (1 s) plus `perCueMs` for each prompt. The prompts stay hidden. A "?" and
   a time bar show, and the dots mark which one is due.
5. Each swipe is judged against the prompt due. **Right:** a hit at once. **Wrong:** that prompt is missed and the next one
   is due (as the staff's wrong circle, Q40). **Time up:** the rest are missed.
6. The play ends with the usual result rows (`results`: all hit = perfect, ...), the same as the sword.

**A red prompt** (from Lv 6) asks for the **other way**: a red ↑ means swipe ↓. Its share is `reverse`.

**Its data** (`weapon_play.json` `"memory_blade"`): the sword's levels, effects, results and combo clips, with
`"play": "swipeRecall"`, `"ultimate": "sigil_chain"` and the timing by level. Each `recall` row sets only its own keys. The
rest come from the rows below it, then from `battle.json` `"plays"`.

| From Lv | Prompts (`levels`) | showMs | answerMs | reverse |
|---|---|---|---|---|
| 1 | 3 | 500 | 1000 | 0 |
| 3 | 4 | 500 | 1300 | 0 |
| 5 | 5 | 500 | 1500 | 0 |
| 6 | 5 | 500 | 1500 | 20 % |
| 8 | 5 | 420 | 1500 | 30 % |

DEX adds `dexWindowMs` (10 ms) a point to the answer time, as for every play (17 §4.4).

**The item** (`items.json` `"memory_blade"`): ATK +3, price 60, the sword's picture and battle clips for now. **Q105, the owner
(2026-11-18): a weapon to choose in the new game's weapon trial**: the weapon master's fifth choice, "Try the
memory blade" (`dialogue/weapon_master.json`, in `intro`, `weapons` and `after`; test `trial_try_memory_blade`). Not in
the store yet.

## 3. The Sigil Chain (千刃印): `swipeChain`

The memory blade's **ultimate** (when the super charge is full). It is a `special_moves.json` entry, so any weapon can use
it by naming it in its `"ultimate"`.

**The play:**

1. **The demo:** the move's name, a burst on the hero, a sound, then the screen dims (1 s).
2. **The bar:** all 10 commands in a row on a bar at the top centre, under the fight's title. Done ones turn green, missed
   ones red, and the one due is bigger and yellow. The count ("4/10") is beside it, and the time bar is under it.
3. **The one due** also shows big over the hero's head, where every attack command shows (the owner, 2026-11-13).
4. Each right swipe: a seal burst on the hero, the combo count and its attack clip, and the next part of the move's name.
   A wrong swipe misses that command and the next is due.
5. **Time:** `timeMs` 5 s for all 10 (+ DEX).

**The finish** depends on how many were done:

**First, the hero's introduction** (6 or 3–5 done; the owner, 2026-11-19: "like a boss intro"): for 1.7 s the camera comes
low and close on the hero and brings him to the middle of the screen (`camera` `value` 0.85), he draws his bow slowly
(`charge` ×0.75) in gathering sparks with the boss sting and the bow's charge sound, and the boss banner slides in from
the right in his colours: "絕招發動！" ("ULTIMATE!", `battle.ultimate_label`) with the move's name typed in under it
(`battle.json` `feelFx` `"heroIntro"`: `nameAtMs` 350, `typeMsPerChar` 90). As in all of a move's show, **the fight waits**:
the monsters' clocks, the attack bar and the guard's wait stop, and the attack button says "attacking" until the end.
Then the shots (below), from 1.8 s.

| Done | Finish | Damage |
|---|---|---|
| **10 of 10** | **full:** the camera orbits in on the monster (×1.7, 50°), a seal trail and a big star burst on it, the big attack clip, **10 hits** with a 1.3 s shake, then a last burst and an explosion sound | ×12 a normal hit |
| 6–9 | **partial:** "The sigil is incomplete...", a short close-up, 4 hits | ×12 × done / 10 |
| 0–5 | **fizzles** (`minHits` 6): the miss word, no damage | 0 |

**New in the move data** (`special_moves.json`, `special_moves.h`): `play.minHits` (fewer steps: the move fizzles), and
`"when"` on a finish beat: `"full"` (only when every step was done), `"partial"` (only when not), or none (always). The
finish lasts as long as the beats it plays.

The commands are 10 ways for now. Gestures and shapes (`"v"`, `"m"`, `"circle"`, ...) work in `cues` already, and the bar
draws them small (Q104).

## 4. The Cyclone Volley (旋風連矢): `spinCircle`

The owner asked for a new bow skill (2026-11-19): a rotating ring with particles; draw a circle clockwise 3 times, then
anticlockwise 3 times; then the special shot: the hero charges his power, then shoots 10 times in 1.5 s, each shot
hurting the monster. It is the **bow's ultimate** now (`weapon_play.json` `"bow"` `"ultimate": "cyclone_volley"`; the
free shot it replaces stays in `special_moves.json`, Q108).

**The play** (`play.kind` `spinCircle`):

1. **The demo:** the move's name, a burst on the hero, the ultimate's sound, then the screen dims (1 s).
2. **The ring** (`size` 220) on the hero's head (`ringY` -0.12: its middle that share of his box's height above the box's
   top, as his picture is drawn taller than the box; the owner, 2026-11-19). Three sparks (`guideFx` `spin_spark`) go round
   it the way to draw, once every 0.9 s (`guidePeriodMs`).
3. **In the ring:** the way ("順時針" / "逆時針", green / pink) and the count of that way ("2/3"); 24 ticks round the ring
   light up as the turn goes. Under it, a dot per turn (done green, missed red, due yellow, the anticlockwise ones purple),
   and the time bar.
4. **A turn:** the finger's angle round the ring's middle; the right way adds, the wrong way takes back (never below 0).
   330° (`spinTurnDeg`) is one circle: a burst (`spin_burst`) where it ends, the combo count. A turn may take several
   strokes; nearer the middle than 30 px (`spinMinRadius`) is not read.
5. **Time:** `timeMs` 6 s for all 6 (+ DEX). The turns not done when it ends are missed.

**The finish:**

| Done | Finish | Damage |
|---|---|---|
| **6 of 6** | **full:** after the introduction the sky darkens and **10 shots go up, 150 ms apart** (each restarts his `attack` clip at ×4 speed, with a release burst on him) and **fall as a meteor shower**: each `meteor` lands 380 ms later on the monster with a fiery blast (`meteor_impact`) and is a hit, and 2 smaller ones fall round it for show; a strong shake while they fall, then a last blast, a star burst, a big shake and an explosion | ×12 a normal hit, in 10 |
| 3–5 | **partial:** the introduction, then "Not all the power gathered...", 4 meteors 200 ms apart (smaller blasts, none for show) | ×12 × done / 6, in 4 |
| 0–2 | **fizzles** (`minHits` 3): the miss word only (no introduction, no partial beats), no damage | 0 |

**New in the move data:** `play.guideFx`, `play.guidePeriodMs` (the ring's sparks) and `play.ringY` (its height); `"speed"`
on a `clip` or `hits` beat (the hero's clip that many times faster); a `hits` beat's `"clip"` and `"fx"` (on the hero at each
shot), and `"projectile"`, `"impact"`, `"flightMs"`, `"extra"`: each hit a projectile launched `flightMs` before it from high
up to the monster's left, its impact where it lands, and `extra` more (the effect's name + `_small`) round the monster;
an `"intro"` beat (the hero's banner: `"text"` its label key, `"ms"`); a `camera` beat's `"value"` (in 2D, the share of the
way its fighter moves to the screen's middle, a little below it); `"when": "landed"` (full or partial, not a fizzle), and
`"partial"` now needs the move to land. `fx/battle.particle` has `meteor`, `meteor_small`, `meteor_impact`,
`meteor_impact_small`.

### 4.1 The sword's and the staff's ultimates (2026-11-20)

The owner asked for the same rundown on all four weapons, and chose to replace the sword's free swipe and the staff's free
tap ("as fast as you can", every hit during the play, no finish to introduce) with the two moves already in the data:

| Weapon | Ultimate | The play | Full finish (after the introduction) | Partial (3 or 4 of 5) |
|---|---|---|---|---|
| Iron sword | **虎虎生風** Tiger Wind (`tiger_wind`) | 5 prompts swiped as shown (the last a V; weapon level adds V's) | 8 fast cuts (his `attack` ×3 speed, a burst on him each), then his big blow and a burst on the monster; ×10 (it was ×4 as a skill) | 3 cuts, "威力未盡……" |
| Staff | **星之印** Star Seal (`star_seal`) | the pentagram drawn by particles, then traced in 6 s, corner to corner (strokes may be lifted) | the star drawn again, 10 falling stars (`meteor` with a `star_burst` impact), his big cast, a big burst; ×10 | 4 hits, "威力未盡……" |

Both fizzle under 3 of 5 (`minHits` 3). The name is no longer built piece by piece during the play (the introduction types
it whole). The shape is now drawn on the hero's head, like the bow's ring (before, it sat at the fighters' feet, half off
the screen). The free swipe / tap / shot moves stay in `special_moves.json`, no weapon's (Q108).

### 4.2 One rundown for every special attack (the owner, 2026-11-20)

The owner's rule (also in `CLAUDE.md`): every special attack, the hero's or a monster's, plays the same three parts.

| | 1. The play | 2. The introduction (1.7 s, the fight waits) | 3. The attack |
|---|---|---|---|
| The hero's ultimate | his input (circles, prompts, a star, commands) | the camera on him, the banner from the right: "絕招發動！" + the move's name; his charge pose | his finish, him in view (the camera ×1.12 on him) |
| A monster's special guard (`"use": "guard"`) | the player's guard play (draw a seal, tap fast) | the camera on it, the banner from the left in a boss's colours: "必殺技！" + its attack's name; its `power` pose, power burst, the boss sting, then its roar | its hit lands, less by the guard done |
| A monster's special attack (its danger cells) | its power-up while the player dodges | the same, right before the hit (`Game::f_StartFoeIntro`; `feelFx` `specialAttack` `"intro"`) | its hit lands (unleash, knock-down) |

During the introduction every clock, bar and cooldown waits, cell moves too. A monster's sounds and effects in it are bound
to it. A fizzled hero's move shows only its miss word (no introduction). Tests: `battle_special_down` and
`battle_floor_boss` expect one `foeIntro` (820 frames now); `battle_ultimate_sword_intro`, `battle_ultimate_sword_partial`,
`battle_ultimate_staff_intro`, `battle_ultimate_staff_full`.


## 5. The numbers in `battle.json` `"plays"`

`recallShowMs` 500, `recallGapMs` 80, `recallReadyMs` 250, `recallAnswerMs` 1000, `recallPerCueMs` 0, `recallReverse` 0,
`chainTimeMs` 5000 (a move's `timeMs` replaces it), `spinTimeMs` 6000 (the same), `spinMinRadius` 30, `spinTurnDeg` 330,
`spinPerfectMs` 900.

## 6. More ideas (not built)

From the owner's request on 2026-11-18. Each reuses the play blocks (16 §2) as far as it can.

**Weapon plays:**

| Weapon | How it attacks | Play type | New in code |
|---|---|---|---|
| **Katana** (sword) | Hold to keep it sheathed; the monster flashes an opening for ~0.2 s; let go and swipe right then. One big hit, or a counter. | `holdCharge` + timing | an "opening" telegraph |
| **Rapier** (sword) | A ring shrinks onto a weak spot; tap as it lines up (Perfect/Great/Good); the spot moves for the next thrust. | `tapTarget` | a shrinking ring, grades by time |
| **Spear** | Drag one straight line: every monster it crosses is pierced; straighter and longer hits harder. The first attack on several monsters. | new `lineDraw` | line vs. monster boxes, a straightness score |
| **Whip** | Draw a loop round a monster to bind it (stun), then flick. | `drawShape` (closed loop) | "did the loop enclose it" |
| **War axe** | A pendulum swings; tap at the top of the swing; a few swings make the combo. | `tapTarget` on a meter | the pendulum |
| **Chakram** (bow) | Swipe to throw; it curves back and must be swiped to catch it. A catch keeps the combo. | `freeSwipe` + a catch target | its return path |
| **Twin daggers** | Prompts on the left and right halves at once (two buttons on desktop, two thumbs on a phone). | `sequenceClick` ×2 | two input lanes |
| **Bell staff** (staff) | The bell plays a short tune on 3–4 spots; play it back in order and on the beat. | `sequenceClick` + `rhythmTap` | a note per spot |
| **Parry shield** (off-hand) | In the **monster's** attack, an arrow shows where it comes from; swipe against it to parry; a perfect parry counters. Makes guarding active. | `swipeWay` in the foe's turn | the attack's direction |

**Skill plays:**

| Skill | Play | Finish |
|---|---|---|
| **Constellation** | Stars appear; link them in order in one stroke without lifting. | A meteor from the drawn stars |
| **Blade Storm** | Swipe prompts pop up at random places and shrink; swipe each its way before it goes. | A slash per hit, then all at once |
| **Focus** | Time slows to 20 %; weak spots light 3 at a time; tap as many as you can in 3 s. | Crit multiplier = spots hit |
| **Rune Memory** | A rune is drawn once and fades; draw it again from memory, scored. | The rune explodes; the score sets the damage |
| **Chain Lightning** (staff) | Draw a path from monster to monster; each link is a jump. | Jumps, each weaker |
| **Arrow Rain** (bow) | Circle an area, then pull and let go N times inside it in time. | Rain on every monster in the area |
| **Clash** (a boss's ultimate) | A tug of war: mash or tap on the beat to push the bar. | Win: reflect it; lose: take it |

**For every play:**

- **Grades everywhere** (Perfect / Great / Good / Miss), each with its own sound and effect; Perfect streaks fill a
  **Fever** meter.
- **Status effects change the input:** Blind makes the prompts flicker, Confusion flips the arrows, Slow shortens the
  window. A monster's status attack then changes how the player plays.
- **Keyboard on the desktop:** the direction plays also take the arrow keys / WASD (Q107).

## 7. Code and tests

- `play_type.h` (the two types; `f_IsSwipePlay` includes them, `f_IsCueTrackSwipe` does not), `touch_play.h/.cpp` (the
  plays), `weapon_play.h/.cpp` (`recall` rows), `special_moves.h/.cpp` (`minHits`, `when`), `game_skills.cpp` (timing by
  level, DEX, the finish by outcome), `game_ui.cpp` (the hints, the dots, "Go!", the bar), `battle.rcss` (their styles),
  `text.json` (6 languages).
- `unit.play_types_test`: the recall's phases, a wrong swipe, time up, red prompts, the chain, the `recall` rows, `minHits`
  and `when`.
- Scenarios: `battle_play_recall` (nothing answered: one combo, no hits), `battle_ultimate_sigil` (nothing swiped: it
  fizzles), `battle_ultimate_sigil_full` (all 10 swiped: 10 hits).
- The Cyclone Volley: `play_type.h` (`eSPIN_CIRCLE`), `touch_play.h/.cpp` (the turns), `special_moves.h/.cpp` (`guideFx`,
  `speed`), `game_skills.cpp` (the ring's place and size, its sparks, a shot per hit), `game_combat.cpp` (the hero's clip
  speed), `game_ui.cpp` and `battle.rcss` (the ring, ticks, label, dots), `fx/battle.particle` (`spin_spark`,
  `spin_burst`, `volley_charge`), `main_sdl.cpp` (a test drag round a circle: `"o:cx:cy:r:turns:frames@frame"`).
  Tests: `unit.play_types_test` (turns both ways, a turn in two strokes, the middle not read, time up, the move's data);
  scenarios `battle_ultimate_cyclone` (no circle: it fizzles) and `battle_ultimate_cyclone_full` (6 circles: 10 hits).
  Battle editor setup: `slime Lv1 bow cyclone volley`.

## 8. Questions for the owner

Also in [13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md). Q100–Q104 are built with the proposal; answer only to
change them.

| Question | Default | Yours | Answer |
|---|---|---|---|
| **Q100** The memory play: a new sword type (built), or the two-handed sword's play (Q59)? | a new type, 憶刃 Memory Blade; the two-handed sword keeps its V swipes | Keep it |  |
| **Q101** Memory blade: a wrong swipe | misses that prompt; the next is due; the hits so far count | Keep it |  |
| **Q102** Memory blade timing by level (§2 table) | 1 s → 1.5 s; red prompts from Lv 6 | Keep it; try it and tune |  |
| **Q103** Which weapons get the Sigil Chain? | the memory blade only; the iron sword's is now Tiger Wind (§4.1) | Keep it |  |
| **Q104** Sigil Chain: commands, time, threshold | 10 ways, 5 s, partial from 6, ×12 | Keep it; add V / circle from a later level |  |
| **Q106** Which idea in §6 next? | none | the parry shield, then Blade Storm |  |
| **Q107** Keyboard for the direction plays on desktop? | mouse only | arrow keys / WASD, diagonals with two keys |  |
| **Q108** The free swipe / tap / shot ultimates are no weapon's now (§4, §4.1); their 3 switched-off scenarios would fail | keep the moves and the scenarios | delete the 3 scenarios; keep the moves for a later weapon |  |
| **Q109** Cyclone Volley numbers | 3 clockwise + 3 anticlockwise, 6 s, partial from 3, ×12, 10 shots in 1.5 s | Keep it; try it and tune |  |

- **The bow's camera when it fires** (the owner, 2026-11-22): after the introduction (low and close on the hero, the
  rule), the shot is seen **from behind the hero, over his shoulder, toward the monster** as the arrows go up; then the
  camera **cranes up and looks down on the monster** as the meteors land, and settles back (`special_moves.json`
  `cyclone_volley` camera `"move": "rain"`; also `"behind"` alone; battle.json stage camera `behind` (distance 3.8 m,
  height ×1.2 the hero, deg 26) and `rain` (rise 4.5 m, back 1.2 m, holdShare, riseShare)).

- **The bow's target in the middle** (the owner, 2026-11-23): the bow's pull-and-release target is no longer over the
  hero at the screen's side but halfway between him and the monster he aims at, over their heads (never over a monster;
  `Game::f_TouchArea`). The other weapons' plays stay over the hero's head.

- **Where a play shows: `"around"`** (2026-11-23): `weapon_play.json` a weapon's `"around"`: `"hero"` (the default: over his
  head), `"between"` (halfway between him and the monster he aims at, over their heads: the bow), `"monster"` (round the
  monster aimed at: the staff, the owner: "show around the monster, not above the player"); a special move's play
  (`special_moves.json`) `"around": "monster"` draws its shape on the monster (the staff's Star Seal). The staff's clicks:
  6 (Lv 1–4, 10), 5 (Lv 5–9).
