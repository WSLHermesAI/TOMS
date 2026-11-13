# TOMS — project rules

## MUST FOLLOW

### Testing: only what changed, unless asked for everything

- **After a change, run only the tests the change can break:** `tools\test.cmd` (it reads `git diff HEAD` plus
  untracked files and picks the tests through `tools/test_select.py`). Use `tools\test.cmd --list` to see which
  tests and why.
- **Run every test only when asked to:** `tools\test.cmd all` (desktop and web). Do it when the owner says "test all",
  and before a release or a publish, or when they ask for it before a commit.
- Don't run the full suite or the individual test commands by habit (`-L smoke`, each screenshot test, every
  backend). If one specific test needs a rerun (for example a reference image refreshed with `TOMS_UPDATE_GOLDEN=1`),
  run just that test.
- **An area that did not change is not tested.** Game changes never run glTF tests; glTF changes never run game
  tests; the same goes for particles, animations, atlases, the renderer and web (the areas are listed in
  `tools/test_select.py` and docs/project/08_TESTS.md). Don't add tests from an unchanged area to a run.
- **When the run reports a file as "NOT MAPPED"** (no test ran for it), add a rule for it in `tools/test_select.py`
  (RULES), so the next change to that file selects its area's tests.
- **When a new test is added,** make sure a rule in `tools/test_select.py` selects it for the files it covers.
- **Report what was run:** say "changed-only: N of M tests" or "all tests", with the result, never just "tests pass".

### Platforms: Windows first

- **New code or new data is built and tested on the Windows desktop version only** (the owner, 2026-10-30). Web,
  Android and other versions are tested and updated only when the owner asks for that version. `tools\test.cmd` picks
  desktop tests only, so don't add web tests to a run. The exception is "test all", which the owner asks for directly.

### C++ programs: Visual Studio F5, and a script that runs with no missing-DLL popup

- **Every C++ program (TOMS and AIGamestyle) builds, runs and debugs in Visual Studio with F5** (the owner,
  2026-11-09; docs/project/02_VISUAL_STUDIO.md §7). In TOMS it is a target of the root CMake project, with a
  `launch.vs.json` item if it needs arguments. A stand-alone app has a `make_vs_solution.bat` that sets it as the
  startup project.
- **A `.bat`/`.cmd` builds it, and the built exe starts from a double-click with no "missing DLL" popup.** Use
  windeployqt for Qt (plus the offscreen plugin for self-tests), and static linking or a post-build copy for other
  DLLs. Never rely on PATH.
- **After adding a program or a DLL dependency, run `python tools/check_exe_dlls.py`.** Exit 0 means nothing is
  missing.

### Sound: nothing is heard from a run in the background

- **Anything running offscreen or in the background has every sound and BGM at volume 0** (the owner, 2026-11-05): a
  hidden window (`--screenshot`, `--record`), a minimized or covered one, a window without focus, an app in the
  background on a phone or a hidden browser tab, a scripted run (tests, `--frames`, `--fixed-dt`, `--scenario-test`,
  `--bench`), and an editor's self-test or an editor that is not the active app.
- `Audio::f_SetBackgroundMuted` does it, called every frame by each host (`src/game/src/main_sdl.cpp`,
  `src/editor/src/bgfx_viewport.cpp`). **A new host of the game calls it too, and new sound or BGM plays through the
  `Audio` engine** so the mute covers it.

### Battle effects belong to their monster

- **Every particle effect, animation and sound a monster starts is bound to that monster** (the owner, 2026-11-13): its
  emitters and sounds live in its own record (`CombatState::Foe::m_vFx`, `m_vSounds`), its animation in its rig
  (`FoeRig`, which plays "die").
- **When the monster dies, each one is checked, not left running:** `battle.json` `"onFoeDeath"` (`fxRules`,
  `soundRules`, by effect name or sound key, with a `default`) says `stop` (destroyed at once), `fade` (no more
  particles, those already out finish; the sound fades out) or `keep` (plays to its end, no longer the monster's).
  `Game::f_FoeEffectsOnDeath` does it.
- **A new monster effect or sound goes through `f_PlayBattleFx(name, foe)` or `f_PlayFoeSound(foe, key)`**, never the
  battle's shared list or a bare `Audio::f_Play`. When the default rule is wrong for it, give it its own line in
  `"onFoeDeath"`.

### Git: no commit and no new branch until the owner asks

- **Don't commit and don't create a branch until the owner asks for that** (the owner, 2026-11-05). Finished work stays
  uncommitted in the working files. Don't offer to commit as a next step, and don't treat earlier approval as lasting:
  each commit or branch needs its own request.

### Git: never commit the AIGamestyle submodule

- **Don't commit anything in `AIGamestyle`**: no commits inside the submodule, and no staging of its pointer in TOMS
  (`git add AIGamestyle`, `git add -A`, `git commit -a`). Stage TOMS files by name. The owner commits it by hand
  (2026-10-30) until they ask me to.

### C++ naming: scope and type in every variable, `f_` on every function

- **Every C++ variable is `<scope>_<type><Name>`** (the owner, 2026-11-02; docs/project/12_CPP_NAMING.md): `m_` member,
  `l_` local (parameters, lambdas, loops), `g_` global; then the type prefix (`str`, `i`, `b`, `f`, `v`, `map`, `p`,
  `sp`, `up`, `o`, ... the table in §2), e.g. `m_strName`, `l_iCount`, `g_bQuit`.
- **Every function is `f_<Name>`** (free, member, static): `f_LoadBattleRig`. Not constructors, destructors,
  operators, entry points, third-party virtual overrides, or names found by lookup (`to_json`, `begin`, `swap`; §3).
- **All the project's C++ follows it** (converted 2026-11-02 with `tools/naming/rename_cpp.py`); write new code the
  same way, and `python tools/naming/rename_cpp.py plan` reports names that do not. Never touch third-party code
  (`src/third_party/`, the libraries CMake downloads, `AIGamestyle`).

### Files: by key, never a path in code

- **Game code names a file by its key** in `assets/media/resources.json` (`toms::key::k…` / `toms::pattern::k…` from
  `src/core/game/core/res_keys.h`), never by a path string (the owner, 2026-10-24; docs/engine/12_RESOURCE_MANAGER.md
  §4.3b). A new file: add it to the manifest (a pattern for a family), run `python tools/make_res_keys.py`.
- **Prefer data naming data:** a file a data file can point to (a weapon's clips, its particles) stays in that data,
  with no key and no code.

### Open decisions: one list, with the owner's answers in it

- **Every question that stays undecided goes in `docs/project/13_OPEN_DECISIONS.md`** (EN + zh_TW), as a row with an empty
  **Answer** column (the owner, 2026-11-04). That includes a question a design document asks. A question needed
  only to finish the current task can stay in the chat.
- **When the owner asks to check the decisions:** read Answer in both copies, and My Answer in any design document whose questions have that column (doc 14 §5). If an answer is unclear, incomplete or
  conflicts with another, ask about it. If all the filled answers are clear, ask whether to do the work, and start only
  after a yes.
- **An empty Answer is not a decision:** don't act on the row or apply its proposal. Once work is decided, write the
  answer in the document the row links to and remove the row.
- **A decision for the owner is a table with at least these 4 columns, in this order** (the owner, 2026-11-06), in the chat
  and in new decision lists: **Question | Default | Yours | Answer**. Default: what happens or stays if nobody decides.
  Yours: Claude's recommendation. Answer: left empty for the owner. More columns may follow them (`13_OPEN_DECISIONS.md` uses these).

### Documents: local files only, nothing online

- **Never upload project data or documents to an online service for viewing or sharing** (Claude Docs, published
  Artifacts, pastebins, gists, or anything else that gives a URL). The owner, 2026-10-14: "do not upload any data
  online for open preview".
- **A document goes into the repo as a local Markdown file, in its area's folder** (the owner, 2026-10-24):
  - `docs/gameplay/`: the game and its content: battle, stages, events, UI (RmlUi), saves, the stage/event and UI
    editors;
  - `docs/engine/`: the core engine and its tools: rendering, resources, animation, atlas, particles, fonts, glTF;
  - `docs/project/`: setup, building, tests, the team, progress reports.
- English in `docs/<area>/NN_NAME.md` (NN: the next number in that folder), Traditional Chinese in
  `docs/<area>/zh_TW/NN_NAME.md` (starting with `> 英文原文：[NN_NAME.md](../NN_NAME.md)`). Add it to
  [docs/README.md](docs/README.md).
- Publish online only when the owner explicitly asks for that specific document.
