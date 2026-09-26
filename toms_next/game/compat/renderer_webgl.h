// compat/renderer_webgl.h -- shadows src/engine/renderer_webgl.h for the bgfx web build.
//
// Under Emscripten the legacy game code includes "renderer_webgl.h" and does
// `ren = new WebGLRenderer();` (game_assets.cpp). Like compat/renderer.h on desktop, this makes
// that line build the bgfx renderer, which runs on WebGL2 in the browser.
#pragma once
#include "bgfx_renderer.h"

using WebGLRenderer = toms::next::BgfxRenderer;
