# 20 — Player menu: one button for everything (plan)

> 中文版：[zh_TW/20_PLAYER_MENU.md](zh_TW/20_PLAYER_MENU.md)

**Status:** **P1, P2 and P3 built 2026-10-10** (§9, §11, §12); upgrading gear (W26) is open. Q11–Q15 were answered by the owner on
2026-10-10 (listed in [progress_report/PROGRESS_REPORT.md](progress_report/PROGRESS_REPORT.md)).

The walking scene keeps **one button**, ≡. Everything else that is a button today (the backpack, the
store, the fullscreen button, the old ≡ list) moves **into the player menu**, so the screen shows only
the map, the stats and that button (§2).

The player menu has five tabs:

1. **Status:** the player's attributes and progress.
2. **Equipment:** change the weapon and the armor.
3. **Items:** what the player owns, as a grid of 3 per row on the left; the selected item's
   information, price and actions (Use / Equip / Drop) on the right.
4. **Events:** the player's missions and events, laid out like Items: the list on the left, the
   selected one's details and next step on the right.
5. **System:** Store, Skills, Forge, Village, Save, Settings, Fullscreen, Back to title.

The game UI is RmlUi ([08_RMLUI.md](08_RMLUI.md)); the event data and rules this menu reads are in
[event_editor/06_EVENT_LOGIC.md](event_editor/06_EVENT_LOGIC.md) §7.

## 1. What exists today, and what this plan changes

| Today | In this plan |
|---|---|
| HUD: `ATK DEF LV`, `GOLD EXP`, keys, `道具 x3 (I)` (`hud.rml`); the item line is clickable | the text stays, **not clickable** any more |
| HUD buttons: ≡ (`hud_menu`) and the coin **store** button (`hud_store`, locked until unlocked) | **only ≡** stays; it opens the player menu. The store button goes: Store is a row on **System** |
| ≡ menu (Esc): Save, Settings, Skills, Village, Forge, Back to title (`menu.rml`) | **becomes the System tab** (same rows and pages). Forge still makes new gear; the Equipment tab wears it |
| Web page buttons over the canvas: ⛶ fullscreen, 背包 backpack (`src/game/web/shell.html`) | **both removed**: Fullscreen is a row on **System**; the backpack is the **Items** tab |
| Backpack (I key): list + detail, Use / Drop (`inventory.rml`) | **becomes the Items tab** (one item screen, not two) |
| Equipment: `equipped_` (weapon, armor, talent; `equipment.json`) is set from the store / forge; there is no screen to change it | the **Equipment** tab |
| Missions run in `missionTrackers_`, chapters have `exitConditions`, events have no player-facing log | the **Events** tab shows them, with a next-step line |
| Player stats: `hp maxhp atk def gold exp lv` + keys (`Player`, `game.h`); level-up numbers are in code | shown on **Status**, plus **new attributes** STR, DEX, AGI, VIT, INT, LUK from a settings file `stats.json` (§3) |
| Item data in three files: `items.json` (map items), `equipment.json`, `store.json` (own ids, prices, names) | **one all-item file**, `items.json`: every item's id, type, stats, price and text keys (§5) |

## 2. One button: ≡

**The walking screen after this change:**

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ 第 3 層 · 黃泉道                                                       [ ≡ ]•│
│ HP ███████████░░░ 82/150   ATK 20 DEF 7 LV 4   GOLD 45 EXP 37   Y1 B0 R0     │
│                                                                              │
│                              (the map)                                       │
│                                                                              │
│ (touch pad, phones only)                                                     │
└──────────────────────────────────────────────────────────────────────────────┘
```

- **≡** is the only button in the walking scene, top right, big enough for a thumb. The dot **•** on it
  means "something new inside": the store was unlocked, the event log changed, skill points are waiting,
  new gear was got. The tab with the news has its own dot; opening it clears that part.
- The touch pad on phones stays: it is movement, not a menu button.
- Banners, the chapter card and the story footer stay; they are not buttons.

**Opening it:**

| How | Opens |
|---|---|
| ≡ (click / tap), **Esc** | the menu on the last tab used (first time: Status) |
| **C** | Status |
| **I** | Items (as today's backpack key) |
| **B** | the Store directly (as today), if unlocked |
| inside the menu: **Q** / **E** or **Tab**, or clicking a tab | switch tabs |
| inside the menu: **Esc**, ✕, or ≡ again | close |

- Only in the walking phase: not in battle, dialogue, the store or on the title.
- **The game pauses** while it is open, as with every modal today (`modalActive()`); roamers do not move.
- **Phones:** the panel fills the screen; tabs are big buttons along the top; below the small-screen
  threshold (§8) the left/right split of Equipment, Items and Events becomes list-then-detail (tap to see
  the detail, ◀ to go back).

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ [ 狀態 ] [ 裝備 ] [ 道具 ] [ 事件 • ] [ 系統 ]                            ✕ │
├──────────────────────────────────────────────────────────────────────────────┤
│                         (the selected tab's page)                           │
└──────────────────────────────────────────────────────────────────────────────┘
```

**The split layout.** Equipment, Items, Events and System share one layout, so they look and work the same:

| Left | Right |
|---|---|
| a list (Items: a grid of 3 per row), scrollable, with a filter on top | the selected entry: icon or kind, name, text, numbers, and its action buttons |

Nothing selected → the right side says "Select an item" (or "Select an event"). The selection is kept
when switching tabs and back.

## 3. Tab 1 — Status

```
┌ Status ───────────────────────────────┬──────────────────────────────────────┐
│ [player sprite]   Lv 4                │ Attributes                           │
│ HP   82 / 150  ███████████░░░         │   STR  力量   12                      │
│ EXP  37 / 120  ████░░░░░░  (next Lv)  │   DEX  靈巧    9                      │
│ ATK  20 (+3)   DEF  7                 │   AGI  敏捷   10                      │
│ Gold 45    Keys  Y 1  B 0  R 0        │   VIT  體質   11                      │
│ Floor F03  ·  Chapter 1               │   INT  智力    8                      │
│ Weapon 見習者法杖   Armor —            │   LUK  幸運    5                      │
│ Skills 引氣 · …   Memory shards 2      │ (click one: its description)         │
└───────────────────────────────────────┴──────────────────────────────────────┘
```

**Q11 — answered 2026-10-10:** extend the status the way most RPGs do; the owner will add gameplay for it
later, so the attributes must exist now. The tab shows two kinds of number:

| Kind | Values | What they do |
|---|---|---|
| **Combat values** (today) | HP / max HP, ATK, DEF, Lv, EXP, Gold, keys | what battles and doors use today |
| **Attributes** (new) | STR, DEX, AGI, VIT, INT, LUK, defined in `stats.json` | stored, saved, shown, raised by level-ups, items and equipment. **They change nothing yet**: the gameplay that reads them comes later |

ATK and DEF show the base value and the bonus from equipment and skills, `20 (+3)`
(`effectiveAtk()` / `effectiveDef()`). Clicking an attribute shows its description on the right.
Story counters (`counters.json`) still appear only once they reach their `displayAt`, as a word.

**The settings file `assets/data/stats.json`** (new). Adding, removing or reordering an attribute here
changes the Status tab, the level-up and the save without code changes:

```json
{
  "attributes": [
    { "id": "str", "short": "STR", "name": "stat.str.name", "desc": "stat.str.desc", "base": 5, "perLevel": 1 },
    { "id": "dex", "short": "DEX", "name": "stat.dex.name", "desc": "stat.dex.desc", "base": 5, "perLevel": 1 },
    { "id": "agi", "short": "AGI", "name": "stat.agi.name", "desc": "stat.agi.desc", "base": 5, "perLevel": 1 },
    { "id": "vit", "short": "VIT", "name": "stat.vit.name", "desc": "stat.vit.desc", "base": 5, "perLevel": 1 },
    { "id": "int", "short": "INT", "name": "stat.int.name", "desc": "stat.int.desc", "base": 5, "perLevel": 1 },
    { "id": "luk", "short": "LUK", "name": "stat.luk.name", "desc": "stat.luk.desc", "base": 5, "perLevel": 0 }
  ],
  "levelUp": { "expPerLevel": 30, "atk": 2, "def": 1, "maxhp": 10, "freePoints": 0 }
}
```

| Field | Meaning |
|---|---|
| `id` | the attribute's id; items and equipment name it in their `stats` (`{ "str": 2 }`) |
| `short`, `name`, `desc` | the short label, and text keys in `text.json` for the name and the description |
| `base`, `perLevel` | the value at Lv 1, and what each level-up adds |
| `levelUp` | today's level-up numbers, moved out of the code: EXP needed = `lv * expPerLevel`, and the ATK / DEF / max HP each level gives. `freePoints`: points the player places by hand on each level-up (0 = none; for the later gameplay) |

**In the game:** `Player` gets the attribute values by id (saved with the run); one function
`attr("str")` = base + levels + items used + equipment worn is what the later gameplay reads.

## 4. Tab 2 — Equipment

```
┌ Equipment ──────────────────────────┬──────────────────────────────────────────┐
│ Worn                                │ [icon]  雙生短匕  Twin Daggers             │
│  Weapon [i] 見習者法杖              │ A fast weapon with a short hit zone.      │
│  Armor  [i] —                       │ ─────────────────────────────────────────  │
│ ─────────────────────────────────── │            now        with this          │
│ [ Weapon ] [ Armor ]   ← slot filter│ ATK        20    →    18   (−2)           │
│ ┌────┐ ┌────┐ ┌────┐                │ Charge     slow  →    very fast           │
│ │法杖│ │短匕│ │重斧│                │ Hit zone   15    →    10   (narrower)      │
│ │ ✓  │ │    │ │    │                │ Skill      —     →    疾風連刺              │
│ └────┘ └────┘ └────┘                │                                          │
│                                     │ [ Equip ]                                │
└─────────────────────────────────────┴──────────────────────────────────────────┘
```

- **Left, top:** what is worn in each slot. Clicking a slot selects that slot's filter.
- **Left, below:** the owned equipment for the selected slot (**Weapon** or **Armor**), as the same
  3-per-row grid as Items; the worn one has a ✓.
- **Right:** the selected piece: name, description, and a **comparison with what is worn now**: ATK /
  DEF from its `stat`, the battle bar from its `bar` block in words (charge speed from `v0` / `vmax` /
  `rampTime`, hit zone from `greenHalf`, top multiplier `maxMult`), and the skills it grants (`actives`).
  Better values are green, worse are red.
- **Buttons:** **Equip** (swaps it in; the old one goes back to the list) or **Unequip** for the worn
  one (armor only; a weapon slot always holds one, the starter wand at worst).
- **Talent** (the third slot in `equipment.json`) is not shown here for now (Q15).
- Changing gear is free and allowed any time the menu can open (not in battle).

**Q15 — answered 2026-10-10: change only, for now.** The tab picks which owned weapon and armor are worn;
a stronger weapon means buying or forging a different one.

**Later (not in this plan's phases): upgrading gear.** Making an owned piece stronger, e.g. Twin Daggers →
+1 → +2 for gold or materials, is a separate task for later. It needs new data (what each step costs
and gives) and an **Upgrade** button on this tab's detail. Tracked as W26 in the progress report.
Talent stays off this tab (the default) until that work or the owner says otherwise.

## 5. Tab 3 — Items

```
┌ Items ──────────────────────────────┬──────────────────────────────────────────┐
│ [ All ] [ Use ] [ Gear ] [ Keys ]   │ [big icon]  生命藥水  Health Potion        │
│ ┌────┐ ┌────┐ ┌────┐                │ ─────────────────────────────────────────  │
│ │藥水│ │寶石│ │卷軸│   ▲            │ Restores 50 HP.                           │
│ │ x2 │ │ x1 │ │ x1 │   █            │ Effect   HP +50                          │
│ └────┘ └────┘ └────┘   │ scroll     │ Price    8 gold                          │
│ ┌────┐ ┌────┐ ┌────┐   │            │ You have 2                               │
│ │黃鑰│ │法杖│ │短匕│   ▼            │                                          │
│ │ x1 │ │ ✓  │ │    │                │ [ Use ]  [ Drop ]                        │
│ └────┘ └────┘ └────┘                │                                          │
└─────────────────────────────────────┴──────────────────────────────────────────┘
```

- **The grid (left):** the items the player **owns** (Q13), **3 per row**, as many rows as needed,
  **scrolled** (mouse wheel, drag on touch, ↑↓←→ with the keys). Each cell: icon, a count badge (**x2**:
  copies stack into one cell; today each copy is its own row), ✓ on worn equipment, a 🔒 mark on important
  items. Filter chips on top: **All**, **Use** (consumables), **Gear** (weapons and armor), **Keys**.
- **The detail (right), when an item is clicked:** big icon, name (current language), description,
  **effect** as readable lines (`HP +50`, `ATK +3`, `STR +2`, …) from its `stats`, **price**, how many the
  player has, and the buttons that fit it:

  | Item | Buttons |
  |---|---|
  | consumable (potion, gem, scroll, …) | **Use**, **Drop** |
  | weapon / armor | **Equip** (or **Unequip** if worn), with the same comparison as the Equipment tab; **Drop** only if not worn and not important |
  | important item (keys, story items) | no Drop: the button is greyed out with "Important items can't be dropped" |

  **Drop** asks for confirmation ("Drop 生命藥水? It is gone for good."). The worn piece can never be dropped.
- **Keys:** Enter = the first button, Del = Drop.

**Q12 — answered 2026-10-10:** every item's price comes from a settings file, and there is **one file for
all items**. `assets/data/items.json` becomes that file: one record per item, for every kind of item in
the game (map items, keys, consumables, weapons, armor, talents, story items):

```json
{
  "potion_red": {
    "type": "consumable", "sprite": "potion_red.png",
    "name": "item.potion_red.name", "desc": "item.potion_red.desc",
    "stats": { "hp": 50 }, "price": 8, "important": false
  },
  "twin_daggers": {
    "type": "weapon", "sprite": "twin_daggers.png",
    "name": "item.twin_daggers.name", "desc": "item.twin_daggers.desc",
    "stats": { "atk": -2, "dex": 2 }, "price": 120,
    "bar": { "v0": 30, "vmax": 320, "rampTime": 1.0, "greenHalf": 10 }, "actives": ["swift_stab"]
  },
  "key_yellow": {
    "type": "key", "sprite": "key_yellow.png",
    "name": "item.key_yellow.name", "desc": "item.key_yellow.desc",
    "stats": { "key_yellow": 1 }, "price": 10, "important": true
  }
}
```

| Field | Meaning |
|---|---|
| the record's key | the item id, used everywhere: on the map (`item:potion_red`), in the store, in saves, in rewards |
| `type` | `consumable`, `key`, `weapon`, `armor`, `talent`, `story`; decides the filter, the buttons and the equipment slot |
| `sprite` | the icon |
| `name`, `desc` | **text keys** in `text.json` (six languages there), not inline text |
| `stats` | what it gives: for a consumable, when used (`hp`, `atk`, `def`, `gold`, `exp`, keys); for gear, while worn; any attribute id of `stats.json` works (`str`, `dex`, …) |
| `price` | its price in gold, shown in the detail; also the store's price |
| `important` | `true` = can't be dropped (keys, story items) |
| `bar`, `actives` | weapons / armor only: the battle-bar settings and granted skills (moved from `equipment.json`) |

**What changes in the other files:**

- `equipment.json` merges into `items.json` (`type` weapon / armor / talent) and is removed.
- `store.json` keeps only **what** the store sells (a list of item ids), its unlock floor and its
  price rule; the names, sprites, descriptions and its own ids (`potion_hp`) go. The store charges the
  item's `price`; if its "doubles after each purchase" rule stays, it starts from that price.
- The inline six-language `name` / `desc` objects move into `text.json` as `item.<id>.name` / `.desc`.
- The footprint test (every item on every floor is something the game picks up) checks against the
  new file, and a new check fails when an item has no `price`, no `type` or a text key missing.

## 6. Tab 4 — Events

The Events tab uses the same split as Items: a list on the left, the selected entry on the right.

```
┌ Events ─────────────────────────────┬──────────────────────────────────────────┐
│ [ Unfinished (3) ] [ All (8) ]      │ ● 村長的謝意                               │
│ ★ 第 1 章 · 我為何舉劍              │ ─────────────────────────────────────────  │
│ ◆ 石巨人退治                        │ The elder wants to thank whoever brought   │
│ ● 村長的謝意                   ◀    │ his grandson home. He waits by the well.   │
│ ✓ 迷途的孩子                        │                                          │
│ ✓ 古井的低語                        │ Connected                                │
│                                     │   迷途的孩子 ✓                             │
│                                     │   古井的低語 ✓                             │
└─────────────────────────────────────┴──────────────────────────────────────────┘
```

**Q14 — answered 2026-10-10:** the detail shows only the **title** and the **description**, and, if the
entry is connected to other events, **only the names of those events**. So every event, mission and
chapter has a title and a description (text keys); there are no progress counts, next-step lines or
rewards in this tab.

- **What is listed:** only what the player has **started** or **finished**: the current chapter, accepted
  missions, started events and finished ones. Missions not yet offered and events not yet met are never
  listed.
- **The switch on top:** **Unfinished** (default): in progress now. **All**: in progress first, then the
  finished ones (✓, greyed, newest first).
- **Each row:** a kind mark (★ chapter, ◆ mission, ● event, ✓ finished) and the title.
- **The detail (right), when a row is clicked:**
  1. the title;
  2. the description;
  3. **Connected:** the titles of the events linked to this one (✓ on finished ones). Clicking one selects
     it when it is listed. A connected event the player has not met yet shows as **？？？**, so the
     names do not spoil what is ahead. No "Connected" heading when there are none.
- **A new entry or a finished one** flashes a small "Event log updated" toast in the walking scene, and
  puts a dot on ≡ and the Events tab until it is opened.

| Kind | Title and description from | Connected | Listed from | Finished when |
|---|---|---|---|---|
| ★ **Chapter** | `story/chapters/ch_XX.json` `title` (exists) and `desc` (new) | the chapter's story events (`beats` of kind `event`) | the chapter starts | its `exitConditions` hold |
| ◆ **Mission** | `missions.json` `title`, `desc` (new) | — (unless a later field links them) | accepted | complete (claimed) |
| ● **Event** | the event record's `title`, `desc` (new, [event_editor/01](event_editor/01_DATA_MODEL.md)) | events linked by the event logic: its `next`, the events its `requires` waits for, and the events that wait for it | started | finished |

When an event counts as **started** or **finished**, which events are listed, and how "connected" is
worked out are defined with the event logic: [event_editor/06_EVENT_LOGIC.md](event_editor/06_EVENT_LOGIC.md) §7.

## 7. Tab 5 — System

The rows of today's ≡ menu, plus the two that were buttons elsewhere. A row that opens a page (Skills,
Forge, Village, Settings) shows it **on the right side of the tab**; Store opens the store screen as
today, and closing the store comes back to this tab.

```
┌ System ─────────────────────────────┬──────────────────────────────────────────┐
│  商店 Store            •            │  (the selected row's page: Skills, Forge, │
│  功法 Skills           2 points     │   Village, Settings; or a short line of   │
│  鍛造 Forge                         │   what the row does)                      │
│  村莊 Village                       │                                          │
│  存檔 Save                          │                                          │
│  設定 Settings                      │                                          │
│  全螢幕 Fullscreen      on / off     │                                          │
│  回到標題 Back to title             │                                          │
└─────────────────────────────────────┴──────────────────────────────────────────┘
```

| Row | Was | Note |
|---|---|---|
| Store | the HUD coin button | greyed, with when it unlocks, until unlocked (the button's old `locked` state) |
| Skills, Forge, Village, Settings, Back to title | the ≡ list (`mainMenuOrder()`) | the same pages and order rules (a row the old menu hides stays hidden) |
| Save / Load | the ≡ list's Save (saved at once) | its own screen: choose the slot, and confirm every save and load (§10) |
| Fullscreen | the web page's ⛶ button | web and desktop; hidden on Android (always full screen). Shows the current state |

**Fullscreen from inside the game (web).** Browsers allow fullscreen only inside a click or key handler,
but a click on an RmlUi row is handled later, in the next game frame. So the row works in two halves:
on **press** (mousedown / touchstart) the game tells the page (`EM_ASM` → `tomsFullscreenArm()`), and the
page's own **pointerup** / **keyup** listener, which runs inside the gesture, calls
`requestFullscreen()` / `exitFullscreen()`. Desktop toggles `SDL_SetWindowFullscreen`. The page reports
`fullscreenchange` back (`jsFullscreenChanged`) so the row shows on / off, also when the player leaves
with the browser's own Esc.

## 8. Building it

| Piece | Where |
|---|---|
| The panel and its five tabs | `assets/media/ui/player.rml` + `.rcss` (one document, tabs inside), replacing `inventory.rml` and `menu.rml`; one shared split-layout style for tabs 2–5 |
| The 3-per-row grid | RmlUi `display: flex; flex-wrap: wrap` cells at a third of the list width, inside an `overflow-y: auto` box (scrolling is supported, [08](08_RMLUI.md) "Not supported" lists what isn't); the selected cell is scrolled into view when moving with the keys |
| Its data | `UiPlayer` in `src/core/game/ui/ui_state.h` (`tab`, `status.*`, `gear.*`, `items.*`, `events.*`, `filter`, `sel`), filled in `Game::buildUiState()`; buttons through `Game::uiEvent()` (`player_tab`, `attr_select`, `item_select`, `item_use`, `item_drop`, `gear_equip`, `gear_unequip`, `event_filter`, `event_select`, …), as [08](08_RMLUI.md) "Adding a screen" describes |
| Attributes | `stats.json` loaded at boot; `Player` holds the values by id; level-up reads `levelUp` instead of the numbers in `game_combat.cpp` / `game_inventory.cpp` / `game_story.cpp`; saves store the values (old saves get `base` + levels) |
| The all-item file | `items.json` read once into one item table; `equipmentDefs_` built from its weapon / armor / talent records; the store reads its id list; `entSprite` and the footprint test use the same table |
| Equipping | the existing `equipped_` (`equipment_system.h`); `effectiveAtk()` etc. update at once |
| Important items | `important` in `items.json`; `item_drop` refuses them (the UI greys the button too) |
| Opening / closing | `Game` state `playerMenuOpen_`, `playerTab_`; keys Esc, C, I, B, Q/E/Tab; the HUD's `hud_menu` opens it; `modalActive()` includes it |
| The HUD | `hud.rml`: remove `#store-btn` and the `#res` click; ≡ gets the news dot (`hud.menu_news`) |
| System tab | today's `InGameMenuPage` pages (`menu.rml` content) move into `player.rml`; `menu.rml` is removed; `openInGameMenu()` opens the player menu on System |
| The web page | `src/game/web/shell.html`: remove `#toms-fullscreen`, `#toms-backpack` and their handlers; add `tomsFullscreenArm()` and the `fullscreenchange` report; `jsInventory` stays for tests (opens Items) |
| Event log | `Game::eventLog()` → the entries of §6, from the chapter, the missions and the event state of 06 §7 |
| Text | every label, attribute, item, event, mission and chapter title / description in `assets/data/text.json` (six languages); new characters → `tools/make_ui_font.py` |
| Small screens | uses `uiScale` like the dialogue box, laid out so the scaled panel still fits (see the 2026-10-08 dialogue fix) |

**Phases:**

| Phase | Work | Needs |
|---|---|---|
| **P1** | The panel and keys; the **one-button HUD** (store button, item-line click and the web page's buttons removed; the news dot); **System** (today's ≡ pages moved in, Fullscreen); **Status** with the attributes of `stats.json`; **Equipment** (change weapon / armor, comparison); **Items** as the grid, with stacking, filters, effect lines, price, Use / Equip / Drop and the important flag; **Events** with ★ chapters and ◆ missions | `stats.json`; the all-item `items.json` (merge `equipment.json`, store ids, text keys, `price`, `type`, `important`); titles / descriptions for chapters and missions |
| **P2** | ● Events in the Events tab, with Connected; the event editor gets **Title** and **Description** fields and a problem rule for missing ones | 06 §7 runtime work (finished-event set, started state); a title and description written for every event |
| **P3** | The gameplay that uses the attributes (the owner's later design) | design and balancing |
| **Later** | Upgrading gear (+1, +2…), W26 | the owner's design, new data |

**Tests:**

- **Screenshot tests**, one per tab, scripted with `--keys` like `smoke.inventory`: `smoke.player_status`,
  `smoke.player_gear`, `smoke.player_items` (more than 9 items, scrolled), `smoke.player_events`
  (Unfinished and All), `smoke.player_system`; plus one at `--ui-scale=1.5` (the phone layout).
- **Goldens to refresh:** every screenshot with the HUD in it changes (the store button is gone), and
  `smoke.menu` / `smoke.inventory` become the System and Items tabs.
- **Web:** the headless-Chrome check opens ≡ → System → Fullscreen and checks `document.fullscreenElement`,
  and that the page has no buttons over the canvas.
- **Unit tests:** `stats.json` drives level-up (values per level, an added attribute appears without
  code changes, old saves load); every item has `type`, `price` and both text keys; the store sells only
  ids that exist; equipping changes ATK / DEF, attributes and the battle bar; an important item and the
  worn piece cannot be dropped; stacking; the event-log entries for a scripted run (a chapter, a mission,
  the elder's-thanks events of 06 §2), their connected names (？？？ for unmet ones), and that unmet
  missions and events are not listed.

## 9. P1 as built (2026-10-10)

**Done:** the one-button HUD, the five tabs, the merged item data and the attributes, on desktop and web. P2 (event
entries with Connected) and P3 (attribute gameplay) are not started; upgrading gear is W26.

| Part | Where |
|---|---|
| The menu's logic: tabs, keys, grids, buttons, the event log | `src/core/game/core/game_player_menu.cpp` |
| Items, use / pickup, level-ups, attributes, owned gear | `src/core/game/core/game_inventory.cpp` |
| Its rules as plain functions (stacking, the grid cursor, dropping, level-ups) | `src/core/game/systems/player_menu_rules.h`, tested by `player_menu_test.cpp` |
| The screen | `assets/media/ui/player.rml` + `player.rcss` (`inventory.rml` and `menu.rml` are gone) |
| Data | `assets/data/items.json` (all items; `equipment.json` is gone), `store.json` (`{ "item": id, "growth": n }` entries), `stats.json` (new), `missions.json` and the chapters (`title`, `desc`), `text.json` |
| Saves | the run save keeps the attributes, the owned gear and what is worn (`player.attrs`, `gear`, `weapon`, `armor`, `talent`; older saves load: attributes are rebuilt from `stats.json`). Worn gear was not saved before |
| Fullscreen | the host: `main_sdl.cpp` (desktop: `SDL_SetWindowFullscreen`; web: `tomsToggleFullscreen` in `shell.html`; Android: hidden) |

**How it behaves, where the plan left room:**

- **Keys:** Esc / ≡ open the last tab; C Status, I Items, B the store; Q / E / Tab switch tabs; arrows move; Enter = the
  selected entry's first button. Left / right past the edge of a grid switches its filter chip (Items: All, Use, Gear,
  Keys; Equipment: Weapon, Armor). There is no Delete key: Drop is a button, with a confirmation.
- **Items:** the Gear filter also lists talents (they are owned items); the Equipment tab shows weapons and armor only (Q15).
  Using or dropping takes the stack's last copy, so the stack keeps its place.
- **Store:** it charges the item's `price` from `items.json`; `growth` 2 doubles it after each purchase, as before. Gear the
  player already owns shows price "-" and tapping it wears it, free. Store-only potions became items `potion_str` (ATK +1,
  as the store applied it) and `potion_def`; the store's `potion_hp` is the map's `potion_red` (the same HP +40).
- **New run:** no gear, as before. Rebirth: owns and wears the wand, as before.
- **Events (P1):** chapters (from the floor table: listed once one of their floors is reached, done when the last floor is
  cleared) and missions (accepted / claimed). Chapters 5–10 have no title text yet and show their id. No Connected list yet.
- **News dots:** ≡ and a tab get a dot for a new piece of gear, a store that just unlocked, skill points to spend, or an
  event-log change (which also shows "Event log updated" once).
- **Phones:** the menu scales with the whole 1024x768 screen, like every other screen; the list-then-detail phone layout of §2
  is not done.
- **Test hook:** `toms_game --give=<id>,...,gold:<n>` hands the player items when the title closes (`smoke.player_*`).

## 10. Save / Load (2026-10-10)

The System tab's save row is **Save / Load** (存檔／讀檔). It used to save at once into the run's own slot;
now it opens its own screen over the menu (`assets/media/ui/saveload.rml`):

```
┌ 存檔／讀檔 Save / Load ──────────────────────────────────── [ Back ] ┐
│ ( Save ) ( Load )                                                   │
│ ┌ Slot 1  [This run] ────────────────────────────────────────────┐ │
│ │ 村莊外緣・井   LV 1   HP 120/120   0G                             │ │
│ │ Saved 2026-10-10 17:58    Play time 00:12:40                    │ │
│ └──────────────────────────────────────────────────────────────────┘ │
│ ┌ Slot 2 ─ [Empty] ┐   ┌ Slot 3 ─ [Empty] ┐   (one card per slot)     │
└──────────────────────────────────────────────────────────────────────┘
```

- **Save tab:** choosing a slot asks first. An empty slot: "Save to slot 2?" (Save preselected). A used slot:
  "Overwrite slot 1? What is saved there now will be replaced." (**Cancel** preselected).
- **Load tab:** empty slots are dimmed and cannot be chosen. A used slot: "Load slot 2? Progress not saved yet
  will be lost." (**Cancel** preselected). Yes loads it and closes the menu.
- **Which slot the run uses:** saving into a slot makes it the run's slot, as loading one does; autosave writes there
  from then on. The card of the run's slot says **This run**.
- **Keys:** ↑↓ a slot, Q / E / Tab / ←→ Save ↔ Load, Enter = choose / answer, Esc = close the question, then the
  screen. Mouse: click a card, the answers, Back.
- **Code:** `Game::openSaveLoad / saveLoadAsk / saveLoadAnswer / buildSaveLoadUi` (`game_player_menu.cpp`), `UiSaveLoad`
  (`ui_state.h`). The slot files are the same as the title's Continue page (`save/slot<N>.json`; IndexedDB on the web).
- **Tested** with scripted runs on desktop (save to an empty slot, overwrite, load, cancel) and in headless Chrome. No
  reference-image test: every card shows the time it was saved, which differs on every run.

## 11. P2 as built (2026-10-10)

- **Story events in the Events tab** (● ): listed once started, ✓ once finished (newest first), with their title and
  description; **Connected** lists the linked events by title (✓ when finished, **？？？** when not met yet). Chapters
  list their story events the same way. How events are tracked and linked: [event_editor/06](event_editor/06_EVENT_LOGIC.md) §7.
- **Toasts** (pick-ups, level-ups, event lines) wait while the menu is open, and "Event log updated" shows once however
  many entries changed together.
- **Data:** the 34 pool events got drafted titles and descriptions (zh_TW + en; other languages fall back to English).
- **Editor:** Title / Description fields, the per-event and per-kind "in the event log" switches, the new warning.
- **Tests:** `player_menu_test` checks the run state (order, no duplicates, save / load, older saves, a new run) and that
  every pool event has its title and description and real `next` ids. Checked in the game by walking F01's three events
  (ledger, wall, well); the editor in headless Chrome (fields, save preview, the warning, Kinds column, the 29-step demo).

## 12. P3 as built: what the attributes do (2026-10-10)

Each attribute's effects are in `assets/data/stats.json` (`effects`: `stat` and `per`). They count **points above the
attribute's base** (5), so a new Lv 1 character plays exactly as before; they grow with level-ups (+1 a level, LUK 0),
items and gear with attribute `stats`, and hand-placed points. The defaults, all tunable there (per 0 or an empty list
turns one off):

| Attribute | Effect a point | At Lv 10 (9 points above base) | Where it acts |
|---|---|---|---|
| **STR** 力量 | ATK +0.5 (total rounded down) | ATK +4 | every attack, the Super attack, the HUD and Status |
| **DEX** 靈巧 | Attack bar hit zone +0.4 | green zone 15 → 18.6 (stays inside the blue zone) | the attack bar's timing |
| **AGI** 敏捷 | the bars' wait after a tap −15 ms | 1000 → 865 ms (never below 200 ms) | both bars |
| **VIT** 體質 | max HP +3 | max HP +27 | the HP cap (healing, respawn, bars) |
| **INT** 智力 | skill (功法) bonuses +2 % | +18 % | what skills add to ATK / DEF |
| **LUK** 幸運 | gold from battles +3 % | (LUK does not grow by level) | battle rewards |

- **The stats an effect can name:** `atk`, `def`, `maxhp`, `hitZone`, `cooldownMs`, `skillPower`, `goldGain`
  (`player_menu_test` fails on any other). An attribute can have several effects.
- **Status tab:** selecting an attribute shows its description, **Each point: …** and **Now: …**; ATK / DEF and the
  HUD's ATK / DEF line show the totals.
- **Free points:** `levelUp.freePoints` (0 by default) gives that many points a level to place by hand: the Status tab
  shows "Points to spend", a **+** on each attribute, and Enter adds a point to the selected one. Saved with the run.
- **Test hook:** `--give=exp:<n>,points:<n>`.
- **Balance:** these are starting values, not tuned in play; change `per` in `stats.json` to tune.
