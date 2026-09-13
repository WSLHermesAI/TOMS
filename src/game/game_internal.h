// game_internal.h — the include set the split game_*.cpp units share.
//
// game.cpp used to hold every one of these in a 3,100-line translation unit. Rather than guess a
// per-file include list (and risk subtle "it only compiled because the other file included it"
// coupling), the split units all include this: the renderer backend selection plus the engine/game
// headers they touch. Headers are include-guarded, so the cost is a few stat lines.
#pragma once

#include "game.h"
#include "game_helpers.h"      // C4 / trParam / readJsonFile / cellSprite / entSprite / GP table
#include "game_condition.h"    // toms::game_detail::GameConditionContext

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <json.hpp>

#include "node.h"      // 2D scene-graph Node (parent/child + local/world transform)
#include "scene.h"     // render binding: GameObject / SpriteNode / TextNode / FullScreenSplash
#include "event_bus.h" // toms::EventBus
#include "condition.h" // toms::evaluate / toms::ConditionContext
#include "story_controller.h"
#include "encounter.h"
#include "mission_system.h"
#include "equipment_system.h"
#include "localization.h"
#include "title_screen.h"
#include "save_slots.h"
#include "game_settings.h"
#include "save_system.h"

#ifdef __EMSCRIPTEN__
  #ifdef WEBGPU
    #include "renderer_webgpu.h"   // WebGPU backend (browser build only)
  #else
    #include "renderer_webgl.h"    // WebGL2 backend (browser build only)
  #endif
#else
  #include "renderer.h"            // Vulkan backend (desktop build only)
  #ifndef __EMSCRIPTEN__
    #include "imgui.h"             // core ImGui API only (backend plumbing is imgui_layer.*)
  #endif
#endif
