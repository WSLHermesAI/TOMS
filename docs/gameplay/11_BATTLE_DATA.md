# 11 — Battle data: stats, fighters, statuses, events and formulas (design)

> 中文版：[zh_TW/11_BATTLE_DATA.md](zh_TW/11_BATTLE_DATA.md)

**Status:** design, 2026-10-24. The owner's four answers are in §2. Built so far: the attribute points by level (§3.2).
Everything in §4 is the plan, not code yet.

The owner's goal: every battle reads from one structure, so the player and monsters share stats, a fighter's
statuses drive its animation and sound, a battle event system changes how a battle runs, and a story can stage any
fight (player vs monster, monster vs monster, NPC vs monster). The feel to aim for: **Dragon Quest**.

## 1. What exists today

| Part | Where | Today |
|---|---|---|
| Stats | [stats.json](../../assets/data/stats.json) | STR, DEX, AGI, VIT, INT, LUK, each with effects: STR → ATK, DEX → hit zone, AGI → shorter wait, VIT → max HP, INT → skill power, LUK → gold. **Player only.** |
| Level | stats.json `levelUp` | `lv × 30` EXP a level; each level: +2 ATK, +1 DEF, +10 HP, +1 to each stat (LUK +0), **and attribute points** (§3.2); top level 99 |
| Abilities | skills.json, actives.json, weapon_play.json | 7 skills (flat ATK / DEF), 2 actives (sure crit, survive a lethal hit), weapon levels (combo points 3→5, stat bonuses, particles) |
| Monsters | [enemies.json](../../assets/data/enemies.json) | 67 entries: `hp, atk, def, exp, gold, atk_interval_ms, boss, sprite`. **No stats, weapons, special moves or sounds.** |
| Statuses → looks | game_combat.cpp | player: idle / charge / attack / miss (clips from the weapon's `battleAnim`); monster: idle / attack / hurt / die (`monster_battle.anim`). Picked with `if`s in `updateBattleRig` / `updateMonsterRig`. Sounds are written in code (`player_attack`, `enemy_attack`). The player has no hurt or die. |
| Events | event_bus.h | `EnemyDefeated` only; the clips' `hit` / `release` events are read and thrown away |
| Who fights | `CombatState` | always the player against one `EnemyInst` |
| Formulas | game_combat.cpp, power_bar.h | in C++: `max(1, ATK − DEF) × combo power × charge`; guard reduces the hit; no crit, dodge or hit chance |

[combat.json](../../assets/data/combat.json) is an old auto-battle formula that no code reads; it should be deleted.

## 2. The owner's decisions (2026-10-24)

1. **Monsters have the same six stats as the player.** One stat list for everyone, so every formula works both ways.
2. **Monster vs monster (or NPC vs monster) is a story event.** It is watched. The player can take control only at
   moments the event sets: "after the 3rd attack", after a dialogue, or when an in-game event shows a dialogue.
3. **Formulas: undecided.** Free expressions or fixed formulas with tunable numbers is an open task (§5). The target
   feel is Dragon Quest: damage from attack against defence with a small random spread, critical hits ("會心一擊"),
   dodges and turn order from agility.
4. **No ability per level.** A simple table gives each level N attribute points. The player places them in the menu
   and **Confirm** saves them to the save file. **Built**, see §3.2.

## 3. Built

### 3.1 Data-driven files (2026-10-24)

Every battle file has a key ([docs/engine/12_RESOURCE_MANAGER.md](../engine/12_RESOURCE_MANAGER.md) §4.3b). Weapon
particles are picked by level from `weapon_play.json` `fx` (`media/fx/battle.particle`).

### 3.2 Attribute points by level

```jsonc
// stats.json
"levelUp": { "expPerLevel": 30, "atk": 2, "def": 1, "maxhp": 10, "maxLevel": 99,
             "points": [ { "from": 2, "points": 3 }, { "from": 20, "points": 4 }, { "from": 50, "points": 5 } ] }
```

- Each level reached gives the points of the row with the highest `from` at or below it.
- On the Status tab, **+ / −** (or Enter) move points in a draft: the row shows `15 → 17`, nothing counts yet.
  **Confirm** applies them and saves the run at once. **Reset**, closing the menu or loading a save drops the draft.
- Details: [03_PLAYER_MENU.md](03_PLAYER_MENU.md) (attributes).
- **Open (balance):** each level still also gives +1 to every stat (`perLevel` in stats.json). With the points on top,
  growth is about twice what it was. Set `perLevel` to 0 for points-only growth.

## 4. The plan

### 4.1 One `Combatant` for everyone

```jsonc
// enemies.json
"slime": {
  "stats":  { "hp": 24, "str": 4, "def": 2, "dex": 3, "agi": 2, "vit": 3, "int": 1, "luk": 1 },
  "weapon": "claw",            // its attack style and timing (weapon_play.json)
  "moves":  ["slime_split"],   // special moves (moves.json)
  "look":   "slime",           // looks.json: the status map (4.2)
  "ai":     "basic"
}
```

The player is built into the same `Combatant` from the save, the level and the gear. The battle runs on combatants and
never asks "is this the player?" except for who reads input.

### 4.2 The status map ("look"): animation, sound and particles per status

```jsonc
// looks.json
"archer": {
  "anim": "anim/archer_bow.anim", "held": "anim/weapon_bow.anim",
  "states": {
    "idle":   { "clip": "idle" },
    "charge": { "clip": "charge", "sound": "bow_draw", "fx": "charge_aura" },
    "attack": { "clip": "attack", "sound": "bow_shot" },
    "hurt":   { "clip": "hurt",   "sound": "player_hurt", "fx": "hit_red" },
    "guard":  { "clip": "guard",  "sound": "shield" },
    "die":    { "clip": "die",    "sound": "player_down" }
  }
}
```

The statuses come from the battle (4.3), never from `if`s in the drawing code. The weapons' `fx` by level (3.1) becomes
part of this map.

### 4.3 Battle events

```
BattleController (rules only)
  emits:  BattleStart  TurnStart{who}  AttackStart{who, target, move}  HitLanded{target, dmg, crit}
          Missed  Guarded  ComboPoint{n}  StateChanged{who, state}  Died{who}  BattleEnd{winner}
      │
      ▼
BattleEventBus ──► presentation: looks.json[state] → clip + sound + particles
      ▲        ──► story: "after the 3rd attack, show a dialogue / give the player control" (4.4)
      └── clip events: the "hit" frame tells the controller when the damage lands (replaces hit_number_delay_ms)
```

The controller never plays a sound or a clip. A story script listens to the same events and sends its own commands
(pause, show a dialogue, hand over control, change a fighter's AI, end the battle).

### 4.4 Who fights whom: the battle setup

```jsonc
// in an event (the event editor) or a stage object
"battle": {
  "left":  [ { "who": "npc:elder",     "ai": "basic" } ],
  "right": [ { "who": "monster:wolf" } ],
  "control": [
    { "when": "attacks:3",        "give": "left" },     // after the 3rd attack the player controls the elder
    { "when": "dialogue:ch2_cry", "give": "left" }       // or after this dialogue
  ]
}
```

Without `control`, a fight with no `player` in it is watched from start to end. Every side not controlled by the player
runs its `ai`.

### 4.5 Parameters and formulas

All the numbers a battle uses move out of C++ into `battle.json` (or a new `battle_rules.json`): the damage, the
guard, the wait between attacks (AGI), crit and dodge (LUK, DEX, AGI), the random spread. The form is undecided (§5).

## 5. Open tasks

| # | Task | Notes |
|---|---|---|
| O1 | **The form of the formulas** | free expressions (`"max(1, a.atk - t.def / 2) * rand(0.9, 1.1)"`, a small evaluator with a fixed list of variables) or fixed Dragon Quest-style formulas with numbers to tune. Decide before 4.5. |
| O2 | Dragon Quest feel | reference: damage ≈ (ATK / 2 − DEF / 4) with a ±1/16 spread, a crit about 1/32 that ignores defence, dodge and order from agility. To check against the bars and the combo. |
| O3 | `perLevel` with the points | keep both, or points only (3.2) |
| O4 | Delete `combat.json` | read by nothing |

## 6. Order of work (proposal)

1. **Battle events (4.3)** and **the status map (4.2)**: they change how the battle works and what it shows. The
   presentation moves to the events first; the rules stay as they are.
2. **`Combatant` (4.1)**: monsters get the six stats (from today's hp/atk/def, so their fights stay the same).
3. **The battle setup (4.4)**: monster vs monster, NPC vs monster, control handed over by the story.
4. **Formulas (4.5)** once O1 is decided.
