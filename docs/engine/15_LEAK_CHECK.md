# 15 — Memory leak check (Windows)

When `toms_game` exits, it reports what is still allocated. There are two reports:

| Report | Builds | What it lists |
|---|---|---|
| Game objects | every build, every platform | `Object::DumpLeaks()`: objects derived from `Trackable` (game objects, resources) still alive, by type and name, in the log |
| Heap blocks | **Windows Debug builds** (`windows-debug`) | every heap block allocated after `main()` started and not freed at exit, from the MSVC debug heap: its allocation number, size and first bytes, in **`toms_leaks.log`** (in the working folder) and the debugger's Output window |

Release builds and other platforms skip the heap report. The code is in `src/game/src/leak_check.h/.cpp`, called
at the start and end of `main()` in `main_sdl.cpp`.

## Using it

```
tools\build.cmd windows-debug
cd <any folder>
Build\windows-debug\bin\toms_game.exe --scenario=tests/scenarios/arena_restart --scenario-test --fixed-dt=16
```

At exit the console says, for example:

```
[leaks] 99 block(s), 292348 bytes still allocated since start -- see toms_leaks.log
```

`toms_leaks.log`:

```
{28529} normal block at 0x000001CA9E5DD080, 1320 bytes long.
 Data: <        def     > ...
```

`{28529}` is the **allocation number**. Two ways to find where a block was made:

- **`--leak-trace=28529,4558,...`**, no debugger needed. Run the same thing again with this option. The report then
  ends with each listed allocation's **call stack**: function names and `file:line`, read from the `.pdb` next to the
  exe. Up to 32 numbers.
- **`--leak-break=28529`**, in the Visual Studio debugger. The program stops inside that allocation; the Call Stack
  window shows who made it.

The numbers stay the same only when the run is the same: use a scenario with `--scenario-test` and `--fixed-dt=16`,
which runs the same frames and keys every time ([08](../gameplay/08_TEST_SCENARIOS.md)).

## What the report holds (since 2026-10-21)

1. **The new / delete map** (below): every allocation of the program still alive at exit, grouped by where it was
   made, with call stacks. This is the report.
2. **A short debug-heap summary**: how many blocks and bytes are still allocated in the whole process (Qt, statics and
   this checker's own map included), the peak, and the total allocated over the run. It is not a list of leaks.

The debug heap's block-by-block list ("Dumping objects ->", thousands of entries with no names) is **off**: the debug
heap cannot tell a leak from a static, and most of it is Qt and the checker itself. `TOMS_LEAK_HEAP_DUMP=1` (an
environment variable) brings it back; `--leak-trace` / `--leak-break` need the `{N}` numbers it prints.

## The new / delete map (Windows Debug only)

In Windows Debug builds `leak_check.cpp` also **replaces the global `operator new` and `operator delete`** (every form:
arrays, `nothrow`, aligned) for the program it is built into (`toms_game`, `toms_editor`):

- every `new` puts the pointer in a map with its size, its number, and **the call stack that made it**. The call
  stack gives the function and `file:line`; there is no `#define new`, which would break placement new and library
  headers;
- the map entry goes when the memory is freed. The debug heap's free hook sees every free, from every module, so a
  widget our code made with `new` and Qt deleted is taken out too. **Anything deleted properly is never in the
  report**;
- at exit, what is left in the map and was made after `main()` began is the report: **only the unreleased
  allocations, grouped by where they were made, every place, the biggest first**, with the call stack (our code's
  frames first; the C runtime and the STL's allocation plumbing left out).

This covers smart pointers too: `shared_ptr`, `make_shared` control blocks, `unique_ptr`, and every STL container go
through `operator new`. Qt's DLLs keep their own `new`, so Qt's internals are not in the map, only the code of the
program (the game, the engine, RmlUi, bgfx, the editor's own code).

**Only Windows Debug builds.** Everything (the operators, the map, the hook, the report) is inside
`#if defined(_WIN32) && defined(_DEBUG)`. Release builds and other platforms get two empty functions; checked on the
built programs: the report's text is in the Debug `toms_game` and `toms_editor` and in neither Release one.

`TOMS_LEAK_TRACK=0` (an environment variable) turns the map off. The map captures a call stack per `new`; a
scenario run took 6 s with it. The map's own memory shows up in the debug-heap part of the report (the second part).

## Making sure nothing leaks (2026-10-21)

**An empty report means no leak; anything it lists is a leak.** Two things make that true:

1. **Singletons are released before the report.** A singleton that owns memory or threads registers how to release
   it (`toms::onReleaseSingletons(name, fn)`, `src/core/engine/singletons.h`); the end of `main()` calls
   `toms::releaseSingletons()`, newest first, once the game (or the editor) is gone. Today: the job system (its worker
   threads), registered by `JobSystem::start`. RmlUi's in-memory files are freed by `RmlUi::shutdown`; the editor's
   translators and tables by `Language::shutdown()` (they were made with `new` and never freed).
2. **The report is written after the statics are destroyed.** `leakCheckBegin` registers the report with `atexit` at
   the very start of `main()`, before any function-local static is made; the C runtime runs `atexit` handlers newest
   first, so every static made later (tables, registries, empty-string defaults, the event bus) is destroyed before the
   report. `leakCheckEnd` only says where the report goes.

A new singleton that keeps memory in a pointer must register a release, or it shows up as a leak -- which is the
point.

The console line says it plainly: `[leaks] no leaks: 0 allocation(s) never deleted` or `[leaks] LEAKS: ...`. Both
`toms_game` and `toms_editor` reported no leaks after this change. The heap summary's change can be negative (the
statics made before `main()` were freed too) or, in the editor, a few hundred blocks of Qt's own (its DLLs keep their
caches until they unload; the map does not cover Qt's internals).

**A deadlock fixed on the way.** The map's own memory used to come from the C runtime heap. The free hook runs inside
the heap's lock and then takes the map's lock, while a thread adding to the map held the map's lock and needed the
heap's: two threads could wait for each other forever (the game hung at exit). The map now uses a private Windows
heap (`HeapCreate`), so holding its lock never needs the C runtime's.

## toms_editor

`toms_editor` has the same check (the same `leak_check.cpp`, called from `src/editor/src/main.cpp`). Its report is
**`toms_editor_leaks.log`**, and it takes the same `--leak-trace` / `--leak-break` options:

```
Build\windows-debug\bin\toms_editor.exe --scenario=tests/scenarios/cards_first_win --frames=90 --screenshot=shot.png
```

(`--frames` with `--screenshot` closes the editor by itself; without them, close it by hand and the report is written.)

Expect a much longer list than the game's (2026-10-21: 6,674 blocks, 1 MB). Most of it is Qt: its font, style, plugin
and file caches are freed when its DLLs unload, after `main()`. Qt's own frames show as `?` in a trace unless Qt's
`.pdb` files are installed. Traced in that run:
- the game's blocks, the same as in `toms_game` (below);
- the editor's translation table (`Language.cpp`, a static);
- Qt caches made while the Stages and Scenarios docks were built.

Look for **your code** in the call stacks (`src/editor`, `src/core`, `src/game`, `tools/studio_common`), and compare a
short and a long session as below.

## Reading the report

Not every block is a leak:

- **Statics are expected.** Function-local statics (the global event bus, the object registry, the shared texture
  registry, a `static std::vector` that bgfx reads from) are freed only after `main()` returns, so they are listed.
  They do not grow when the game runs longer.
- **A real leak grows.** Run a short and a long version of the same scenario (more fights in the arena, more floor
  changes); blocks whose count or size grows with the run are leaks. Trace those.

## What the map found (2026-10-21)

- **`toms_game`** (`arena_restart`): 99 allocations never deleted, the same blocks as the debug heap below, each now
  with its place. Statics: the sprite index buffer (`bgfx_renderer.cpp:142`), the ease-name table
  (`anim_clip.cpp:67`), the object registry (`object.h:35`), the save folder name (`save_slots.cpp:19`), the job
  system (`job_system.cpp:114`), RmlUi's virtual files (`rml_ui.cpp:443`) and its data-model names.
- **`toms_editor`** (`cards_first_win`): a **real leak, fixed**. `StageScene` (the stage editor tab) made a
  `GameObject` with `new` for every wall and floor cell (`stagescene.cpp:65`, 143 of them). A rebuild freed the
  previous set, but nothing freed the last one: it had no destructor. `~StageScene` now frees them; the editor's count
  went from 436 to 293 allocations, the rest statics: the language table (`Language.cpp:70`), the stage editor's
  object catalog (`catalog.h:31`), and the game's.

## First report (2026-10-21, `arena_restart`)

99 blocks, 292 KB. Traced:

| Block | Size | Made in | Verdict |
|---|---|---|---|
| {4558} | 276 KB | `BgfxRenderer::init`, the `static std::vector<uint16_t> indices` (`bgfx_renderer.cpp:139`) | a static on purpose (bgfx keeps a reference to it): not a leak |
| {19435} and other 16-byte blocks | 16 B | `sharedTextureKey` / `lendTexture` (`shared_textures.h`), the shared texture registry | a function-local static: not a leak |
| {342466} | 280 B | RmlUi data-model type registration (`registerTypes`, `game_ui.cpp:320`) | RmlUi keeps it until its static types are freed; check whether `GameUi` shutdown should remove the data model |
| {1193507} … | 16 B each | RmlUi: `Debugger::Shutdown` → `UnloadDocument` → `GenerateMouseEventParameters` during `GameSession::stop` | made during shutdown and never freed; to check |
| {348307} | 112 B | a string from `Game::missingUiSprites` (`game_ui.cpp:62`) via `GameUi::publishSprites` | kept by something after the game ended; to check |
| {28529} | 1320 B | first read as an animation's node list; the new / delete map shows it is the ease-name table (`easeNames`, `anim_clip.cpp:67`) | a static: not a leak |

The RmlUi and `missingUiSprites` rows are still to check; none of them grows during play in this run.
