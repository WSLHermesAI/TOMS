// compat/renderer.h -- shadows src/engine/renderer.h (Vulkan + GLFW) for the bgfx build.
//
// The legacy game code does `#include "renderer.h"` and `ren = new Renderer();`. This folder is
// put FIRST on the include path of toms_legacy_game (game/CMakeLists.txt), so those lines pick up
// the bgfx renderer instead and the legacy sources compile without a single edit.
// Remove this shim once the game code is ported to talk to the engine directly (migration phase 3).
#pragma once
#include "bgfx_renderer.h"

using Renderer = toms::next::BgfxRenderer;
