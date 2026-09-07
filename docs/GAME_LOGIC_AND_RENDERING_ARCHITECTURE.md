# Game Logic & Rendering Architecture — End-to-End Technical Specification

> **Status:** Proposed (synthesis document). This is the connective-tissue document the project
> was missing: every other doc in `docs/` specifies one slice (combat math, dialogue data, battle
> scene, roguelike proposal) but none of them lay out **every system the game needs, end to end,
> and how they hand off to each other** — from the title screen to a finished run and back again.
> This document does that. It does not introduce new gameplay numbers or replace any established
> formula; it names the systems, the objects they operate on, and the flow that wires them
> together, and it makes one concrete recommendation this project has not made yet: **which UI
> framework to build the screen-space UI on.**
>
> No source code is included anywhere in this document by design — only responsibilities, data
> ownership, state machines, and pseudocode-level formulas (matching the style already used in
> `FIGHT_SCENE_DESIGN.md` and `MAIN_BATTLE_SCENE_DESIGN.md`).
>
> **How to read the status tags:** *(Established)* = already implemented and verified in this
> repo (cited to the file/doc). *(Proposed)* = new, needed to satisfy this request, not yet built.
> *(Proposed, extends X)* = a small addition to something Established.

---

## 0. Document Map — how this relates to everything else in `docs/`

| Existing document | What it owns | This document's relationship |
|---|---|---|
| `GAME_DESIGN_DOCUMENT.md` | Vision, pillars, story beats, bestiary, items, HUD intent | Source of truth for *why*; this doc is the *how*, system by system |
| `FIGHTING_TALKING_DESIGN.md` | Baseline combat formula + dialogue JSON schema | Reused verbatim in §6 (damage) and §11 (dialogue) |
| `FIGHT_SCENE_DESIGN.md` | Attack/Defense Power Bar minigame | Reused verbatim as the Battle System's round-resolution step (§6.3) |
| `MAIN_BATTLE_SCENE_DESIGN.md` | Battle scene screen layout + Equipment System | Reused verbatim; this doc places it inside the full state machine (§4) |
| `NODE_SYSTEM.md` | Scene-graph (`Node`/`GameObject`/`SpriteNode`/`TextNode`) + render binding | Reused as the **world-space** rendering layer (§3); this doc adds the **screen-space UI framework** on top (§2) |
| `GAMEPLAY_ROGUELIKE_*.md` | A larger, branching-maze roguelike proposal (separate future direction) | This doc borrows its *system-naming discipline* (§9 "Tech Arch") but scopes everything to the **current 11-floor game plus a stage-select hub**, not the full roguelike rewrite — see §4.1 for how the two reconcile |
| `docs/BackpackDemo/` | Inventory/backpack overlay demo | Cited as the Established starting point for §12 (Inventory & Equipment System) |

---

## 1. What this document adds that didn't exist before

The user-facing gap this closes: every prior doc assumes the player is always either *walking the
map* or *in a fixed auto-battle*. Real content needs more branches than that — an enemy tile that
talks first, a mission board, a stage-select screen that can say "locked" with a reason, missions
that reset daily vs. missions that fire once ever, and a UI system capable of building all of the
above without hand-building a node tree per screen (as `NODE_SYSTEM.md` currently does for the
inventory grid). Concretely, this document adds:

1. A recommended, researched **UI framework** decision (§2).
2. A single **Game State Machine** naming every screen/mode the game can be in and how they
   transition (§4).
3. An **Entity Status System** so enemies/items/doors on a stage remember whether they've been
   fought/looted/opened, persistently (§5).
4. An **Encounter Resolution System** so walking onto an enemy/NPC tile can resolve into *direct
   battle*, *dialogue-then-choice*, *story trigger*, or *merchant* — not just always combat (§5.3).
5. A **Story System** (main-beat progression) and a separate **Mission System** (side / daily /
   one-time quests) with an explicit scheduler for resets (§7, §8).
6. A **Condition/Flag Evaluator** — one shared "can the player see/do X right now" engine reused by
   dialogue gating (already established), stage-select lock reasons, and mission prerequisites (§9).
7. A **Stage Select System** with concrete lock/new/completed badge logic (§10).
8. A full **rendering layer/draw-order model** that folds the new UI framework and new screens into
   the existing `IRenderer`/scene-graph pipeline without breaking the diagnostic split-screen
   system already in `render_iface.h` (§3.3).
9. A master **system list** (§13) and **game object catalogue** (§14) — the two tables the request
   asked for explicitly: "list all game object and game logic works together to make game run."

---

## 2. UI Framework Decision

### 2.1 Why this project needs one at all

Today, every interactive screen (`NODE_SYSTEM.md`'s inventory, the store, the dialogue box) is
built by hand: construct a `Node` tree of `SpriteNode`/`TextNode` every frame, position each slot
manually, and write bespoke hit-testing for clicks/taps in `game.cpp`. That was the right call for
one 9-grid panel. It stops scaling once the game also needs: a stage-select grid with per-tile
lock overlays and tooltips, a mission board with progress bars, an equipment-comparison panel, and
(per `MAIN_BATTLE_SCENE_DESIGN.md`) two live timing bars with hit-zone bands — each of those is
its own layout/hit-testing/state problem if built the same hand-rolled way. A UI framework buys:
widget layout, input routing (focus, hover, drag), text input where needed, and — critically for a
stats-heavy game — cheap tooltips, so `GAME_DESIGN_DOCUMENT.md` §13's "show the number, not just
the icon" requirement becomes a one-line `SetTooltip` call instead of a hand-built text node per
panel.

### 2.2 Candidates researched

| Library | UI model | Styling | Vulkan | WebGL2/WebGPU + Emscripten | License | Verdict for TOMS |
|---|---|---|---|---|---|---|
| **Dear ImGui** | Immediate-mode | Programmatic (colors/rounding/fonts); themeable, not CSS | Official backend | Official GL/WebGPU backends; **officially supported Emscripten target**, ships prebuilt web demos | MIT | **Recommended** — see §2.3 |
| **Nuklear** | Immediate-mode, single-header | Minimal, very manual | Community backend | Works via GL2/GL3 backends, portable to Emscripten | Public domain/MIT | Viable fallback if ImGui's footprint is ever a concern; less mature widget set (no built-in docking/tables/drag-drop as polished as ImGui's) |
| **RmlUi** (libRocket fork) | Retained-mode | HTML/CSS — the most designer-friendly styling of any option here | Custom render-interface only (no official Vulkan backend) | No official WebGPU/Emscripten backend; would need a from-scratch render interface for **each** of Vulkan/WebGL2/WebGPU | MIT | Attractive for visual polish, but the integration cost is 3x (one custom render-interface implementation per existing backend) with no official web/Emscripten path — too much risk for this project's multi-backend requirement |
| **CEGUI** | Retained-mode, XML-skinned | Rich (skins, layouts, animations) | Has a Vulkan renderer module | No first-class Emscripten story found | MIT | Heavier dependency (own XML parsing, font, image codec stack) largely duplicating things this project already has (`font.cpp`, `texture.cpp`, `json`); mostly chosen by older Ogre3D-era projects |
| **MyGUI** | Retained-mode, XML-skinned | Similar era/approach to CEGUI (used by OpenMW) | No official modern Vulkan renderer | No modern web/Emscripten path | LGPL | Same integration profile as CEGUI but even less actively aimed at new Vulkan/web projects; the LGPL is also a needless license complication for a project that is otherwise MIT/public-domain deps (`json`, `stb`, `miniaudio`) |
| **NoesisGUI** | Retained-mode, XAML | Best-in-class (used by shipped AAA titles) | Yes | Not a fit for a browser-first Emscripten build | **Commercial** | Ruled out — this is a hobby/open project; paid licensing isn't proportionate |

### 2.3 Recommendation: **Dear ImGui**, in a hybrid split with the existing scene-graph

**Adopt Dear ImGui for every screen-space, interactive UI surface**: main menu, stage select,
HUD overlays that need interaction (inventory, equipment, shop, mission board), dialogue box
choices, pause/settings. **Keep the existing `Node`/`SpriteNode`/`TextNode` scene-graph
(`NODE_SYSTEM.md`) exactly as-is for everything world-space**: the 13×11 tile map, walking sprites,
the battle-scene actors and their HP bars, and the fullscreen fade/transition splash. This is the
same split the research surfaced as a common real-world pattern ("some teams use a retained UI for
shipped game screens and immediate-mode for tool/overlay work") — here it's inverted in one sense
(ImGui is the *shipped* UI, not just a dev tool) because ImGui's actual widget set (buttons, grids
via `ImageButton`, drag-and-drop reordering, tables, tooltips, sliders for the Power Bar's
debug-tuning) already covers everything this game's UI needs once it's given a custom style.

**Why this wins over RmlUi/CEGUI/MyGUI specifically for *this* codebase:**

1. **Three renderer backends already exist and must all keep working** (`render_iface.h`: Vulkan
   desktop, WebGL2 browser, WebGPU browser). Dear ImGui is the only candidate with **official,
   maintained backends for all three plus Emscripten** out of the box — RmlUi/CEGUI/MyGUI would each
   need a hand-written render-interface adapter per backend, tripling the integration work for a
   worse-supported web target.
2. **The project already has its own texture/font pipeline** (`texture.cpp`, `font.cpp` with TTF +
   runtime CJK fallback glyph baking). ImGui is deliberately unopinionated about asset loading — it
   takes a font atlas texture handle and draws quads, so the existing `TextureManager` and the
   already-Established CJK-capable `Font` system slot in directly instead of being replaced by a
   second, competing font/asset stack (which CEGUI/MyGUI would otherwise bring).
3. **No embedded scripting, no XML skin files** — matches this project's explicit existing
   preference (`MAIN_BATTLE_SCENE_DESIGN.md` §4.4: "a small enum rather than a generic scripting
   hook... simple, explicit JSON over embedded logic") far better than CEGUI/MyGUI's XML-skin
   pipelines or RmlUi's HTML/CSS documents, all of which are a second data format to maintain
   alongside `data/*.json`.
4. **Traditional-Chinese text already works.** ImGui supports arbitrary glyph ranges/atlases the
   same way the existing `Font` class does — the CJK glyph-fallback work already done for the
   scene-graph's `TextNode` is directly reusable for ImGui's font atlas (build once, hand both
   consumers the same atlas texture).
5. **Slot-grid inventory/equipment UI is a well-worn ImGui pattern** — `ImGui::ImageButton` in a
   grid plus `BeginDragDropSource`/`AcceptDragDropPayload` is the standard way shipped indie RPGs
   build exactly this kind of drag-to-equip/drag-to-swap slot UI, directly satisfying the request's
   "this game needs a slot UI."

**Styling plan (so it doesn't look like a debug tool):** ImGui windows are configured with no
title bar / no default chrome (`ImGuiWindowFlags_NoDecoration`, transparent background), and the
game's existing 9-slice dialogue-panel art and HUD chrome (`GAME_DESIGN_DOCUMENT.md` §14) is drawn
**underneath** each ImGui window using the existing scene-graph `SpriteNode` 9-slice technique —
ImGui then only owns interactive layout, text, and input, while the game's own art draws every
visible panel background/border. This is why the two systems coexist instead of one replacing the
other: scene-graph = pixels and art direction, ImGui = layout, focus, drag/drop, and hit-testing.

**Integration phases (incremental, lowest-risk first):**

| Phase | Scope | Risk |
|---|---|---|
| 1 | Wire ImGui into all three backends as a **dev-only debug overlay** (stat sliders, node-filter toggle already in `render_iface.h`, log viewer) | Lowest — no shipped-screen behavior changes yet |
| 2 | Migrate **Inventory + Store** panels from hand-built `Node` trees to ImGui (these are the two screens `NODE_SYSTEM.md`/`FIGHTING_TALKING_DESIGN.md` already describe as grid/card layouts — most direct fit) | Low — behavior is already spec'd, only the construction method changes |
| 3 | Migrate **Dialogue box** (text + choice buttons) to ImGui | Low |
| 4 | Build the **new** screens directly in ImGui from day one: Stage Select (§10), Mission Board (§8), Main Menu, Pause/Settings | N/A — new content |
| 5 | Battle scene's Power Bars (`FIGHT_SCENE_DESIGN.md`) stay **scene-graph rendered** (they're precision-timed custom-drawn bars, not standard widgets) but their surrounding HUD (HP numbers, flee/item buttons) migrates to ImGui | Medium — timing-critical drawing should not go through immediate-mode retained state to avoid frame-timing surprises |

---

## 3. Rendering Architecture

### 3.1 Established foundation (unchanged by this document)

- **`IRenderer`** (`src/engine/render_iface.h`) — backend-agnostic interface; `Renderer` (Vulkan,
  desktop), `renderer_webgl` (browser), `renderer_webgpu` (browser) all implement it. All game code
  talks only to `IRenderer`.
- **`BatchRenderer`** — groups quads by texture-set/blend-mode, flushes on change; already reduces
  hundreds of quads to a handful of draw calls.
- **`Node` / `GameObject` / `SpriteNode` / `TextNode`** (`NODE_SYSTEM.md`) — the scene-graph; world
  transforms cascade parent→child; `RenderTree()` culls whole invisible subtrees for free.
- **`Texture` / `TextureManager`**, **`Font`** (TTF + runtime CJK fallback baking) — asset pipelines
  both the scene-graph and (per §2.3) the new ImGui layer will share.
- **`RenderNode` diagnostic tagging** (`render_iface.h`): each `Quad` is stamped with a subsystem id
  (`NODE_STAGE`, `NODE_CHAR`, `NODE_TALK`, `NODE_BATTLE`, `NODE_STORE`) so the existing 4-way
  split-screen debug view can isolate one subsystem's draw calls.

### 3.2 Additions this document proposes

- **Extend `RenderNode`** with `NODE_MENU`, `NODE_STAGESELECT`, `NODE_MISSION` so every *new* screen
  from §4 gets the same diagnostic isolation the existing ones have. Pure enum growth, no behavior
  change to existing tags.
- **ImGui draw pass** sits in `IRenderer::end()`'s pipeline as one more batch source: ImGui emits
  its own vertex/index buffers per its backend (GL/Vulkan/WebGPU), submitted **after** the
  scene-graph's `RenderTree()` call and **before** the final fullscreen fade overlay (see layer
  order below), so world content is always behind UI and a transition fade always wins over both.

### 3.3 Draw-order / layer model (bottom → top, every frame)

```
0. Clear
1. World layer          — StageSystem tiles, entities, player sprite      (NODE_STAGE)
2. Battle actor layer    — only while BattleState is active               (NODE_BATTLE)
3. Always-on HUD chrome  — 9-slice panel backgrounds, stat bar art        (NODE_CHAR)
4. ImGui interactive layer — inventory/equipment/shop/dialogue/stage-select/mission board
                             (NODE_STORE / NODE_TALK / NODE_STAGESELECT / NODE_MISSION)
5. Modal overlay          — FullScreenSplash (dim background behind a modal)
6. Transition/fade        — floor-change wipe, victory/defeat flash
7. Debug/diagnostic overlay — node-filter view, log/stat overlay (dev builds only)
```

Because ImGui panels are drawn with **no opaque background of their own** (§2.3), layer 3's HUD art
remains visible through/around them exactly where the scene-graph already draws it — this
preserves `GAME_DESIGN_DOCUMENT.md` §13's "HUD is always visible, never fully obscured" rule even
after the UI framework migration.

---

## 4. The Game State Machine (full flow, boot to credits)

### 4.1 Scope note — reconciling the linear game with a stage-select hub

Today the 11 floors are a **straight line** (`connect.up`/`connect.down`, one path,
`GAME_DESIGN_DOCUMENT.md` §11). The separate `GAMEPLAY_ROGUELIKE_*` docs propose a much bigger
branching-maze rewrite. This document does **neither extreme**: it adds a **Stage Select hub
screen** in front of the existing linear climb — every floor the player has ever reached becomes a
replayable, individually-selectable entry (useful for farming an earlier floor's gems, replaying
for a missed NPC hint, or just re-entering where you left off), while the climb itself stays
linear and hand-authored. This is the minimum change that satisfies the request's "select stage,
this should show what's new and what can not select" ask without committing to the larger
roguelike rewrite.

### 4.2 Top-level states

```
BOOT
  └─> TITLE / MAIN_MENU
        ├─(Continue)──────────┐
        ├─(New Game)──────────┤
        ├─(Settings)──> SETTINGS ──back──> MAIN_MENU
        └─(Quit)──> exit
                            │
                            v
                     STAGE_SELECT  ◄────────────────────────────┐
                       │  (grid of all floors; lock/new/done    │
                       │   badges computed per §10)             │
                       ├─(pick unlocked stage)                  │
                       v                                        │
                   STAGE_LOADING (StageSystem.load)              │
                       │                                        │
                       v                                        │
                    EXPLORE  ◄────────────────────────┐          │
                       │   (walk the 13×11 grid;      │          │
                       │    HUD always visible)        │          │
                       ├─ tap enemy/NPC tile ──> ENCOUNTER_RESOLVE│
                       │                            │             │
                       │      ┌─────────────────────┼──────────┐  │
                       │      v                     v          v  │
                       │  DIRECT_BATTLE        DIALOGUE     MERCHANT
                       │   (§6)                 (§11)        (shop)
                       │      │                     │          │  │
                       │      │            (choice may set a   │  │
                       │      │             flag / start a     │  │
                       │      │             mission / open     │  │
                       │      │             DIRECT_BATTLE)      │  │
                       │      v                     │          │  │
                       │  WIN / LOSE ────────────────┴──────────┘  │
                       │      │                                    │
                       ├─ tap item tile ──> instant pickup (§12)    │
                       ├─ tap door tile ──> consume key or blocked  │
                       ├─ open inventory/equipment (ImGui overlay)  │
                       ├─ open mission board (ImGui overlay)        │
                       └─ tap stairs ──> STAGE_CLEAR_CHECK          │
                                              │                     │
                                    floor fully cleared? ───no──> back to EXPLORE
                                              │yes
                                              v
                                     STAGE_COMPLETE  (rewards, mission
                                       │               progress ticks, §8)
                                       └────────────────────────────┘
                                       (return to STAGE_SELECT; if this
                                        was floor 10 → ENDING sequence)

ENDING (epilogue floor 11 per GAME_DESIGN_DOCUMENT §5) ──> CREDITS ──> MAIN_MENU
```

### 4.3 State → active systems table

| State | Active systems | UI surface |
|---|---|---|
| `TITLE` / `MAIN_MENU` | Save System (detect existing save) | ImGui menu |
| `STAGE_SELECT` | Stage Select System, Condition Evaluator, Save System | ImGui grid |
| `STAGE_LOADING` | Stage System, Entity Status System (restore per-stage state) | fade transition (scene-graph) |
| `EXPLORE` | Stage System, Entity Status System, HUD System, Input System | Scene-graph (world) + ImGui HUD chrome |
| `ENCOUNTER_RESOLVE` | Encounter Resolution System, Condition Evaluator | brief scene-graph flash / no dedicated screen |
| `DIRECT_BATTLE` | Battle/Combat System, Damage Formula, Equipment System, Progression System | Scene-graph actors + ImGui HP/HUD numbers |
| `DIALOGUE` | Dialogue System, Condition Evaluator, Story/Mission System (for `action.*`) | ImGui box |
| `MERCHANT` | Economy/Shop System, Inventory System | ImGui panel |
| `STAGE_COMPLETE` | Progression System, Mission System, Save System | ImGui summary panel |
| `ENDING` / `CREDITS` | Story System | scene-graph cutscene beats + ImGui text |

---

## 5. On-Stage Entity Status & Encounter Resolution *(Proposed)*

### 5.1 Why enemies/items need a status at all

Once floors are individually re-enterable from Stage Select (§4.1), "walk onto a monster tile"
can no longer assume the monster is always there and always fresh — the player might be
revisiting a floor they already cleared. Established behavior for *items already carries* this
implicitly (`GAME_DESIGN_DOCUMENT.md` §6: "tile clears" on win) but it was never named as a
first-class per-entity field. This system names it explicitly so Stage System, Save System, and
Stage Select can all agree on the same source of truth.

### 5.2 `StageEntityStatus` (per placed enemy/item/door/NPC instance, keyed by stage id + tile id)

| Status | Meaning | Set by |
|---|---|---|
| `Untouched` | Default; renders normally, fully interactive | Stage load, first visit |
| `Engaged` | Combat or dialogue currently open on this instance | Encounter Resolution, on tile enter |
| `Defeated` | Enemy killed; tile permanently cleared (per existing win rule) | Battle System, on WIN |
| `Collected` | Item picked up; tile permanently cleared | Item pickup handler |
| `Opened` | Door consumed its key; stays open on revisit | Door interaction handler |
| `Hidden` | Not yet visible/interactive — gated behind a story flag or mission state (§9) | Condition Evaluator, re-evaluated on stage load |
| `RespawnPending` | *(optional, off by default — see Pillar 3 conflict below)* | Not used unless a future daily-mission floor explicitly opts in |

**Persistence:** entity status is part of the **Run/Meta save** (§13's Save System) keyed by
`stageId + entityId`, so re-entering a cleared floor from Stage Select shows it already cleared —
this is what makes "what's new" on the Stage Select screen meaningful (§10) rather than every
floor always looking freshly stocked.

**Note on `RespawnPending` and Pillar 3:** `GAME_DESIGN_DOCUMENT.md` Pillar 3 ("losing costs time,
not progress") and the existing rule that a defeated enemy's tile clears *permanently* are both
about **death**, not about **replaying an already-won floor from Stage Select**. Whether a
re-entered, already-cleared floor should repopulate its regular enemies (so it's farmable again)
or stay permanently cleared (so completing the tower once is the whole point) is a genuine open
design question — flagged in the checklist (§15) rather than decided here, since it changes the
economy balance note in `GAME_DESIGN_DOCUMENT.md` §9.

### 5.3 Encounter Resolution — what happens when a tile is entered

Every enemy/NPC placement in a stage's data gets one new field, `encounterKind` (Proposed addition
to the existing stage tile legend in `GAMEPLAY_ROGUELIKE_DATA_SCHEMA.md`-style data, applied here
to `data/stages/*.json`):

| `encounterKind` | Flow | Matches the request's phrase |
|---|---|---|
| `direct_battle` (default — today's only behavior) | Tile enter → straight into `DIRECT_BATTLE` | "some enemy may reach to get into battle scene" |
| `dialogue_gate` | Tile enter → `DIALOGUE` opens first (enemy's `enemy_*.json` flavor line, per `FIGHTING_TALKING_DESIGN.md` §2.2) → player's choice resolves via the choice's `action` | "some may have story to show dialog then choice what to do and active a new story" |
| `story_trigger` | Tile enter → `DIALOGUE` only, no combat path exists at all (pure NPC/lore beat, today's established NPC behavior) | Existing NPC tiles (villager elder, scholar, etc.) |
| `merchant` | Tile enter (or HUD icon tap, already established) → `MERCHANT` | Existing shop |

**Extending the dialogue `action` verb set** (Proposed, extends the already-established
`action.give`/door/flag verbs in `FIGHTING_TALKING_DESIGN.md` §2.1) with three more explicit,
non-scripted verbs — consistent with the project's stated preference for "a small enum, not a
scripting hook":

- `action.enterBattle` — the choice resolution transitions into `DIRECT_BATTLE` against this same
  enemy instance (e.g. "Fight" after a taunt).
- `action.setStoryFlag` — writes one key into the Story System's flag set (§7), which the Condition
  Evaluator (§9) can later gate on.
- `action.startMission` — activates a `MissionDefinition` (§8) by id, moving it from `available` to
  `active` in the player's Mission Tracker.

This is the single mechanism that satisfies "then choice what to do and active a new story" — a
dialogue choice's `action.setStoryFlag`/`action.startMission` *is* "activating a new story," using
data the game already has a proven, working pattern for.

---

## 6. Battle / Combat System *(Established formula, consolidated here)*

This section does not change any number — it exists so damage calculation has one canonical
description that both the design docs and this architecture doc agree on, per the request's "must
have a system to calculate damage."

### 6.1 Base deterministic formula *(Established — `data/combat.json`, `FIGHTING_TALKING_DESIGN.md` §1.2)*

```
base_hit  = max(1, attacker.atk - target.def)      // per-hit damage, floored at 1
hits_to_kill = ceil(target.hp / base_hit)
damage_taken = (hits_to_kill - 1) * max(1, target.atk - attacker.def)
```
Player always strikes first; the enemy retaliates after every hit except the killing blow.

### 6.2 Stat inputs, by layer *(established base + proposed modifier stack)*

```
effective.atk = player_base.atk (12)
              + level_up_bonus        (+2 per level, Established — FIGHTING_TALKING_DESIGN.md §1.4)
              + gem/potion bonuses    (Established — data/items.json, data/store.json)
              + equipment flat bonus  (Proposed — MAIN_BATTLE_SCENE_DESIGN.md §4, e.g. War Hammer +8 ATK)

effective.def = player_base.def (4) + level_up_bonus + gem/potion + equipment flat bonus (same stack)
```

### 6.3 Round resolution *(Proposed active version, replacing the passive 700ms timer)*

Per `FIGHT_SCENE_DESIGN.md` — each round is two player-timed Power Bar inputs, not an automatic
timer tick:

```
power_mult(P_atk) = 0.2 + 1.8 × (P_atk/100)         [+25% if P_atk >= 97, "Perfect"]
dmg_dealt         = ceil(base_hit × power_mult)

mitigation(P_def) = P_def/100                        [100% mitigation if P_def >= 99, "Perfect Guard"]
dmg_taken         = ceil(incoming × (1 - mitigation))
```
where `base_hit`/`incoming` are computed from §6.2's **effective** stats (post-equipment), not the
raw base stats — equipment changes the numbers that feed the same unchanged formula, it never
replaces the formula. Full round pseudocode, Power Bar geometry/speed-curve math, and the
Equipment System's weapon/armor/talent slot design are specified in `FIGHT_SCENE_DESIGN.md` and
`MAIN_BATTLE_SCENE_DESIGN.md` — not repeated here.

### 6.4 Win/Lose resolution *(Established, unchanged)*

- **WIN:** enemy tile → `Defeated` status (§5.2); `+exp`/`+gold`; EXP threshold may trigger
  level-up (`atk+=2, def+=1`).
- **LOSE:** HP reaches 0 → respawn at the current floor's entrance; all stats/items/status
  retained (Pillar 3, no permadeath).

---

## 7. Story System *(Established data + Proposed controller)*

### 7.1 Established data

`data/story.json` already defines a 10-beat main arc, 1:1 with floors 1–10, each with a
`story_note` shown on floor entry (`GAME_DESIGN_DOCUMENT.md` §5).

### 7.2 Proposed: `StoryController`

A thin state owner, not a new content format:

- **`storyFlags: Set<string>`** — every flag ever set by `action.setStoryFlag` (§5.3) or by a
  built-in beat-completion event (e.g. `beat_04_scholar_hint_given`). Lives in the Meta save (§13)
  so it survives death (Pillar 3) and persists across Stage Select re-entries.
- **`currentBeat: int`** — the highest story beat reached; drives which `story_note` displays and
  (per §10) which stages Stage Select marks unlocked.
- **Beat advancement rule (unchanged from today):** entering floor *N* for the first time advances
  `currentBeat` to *N*'s beat — this is exactly what `connect.up` already does, now just also
  written into the persisted flag set instead of being implicit in "which floor am I standing on."

**Why a separate system from Mission (§8):** the main story is linear, mandatory, and has no
reset/expiry — it belongs conceptually with "which floors exist and are unlocked," not with the
optional/repeatable objectives a Mission System manages.

---

## 8. Mission System *(Proposed, new)*

Satisfies the request's "some mission is daily or only happen once, so this required a system to
control."

### 8.1 `MissionDefinition` (static content, `data/missions.json` — Proposed, follows the existing
`items.json`/`equipment.json` convention)

| Field | Meaning |
|---|---|
| `missionId` | unique id |
| `kind` | `side` \| `daily` \| `once` |
| `giver` | NPC id or `stageId` that surfaces it |
| `prerequisites` | a Condition Evaluator expression (§9) — e.g. "floor 4 reached" |
| `objective` | e.g. `{ type: "defeat", enemyId: "golem_stone", count: 1 }` or `{ type: "collect", itemId: "gem_atk", count: 3 }` |
| `reward` | `{ exp, gold, itemId? }` |
| `resetPolicy` | `none` (for `side`/`once`) \| `daily` (midnight rollover, real wall-clock — this is a short single-session game, not a live service, so a simple local-date comparison is sufficient; no server clock needed) |

### 8.2 `MissionTracker` (per-save runtime state)

| Field | Meaning |
|---|---|
| `missionId` | references the definition |
| `state` | `locked` \| `available` \| `active` \| `completed` \| `claimed` |
| `progress` | current count toward `objective.count` |
| `lastResetAt` | date the daily instance was last rolled over |

### 8.3 `MissionScheduler` (Proposed logic, runs once per session start + on Stage Select entry)

```
for each mission definition where kind == "daily":
    if tracker.lastResetAt is before today:
        tracker.state    = "available"
        tracker.progress = 0
        tracker.lastResetAt = today
```

**One-time (`once`) missions never re-run this check** — once `state == "claimed"`, that mission id
is permanently excluded from ever becoming `available` again, even across a full save wipe of the
*run* save (it lives in the **Meta** save, §13, precisely because "only happen once" must outlive
any single run/session).

### 8.4 Where progress ticks come from

Mission progress is **not** polled — it's pushed by an event (§13's Event Bus) from whichever
system produced the fact: Battle System fires `EnemyDefeated{enemyId}` on every WIN (§6.4), Item
pickup fires `ItemCollected{itemId}`, Dialogue fires `ChoiceMade{npcId, choiceId}`. The Mission
Tracker subscribes to these and increments any `active` mission whose `objective` matches — this
keeps Mission System decoupled from Battle/Item/Dialogue (none of them need to know missions
exist).

---

## 9. Condition/Flag Evaluator *(Proposed — the shared "what can the player see/do" engine)*

This is the single most-reused piece of new logic — the request's "how game logic control what to
show is important" is this system, named once and consumed everywhere.

### 9.1 Expression shape (data, not code — same philosophy as dialogue's existing `requires`)

```
{ "all": [
    { "type": "storyBeatAtLeast", "value": 4 },
    { "type": "itemHeld", "itemId": "key_blue" },
    { "any": [
        { "type": "missionComplete", "missionId": "m_scholar_favor" },
        { "type": "statAtLeast", "stat": "atk", "value": 20 }
    ]}
]}
```
Supported leaf types (Proposed, extendable): `storyBeatAtLeast`, `storyFlagSet`, `itemHeld`,
`missionComplete`, `missionActive`, `statAtLeast`, `stageCleared`. Combinators: `all`, `any`, `not`.

### 9.2 Consumers (every "what's visible/available" decision in the game routes through this)

| Consumer | Uses it for |
|---|---|
| Dialogue System (Established `requires`, reframed) | which choices appear in a node |
| Stage Select System (§10) | lock/unlock, "new" badge |
| Mission System (§8) | `prerequisites` gating `locked`→`available` |
| Encounter Resolution (§5) | whether a `Hidden` entity becomes `Untouched`/visible on stage (re)load |
| Door/Key system (Established, reframed) | already effectively `itemHeld` — folded into the same evaluator for consistency rather than special-cased |

Centralizing this means a future designer adds a new gating rule (e.g. "only after New Game+") by
adding one leaf type to the evaluator, and every consumer above gets it for free — nobody hand-rolls
their own condition-checking code.

---

## 10. Stage Select System *(Proposed)*

Directly answers: *"there is select stage, this should show what's new and what can not select
because some condition is not reached."*

### 10.1 Per-stage computed state (recomputed on entering the Stage Select screen)

| Field | Rule |
|---|---|
| `locked` | `true` unless Condition Evaluator's stage-unlock expression passes (default expression for floor *N*: `stageCleared(N-1)` — i.e. today's linear gate, expressed declaratively instead of implicitly) |
| `lockReason` | human-readable text sourced from the *first failing* leaf in the expression (e.g. "尚未擊敗第 9 層" / "Defeat floor 9 first") — so a locked tile never just looks disabled with no explanation, satisfying the same "always explain the plan" spirit as Pillar 4 |
| `isNew` | `true` if: `justUnlockedSinceLastVisit` OR `hasActiveDailyMission` OR `hasUnclaimedMissionReward` tied to this stage |
| `isCompleted` | Entity Status System: all mandatory enemies `Defeated` and stairs reached at least once |
| `preview` | recommended-stats line, pulled straight from that floor's bestiary entries (`GAME_DESIGN_DOCUMENT.md` §10) — this is what makes locked-but-visible floors still informative, not just grey boxes |
| `missionBadgeCount` | count of `active`+`available` missions whose `giver` is this stage |

### 10.2 Screen composition (ImGui, per §2.3's styling plan)

A scrollable grid of stage tiles (one per floor, image = the floor's establishing sprite/theme
color per `GAME_DESIGN_DOCUMENT.md` §14), each tile:
- Greyed + a lock icon + `lockReason` tooltip when `locked`.
- A "NEW" ribbon badge when `isNew`.
- A checkmark overlay when `isCompleted`.
- A small mission-count pip when `missionBadgeCount > 0`.
- Click → `STAGE_LOADING` (§4).

---

## 11. Dialogue System *(Established, reframed as a system with its interfaces named)*

- **Input:** `npcId` → loads `data/dialogue/<npcId>.json` (Established schema:
  `start`/`nodes`/`text`/`choices`/`requires`/`action`, `FIGHTING_TALKING_DESIGN.md` §2.1).
- **Reads:** Condition Evaluator (§9) for each choice's `requires`.
- **Writes:** Story System flags, Mission System state, Encounter Resolution transition (§5.3's
  three new `action` verbs), Inventory (existing `action.give`).
- **Renders via:** ImGui dialogue box (§2.3 Phase 3), 9-slice background drawn by the scene-graph
  underneath it (unchanged art direction, `GAME_DESIGN_DOCUMENT.md` §13/§14).

---

## 12. Inventory, Equipment & Economy Systems *(Established data, migrating UI construction per §2.3)*

| System | Established data source | What changes here |
|---|---|---|
| Inventory | `data/items.json`, 9-grid per `NODE_SYSTEM.md` | Grid construction moves from hand-built `Node` tree to ImGui `ImageButton` grid + drag-drop (§2.3 Phase 2); item effect text still sourced from the same `desc`/`effect_text` fields |
| Equipment | `data/equipment.json` (Proposed schema, `MAIN_BATTLE_SCENE_DESIGN.md` §4.4) | Three slots (weapon/armor/talent), each swap re-derives §6.2's effective stats + Power Bar parameters |
| Shop | `data/store.json`, doubling cost curve (Established) | Card-grid migrates to ImGui (§2.3 Phase 2); cost curve/logic unchanged |

---

## 13. Master System List

| # | System | Responsibility | Owns data | Talks to | Status |
|---|---|---|---|---|---|
| 1 | Renderer Service (`IRenderer`) | Backend-agnostic draw submission | GPU resources | Everything that draws | Established |
| 2 | Scene-Graph (`Node`/`GameObject`) | World-space transforms + culling | Node tree | Stage, Battle, HUD chrome | Established |
| 3 | Batch Renderer | Draw-call batching | Quad batches | Renderer Service | Established |
| 4 | Texture/Sprite Atlas Manager | Load/pack/upload textures | Texture handles | Renderer, UI framework | Established |
| 5 | Font System | TTF bake + runtime CJK fallback | Glyph atlas | Scene-graph text, ImGui | Established |
| 6 | Audio System | SFX/music playback | Audio handles | Battle, stairs, UI | Established |
| 7 | Logging/Object Lifecycle | Diagnostics, leak detection | Object registry | Everything (cross-cutting) | Established |
| 8 | Input System | Keyboard/mouse (desktop), touch/virtual gamepad (web) | Input state | Game State Machine | Established |
| 9 | **UI Framework Layer (ImGui)** | Screen-space widgets, layout, drag-drop, tooltips | Widget state | Every interactive screen | **Proposed (§2)** |
| 10 | **Event Bus** | Decoupled cross-system notifications | Subscriber lists | Mission, Notification, Stage Select | **Proposed (§8.4)** |
| 11 | **Game State Machine** | Owns which screen/mode is active | Current state | All gameplay systems | **Proposed (§4)** |
| 12 | Stage System | Load floor tile grid + entity placements | `data/stages/*.json` | Entity Status, Encounter Resolution | Established |
| 13 | **Entity Status System** | Per-tile runtime status, persisted | `StageEntityStatus` map | Stage, Save, Stage Select | **Proposed (§5)** |
| 14 | **Encounter Resolution System** | Decide battle vs. dialogue vs. story vs. merchant | `encounterKind` per tile | Battle, Dialogue, Story | **Proposed (§5.3)** |
| 15 | Battle/Combat System | Round loop, Power Bar, resolution | `CombatState` | Damage Formula, Equipment, Progression | Established + Proposed (Power Bar) |
| 16 | Damage/Stat Formula | Pure calculation | none (stateless) | Battle System | Established |
| 17 | Progression System | EXP/level curve, permanent growth | player level/exp | Battle System, HUD | Established (curve values Proposed) |
| 18 | Inventory System | Held items, 9-grid | `inventory[]` | UI Framework, Item pickup | Established |
| 19 | Equipment System | Weapon/Armor/Talent slots | `equipped{}` | Battle System (stat + bar params) | Proposed |
| 20 | Economy/Shop System | Store catalog, cost curve | `data/store.json` | Inventory, Merchant state | Established |
| 21 | Dialogue System | NPC/enemy talk trees | `data/dialogue/*.json` | Condition Evaluator, Story, Mission | Established |
| 22 | **Story System** | Main-beat progression, story flags | `storyFlags`, `currentBeat` | Condition Evaluator, Stage Select | Proposed controller over Established data |
| 23 | **Mission System** | Side/daily/once quests + scheduler | `MissionDefinition`/`MissionTracker` | Event Bus, Condition Evaluator | **Proposed (§8)** |
| 24 | **Condition/Flag Evaluator** | Shared "can player see/do X" engine | none (stateless over save data) | Dialogue, Stage Select, Mission, Encounter | **Proposed (§9)** |
| 25 | **Stage Select System** | Hub screen logic, badges | derived per-stage view-state | Condition Evaluator, Entity Status, Save | **Proposed (§10)** |
| 26 | Save/Persistence System | Meta save (permanent) + Run save (session) | save files | Everything with persisted state | Established (basic) + Proposed (schema growth) |
| 27 | HUD System | Always-on stat display | reads player stats | Progression, Inventory | Established |
| 28 | **Notification/Toast System** | "New mission," "daily reset," "level up" pop-ups | transient queue | Event Bus | Proposed |

---

## 14. Game Object Catalogue

| Object | Key fields | Owned by | Persisted? |
|---|---|---|---|
| `Player` | hp, atk, def, level, exp, gold, keys held, position | Progression, Battle | Run save |
| `EnemyInstance` | enemyId, stageId, tileId, hp/atk/def (from bestiary), `StageEntityStatus`, `encounterKind` | Stage, Entity Status, Battle | Run save (status only; stats are static data) |
| `ItemInstance` (field) | itemId, stageId, tileId, effect, `StageEntityStatus` | Stage, Entity Status, Inventory | Run save |
| `NPCInstance` | npcId, stageId, tileId, dialogue ref | Stage, Dialogue | Run save (visited flag only) |
| `Door` | color, stageId, tileId, `Opened` status | Stage, Entity Status | Run save |
| `Key` (held) | color, count | Inventory | Run save |
| `Stairs/Warp` | direction, `connect.up/down` target stageId | Stage | n/a (static) |
| `Stage`/`Floor` | stageId, 13×11 tile grid, legend, `story_note`, `connect` | Stage System | Static data + per-instance status |
| `CombatState` | attacker/target refs, round number, pending Power Bar result | Battle System | Transient (not persisted) |
| `Equipment` (weapon/armor/talent) | slot, stat bonus, Power Bar params (`v0/vmax/rampTime/greenHalf/blueOuter/redOuter`) | Equipment System | Static data; equipped choice in Run save |
| `DialogueTree` | npcId, nodes, choices, `requires`, `action` | Dialogue System | Static data |
| `StoryFlag` | key, bool | Story System | Meta save |
| `MissionDefinition` | see §8.1 | Mission System | Static data |
| `MissionTracker` | see §8.2 | Mission System | Meta save (`once`/`side`) or Run save (`daily` reset marker in Meta) |
| `ShopEntry` | itemId, cost_base, cost_multiplier, purchases | Economy System | Run save (purchase count) |
| `SaveSlot` (Meta) | storyFlags, unlockedStages, permanent stat bonuses, mission `claimed` set | Save System | Disk |
| `SaveSlot` (Run) | current stage, player state, per-stage `StageEntityStatus` map, active mission progress | Save System | Disk |
| **UI widgets** (ImGui, transient) | Panel, Slot/ImageButton, Bar, Tooltip, Badge | UI Framework Layer | Not persisted (rebuilt from the above every frame) |

---

## 15. Open Design Checklist *(extends `GAME_DESIGN_DOCUMENT.md` §17)*

- [ ] Decide: does re-entering an already-cleared floor from Stage Select repopulate regular
      enemies, or stay permanently cleared? (§5.2 — affects economy balance.)
- [ ] Confirm ImGui's default widget visuals can be restyled enough to match the 9-slice art
      direction before committing to the Phase 2 migration (§2.3) — spike a single panel first.
- [ ] Define the full leaf-type list for the Condition Evaluator (§9.1) against every gating case
      actually needed by launch content, not just the examples given here.
- [ ] Decide daily-mission reset boundary: local device midnight vs. a fixed UTC hour (§8.1) —
      matters once/if the game ever syncs saves across devices.
- [ ] Validate the extended dialogue `action` verbs (`enterBattle`/`setStoryFlag`/`startMission`,
      §5.3) against at least one real authored encounter before treating the schema as final.
- [ ] Confirm the Event Bus (§13 #10) doesn't reintroduce hidden ordering dependencies between
      systems that are supposed to be decoupled (e.g. mission progress ticking twice on one WIN).

---

## Sources

Research used for §2 (UI framework decision):
- [Dear ImGui — Emscripten/Web support](https://www.dearimgui.com/webdemo/master/example_emscripten.html)
- [Dear ImGui Web and Emscripten (DeepWiki)](https://deepwiki.com/ocornut/imgui/5.5-web-and-emscripten)
- [Vulkan Tutorial — Building a Simple Engine: GUI](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/GUI/06_conclusion.html)
- [Immediate Mode GUI (Wikipedia)](https://en.wikipedia.org/wiki/Immediate_mode_GUI)
- [Why Immediate Mode GUI for GameDev tools (gist, industry practitioner notes)](https://gist.github.com/bkaradzic/853fd21a15542e0ec96f7268150f1b62)

Project sources (verified directly against files, same convention as `GAME_DESIGN_DOCUMENT.md`):
`TOMS/src/engine/render_iface.h`, `TOMS/docs/NODE_SYSTEM.md`, `TOMS/docs/FIGHT_SCENE_DESIGN.md`,
`TOMS/docs/MAIN_BATTLE_SCENE_DESIGN.md`, `TOMS/docs/GAME_DESIGN_DOCUMENT.md`,
`TOMS/FIGHTING_TALKING_DESIGN.md`, `TOMS/docs/GAMEPLAY_ROGUELIKE_TECH_ARCH.md`,
`TOMS/docs/GAMEPLAY_ROGUELIKE_DATA_SCHEMA.md`, `TOMS/docs/WORKING_LOG.md`.
