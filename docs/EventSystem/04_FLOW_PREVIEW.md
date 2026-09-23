# 04 — Previewing the Whole Event Flow

Part of the [Event System](README.md) design. This doc covers how the editor ([03](03_EVENT_EDITOR.md))
lets you **see and test an entire flow, or all flows together, without playing the game**, and how it
then hands off to the real game for a final check.

There are four preview levels. Each answers a different question:

| Level | Question it answers | Runs game code? | Speed |
|---|---|---|---|
| 1. **Overview** | "How do all events connect, across flows and floors?" | no (static analysis) | instant |
| 2. **Simulator** | "What exactly happens, step by step, if the player does X?" | **yes**: the real `EventRunner` against a simulated host | interactive |
| 3. **Path explorer** | "What are *all* the ways this flow can end, and what does each one give the player?" | yes, automated | seconds |
| 4. **Live in-game** | "Does it look and feel right in the actual game?" | the real game | real time |

---

## 1. Overview: the whole flow at a glance

*⤢ Overview* in the toolbar switches the graph to a **tower-wide view**:

```
         F31        F32        F33                    F34        F35
ch_05 ─┬──────────┬──────────┬──────────────────────┬──────────┬──────────────┐
ss05   │          │          │ ⚑hear ─▶ 💬talk ─▶ ⚔fight ──────────────▶ 💬thanks │
       │          │          │            ╎refuse                  │              │
ss08   │   ⚑…  ─▶ │  …       │            ╰╌⚡choir_ignored╌╌╌╌╌╌╌╌╌╌╌╌▶ ⚡…      │
npc_*  │ 💬elder↺ │          │                      │          │ 💬ghost↺      │
       └──────────┴──────────┴──────────────────────┴──────────┴──────────────┘
```

- **Columns = floors** (F01…F70, plus the hand-authored `stage*`), in tower order from
  `data/story/floors/*.json` `nextFloor`. **Rows = flows**, grouped by act. Each event sits in the
  column of the floor its trigger targets. Events without a floor (`signal`, `condition`,
  `immediate`) sit next to the event that arms them.
- Solid edges are `next`/`arm`. Dashed edges are signals, drawn across flows, so a side story that
  depends on another becomes visible.
- **Filters**: act, flow, trigger kind, "only events touching flag X / item Y / enemy Z", and
  "only problems".
- **Heat overlays**, toggled on the floor axis:
  - *reward density*: total money / items / attr deltas that events can hand out per floor (balance check)
  - *choice density*: blocking `talk` steps per floor (pacing check)
  - *coverage*: which events the path explorer (level 3) actually reached

Static analysis runs on every change and feeds the Problems pane. It reports unreachable events,
orphan signals, events on floors that do not exist, flows whose `requires` can never be true given
declared flags, and dependency **cycles between flows** (A waits on B's signal and B waits on A's).

## 2. Simulator: step through with the real runtime

*▶ Simulate* opens a dock over the graph. It creates an `EventRunner` from
`src/game/systems/event_system.cpp`, the **same compiled code the game uses**, and fills the same
`StepHandlerRegistry` the game fills ([02 §2](02_GAME_INTEGRATION.md#2-runtime-contract)). The only
difference is which handler owns each verb:

- **Simulator handlers, one per owning system.** `SimDialogueHandler` (`talk`) shows the real
  dialogue tree and waits for you to click a choice. `SimCombatHandler` (`battle`) waits for
  *Win*/*Lose* or resolves the fight with real math (§2.2). `SimStageHandler` (roles, `changeScene`)
  updates the Map pane. `SimFxHandler` (`play`) previews the clip. Instant verbs (`add.*`,
  `setFlag`, `action`) apply to `SimState`. Each simulator handler reads its verb's
  `_schema/<verb>.json` for the outcomes to offer. **A new verb that a system adds without a
  simulator handler still works** through a generic fallback: it offers the schema's outcomes as
  buttons, and the simulator needs no code change.
- **`SimState : ConditionContext`**: an editable copy of everything conditions can read (flags,
  counters, choices, side-story states, items, stats, cycle, cleared floors, shards…).

```
┌ SIMULATOR ─ flow_ss05_crypt_choir ─────────────────────────────────────────────────────────────┐
│ Preset [ch_05 fresh ▾] [Save preset]   Floor [F33 ▾]   ⏮ Reset  ⏯ Run to block  ⏭ Step  ● Break │
├───────────────────────────────┬───────────────────────────────┬──────────────────────────────┤
│ PLAYER ACTIONS                │ TIMELINE                      │ STATE                        │
│ Map: click a cell = walk there│ 00 enterFloor F33             │ gold        120 → 180  ▲60  │
│  ⚑ Approach m_crypt_altar     │ 01 ⚑ ev_hear_choir  Armed→Run │ potion_blue   0 → 1    ▲1   │
│  💬 Interact npc:ghost_villager│ 02   ▸ play fx_ghost_mist  ✓ │ humanity      2 → 3    ▲1   │
│  ✋ Pick up …                  │ 03   ⇢ placeRole r_ghost @8,18│ flag_choir_silenced   ✔ new │
│  ☠ Defeat …   ⇥ Enter floor … │ 04 💬 ev_talk_ghost Armed→Run │ ss_05       active          │
│ ───────────────────────────── │ 05   ▸ talk ghost_villager    │ ──────────────────────────  │
│ WAITING: ⚔ battle wraith      │        ⤷ outcome choice:opt_help│ EVENTS                      │
│   [ Win ]  [ Lose ]           │ 06 ⚔ ev_fight_choir Armed→Run │ ✓ hear_choir   Done         │
│   (or ▶ Resolve with real     │ 07   ▸ battle wraith  ⏸ waiting│ ✓ talk_ghost   Done         │
│    combat math, 04 §2.2)      │                               │ ▶ fight_choir  Running [0]  │
│                               │                               │ · ghost_thanks Dormant      │
└───────────────────────────────┴───────────────────────────────┴──────────────────────────────┘
```

### 2.1 What you can do

- **Act as the player**: use the buttons (generated from the *armed* events' triggers, so you always
  see what is possible right now) or click the Map pane to "walk" onto a cell, which calls
  `onPlayerMoved`.
- **Resolve blocking steps**: for `talk`, the dock shows the actual dialogue tree from
  `data/dialogue/*.json`, filtered by each choice's `requires` against `SimState`, and you click
  through it. For `battle`, choose *Win* / *Lose*. For `play`, it plays in a small preview panel or
  you skip it.
- **Edit state at any time**: toggle a flag, set gold, add an item, then *Re-check*. This fires
  `onStateChanged()`, which is how you test `condition` triggers and `requires` gates.
- **Breakpoints**: on an event (on arm, on run, on done) or on a step. *Run to block* runs until the
  next blocking step or breakpoint.
- **Timeline scrubbing**: every timeline row stores a state snapshot. Clicking a row rewinds the
  STATE and EVENTS panes to that moment. *Branch from here* forks a new run from that snapshot, so
  you can try "Lose" after having tried "Win" without replaying everything.
- The graph **lights up live**: the running node pulses, done nodes turn green, failed ones red, and
  the edge that just fired animates.

### 2.2 State presets

A preset is a JSON snapshot of `SimState` saved under `editor/presets/<name>.json`, for example
*"ch_05 fresh"* or *"cycle 2, refused ss_04"*. Presets can also be **imported from a real save
file** (`RunSaveData` / `MetaSaveData` JSON), so a bug report save becomes a reproducible
simulator session.

Optional *real combat math*: a `battle` step can be resolved by the game's own combat formulas (`data/combat.json` + the round math in
`game_combat.cpp`, once that is factored into a pure function the editor can link) using the preset's stats. The simulator
then shows **"with these stats the player wins in 7 rounds losing 64 HP"** instead of asking.

### 2.3 Trace output

Every session can be exported as a plain-text trace:

```
[F33] enterFloor
[flow_ss05_crypt_choir/ev_hear_choir] Armed -> Running (trigger approach m_crypt_altar r2 @7,17)
  step 0 play fx_ghost_mist ... done
  step 1 notify flow_ss05.hear_choir.text
  fire placeRole r_ghost npc:ghost_villager F33@8,18
  arm ev_talk_ghost
[flow_ss05_crypt_choir/ev_talk_ghost] Armed -> Running (trigger interact npc:ghost_villager)
  step 0 talk ghost_villager#choir_plea ... outcome choice:opt_help -> continue
...
```

This is the format the golden-trace CI check ([02 §8](02_GAME_INTEGRATION.md#8-testing)) diffs, and the
one that *Copy for AI* ([03 §6](03_EVENT_EDITOR.md#6-save-format-and-the-ai-round-trip)) pastes.

## 3. How the simulator stays honest

The preview is only useful if it cannot drift from the game. Three rules keep it honest:

1. **One runtime.** The simulator links `event_system.cpp` and `condition.cpp` directly. There is
   no editor-side interpretation of steps, triggers or outcomes.
2. **The verb contract is shared.** The game's handler and the simulator's handler for a verb both
   follow that verb's `_schema/<verb>.json` (fields, blocking, outcomes). Each system's own test checks
   that its game handler only ever completes with outcomes listed in its schema. Anything the
   simulator can offer is therefore something the game can actually produce.
3. **Dialogue filtering uses `condition.cpp` too.** Choice visibility in the simulator comes from
   the same `evaluate()` the game uses in `Game::enterNode()`.

What the simulator deliberately does **not** model: movement pathing, walls, roamers, rendering and
timing. For those, use level 4.

## 4. Path explorer

*Explore all paths* runs the flow automatically from a preset. At every blocking step it
**branches on every possible outcome** (each visible dialogue choice, win/lose, …) and at every
point it **tries every armed trigger**. The search is depth-first, with a configurable bound
(default: 12 blocking decisions, and 5,000 end states per flow).

Result view:

```
flow_ss05_crypt_choir  — 4 distinct endings, 7 paths, all events reached ✔
┌───┬───────────────────────────────────────────────┬────────┬─────────────────────────┬──────────┐
│ # │ decisions                                      │ result │ player delta            │ signals  │
├───┼───────────────────────────────────────────────┼────────┼─────────────────────────┼──────────┤
│ 1 │ opt_help · win                                 │ done   │ +60g +potion_blue +1 hum│ silenced │
│ 2 │ opt_help · lose · win                          │ done   │ +60g +potion_blue +1 hum│ silenced │
│ 3 │ opt_help · lose · lose · …                     │ open   │ —                       │ —        │
│ 4 │ opt_refuse                                     │ failed │ —                       │ ignored  │
└───┴───────────────────────────────────────────────┴────────┴─────────────────────────┴──────────┘
⚠ path 3: retry loop never forces a resolution (by design? mark "allowOpen" to silence)
```

- Selecting a row replays it in the simulator (level 2) and highlights its route on the graph.
- **"All flows" mode** explores the whole content set with signals flowing between flows. It is
  run headlessly in CI (`tower_editor --events-explore`). It reports events never reached from a
  new game, soft-locks (a floor you can leave with a flow Running and no way back to its trigger),
  and the maximum possible reward per floor, which feeds the balance overlay in §1.

## 5. Live in-game preview

The final check happens in the real game, launched from the editor with *▶ Play in game*:

```
tower_vulkan.exe assets --event-preview flow_ss05_crypt_choir/ev_fight_choir --preset ch05_fresh.json
```

- `main.cpp` already takes `argv[1]` (asset dir) and `argv[2]` (scenario mode). `--event-preview`
  adds a scenario: load the preset as the run/meta state, load the event's floor, put the player
  next to the trigger target, and set every event *before* the chosen one to the states the
  simulator run had at that point. You start **exactly at the moment you want to check**.
- **Hot reload**: while launched this way, the game watches `data/event_flows/`. Saving in the
  editor reloads the library, and running events restart from their current step. The web build
  gets the same through the debug page (`web-debug`), which re-fetches `event_flows` on demand.
- **Debug overlay** (ImGui, existing `imgui_layer`; toggle F9): a list of active flows and each
  event's state; approach zones and markers drawn on the map; the live timeline in the same trace
  format as §2.3; and buttons to force an outcome (skip battle as win) or arm/disarm an event.
- **Editor link**: the running game writes its trace to `toms.log` (existing `log.h`). The editor
  tails it and lights up the graph as in §2.1. That is the same visual whether the source is the
  simulator or the real game.

## 6. Typical workflow

1. AI (or you) writes `data/event_flows/flow_x.json`.
2. Open it in the editor. Auto-layout, **Overview** (does it connect where I expect?) and Problems.
3. **Simulate** the main path from a preset. Scrub, branch, try the other choice.
4. **Explore all paths**. Check the endings table and rewards; fix warnings.
5. **Play in game** at the key moment to check pacing and visuals.
6. Commit. CI runs `validate_events.py` plus the golden-trace diff of the path explorer output.
