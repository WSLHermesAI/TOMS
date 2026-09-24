# 05 — Developer Workflow (Visual Studio) and QA

Part of the [Engine Blueprint](README.md). **Hard requirement: the game, the editor and every test
run and debug from Visual Studio 2022 with F5.** This doc also covers the QA systems Q.1–Q.5.

---

## 1. What TOMS already has

| Piece | State |
|---|---|
| `CMakePresets.json` with `vs2022-x64`, `ninja` and `emscripten-web-debug` | works, but the emsdk path (`D:/Work/emsdk`) and a node version are **hard-coded** |
| `launch.vs.json` → `tools/run_web_debug.ps1` → Chrome with DWARF debugging of the web build | works. **Kept as the pattern** for web F5 |
| `.vs/` Open Folder workspace | in use |
| 30 `*_test.cpp` binaries | built, but **not registered with CTest**, so not visible in Test Explorer; run by hand |
| `.natvis` visualizers | none |

## 2. Project format

- **VS Open Folder + `CMakePresets.json` is the only project format.** No `.sln`/`.vcxproj` is committed.
- Committed presets:

| Configure preset | Generator | Use |
|---|---|---|
| `desktop-debug` / `desktop-dev` / `desktop-shipping` | Visual Studio 17 2022 x64 (or Ninja + MSVC) | Vulkan desktop. `dev` = optimized + editor + asserts |
| `desktop-asan` | same, `ENGINE_ASAN=ON` (`/fsanitize=address`) | leak / memory tests ([08 §8](08_RESOURCE_AND_LIFETIME.md#8-leak-and-memory-tooling-r4-all-automated)) |
| `web-debug` | Ninja + Emscripten toolchain from **`$env{EMSDK}`** | DWARF debug web build (WebGPU, WebGL2 fallback) |
| `web-release` | same | shipping web build |

- **Machine-specific paths** go into a git-ignored `CMakeUserPresets.json`, or come from environment
  variables (`EMSDK`, `VULKAN_SDK`). Nothing machine-specific is committed.
- **Dependencies:** a `vcpkg.json` manifest. VS 2022 integrates vcpkg manifest mode, so the first
  configure installs the libraries ([07](07_THIRD_PARTY_LIBRARIES.md)).

## 3. Tests in Test Explorer

- `enable_testing()` at the root, and `add_test()` for **every** module's test target (engine
  `<module>/tests/`, game `games/magictower/tests/`). VS lists CTest tests in **Test Explorer**,
  where you can run and debug each one (F5 on a test).
- **Test framework:** adopt **doctest** or **Catch2** (see [07](07_THIRD_PARTY_LIBRARIES.md)) instead of
  today's hand-rolled `CHECK` macros. They give per-case results, filtering, and failure locations
  VS can jump to.
- Tests that read real data get `WORKING_DIRECTORY` set in `add_test`, so they run from any location.
- **Every engine module has a headless test target** (no window, no GPU unless the test says so).
  GPU tests use the offscreen path (Q.2) and are labelled `gpu`, so CI without a GPU can skip them.

## 4. F5 targets

`launch.vs.json` lists everything runnable in the startup-target dropdown, each with the correct
working directory (also set with `VS_DEBUGGER_WORKING_DIRECTORY` for the VS generator):

| Target | What F5 does |
|---|---|
| `magictower` (desktop) | MSVC debugger on the Vulkan build. Debug builds enable the **Vulkan validation layers**. An optional RenderDoc capture hotkey is available |
| `magictower --editor` (desktop) | the same exe with the in-game editor open ([10](10_EDITOR_PREFAB_BLUEPRINT.md)) |
| `magictower-web` | builds the `web-debug` preset, starts a local server and opens Chrome/Edge (the existing `run_web_debug.ps1` pattern, generalized) |
| `magictower-web --editor` | the same, with `?editor=1` |
| any test | runs under the debugger (also available from Test Explorer) |

### 4.1 Debugging the web build: the honest limits

- **Visual Studio cannot step C++ inside WASM.** C++ breakpoints for the web build are set in
  **Chrome/Edge DevTools** with the *C/C++ DevTools Support (DWARF)* extension. VS builds and
  launches it; the browser debugs it.
- The rule that makes this rarely matter: **all game and engine logic runs on desktop too**, so
  almost every bug is reproduced and debugged in VS. Web-only issues (browser APIs, WebGPU/WebGL
  differences, IDBFS) are the ones debugged in DevTools.

### 4.2 Debugger quality of life

- **`.natvis` files** are committed under `engine/natvis/` and added to targets with `target_sources`:
  - `Handle<T>` / `Ref<T>`: show the asset id, state and refcount
  - scene nodes and UI widgets: name, rect, children
  - entities: components
  - event runner state
  - UTF-8 strings
  - nlohmann json, using its upstream natvis
- **C++ Hot Reload:** MSVC Hot Reload (`/ZI`, Debug) for small gameplay edits. Live++ (commercial)
  is an option for heavier use. Data (prefabs, UI, flows) hot-reloads through the resource system
  in every build ([08 §7](08_RESOURCE_AND_LIFETIME.md#7-hot-reload)).
- **Mobile:** Android and iOS build from the same CMake. They are **debugged in Android Studio /
  Xcode, not VS**. Keeping logic desktop-runnable (§4.1) applies here too.

## 5. QA systems

| ID | System | Recommendation | TOMS today |
|---|---|---|---|
| Q.1 | Test framework + CTest | doctest or Catch2 + CTest → Test Explorer and CI | hand-rolled `CHECK`, no CTest |
| Q.2 | Headless & golden-image tests | render scenes offscreen through the RHI (no window) and compare PNGs per quality tier with a tolerance. Also the 100× load/unload soak ([08 §8](08_RESOURCE_AND_LIFETIME.md#8-leak-and-memory-tooling-r4-all-automated)) | lost: `savePNG` is a stub in the Vulkan renderer |
| Q.3 | Web end-to-end tests | **Playwright** drives the web build through a small JS test API (today's ~40 `js*` exports in `emscripten_main.cpp`, cleaned up into one `window.engineTest` object) | hooks exist; no runner |
| Q.4 | CI + web preview | GitHub Actions: a Windows MSVC job (build, CTest, ASan soak) and an Emscripten job (build, Playwright, **deploy each branch to a preview URL** so anyone can test without installing) | none |
| Q.5 | Profiling & crash reporting | **Tracy** (CPU and GPU zones, memory) on desktop; WebGPU timestamp queries where available; **sentry-native** for desktop crashes; `window.onerror` + a log upload on web | `log.h` only |

## 6. Everyday loop

1. Open the folder in VS, pick `desktop-dev`, and press F5 on `magictower --editor`.
2. Edit C++ (Hot Reload) or data/prefabs (live reload), and play in the editor.
3. Run the affected tests in Test Explorer.
4. Before pushing, run the `web-debug` F5 once if the change touches platform, rendering or UI.
5. CI builds both targets, runs every test plus the soak and Playwright, and posts the web preview link.
