# Progress Report — TOMS on Qt6 + bgfx

> **Purpose:** the living status board for TOMS on Qt6 + bgfx and its migration plan
> ([../04_MIGRATION_PLAN.md](../04_MIGRATION_PLAN.md)). Read the **Next Step** line first when
> resuming work: it says what to do next. Below it are the phase status table, the **Waiting to
> finish** list, a **Log** index (the dated entries live in `1_PROGRESS_REPORT.md`,
> `2_PROGRESS_REPORT.md`, …, oldest first), and **Open Questions**, meaning decisions that need
> the project owner before work continues.
>
> The game's code is `src/core` (compiled unmodified until phase 3) and its content is `assets/`.
> Until 2026-09-27 this project lived in a `toms_next/` subfolder; it is now the repository root
> and that folder no longer exists (older dated entries still mention it).

---

## ▶ Next Step

**Resource manager, objects by class, the stage editor's palette and layout** (2026-10-17; not committed;
[DAILY_2026-10-17.md](DAILY_2026-10-17.md), [../25_RESOURCE_MANAGER.md](../25_RESOURCE_MANAGER.md)):
- The engine: a resource cache by path with a manifest (`assets/media/resources.json`), shared atlases, a memory budget,
  hot reload, a missing-file message; render objects made from JSON by `"type"`; UTF-8 paths on Windows.
- The game: a class per thing on a floor (`map_objects.h`), drawn by status; the map part of `Game::draw` is ~20 lines.
  Every game screenshot unchanged.
- The stage / event editor: one dockable window; the freezes fixed (floor switch 1.1–1.5 s → 0.15 s in Debug);
  palette click opens the details; + on every group, new doors with their keys, your own categories, Chinese ids;
  a key's tint shown; View → Default layout.
- Changed-only run: 75 of 112, all pass. **First:** `tools\test.cmd all` with the web build (four new `toms_core`
  files, `vfs.h`), then commit; refresh `Editor/bin`.
- **Owner:** next is batching (§4.11) or stage / event data on the resource manager?

**The stage and event editors as a Windows app, native** (2026-10-16 evening; committed `149856a`;
[DAILY_2026-10-16.md](DAILY_2026-10-16.md) rows 11–16, [../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md) section 6):
- `stage_event_editor.exe`: both editors on the game's stack (Qt / C++ / bgfx), tabs or separate windows; the stage map
  drawn by the game renderer (animations play); the event editor with the tree, inspector, condition rows, Problems,
  Floor map, Variables, Kinds, NPCs with the talk preview, Dialogues. Checked against the HTML editors.
- **Next:** refresh `Editor/bin` (`tools\package_editors.cmd`; it adds the app); `tools\test.cmd all` with the web
  build; then the event editor's drawn views (diagram, values map, node graph) and ▶ Play from here.

**Editor release folder; NPCs and their events; NPC / dialogue editors; fonts** (2026-10-16; committed `090736d`,
`4067852`; [DAILY_2026-10-16.md](DAILY_2026-10-16.md), [WORKING_LOG.md](WORKING_LOG.md)):
- NPCs and their events ([../event_editor/08_NPC_DIALOGUE.md](../event_editor/08_NPC_DIALOGUE.md)): NPC files, chat lines,
  topic menu, choices with results, after-dialogues, repeatable and `varChanged` events, one action list
  (`story_actions.h`); the village elder's seven events on F02 as the example.
- Event editor: NPCs tab with a talk preview, Dialogues tab (cards), condition rows everywhere, *Talked to on*; stage
  editor: an NPC's panel.
- Fonts: the subset rebuilt (30 missing characters), `data.ui_font_covers_text`, a system fallback font (Windows,
  Android; Android not seen running).
- Changed-only run: 69 of 107 tests, all pass. Committed, with the reference images. `tools\test.cmd all` with the web
  build is still to run (`toms_core` has two new files).
- **Owner:** read the elder's drafted text; names and topics for the other five NPCs when wanted.

**Font tool, editor languages, the bell event chain with key gates, glTF lights / cameras / Unicode paths**
(2026-10-14, later; not committed; [DAILY_2026-10-14.md](DAILY_2026-10-14.md) rows 12–20, [WORKING_LOG.md](WORKING_LOG.md)):
- Font tool (`fontpack`, `font_editor`; [../22_FONT_TOOL.md](../22_FONT_TOOL.md)): picture fonts from one picture per
  character; `FontText` in `toms_core` for the game (no screen uses it yet).
- Every Qt editor switches English / 中文 from its menu bar ([../24_EDITOR_LANGUAGE.md](../24_EDITOR_LANGUAGE.md)).
- "The Silent Bell": six chapter-1 events across F02–F06, a key gate (`gate:<item>`) and its room on F04, event
  `actions` in the game, finished event tiles no longer come back ([../event_editor/07_KEY_GATES.md](../event_editor/07_KEY_GATES.md)).
- glTF viewer: the file's lights light the scene (ambient 0.1), camera buttons, Unicode file names.
- Atlas: extrude ring dimmed in the page view; extrude defaults to 1.
- Changed-only run: 96 of 101 tests, all pass. **First:** `tools\test.cmd all` (with the web build) and commit,
  including the untracked reference images (now also `bell_gate`, `gltf_lights`, `gltf_camera_side`).
- **Owner decisions:** keep finished events gone from their floor (caches no longer pay out on every visit)? The bell
  chain's choices (key used up, F06 as the top, an event in the room, the reward, draft text).

**Editors: MPDI import, plist import, x, y, z anim data, the edit freeze fixed** (2026-10-14, not committed;
[DAILY_2026-10-14.md](DAILY_2026-10-14.md), [WORKING_LOG.md](WORKING_LOG.md)):
- Atlas tool: imports Cocos / TexturePacker `.plist`. Anim editor: Convert → MPDI Folder to .anim, covering the old
  engine's `.mpdi` and `.pi` (47 of 48 old files convert).
- Anim data: position, rotation and scale are x, y, z (file version 2; version 1 still loads). The rotation order is
  the old engine's (Rx · Ry · Rz). Drawing is still 2D through an orthographic flatten.
- The anim editor's stall on large files is fixed: undo compared JSON dumps of the whole file on every edit.
- Mouse position readout on the editor canvases; double-click plays a clip; Show Grid removed.
- New ctests: `anim.mpdi_convert`, `anim.editor_selftest` (now offscreen, with Qt's offscreen plugin copied),
  `studio.canvas_coordinates`. Changed-only run: 44 of 93 tests, all pass.
- **First:** commit the 9 untracked reference images in `tests/golden/`. Their tests are already committed, so a fresh
  clone fails them.
- **Next import:** `.prt` / `.prtg` particles (a Convert menu in the particle editor).

**glTF physics: KHR_physics_rigid_bodies on Jolt** (2026-10-12, committed 2026-10-14 in `83b1461`; [../18_GLTF.md](../18_GLTF.md) §7):
- `Model::physics` reads `KHR_implicit_shapes` + `KHR_physics_rigid_bodies`: shapes, materials, filters, joints, and
  motion / collider / trigger / joint on nodes. `gltf::PhysicsWorld` (Jolt Physics 5.6, MIT, deterministic)
  simulates them and writes the bodies back into the `Pose`. Compounds, kinematic and animated bodies, triggers with
  events, combine rules, filters and joints (limits, drives) are all supported; the approximations are listed in §7.2.
- `gltf_viewer`: simulates on open, with a Physics panel and collider boxes. The extension author's 7 sample files
  load and simulate.
- Tests: `unit.gltf_physics_test` (35 checks), `smoke.gltf_physics`. Changed-only run: 85 of 90 tests, all pass.
- `falling_cube.glb` (old engine) is not a physics file: it is 50 baked soft-body frames as separate meshes.
- Not yet: the game, web and Android builds (W36); no TOMS content uses physics.

**The owner's answers built: Q6, Q10 (a), English, locked stairs, the event index, the image-map design**
(2026-10-12, not committed; [6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md) §22):
- **Q6, then the generator removed.** First built: the floor generator, restored, asked before replacing hand-edited
  floors. Then the owner chose to edit floors only in the web stage editor, so the generator and its guard were removed
  again (they stay in git history). Nothing rewrites the floors any more ([../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md) §3).
- **Q10 (a), every `_v2`…`_v5` id is its own event:** 92 new events (the chapter 4–10 pools `pool_act04`…`pool_act10`,
  28 more in `pool_common`) with their own English title, description and text. No floor names an undefined id any
  more (W22 closed).
- **English is the default language.** Settings v2 moves an old saved Chinese setting to English; every text still goes
  through its key. The 197 "English" strings that were Chinese copies are English now. All screenshots refreshed.
- **Q7, locked stairs** ([../event_editor/05_FLOOR_LINKS.md](../event_editor/05_FLOOR_LINKS.md) §3.2, §5):
  - the game reads a floor's `stairs` list;
  - a locked stair is drawn with a padlock and says which event opens it;
  - finishing the event pops up *"Stairs unlocked — The stairs on floor 3 that lead to floor 4 are now open."*;
  - a **"Go to the stairs on floor 3"** event starts, and finishes when the player takes those stairs;
  - the unlocking event's description names the stairs.

  No shipped floor has a lock yet: the tests lock F01 with `--stair-lock=`.
- **Q10, the per-floor event index:** `assets/data/events/floor_index.json`. For each floor it lists the events (kind,
  title, tiles), the beats and the stair locks, plus event → floors. The HTML editors rebuild it when they open the
  project folder and after every Save (`tools/web_editors/floor_index.js`); no Python step.
- **Q9, the design for stage maps as images** (one integer id per pixel, a shared id table): [../21_IMAGE_STAGES.md](../21_IMAGE_STAGES.md),
  with three questions for the owner (§6).
- **Fixed on the way:** a new game now starts on F01's `@`, not on its stair. The ● / ◆ row icons of the event log were
  missing from the UI font.
- **Tests:** desktop 83 / 83 (unit 42 / 42 with the new `stair_links_test` and `floor_index_test`; smoke + glTF all
  pass, 2 new: `stairs_locked`, `stairs_unlock`). The event editor's Play demo passes 29 / 29 on its frozen tutorial data.

**Crowds: instanced skinning from an animation texture** (2026-10-12, not committed; [../18_GLTF.md](../18_GLTF.md) §6,
[6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md) §21):
- Many copies of an animated glTF model in one draw call per primitive; each copy has its own clip and time, and can
  **cross-fade or hold a blend of two clips**. The animation is baked once into a texture all copies share, as joint
  rotation (quaternion) + translation, so blends keep limb lengths. No CPU posing per copy.
- `gltf::AnimTexture` / `gltf::Crowd` (bake, save / load with a stale check, play / crossFade / setBlend),
  `GltfRenderer::uploadAnimTexture` / `drawCrowd`, shader `vs_mesh_animtex.sc`, viewer `gltf_viewer --crowd=<n>`.
- Checked: one copy is pixel-identical to the normal `Pose` path; D3D11 / D3D12 / Vulkan / OpenGL agree; the web build
  compiles it. Tests: unit 40 / 40 (new `gltf_anim_texture_test`), glTF screenshots 21 / 21 (4 new).
- Replaces the idea of the old engine's `cAnimTexture` (its problems are listed in 18 §6). Not used by the game yet
  (the game is 2D; the title scene has no characters).

**The player menu is built: P1, P2, P3 and Save / Load** (2026-10-10, not committed;
[../20_PLAYER_MENU.md](../20_PLAYER_MENU.md) §9–12, [6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md) §16–20):
- **One button while walking, ≡** (or Esc), with a dot when something is new. The HUD's store button, its clickable item
  line and the web page's fullscreen and 背包 buttons are gone.
- **Five tabs:**
  - **Status:** level, HP / EXP bars, ATK / DEF with bonuses, and the attributes STR, DEX, AGI, VIT, INT, LUK with what
    each point does;
  - **Equipment:** change weapon and armor, with a comparison;
  - **Items:** owned items as a 3-per-row grid; Use / Equip / Drop; keys and important items cannot be dropped;
  - **Events:** chapters, missions and story events, Unfinished / All, title, description, connected events;
  - **System:** Store, Skills, Forge, Village, **Save / Load**, Settings, Fullscreen, Back to title.
- **Save / Load:** a slot screen; every save and every load asks first (Cancel preselected for overwrite and load).
- **The attributes act:** STR ATK, DEX attack hit zone, AGI bar wait, VIT max HP, INT skill power, LUK battle gold, per
  point above 5, so Lv 1 plays as before. Optional hand-placed points (`levelUp.freePoints`).
- **Data:** one all-item file `items.json` (`equipment.json` merged in), new `stats.json`, titles and descriptions for
  chapters, missions and the 34 pool events; the game records every event as started / finished.
- **Tests:** 39 / 39 unit (new `player_menu_test`), 57 / 62 screenshot (the 5 failing are W16, W23, as before), five new
  `smoke.player_*` scenes; web build checked in headless Chrome (menu, Fullscreen, Save / Load).

**Owner, next (game):**
- **Try the player menu** (≡ or Esc while walking) and say what to change.
- **Play a few floors** (the key and door fixes too) and **tune** `assets/data/battle.json` and `assets/data/stats.json`:
  the attribute effect sizes are first guesses (W28).
- **Read the drafted event titles and descriptions** in the event editor and rewrite what does not fit (W29).
- **Commit** the 2026-10-08 and 2026-10-10 work.
- **W16 and W23 are closed** (2026-10-12): every desktop test passes (79 / 79).
- Later: upgrading gear (W26), the phone layout of the menu (W27), the rest of the event runtime
  ([../event_editor/06_EVENT_LOGIC.md](../event_editor/06_EVENT_LOGIC.md) §6: variables, talk triggers, the dispatcher).

**The game, 2026-10-08** ([6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md) §12–15):
- **Monsters animate on the map:** an idle per enemy, sillier with level
  (`assets/media/anim/monster_idle.anim`, editable in anim_editor).
- **Battle settings file** `assets/data/battle.json`: the bars' slow start, top speed and wait after
  a tap.
- **Fixed:**
  - keys on the generated floors were invisible and could not be picked up, so doors could be dead ends;
  - items were drawn as floor;
  - doors now open once, with an animation, and stay open.

**The event and stage editors work in HTML** (2026-10-05 → 10-08, [6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md)):
- **One folder:** [tools/web_editors/](../../tools/web_editors/index.html). Run `serve.cmd`, then switch
  editors with **⇄**.
- **Event editor:**
  - events, floor links with locked stairs, event logic, a node graph, a values map;
  - event **kinds** (new `assets/data/events/kinds.json`);
  - a 29-step tutorial with Play demo.
- **Stage editor:** paints the floors, places events, and has a story check and safe auto-place.
- **Saving:** both editors save straight into `assets/data` with *Open project folder*, with backups.
  The stage editor can also download many floors as one zip.
- **Names:** ids, variables, kinds and floors can be named in Traditional Chinese.
- **Docs:** Traditional Chinese versions in [../zh_TW/](../zh_TW/).

**Owner, next:**
- **Confirm the event editor** by trying it on `http://localhost:8000/tools/web_editors/index.html#event`.
  Confirming starts the Qt/C++ version on the game renderer ([../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md) §6).
- **Answer Q6–Q10** below (generated floors, stair locks, rebirth, the `@` start, event lists).

**3D is in the engine** (2026-10-05, [5_PROGRESS_REPORT.md](5_PROGRESS_REPORT.md), [../18_GLTF.md](../18_GLTF.md)):
- **glTF 2.0:** skins, morph targets, instancing, PBR, glTF cameras and lights, and shadow maps for
  directional, point and spot lights.
- **Viewer:** `gltf_viewer`, plus 17 screenshot tests.
- **In the game:** the title screen plays `VirtualCity.glb` behind its menu.

**Owner, first:**
- **Publish:** run `publish_web.bat nobuild` and answer **y** (W18). The web package is built and
  tested; only the push needs your GitHub sign-in.
- **Still due:** the **legacy import bridge** (`.prt`/`.prtg` → `.particle`, `.pi`/`.mpdi` →
  atlas/`.anim`).

**The content tools are in** (2026-10-03, [4_PROGRESS_REPORT.md](4_PROGRESS_REPORT.md)):
- **Three editors:** `atlas_editor`, `anim_editor` and `particle_editor`. All three preview with
  toms_game's own renderer.
- **Particles:** the new `.particle` JSON format. Particles simulate on the GPU when an emitter
  asks for it or is over the `particleGpuThreshold` setting (5000 desktop, 3000 phone).
- **Docs:** [../14_ATLAS_TOOL.md](../14_ATLAS_TOOL.md), [../15_ANIMATION.md](../15_ANIMATION.md),
  [../16_ANIMATION_RECIPES.md](../16_ANIMATION_RECIPES.md), [../17_PARTICLES.md](../17_PARTICLES.md).
- **Due now:** the **legacy import bridge** (`.prt`/`.prtg` → `.particle`, `.pi`/`.mpdi` →
  atlas/`.anim`).

**Phase 2 is done** (2026-09-30): `tools\test.cmd` runs 40 tests in ~80 s: 26 unit tests, 10 screenshot
smoke tests (7 scenes on D3D11, the map on D3D12/Vulkan/OpenGL), and 4 web tests (single- and multithreaded
builds, the published page with and without threads). Visual Studio's Test Explorer lists the same ones.
Guide: [../09_TESTS.md](../09_TESTS.md). Run it before committing.

**Threads are ready for heavier work** (2026-09-30, [../10_THREADS.md](../10_THREADS.md)): a job system
(8 workers here, 4 in the browser, none in the single-threaded web build) and two web builds; the page picks
the threaded one where the browser allows it, and on GitHub Pages a service worker makes that possible.

**Android runs on the emulator** (2026-09-30, [../07_BUILD_ANDROID.md](../07_BUILD_ANDROID.md)):
`tools\build_android.cmd` builds the APK, `tools\run_android.cmd` installs and starts it. Checked on the
Android Studio emulator (Pixel Tablet, API 35): title, new game, touch pad and tap-to-walk, a battle, saves
that survive a force-stop, loading, background → foreground, the Back key. The arm64 phone build compiles.

**Next, the owner picks one track:**

- **A. Android on a real phone**: install `tools\build_android.cmd debug` on an arm64 phone and check it
  as a player (button sizes, text). Also open: audio pause while in the background.
- **B. Phase 3, the rest** ([../04_MIGRATION_PLAN.md](../04_MIGRATION_PLAN.md)): drop
  `src/game/compat/renderer.h` (the core code creates the bgfx renderer directly) and split the
  `Game` class.

Before either, two quick owner checks: play the web build on a real phone (W9) and press F5 once in
the Visual Studio IDE (W1).

---

## Status

| Phase | What | Status |
|---|---|---|
| 0 | Decision: bgfx runtime, Qt6 editor, shipped builds contain bgfx only | ✅ 2026-09-25 |
| 0 | EngineBlueprint docs updated for the decision | ◐ 10 of 11 docs updated; `11_BGFX_QT_ARCHITECTURE.md` not written yet (already linked) |
| 1 | Existing game on bgfx through the compat `renderer.h` (no edits to `src/`) | ✅ verified on D3D11, D3D12, Vulkan, OpenGL |
| 1 | SDL3 game executable `toms_game.exe` (no Qt) | ✅ verified; imports no Qt DLLs |
| 1 | Qt editor `toms_editor.exe`: bgfx Play view, stage list, session panel, old stage editor tab | ✅ verified on D3D11 |
| 1 | Prerequisite checks with popups + links (`check_env`, CMake configure, runtime) | ✅ console output verified; popups not clicked through |
| 1 | Visual Studio presets, `launch.vs.json`, `build.cmd` | ◐ presets built from the command line; not yet pressed F5 in the IDE |
| 1 | Docs 01–05 | ✅ |
| 2 | Web build (bgfx WebGL2): presets, `build_web.cmd`, `serve_web.cmd`, IDBFS saves, page shell, doc 06 | ✅ 2026-09-26, verified in headless Chrome (release + debug) |
| 2 | Tests in CTest: 25 unit, 10 screenshot smoke (4 backends), web smoke; `tools\test.cmd`, doc 09 | ✅ 2026-09-30, 36/36 pass; a changed HP bar is caught |
| 2 | Mobile scaling on web (`setUiScale` / `setPadScale`, the owner's rule) | ◐ wired for small viewports; the RmlUi pad and dialogue scale, other screens do not yet |
| 3 | Edit `src/core` directly: drop `src/game/compat/`, split `Game`, game UI on RmlUi | ◐ all UI on RmlUi (desktop + web) 2026-09-27 ([08](../08_RMLUI.md)); compat shim and `Game` split still open |
| 2 | Job system + multithreaded web build (the page chooses; `coi-serviceworker` for GitHub Pages), doc 10 | ✅ 2026-09-30, verified in headless Chrome; not on a phone or Safari |
| A | Android (`07_BUILD_ANDROID.md`): file access through `vfs.h`, saves, logcat, audio, shaders, linking, the APK (`android/`, `tools\build_android.cmd` / `run_android.cmd`) | ◐ 2026-09-30 runs on the emulator (x86_64); arm64 compiles; not yet on a real phone; audio pause in the background open |
| 4 | Editor: data-driven stage editor on the bgfx viewport, docking, inspector, undo, event editor | ◐ 2026-10-08 HTML versions done ([tools/web_editors](../../tools/web_editors/index.html), [19](../19_STAGE_EDITOR.md), [event_editor](../event_editor/README.md)); Qt/bgfx version waits for the owner to confirm the event editor |
| T | Atlas tool (`.atlasproj` → `.atlas`/`.plist`/XML; Qt + web), doc 14 | ✅ 2026-10-03 |
| T | Node animations (`.anim`, per-channel keys, `anim_editor`), docs 15–16 | ✅ 2026-10-03 |
| T | Particles (`.particle` JSON, `particle_editor`, GPU simulation, CPU/GPU threshold), doc 17 | ◐ 2026-10-03 editor + runtime done; legacy import, multiply/screen blend, effects on `.anim` nodes open |
| T | Editors preview with toms_game's `BgfxRenderer`; right-drag pans | ✅ 2026-10-03 on D3D11 |
| T | Anim editor: per-node playback (once / hide / loop), solo, clip switching with discard prompt; editors: History dock + automatic backups | ✅ 2026-10-05 |
| G | Locked stairs (Q7), the generator's hand-edit guard (Q6), the per-floor event index, English default | ✅ 2026-10-12 desktop; no floor has a lock yet (W34) |
| G | Player menu: one ≡ button; Status / Equipment / Items / Events / System; Save / Load; attributes in battle ([20](../20_PLAYER_MENU.md)) | ◐ 2026-10-10 P1, P2, P3 and Save / Load built, verified on desktop + web (headless); open: W26 upgrading gear, W27 phone layout, W28 tuning, W29 event text review |
| T | Monster idle animations on the map (`monster_idle.anim`), door-opening animation (`door_open.anim`) | ✅ 2026-10-08 desktop; web/Android not seen (W24) |
| 5 | glTF 2.0 on bgfx (`gltf_model`, `GltfRenderer`, `gltf_viewer`): skins, morphs, instancing, PBR, cameras, lights, shadow maps; the title's 3D background | ◐ 2026-10-05 desktop D3D11/Vulkan/OpenGL + web verified; not on a phone; no IBL / cascades yet |
| 5 | Crowds: instanced skinning from a baked animation texture, two-clip blends ([18](../18_GLTF.md) §6) | ✅ 2026-10-12 desktop D3D11/D3D12/Vulkan/OpenGL; web compiles it (not drawn there yet); not used by the game |
| 5 | 3D on bgfx (glTF, ozz skinning, instancing, shadows, Effekseer) | ⬜ researched only |

---

## Waiting to finish (known gaps and unverified items)

| # | Item | Why it is open | How to close it |
|---|---|---|---|
| W1 | **F5 inside the Visual Studio IDE** | builds used the same presets from `tools\build.cmd`; `launch.vs.json` startup items not tried in the IDE | open the `TOMS` root folder in VS, F5 `toms_game` and `toms_editor` |
| W2 | **Popups not clicked through** | they block unattended runs; only the console path and the logic were run | run `tools\check_env.cmd` on a machine missing Qt/font; configure once without Qt |
| W3 | **Fresh-machine test** | this machine already had everything (Qt 6.9.0, VS 2026, SDK); the font no longer needs installing | clone on a clean PC/VM, follow `02_INSTALL_WINDOWS.md` only |
| W4 | **Editor frame rate 26–37 fps** in the smoke test (1972×1634 viewport) | not investigated; may be startup frames, the `QTimer(0)` + vsync interplay, or HiDPI fill | measure with `--stats` over a longer run; compare with `toms_game` at the same size |
| W5 | **`11_BGFX_QT_ARCHITECTURE.md`** in EngineBlueprint + UI-tool rows in docs 02/04/07 | the UI-editor research agent stopped before finishing | redo the research: UI editors whose output bgfx can use (RmlUi, NoesisGUI, Rive, Lottie/ThorVG, …); add the glTF/skinning findings |
| ~~W6~~ | ~~"???????" label on stage 1~~ | closed 2026-09-27: it was an ImGui notification without CJK glyphs; notifications are RmlUi now and show their text | — |
| ~~W7~~ | ~~Vulkan ~1 % pixel difference~~ | closed 2026-09-30: with `--fixed-dt` the backends match the D3D11 reference (Vulkan/D3D12 0.000%, OpenGL 0.022%); smoke tests allow 0.5% | — |
| ~~W8~~ | ~~Commits~~ | closed: the root move, the RmlUi UI and the Android work are committed (up to `d7be854`) | — |
| W9 | **Web build on a real phone/tablet**, Firefox, Safari | only headless Chrome was run (incl. an 844×390 viewport) | open `serve_web.cmd`'s URL from a phone on the same network (serve with `--bind 0.0.0.0`) |
| W10 | **Linux/WSL web build in a browser** | builds and links on WSL (exit 0, 2026-09-26, `817627c`) but was never opened in a browser there, and not re-run since the move to the root | run `tools/web_smoke_test.mjs` against it on a machine with Chrome |
| W13 | **Web resize while the tab is throttled** | in a visible window at DPR 2.4, one resize (1600×800, wider than the screen) kept the old canvas size while Chrome drew ~9 frames; the next resize caught up | resize the real browser window by hand a few times; if it sticks, also react to the browser `resize` event in `shell.html` |
| ~~W14~~ | ~~CJK font `wqy-zenhei.ttc` lost~~ | closed 2026-09-27: the UI is RmlUi with a committed font (`NotoSansCJKtc-TOMS.otf`); no font download any more | — |
| W12 | **Headless tests miss display-scale bugs** | the black-canvas bug (2026-09-26) only showed in a visible window at DPR 2.4 | extend `web_smoke_test.mjs` with a visible-window / `--force-device-scale-factor=2.4` run and a canvas-size check |
| W15 | **Two docs numbered 07** | `07_BUILD_ANDROID.md` and `07_PUBLISH_GITHUB_PAGES.md` | renumber one (links in README / docs point at both) |
| ~~W16~~ | ~~`smoke.stage*` fail~~ | closed 2026-10-12: the reference still showed the on-screen pad, removed on purpose (commit `3df014b`, click-to-go replaced it), and items that used to be invisible; reference refreshed, all 4 backends pass | — |
| W17 | **GPU particles off D3D11** | measured and tested on D3D11 only; the web (GLES 3.0) has no compute and uses the CPU | run `toms_game --fx=docs/examples/fx_recipes.particle#… --fps` on D3D12/Vulkan/OpenGL and an Android phone |
| W18 | **Web build not published yet** | the push to `gh-pages` needs the owner's GitHub sign-in (Git Credential Manager), which a non-interactive session cannot do | run `publish_web.bat nobuild`, sign in once, answer **y**; check https://wslhermesai.github.io/TOMS/ after 1–2 min |
| W19 | **3D title on phones** | built for Android and checked in headless Chrome only; shadows are off there, the model adds 3 MB | run the APK / the web page on a real phone; watch the frame rate on the title |
| ~~W20~~ | ~~**Event editor cannot write files**~~ closed 2026-10-08: *Open project folder* + *Write to project*, with backups, merged with the stage editor's saves ([event_editor/README](../event_editor/README.md)) | *Save…* only shows the changes; the stage editor writes through *Open project folder* | give the event editor the same folder access, sharing files with the stage editor |
| W21 | **Event editor features not in the game yet** | the game ignores triggers / conditions / actions / `next` / `vars.json` / stair locks / `kind`; the "event finished" flag is set only for whispers and rescues | runtime work in [06](../event_editor/06_EVENT_LOGIC.md) §6 and [05](../event_editor/05_FLOOR_LINKS.md) |
| ~~W23~~ | ~~`smoke.battle` depends on `battle.json`~~ | closed 2026-10-12: the test runs with `--battle-tuning=default` (the built-in bar values), so tuning `battle.json` no longer breaks it; reference refreshed | — |
| W24 | **Idle and door animations not seen on web / Android** | checked on desktop D3D11 only; the `.anim` files are in `assets/media`, so they are packaged | play a floor in the web build and on the Android emulator |
| W25 | **Docs in two languages** | every `docs/*.md` (except `progress_report/`) has a copy in `docs/zh_TW/` that is not updated automatically | update the `zh_TW` copy with each English change; check links with the script used on 2026-10-08 |
| W27 | **The player menu on a phone** | P1 shows the menu as the 1024x768 screen scaled down, like every other screen; the list-then-detail phone layout of [20](../20_PLAYER_MENU.md) §2 is not built, and no phone has seen it | open it on a phone (web build); if too small, build the phone layout |
| W28 | **Attribute effects not play-tuned** | the sizes in `stats.json` (STR ATK +0.5, DEX hit zone +0.4, AGI −15 ms, VIT HP +3, INT skills +2 %, LUK gold +3 % a point) are first guesses, checked only with a scripted Lv 10 character | play several floors and adjust `per` in `assets/data/stats.json` ([20](../20_PLAYER_MENU.md) §12) |
| W29 | **Drafted event titles and descriptions** | the 34 pool events got titles and descriptions written from their one line of text (zh_TW + en; other languages show English); no event has `next` / `requires` links yet, so only chapters show connected events | read and rewrite them in the event editor; link events (`next`, `requires`) where the story connects them |
| W30 | **`web.page_coi` fails** | the packaged page (`Build/dist/TOMS-web`, built 2026-10-10) does not become threaded through its service worker in headless Chrome (`isolated=false, workers=0`); the other 4 web tests pass. Not investigated; unrelated to the 2026-10-12 renderer change (it tests a package made before it) | rebuild the package (`build_web.bat`), re-run `ctest -L web`; if it still fails, check the service worker registration on a served copy |
| W31 | **Crowds not seen on the web or a phone** | the shader compiles for WebGL2 and every desktop backend draws it the same, but no web or Android page draws a crowd yet | add a crowd to the web title scene or a test page and check RGBA32F vertex fetch there |
| W26 | **Upgrading gear** (player menu, later) | the owner chose change-only for now (Q15, 2026-10-10); upgrading (+1, +2… for gold or materials) is a later feature | design the steps and costs, add them to `items.json`, add an Upgrade button to the Equipment tab ([20](../20_PLAYER_MENU.md) §4) |
| ~~W22~~ | ~~**Event data gaps**~~ | closed 2026-10-12 (Q10 a): the 92 ids are real events with their own English text (`pool_act04`…`pool_act10`, `pool_common`); 4 orphan events remain (defined, placed nowhere) | — |
| W32 | **The 92 new events' text is a first draft** | written in English from each id's theme (and the Chinese line where one existed); effects still come from the id's words (trap, cache, relic, shard) | read them in the event editor (project folder) and rewrite what does not fit; add `next` / `requires` where the story links them |
| ~~W33~~ | ~~The generator re-rolls every floor's events~~ | closed 2026-10-12: the generator was removed; floors are edited by hand in the stage editor | — |
| W34 | **Locks are not in any floor yet** | the runtime is built and tested on F01 with `--stair-lock=`; no floor spec has a `stairs` list with `unlock`, and the generator does not place extra stair tiles ([05](../event_editor/05_FLOOR_LINKS.md) §5) | choose the floors and events (event editor, floor inspector), place the stair tiles (stage editor), or teach the generator |
| W35 | **Docs in two languages, new ones** | `21_IMAGE_STAGES.md` has its `zh_TW` copy; the event-editor UI's own Chinese strings were kept | as W25 |
| W36 | **Physics only on desktop tools** | `toms_physics` (Jolt) links into `gltf_viewer` and the tests; the game, the web and Android builds do not build it | when the game needs physics: link `toms_physics` into the game, build Jolt for wasm (WASM SIMD) and Android, check size and speed |
| W11 | **Web presets inside the VS IDE** | built only through `build_web.cmd` (which loads emsdk first) | select `web-release-windows` in VS after running `emsdk_env` in a developer prompt, or keep using the script |

---

## Log

| Part | Dates | Contents |
|---|---|---|
| [1_PROGRESS_REPORT.md](1_PROGRESS_REPORT.md) | 2026-09-25 | Qt + bgfx decision; EngineBlueprint update; bgfx glTF / skinning / morph / instancing research; toms_next phase 1 built and verified; WSL web presets and the etcpak stop |
| [3_PROGRESS_REPORT.md](3_PROGRESS_REPORT.md) | 2026-09-27 | the project moved to the repository root; old code/content restored and organised as `assets/` (content) + `src/` (code); desktop, editor and web rebuilt and verified from there |
| [6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md) | 2026-10-05 evening – 2026-10-12 | HTML event editor (tutorial, floor links, event logic, node graph, values map, kinds, saving into assets/data) and stage editor (story check, auto-place, overlap rule, zip download); both in `tools/web_editors/`; docs in Traditional Chinese (`docs/zh_TW/`); the game: monster idle animations, `battle.json`, the key fix, doors opening; the player menu (P1–P3, Save / Load); crowds (instanced skinning) |
| [5_PROGRESS_REPORT.md](5_PROGRESS_REPORT.md) | 2026-10-03 evening, 2026-10-05 | editor coordinate hints; anim editor per-node playback, solo, clip switching; History + auto backups; glTF 2.0 loader, renderer, viewer, shadows, cameras; the 3D title; web build ready to publish |
| [4_PROGRESS_REPORT.md](4_PROGRESS_REPORT.md) | 2026-10-03 | atlas tool; `.anim` runtime + editor + recipes; `.particle` format, runtime, editor; GPU sprite paths and GPU particle simulation; the editors on the game renderer; merged test runner |
| [2_PROGRESS_REPORT.md](2_PROGRESS_REPORT.md) | 2026-09-26 | web build done on Windows (host shaderc, `bimg_encode` excluded, one entry point for desktop + web, IDBFS saves); five bugs fixed; doc 06; `web_smoke_test.mjs` |

Short dated bullets: [WORKING_LOG.md](WORKING_LOG.md). Daily reports: [DAILY_2026-10-12.md](DAILY_2026-10-12.md), [DAILY_2026-10-14.md](DAILY_2026-10-14.md),
[DAILY_2026-10-16.md](DAILY_2026-10-16.md), [DAILY_2026-10-17.md](DAILY_2026-10-17.md).

---

## Open Questions (need the owner)

| # | Question | Default if no answer |
|---|---|---|
| Q1 | ~~Commit the 2026-09-27 move?~~ Answered: committed (`ea386d6` and later) | — |
| Q2 | ~~Keep the folder name `toms_next`?~~ Answered 2026-09-27: no folder, the project is the root | — |
| Q3 | Is browser **WebGPU** definitely not needed? (bgfx only does WebGL2 in the browser) | WebGL2 only, as decided 2026-09-25 |
| Q4 | ~~Game UI library for phase 3?~~ Answered 2026-09-27: RmlUi, every screen, desktop and web ([08](../08_RMLUI.md)) | — |
| Q6 | ~~A generated floor edited by hand~~ Answered 2026-10-12: first a dialog before the generator replaced hand-edited floors; then the owner chose to **edit floors only in the stage editor**, and the generator was removed ([19](../19_STAGE_EDITOR.md) §3) | — |
| Q7 | ~~Locked stairs: invisible or visible?~~ Answered 2026-10-12: **visible, with a lock icon**. When the unlocking event finishes, a **dialog says which floor's stairs are now open**, and the event's description says it too; a **follow-up event "go to F<n>'s stairs"** is started so the player is led there (and finished on reaching them). **Built 2026-10-12** ([05](../event_editor/05_FLOOR_LINKS.md) §3.2, §5); no shipped floor has a lock yet (W34) | — |
| Q8 | Do finished events reset on rebirth (`run` or `meta` scope)? Can a stair wait for several events or a flag? *2026-10-12: the owner will decide later* | reset on rebirth; one event per lock |
| Q9 | ~~The `@` player start~~ Replaced 2026-10-12 by the owner's proposal: **stage maps as a 2D image** instead of characters -- each pixel an integer id (tile, monster, item, event, start, stairs ...), so there are far more ids than characters; a parser reads the pixels into the stage. **Design written 2026-10-12: [21_IMAGE_STAGES.md](../21_IMAGE_STAGES.md)**; three questions there (§6): packed RGB ids or a palette PNG; one id per event; convert all floors at once | keep the character maps until the owner confirms the design |
| Q10 | ~~Event lists, `_v2`.. ids, languages~~ Answered 2026-10-12: a **per-floor event index** (metadata built from the floor files, for quick search of what happens on a floor); `_v2`..`_v5` explained (variant ids the floor generator made so a floor never repeats an id; most have no pool record, W22) -- owner's call (2026-10-12): **(a) each becomes a real event**, built; **languages: English only for now**, every text still through its key and the language setting, translations when the owner asks -- English is the default since 2026-10-12. **The per-floor index is built** (`assets/data/events/floor_index.json`) | — |
| Q11 | ~~Player menu: STR / DEX / AGI?~~ Answered 2026-10-10: extend the status like a common RPG (STR, DEX, AGI, VIT, INT, LUK in a settings file `stats.json`); the gameplay that uses them comes later ([20](../20_PLAYER_MENU.md) §3) | — |
| Q12 | ~~Item prices and ids?~~ Answered 2026-10-10: one all-item file (`items.json`) with each item's id, type, stats, price and text keys; `equipment.json` merges in, the store lists ids only ([20](../20_PLAYER_MENU.md) §5) | — |
| Q13 | ~~Items tab: owned items or every item?~~ Answered 2026-10-10: owned items only | — |
| Q15 | ~~Equipment tab: change or upgrade?~~ Answered 2026-10-10: change only for now; upgrading is a later task (W26); no Talent slot ([20](../20_PLAYER_MENU.md) §4) | — |
| Q14 | ~~Chapter goals?~~ Answered 2026-10-10: every event (and mission, chapter) has a title and a description; the Events tab shows only those, plus the names of connected events ([20](../20_PLAYER_MENU.md) §6, [06](../event_editor/06_EVENT_LOGIC.md) §7) | — |
| Q5 | The bgfx web build now passes its check (2026-09-26). Replace the old `build_web.sh` output (`web/`, `web-gl/`) with it, and where is it published? | keep both until the owner has played the new one (W9) |
