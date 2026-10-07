# 06 — Event logic: triggers, conditions, actions, links

**Status:** design, and the editor mockup ([mockups/04_tutorial.html](mockups/04_tutorial.html), part 5,
and its **Logic graph** node editor). The game does not run this yet; section 6 lists the runtime work.

An event is more than an id, a text key and the floors that roll it. It can:

- **fire** when the player steps on its tile, **talks to someone**, or **a variable changes**, or when
  another event **links** to it;
- be **active only when** variables, counters, flags or finished events say so;
- **do** things: set or add to a variable, add to a counter, set a flag, give an item;
- **fire other events directly** (`next`).

## 1. Format

The new fields live on the event's record in its pool file. A record without them is unchanged and
keeps today's behaviour (fires on its tile, no conditions, no actions).

```json
{
  "eventId": "ev_village_elder_thanks",
  "kind": "rescue",
  "text": "ev_village_elder_thanks.text",
  "trigger":  { "on": "talk", "npc": "villager" },
  "requires": { "type": "varAtLeast", "var": "well_clues", "value": 2 },
  "actions":  [ { "type": "addCounter", "counter": "insight", "delta": 1 } ],
  "next":     [ "ev_village_grave_new" ]
}
```

### trigger: when it fires

| `trigger` | Fires when | Needs a tile |
|---|---|---|
| absent, or `{ "on": "step" }` | the player steps on its tile (today's behaviour) | yes |
| `{ "on": "talk", "npc": "villager" }` | the player talks to that NPC (after the dialogue closes) | no |
| `{ "on": "varChanged", "var": "well_clues" }` | that event variable changes | no |
| `{ "on": "chain" }` | only when another event's `next` links to it | no |

Whatever its trigger, an event fires only while `requires` is true, and once per run (`"once": false`
can be added later for repeatable events).

### requires: when it is active

`requires` uses the condition format the game already evaluates (`src/core/engine/condition.cpp`):
a leaf, or `all` / `any` / `not` of leaves. The editor shows a leaf, or an `all` of leaves, as rows;
anything else is kept exactly as written and shown as JSON.

| Leaf | Meaning | Status |
|---|---|---|
| `{ "type": "varAtLeast", "var": "x", "value": 2 }` | event variable x ≥ 2 | **new** |
| `{ "type": "varEquals", "var": "x", "value": 2 }` | event variable x = 2 | **new** |
| `{ "type": "counterAtLeast", "counter": "insight", "min": 1 }` | story counter ≥ 1 | exists |
| `{ "type": "runFlagSet", "flag": "flag_truth_told" }` | a flag is set | exists |
| `{ "type": "runFlagSet", "flag": "event_ev_village_well" }` | an event is finished | exists (see 6.2) |

The other existing leaves (`choiceMade`, `itemHeld`, `statAtLeast`, `stageCleared`, …) work too.

### actions: what it does

| Action | Meaning | Status |
|---|---|---|
| `{ "type": "addVar", "var": "x", "delta": 1 }` | add to an event variable | **new** |
| `{ "type": "setVar", "var": "x", "value": 3 }` | set an event variable | **new** |
| `{ "type": "addCounter", "counter": "insight", "delta": 1 }` | add to a story counter (clamped) | exists (dialogue action) |
| `{ "type": "setFlag", "flag": "flag_truth_told" }` | set a run flag | **new** verb; `run_.setFlag` exists |
| `{ "type": "give", "itemId": "gem_def" }` | give an item | exists (dialogue action) |

### next: direct links

`next` lists events to fire right after this one, in order. Each still checks its own `requires`.

### Event variables: `assets/data/story/vars.json` (new)

```json
{ "well_clues": { "type": "int", "default": 0, "scope": "run" } }
```

`scope` follows `flags.json`: `run` resets with a new game and on rebirth, `meta` survives rebirth.
Counters (`counters.json`) and flags (`flags.json`) keep their own files; events can read and write
them too.

## 2. Example: the elder's thanks (tutorial part 5)

```
ev_village_well   ──+1──▶ ◆ well_clues ──≥ 2──▶ ev_village_elder_thanks ──then──▶ ev_village_grave_new
ev_village_ledger ──+1──▶        ▲                   ▲            │
                                                💬 villager       └──+1──▶ ▲ insight
```

Finding the well and the ledger each add a clue. Talking to the villager after both fires the
elder's thanks, which adds insight and fires the grave event directly. That event needs no tile, so it
stops being an orphan.

## 3. Editor

- **Inspector → Logic:** *Fires when* (step / talk to an NPC with its floors / a variable changes / a
  link); *Active only when* (rows: subject, ≥ or =, value); *Then do* (rows: action, target, amount);
  *Then fire* (linked events, click one to open it); *Fired by* (who links to it).
- **Variables tab:** event variables (create, default, scope, delete), counters and flags, each with
  *changed by* and *read by*.
- **Logic graph tab: a node editor** after [thedmd/imgui-node-editor](https://github.com/thedmd/imgui-node-editor)
  (MIT), in its Blueprint style:
  - nodes for events (▶ Fire and conditions in; Then ▶, *done* and actions out), event variables,
    counters, flags and NPCs; pins are ▶ for "fires" and ● for values, filled when connected;
  - Bézier links: white fires an event, purple / teal / red / gold carry a variable / counter / flag /
    "event done"; the selected node's links animate;
  - **drag pin → pin to connect**: pins that fit light up green, and the link edits the same data as
    the inspector (talk → Fire sets the trigger, Then → Fire adds `next`, a value → a condition, an
    action → a variable). Drop a link on empty space for a menu of nodes that fit;
  - right-click: add a node; click a link, then Delete, to remove it; drag nodes; box-select; right-drag
    to pan; wheel to zoom; F to fit; ⛶ to maximize. Node positions are kept in the browser (not in game
    data).
- **Values map tab:** every event variable, counter and flag in the middle, the events that change it on
  the left and the events that read it on the right, with badges ✎ changed by · 👁 read by · ⇄ linked
  values (values that share an event). Filter by name and type; *show values nothing uses* adds the rest.
  **Hover** a value: how many events change it, read it, fire when it changes, and which values it is
  linked to; everything unconnected fades. The same tooltip appears on value names in the Variables tab
  and on value nodes in the Logic graph.
- **Categories and the filter:** every event variable, counter and flag can have a `category`
  (`"category": "village/clues"`, saved in `vars.json`, `counters.json` or `flags.json`; `/` makes
  sub-categories). Without one, a value is grouped automatically: flags by the chapter in their `setBy`
  (`Chapter ch_03`), counters as *Story counters*, the rest as *Uncategorized*. The **Variables** tab lists
  values by category (collapsible headings; ✎ renames a category and its sub-categories; the category is
  editable on each row and when creating a variable). One filter is shared by the Variables tab and the
  Values map: text (name, category, description, HUD label, set by), a category (a parent includes its
  sub-categories) and the type chips. The Values map shows category headings, and the condition and
  action pickers group event variables by category. The game ignores the extra field (it reads
  `counters.json` field by field and does not load `flags.json` at all).
- **Value tabs:** click a value (or double-click its node in the Logic graph) to open it in its own
  closable tab: `◆ well_clues ✕`. The tab edits its data and lists its connections with a mini map:
  - event variable: name (renaming updates every action, condition and trigger that uses it), type,
    default, scope, description (zh_TW / en); delete;
  - counter: HUD label (zh_TW / en), *show on HUD at* (`displayAt`), clamp;
  - flag: scope, *set by*. A flag whose `setBy` names an event (`ev_…`) counts as changed by that event.
  - every kind: category.
  Counters and flags are not renamed here: chapter and dialogue files and the game code use their
  names. **Save…** shows the changes to `vars.json`, `counters.json` and `flags.json` with the rest.
- **Checks:**

| Check | Severity |
|---|---|
| A condition, action or trigger names a variable, counter, flag, item or event that does not exist | ⛔ |
| `next` links to an event no pool defines | ⛔ |
| A `chain` event no event links to; a `talk` event whose NPC is placed on no floor | ⚠ never fires |
| `var ≥ N` where no event changes the variable and its default is below N | ⚠ never active |
| Events that link back to themselves through `next` | ⚠ loop |
| An event variable nothing reads or writes | ⓘ |

An event whose trigger is not `step` needs no floor, so it is no longer reported as an orphan.

## 4. Save

Records with logic are written out in full; records without it stay on one line as today. A new
`story/vars.json` is created the first time an event variable is added.

## 5. HTML first, then Qt

The node editor is plain HTML, CSS and SVG, with no library. Doing it in HTML was not hard: the
tutorial page has it working, with Play demo driving it by real mouse events. For the Qt/C++ editor
([19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md) section 6), imgui-node-editor itself is a good fit: it is
MIT, needs Dear ImGui 1.72+ and C++14, and the engine already has an ImGui-on-bgfx layer
(`src/engine/src/imgui_bgfx.h`). The graph model and the "what does this link mean" rules
(`linkAction` in the mockup) carry over unchanged.

## 6. Runtime work (not done)

1. **Run state:** event variables (from `vars.json`, saved with the run like counters), and the set of
   finished events.
2. **"Event finished" for every event.** Today `game_input.cpp` sets `event_<id>` only for whispers and
   rescues; traps, caches, relics and shards set nothing. It should be set for all, since conditions
   and the stair locks of [05_FLOOR_LINKS.md](05_FLOOR_LINKS.md) read it.
3. **Condition leaves** `varAtLeast` / `varEquals` in `condition.cpp` (plus the context method).
4. **Actions** `addVar` / `setVar` / `setFlag` next to the existing dialogue actions.
5. **Dispatcher:** fire on tile, on talk (after the dialogue closes), on variable change, and through
   `next`; check `requires` and `once`; guard against loops (an event fires at most once per chain).
6. **Effects from data:** the hard-coded "trap = −8 HP, cache = +10 HP +12 gold" in `game_input.cpp`
   becomes actions on the records.
