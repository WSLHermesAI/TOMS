# Event System — Index

> Status: **design proposal** (2026-09-23). Nothing here is implemented yet. This folder explains
> how a data-driven in-game Event System would plug into the existing engine. It also covers how
> its editor authors events and how the editor previews a whole event flow before anyone plays it.

## Why

Today every event in the game was written by AI straight into JSON and C++. There is no single
place that says "when X happens, do Y, then Z". The logic is spread across several places:

| Where today | What it does | Problem |
|---|---|---|
| [`game_input.cpp` `movePlayer()`](../../src/game/core/game_input.cpp) `event:` branch | Floor event tiles | The effect is **guessed from the id string**: `"trap"` → −8 HP, `"cache"` → +10 HP/+12 gold, and so on |
| [`game_story.cpp` `runDialogueAction()`](../../src/game/core/game_story.cpp) | Dialogue choice verbs (`give`, `makeChoice`, `enterBattle`, …) | Only reachable from inside a dialogue |
| [`game_input.cpp` `interact()`](../../src/game/core/game_input.cpp) | NPC → dialogue file | **Hard-coded if/else** map from NPC id to dialogue file |
| `data/story/floors/*.json` `sideStoryHooks` | "Proximity" side-story triggers | Declared in data, but nothing runs them |
| [`event_bus.h`](../../src/engine/event_bus.h) | `EnemyDefeated` / `ItemCollected` / `ChoiceMade` | Only missions listen to it |

The Event System replaces this with one model: **Event = Trigger → Steps → Fire**. The model is
loaded from files, runs as a small state machine, and moves a flow forward one step at a time.
Designers (or AI) author it, a validator checks it, and an editor shows and simulates it.

## The model (from the design mind-map)

```
N Type
├── EventType   (what a step DOES)            -> "Step" in these docs
│   ├── Talk                                   talk        -> opens a dialogue
│   ├── Battle                                 battle      -> starts combat, outcome win/lose
│   ├── Add something                          add
│   │   ├── money                                  what: "money"
│   │   ├── item                                   what: "item"
│   │   └── attribute value change                 what: "attr"
│   └── Play animation (or change scene)       play / changeScene
└── Event       (WHEN it happens and WHAT it leaves behind)
    ├── TriggerCondition                       "trigger" + "requires"
    │   └── Approach (or talk/pick to this something/someone)
    └── FireEvent                              "fire"
        ├── place role in somewhere                placeRole / removeRole / moveRole
        └── emit event data                        emit  -> other events' triggers
```

"Progress goes to the next step" works at two levels:

1. **Inside an event**: its `steps[]` run in order. A blocking step (talk, battle, animation)
   waits for the game to report that it finished, then the next step starts.
2. **Between events**: when an event finishes, its `fire` outputs **arm** the next events
   (`next`) and/or **emit** a signal that other events' triggers listen for. A chain of events is
   an **Event Flow** (one file = one flow, e.g. a side story).

## Documents

| # | Doc | Read it for |
|---|---|---|
| 1 | [`01_CONCEPTS_AND_DATA_SCHEMA.md`](01_CONCEPTS_AND_DATA_SCHEMA.md) | Glossary, event lifecycle, and the JSON file format (triggers, steps, fire outputs, outcomes) |
| 2 | [`02_GAME_INTEGRATION.md`](02_GAME_INTEGRATION.md) | How the runtime binds to the game: the runner only routes, and each owning system (dialogue, combat, inventory, stage, …) registers a handler for its own step verbs. Also hook points, blocking steps, save/load, and migration of today's hard-coded events |
| 3 | [`03_EVENT_EDITOR.md`](03_EVENT_EDITOR.md) | The editor: graph canvas, inspector, map binding, validation, the AI-authoring round-trip |
| 4 | [`04_FLOW_PREVIEW.md`](04_FLOW_PREVIEW.md) | Previewing a whole flow: static overview, dry-run simulator, path explorer, live in-game preview |
| — | [`examples/flow_ss05_crypt_choir.json`](examples/flow_ss05_crypt_choir.json) | One complete example flow that uses every step and fire type |

## Design rules (carried over from the existing codebase)

- **Plain JSON, no scripting language.** Steps are a closed set of verbs. This matches
  `runDialogueAction()` and the Condition DSL. There is no Lua and no embedded expressions.
- **Reuse, don't duplicate.** Trigger requirements use the existing
  [Condition DSL](../../src/engine/condition.h) unchanged. Event steps can call the existing
  dialogue actions (`makeChoice`, `addCounter`, `unlockSkill`, …) through an `action` step.
- **Pure logic, headlessly testable.** The runtime (`src/game/systems/event_system.*`) knows
  nothing about `Game`. It talks to the game only through an interface, the way
  `ConditionContext` does. The editor links the same code for its simulator, so **the preview is
  the real runtime, not a re-implementation.**
- **Fail closed.** An unknown step type, a missing dialogue or a bad reference is a validation
  error. At runtime the event is skipped and logged; it is never "partially applied".

## Proposed file locations

```
data/event_flows/<flowId>.json          authored flows (one per side story / chapter beat / floor)
data/event_flows/_index.json            optional: load order + enabled flag per flow
src/game/systems/event_system.h/.cpp    runner + handler registry (pure logic) + event_system_test.cpp
data/event_flows/_schema/<verb>.json    each owning system's step contract (fields, blocking, outcomes)
<owning system's file>                  its IStepHandler, e.g. DialogueStepHandler in game_story.cpp,
                                        CombatStepHandler in game_combat.cpp (see 02 §3)
tools/validate_events.py                CI / AI-output validator
editor/src/event*/                      editor module (graph, inspector, simulator)
```

`data/events/pool_*.json` (the per-floor flavor pools) stay as they are. Phase 3 of the migration
([02 §7](02_GAME_INTEGRATION.md#7-migration-plan)) turns a pool entry's `kind` into a real,
data-driven step list instead of the id-string guessing.
