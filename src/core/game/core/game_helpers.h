// game_helpers.h — helpers that used to be file-static inside the 3,100-line game.cpp.
//
// The 2026-09-13 refactor split that file into cohesive translation units (game_assets,
// game_scene_draw, game_input, game_inventory, game_store, game_combat, game_story,
// game_title_glue, game_ui); these are the small utilities several of them share, so they live
// here as inline definitions instead of being duplicated per file. Each unit does
// `using namespace toms::game_detail;` and writes C4(...), cellSprite(...), trParam(...).
#pragma once

#include "../engine/vfs.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <json.hpp>
#include <ctime>

#include "game.h"

namespace toms {
namespace game_detail {

// One-shot 4-float color literal (the tint arrays drawSprite takes). NOTE: returns a
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
    // through vfs.h so the same call works on desktop, web (preloaded FS) and Android (APK entries).
    // On desktop/web this is still C stdio -- deliberately, not ifstream (libc++ ifstream reads nothing
    // from Emscripten preloaded files), so behaviour here is unchanged.
    std::string buf;
    if (!toms::vfsReadAll(path, buf)) {
        fprintf(stderr, "[readJsonFile] cannot open %s\n", path.c_str());
        return {};
    }
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

} // namespace game_detail
} // namespace toms
