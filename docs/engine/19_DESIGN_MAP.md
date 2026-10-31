# 19 — Design map: how the game is put together, and how extendable it is

中文：[zh_TW/19_DESIGN_MAP.md](zh_TW/19_DESIGN_MAP.md).

**Open [design_map/design_map.html](design_map/design_map.html) in a browser** (a local page, no server, nothing online):
tabs for the layers, one frame, the screens and how they switch, the drawing order, events, talking, battle, and an
extendability review. This page holds the code references behind it. Read from the code on 2026-10-31.

## 1. Layers

| Layer | Where | Facts |
|---|---|---|
| Host | src/game/src | main_sdl.cpp (frame loop, desktop + web), GameSession (game_session.cpp:225 `frame`), GameUi (RmlUi documents) |
| Game | src/core/game/core | one class, game.h 1,544 lines (~300 functions, ~350 fields, a regex estimate) over game.cpp and 12 `game_*.cpp` files, about 9,100 lines; map_objects.cpp: one MapObject subclass per map thing |
| Rules | src/core/game/systems | 28 pure modules, 21 with their own `*_test.cpp` (cue_track, gesture, touch_play also in play_blocks_test) |
| Engine | src/core/engine, src/engine | IRenderer → BgfxRenderer, resource manager (resources.json keys), anim (CharacterRig, node effects), particle_fx, condition, event_bus, glTF, audio, RmlUi |
| Content | assets/data | read by key (res_keys.h); a new entry needs no code |
| Screens | assets/media/ui | 12 RmlUi documents filled from `UiState` (27 `Ui*` structs, ui_state.h); buttons call `Game::uiEvent(name)` |

## 2. One frame

- `appFrame` (main_sdl.cpp:446) → `GameSession::frame` (game_session.cpp:225) → `bgfx::frame()` (bgfx_host.cpp:188).
- Inside `frame`, in order: `Game::update` (:230), keys (:234-332), mouse (:336-359), ImGui (:362), title 3D (:374), `Game::draw` (:378),
  the UI (`sync`, RmlUi update and render, :379-387), the ImGui render (:388), the HUD text (:389).
- **The update runs before this frame's keys.** A press acts on the next update.
- `Game::update` (game.cpp:177) has no scene switch: each part checks its own flag (battle time, rigs, timers, mini game,
  the battle block, the arena, walking, the camera).

## 3. Screens and switching

- **No scene enum.** The screen follows from flags: `title_.isOpen()`, `activeEndingId_`, `storeOpen`, `cs.active` / `cs.won`,
  `inDialogue`, `inGameMenuOpen_`, `saveLoadOpen_`, `stageSelectOpen_`, `stairsConfirmOpen_`, `miniGame_.open`, `notices_`
  (predicates in game.h:423-541). `modalActive()` (game.h:449) stops walking and talking.
- The priority is written three times: the key chains (game_session.cpp:249-332), `buildUiState` (game_ui.cpp:224-228:
  store > battle > talk > walk), `draw` (game_scene_draw.cpp:35-46).
- RmlUi stacking is each document's body `z-index`: hud 10, battle 30, dialogue 30, store 40, minigame 40, player 45,
  stage_select 45, saveload 50, dialogs 50, title 60, ending 70, overlay 90.

## 4. Drawing order (bgfx views)

| View | id | What |
|---|---|---|
| kViewClear | 0 | clear |
| title scene | 16+ | glTF (title only) |
| layers | 200+ | node-effect pictures, inner first |
| kViewGame | 1 | the world: tiles → map objects; or the battle backdrop → fighters → particles (`drawBattleRig`, game_combat.cpp:526) |
| kViewUi | 2 | RmlUi |
| kViewTop | 3 | `setLayer(1)` sprites over the UI (the battle track's hints) |
| kViewOverlay | 4 | ImGui |

Order set by `applyViewOrder` (bgfx_renderer.cpp:1023). One sprite batch per frame; meshes, GPU particles and layer
pictures are markers in it.

## 5. Events

- Records in `events/pool_*.json` (loader game_player_menu.cpp:319); links on map things in `.stage.json` `objects."x,y".events`.
- Triggers: an event tile (map_objects.cpp:294), linked things incl. a defeated monster (`fireLinkedEvents`,
  game_story.cpp:278), `talk` topics, `chain` (`wakeEvent`, :268), `varChanged` (:290).
- Finish: `completeEvent` (game_story.cpp:250) → `finishEvent` (game_input.cpp:220): flag, `next`, stair locks.
- Conditions: 19 leaves + all / any / not (condition.cpp). Actions: `story_actions` (11 verbs) + `runDialogueAction`
  (about 12 Game verbs). Monster events (enemies.json) have their own action keys (game_combat.cpp:275).
- Gaps: event tiles have id-matched effects in code (map_objects.cpp:313); no `eventDone` condition leaf; chapter beats are
  not run; the event bus only feeds missions.

## 6. Talking

- `npcs/*.json`, `dialogue/*.json`, topics = pool events with `trigger.on: "talk"`.
- `talkTo` (game_story.cpp:166) → `planTalk` (talk_system.cpp:133: chat line / one topic / menu) → `openDialogue` (:46) →
  `chooseDialogue` (:89) → `endDialogue` (:123).
- Not supported: talking during a battle (game_combat.cpp:349).

## 7. Battle

- State: `CombatState cs` (game.h:118), reset by `startCombat`.
- Attack: the weapon's play (`armTouchPlay`, game_skills.cpp) or the track; answers hit at once (`playHit`); the end resolves
  through `resolveCombo`.
- Monsters: `stepFoes` → `resolveEnemyClockFire` (rhythm, the guard modes, special guard plays) → monster events.
- Moves: `startSkill` / `updateSkill` (game_skills.cpp) run a special_moves.json entry: demo → play → finish.
- End: `afterPlayerHit` → `startWinMoment` → `finishCombatWin` → result → `dismissVictory`.

## 8. Extendability

| Kind of change | Cost | Why |
|---|---|---|
| New content (monsters, items, moves, events, NPCs, dialogues, floors, texts) | easy | data only |
| Rule changes | easy | numbers in data, rules in tested pure modules |
| New kinds (an event action, a play kind, a map thing, a condition) | medium | C++ in 2-3 if-chains; no tables |
| A new screen | costly | a flag, three priority lists, a Ui struct, an .rml, uiEvent names |

Suggested order (none blocks today's work):
1. **A screen state machine**: a `Screen` enum and a small stack, one "top screen" function for input, UI and drawing.
2. **Tables instead of if-chains**: name → handler for `uiEvent` (32 + 20 names), story actions (3 chains; the validator does
   not know `tryWeapon`, `chooseWeapon`, `miniGame`), monster event actions, play kinds.
3. **Split Game by area**: battle, dialogue and menu controllers that Game holds.
4. **One action vocabulary** for map and monster events; events that listen on the bus.
5. **Content out of code**: event tile effects into `actions`; an `eventDone` leaf; decide on chapter beats; delete the
   unused combat.json and story.json.
