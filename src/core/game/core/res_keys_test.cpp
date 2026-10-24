// res_keys_test.cpp -- res_keys.h and assets/media/resources.json say the same, and every key and pattern names a file
// that is there (docs/engine/12_RESOURCE_MANAGER.md). Runs in assets/ (tests/CMakeLists.txt). Exits 0 on success.
#include "res_keys.h"
#include "resource.h"
#include "vfs.h"

#include <cstdio>
#include <set>
#include <string>

using toms::res::ResourceManager;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", std::string(msg).c_str()); g_fail++; } } while (0)

int main() {
    ResourceManager rm;
    rm.setRoot("media");
    CHECK(rm.loadManifest("resources.json"), "media/resources.json loads");

    // 1. The header is the manifest's (run python tools/make_res_keys.py after changing the manifest).
    std::set<std::string> header(std::begin(toms::key::kAll), std::end(toms::key::kAll));
    const std::vector<std::string> keys = rm.keys();
    for (const std::string& k : keys) CHECK(header.count(k), "key \"" + k + "\" is not in res_keys.h: run tools/make_res_keys.py");
    for (const std::string& k : header) CHECK(rm.has(k), "res_keys.h has \"" + k + "\", the manifest does not: run tools/make_res_keys.py");
    std::set<std::string> pheader(std::begin(toms::pattern::kAll), std::end(toms::pattern::kAll));
    for (const std::string& p : rm.patternNames()) CHECK(pheader.count(p), "pattern \"" + p + "\" is not in res_keys.h: run tools/make_res_keys.py");
    for (const std::string& p : pheader) CHECK(rm.hasPattern(p), "res_keys.h has pattern \"" + p + "\", the manifest does not");

    // 2. Every key names what is there: a file, or a folder with files in it.
    for (const std::string& k : keys) {
        const std::string path = rm.resolve(k);
        if (rm.entry(k)->optional) continue;   // may not be there yet
        if (rm.entry(k)->type == "folder") CHECK(!toms::vfsListDir(path).empty(), "folder " + k + " (" + path + ") has files");
        else CHECK(toms::vfsExists(path), "file " + k + " (" + path + ") is there");
    }

    // 3. The patterns give the files the game has (one known id each); an unknown pattern gives "".
    CHECK(toms::vfsExists(rm.pathFor(toms::pattern::kStage, "stage01")), "stage stage01");
    CHECK(toms::vfsExists(rm.pathFor(toms::pattern::kFloorStage, "F01")), "floor_stage F01");
    CHECK(toms::vfsExists(rm.pathFor(toms::pattern::kChapter, "ch_01")), "chapter ch_01");
    CHECK(toms::vfsExists(rm.pathFor(toms::pattern::kDialogue, "advisor")), "dialogue advisor");
    CHECK(toms::vfsExists(rm.pathFor(toms::pattern::kStyleAtlas, "dark16")), "style_atlas dark16");
    CHECK(rm.pathFor(toms::pattern::kStage, "stage01") == "data/stages/stage01.json", "a path out of the root is folded: " +
          rm.pathFor(toms::pattern::kStage, "stage01"));
    CHECK(rm.pathFor("no_such_pattern", "x").empty(), "an unknown pattern gives \"\"");

    if (g_fail) { std::printf("res_keys_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("res_keys_test: all passed (%zu keys, %zu patterns)\n", keys.size(), pheader.size());
    return 0;
}
