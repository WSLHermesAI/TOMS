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
inline const float* f_C4(float l_fA, float l_fB, float l_fC, float l_fD) {
    static float l_arrV[4];
    l_arrV[0] = l_fA; l_arrV[1] = l_fB; l_arrV[2] = l_fC; l_arrV[3] = l_fD;
    return l_arrV;
}

// Substitutes one "{key}" placeholder in a locale string with `val` (used by combat-log/toast
// strings with an embedded dynamic value, e.g. damage numbers).
inline std::string f_TrParam(std::string l_strS, const std::string& l_strKey, const std::string& l_strVal) {
    std::string l_strToken = "{" + l_strKey + "}";
    size_t l_nP = l_strS.find(l_strToken);
    if (l_nP != std::string::npos) l_strS.replace(l_nP, l_strToken.size(), l_strVal);
    return l_strS;
}

// Robust JSON file load. NOTE: Emscripten's libc++ std::ifstream is unreliable for preloaded
// files (tellg reports the right size but read/>> return empty), so this uses C stdio, which
// reads preloaded data correctly, then json::parse.
inline nlohmann::json f_ReadJsonFile(const std::string& l_strPath) {
    // through vfs.h so the same call works on desktop, web (preloaded FS) and Android (APK entries).
    // On desktop/web this is still C stdio -- deliberately, not ifstream (libc++ ifstream reads nothing
    // from Emscripten preloaded files), so behaviour here is unchanged.
    std::string l_strBuf;
    if (!toms::f_VfsReadAll(l_strPath, l_strBuf)) {
        fprintf(stderr, "[readJsonFile] cannot open %s\n", l_strPath.c_str());
        return {};
    }
    try { return nlohmann::json::parse(l_strBuf); }
    catch (const std::exception& l_oE) {
        fprintf(stderr, "[readJsonFile] parse error %s: %s\n", l_strPath.c_str(), l_oE.what());
        return {};
    }
}

// ---- sprite id order (atlas grid position; must match the generated manifest) ----
inline const char* const g_arrSPRITEORDER[] = {
    "floor","wall","stairs_up","stairs_down","door_yellow","door_blue","door_red",
    "gem_atk","gem_def","potion_red","potion_blue","coin",
    "key_yellow","key_blue","key_red",
    "player","slime","bat","golem","skeleton","wraith","demon","boss_demonlord",
    "npc_sorcerer","npc_villager","npc_princess","npc_king","npc_handmaiden",
    "exp_up","scroll"          // item icons (added for the inventory UI)
};
inline constexpr int g_iNSPRITES = 30;

// Stage tile char -> sprite id, and entity id -> sprite id.
inline std::string f_CellSprite(char l_cC) {
    switch (l_cC) {
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
inline std::string f_EntSprite(const std::string& l_strId) {
    if (l_strId == "slime") return "slime";
    if (l_strId == "bat") return "bat";
    if (l_strId == "golem") return "golem";
    if (l_strId == "skeleton") return "skeleton";
    if (l_strId == "wraith") return "wraith";
    if (l_strId == "demon") return "demon";
    if (l_strId == "demonlord_vorkath") return "boss_demonlord";
    if (l_strId == "sorcerer") return "npc_sorcerer";
    if (l_strId == "villager") return "npc_villager";
    if (l_strId == "princess") return "npc_princess";
    if (l_strId == "king") return "npc_king";
    if (l_strId == "handmaiden") return "npc_handmaiden";
    // items are named like their sprites (data/items.json); a key loads as the item key_<colour> (stage.h)
    static const char* const l_arrItems[] = {"coin", "gem_atk", "gem_def", "potion_red", "potion_blue", "exp_up", "scroll",
                                         "key_yellow", "key_blue", "key_red"};
    for (const char* l_pszIt : l_arrItems) if (l_strId == l_pszIt) return l_strId;
    return "floor";
}


// Milestone 5: local-date rollover for daily missions (architecture-doc section 8.1's "local device
// midnight" default). Not itself unit-tested (wall-clock dependent) -- the pure logic it feeds,
// toms::rollDailyReset, already is (mission_test.cpp). Was a static in game.cpp.
inline std::string f_TodayDateStringLocal() {
    std::time_t l_i64T = std::time(nullptr);
    std::tm l_oTmv{};
#ifdef _WIN32
    localtime_s(&l_oTmv, &l_i64T);
#else
    localtime_r(&l_i64T, &l_oTmv);
#endif
    char l_pszBuf[16];
    std::snprintf(l_pszBuf, sizeof(l_pszBuf), "%04d-%02d-%02d", l_oTmv.tm_year + 1900, l_oTmv.tm_mon + 1, l_oTmv.tm_mday);
    return std::string(l_pszBuf);
}

} // namespace game_detail
} // namespace toms
