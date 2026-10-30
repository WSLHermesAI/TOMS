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

### Git: never commit the AIGamestyle submodule

- **Don't commit anything in `AIGamestyle`**: no commits inside the submodule, and no staging of its pointer in TOMS
  (`git add AIGamestyle`, `git add -A`, `git commit -a`). Stage TOMS files by name. The owner commits it by hand
  (2026-10-30) until they ask me to.

### Files: by key, never a path in code

- **Game code names a file by its key** in `assets/media/resources.json` (`toms::key::k…` / `toms::pattern::k…` from
  `src/core/game/core/res_keys.h`), never by a path string (the owner, 2026-10-24; docs/engine/12_RESOURCE_MANAGER.md
  §4.3b). A new file: add it to the manifest (a pattern for a family), run `python tools/make_res_keys.py`.
- **Prefer data naming data:** a file a data file can point to (a weapon's clips, its particles) stays in that data,
  with no key and no code.

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
