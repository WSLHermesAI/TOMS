// atlas_sprites_test.cpp — the shipped sprite atlases have every sprite the game can ask for.
//
// The map draws from media/atlas/game.atlas (and media/styles/<id>/atlas/game.atlas for an art
// style), and the UI draws from the same atlas through the RmlUi sprite sheet the game makes from
// it at start-up (Game::uiSpritesheet, "_atlas.rcss"). Positions never go stale -- the sheet comes
// from the atlas itself -- but a NAME can: a sprite renamed or deleted in the atlas editor while
// the code, the data (items, enemies, equipment, store ...) or a .rml file still uses it would
// draw nothing. This fails first. Names come from:
//   - SPRITE_ORDER (the map's sprites),
//   - every "sprite" value anywhere in data/*.json,
//   - every sprite="..." in media/ui/*.rml.
//
// Run: ./atlas_sprites_test   (cwd = assets/, like the other tests)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "atlas_file.h"
#include "game_helpers.h"

static int g_iChecks = 0, g_iFails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_iChecks;                                                               \
        if (!(cond)) {                                                            \
            ++g_iFails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::game_detail;

namespace {

std::string f_ReadFile(const std::string& l_strPath) {
    std::ifstream l_oF(l_strPath, std::ios::binary);
    std::stringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    return l_oSs.str();
}

std::string f_StripPng(std::string l_strS) {
    if (l_strS.size() > 4 && l_strS.compare(l_strS.size() - 4, 4, ".png") == 0) l_strS.resize(l_strS.size() - 4);
    return l_strS;
}

void f_CollectSprites(const nlohmann::json& l_jJ, std::set<std::string>& l_setOut) {
    if (l_jJ.is_object()) {
        for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
            if (l_oIt.key() == "sprite" && l_oIt.value().is_string() && !l_oIt.value().get<std::string>().empty())
                l_setOut.insert(f_StripPng(l_oIt.value().get<std::string>()));
            else f_CollectSprites(l_oIt.value(), l_setOut);
        }
    } else if (l_jJ.is_array()) {
        for (const auto& l_jV : l_jJ) f_CollectSprites(l_jV, l_setOut);
    }
}

// Every sprite name the game can ask any atlas for, with where it came from.
std::map<std::string, std::string> f_UsedSprites() {
    std::map<std::string, std::string> l_mapUsed;
    for (const char* l_pszS : g_arrSPRITEORDER) l_mapUsed[l_pszS] = "SPRITE_ORDER";
    for (const auto& l_oE : std::filesystem::directory_iterator("data")) {
        if (l_oE.path().extension() != ".json") continue;
        std::set<std::string> l_setNames;
        try { f_CollectSprites(nlohmann::json::parse(f_ReadFile(l_oE.path().string())), l_setNames); }
        catch (const std::exception& l_oEx) { CHECK(false, "%s: %s", l_oE.path().string().c_str(), l_oEx.what()); }
        for (const std::string& l_strN : l_setNames) l_mapUsed.emplace(l_strN, l_oE.path().filename().string());
    }
    const std::regex l_oAttr("[^-]sprite=\"([^\"]+)\"");   // sprite="..." but not data-attr-sprite
    for (const auto& l_oE : std::filesystem::directory_iterator("media/ui")) {
        if (l_oE.path().extension() != ".rml") continue;
        const std::string l_strText = f_ReadFile(l_oE.path().string());
        for (std::sregex_iterator l_itIt(l_strText.begin(), l_strText.end(), l_oAttr), l_itEnd; l_itIt != l_itEnd; ++l_itIt)
            l_mapUsed.emplace((*l_itIt)[1].str(), l_oE.path().filename().string());
    }
    return l_mapUsed;
}

void f_CheckAtlas(const std::string& l_strPath, const std::map<std::string, std::string>& l_mapUsed) {
    toms::AtlasFile l_oA;
    std::string l_strErr;
    const std::string l_strText = f_ReadFile(l_strPath);
    CHECK(!l_strText.empty(), "%s is missing", l_strPath.c_str());
    if (l_strText.empty()) return;
    CHECK(toms::f_ParseAtlas(l_strText, l_oA, &l_strErr), "%s: %s", l_strPath.c_str(), l_strErr.c_str());
    CHECK(l_oA.m_vPages.size() == 1, "%s: the game draws from one page, this has %zu", l_strPath.c_str(), l_oA.m_vPages.size());
    const std::string l_strSheet = toms::f_RmlSpritesheets(l_oA, "../atlas/");
    for (const auto& [l_strName, l_strFrom] : l_mapUsed) {
        CHECK(l_oA.f_Find(l_strName) != nullptr, "%s has no sprite '%s' (used by %s)", l_strPath.c_str(), l_strName.c_str(), l_strFrom.c_str());
        CHECK(l_strSheet.find("    " + toms::f_RmlSpriteName(l_strName) + ": ") != std::string::npos,
              "the UI sprite sheet from %s lacks '%s'", l_strPath.c_str(), l_strName.c_str());
    }
}

}  // namespace

int main() {
    const std::map<std::string, std::string> l_mapUsed = f_UsedSprites();
    CHECK(l_mapUsed.size() >= (size_t)g_iNSPRITES, "only %zu sprite names found", l_mapUsed.size());
    f_CheckAtlas("media/atlas/game.atlas", l_mapUsed);
    // Every art style with a packed atlas of its own must have the same names.
    const nlohmann::json l_jStyles = nlohmann::json::parse(f_ReadFile("media/styles/styles.json"), nullptr, false);
    if (l_jStyles.is_object())
        for (const auto& l_jS : l_jStyles.value("styles", nlohmann::json::array())) {
            const std::string l_strPath = "media/styles/" + l_jS.value("id", std::string()) + "/atlas/game.atlas";
            if (std::filesystem::exists(l_strPath)) f_CheckAtlas(l_strPath, l_mapUsed);
        }
    // rmlSpriteName keeps RCSS-safe names as they are and replaces the rest.
    CHECK(toms::f_RmlSpriteName("npc_king") == "npc_king", "plain names stay");
    CHECK(toms::f_RmlSpriteName("ui/btn ok") == "ui-btn-ok", "'/' and spaces become '-'");
    std::printf("atlas_sprites_test: %d check(s), %d failed (%zu sprite names)\n", g_iChecks, g_iFails, l_mapUsed.size());
    return g_iFails ? 1 : 0;
}
