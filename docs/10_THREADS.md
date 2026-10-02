# 10 — Threads: the job system, and threads on the web

The game's frame runs on one thread: input → update → UI → bgfx submit. bgfx renders on that thread too (see
`bgfx_host.cpp`); at about 0.4 ms a frame there is nothing to gain from a render thread in this demo. For heavier
work (3D later: culling, skinning, loading) there is a **job system** that runs the same code on every platform,
threaded wherever threads exist.

## The job system

`src/core/engine/job_system.h`:

```cpp
toms::JobSystem::parallelFor(count, [&](int i) {   // blocks until all count calls have returned
    results[i] = work(inputs[i]);                  // each i runs once, on a worker or the caller
});
```

- `GameSession::start` starts it once: hardware threads − 1 workers (at most 8). The desktop log shows `[jobs] 8
  worker thread(s)`.
- **Zero workers is a normal mode.** The single-threaded web build always has zero, and then `parallelFor` just
  loops on the caller. Code never needs an `#ifdef` for "are there threads".
- **Rules:**
  - The body must be safe to run on several threads at once: write only to your own slot `i`, and don't touch
    shared containers.
  - One `parallelFor` runs at a time; a `parallelFor` inside a body runs inline.
  - bgfx and RmlUi calls stay on the main thread.
- **Used today** by `Game::loadAssets`: the sprite PNGs decode in parallel. `stb_image` keeps its error state per
  thread.
- **Tested** by `unit.job_system_test`: each index runs exactly once, 2,000 back-to-back jobs, nested jobs, zero
  workers, and work really spreading over threads.

## Threads on the web

WebAssembly threads are Web Workers sharing one memory (`SharedArrayBuffer`). Browsers only allow that on a
**cross-origin isolated** page: the server must send

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Without them, a threaded build doesn't start at all: "SharedArrayBuffer transfer requires
self.crossOriginIsolated". So there are two web builds, and the page picks one:

| Build | Preset / command | Output |
|---|---|---|
| single-threaded | `tools\build_web.cmd` (`web-release-windows`) | `Build\web-release-windows\bin` |
| multithreaded | `tools\build_web.cmd mt` (`web-release-mt-windows`: `-pthread`, 5 pre-started workers, the job system uses 4) | `Build\web-release-mt-windows\bin` |

`build_web.bat` builds both and `tools\package.ps1` puts both in `Build\dist\TOMS-web` (`toms_game.<stamp>.*` and
`toms_game_mt.<stamp>.*`). A player downloads only one. `index.html` decides at load time:

```js
threaded = !nothreads && self.crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'
```

| The page is served from … | The player gets |
|---|---|
| a host that sends the two headers (itch.io, Netlify, Cloudflare Pages, own server) | the threaded build, straight away |
| a host that can't, like **GitHub Pages** | `coi-serviceworker.js` (in the package) installs a service worker that adds the headers; the page reloads itself once on the first visit, then gets the threaded build |
| a browser that blocks service workers (some private modes, some in-app browsers) | the single-threaded build: same game, just no workers |
| `index.html?nothreads` | the single-threaded build, on purpose (for comparing or troubleshooting) |

On a first visit to a host without the headers, the page waits up to 4 s for that reload before it starts
downloading the single-threaded build. That way nobody downloads a build they won't use.

In the browser the job system uses only the pre-started workers: the browser's main thread can't wait for a new
worker to be created, because the worker only starts after the main thread returns. For the same reason, don't
block the main thread on anything but `parallelFor`.

## Trying it

```bat
tools\build_web.cmd mt          :: the threaded build
tools\serve_web.cmd mt          :: serves it cross-origin isolated (tools\serve_web.py) and opens it
build_web.bat                   :: both builds + the package
tools\serve_web.cmd dist        :: the package, as a player gets it
```

In the browser console: `crossOriginIsolated` (true = threads allowed), `tomsThreaded` (which build the page chose),
and `Module.ccall('jsWorkerCount','number',[],[])` (4 in the threaded build, 0 otherwise).

`tools\test.cmd` checks all of it (`-L web`; [09](09_TESTS.md)):

| Test | What it checks |
|---|---|
| `web.smoke` | the single-threaded build: 0 workers |
| `web.smoke_mt` | the threaded build with the headers: 4 workers |
| `web.page_coi` | the package on a server **without** the headers (like GitHub Pages): threaded after the service worker's reload |
| `web.page_nothreads` | the package with `?nothreads`: 0 workers |

## Costs and limits

- **Two builds:** `build_web.bat` takes about twice as long. The upload doubles (about 10 MB for both), but the
  download doesn't: each player fetches one.
- The threaded build warns that `-pthread` with a growing memory makes JavaScript's access to wasm memory a little
  slower. It doesn't matter here.
- bgfx stays single-threaded on the web: its WebGL context belongs to the page's main thread. The threads are for
  the job system.
- Not tried on a real phone or on Safari yet. Safari supports cross-origin isolation since 15.2.
