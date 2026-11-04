# 21. The ocean: a real-time sea and sky, and its viewer

The first of the three 3D backgrounds in [17 §12](../gameplay/17_WEAPONS_AND_PLAYS.md). It has its own app, `ocean_viewer`, so the
look can be tried and tuned.

**In the game: the title screen only.** Each start takes the sea and the city model in turns, the sea first (`--title-scene=ocean`
forces the sea; [08 §3](08_GLTF.md)). It uses the default look, darker and with fewer clouds so the menu stays readable, at "high" on the
desktop and "low" on the web. Android always draws the city for now. The battle does not use it yet (Q69).

## 1. Running it

Build the `ocean_viewer` target, then run `Build\windows-release\bin\ocean_viewer.exe`. The panel has every setting on sliders, and
four presets: day, sunset, storm, calm.

| Control | Does |
|---|---|
| Left drag | look around |
| Wheel | camera up and down |
| H | hide the panel |
| Space | pause |
| Esc | quit |

**Save settings** writes `tools/ocean_viewer/ocean_settings.json`, which the viewer reads when it starts.

**In the browser:** run `tools\build_ocean_web.cmd`, then `tools\serve_web.cmd ocean`. The same options go in the URL:
`ocean_viewer.html?preset=storm&quality=low`. The panel starts folded on the web. "Save settings" keeps them in that browser.

Pictures without a window (it stays hidden): `--preset=storm --no-ui --frames=120 --fixed-dt=33 --record=<folder>`, then
`python tools/frames_to_gif.py <folder> out.gif`. Other options are listed at the top of `tools/ocean_viewer/main.cpp`.

## 2. How it works

| Part | What | Where |
|---|---|---|
| Waves | Tessendorf's FFT ocean with a JONSWAP spectrum and a cos^2s spread, on the CPU through the job system. Three square patches (700, 110, 19 m), each with its own band of wave lengths, so the repeat does not show | `src/core/engine/ocean_sim.*` |
| Foam | where the surface squeezes (its Jacobian falls low), then fading with time | `ocean_sim.cpp`, `fs_ocean_water.sc` |
| Drawing | everything below, and the quality table | `src/engine/src/ocean_renderer.*`, `src/engine/shaders/*ocean*` |
| Water mesh | a grid over the screen: each vertex goes along its ray to the flat sea, then the waves move it. So it reaches the horizon, with its detail spread evenly on screen | `vs_ocean_water.sc` |
| Water colour | Fresnel between the sky's reflection and the light inside the water, a glow through thin crests, the sun's glitter (GGX), haze toward the horizon | `fs_ocean_water.sc` |
| Sky | Preetham's daylight model, a warm tint for a low sun, a flat layer of noise clouds, the sun's disc. No picture needed | `ocean_common.sh`, `fs_ocean_sky.sc` |

The waves' pictures are RGBA16F with their mip levels, uploaded each frame.

## 3. Quality, for the web and phones

`--quality=` or the panel picks one (the table `g_arrQualities` in `ocean_renderer.h`):

| Quality | Patches | Grid rows | Cloud noise: sky / in the water | Waves simulated |
|---|---|---|---|---|
| high (desktop) | 3 of 256 | 360 | 5 / 3 | every frame |
| medium (web, PC) | 3 of 128 | 220 | 4 / 2 | every frame |
| low (web, phone) | 2 of 128 | 140 | 3 / none | every 2nd frame |
| lowest | 2 of 64 | 90 | 2 / none | every 2nd frame |

- **Auto (the web's default):** it starts at medium, or low on a phone. If frames average over 24 ms for 2 seconds, it steps down one
  level. It never steps back up by itself, so the picture does not keep changing.
- **A phone draws one pixel per CSS pixel,** not its 2-3 device pixels. That is the largest saving, at most a ninth of the pixels.
- **The web build has one thread and no MSAA,** and encodes gamma in the shader, since WebGL has no sRGB screen.

## 4. Against the repository the owner pointed to

[kentril0/WaterSurfaceRendering](https://github.com/kentril0/WaterSurfaceRendering) (MIT) was the reference for the method. Its code
was not copied, since it is Vulkan-only and this project draws through bgfx.

| | That project | Here |
|---|---|---|
| Graphics | Vulkan | bgfx: Direct3D 11 and 12, Vulkan, OpenGL |
| FFT | FFTW, which is GPL | a small FFT of our own, so no license question |
| Spectrum | Tessendorf's choppy waves | the same, with JONSWAP |
| Patches | one | three |
| Foam | listed as future work | yes |
| Sky | Preetham | Preetham, with clouds and a low-sun tint |
| Underwater terrain | yes (value noise) | no: a deep sea only |

## 5. Numbers

On the owner's PC (8 worker threads), 1920 x 1080, 3 patches of 256 x 256, 4x MSAA, measured over 400 frames:

| | ms a frame |
|---|---|
| Waves | 6.0 |
| Mip pictures and half floats | 1.4 |
| The whole frame | 9.4 |

The "whole frame" time is the run's wall clock with no vsync, startup included.

**The web build** (1.9 MB of wasm, 216 KB of JS, no data files) in headless Chrome on the same PC's GPU, one thread, capped at 60 fps:

| Run | Quality | fps | Waves ms |
|---|---|---|---|
| PC, 1254 x 620 | high | 43 | 16.7 |
| PC | medium (auto) | 60 | 4.2 |
| PC | low | 60 | 2.9 |
| PC | lowest | 60 | 0.6 |
| PC, CPU 4x slower | medium | 35 | 18.0 |
| Imitated phone, 412 x 915 | low (auto) | 60 | 2.5 |
| Imitated phone, CPU 4x slower | low (auto) | 60 | 10.8 |
| Imitated phone, CPU 6x slower | low (auto) | 53 | 21.6 |

**The imitated phone has a phone's screen and user agent, and a slowed CPU, but this PC's GPU.** On Chrome's software GPU, a very slow
worst case, auto stepped down from medium to lowest (22 fps at 1254 x 620). **It has not been run on a real phone.**

## 6. Tests

- `unit.ocean_sim_test`: the FFT, the height's variance against the spectrum's integral (within 2 % when written), slopes against
  heights, repeatable runs, foam.
- `smoke.ocean_viewer`: it starts, its shaders load and it draws. It has no reference picture, since the look is still being chosen.
- `smoke.title_ocean`: the game's title with `--title-scene=ocean`; it fails if the sea did not start. No reference picture either.
- `web.ocean_viewer` (only with the web tests): the page in headless Chrome on the real GPU: it draws and no shader fails.
  `tools/ocean_viewer/web/ocean_web_test.mjs` also takes `--phone`, `--cpu=<slower by>`, `--software`, and prints the numbers above.

Checked by hand: the pictures of all four presets on Direct3D 11; day on Direct3D 12, Vulkan and OpenGL.
It is not in the Android app: phones use the browser page.

## 7. Not done

- Into the battle: the host would draw it in the scene views the title uses, and the battle's black backdrop would go. The open
  question is [Q69](../project/13_OPEN_DECISIONS.md).
- "Save settings" is only the viewer's: the title does not read `ocean_settings.json`.
- Spray, and objects floating on the water.
- The other two backgrounds (a sky, a volcano). The sky here can be the start of the sky background.
