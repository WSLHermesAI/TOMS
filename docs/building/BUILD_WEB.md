# Building the Web (Emscripten) version of Tower of the Sorcerer

The game runs in a browser via **Emscripten** and now uses a single
**WebGPU** backend. The shared game logic is untouched through the
`IRenderer` interface.

| Backend | API | Best for | Files |
|---------|-----|----------|-------|
| **WebGPU** | WebGPU (Dawn C API) + WGSL | Closest match to the Vulkan engine; Chrome/Edge | `src/renderer_webgpu.cpp` |

The browser backend is fully isolated from the Windows/Linux **Vulkan** desktop build.
Audio (miniaudio) ships in the browser build too, via miniaudio's built-in
Web Audio backend — same `Audio.cpp`/interface as desktop, no extra JS glue.

---

## 1. Prerequisites (no root needed)

- **Emscripten SDK** (tested with 6.0.6). Install once:
  ```bash
  cd ~/opt
  git clone https://github.com/emscripten-core/emsdk.git
  cd emsdk
  ./emsdk install latest
  ./emsdk activate latest
  ```
  The build script auto-detects it at `~/opt/emsdk` (or set `$EMSDK`).
- **cmake** on PATH. A no-root copy at `~/opt/cmake/bin` is used automatically
  if present.
- **`assets/` and `data/`** at the repo root — they are packed into the
  single `.data` bundle that ships with the page.

---

## 2. One-click build (recommended)

```bash
./build_web.sh            # build WebGPU
```

The script sources the Emscripten environment, finds cmake, runs `emcmake`
for each backend, and copies the runnable artifacts into:

```
web-gpu/  toms_web.html  .js  .wasm  .data   (WebGPU)
```

---

## 3. Manual build (equivalent to the script)

```bash
source $HOME/opt/emsdk/emsdk_env.sh
export PATH=$HOME/opt/cmake/bin:$PATH        # if using a no-root cmake

# WebGPU
emcmake cmake -S . -B build-web -DWEB=ON
cmake --build build-web -j4
```

The browser build emits `web/toms_web.{html,js,wasm,data}`. Use `./build_web.sh`
to keep the output in `web-gpu/`.

### How the backend is selected

`Game` picks the renderer at **compile time** (`src/game.cpp`):

```cpp
#ifndef __EMSCRIPTEN__
    ren = new Renderer();        // Vulkan (Windows / Linux)
#else
    ren = new WebGPURenderer();  // WebGPU (browser)
#endif
```

The CMake switch is now fixed to WebGPU and defines `WEBGPU` for the source.

---

## 4. Run in the browser

Emscripten needs HTTP (not `file://`):

```bash
python3 -m http.server 8099
# open http://localhost:8099/web-launch.html
```

`web-launch.html` opens the WebGPU build in an iframe. On WSL, forward port 8099 to your Windows browser
(`ssh -N -L 8099:localhost:8099 ...`) or open it in the WSL browser.

- **WebGPU**: best in Chrome/Edge. If a browser lacks WebGPU, the build will fail to start.

---

## 5. Asset bundle & single-file download

- All `assets/` and `data/` files are packed into a **single `.data` file**
  via Emscripten's `--preload-file`, mounted into the in-memory filesystem at
  startup — no per-file fetches.
- A runtime helper `downloadFile(url, dest)` is exported (callable from JS via
  `ccall`) to fetch a **single file from the internet** and write it into the
  filesystem. See `web/README.md` for the snippet.

---

## 6. Desktop (Vulkan) build — unaffected

The browser work does not change the Windows/Linux Vulkan build:

```bash
cmake -S . -B build            # desktop target (Vulkan)
cmake --build build -j4
```
