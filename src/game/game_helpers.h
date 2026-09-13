// game_helpers.h — helpers that used to be file-static inside the 3,100-line game.cpp.
//
// The 2026-09-13 refactor split that file into cohesive translation units (game_assets,
// game_text_draw, game_scene_draw, game_input, game_inventory, game_store, game_combat,
// game_story, game_title_glue); these are the small utilities several of them share, so they live
// here as inline definitions instead of being duplicated per file. Call sites are unchanged --
// each unit does `using namespace toms::game_detail;` and keeps writing C4(...), cellSprite(...),
// trParam(...), GP[i] exactly as before.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <json.hpp>
#include <ctime>

#include "game.h"   // g_textGame holds a Game* (helpers are game-internal, so this is not a layering break)

namespace toms {
namespace game_detail {

// One-shot 4-float color literal (the tint arrays drawText/drawSprite take). NOTE: returns a
// pointer to a function-local static buffer -- same lifetime/aliasing semantics this always had
// when it was file-static; it is only ever consumed immediately at the call site.
inline const float* C4(float a, float b, float c, float d) {
    static float v[4];
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
    return v;
}

// Substitutes one "{key}" placeholder in a locale string with `val` (used by combat-log/toast
// strings with an embedded dynamic value, e.g. damage numbers).
inline std::string trParam(std::string s, const std::string& key, const std::string& val) {
    std::string token = "{" + key + "}";
    size_t p = s.find(token);
    if (p != std::string::npos) s.replace(p, token.size(), val);
    return s;
}

// Robust JSON file load. NOTE: Emscripten's libc++ std::ifstream is unreliable for preloaded
// files (tellg reports the right size but read/>> return empty), so this uses C stdio, which
// reads preloaded data correctly, then json::parse.
inline nlohmann::json readJsonFile(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) { fprintf(stderr, "[readJsonFile] cannot open %s\n", path.c_str()); return {}; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); fprintf(stderr, "[readJsonFile] empty %s\n", path.c_str()); return {}; }
    std::string buf((size_t)sz, '\0');
    size_t rd = fread(&buf[0], 1, (size_t)sz, fp);
    fclose(fp);
    if (rd == 0) { fprintf(stderr, "[readJsonFile] read 0 bytes %s\n", path.c_str()); return {}; }
    try { return nlohmann::json::parse(buf); }
    catch (const std::exception& e) {
        fprintf(stderr, "[readJsonFile] parse error %s: %s\n", path.c_str(), e.what());
        return {};
    }
}

// ---- sprite id order (atlas grid position; must match the generated manifest) ----
inline const char* const SPRITE_ORDER[] = {
    "floor","wall","stairs_up","stairs_down","door_yellow","door_blue","door_red",
    "gem_atk","gem_def","potion_red","potion_blue","coin",
    "key_yellow","key_blue","key_red",
    "player","slime","bat","golem","skeleton","wraith","demon","boss_demonlord",
    "npc_sorcerer","npc_villager","npc_princess","npc_king","npc_handmaiden",
    "exp_up","scroll"          // item icons (added for the inventory UI)
};
inline constexpr int N_SPRITES = 30;

// Stage tile char -> sprite id, and entity id -> sprite id.
inline std::string cellSprite(char c) {
    switch (c) {
        case '#': return "wall";
        case '.': return "floor";
        case 'U': return "stairs_up";
        case 'D': return "stairs_down";
        case 'y': return "door_yellow";
        case 'b': return "door_blue";
        case 'r': return "door_red";
        default:  return "floor";
    }
}
inline std::string entSprite(const std::string& id) {
    if (id == "slime") return "slime";
    if (id == "bat") return "bat";
    if (id == "golem") return "golem";
    if (id == "skeleton") return "skeleton";
    if (id == "wraith") return "wraith";
    if (id == "demon") return "demon";
    if (id == "demonlord_vorkath") return "boss_demonlord";
    if (id == "sorcerer") return "npc_sorcerer";
    if (id == "villager") return "npc_villager";
    if (id == "princess") return "npc_princess";
    if (id == "king") return "npc_king";
    if (id == "handmaiden") return "npc_handmaiden";
    return "floor";
}

// ---- on-canvas virtual gamepad (web/touch build) ----
// Single source of truth: the same rects are used for drawing (drawGamepad) and hit-testing
// (handleTouch). Buffer space is 1024x768, y-down.
struct GPadBtn { int id; float x, y, w, h; const char* label; float col[4]; int dx, dy; };
inline const GPadBtn GP[] = {
    {0, 104,510, 72,72, "^", 0.40f,0.45f,0.55f,0.70f, 0,-1}, // up
    {1, 104,626, 72,72, "v", 0.40f,0.45f,0.55f,0.70f, 0, 1}, // down
    {2,  20,568, 72,72, "<", 0.40f,0.45f,0.55f,0.70f,-1, 0}, // left
    {3, 188,568, 72,72, ">", 0.40f,0.45f,0.55f,0.70f, 1, 0}, // right
    {4, 880,626, 68,68, "A", 0.30f,0.70f,0.35f,0.80f, 0, 0}, // interact / use
    {5, 792,566, 64,64, "B", 0.75f,0.30f,0.30f,0.80f, 0, 0}, // drop
    {6, 880,526, 56,56, "I", 0.30f,0.35f,0.75f,0.80f, 0, 0}, // inventory
    {7,  20,490, 48,48, "P", 0.25f,0.25f,0.25f,0.85f, 0, 0}, // toggle: show/hide gamepad
};


// A (mobile): GP[]'s rects were authored against a 768-tall design. On a smaller design (the browser
// uses 768x576 on phones) the pad would sit below the visible area, so every consumer goes through
// this helper -- drawing and hit-testing must shift by the SAME amount, or a button would look right
// and do nothing. g_padShiftY is set by the platform entry point (0 on desktop).
inline int kPadShiftY = 0;      // 0 == the authored layout (see gpadBtn)
// C-lite (mobile): the pad's plates scale about their own centre. Drawing and hit-testing both go
// through gpadBtn(), so enlarging a plate enlarges its tap target too -- which is the whole point for
// a phone ("UI is hard to see and hard to click buttons"): at 1024x768 on a 390 px-tall landscape
// phone the authored 72 px button renders at ~36 css px, below the 44 px guidance.
inline float kPadScale = 1.0f;
inline GPadBtn gpadBtn(int i) {
    GPadBtn b = GP[i];
    const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
    float ncx = cx, ncy = cy;
    if (i >= 0 && i <= 3 && kPadScale != 1.0f) {
        // The four direction plates are packed tightly in the authored layout, so scaling each about
        // its own centre alone makes them overlap into a blob (the 1.40 attempt). Spreading them about
        // the cluster's centre by the SAME factor keeps every gap proportional, so bigger plates stay
        // visually distinct. The isolated action buttons (A/B/P) are already far apart and only need
        // their own centre.
        const float CCX = 140.0f, CCY = 604.0f;      // the authored d-pad centre
        ncx = CCX + (cx - CCX) * kPadScale;
        ncy = CCY + (cy - CCY) * kPadScale;
    }
    b.w *= kPadScale;
    b.h *= kPadScale;
    b.x = ncx - b.w * 0.5f;
    b.y = ncy - b.h * 0.5f - kPadShiftY;
    return b;
}
inline constexpr int GP_N = 8;


// Milestone 5: local-date rollover for daily missions (architecture-doc section 8.1's "local device
// midnight" default). Not itself unit-tested (wall-clock dependent) -- the pure logic it feeds,
// toms::rollDailyReset, already is (mission_test.cpp). Was a static in game.cpp.
inline std::string todayDateStringLocal() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

// TextNode draws through Game's font; Game::loadAssets() binds this once per session, and the
// TextNode::Draw trampoline in game.cpp reads it. Was a file-static `Game* g_textGame`.
inline Game* g_textGame = nullptr;

} // namespace game_detail
} // namespace toms
