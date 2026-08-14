# Tower of the Sorcerer — Web (Emscripten / WebGL2)

A browser build of the game. The Vulkan renderer is replaced by a WebGL2
backend (`src/renderer_webgl.cpp`); everything else (game logic, stages, CJK
text, combat, dialogue) is shared with the desktop build through the
`IRenderer` interface. The WebGL build is fully isolated from the
Windows/Linux Vulkan build — no Vulkan, no miniaudio.

## Run locally
Serve the folder over HTTP (Emscripten needs `fetch`/workers; opening the
file directly with `file://` will not work):

```bash
cd web
python3 -m http.server 8099
# then open http://localhost:8099/tower_vulkan_web.html
```

On WSL, forward the port to your Windows browser:
```bash
ssh -N -L 8099:localhost:8099 fatming@wslhost   # or open http://localhost:8099 in the WSL browser
```

## Asset bundle
`tower_vulkan_web.data` is a **single packed file** created by Emscripten's
`--preload-file assets --preload-file data`. At startup it is mounted into the
in-memory filesystem, so the game reads `assets/...` and `data/...` exactly as
the desktop build does — no per-file fetches.

## Download a single file from the internet at runtime
The C function `downloadFile(url, dest)` is exported and callable from JS via
`ccall`. It fetches one file and writes it into the filesystem:

```html
<script>
  var Module = require('./tower_vulkan_web.js');
  Module.onRuntimeInitialized = function () {
    // fetch a remote stage JSON and drop it into /data/stages/
    Module.ccall('downloadFile', null,
      ['string', 'string'],
      ['https://example.com/data/stages/stage01.json', '/data/stages/stage01.json']);
  };
</script>
```

## Rebuild (Emscripten)
```bash
source $HOME/opt/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DWEB=ON
cmake --build build-web -j4
# -> web/tower_vulkan_web.html (+ .js/.wasm/.data)
```

Requires the Emscripten SDK (`./emsdk install latest && ./emsdk activate latest`).
The desktop Windows/Linux Vulkan build is unchanged (`cmake -S . -B build`).
