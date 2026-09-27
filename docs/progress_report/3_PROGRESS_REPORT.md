# Progress Report — Log Part 3 (2026-09-27)

> Chronological log entries, oldest first. The live **Next Step**, status table, waiting list and
> open questions are in [PROGRESS_REPORT.md](PROGRESS_REPORT.md).

---

### 2026-09-27 — the project is the repository root now; old project removed; builds verified

Owner: deleted the old project's data (commit `979bcf3`, 493 files) and asked for toms_next to be the
root with "no more toms_next", then to check the build configuration and build desktop and web.

**State found:** only `toms_next/` was left in the repository; the move itself had not happened on disk
or on `origin`. And toms_next could not build: it compiles the old game code (`../src`) and packs
`../assets`, `../data`, `../external`, `../editor/src`, all deleted.

**Done:**

1. **Moved** every tracked file of `toms_next/` to the root with `git mv` (history kept). The old root
   `.gitignore` (old-project patterns such as `web/`, `build/`) was replaced by toms_next's, plus
   `.claude/`, `legacy/assets/wqy-zenhei.ttc`, `*.log`, `imgui.ini`.
2. **Restored** what the build needs from `979bcf3^` into **`legacy/`**: `src/` (120 files), `assets/`
   (44), `data/` (181), `external/` (5), `editor/src/` (9). Not at the root, because `editor/` and
   `docs/` names collide with the new project's folders. Nothing else from the old project was
   restored (Vulkan/WebGPU renderers, old build files, `web*/`, `docs/EngineBlueprint`): those stay in
   git history only.
3. **Config:** `TOMS_LEGACY_ROOT` defaults to `legacy/` (the one setting the whole build reads for the
   old code and content). Fixed relative paths that assumed the old nesting: the emsdk lookup in
   `build_web.cmd` / `serve_web.cmd` / `check_env.ps1` (`..\..\..\emsdk`), `check_env.ps1`'s content
   checks, and messages that said `TOMS/assets`. Docs README, 01–06 updated for the root layout.

**Lost:** `wqy-zenhei.ttc` (16 MB CJK font). It was gitignored, never committed, and deleted from
disk with the old folder. The desktop game falls back to Microsoft JhengHei (the checker and the
configure both say so); the web build does not need it. To restore the shipped look: download it
again into `legacy/assets/`.

**Verified (fresh configure from the root, Windows 11, VS 2026, Qt 6.9.0, Emscripten 6.0.9):**

- `tools\build.cmd ci-windows`: exit 0 → `toms_game.exe`, `toms_editor.exe`, `shaderc.exe`.
- `toms_game`: D3D11, stage 1 after a scripted Enter, assets from `legacy/assets`, JhengHei
  fallback, no leaks. `toms_editor`: D3D11 Play tab, no leaks.
- `tools\build_web.cmd release`: exit 0, emsdk found through the new relative path, host shaderc
  from `out/build/ci-windows`; `.wasm` 2.94 MB, `.data` 0.74 MB.
- Web, headless smoke test: title → new game → save → reload restores the save.
- Web, visible Chrome at the owner's DPR 2.4: canvas 3336×1939 for 1390×808 css; resizes to
  1000×700 and 890×821 followed. One resize (to 1600×800, wider than the screen at 240%) left the
  canvas at the previous size in two runs while Chrome was drawing only ~9 frames in that interval,
  i.e. throttling the (clamped / partly covered) test window; the next resize caught up. Not
  reproduced at normal frame rate the day before. Recorded as W13 to check by hand.

**Cleanup:** the old `toms_next/` build output (11.8 GB) was deleted; one empty file inside it is
still locked by TortoiseGit's status cache (`TGitCache`), so an empty `toms_next/` folder may remain
until that lets go.

### 2026-09-27 (later) — layout: `assets/` = all content, `src/` = all code

Owner: "the data structure folder is wrong, please make assets a completely different folder first,
then make source code at the same folder". `legacy/` (mixed code + content) is gone:

| Was | Now |
|---|---|
| `legacy/assets` | `assets/media` (the old Vulkan `shaders/` dropped: bgfx does not use them) |
| `legacy/data` | `assets/data` (must stay the sibling of `media`: the game code reads `<media>/../data`) |
| `engine/`, `game/`, `editor/` | `src/engine`, `src/game`, `src/editor` |
| `legacy/src` | `src/legacy` (the original game code, still compiled unmodified) |
| `legacy/editor/src` | `src/editor/stage` |
| `legacy/external` | `src/third_party` |

CMake now names each location instead of one `TOMS_LEGACY_ROOT`: `TOMS_SRC_DIR`, `TOMS_LEGACY_SRC`,
`TOMS_THIRD_PARTY_DIR`, `TOMS_STAGE_EDITOR`, `TOMS_ASSET_DIR`, `TOMS_DATA_DIR`; the configure checks
every one and that `data` sits next to `media`. Checker, messages and docs follow.

**Two traps found and fixed on the way:**
- `git mv` of the previous step's files, which were only staged, not committed, moved them on disk
  but left the index at the old paths; `git add -A` re-staged the tree (index and disk now match).
- **The old `.gitignore` rule `save/` (meant for runtime saves) also matched the source folder
  `src/legacy/game/save/`**: `save_system.cpp`, `save_slots.cpp`, `game_settings.cpp` and friends
  (7 files) would have been left out of the commit silently. Anchored to `/save/`; all 120 files of
  `src/legacy` are tracked now.

**Verified:** desktop `ci-windows` build exit 0; `toms_game` loads from `assets/media` (D3D11, stage 1,
no leaks); `toms_editor` Play tab (no leaks); web `release` build exit 0 and the headless smoke test
passes (new game → save → reload restores the save).

### 2026-09-27 (later) — `src/legacy` renamed to `src/core`

Owner: "legacy" is not a good name; it suggests the folder can be deleted, but it is the game's own
code (gameplay, story, combat, saves, UI) and stays. Renamed to **`src/core`** (the core game code,
next to `src/engine`, `src/game`, `src/editor`). CMake: `TOMS_LEGACY_SRC` → `TOMS_CORE_SRC`, target
`toms_legacy_game` → `toms_core`; comments, checker and docs follow ("the core game code"). Phase 3
in 04_MIGRATION_PLAN.md is reworded: `src/core` is rewritten in place, not moved out. (The entries
above still say `legacy`; they describe the state at the time.)

**Verified:** desktop and web builds exit 0; `toms_game`, `toms_editor` and the web smoke test pass.
