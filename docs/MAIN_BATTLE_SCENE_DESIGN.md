# Main Battle Scene Design

> **Status:** Proposed. Describes the full battle scene end-to-end — screen flow, HUD, and how the
> new Attack/Defense Power Bar (`FIGHT_SCENE_DESIGN.md`) and a new **Equipment System** fit into a
> single round of combat. This extends `FIGHTING_TALKING_DESIGN.md` §1 and §3 and the combat
> section of `GAME_DESIGN_DOCUMENT.md` §7 — it does not replace their underlying stat model.
>
> **Live demo:** [`demos/main_battle_scene_demo.html`](demos/main_battle_scene_demo.html) — a full
> playable mockup: pick a weapon/armor/talent, pick an enemy from the bestiary, fight it round by
> round with the power bars, watch HP bars and a battle log update live. Every number shown is
> computed with the exact formulas in this document and `FIGHT_SCENE_DESIGN.md`, not scripted.

## 1. Where this fits

```
Overworld (13×11 floor grid)
   └─ player walks onto a monster tile
        └─ BATTLE SCENE  ◄──────────────── this document
             ├─ intro beat (enemy flavor line, per data/dialogue/enemy_*.json)
             ├─ round loop:
             │    ├─ Attack Bar  → damage to enemy   (FIGHT_SCENE_DESIGN.md §4)
             │    ├─ (enemy dead? → WIN, break)
             │    ├─ Defense Bar → damage to player   (FIGHT_SCENE_DESIGN.md §4)
             │    └─ (player dead? → LOSE, break)
             └─ resolution
                  ├─ WIN  → tile clears, +exp/+gold, EXP threshold may level up
                  └─ LOSE → respawn at floor entrance, stats retained (unchanged from today)
   └─ back to overworld
```

Nothing about *when* combat triggers or what happens after it (item drops, floor-clear state,
death/respawn) changes — only what happens **during** the fight itself.

## 2. Screen layout

Per `GAME_DESIGN_DOCUMENT.md` §13 (UI/HUD Design), the HUD (stage name, ATK/DEF/LV/EXP/keys/HP)
stays on screen throughout combat — this document adds the battle-specific layer on top of it,
not in place of it:

- **Top:** unchanged persistent HUD.
- **Center:** player sprite (left) vs. enemy sprite (right), both with a visible HP bar overhead —
  this part is unchanged from the existing fight-scene screenshot in `README.md`.
- **Bottom-center:** the **active Power Bar** for whichever side is currently acting (Attack Bar
  first each round, Defense Bar second) — replaces the old passive "flashes red" hit animation
  with the interactive bar described in `FIGHT_SCENE_DESIGN.md`.
- **Bottom:** narrative/flavor line area (unchanged — this is where enemy dialogue lines like
  golem's "此地不容活物" already render per `FIGHTING_TALKING_DESIGN.md`).

Only one bar is interactive at a time — the player is never asked to split attention between two
timing inputs simultaneously, keeping the skill-check readable.

## 3. Round flow (detailed)

1. Enemy tile entered → combat scene opens, enemy flavor line plays.
2. **Attack Bar appears**, already zoned (§2 of `FIGHT_SCENE_DESIGN.md`); player holds and
   releases.
3. Damage applied to enemy HP. If enemy HP ≤ 0 → **WIN**, skip to resolution.
4. **Defense Bar appears** (Attack Bar hides). Player holds and releases.
5. Damage applied to player HP. If player HP ≤ 0 → **LOSE**, skip to resolution.
6. Otherwise, loop back to step 2 for another round — no fixed round-time-ms wait anymore, the
   *player's* press/release cadence paces the fight instead of a timer (though a short forced
   pause between a release and the next bar appearing, e.g. 200–300ms, is worth keeping so the
   result of the previous bar is readable before the next one starts).
7. **Resolution:** on WIN, same reward flow as today (`combat.json: win_reward`) — exp/gold,
   possible level-up. On LOSE, same respawn flow as today (`combat.json: lose`) — floor-entrance
   respawn, stats retained.

## 4. Equipment System

This is the "extra part" requested alongside the fight scene: a way for the player to change
their own parameters — both raw power/defense stats *and* how forgiving the Power Bars are — by
equipping gear, layered on top of the existing gem/potion stat system (`data/items.json`,
`data/store.json`), not replacing it.

### 4.1 Slots

| Slot | Governs | Bar it tunes |
|---|---|---|
| **Weapon** | flat ATK bonus + attack power ceiling (`maxMult`) | Attack Bar `V0`/`Vmax`/`rampTime` **and** `greenHalf`/`blueOuter`/`redOuter` (zone radii + bar length) |
| **Armor** | flat DEF bonus | Defense Bar `V0`/`Vmax`/`rampTime` **and** `greenHalf`/`blueOuter`/`redOuter` (zone radii + bar length) |
| **Talent** (accessory) | one passive battle-modifier | either/both bars |

Per `FIGHT_SCENE_DESIGN.md` §2, every Power Bar is symmetric around its center: green in the
middle, blue flanking it, red at the outer edges, and the needle's speed follows a cubic ease-in
(slow → speeding up → **Boost**), not a flat ramp. A weapon/armor tunes **two independent axes**:
the shape of that slow→boost curve (`V0`/`Vmax`/`rampTime` — a short `rampTime` means Boost
arrives almost immediately, a long one buys a lot of controllable time before it), and the bar's
geometry — three radii from center, `greenHalf` / `blueOuter` / `redOuter`, where `redOuter` is
also the bar's own edge (so it doubles as "how long is this bar overall"). A slow-to-boost weapon
isn't automatically an easy one, and a narrow-target weapon isn't automatically a fast-to-boost
one; the example set below deliberately couples them (long-ramp+wide+long-bar,
short-ramp+narrow+short-bar) for a clean "power vs. speed" read, but nothing stops a future item
from decoupling them (e.g. a quick-to-boost-but-wide "reckless but forgiving" weapon).

One item per slot, swappable outside combat (e.g. from the inventory panel already speced in
`NODE_SYSTEM.md`'s 9-grid UI). This mirrors `GAME_DESIGN_DOCUMENT.md` §13's requirement that item
effects be shown as readable numbers, not icon-only — every piece of gear should state its bar
parameters and stat bonus in its `desc`/`effect_text`, the same convention `items.json` already
uses.

### 4.2 The power/speed tradeoff

This is the core equipment design tension the user asked for — "power up or speed up":

- **Power-leaning gear** (e.g. War Hammer, Guardian Plate): higher flat stat bonus and/or higher
  `maxMult`/mitigation ceiling, **a long `rampTime` and low `Vmax` AND wider `greenHalf`/
  `blueOuter`/`redOuter`** — the needle stays in its slow, controllable phase for a long time and
  never gets *that* fast even at full Boost, the center target is large, *and* the bar itself is
  physically longer (bigger `redOuter`), so the player reliably lands Blue/Green with little
  practice — but the raw stat numbers underneath are what's actually carrying the damage rather
  than skillful timing.
- **Speed-leaning gear** (e.g. Twin Daggers, Swift Leather): lower flat stat bonus, but a **short
  `rampTime`, high `Vmax`, AND a narrower green/blue band on a shorter overall bar** (smaller
  `redOuter`) — Boost arrives fast and the whole bar is compact and frantic, so a skilled player
  who can consistently thread a small target *right as it accelerates* gets a *higher* ceiling than
  the power build (`maxMult` 2.2–2.6 vs. 2.0 baseline), but an average player will get caught by
  the early Boost, miss the narrow center, and bounce around Red far more often — underperforming
  the safe build.
- **Talents** are a third axis — small, situational passive effects rather than raw stat/bar
  tuning (e.g. *Focus*: stretch `rampTime` by +25% on both bars — Boost arrives later, for players
  who want consistency over ceiling; *Berserker*: raise the ceiling further at the cost of zeroing
  out Red-zone attacks entirely, a pure high-risk/high-reward pick; *Guardian*: a flat mitigation
  floor that makes the Defense Bar's worst case less punishing).

This gives the same player-facing choice as a classic "glass cannon vs. tank" build decision, but
expressed through *timing difficulty* rather than just raw numbers — a build can be strictly
higher-ceiling and still be a worse pick for a player who isn't good at the minigame yet, which is
exactly the kind of decision this project's skill-first pillar (`GAME_DESIGN_DOCUMENT.md` Pillar 1
extension, see `FIGHT_SCENE_DESIGN.md` intro) should reward.

### 4.3 Example items (proposed starting set)

| Item | Slot | Stat | Speed (`V0`/`Vmax`/`rampTime`) | Zone radii (`greenHalf`/`blueOuter`/`redOuter`) | Bar length (`2×redOuter`) | `maxMult` | Design intent |
|---|---|---|---|---|---|---|---|
| Apprentice Wand | Weapon | +0 ATK | 15/200/1.8s | 15/35/60 | 120 | 2.0 | Default geometry |
| Twin Daggers | Weapon | −2 ATK | 20/260/1.0s | 8/20/45 | 90 | 2.2 | Speed build — fast Boost, small target, short bar |
| War Hammer | Weapon | +8 ATK | 10/140/2.6s | 25/40/75 | 150 | 2.6 | Power build — slow Boost, huge target, long bar |
| Cloth Robe | Armor | +0 DEF | 18/220/1.4s | 15/35/60 | 120 | — | Default geometry |
| Guardian Plate | Armor | +5 DEF | 12/150/2.2s | 24/38/68 | 136 | — | Power/tank build |
| Swift Leather | Armor | −2 DEF | 25/280/0.9s | 7/18/42 | 84 | — | Speed build |
| Focus Talisman | Talent | — | `rampTime` ×1.25 on both bars | unchanged | unchanged | — | Consistency pick — delays Boost |
| Berserker Charm | Talent | — | +15% `maxMult`; Red-zone attacks deal 0 | unchanged | unchanged | — | High-risk pick |
| Guardian Ring | Talent | — | Defense mitigation gets a +10% flat floor | unchanged | unchanged | — | Safety-net pick |

(These are the exact presets wired into [`demos/main_battle_scene_demo.html`](demos/main_battle_scene_demo.html) — the zone-radii columns directly control how wide the green/blue/red bands are painted, and how long the whole bar is, on each track; you can watch it redraw live when you switch weapon/armor in the demo.)

These are starting values for playtesting, not final balance — tune against the same "every
mandatory fight must be winnable" checklist item already in `GAME_DESIGN_DOCUMENT.md` §17.

### 4.4 Proposed data schema (`data/equipment.json`)

Following the existing `items.json`/`store.json` convention so it's a natural extension of the
current data-driven content pipeline, not a new pattern:

```json
{
  "wand": {
    "name": "見習巫師之杖", "sprite": "wpn_wand.png", "slot": "weapon",
    "desc": "平衡之選，能量條的加速曲線與命中區半徑皆為基準值。",
    "stat": { "atk": 0 },
    "bar": { "v0": 15, "vmax": 200, "rampTime": 1.8, "greenHalf": 15, "blueOuter": 35, "redOuter": 60, "maxMult": 2.0 }
  },
  "twin_daggers": {
    "name": "雙短刀", "sprite": "wpn_daggers.png", "slot": "weapon",
    "desc": "能量條很快進入衝刺（Boost），命中區更窄、整條能量條也更短，上限更高，但基礎攻擊力較低。",
    "stat": { "atk": -2 },
    "bar": { "v0": 20, "vmax": 260, "rampTime": 1.0, "greenHalf": 8, "blueOuter": 20, "redOuter": 45, "maxMult": 2.2 }
  },
  "guardian_ring": {
    "name": "守護之戒", "sprite": "talent_guardian.png", "slot": "talent",
    "desc": "格擋減傷永遠至少 +10%。",
    "talent": "guardian"
  }
}
```

`slot` ∈ `{weapon, armor, talent}`; `bar` overrides (`v0`/`vmax`/`rampTime`/`greenHalf`/
`blueOuter`/`redOuter`/`maxMult`) feed directly into the Attack/Defense Bar parameter table in
`FIGHT_SCENE_DESIGN.md` §5 — `rampTime` is the cubic ease-in's "seconds to reach Vmax," i.e. how
long the slow → speeding-up → Boost curve takes to fully unwind; `redOuter` is both the red zone's
outer radius *and* the bar's own edge, so it also sets the bar's total length (`2×redOuter`);
`talent` is a string key the battle-scene code
switches on for the handful of special-case effects (Perfect-zone override, mitigation floor,
etc.) — deliberately kept as a small enum rather than a generic scripting hook, matching this
project's existing preference for simple, explicit JSON over embedded logic (see how
`dialogue/*.json`'s `action.give` is a fixed verb, not a script).

## 5. Worked example

Player equips **War Hammer** (+8 ATK, slower bar, `maxMult` 2.6) and **Guardian Plate** (+5 DEF,
slower bar) against 暗影蝙蝠 Shadow Bat (HP 18, ATK 11, DEF 1):

- Effective player stats: ATK 12+8=20, DEF 4+5=9.
- `base_hit = max(1, 20 - 1) = 19` → the bat dies in one hit even at minimum power
  (`0.2×19 ≈ 4` isn't enough, but landing anywhere in Blue already yields `≥19` after the
  power curve crosses 1.0× around power≈44%) — and with War Hammer's wide `greenHalf=25` band
  (half the bar's width), that's an easy, low-precision release even at a modest speed, so the
  War Hammer alone trivializes early floors. That's intended: power builds should feel strong and
  low-effort against content *below* the curve, while speed builds (Twin Daggers, `greenHalf=8` —
  a needle-thin target moving fast) are the ones meant to shine against *tight, high-DEF* fights
  later (e.g. the floor-10 boss) where the higher `maxMult` ceiling actually matters and a player
  has had a full campaign to practice landing the narrow window.
- `incoming = max(1, 11 - 9) = 2` → even a totally whiffed Defense Bar (P=0, 0% mitigation) only
  takes 2 damage; Guardian Plate's real value shows up against higher-ATK enemies (wraith 20,
  demon 24, boss 34), not this one.

This example is exactly the kind of hand-trace that should be run against every bestiary entry
(§7 checklist below) before committing final numbers.

## 6. Integration notes

- **No change to `data/combat.json`'s `player_base` or `data/enemies.json`** — this system adds a
  modifier layer (equipment stat bonuses, bar parameters) on top of those files, it doesn't
  replace them.
- **No change to the win/lose contract** (`combat.json: win_reward` / `lose`) — only *how* a round
  resolves changes, not what happens after the fight ends.
- **Engine note:** the current `main.cpp` is a scripted, headless playthrough that dumps PNG
  frames (per `README.md`) — it does not yet have a real-time input loop capable of measuring
  press-and-hold duration. Implementing this system requires adding an actual per-frame input
  poll (hold-start timestamp → release timestamp) to the interactive (non-headless) build path;
  the headless scripted demo can still exercise the *formulas* by feeding synthetic release
  percentages, exactly as the two HTML demos in `docs/demos/` do in JS.

## 7. Balance checklist additions

Extends `GAME_DESIGN_DOCUMENT.md` §17:

- [ ] Every bestiary entry hand-traced at P≈44% (the "old-parity" release point) still matches
      pre-Power-Bar balance expectations.
- [ ] Power-build gear (War Hammer/Guardian Plate) doesn't trivialize floors 1–3 so completely
      that the Power Bar timing skill-check becomes irrelevant early game.
- [ ] Speed-build gear (Twin Daggers/Swift Leather) has a real payoff by the boss fight (floor 10)
      to justify its lower floor-1 stat bonus.
- [ ] Berserker Charm's "Red deals 0" downside is actually felt in at least one realistic route
      (i.e. a player who picks it isn't just strictly better off ignoring the drawback).
- [ ] `data/equipment.json` schema validated against a real stage/inventory implementation before
      being treated as final.
