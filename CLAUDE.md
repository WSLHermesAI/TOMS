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
  `tools/test_select.py` and docs/09_TESTS.md). Don't add tests from an unchanged area to a run.
- **When the run reports a file as "NOT MAPPED"** (no test ran for it), add a rule for it in `tools/test_select.py`
  (RULES), so the next change to that file selects its area's tests.
- **When a new test is added,** make sure a rule in `tools/test_select.py` selects it for the files it covers.
- **Report what was run:** say "changed-only: N of M tests" or "all tests", with the result, never just "tests pass".
