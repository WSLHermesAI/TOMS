# 08 — NPCs and their events (the dialogue event system)

中文：[../zh_TW/event_editor/08_NPC_DIALOGUE.md](../zh_TW/event_editor/08_NPC_DIALOGUE.md)

**Status:** built 2026-10-16: the game, the data format, the village elder's events on F02, tests, and the event
editor (talk event fields, an NPCs tab with a talk preview, a Dialogues tab). Repeatable events (`once: false`) and
`varChanged` events were added the same day (sections 6.5, 11.1). The owner's decisions are in section 12.

This document describes how an NPC behaves when the player talks to it, and how the same pieces connect bigger
stories (section 11):
- what it says when it has nothing special to say: **chat lines** that rotate with a talk index;
- how events are **bound** to an NPC and offered to the player as **topics**, with a menu when several
  are active;
- how an event's dialogue offers **choices**, and how each choice changes the game: a status, a variable,
  an item, or another event;
- what happens to an event's topic once it is finished.

## 1. Where it lives

| Part | Where |
|---|---|
| NPC files | `assets/data/npcs/<npcId>.json` (section 3): all six NPCs placed on the maps have one |
| Talk events | the pool files `assets/data/events/pool_*.json`: `"trigger": {"on": "talk", "npc": "<id>"}` plus `dialogue`, `afterDialogue`, `topicOrder` (section 6) |
| Dialogues | `assets/data/dialogue/<id>.json`: nodes with text and choices; choices carry results (section 6.2) |
| Event variables | `assets/data/story/vars.json` (the event editor's Variables tab; section 8) |
| Talk logic | `src/core/game/systems/talk_system.h/.cpp`: NPC files, topics, the chat rotation, the menu decision (pure logic) |
| Results | `src/core/game/systems/story_actions.h/.cpp`: the one action list every story place runs (section 7) |
| Game glue | `Game::talkTo`, `openTopic`, `openTopicMenu`, `chooseDialogue`, `wakeEvent`, `runStoryActions` (`game_story.cpp`) |
| Conditions | `src/core/engine/condition.cpp`, with the new `varAtLeast`, `varEquals`, `talkCountAtLeast`, `onStage` |
| Tests | `unit.talk_system_test` (logic, saves and every shipped NPC file, dialogue and action); `smoke.talk_menu`, `smoke.talk_chain` (the game) |

**Talking:** the player steps onto the NPC's tile (as before), or presses Enter / Space while standing on it.

## 2. Words used here

| Word | Meaning |
|---|---|
| **NPC** | A character on the map (`npc:<id>` in a floor's legend). Its behaviour comes from its **NPC file** |
| **Chat lines** | The NPC's ordinary dialogues, for when no event is offered. Several rotate |
| **Talk index** | Which chat line plays next, per NPC, per run |
| **Bound event** | An event whose trigger is *talk to this NPC* |
| **Topic** | A bound event as the player sees it: one line in the NPC's topic menu |
| **Event dialogue** | The dialogue an event plays when its topic is picked; its choices are where the results happen |
| **Result** | What a choice does: actions (change a status, set a variable, give an item…) and whether the event is finished |

## 3. The NPC file

One file per NPC: `assets/data/npcs/<npcId>.json`. The village elder's:

```json
{
  "npcId": "villager",
  "name": "npc.villager.name",
  "greeting": "npc.villager.greeting",
  "chat": {
    "lines": [
      { "dialogue": "villager_elder" },
      { "dialogue": "elder_chat_weather" },
      { "dialogue": "elder_chat_tower", "requires": { "type": "stageCleared", "stageId": "F05" } },
      { "dialogue": "elder_secret_well", "requires": { "type": "talkCountAtLeast", "npc": "villager", "value": 8 },
        "actions": [ { "type": "setVar", "var": "well_secret", "value": true } ] }
    ],
    "order": "loop"
  },
  "topics": { "menu": "auto" }
}
```

| Field | Meaning | Default |
|---|---|---|
| `npcId` | The id in the map legend (`npc:villager`); the file is named after it | required |
| `name` | Text key for the name shown on the dialogue box | none: no name line |
| `greeting` | Text key for the one line the NPC says at the top of the topic menu (section 5) | none: the menu shows only the name |
| `sprite` | Its map sprite, a sprite name in the game atlas, so a new NPC needs no code | `npc_<id>` when the atlas has it, else the old table |
| `chat.lines` | The chat lines, in order: a dialogue file id, an optional `requires`, optional `actions` (run each time it plays) | required, at least one |
| `chat.order` | Which chat line plays next: `loop`, `stay`, `random` (section 4) | `loop` |
| `topics.menu` | When to show the topic menu: `auto` (section 5) or `always` | `auto` |

- What a finished event's topic does is set on each event (section 6.4).
- **No NPC file:** the game uses `assets/data/dialogue/<npcId>.json` as the NPC's only chat line.
- **The princess** (a different dialogue on `stage_11`) is two chat lines with `{"type": "onStage", "stageId":
  "stage_11"}` and its `not`, instead of a special case in code.
- The other NPCs keep the one dialogue they had: `sorcerer` → `sorcerer_teacher`, `king` → `king_lieutenant`, …

## 4. Chat lines and the talk index

Each NPC has a **talk index** per run, starting at 0. When the player talks to the NPC and no topic is
offered (section 5), the NPC plays a chat line:

1. Take the lines whose `requires` holds now (a line without `requires` always holds). Call them the
   **available lines**, in file order.
2. Play the line at `talk index mod (number of available lines)`, and add 1 to the talk index. With
   `"order": "stay"`, the index stops at the last line instead of going back to 0.

With `"order": "random"`, step 2 picks a random available line instead, never the same line twice in a row
when there are two or more.

| Lines | Talk 1 | Talk 2 | Talk 3 | Talk 4 | Talk 5 |
|---|---|---|---|---|---|
| 1 line (A) | A | A | A | A | A |
| 3 lines, `loop` | A | B | C | A | B |
| 3 lines, `stay` | A | B | C | C | C |
| 3 lines, `random` | B | A | C | A | B (for example) |

- **One line:** always the first, whatever the order.
- **Hidden messages that need many talks:** a chat line with
  `"requires": {"type": "talkCountAtLeast", "npc": "villager", "value": 8}` joins the rotation after the eighth
  talk: conditions see the talks *before* this one, so it can play from the ninth. Chain them (8, 15, 25 talks)
  for clues that come slowly. The line's `actions` can open something, as the elder's secret does: it sets
  `well_secret`, which shows a third choice in *The Well at Night*.
- **Only chat lines move the index.** Talking about a topic does not, so the next plain talk continues where
  the chat left off. Every talk counts for `talkCountAtLeast`, topics included.
- **The index moves when a line plays**, so leaving the dialogue early still counts.
- **A line that becomes available later** joins the rotation from then on. The index is not reset.
- The talk index, the talk count and the last random line are saved with the run (section 8).

## 5. Talking to an NPC: the whole flow

```
player steps onto the NPC (or presses Enter on its tile)
        │
        ▼
collect the NPC's topics:
  open     = bound events not finished, whose `requires` holds, and (if another event names them in
             `next`) that have been woken
  finished = bound events that are finished and have an `afterDialogue` (section 6.4)
        │
        ├─ no open and no finished topics ─────────────► play the next chat line (section 4)
        │
        ├─ exactly 1 open, no finished, menu `auto` ──► play that event's dialogue directly
        │   (every other bound event is inactive, or finished and hidden)
        │
        └─ otherwise ─────────────────────────────────► the topic menu:
                                                          ┌────────────────────────────────┐
                                                          │ Village Elder                  │  ← name
                                                          │ Ah, it's you again. ...        │  ← greeting
                                                          │ ▶ The Road North               │  ← open topics
                                                          │   The Missing Ledger           │
                                                          │   ✓ The Well at Night          │  ← finished, with an afterDialogue
                                                          │   Just chat                    │  ← plays a chat line
                                                          │   Leave                        │
                                                          └────────────────────────────────┘
```

(That menu is the `smoke.talk_chain` screenshot: after *The Well at Night*, which woke *The Missing Ledger*.)

- **The menu order is fixed:** open topics first, then finished ones, each in the order of the NPC's bound
  events (pool files by name, then the order inside the file). An event can set `"topicOrder": <number>` to
  move it (lower first; events without it come after, in pool order). Each line is the event's `title` text.
- **Inactive events** (their `requires` does not hold yet, or they wait to be woken) are **not listed**.
- **Woken:** an event that another event names in `next` (or a choice in `fire`) waits until that happens,
  even when its own `requires` holds. Events nobody links to are open as soon as `requires` holds.
- **The greeting** is one line of speech at the top of the menu. No chat line plays before the menu.
- **When the menu appears** (`auto`): whenever there are two or more topics, open or finished. A single open
  topic with nothing else to list plays directly; a single open topic next to a finished one shows the menu.
- **Just chat** plays the next chat line and moves the talk index. **Leave** closes the menu.
- After an event dialogue closes, the player is back on the map, and the next talk rebuilds the list.

**Example: the elder with five bound events, three listed and two not** (checked by `talk_system_test`).

| Event | `requires` | Now | In the menu |
|---|---|---|---|
| `ev_elder_north_road` | none; finished, has an `afterDialogue` | finished | ✓ The Road North |
| `ev_elder_well_night` | none | open | The Well at Night |
| `ev_elder_ledger` | `ev_elder_well_night` done, and woken by its `next` | inactive | not listed |
| `ev_elder_bell_rumour` | `insight ≥ 1` (the bell chain gave it) | open | A Bell With No Sound |
| `ev_elder_last_wish` | `elder_trust ≥ 3` and F05 cleared | inactive | not listed |

## 6. Event dialogues: choices and results

### 6.1 Binding an event to an NPC

```json
{
  "eventId": "ev_elder_well_night",
  "kind": "whisper",
  "text": "ev_elder_well_night.text",
  "title": "ev_elder_well_night.title",
  "desc": "ev_elder_well_night.desc",
  "trigger": { "on": "talk", "npc": "villager" },
  "dialogue": "elder_well_night",
  "afterDialogue": "elder_well_night_after",
  "next": ["ev_elder_ledger"]
}
```

- `dialogue` is a file in `assets/data/dialogue/`. Without it, the event shows its `text` as one page with
  **OK**, and OK finishes it.
- One event is bound to one NPC; one NPC can have any number of events, from any pool.
- A talk event needs no tile, and no floor's `events` list has to name it. Its NPC must stand on a floor.

### 6.2 Dialogue files: start, choices and results

```json
{
  "npcId": "villager",
  "start": "root",
  "nodes": {
    "root": {
      "text": { "zh_TW": "每到夜裡，老井裡就有東西在哭。", "en": "Something cries in the old well at night." },
      "choices": [
        { "label": { "zh_TW": "它怕光。我帶火把下去。", "en": "It fears light. I'll take a torch." },
          "id": "secret", "requires": { "type": "varEquals", "var": "well_secret", "value": true },
          "actions": [ { "type": "addStat", "stat": "str", "delta": 2 }, { "type": "setVar", "var": "well_helped", "value": true },
                       { "type": "addVar", "var": "elder_trust", "delta": 2 } ],
          "finish": true, "next": "secret_done" },
        { "label": { "en": "I'll climb down." }, "id": "help", "requires": { "type": "statAtLeast", "stat": "hp", "value": 50 },
          "actions": [ { "type": "addStat", "stat": "atk", "delta": 2 }, { "type": "setVar", "var": "well_helped", "value": true } ],
          "finish": true, "next": "help_done" },
        { "label": { "en": "Pay me first." }, "id": "pay",
          "actions": [ { "type": "addStat", "stat": "gold", "delta": 30 }, { "type": "addCounter", "counter": "humanity", "delta": -1 } ],
          "finish": true, "next": "pay_done" },
        { "label": { "en": "Not now." }, "next": null }
      ]
    }
  }
}
```

| Field | Meaning |
|---|---|
| `start` | The first node, or a list `[{"node": "pay", "requires": {...}}, {"node": "thanks"}]`: the first whose `requires` holds (the last should have none) |
| `text` | One string per language (zh_TW and en at least; others fall back to English) |
| `label` | The choice's text |
| `requires` | Hides the choice while false |
| `actions` | Results, run in order when the choice is picked (section 7). The older single `action` still works |
| `finish` | `true`: picking it finishes the dialogue's event (section 6.3) |
| `fire` | Event ids to wake now (`next` for one choice only) |
| `id` | Remembered as `choiceMade {eventId, id}`, for later conditions |
| `next` | The next node; `null` or `""` closes |
| `finishOnClose` | (top level) finishes the event however the dialogue closes |

- **Different choices, different results:** each choice has its own `actions` and `fire`. In the example the
  torch raises STR and trust, climbing raises ATK, asking for money gives gold and lowers humanity, and *Not
  now* closes without finishing, so the topic is there next time.
- **Remembered choices:** later dialogues and events ask `{"type": "choiceMade", "choiceId":
  "ev_elder_well_night", "optionId": "pay"}`; the after-dialogue uses it to pick its first node.

### 6.3 Running the event

When the topic is picked:

1. The event is **started** (the player's event log lists it as in progress).
2. Its dialogue plays; each picked choice runs its `actions`, then wakes its `fire` events.
3. A choice with `"finish": true` **finishes** the event: the event record's own `actions` run, `event_<id>`
   is set, and its `next` events are woken (section 11.1).
4. If the dialogue closes without a finishing choice, nothing more happens; the topic stays open.

### 6.4 After an event is finished

| Event record | The topic after finishing | Picking it |
|---|---|---|
| no `afterDialogue` | disappears | — |
| `"afterDialogue": "elder_well_night_after"` | stays in the menu, marked ✓ | plays that dialogue |

- The after-dialogue is its own dialogue file: short, with a conditional `start` to answer the choice the
  player made (the elder thanks a helper, and needles one who asked for money).
- `finish` and `fire` are ignored there. It should give nothing: the player can repeat it, and the test fails
  on an `addStat`, `setStat` or `give` in an after-dialogue or a chat line.

### 6.5 Repeatable events

`"once": false` on an event record makes it repeatable; `"maxTimes": N` caps it (0 or absent = no limit):

```json
{ "eventId": "ev_elder_potion", "trigger": { "on": "talk", "npc": "villager" },
  "requires": { "type": "runFlagSet", "flag": "event_ev_elder_last_wish" },
  "actions": [ { "type": "give", "itemId": "potion_red" } ], "once": false, "maxTimes": 3, "dialogue": "elder_potion" }
```

- **Every run** does the event's results again: a talk topic stays open after finishing (its dialogue's results and the
  record's `actions` each time), a tile event stays on its tile (bump it again), a `chain` / `varChanged` event runs
  again when it fires again.
- **The first run only** finishes it: `event_<id>` is set, it enters the event log, and its `next` events are woken.
- **Used up** (`maxTimes` runs): it behaves as a finished one-off: the topic is hidden or shows its after-dialogue, the
  tile goes.
- The run count is saved with the run (`eventCounts`). The elder's potion trade sells three potions for 20 gold each
  (`smoke.talk_repeat`).

## 7. Actions (results)

One list for event records, dialogue choices and chat lines (`story_actions.h`). A result is open: any mix,
in any order.

| Action | Does |
|---|---|
| `{"type": "addStat", "stat": "str", "delta": 2}` | Adds to a player status by name: `hp`, `maxhp`, `atk`, `def`, `gold`, `exp` (with level-ups), `lv`, and the attributes of `stats.json` (`str`, `dex`, `agi`, `vit`, `int`, `luk`). Negative takes away (not below 0; HP not below 1). Shows "STR +2" |
| `{"type": "setStat", "stat": "hp", "value": 100}` | Sets a status |
| `{"type": "give", "itemId": "gem_def", "count": 1}` | Gives items ("Received: …") |
| `{"type": "take", "itemId": "potion_red", "count": 1}` | Takes items, a trade; fails (and changes nothing) when the player has too few, so `require` `itemHeld` on the choice |
| `{"type": "setVar", "var": "well_helped", "value": true}` | Sets an event variable (`true` / `false` are 1 / 0) |
| `{"type": "addVar", "var": "elder_trust", "delta": 1}` | Adds to an event variable (clamped to its range) |
| `{"type": "setFlag", "flag": "flag_x"}` | Sets a run flag |
| `{"type": "setStoryFlag", "flag": "..."}` | Sets a story flag (kept across runs) |
| `{"type": "addCounter", "counter": "insight", "delta": 1}` | Story counter |
| `{"type": "fireEvent", "eventId": "ev_x"}` | Wakes another event (as `fire`) |
| (any `setVar` / `addVar` that changes a value) | also runs the `varChanged` events on that variable (section 11.1) |
| `{"type": "message", "text": "text.key"}` | A notification line |
| `startMission`, `claimMission`, `makeChoice`, `unlockSkill`, `setSideStoryState`, `enterBattle` | as before |

An action that fails (an unknown status, too few items) is logged as `[story] action failed` and changes
nothing; `talk_system_test` rejects such actions in the shipped data before they get there.

## 8. Variables, conditions and saved state

- **Event variables** live in `assets/data/story/vars.json`, the file the event editor's Variables tab writes:

  ```json
  {
    "elder_trust": { "type": "int", "default": 0, "scope": "run", "min": 0, "max": 5, "category": "elder" },
    "well_helped": { "type": "bool", "default": false, "scope": "run", "category": "elder" },
    "_categories": ["elder"]
  }
  ```

  A `bool` is 0 / 1; `min` / `max` clamp every change. An undeclared variable reads 0.
- **Conditions:** `{"type": "varEquals", "var": "well_helped", "value": true}`,
  `{"type": "varAtLeast", "var": "elder_trust", "value": 3}`, `{"type": "talkCountAtLeast", "npc": "villager",
  "value": 8}`, `{"type": "onStage", "stageId": "stage_11"}`, plus every earlier leaf (`runFlagSet "event_<id>"`
  = that event is finished, `counterAtLeast`, `choiceMade`, `stageCleared`, `itemHeld`, `statAtLeast` — which
  now reads `maxhp` and the attributes too — …) with `all` / `any` / `not`.
- **Saved with the run** (next to `eventsStarted` / `eventsFinished`; older saves load without them):

  ```json
  "vars": { "well_helped": 1, "elder_trust": 2 },
  "npcTalk": { "villager": [2, 7, -1] }
  ```

  `npcTalk` is [talk index, talk count, last random line]. A new run starts every variable at its `default`
  and every talk at 0. Story flags (`setStoryFlag`) and the meta save are not affected.

## 9. The editors

All in the web event editor (`tools/web_editors/event_editor.html`, `WebEditors.cmd`). Edits are written only after
**Open project folder…** and **Save**; each changed file is backed up to `Build/editor_backups/` first.

**Event form** (Logic):
- a talk event: **Dialogue** (⛔ when the file is missing), **After finishing** (hide the topic, or a dialogue file),
  **Talked to on** (any floor the NPC stands on, or one of them: it adds or removes an `onStage` condition and keeps the
  others), **Menu order** (`topicOrder`);
- any event: **Repeat** — *repeatable*, *at most N times* (section 6.5); *Fires when → this variable changes* for
  `varChanged`;
- actions: *take item* and *add to status* (statuses and attributes), beside the earlier ones.

**NPCs tab:**
- every NPC id (from the NPC files, the maps and the talk events); ⚠ = no NPC file yet, with **Create its NPC file**,
  and **+ NPC** for a new one;
- its file: name and greeting (zh_TW, en), map sprite (the atlas's sprite names), where it is placed, the topic menu
  rule;
- chat lines: dialogue, ▲ / ▼, ✕, *Only when* (condition rows), *When it plays* (actions), the line's first words,
  and the order (loop / stay / random);
- its topics in menu order, with the floor they are talked about on, their conditions, after-dialogue and repeat;
  **▲ / ▼** renumbers `topicOrder`;
- **Talk preview:** pick the floor the talk happens on (an NPC stands on several), tick finished events, set the
  variables and counters the topics read and the floors cleared, press
  **💬 Talk**: each talk shows what the game would (a chat line with its first words, one topic at once, or the menu).
  *finish* on a topic pretends the player finished it: its `next` events wake and its variables change. It uses the
  game's rules (`talk_system.cpp`); the talk count and chat index are shown.

**Dialogues tab:** a dialogue file as cards, one per node:
- *Used by* (topics, after-dialogues, chat lines), *Starts at* a node or **by condition** (a row per node with its
  condition rows), *finish the event however it closes*;
- per node: rename, zh_TW and en text (other languages are kept), delete; **+ Node**;
- per choice: label (zh_TW, en), *Then* (a node, or close), *finishes the event*, `id`, *Only when* (condition rows), *Then do*
  (action rows; unusual actions stay as chips), *Wakes* (events), ▲ / ▼ / ✕; **+ Choice**;
- **+ Dialogue** makes a new file. Problems: *Dialogue: missing node* (a `start` or `next` naming a node the file lacks).

**Conditions** (events, chat lines, choices, start rows) are edited as **rows**, one condition per row, in every place:

| Subject | Operators | Condition |
|---|---|---|
| a variable | ≥ n, = n | `varAtLeast`, `varEquals` |
| a counter | ≥ n | `counterAtLeast` |
| a flag | is set, is not set | `runFlagSet` |
| an event | is done, is not done | `runFlagSet "event_<id>"` |
| talks with an NPC | ≥ n | `talkCountAtLeast` |
| a status or attribute | ≥ n | `statAtLeast` |
| a floor | is cleared, is not cleared | `stageCleared` |
| an item | ≥ n held | `itemHeld` |
| the current stage | is, is not | `onStage` |
| a choice made in an event's dialogue | chose `id` (the dialogue's choice ids are offered) | `choiceMade` |

Two or more rows join with **all of these** or **any of these**; *is not* writes `{"not": …}`. A condition the rows cannot
show (nested `all` / `any` / `not`) stays as JSON, and **{ }** switches any condition to JSON (invalid JSON turns red and
keeps the old value). `talk_system_test` checks every NPC file, dialogue and action again when the tests run.

## 10. Code and tests

| File | What |
|---|---|
| `talk_system.h/.cpp` | `parseNpc`, `collectTopics` (talk events grouped by NPC, menu order, woken-by-`next`), `menuTopics`, `nextChatLine`, `planTalk`, `startNode`. No Game dependency: `TalkQuery` answers conditions, events and random numbers |
| `story_actions.h/.cpp` | `runActions` (through `StoryEffects`, which the game implements), `checkActions` (data checks against a catalogue), `grantsReward` |
| `run_state.h/.cpp`, `save_system.*` | event variables (`var`, `setVar`, `addVar`, declared ranges) and `npcTalk`, saved and loaded |
| `game_story.cpp` | `talkTo`, `playChatLine`, `openTopic`, `openTopicMenu`, `chooseDialogue`, `endDialogue`, `finishTalkEvent`, `wakeEvent`, `runStoryActions`, `loadNpcs` |
| `game_input.cpp` | stepping onto an NPC and Enter on its tile call `talkTo`; tile events run their actions through `runStoryActions`; `finishEvent` wakes `next` |
| `dialogue.rml/.rcss`, `UiDialogue::speaker` | the name line on the dialogue box |

| Test | Checks |
|---|---|
| `unit.talk_system_test` (154 checks) | rotation (1 line, loop, stay, random, a joining line, talk counts); topics (open, inactive, woken, finished with / without after-dialogue, `topicOrder`, when the menu appears); conditional start; the four new conditions; every action verb and its failures; `checkActions`; save and load; all shipped NPC files, talk events, dialogues and actions; the elder's story from start to the last wish |
| `smoke.talk_menu` | F02: stepping onto the elder shows his name, greeting and two topics |
| `smoke.talk_chain` | *The Well at Night* → "I'll climb down" (ATK +2) → Enter on his tile: the ledger is woken and the well listed as done |
| `smoke.talk_trusted` | the road, the well and the ledger bring trust to 3: the `varChanged` event's notice *The Elder Trusts You* |
| `smoke.talk_repeat` | the potion trade bought three times (gold −20, a potion each), then gone from the menu |

## 11. Connecting bigger stories: the reusable model

Everything above is made of a few pieces, and a bigger story is the same pieces linked together. There is no
special code for one story: the elder's five events are only data.

### 11.1 The pieces

| Piece | Is | Links to the others by |
|---|---|---|
| **Event** | a story step with an id, its texts, `requires`, `actions` and `next` | fires on a tile (`step`), from an NPC (`talk`), or as soon as it is woken and `requires` holds (`chain`) |
| **Condition** | `requires` on events, choices, chat lines and `start` lists | reads finished events, variables, counters, flags, choices made, talk counts, floors, items, statuses |
| **Action** | a result: status, item, variable, counter, flag, wake an event | runs on an event record, a choice or a chat line |
| **Link** | `next` (on finishing), `fire` (on one choice), `fireEvent` (as an action) | **wakes** events: a talk event becomes a topic, a tile event waits on its tile, a `chain` event runs at once if its `requires` holds |
| **NPC** | chat lines + bound events | its topics come and go with the events' conditions |
| **Dialogue** | nodes and choices | each choice is a branch with its own results; `start` picks a node by condition |

A **chain** event (`"trigger": {"on": "chain"}`) has no tile and no NPC: it is a logic step, for "when both
clues are found, the gate opens" — it runs its actions the moment it is woken and its `requires` holds, then
wakes its own `next`. Waking stops at depth 32, so a loop in the data cannot hang the game.

A **varChanged** event (`"trigger": {"on": "varChanged", "var": "elder_trust"}`) runs when an action changes that
variable (to a new value) and its `requires` holds: "when trust reaches 3, the elder trusts you". Variables changed by
its own actions can fire further events, up to 16 deep.

Both have no tile and no NPC, so when one runs **its title shows as a notice** (beside the notices of its actions, such
as "ATK +2"); `smoke.talk_trusted` shows *The Elder Trusts You*.

### 11.2 Patterns

| Pattern | How |
|---|---|
| **Sequence** A → B → C | A's `next: ["B"]`; B's `requires: event_A done` (the editor's *Active only when*) |
| **Branch** by choice | different choices `fire` different events, or set a variable later events read |
| **Join** (both needed) | the event `requires` `all` of two finished events; both name it in `next` (the bell chain's `ev_bell_rings`) |
| **Threshold** | `addVar` on many choices across NPCs, one event `requires varAtLeast` (the elder's trust → *Last Wish*) |
| **Remembered choice** | a choice `id`; later `choiceMade` in conditions, `start` lists and after-dialogues |
| **Slow clue** | a chat line gated by `talkCountAtLeast`; its actions set a variable a topic's choice requires |
| **Cross-NPC** | finishing a topic on NPC A wakes a talk event bound to NPC B |
| **Cross-floor** | a tile event on F03 wakes a talk event of an NPC on F02, or the reverse; a key gate opens a room with the next step |
| **Consequence** | a status, item or counter change on the choice (`addStat`, `give`, `addCounter humanity -1`) |

### 11.3 The elder's story as one graph

```
 F03 ev_bell_cracked ─► F05 ev_bell_ringer_key (gives the key) ─► F04 gate ─► ev_bell_ringer_room
        ─► F06 ev_bell_rope ┐
        ─► F02 ev_bell_clapper ┴─► F03 ev_bell_rings (insight +1) ──────────────┐
                                                                                 ▼ insight ≥ 1
 F02 elder: ev_elder_north_road (trust +1)          ev_elder_bell_rumour (DEF +1, trust +1 | resolve +1)
            ev_elder_well_night ──next──► ev_elder_ledger (trust +1 + gem if well_helped)
              ├ torch (needs well_secret ◄── chat line after 8 talks): STR +2, trust +2
              ├ climb: ATK +2, trust +1
              └ pay: gold +30, humanity −1 ──► the ledger is lost; the after-dialogue needles you
              trust reaches 3 ──► ev_elder_trusted (varChanged: a notice, humanity +1)
                         trust ≥ 3 and F05 cleared ──► ev_elder_last_wish (gem, max HP +20)
                                                         └─► ev_elder_potion (repeatable ×3: 20 gold → a potion)
```

Two chains, two floors' worth of tile events and one NPC's topics meet through a counter (`insight`) and a
variable (`elder_trust`), and no code knows about any of them.

### 11.4 Writing a new story

1. **Plan the graph** first (as above): steps, who or where each fires, what links them.
2. **Variables:** add them in the event editor's Variables tab (`story/vars.json`), with ranges.
   Use the NPCs tab's **talk preview** to try the topic order and conditions before playing.
3. **Events:** in the event editor: kind, texts (title, description, text; zh_TW and en), *Fires when*,
   *Active only when*, *Then do*, *Then fire*; for a talk event its **Dialogue**, **After finishing** and
   **Menu order**.
4. **Dialogues:** the event editor's Dialogues tab (**+ Dialogue**): a node per page, a choice per branch, *finishes
   the event* on the choices that end it, an `id` on choices later steps ask about.
5. **NPCs:** the NPCs tab (**+ NPC**: name, greeting, sprite, chat lines), and a place on a floor (the stage editor).
6. **Check:** `tools\test.cmd` runs `talk_system_test` (and the screenshots) for these files. It names every
   bad reference; the event editor's Problems tab shows the same for events.
7. **Play it:** `toms_game --goto=<floor> --events-done=<ids> --give=<items>` starts at any point of the story.

## 12. Decisions (owner, 2026-10-16)

| # | Question | Decision |
|---|---|---|
| 1 | Inactive events in the menu | **Hidden** |
| 2 | Only one open topic | **Plays directly** when every other bound event is inactive (or finished and hidden). If a finished event is still listed, **show the menu** so the player can pick |
| 3 | Finished topics | **Per event:** hidden, or kept with an **after-dialogue** (thanks, or retelling how brave the player was), assigned in the editor (section 6.4) |
| 4 | Menu order | **A fixed order** (pool order, `topicOrder` to change it; section 5) |
| 5 | The talk index | `loop` by default, plus `stay` and **`random`**; hidden messages that need many talks use `talkCountAtLeast` on a chat line (section 4). Talking about a topic does not move the index; it does count as a talk |
| 6 | "Increase STR" | **Results are open:** any player status by name (`addStat` / `setStat`, STR included), an item as a gift (`give`), a variable (`setVar` / `addVar`), in any mix (section 7) |
| 7 | Chat before the menu | **No:** the menu has an optional one-line `greeting` |
| 8 | First NPC | **The villager** (the village elder, on F02), with five events as real test data |
| 9 | Editing dialogues | **Cards:** one per node, with its choices (section 9) |
| 10 | Repeatable events | **Every time, with an optional limit** (`maxTimes`); `next` wakes only on the first run (section 6.5) |
| 11 | An event with no tile and no NPC runs | **Its title as a notice** (section 11.1) |
