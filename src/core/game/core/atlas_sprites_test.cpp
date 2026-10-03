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

static int g_checks = 0, g_fails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_fails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::game_detail;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string stripPng(std::string s) {
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".png") == 0) s.resize(s.size() - 4);
    return s;
}

void collectSprites(const nlohmann::json& j, std::set<std::string>& out) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "sprite" && it.value().is_string() && !it.value().get<std::string>().empty())
                out.insert(stripPng(it.value().get<std::string>()));
            else collectSprites(it.value(), out);
        }
    } else if (j.is_array()) {
        for (const auto& v : j) collectSprites(v, out);
    }
}

// Every sprite name the game can ask any atlas for, with where it came from.
std::map<std::string, std::string> usedSprites() {
    std::map<std::string, std::string> used;
    for (const char* s : SPRITE_ORDER) used[s] = "SPRITE_ORDER";
    for (const auto& e : std::filesystem::directory_iterator("data")) {
        if (e.path().extension() != ".json") continue;
        std::set<std::string> names;
        try { collectSprites(nlohmann::json::parse(readFile(e.path().string())), names); }
        catch (const std::exception& ex) { CHECK(false, "%s: %s", e.path().string().c_str(), ex.what()); }
        for (const std::string& n : names) used.emplace(n, e.path().filename().string());
    }
    const std::regex attr("[^-]sprite=\"([^\"]+)\"");   // sprite="..." but not data-attr-sprite
    for (const auto& e : std::filesystem::directory_iterator("media/ui")) {
        if (e.path().extension() != ".rml") continue;
        const std::string text = readFile(e.path().string());
        for (std::sregex_iterator it(text.begin(), text.end(), attr), end; it != end; ++it)
            used.emplace((*it)[1].str(), e.path().filename().string());
    }
    return used;
}

void checkAtlas(const std::string& path, const std::map<std::string, std::string>& used) {
    toms::AtlasFile a;
    std::string err;
    const std::string text = readFile(path);
    CHECK(!text.empty(), "%s is missing", path.c_str());
    if (text.empty()) return;
    CHECK(toms::parseAtlas(text, a, &err), "%s: %s", path.c_str(), err.c_str());
    CHECK(a.pages.size() == 1, "%s: the game draws from one page, this has %zu", path.c_str(), a.pages.size());
    const std::string sheet = toms::rmlSpritesheets(a, "../atlas/");
    for (const auto& [name, from] : used) {
        CHECK(a.find(name) != nullptr, "%s has no sprite '%s' (used by %s)", path.c_str(), name.c_str(), from.c_str());
        CHECK(sheet.find("    " + toms::rmlSpriteName(name) + ": ") != std::string::npos,
              "the UI sprite sheet from %s lacks '%s'", path.c_str(), name.c_str());
    }
}

}  // namespace

int main() {
    const std::map<std::string, std::string> used = usedSprites();
    CHECK(used.size() >= (size_t)N_SPRITES, "only %zu sprite names found", used.size());
    checkAtlas("media/atlas/game.atlas", used);
    // Every art style with a packed atlas of its own must have the same names.
    const nlohmann::json styles = nlohmann::json::parse(readFile("media/styles/styles.json"), nullptr, false);
    if (styles.is_object())
        for (const auto& s : styles.value("styles", nlohmann::json::array())) {
            const std::string path = "media/styles/" + s.value("id", std::string()) + "/atlas/game.atlas";
            if (std::filesystem::exists(path)) checkAtlas(path, used);
        }
    // rmlSpriteName keeps RCSS-safe names as they are and replaces the rest.
    CHECK(toms::rmlSpriteName("npc_king") == "npc_king", "plain names stay");
    CHECK(toms::rmlSpriteName("ui/btn ok") == "ui-btn-ok", "'/' and spaces become '-'");
    std::printf("atlas_sprites_test: %d check(s), %d failed (%zu sprite names)\n", g_checks, g_fails, used.size());
    return g_fails ? 1 : 0;
}
