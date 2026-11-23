# 25. A boss's fight as a sequence (2026-11-22)

中文：[zh_TW/25_BOSS_GAUNTLET.md](zh_TW/25_BOSS_GAUNTLET.md).

The owner (2026-11-22, with a drawing: three blocks, arrows down, then "BOSS a ? picture"): three monsters as a team
fighting together is right for normal monsters, but not for a boss. A boss's fight should be a **sequence**: one
monster; win, and the 3D scene moves on to the next monster's place; win again, move on; when all are down, the boss
comes. On the way, a **vertical ladder** shows how many fights there are: each block black until that monster is
fought, then its picture.

**Built (§4).** The open questions are in §5 and in [13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md).

## 1. When

- **A boss** (enemies.json `"intro": true`: the floor bosses, Grey Mask, Vorkath) bumped into on the map starts its
  sequence. A normal monster's `"party"` still fights beside it, all at once, as before.
- **Its guards**, in this order of choice: the stage object's `"gauntlet"` (a list of monster ids), else the boss's
  enemies.json `"gauntlet"`, else its `"party"` (it no longer fights beside a boss), else up to `count` (3) kinds of the
  floor's other monsters, each once, in the map's order (battle.json `"gauntlet"` `"fromFloor"`). No guards: the boss
  alone, as before. F01's hivebear: the capling, the mossling, the bat, then it.

## 2. The fights

- **Each guard is its own fight**, one monster; its win pays as any win (gold, EXP, weapon EXP, a card), but **shows no
  result screen** (the owner, 2026-11-22): its pay is a notice ("+6 gold · +7 EXP"), and the next fight starts at once.
- **The next fight**: the hero is back in his idle pose (not the win pose), and the 3D stage **travels** to the next
  monster's place (battle.json `"gauntlet"` `"step"`: [0, 0, -3.5] metres a fight, into the stage; `"travelMs"` 1500):
  the hero and the camera walk up to the monster, which waits at its own place; everything waits while they move. The
  hero **runs** on (2026-11-23): a bounce each step, leaning into it, a puff of dust at his feet (`"run"`). The
  **charge points** (必殺充能) go on from fight to fight; they are not reset.
- **The monsters still to come stand further on** (Q126, the owner: "show them in the distance"): each at its own place
  along the way, a little to the side (left and right in turn, `waitingSide` 0.9 m), as a dark shape (`waitingShade`,
  `waitingAlpha`): who it is shows when it is reached, as on the ladder.
- **The boss** is the last fight. It **comes out of the dark** first (the owner, 2026-11-23: "half black, then turn to
  colored, then the Mega Man effect"): dark while it waits (bossIntro `"dark"` 0.97, drawn again in black over itself),
  its colours come back over `"revealMs"` 1400, then its introduction (the boss banner) as before; it is like this for any boss
  with an introduction, sequence or not; its win shows the result screen;
  winning it clears its tile, and a floor boss opens the stairs up.
- **A loss** ends the sequence: the boss stays on its tile; the next try starts again from the first guard. The HP lost
  is not given back between the fights (the guards are the test).
- Without the 3D stage (Settings > 3D battle: off) the fights follow each other the same way, without the travel.

## 3. The ladder (battle.rml `#gauntlet`)

Down the left side of the battle screen, one block a fight, the boss's at the bottom (bigger, rounder, a red frame):

| State | Block |
|---|---|
| not reached | black: the monster's picture in black, "?" (the boss: "BOSS ?" / 「頭目 ？」) |
| now | its picture, a gold frame (the boss: red) |
| won | its picture, dimmed, its name in green |

## 4. As built

- `src/core/game/core/game_gauntlet.cpp`: `Game::f_GauntletGuards`, `f_StartGauntlet`, `f_DismissVictory` (the next
  fight and its travel), `f_EndGauntlet`, `f_StepGauntletTravel`, `f_FillGauntletUi`, `f_DrawGauntletWaiting` (the dark shapes further on);
  `Gauntlet::m_bAdvance` (a guard beaten: on at once, game.cpp). `Game::f_EngageMonster` starts
  it for a boss; `f_FinishCombatLose` ends it. The travel is `m_v3BattleTravel`, added to every 3D mark and the camera;
  `CombatState::m_iTravelMs` makes everything wait (as an introduction does), the fighters still shown.
- A stage object's `"gauntlet"` (`Entity::m_vGauntlet`, stage.h); battle.json `"gauntlet"` (`fromFloor`, `count`,
  `step`, `travelMs`); the 3D stage's shadows now reach 20 m in (`shadowBounds`).
- `UiGauntletStep` / `battle.steps`; battle.rml `#gauntlet`, battle.rcss; the text `battle.gauntlet_boss`.
- Scenarios: a battle's `"gauntlet"` (its guards before the scenario's enemy; `"guards"`: their overrides); expect
  `"gauntlet"`: `steps`, `at`, `foe`, `moved`. Tests: `boss_gauntlet_start` (F01's boss: 4 fights, a capling first),
  `boss_gauntlet_next` (a win closed: the stage moved on, the second fight).

## 5. Open questions

| Question | Default | Yours | Answer |
|---|---|---|---|
| **Q122** A boss without its own list: guards from the floor's monsters (3 kinds), or no sequence until each boss has its list? | the floor's monsters (built) | keep it; write a list for the story bosses later |  |
| **Q123** HP between the fights: kept as it is, or some back after each win? | kept (built) | kept: the sequence is the test; potions in the menu between fights later |  |
| **Q124** Each guard pays as a normal win (gold, EXP, card)? | yes (built) | yes |  |
| **Q125** A loss: start again from the first guard, or from the fight lost? | from the first guard (built) | from the first guard |  |
| **Q126** The guards waiting further on: show them standing in the distance, or only on the ladder? | — | — | **in the distance, as dark shapes** (the owner, 2026-11-22) |
