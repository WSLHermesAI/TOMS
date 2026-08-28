# 巫師之塔 (Tower of the Sorcerer) — Game Design Document

> **Scope of this document.** `README.md`, `FIGHTING_TALKING_DESIGN.md`, `NODE_SYSTEM.md`, and
> `WORKING_LOG.md` already cover *how the engine is built*. This document covers *what the game
> is* — vision, pillars, systems in full, and the design areas that existed only as implicit
> choices in code/data (art direction, UX flow, audio, difficulty philosophy, scope/roadmap).
> Everything under **"Established"** headings is transcribed from the current `data/*.json` and
> existing docs (verified against source, not paraphrased from memory). Everything under
> **"Proposed"** headings is new — written to fill a gap the existing project left open — and is
> flagged as such so it's clear what's already load-bearing vs. what's a recommendation to review.
> Genre framing draws on public design analysis of *Mota* (魔塔, 1996, the genre's originator) and
> *Tower of the Sorcerer* (1998) — see **Sources** at the end.

---

## 1. Vision

A short, hand-crafted **deterministic** dungeon-climber in the *Mota / 魔塔* tradition: an
apprentice sorcerer climbs a 10-floor tower, fighting fixed monsters, collecting fixed items, and
rescuing a princess from the wizard who sealed the kingdom's light. There is no RNG anywhere in
combat or drops — every floor is a solvable puzzle of "do I have enough ATK/DEF/HP to take this
route, and in what order." The game rewards **route-planning and arithmetic**, not reflexes: the
player never presses an attack button — walking into a monster *is* the commitment, and the
outcome is knowable in advance from the numbers on screen.

This project's existing implementation already delivers the classic loop (11 hand-authored
floors, 7 monster types + 1 boss, a full item/shop economy, an NPC dialogue layer, deterministic
auto-combat). This document exists to make the *design intent* behind those numbers explicit and
to specify the pieces that were never written down: what the game should feel like moment to
moment, how the UI should communicate the deterministic-math promise, and what "done" looks like.

## 2. Design Pillars

1. **Perfect information, zero randomness.** Every stat the player needs to plan a route — own
   ATK/DEF/HP, every enemy's ATK/DEF/HP/EXP/gold, every item's effect — is visible before the
   player commits. If a fight is unwinnable, the player should be able to *know that from the
   numbers*, not discover it by dying. This is the genre's defining trait and the project's
   `combat.json` formula already guarantees it: outcomes are pure functions of stats, not seeded
   rolls.
2. **The tower is a puzzle, not a gauntlet.** Floors are solved by *sequencing* — which door to
   open first, which gem to eat before which fight, when to backtrack for a stat pickup you
   skipped — not by grinding. A floor that can only be solved by leaving and re-entering with more
   levels is a design smell, not intended difficulty.
3. **Losing costs time, not progress.** Death returns the player to the current floor's entrance
   with all permanent stats intact (`combat.json: "lose"`). Failure is a puzzle hint ("this route
   needs more ATK first"), never a setback to a save file.
4. **Every screen answers "what do I do next."** Dialogue, HUD, and store all exist to keep the
   player's plan legible — NPC hints (`skeleton_scholar` telling the player Vorkath's weakness is
   stacked gems, `sorcerer_teacher` tutorializing the fight loop) are core UX, not flavor.
5. **Short, complete, replayable.** 10 core floors + 1 epilogue is a 1–2 hour experience by
   design, not a truncated MMO-scale climb — see §12 for the floor-count rationale pulled from the
   genre's own history.

## 3. Genre & Inspiration

The project sits squarely in the **魔塔 (Mota)** lineage: *Mota* (1996) established "fixed-value
strategy RPG" — a hero climbs floors of a tower, enemy stats and positions are fixed and fully
visible, and the fight formula is deterministic:

```
hits_to_kill   = ceil(enemy.hp / max(1, player.atk - enemy.def))
damage_taken   = hits_to_kill * max(1, enemy.atk - player.def)
```

*Tower of the Sorcerer* (1998, Watanabe Nao) is the more elaborate, story-driven descendant most
players outside China know it by — "a puzzle game disguised as an RPG," map layout fixed, enemy
stats fully revealed, no randomization in combat or leveling, structured around fight → key →
door → next fight. This project's title (*Tower of the Sorcerer*, 巫師之塔) and cursed-tower /
imprisoned-princess premise are a direct homage to that game specifically, layered onto the
`combat.json` formula that is *Mota's* original fixed-value system almost verbatim (see §3 of
`FIGHTING_TALKING_DESIGN.md`).

**What this project deliberately does NOT borrow (yet — see §16 Roadmap):** *Tower of the
Sorcerer*'s signature "hidden dark walls" (invisible walls gating secret rooms, discovered by
bumping into open-looking floor tiles) is a well-known genre puzzle hook that rewards
thoroughness. The current 11 stages don't use hidden walls — every passable tile is genuinely
passable. This is a legitimate simplification for a first release (it keeps every floor solvable
by reading the map alone) but is worth flagging as a intentionally-cut classic mechanic rather
than an oversight.

## 4. Target Audience & Platform

- **Audience:** players who enjoy logic puzzles, spreadsheet-style optimization, and short
  turn-based RPGs — the same audience as *2048*, *Baba Is You*, or mobile "idle RPG" stat-planning
  games. Traditional-Chinese-literate audience primarily (all in-game text is Traditional
  Chinese), with the genre itself being most recognized in Chinese-speaking gaming culture.
- **Session shape:** designed to be finishable in a single 1–2 hour sitting, or in short 5–10
  minute floor-by-floor sessions since progress is never lost to death.
- **Platforms (established):** the engine is Vulkan C++, headless-renderable, cross-platform
  Windows + Linux, with a WebGL/WebGPU browser build (`web/`, `web-gl/`, `web-gpu/`,
  `build_web.sh`) — so **desktop + browser** are both first-class targets already. No touch/mobile
  input layer exists yet (**Proposed**, §16).

## 5. Story & World *(Established — `data/story.json`)*

**Title:** Tower of the Sorcerer / 魔法塔 — 巫師之塔

**Premise:** Ten years ago the Dark Sorcerer King **Vorkath (沃卡司)** sealed the **Star of
Peace/Stability (安穩之星)** — the kingdom's life source — with a forbidden curse, and gave the
only counter-curse key to **Princess Liora (莉歐拉)**, whom he then imprisoned atop the tower.
Since then the land has withered and monsters roam free. The player is a young apprentice
sorcerer who must climb the ten-floor Wizard's Tower, defeat Vorkath, rescue the princess, and
reignite the Star of Peace.

**10-beat story arc (maps 1:1 to floors 1–10):**

| Floor | Beat | Location | Story function |
|---|---|---|---|
| 1 | Prologue | 村莊外緣 Village Outskirts | Elder explains the calamity's origin |
| 2 | Training | 森林小徑 Forest Path | Old sorcerer teaches auto-round combat |
| 3 | Key-taking | 門樓 Gatehouse | Defeat the stone golem for the first key |
| 4 | Seeking knowledge | 藏書閣 Library | Skeleton scholar reveals Vorkath's weakness |
| 5 | Words of the dead | 墓穴 Crypt | Villager ghosts confirm the princess still lives |
| 6 | Trial | 洞窟 Cavern | Obtain attack/defense gems to strengthen yourself |
| 7 | The king's aid | 軍營 Barracks | King's lieutenant grants a blessing (potion_blue) |
| 8 | Sanctuary | 神殿 Sanctum | Princess's handmaiden reveals the top-floor mechanism |
| 9 | Antechamber of the end | 王座前室 Throne Antechamber | Princess personally entrusts her hope |
| 10 | Final battle | 巫師王座 Sorcerer's Throne | Battle against Vorkath |
| 11 (epilogue) | Resolution | 安穩之星 Star of Peace | Princess rescued, Star reignited, kingdom saved |

**Final boss:** Vorkath, floor 10, HP 400 / ATK 34 / DEF 14 — "he is the source of the seal;
without defeating him the Star of Peace can never reignite." His boss taunt: *"凡人，也敢踏足吾之
王座？安穩之星將永世沉睡！"* ("Mortal, you dare set foot in my throne? The Star of Peace shall
sleep for eternity!")

**Epilogue** (`princess_victory.json`): *"You did it… Vorkath is gone, the Star of Peace burns
again. The kingdom greets its dawn."* → *"Take me back to the palace. From now on, the Magic
Tower is no longer a place of fear, but a tower of protection."*

### Character voices *(Established — `data/dialogue/*.json`)*

| NPC | Role | Signature line / function |
|---|---|---|
| 村民長老 Villager Elder | Quest-giver, floor 1 | Sets up the premise, tells player to climb and grow stronger |
| 巫師導師 Sorcerer Teacher | Tutorial, floor 2 | Explicitly explains the auto-battle mechanic; advises eating ATK gems before high-DEF enemies |
| 骷髏學者 Skeleton Scholar | Lore/hint, floor 4 | Gives the explicit boss counter-strategy: stack gems until Vorkath's high DEF "becomes paper" |
| 幽魂村民 Ghost Villager | Lore, floor 5 | Confirms the princess is alive |
| 王之副官 King's Lieutenant | Gift-giver, floor 7 | Grants `potion_blue` via dialogue `action.give` |
| 侍女 Handmaiden | Lore, floor 8 | Foreshadows the top-floor mechanism |
| 公主莉歐拉 Princess Liora | Story climax, floor 9 | Entrusts her hope directly to the player |
| 公主（勝利）Princess (Victory) | Epilogue, floor 11 | Closes the arc |

Enemy "dialogue" is a one-line flavor beat immediately before auto-combat resolves (e.g. slime
"咕嚕…", golem "此地不容活物", skeleton "加入我們的行列吧…", wraith "仇恨…永不消散…") — this is the
*only* place monsters get characterization, since combat itself has no player input.

## 6. Core Gameplay Loop *(Established, with UX framing — Proposed)*

```
 ┌─> Read the floor (HUD shows own stats; map shows every enemy/item/door in view)
 │        │
 │        v
 │   Plan a route (which items to grab first, which monsters are safe to fight now)
 │        │
 │        v
 │   Walk into a tile
 │        │
 │        ├─ Monster tile ──> Auto-combat resolves (§7) ──> win: EXP+gold, tile clears
 │        │                                             └─> lose: return to floor entrance, stats kept
 │        ├─ Item tile ─────> Instant pickup + effect (§9)
 │        ├─ Door tile ─────> Consumes matching key, or blocks if missing
 │        ├─ NPC tile ──────> Dialogue tree opens (§8)
 │        └─ Stairs tile ──> Floor transitions via connect.up / connect.down
 │
 └──── Loop continues until floor is clear enough to reach the stairs up
```

The loop never branches into a different *mode* — there's no separate "explore" vs. "battle"
screen transition beyond the auto-combat overlay (`docs` screenshot: HUD stays visible, showing
stage name, ATK/DEF/LV/EXP/keys/HP throughout). This is deliberate: keeping the full stat picture
on screen during combat is what lets the player pre-verify a fight's outcome instead of just
reacting to it.

## 7. Combat System *(Established — `data/combat.json`, `FIGHTING_TALKING_DESIGN.md` §1)*

> **Update:** the passive "auto-punch every 700ms" round described below is proposed to become an
> active, skill-timed exchange — see [`FIGHT_SCENE_DESIGN.md`](FIGHT_SCENE_DESIGN.md) (the
> Attack/Defense Power Bar minigame) and [`MAIN_BATTLE_SCENE_DESIGN.md`](MAIN_BATTLE_SCENE_DESIGN.md)
> (full battle-scene flow + a new Equipment System). Both build directly on the `base_hit`/
> `incoming` formulas below rather than replacing them, and preserve Pillar 1's "no randomness"
> promise — the added variance is player-timed, not seeded. This section is kept as-is as the
> baseline formula reference.

**Trigger:** player walks onto a monster tile → enters `CombatState`. No manual input; both sides
auto-attack in alternating rounds every `round_time_ms` (700ms).

**Formula (deterministic, no randomness):**
```
dmg_to_target = max(1, attacker.atk - target.def)
hits_to_kill  = ceil(target.hp / dmg_to_target)
damage_taken  = (hits_to_kill - 1) * max(1, target.atk - attacker.def)
```
- Player always strikes first each combat; the enemy retaliates after every player hit except the
  killing blow — so `damage_taken` uses `hits_to_kill - 1`, not `hits_to_kill`.
- Damage floors at 1 in both directions — a fight is never literally unwinnable/unlosable by
  stalemate, and an attacker with lower ATK than the target's DEF still chips away (just slowly).
- If `target.atk <= attacker.def`, the attacker takes zero damage — this is the state the player
  is chasing via gems: stack enough DEF and even the boss becomes a free kill.

**Player base stats** (`combat.json: player_base`): HP 120, ATK 12, DEF 4.

**Win:** enemy tile clears permanently, player gains `exp` + `gold`; EXP crossing a threshold
triggers an automatic level-up (`atk += 2`, `def += 1` per `FIGHTING_TALKING_DESIGN.md` §1.4).
**Lose:** HP hits 0 → respawn at the current floor's entrance with all stats retained (no
permadeath, no floor reset of items already collected).

### Proposed: explicit level-up curve

The existing docs state *that* EXP crossing a threshold levels the player up, but no source file
specifies the threshold(s) — this is a genuine gap. A curve consistent with the bestiary's EXP
values (6 to 40 per kill, climbing to a floor-10 boss fight) and the fixed `atk+=2/def+=1` step:

| Level | EXP required (cumulative) | Rationale |
|---|---|---|
| 1→2 | 30 | Clearable from floor 1's slime+bat alone (6+7=13) plus one item pickup — keeps the floor-2 tutorial fight winnable even for a player who explored minimally |
| 2→3 | 70 | Reachable partway through floor 3 |
| 3→4 | 130 | ~floor 4–5 |
| 4→5 | 220 | ~floor 6 |
| 5→6 | 340 | ~floor 7–8 |
| 6→7 | 500 | ~floor 9, priming for the boss |

This is a **recommendation to validate against the actual per-floor monster/item placements**
(the balance checklist in `FIGHTING_TALKING_DESIGN.md` §4 already flags "boss must be beatable
under typical growth" as an open checkbox) — treat the numbers above as a starting point for that
verification pass, not as committed data.

## 8. Talking / Dialogue System *(Established — `FIGHTING_TALKING_DESIGN.md` §2)*

Each NPC/enemy has one dialogue-tree JSON:
```json
{
  "npcId": "sorcerer_teacher",
  "start": "root",
  "nodes": {
    "root": { "text": "...", "choices": [ { "label": "...", "next": "tip" }, { "label": "懂了。", "next": null } ] },
    "tip":  { "text": "先吃攻擊寶石再打高防敵人。", "choices": [ { "label": "記住了。", "next": null } ] }
  }
}
```
`start` names the entry node; `next: null` ends the conversation; nodes optionally carry
`requires` (a numeric gate on whether a choice appears) and `action` (`give` an item, open a door,
set a flag). A more general, engine-agnostic version of this same data-driven model (with speaker
portraits and conditional branching) is already speced separately in
`MagicSrc/Doc/TalkingSystem/TalkingSystemPlan.md` — the two are architecturally compatible; TOMS's
`dialogue/*.json` is the simpler subset actually implemented.

## 9. Items & Economy *(Established — `data/items.json`, `data/store.json`)*

### Field items (found on floor tiles)

| Item | Effect | Notes |
|---|---|---|
| 攻擊寶石 gem_atk | ATK +6 permanent | Core power spike; the boss counter-strategy hinges on stacking these |
| 防禦寶石 gem_def | DEF +5 permanent | Same, for survivability |
| 紅血瓶 potion_red | HP +40 instant | |
| 藍血瓶 potion_blue | HP +80 instant | Also the King's Lieutenant's gift item |
| 經驗水晶 exp_up | EXP +100 instant | "enough to level up" per its own description |
| 傳送卷軸 scroll | Warp directly to `stage03` | Shortcut / sequence-break tool |
| 金幣 coin | Gold +10 | |
| 黃/藍/紅鑰匙 key_yellow/blue/red | Unlocks matching door | Keys are consumed on use; doors gate progress, not just loot |

### Shop *(Established)*

Card-grid layout, unlocked from floor 1, currency = gold. Three items, all starting at cost 2 and
**doubling per purchase** (`cost_base * cost_multiplier^purchases` → 2, 4, 8, 16, 32…):

| Shop item | Effect |
|---|---|
| 生命藥水 potion_hp | HP +40 |
| 力量藥水 potion_str | STR (ATK) +1 permanent |
| 防禦藥水 potion_def | DEF +1 permanent |

The doubling curve is a deliberate anti-grind lever: early purchases are cheap top-ups, but
gold-farming a specific stat past a few purchases becomes exponentially inefficient, which pushes
the player back toward *route planning through field items* (fixed, one-time, "free") rather than
*grinding gold* as the primary power source — consistent with Pillar 2.

### Proposed: economy balance note

Because gold values on the bestiary (4–22 per regular kill) are an order of magnitude below the
shop's post-3rd-purchase costs (16+), the shop is best understood as a **top-up safety net for a
player who under-explored a floor**, not a primary growth path — worth stating explicitly so a
future balance pass doesn't accidentally buff gold income and flatten the "explore thoroughly"
incentive.

## 10. Bestiary *(Established — `data/enemies.json`)*

| Monster | HP | ATK | DEF | EXP | Gold | Role |
|---|---|---|---|---|---|---|
| 史萊姆 Slime | 24 | 8 | 2 | 6 | 4 | Floor-1 intro fodder |
| 暗影蝙蝠 Shadow Bat | 18 | 11 | 1 | 7 | 5 | Low HP, glass-cannon-ish ATK — teaches that low DEF ≠ low threat |
| 石巨人 Stone Golem | 70 | 14 | 8 | 20 | 12 | First DEF check — gates the first key |
| 骷髏兵 Skeleton Soldier | 40 | 16 | 4 | 14 | 9 | Mid-tier all-rounder |
| 怨靈 Wraith | 55 | 20 | 3 | 22 | 14 | High ATK spike, low DEF — punishes under-leveled HP |
| 小惡魔 Imp/Demon | 90 | 24 | 9 | 40 | 22 | Pre-boss check, largest regular EXP/gold payout |
| **Vorkath 沃卡司 (boss)** | 400 | 34 | 14 | 0 | 0 | Floor 10 final battle; 4.4× the next-strongest regular's HP |

Difficulty scales smoothly from 24 HP trash to a 400 HP final boss. The boss's DEF (14) is
explicitly the stat the game's own hint chain (skeleton scholar, floor 4) tells the player to
break via `gem_atk`/`gem_def` stacking — the bestiary numbers and the dialogue hints are
consistent with each other, which is worth preserving in any future rebalance.

## 11. Level / Floor Design *(Established structure — `data/stages/*.json`)*

Every floor is a fixed **13×11 tile grid**, defined as an ASCII map plus a legend:

```
# wall   . floor   @ player_start   U/D stairs up/down
y/b/r door:color   Y/B/R key:color
a gem_atk   d gem_def   h/H potion_red/blue   c coin
s/v/k/p/m npc:sorcerer/villager/king/princess/handmaiden
1..6 monster:slime/bat/golem/skeleton/wraith/demon   Z monster:boss
```
Each floor also carries a `story_note` (the beat's dialogue-flavor text shown on entry) and a
`connect: {up, down}` pointing at the adjacent floor's id — floors are singly linked in sequence
(no branching tower topology yet; see §16).

**11 floors total** = 10 core climb floors (1:1 with the story arc, §5) + **floor 11 as an
epilogue**, not part of the climb proper — `story.json`'s arc lists exactly 10 beats and names
floor 10 as the final boss floor, so the "climb" is explicitly a 10-floor commitment with a
victory-lap floor after.

### Proposed: level design principles (to guide new floors / a rebalance pass)

These aren't in any source file as explicit rules, but are the implicit contract the existing 11
floors already follow and that new floors should keep following:

1. **Never place an unwinnable-by-the-numbers fight as mandatory.** Every monster blocking the
   only route to the stairs must be beatable with the stats obtainable from *that floor and
   everything before it*, verifiable by hand with the combat formula (this is exactly
   `FIGHTING_TALKING_DESIGN.md` §4's open checklist item).
2. **Optional fights should out-pace mandatory ones.** A thorough player (who fights every
   optional monster, grabs every gem) should always be *ahead* of the minimum curve, so
   skipping content is a valid choice, not a trap.
3. **Keys gate progress, not just loot.** A colored door should sit on the *only* path to
   something the player needs (stairs, a stat gem cluster, an NPC hint) so the key hunt is never
   busywork.
4. **One new teaching moment per floor.** Floor 2 teaches the combat loop via dialogue; floor 3
   teaches "keys gate progress"; floor 4 teaches "NPCs hint at strategy"; floor 6 teaches "gems
   stack." Each subsequent floor should introduce at most one new *idea*, then combine
   previously-taught ideas for the rest of the climb.
5. **Hidden walls (genre-classic, currently unused — see §3) are the natural "advanced floor"
   hook** if/when the tower is extended past floor 11: they reward re-reading a floor rather than
   its layout at face value, without adding randomness.

## 12. Why 10 Floors *(Proposed — design rationale)*

The genre's most famous entry, *Tower of the Sorcerer*, runs 25 floors and is a multi-hour
commitment with dozens of hidden walls and item interactions. This project's 10-floor core climb
is a deliberate scope choice, not a truncation: it keeps every floor hand-verifiable for the
"no unwinnable fight" rule (§11.1) at design time, keeps the full story arc tight enough that
every floor can carry exactly one story beat (§5) without padding, and keeps a full playthrough
inside a single sitting (§4). If the tower is extended (§16), each new floor should still map to
a story beat or a new mechanical idea — never added purely to extend playtime.

## 13. UI / HUD Design *(Proposed — fills a real gap; existing docs describe HUD *contents*, not layout rationale)*

Per the README's own screenshots, the HUD already shows: stage name, ATK/DEF/LV/EXP, keys held,
and HP — persistently, including during combat. That choice should be treated as a hard
requirement, not an implementation detail: **the HUD's job is to make Pillar 1 (perfect
information) true at every moment**, so:

- **Always-visible, never a separate "status" menu.** If checking your own stats requires
  navigating away from the map, the player can't route-plan while looking at the floor.
- **Enemy stats visible before combat commits**, not just once combat starts — e.g. tapping/
  hovering an unfought monster tile should surface its HP/ATK/DEF so the "is this fight winnable"
  math can happen *before* the player walks into it, not only during the auto-battle replay.
- **Inventory (9-grid, per `NODE_SYSTEM.md`)** should show each held item's effect text
  (`items.json`/`store.json` already carry human-readable `desc`/`effect_text` fields for exactly
  this) rather than icon-only — the player is optimizing *when* to consume an item, which requires
  the number, not just the icon.
- **Dialogue overlay** (9-slicing panel style, per the legacy `9Slicing_TalkingDialob` asset — see
  §14) should not obscure the HUD, since hint dialogue is meant to feed directly back into the
  player's still-visible stat plan.

## 14. Art Direction *(Proposed — synthesized from `MagicTowerMedia` legacy assets + procedurally-generated current sprites)*

The current build uses 27 procedurally generated sprites (per README) rather than hand-painted
art. The earlier `MagicTowerMedia` prototype (2023, different engine, ultimately abandoned)
established a visual language worth treating as the intended target style if/when hand-authored
art replaces the procedural placeholders:

- **Top-down, single-tile-per-actor** grid perspective (not isometric) — matches the 13×11
  ASCII-map data model directly, one sprite cell = one tile.
- **Palette-limited dungeon environment set**: cave, stone floor, road, and tree/foliage tiles,
  plus distinct wall variants (plain/horizontal/vertical) — enough variety to visually distinguish
  a "forest path" floor from a "crypt" floor from a "sanctum" floor (§5's floor themes) using the
  same tile grammar.
- **Character rig**: 4-directional + idle player sprite (front/back/left/right/idle) — full
  walk-cycle coverage even though the actual game logic doesn't require directional facing
  beyond "which way did you last move," so this is a fidelity/polish investment, not a
  functional requirement.
- **Monster silhouette scaling communicates threat**: regular monster sprites read as
  small/medium, the boss sprite is visibly larger/more detailed (`Demon_Boss1.png` at ~4× the
  file size of a regular enemy sprite) — a cheap, readable way to telegraph "this is the floor-10
  fight" before the player even checks its stats.
- **UI chrome**: 9-slice dialogue box, a dedicated HUD info-bar art asset, distinct from in-world
  sprites — reinforcing §13's "HUD is always-on and separate from the map" requirement visually,
  not just functionally.

## 15. Audio Direction *(Proposed — fills a gap; only 4 SFX exist in the legacy prototype, no music anywhere)*

Established assets (`MagicTowerMedia/Sound/`): two attack-impact SFX and two floor-transition SFX
(`StoreyUp.wav`, `StoreyDown.wav`). No music track exists in either data source. Recommended
minimum audio pass, in priority order:

1. **Combat impact SFX** (already exists, port forward) — a hit sound each auto-battle round is
   the single highest-value audio cue, since combat has no other player-facing feedback beyond
   HP bars ticking.
2. **Stairs-up/down SFX** (already exists, port forward) — confirms a floor transition actually
   happened, especially relevant since floors look structurally similar (same 13×11 grid, similar
   tile palette).
3. **A single ambient tower loop**, optionally re-pitched or filtered per "floor mood" (village →
   forest → crypt → sanctum → throne) rather than 11 unique tracks — keeps scope proportional to
   an 11-floor game.
4. **A distinct boss-fight cue** for floor 10 only — the one moment in the game that deserves to
   sonically stand apart from the other 9 floors.
5. Victory/level-up stingers are optional polish, not core-loop-critical, and can be cut first
   under time pressure.

## 16. Scope, Roadmap & Future Features *(Proposed)*

**Done / in scope for v1** (all Established above): 11 floors, 7 regular monsters + 1 boss, 10
items, 3-item shop, 8-NPC dialogue layer, deterministic combat, desktop + browser builds, Qt6
stage editor for authoring new floors.

**Natural next steps, roughly prioritized:**
1. **Explicit level-up thresholds** (§7) — close the one genuinely undefined number in the core
   loop.
2. **Balance verification pass** against `FIGHTING_TALKING_DESIGN.md` §4's checklist — hand-trace
   at least one full route to confirm the floor-10 boss is beatable under typical growth, and that
   no `next` dialogue node is dangling.
3. **Touch/mobile input layer** for the WebGL/WebGPU browser build, given the browser target
   already exists but the input model is presumably desktop-first (keyboard/mouse) today.
4. **Hidden walls** (§3, §11.5) as a genre-authentic difficulty/secret-content lever for any
   floors added past 11 — deliberately deferred rather than cut for good.
5. **Branching tower topology** — today `connect.up/down` is a straight line; a floor with two
   `up` exits leading to alternate paths (both eventually reconvening) would let route-planning
   extend across floor boundaries, not just within one floor.
6. **Music pass** (§15) — currently the single largest missing production-value item.
7. **New Game+ / boss rematch** — a low-cost way to extend replay value of a deliberately short
   (§12) core game without inflating the main climb.

**Explicitly out of scope**, to protect Pillar 1: any randomized combat/drops, any timed/reflex
mechanic, any hidden stat the player can't see before committing to a fight.

## 17. Open Design Checklist

Carried forward from `FIGHTING_TALKING_DESIGN.md` §4 (still open) plus new items from this
document:

- [ ] Every route has a formula-verifiable solution (eat gems before high-DEF fights).
- [ ] Floor-10 boss (HP 400/ATK 34/DEF 14) is beatable under typical player growth.
- [ ] Combat outcomes are 100% determined by atk/def/hp — no hidden randomness anywhere.
- [ ] No dialogue `next` chain points at a non-existent node.
- [ ] **(New)** EXP level-up thresholds are defined in data, not just "a threshold exists" (§7).
- [ ] **(New)** Every mandatory fight is confirmed winnable using only stats obtainable earlier on
      that floor or before it (§11.1).
- [ ] **(New)** Shop cost curve vs. bestiary gold drops confirmed to keep gold a top-up, not a
      primary growth path (§9).

---

## Sources

Genre and comparative design research (used for §3, §12, §16 item 4):
- [Magic Tower — Strategy Fixed-Value RPG Game (Baidu Baike, EN)](https://baike.baidu.com/en/item/Magic%20Tower/1459682)
- [Tower of the Sorcerer (MobyGames)](https://www.mobygames.com/game/4594/tower-of-the-sorcerer/)
- [Tower of the Sorcerer (TV Tropes)](https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/TowerOfTheSorcerer)
- [This Promising New Puzzle RPG Pays Homage To A PC-98 Classic That Inspired A Genre (Time Extension)](https://www.timeextension.com/news/2025/04/this-promising-new-puzzle-rpg-pays-homage-to-a-pc-98-classic-that-inspired-a-genre)
- [Tower of the Sorcerer (Baidu Baike, EN)](https://baike.baidu.com/en/item/Tower%20of%20the%20Sorcerer/3329385)

Project sources (used throughout, verified directly against files):
- `TOMS/data/*.json` (combat, enemies, items, store, story, stages/, dialogue/)
- `TOMS/README.md`, `TOMS/FIGHTING_TALKING_DESIGN.md`, `TOMS/docs/NODE_SYSTEM.md`, `TOMS/docs/WORKING_LOG.md`
- `MagicTower/MagicTowerMedia/MagicTower/` (legacy 2023 prototype — art/audio direction reference only, stat data there is placeholder and was not used)
