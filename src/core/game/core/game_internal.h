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

#include "renderer.h"   // Renderer = the bgfx renderer (src/game/compat/renderer.h), desktop and web

// M2: ImGui is wired into every backend, so the dev windows in game_scene_draw.cpp (F1 debug
// overlay, F2 styling spike, font scale) build on web too. Only the *core* API is included here --
// the backend (src/engine/src/imgui_bgfx.*) is set up by the host, src/game/src/game_session.cpp.
#include "imgui.h"
