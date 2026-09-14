# Progress Report — Log Part 4 (2026-09-11 to 2026-09-13)

> Archived log entries split out of `PROGRESS_REPORT.md` (the single file grew too large).
> That file still carries the live **Next Step** banner, the milestone status table, and
> **Open Questions / Blockers** — this file is chronological log entries only, read top to
> bottom, oldest first. See `PROGRESS_REPORT.md`'s Log Index for the full list of parts.

---

### 2026-09-11 to 2026-09-13 — Title phase, a real web pipeline, upstream pulls, and the next-phase design set

Six work streams landed after the last log entry, all verified rather than assumed:

**1. Title phase (now M10).** New Game / Continue / Settings with numbered save slots, a settings
file (language, slot count, font scale), a six-language text table with built-in fallback, and a
confirm dialog before starting a run in an empty slot (owner-reported: an empty slot previously did
nothing). Two real bugs found and fixed while verifying: language selection reverted because the
preference was re-applied from stale state, and `stageDisplayName()` never matched because it
compared the file stem against the JSON id (`stage01` vs `stage_01`). Verified natively (Xvfb, real
key events, screenshots) and in a browser on the live site.

**2. The virtual keypad was not a keypad bug.** Owner-reported dead on-screen controls traced to the
WebGL backend conflating two sizes: it reported a 1280x720 design space while the canvas and the
page tap mapping were 1024x768, and `end()` set `glViewport` to the design size inside a 768-tall
drawing buffer (a GL viewport is bottom-left anchored, so everything drew 48px below the coordinate
the game hit-tested taps against). Fixed by separating design space from drawing buffer
(`emscripten_get_canvas_element_size`, re-checked every frame). Taps then landed correctly, verified
by frame-diffing the map while injecting pointer events at design coordinates.

**3. The published page is now maintainable.** `index.html` used to be an iframe wrapper around
Emscripten's stock shell: logo, spinner/status block, an `#output` textarea that was literally a
visible debug console, and the title "Emscripten-Generated Code". Now `web/clear.html` is
hand-maintained source (clean full-viewport page, errors to the browser console only) and
`build_web.sh` regenerates the deployable page from it on every build, so a clean page always
survives a rebuild. Added afterwards, on the owner's report that a static loading screen reads as a
crash: real byte-level progress from the loader's own status reports ("downloading data x / 17 MB"),
held below 100% until `onRuntimeInitialized` actually fires, with a CSS shimmer and a breathing
brand so it never looks frozen; plus a pulsing, sliding selection highlight on the title screen
(measured: consecutive no-input frames differ by about 7k samples on web and 5.8k on native).

**4. Deploy safety.** A deploy used to replace `toms_web.data` / `.wasm` under the same names, so a
returning visitor could run fresh page JS against a cached old `.data` -- reproduced here as a black
canvas that only a cold browser profile fixed. Artifact filenames are now version-stamped per build
with the two fetch URLs rewritten inside the JS, and the deploy keeps the previously referenced set.
Also pruned about 72 MB of stale artifact sets from `gh-pages`, and stopped the deploy script from
copying every past build's stamped bundle out of the build directory (94.6 MB of local cruft).

**5. Upstream pulls (owner-requested; each followed by a build, a live deploy and verification).**
- 2026-09-11: multi-language, web audio, TTF fonts -- `toms_web.data` dropped from 17,311,851 B to
  208,318 B (the bitmap font atlas was replaced by subsetted TTFs), so the page now loads almost
  instantly and the loading bar jumps straight to 100%.
- 2026-09-12: camera, battle system v2, more multi-language content. Battle v2 replaces the
  press-and-hold Power Bar with auto-moving bars and tap-to-freeze, three independent buttons
  (attack / defend / super) and multi-touch concurrency; verified live in a browser (a two-finger
  tap cools both bars, the enemy clock advances on its own). One content bug the deployed page
  exposed: the new Attack/Defend prompt strings were longer than their 230px column and printed over
  each other -- trimmed across all six languages.
- Each pull was checked for surviving local work: the earlier battle-button and input-routing fixes
  were superseded by upstream's design, while the clean page, version-stamped artifacts, loading
  screen, title animation and the harness diagnostics (`jsCombatInfo`, `jsPlayerInfo`,
  `jsDebugBattle`) survived and are still in use.

**6. Next-phase design set (documents only, not implemented).** `STORY_BIBLE.md` v3 turns the story
into a xianxia reincarnation main line -- a fallen Grand Emperor reborn as a powerless apprentice,
each floor releasing one seal of memory and cultivation -- over **70 floors = 10 acts x 7**, with
eight cross-act choices feeding three counters (insight / resolve / humanity) and **15 endings** (3
good, 2 neutral, 10 bad, 1 hidden) resolved by a priority condition tree from choices made on
different floors *and* key items never obtained; rebirth carries half the power and resets every
item. `SIDE_STORIES.md` v3 places the ten flagship side stories one per act (5th floor), two
choice-gated and several mirroring main-line choices, plus per-floor minor event pools so 70 floors
have daily content. `STORY_DATA_SCHEMA.md` v3 is the data contract (per-floor files, story graph,
four new condition types, maze scaling table to 63x42, `endings.json`, `cycles.json`, save v3,
16 validators). `ART_AND_ABILITY_DESIGN.md` specifies **50 enemies** (40 regular plus 10 unique
bosses, each with a full ComfyUI prompt) and **20 equipment** with gameplay values, a **36-status**
system, and the resulting asset list -- **1,006 animation frames and 469 static images** -- built on
nine shared body rigs plus swappable parts plus shader colour variants (198 enemy frames instead of
1,100), with **1/2/4-grid footprints** as a wordless visual language (4 grids means boss or roamer).

