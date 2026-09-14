# Docs Folder Guide

What each subfolder under `docs/` is for. If you're looking for "what to read first," start with
`architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md` (the system-level synthesis doc) and
`progress_report/PROGRESS_REPORT.md` (the live status board — its own **Next Step** line always
says exactly what to do next).

| Folder | What's in it |
|---|---|
| [`progress_report/`](progress_report/) | The living status board: `PROGRESS_REPORT.md` (Next Step, milestone status table, a log index, open questions/blockers) plus the dated log itself, split into numbered parts (`1_PROGRESS_REPORT.md`, `2_...`, oldest first) once the single file grew too large. `WORKING_LOG.md` is the older, narrower predecessor log (pre-dates `PROGRESS_REPORT.md`, kept for history). |
| [`architecture/`](architecture/) | How the engine/code is actually built: `GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md` (the end-to-end technical spec every other design doc plugs into), `IMPLEMENTATION_ROADMAP.md` (the milestone plan `PROGRESS_REPORT.md` tracks progress against), `CODE_LAYOUT.md` (what each `src/game/`+`src/engine/` file is responsible for), `NODE_SYSTEM.md` (the 2D scene-graph). |
| [`design/`](design/) | Gameplay/UI design docs for the *main* game: `GAME_DESIGN_DOCUMENT.md` (the pillars/overview), `TITLE_PHASE.md` (New Game/Continue/Settings), the battle system's own design lineage — `FIGHTING_TALKING_DESIGN.md` (original, superseded auto-combat) → `FIGHT_SCENE_DESIGN.md` + `MAIN_BATTLE_SCENE_DESIGN.md` (the Power Bar + Equipment System that shipped) → `BATTLE_SYSTEM_V2_PROPOSALS.md` (the real-time auto-bar redesign that replaced it) — and `ART_AND_ABILITY_DESIGN.md` (sprite/animation/shader-variant pipeline). |
| [`story/`](story/) | The 70-floor main story's content design: `STORY_BIBLE.md` (the 10-act narrative), `STORY_DATA_SCHEMA.md` (the file layout/schema every story JSON under `data/story/` follows), `SIDE_STORIES.md` (the flagship side-quest set). |
| [`building/`](building/) | `BUILD_WINDOWS.md` / `BUILD_WEB.md` — how to actually build each target. (Named `building/`, not `build/`, on purpose — `.gitignore` has a bare `build/` rule for the CMake output directory that matches at any depth, so `docs/build/` would have been silently untracked.) |
| [`Roguelike/`](Roguelike/) | A **separate, proposed** game mode, not the main 70-floor story — its own index (`GAMEPLAY_ROGUELIKE_INDEX.md`), gameplay, tech-arch and data-schema docs. Not built; a design proposal set. |
| [`demos/`](demos/) | Standalone playable HTML mockups referenced by the design docs (`fight_scene_demo.html`, `main_battle_scene_demo.html`, `battle_v2_realtime_demo.html`) — self-contained, open directly in a browser, no build step. |
| [`BackpackDemo/`](BackpackDemo/) | A standalone inventory-UI prototype (mouse+touch 9-grid backpack), separate small app with its own `README.md`. |
| [`assets/`](assets/) / [`screenshots/`](screenshots/) | Reference images: `assets/` holds source art references (e.g. `assets/roguelike/` for the Roguelike proposal); `screenshots/` holds actual captured screenshots of the running game used as evidence in the design docs/progress log. |

## Cross-reference note

Docs were reorganized into these folders on 2026-09-14 (they used to sit flat under `docs/`).
Live references elsewhere in the repo (other docs, C++ source comments, `tools/*.py`) were updated
to the new paths. The **frozen historical log entries** inside `progress_report/1_PROGRESS_REPORT.md`
through `6_PROGRESS_REPORT.md` were **not** rewritten — they're a point-in-time record of what was
true when each entry was written, so a bare filename mentioned there (from before this reorg
existed) is left as-is rather than edited after the fact.
