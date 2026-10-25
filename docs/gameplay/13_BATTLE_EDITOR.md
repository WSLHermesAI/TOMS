# 13 — Battle editor, and a real-time strategic battle (design)

> 中文版：[zh_TW/13_BATTLE_EDITOR.md](zh_TW/13_BATTLE_EDITOR.md)

**Status:** design, 2026-10-25; the owner's answers are in §8. Built so far: §4.6 (one to three monsters, targeting)
and the battle's event log (§4.5), see §4.6's **Built**. Two pages show it (open them in a browser, they work offline):

- [battle_editor/editor_mockup.html](battle_editor/editor_mockup.html): how the editor looks, every panel; the level
  growth calculator (§4.3) works.
- [battle_editor/battle_demo.html](battle_editor/battle_demo.html): a small playable demo of the real-time strategic
  battle of §6 (the monster shows its next move, you choose how to answer).

Builds on [11_BATTLE_DATA.md](11_BATTLE_DATA.md) (shared stats, battle events, the status map) and
[07_GAMEPLAY_PLAN.md](07_GAMEPLAY_PLAN.md) (the attack track, weapon levels).

## 1. What the owner asked for (2026-10-25)

1. A **monster list** to pick who to fight; each monster's settings edited and saved.
2. **Weapon data** saved and loaded, a list to switch weapons for testing, and **special settings**: a higher weapon
   level has more points, and how many points the player matched changes the effect. Changeable in data; more particle
   and animation files are needed first.
3. **One button to battle** (and again to restart).
4. **Player status** saved and loaded (HP, STR, DEF, ...), and a **calculator** for the points each level gives: "100
   extra points by level 20" becomes a growth curve whose levels add up to exactly 100.
5. **Monster events**: before the battle (a dialogue), when its HP falls below a share (it grows stronger, or turns
   into another monster), and more.
6. Ideas to make the battle **more fun: real time, with strategy**.

## 2. Where it lives

**Its own program, `battle_editor.exe`** (the owner, 2026-10-25: a new editor, not more put into `toms_editor`). It is
built like `ui_editor`, which is already a separate program sharing `toms_editor`'s game viewport (`bgfx_viewport`,
`GameSession`): Qt panels on the sides, the real game in the middle. The battle in the editor is the battle in the game,
the same code, so what is tuned there is what the player gets. It is packaged into `Editor/bin` with the other tools.

- **One click to battle** = the editor writes the current setup as a **test scenario** (the scenarios already describe
  a battle: the enemies, their HP, the player's weapon, level, attributes; [08_TEST_SCENARIOS.md](08_TEST_SCENARIOS.md))
  and starts it in the viewport. Restart = start it again.
- **Test setups are editor files only** (the owner: not automated tests): `tools/battle_editor/setups/<name>.json`, in
  the scenario format, kept in the repo so the team shares them, never shipped with the game and never run by
  `tools	est.cmd`.
- **Saving** writes the game's own data files: monsters to `data/enemies.json`, weapons to `data/weapon_play.json`,
  growth to `data/stats.json`, the battle's pace to `data/battle.json`. A file another editor changed is read again
  (`checkFiles`).

## 3. Layout

```
┌──────────────┬──────────────────────────────────────┬─────────────────────────┐
│ MONSTERS     │  [▶ Battle] [⟲ Restart] [⏸] [×0.5 ×1 ×2]│ Monster | Weapon |       │
│ IN BATTLE: 2 │  pace: (●) cooldown ( ) stamina      │                         │
│ search…      │                                      │ Player  | Events | Log   │
│ ch.1 ▾       │        the game's battle screen      │                         │
│ ● Slime      │        (the real battle, live)       │  the selected tab's     │
│ ○ Bat        │                                      │  settings               │
│ ○ Golem      │                                      │                         │
│ …            │  Portrait / landscape  [⇄]           │  [Save] [Revert]        │
├──────────────┴──────────────────────────────────────┴─────────────────────────┤
│ TEST SETUPS: [slime Lv1 bow] [3 bats Lv5 sword] [+ save current]    log ▸       │
└───────────────────────────────────────────────────────────────────────────────┘
```

The viewport can switch between landscape and portrait ([12_UI_LAYOUT.md](12_UI_LAYOUT.md)), so both layouts are
tested while tuning.

## 4. The panels

### 4.1 Monsters

The list: search, filter by chapter, the boss marked; **Duplicate** (a variant: "slime_red" from "slime"), **New**,
**Delete** (asks; a monster placed on a floor is named in the warning).

A monster's settings (the fields today, then the ones [11 §4.1](11_BATTLE_DATA.md) adds):

| Group | Fields |
|---|---|
| Name and picture | name (per language), sprite (atlas picker with preview) |
| Stats | HP, STR, DEF, DEX, AGI, VIT, INT, LUK (the player's six, [11 §2](11_BATTLE_DATA.md)); today `atk` / `def` |
| Battle | attack interval (ms), its weapon (style and timing), special moves |
| Look | its status map: a clip, sound and particle for idle, attack, hurt, die, special ([11 §4.2](11_BATTLE_DATA.md)) |
| Rewards | EXP, gold, card chance |
| Events | §4.4 |

A **difficulty readout** next to the stats: with the current test player, how many hits it takes, how much damage it
deals per attack, the expected fight length, and "the player wins / loses on average" from a quick simulation.

### 4.2 Weapons

The list of `weapon_play.json` entries; **Switch to this weapon** equips it for the next battle.

Per weapon: the sweep time, turn back, charge, the timing window (`hitWindow`, `hitWindowMs`, `lateMs`, §17 of
[07](07_GAMEPLAY_PLAN.md)), the miss rule, and a **table by level**: the points on the track, the stat bonuses, the
wins to the next level, the particles (`fx`).

**Special results** (new, the owner's item 2): what happens depends on how many points were matched.

```jsonc
"results": [                                   // weapon_play.json, per weapon (or per level)
  { "hits": "all",  "name": "perfect",  "damage": 1.5, "fx": "bow_perfect", "anim": "attack_big", "sound": "perfect" },
  { "hits": 3,      "name": "great",    "damage": 1.2, "fx": "bow_hit_3" },
  { "hits": 1,      "name": "hit",      "damage": 1.0, "fx": "bow_hit_1" },
  { "hits": 0,      "name": "miss",     "fx": "miss_puff", "anim": "miss" }
]
```

The highest row the result reaches applies ("all" = every point). A row can also add an effect: stun the monster for
N ms, push its attack timer back, a second hit, a status. The editor shows the rows as a list with pickers for the
particle (from `media/fx/battle.particle`), the clip (from the weapon's `.anim`) and the sound, and a **Preview**
button that plays the result's particle and clip in the viewport without a battle.

**What has to be made first:** particles for each result tier of each weapon (perfect / great / hit / miss), clips for
the big attacks (`attack_big`, `special`) in `archer_bow`, `fighter_sword`, `caster_staff`, and the monsters' `special`
and `transform` clips in `monster_battle.anim`. The editor lists what a setting names but is missing (red).

### 4.3 Player

The test player: level, HP / max HP, the six attributes, gold, the weapon and its level (wins), armour, skills. Saved
and loaded as test setups (§2).

**The growth calculator** (the owner's item 4): "by level L the player has gained T attribute points" → the points
each level gives, on a curve, adding up to exactly T.

```
points gained by level n (n = 2 .. L) = T · ( (n-1)^k − (n-2)^k ) / (L-1)^k
```

`k` is the curve's shape: 1 = the same every level, above 1 = more later (slow start), below 1 = more early. The
levels are rounded so that they still add up to T exactly (the largest remainders get the extra point). Example:
T = 100, L = 20, k = 1.3 gives Lv 2–20: 2, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, and the sum is 100.
The calculator:

- shows the per-level points as bars and the running total as a line;
- **Apply to the test player** (that level, those points);
- **Save as the game's table** (`stats.json` `levelUp.points`, [03_PLAYER_MENU.md](03_PLAYER_MENU.md)), the rows
  merged where neighbouring levels give the same, so the table stays short.

The mockup's calculator works ([editor_mockup.html](battle_editor/editor_mockup.html), Player tab).

### 4.4 Monster events

Bound to a monster in its record (`enemies.json` `"events"`), run by the battle events of [11 §4.3](11_BATTLE_DATA.md):
a **trigger**, an optional **condition**, a list of **actions**, and how often (once / every time).

| Triggers | Actions |
|---|---|
| before the battle | show a dialogue (a dialogue file and node, or a line) |
| the battle starts / its first attack | change a stat (+30% STR, DEF × 2, for N s or for good) |
| its HP falls below N % | **transform** into another monster (its HP kept as a share, or full) |
| the player's HP falls below N % | summon / call a helper (later: battles with several monsters) |
| every N s, or after its Nth attack | change its attack interval, its next move |
| it is hit by a perfect / it dodges | play a clip, a particle, a sound; shake the screen |
| it falls | heal, shield, flee (the battle ends, no reward) |
| | set a story flag, start an event, give the player control (story fights, [11 §4.4](11_BATTLE_DATA.md)) |

```jsonc
"events": [
  { "on": "beforeBattle", "do": [ { "dialogue": "golem_wakes" } ] },
  { "on": "hpBelow", "value": 50, "once": true,
    "do": [ { "say": "The golem's runes blaze!" }, { "stat": "str", "mul": 1.3 }, { "anim": "special" } ] },
  { "on": "hpBelow", "value": 20, "once": true,
    "do": [ { "transformInto": "golem_core", "hp": "keepShare" }, { "fx": "transform_burst" } ] }
]
```

The editor shows the events as cards (trigger → actions), with pickers for the dialogues, monsters, clips, particles
and sounds, and a **Fire now** button on each card to try it during a test battle.

### 4.5 The battle bar

**▶ Battle** (writes the setup, starts it; the same button restarts), **Pause**, speed ×0.5 / ×1 / ×2, **Win now** /
**Lose now** (to try the endings and the events on death), and the **log**: every battle event with its time
(attacks, hits with their damage and result tier, guards, events fired, status changes), so a balance question is
answered from the log rather than by watching.

### 4.6 Several monsters in one battle (now: the owner, 2026-10-25)

A battle has **one to three monsters**. In the editor, each monster in the list has a **+ Battle** box; the ones
ticked (in order) are the battle's enemies, and the bar shows "In battle: 2". Summons (§4.4) add one while it runs, up
to the limit.

What it changes in the game:

- **The battle setup** ([11 §4.4](11_BATTLE_DATA.md)): `"right": [{"who": "monster:bat"}, {"who": "monster:bat"}]`.
  A floor's monster can bring company (`enemies.json` `"party": ["bat", "bat"]`).
- **Targeting:** the attack goes to the **target**: tap a monster (or ←/→) to choose; it starts on the first. A fallen
  monster passes the target to the next. The guard bar protects against all of them.
- **Each monster runs its own clock and shows its own intent** (§6.1); when two attack at once, their intents show it
  and a perfect guard parries both.
- **The screen:** landscape: the monsters in a row on the right (one big, two or three smaller); portrait: in a row
  at the top. Their boxes come from the UI layout as today (`.foe .fbox`, `Game::setBattleBoxes`), so the particles
  and numbers land on the right one.
- **Rewards:** the sum; a card chance per monster.
- **Battle v3's attack pause** ([07 §4](07_GAMEPLAY_PLAN.md)): while anyone attacks, every clock waits, as now.

**Built (2026-10-25):**

- `CombatState::foes` (each its `EnemyInst`, HP and clock) and `cs.target`; `toms::nextStanding` / `allDown`
  (`systems/battle_party.h`, tested by `unit.battle_party_test`). A fallen monster plays its die clip and stays down; the
  last one falling wins the battle. Gold and EXP are summed, each monster's card rolls on its own; the map tile of the
  lead (the monster walked into) is cleared.
- Each monster has its own rig (`Game::foeRigs_`) and box. `battle.rml` repeats `.foe` per monster (`.n1`–`.n3`,
  `.f1`/`.f2` in `battle.rcss`, in landscape and both portrait layouts); `GameUi::sync` reads the `.foe .fbox` boxes
  in order. One monster stays exactly where it was (the battle screenshots did not change).
- Targeting: tap a monster (`act('battle_target', i)`) or ←/→; a yellow bar over the target when there are two or
  more. The title reads "Battle! Slime +2"; with two or three, each name and its HP are on two lines (a long name wraps); the clock bar shows the monster closest to attacking.
- `enemies.json` `"party": ["bat", "bat"]` (up to two); scenarios: `"battle": {"enemy": ..., "party": ["bat",
  {"enemy": "skeleton", "hp": 5}]}`, and `expect` `"foesDown"` / `"target"` (`scenario.battle_party`,
  `scenario.battle_party_target`).
- The log: `CombatState::events` (`toms::BattleEvent`: start, combo, hit, down, target, super, active, win, lose) with
  the battle's time; `toms_game --battle-log` prints each as it happens, e.g. `  2.40  player -> foe 1 (bat): hit 12`.
- Next: the status map, the monsters' HP events (§4.4), then one `Combatant` for every side (11 §4.1).

## 5. Data: what changes

| File | Change |
|---|---|
| `data/enemies.json` | the six stats, `weapon`, `moves`, `look`, `events`, `party` (with today's `hp` / `atk` / `def` read as before) |
| `data/weapon_play.json` | `results` per weapon (§4.2) |
| `data/stats.json` | `levelUp.points` written by the calculator |
| `data/battle.json` | `"pace": "cooldown"` or `"stamina"` and the stamina numbers (§6.3) |
| `data/moves.json` (new) | monster special moves |
| `media/fx/battle.particle`, `media/anim/*.anim` | the result tiers' particles, the big attacks' and the monsters' special / transform clips |
| `tools/battle_editor/setups/<name>.json` (new) | saved test setups (editor files, not tests) |

## 6. A real-time strategic battle: how to make it more fun

Today's battle is two timing games side by side: tap the attack track, tap the guard bar, while the monster attacks
on a fixed clock. It tests the player's hands, but there are few **decisions**. The ideas below add decisions without
turning it into a menu-driven turn battle: the battle stays real time, but the player always has information to act
on and a choice to make. The demo ([battle_demo.html](battle_editor/battle_demo.html)) plays ideas 1–4.

**1. The monster shows its next move (intent).** Over the monster: an icon and a filling ring, *"Heavy blow in 2.4 s"*,
*"Quick jabs"*, *"Charging a spell"*, *"Guarding"*. The player sees what is coming and decides: attack now, or guard,
or wait. This one idea makes the clock a puzzle instead of a surprise.

**2. Answer the move, not just the clock.** Each move has a right answer:
- a **heavy blow**: a perfect guard **parries** it (no damage, the monster is staggered for 1.5 s and takes double
  damage);
- **quick jabs**: guarding blocks them, but a combo during them is interrupted;
- **a charging spell**: a combo that matches enough points **breaks** it (the spell is cancelled);
- **guarding**: hits do little, but a full charge **breaks the guard**.

**3. A stamina (or focus) bar.** Attacking and guarding both cost it; it refills while you wait. The player cannot do
everything: spend it on a combo now, or keep enough for the parry the intent says is coming.
**Both, switchable** (the owner, 2026-10-25): the battle's **pace** is `"cooldown"` (today: each bar waits after a
tap) or `"stamina"` (no waits; everything costs stamina) in `data/battle.json`; a weapon (`weapon_play.json`) or a
battle setup can override it. The editor has the switch on its battle bar, so the two can be compared on the same
fight.

**4. Break (stagger).** Matched points fill the monster's break gauge; when it fills, the monster is stunned for a few
seconds and every hit is worth more. Bosses have a large gauge: a fight has a rhythm (pressure, break, burst).

**5. Weapons with a role.** The bow hits from range: it can shoot during the monster's wind-up without being hit. The
sword's perfect parries. The staff's full charge breaks guards and spells. Choosing the weapon for the monster
becomes part of the strategy (the weapon trial already teaches them).

**6. Skills on cooldown, chosen before the battle.** Two or three slots (the skill tree's actives): *a shield for the
next hit*, *slow time for 3 s*, *a free perfect*. Real time, but when to use them is a decision.

**7. Weak points and elements.** The monster card shows its weakness (fire, ice, pierce); the right weapon or skill
does more. Collecting a card teaches its weakness: the card collection feeds the strategy.

**8. Phases and events (§4.4).** At 50% the golem's runes blaze (faster attacks, new move); at 20% it becomes the
core. A boss fight tells a small story and changes the answer the player needs.

**9. Risk and reward.** *Greed*: keep the combo going past the last point for a multiplier, but a miss loses it all.
*Perfect chain*: perfects in a row raise the next reward tier (§4.2's results).

**10. Feel.** Hit stop (a 60 ms freeze on a heavy hit), screen shake, the damage number's size by tier, the monster
flinching, a sound per tier. Cheap, and they make every hit feel earned.

**Priority (agreed 2026-10-25):** 1 + 2 + 4 first (intent, answers, break), with 3 as the switchable pace; then 5 and 6
with the weapons, then 7–9 with the cards and bosses. 10 alongside everything.

## 7. Order of work

1. **The battle core:** the battle events and the status map ([11 §6](11_BATTLE_DATA.md) step 1), one `Combatant`
   for the player and every monster (11 §4.1), and **one to three monsters** with targeting (§4.6).
2. **Intent, answers, break** (§6.1, 6.2, 6.4) and the **pace switch** (cooldown / stamina, §6.3), with their UI in
   both orientations.
3. **`battle_editor.exe`:** its frame (the monster list with **+ Battle**, the viewport running a setup, ▶ Battle /
   Restart, the pace switch, the log), then the panels: Player (with the calculator), Weapon (with results), Monster,
   Events.
4. **The art** the settings name (§4.2's list), and the intents' icons.

## 8. The owner's answers (2026-10-25)

| # | Question | Answer |
|---|---|---|
| B1 | The editor in `toms_editor`? | **No: its own program** (`battle_editor.exe`, §2) |
| B2 | Which battle ideas first? | **1 + 2 + 4** (intent, answers, break) |
| B3 | Stamina, or today's cooldowns? | **Both, switchable** (the pace, §6.3) |
| B4 | Several monsters in one battle? | **Now** (§4.6) |
| B5 | Test setups as automated tests? | **No**: editor files (`tools/battle_editor/setups/`, §2) |
