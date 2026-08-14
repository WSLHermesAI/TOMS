# Building the Web (Emscripten) version of Tower of the Sorcerer

The game runs in a browser via **Emscripten**. Two rendering backends are
supported, both implementing the same `IRenderer` interface
(`src/render_iface.h`) so the shared game logic is untouched:

| Backend | API | Best for | Files |
|---------|-----|----------|-------|
| **WebGL2** (default) | WebGL2 / GLSL ES | Universal browser support | `src/renderer_webgl.cpp` |
| **WebGPU** | WebGPU (Dawn C API) + WGSL | Closest match to the Vulkan engine; Chrome/Edge | `src/renderer_webgpu.cpp` |

Both backends are **fully isolated** from each other and from the
Windows/Linux **Vulkan** desktop build. Audio (miniaudio) is excluded from the
browser builds.

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
./build_web.sh            # build BOTH WebGL2 and WebGPU
./build_web.sh webgl      # build only WebGL2  -> web-gl/
./build_web.sh webgpu     # build only WebGPU  -> web-gpu/
```

The script sources the Emscripten environment, finds cmake, runs `emcmake`
for each backend, and copies the runnable artifacts into:

```
web-gl/  tower_vulkan_web.html  .js  .wasm  .data   (WebGL2)
web-gpu/ tower_vulkan_web.html  .js  .wasm  .data   (WebGPU)
```

---

## 3. Manual build (equivalent to the script)

```bash
source $HOME/opt/emsdk/emsdk_env.sh
export PATH=$HOME/opt/cmake/bin:$PATH        # if using a no-root cmake

# WebGL2 (default)
emcmake cmake -S . -B build-web -DWEB=ON
cmake --build build-web -j4

# WebGPU
emcmake cmake -S . -B build-webgpu -DWEB=ON -DWEB_BACKEND=WebGPU
cmake --build build-webgpu -j4
```

Both emit `web/tower_vulkan_web.{html,js,wasm,data}` (the last build wins in
that shared folder). Use `./build_web.sh` to keep WebGL and WebGPU outputs in
separate folders (`web-gl/`, `web-gpu/`).

### How the backend is selected

`Game` picks the renderer at **compile time** (`src/game.cpp`):

```cpp
#ifndef __EMSCRIPTEN__
    ren = new Renderer();        // Vulkan (Windows / Linux)
#else
  #ifdef WEBGPU
    ren = new WebGPURenderer();  // WebGPU (browser)
  #else
    ren = new WebGLRenderer();   // WebGL2 (browser, default)
  #endif
#endif
```

The CMake switch is `-DWEB_BACKEND=WebGL` (default) or `-DWEB_BACKEND=WebGPU`,
which adds `--use-port=emdawnwebgpu` (Emscripten 6.0.6 replaced the old
`-sUSE_WEBGPU=1`) and defines `WEBGPU` for the source.

---

## 4. Run in the browser

Emscripten needs HTTP (not `file://`):

```bash
python3 -m http.server 8099
# open http://localhost:8099/web-launch.html
```

`web-launch.html` has one-click buttons that open the WebGL2 and WebGPU builds
in separate tabs. On WSL, forward port 8099 to your Windows browser
(`ssh -N -L 8099:localhost:8099 ...`) or open it in the WSL browser.

- **WebGL2**: works in all current browsers.
- **WebGPU**: best in Chrome/Edge. If a browser lacks WebGPU, the WebGPU tab
  logs an adapter/device error in the console.

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
