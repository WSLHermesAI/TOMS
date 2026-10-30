# TOMS documents

The documents are in three folders (the owner, 2026-10-24). Each folder is numbered from 01 and has its Traditional
Chinese copies in its own `zh_TW/` (中文版 in each row).

| Folder | What belongs there |
|---|---|
| [gameplay/](#gameplay) | the game and its content: battle, stages, events, UI (RmlUi), saves, the stage/event and UI editors |
| [engine/](#engine) | the core engine and its tools: rendering, resources, animation, atlas, particles, fonts, glTF |
| [project/](#project) | setup, building, tests, the team, progress reports |

`docs/examples/` holds example files the tests and the editors open (recipes, tutorials); it is not a document folder.

## gameplay

| Doc | Contents | 中文版 |
|---|---|---|
| [01_RMLUI.md](gameplay/01_RMLUI.md) | the game UI (RmlUi): where each screen lives, how it binds to the game, editing, the UI font | [zh_TW](gameplay/zh_TW/01_RMLUI.md) |
| [02_STAGE_EDITOR.md](gameplay/02_STAGE_EDITOR.md) | the stage editor (HTML, and the Windows app on the game engine) | [zh_TW](gameplay/zh_TW/02_STAGE_EDITOR.md) |
| [03_PLAYER_MENU.md](gameplay/03_PLAYER_MENU.md) | the player menu: status, items, gear, events, system | [zh_TW](gameplay/zh_TW/03_PLAYER_MENU.md) |
| [04_IMAGE_STAGES.md](gameplay/04_IMAGE_STAGES.md) | stage maps as images (design) | [zh_TW](gameplay/zh_TW/04_IMAGE_STAGES.md) |
| [05_SAVE_SYSTEM.md](gameplay/05_SAVE_SYSTEM.md) | the save system: slots, what a save holds, old saves | [zh_TW](gameplay/zh_TW/05_SAVE_SYSTEM.md) |
| [06_FLOOR_REDESIGN.md](gameplay/06_FLOOR_REDESIGN.md) | a look and a cast per chapter of the tower | [zh_TW](gameplay/zh_TW/06_FLOOR_REDESIGN.md) |
| [07_GAMEPLAY_PLAN.md](gameplay/07_GAMEPLAY_PLAN.md) | the gameplay plan: cards, combat, weapons, growth, phases built | [zh_TW](gameplay/zh_TW/07_GAMEPLAY_PLAN.md) |
| [08_TEST_SCENARIOS.md](gameplay/08_TEST_SCENARIOS.md) | test scenarios: start any scene from a setting file | [zh_TW](gameplay/zh_TW/08_TEST_SCENARIOS.md) |
| [09_DOWNLOADS_AND_DLC.md](gameplay/09_DOWNLOADS_AND_DLC.md) | downloads: shared test scenarios and DLC packs (plan) | [zh_TW](gameplay/zh_TW/09_DOWNLOADS_AND_DLC.md) |
| [10_UI_EDITOR.md](gameplay/10_UI_EDITOR.md) | tutorial: the UI editor | [zh_TW](gameplay/zh_TW/10_UI_EDITOR.md) |
| [11_BATTLE_DATA.md](gameplay/11_BATTLE_DATA.md) | battle data: shared stats, fighters, statuses → looks, battle events, who fights whom, formulas (design; points by level built) | [zh_TW](gameplay/zh_TW/11_BATTLE_DATA.md) |
| [12_UI_LAYOUT.md](gameplay/12_UI_LAYOUT.md) | UI layout for landscape and portrait: the canvas that follows the screen, the map, each screen's portrait rules | [zh_TW](gameplay/zh_TW/12_UI_LAYOUT.md) |
| [13_BATTLE_EDITOR.md](gameplay/13_BATTLE_EDITOR.md) | the battle editor (monsters, weapons and their result tiers, the test player and the growth calculator, monster events, one-click battle) and ideas for a real-time strategic battle; HTML mockup and playable demo in [gameplay/battle_editor/](gameplay/battle_editor/) | [zh_TW](gameplay/zh_TW/13_BATTLE_EDITOR.md) |
| [14_WEAPON_PLAY_AND_LOOT.md](gameplay/14_WEAPON_PLAY_AND_LOOT.md) | weapon plays v2 (bow: draw and hit targets; sword: slicing and memory combos; staff: timed circles and magic circles) and the loot grind (farm monsters, chests, go deeper, gear bonuses); the owner's ideas 2026-10-30, questions Q27–Q33 | [zh_TW](gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) |
| [event_editor/](gameplay/event_editor/README.md) | the event editor: data model, design, user guide, tutorial, floor links, logic, key gates, NPC dialogue | [zh_TW](gameplay/zh_TW/event_editor/README.md) |

## engine

| Doc | Contents | 中文版 |
|---|---|---|
| [01_ARCHITECTURE.md](engine/01_ARCHITECTURE.md) | targets, one frame, Qt + bgfx, shaders | [zh_TW](engine/zh_TW/01_ARCHITECTURE.md) |
| [02_THREADS.md](engine/02_THREADS.md) | the job system, and threads on the web | [zh_TW](engine/zh_TW/02_THREADS.md) |
| [03_ART_STYLES.md](engine/03_ART_STYLES.md) | art styles (AI-redrawn art, switched in Settings) | [zh_TW](engine/zh_TW/03_ART_STYLES.md) |
| [04_ATLAS_TOOL.md](engine/04_ATLAS_TOOL.md) | the atlas tool (sprites → one texture) | [zh_TW](engine/zh_TW/04_ATLAS_TOOL.md) |
| [05_ANIMATION.md](engine/05_ANIMATION.md) | node animations (`.anim`) and the anim editor | [zh_TW](engine/zh_TW/05_ANIMATION.md) |
| [06_ANIMATION_RECIPES.md](engine/06_ANIMATION_RECIPES.md) | writing simple `.anim` clips by hand (or by AI) | [zh_TW](engine/zh_TW/06_ANIMATION_RECIPES.md) |
| [07_PARTICLES.md](engine/07_PARTICLES.md) | particle effects (`.particle`) and the particle editor | [zh_TW](engine/zh_TW/07_PARTICLES.md) |
| [08_GLTF.md](engine/08_GLTF.md) | glTF 3D models | [zh_TW](engine/zh_TW/08_GLTF.md) |
| [09_FONT_TOOL.md](engine/09_FONT_TOOL.md) | picture fonts (the font tool) | [zh_TW](engine/zh_TW/09_FONT_TOOL.md) |
| [10_FONT_TOOL_GUIDE.md](engine/10_FONT_TOOL_GUIDE.md) | the font tool user guide | [zh_TW](engine/zh_TW/10_FONT_TOOL_GUIDE.md) |
| [11_EDITOR_LANGUAGE.md](engine/11_EDITOR_LANGUAGE.md) | the editors' language switch (English / 中文) | [zh_TW](engine/zh_TW/11_EDITOR_LANGUAGE.md) |
| [12_RESOURCE_MANAGER.md](engine/12_RESOURCE_MANAGER.md) | the resource manager and objects from JSON | [zh_TW](engine/zh_TW/12_RESOURCE_MANAGER.md) |
| [13_AI_RESTYLE.md](engine/13_AI_RESTYLE.md) | restyling a game's art with AI (moved to `AIGamestyle/`) | [zh_TW](engine/zh_TW/13_AI_RESTYLE.md) |
| [14_AI_SPRITES.md](engine/14_AI_SPRITES.md) | new TOMS sprites with AI | [zh_TW](engine/zh_TW/14_AI_SPRITES.md) |
| [15_LEAK_CHECK.md](engine/15_LEAK_CHECK.md) | the memory leak check (Windows) | [zh_TW](engine/zh_TW/15_LEAK_CHECK.md) |
| [16_MESH_ANIMATION.md](engine/16_MESH_ANIMATION.md) | mesh animation: replacing Spine | [zh_TW](engine/zh_TW/16_MESH_ANIMATION.md) |
| [17_SKINNING_TUTORIAL.md](engine/17_SKINNING_TUTORIAL.md) | tutorial: skinning, deforming and weight painting | [zh_TW](engine/zh_TW/17_SKINNING_TUTORIAL.md) |
| [18_NODE_EFFECTS.md](engine/18_NODE_EFFECTS.md) | node effects (shaders by hierarchy, afterimages) and the brawler showcase: the anim editor vs Spine | [zh_TW](engine/zh_TW/18_NODE_EFFECTS.md) |

## project

| Doc | Contents | 中文版 |
|---|---|---|
| [01_INSTALL_WINDOWS.md](project/01_INSTALL_WINDOWS.md) | every prerequisite, the checker and popups, Qt and font setup | [zh_TW](project/zh_TW/01_INSTALL_WINDOWS.md) |
| [02_VISUAL_STUDIO.md](project/02_VISUAL_STUDIO.md) | build, run and debug in Visual Studio | [zh_TW](project/zh_TW/02_VISUAL_STUDIO.md) |
| [03_MIGRATION_PLAN.md](project/03_MIGRATION_PLAN.md) | the phases from the Vulkan project to Qt + bgfx | [zh_TW](project/zh_TW/03_MIGRATION_PLAN.md) |
| [04_TROUBLESHOOTING.md](project/04_TROUBLESHOOTING.md) | configure, build and runtime problems | [zh_TW](project/zh_TW/04_TROUBLESHOOTING.md) |
| [05_BUILD_WEB.md](project/05_BUILD_WEB.md) | the browser version | [zh_TW](project/zh_TW/05_BUILD_WEB.md) |
| [06_BUILD_ANDROID.md](project/06_BUILD_ANDROID.md) | the Android build and the emulator | [zh_TW](project/zh_TW/06_BUILD_ANDROID.md) |
| [07_PUBLISH_GITHUB_PAGES.md](project/07_PUBLISH_GITHUB_PAGES.md) | publishing the web version to GitHub Pages | [zh_TW](project/zh_TW/07_PUBLISH_GITHUB_PAGES.md) |
| [08_TESTS.md](project/08_TESTS.md) | the tests: `tools\test.cmd`, changed-only selection, reference images | [zh_TW](project/zh_TW/08_TESTS.md) |
| [09_NEW_MACHINE_SETUP.md](project/09_NEW_MACHINE_SETUP.md) | starting from scratch on a new computer | [zh_TW](project/zh_TW/09_NEW_MACHINE_SETUP.md) |
| [10_SSH_KEY_SETUP.md](project/10_SSH_KEY_SETUP.md) | SSH keys, and using them on another computer | [zh_TW](project/zh_TW/10_SSH_KEY_SETUP.md) |
| [11_TEAM_PLAN.md](project/11_TEAM_PLAN.md) | the team plan: a programmer, a game designer and an artist | [zh_TW](project/zh_TW/11_TEAM_PLAN.md) |
| [PROGRESS_REPORT.md](project/PROGRESS_REPORT.md) | the progress report | [zh_TW](project/zh_TW/PROGRESS_REPORT.md) |
| [progress_report/](project/progress_report/PROGRESS_REPORT.md) | the status board, daily reports, the working log | [zh_TW](project/zh_TW/progress_report/) |

## Old numbers

Before 2026-10-24 every document was in `docs/` with one number. Old commits, notes and messages may use them:

| Old | New | Old | New | Old | New |
|---|---|---|---|---|---|
| 01 | engine/01 | 14 | engine/04 | 27 | engine/14 |
| 02 | project/01 | 15 | engine/05 | 28 | gameplay/05 |
| 03 | project/02 | 16 | engine/06 | 29 | gameplay/06 |
| 04 | project/03 | 17 | engine/07 | 30 | gameplay/07 |
| 05 | project/04 | 18 | engine/08 | 31 | gameplay/08 |
| 06 | project/05 | 19 | gameplay/02 | 32 | gameplay/09 |
| 07 (Android) | project/06 | 20 | gameplay/03 | 33 | engine/15 |
| 07 (Pages) | project/07 | 21 | gameplay/04 | 34 | engine/16 |
| 08 | gameplay/01 | 22 | engine/09 | 35 | engine/17 |
| 09 | project/08 | 23 | engine/10 | 36 | project/11 |
| 10 | engine/02 | 24 | engine/11 | 37 | gameplay/10 |
| 11 | project/09 | 25 | engine/12 | | |
| 12 | project/10 | 26 | engine/13 | | |
| 13 | engine/03 | | | | |
