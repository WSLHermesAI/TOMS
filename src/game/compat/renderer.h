// compat/renderer.h -- the renderer the core game code creates.
//
// The core game code (src/core) does `#include "renderer.h"` and `ren = new Renderer();` (desktop
// and web). This header makes `Renderer` the bgfx renderer. (It used to shadow the old Vulkan
// renderer.h, removed 2026-09-27.) Remove this shim once the game code uses the engine directly
// (migration phase 3).
#pragma once
#include "bgfx_renderer.h"

using Renderer = toms::next::BgfxRenderer;
