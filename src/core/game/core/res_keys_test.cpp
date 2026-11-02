// res_keys_test.cpp -- res_keys.h and assets/media/resources.json say the same, and every key and pattern names a file
// that is there (docs/engine/12_RESOURCE_MANAGER.md). Runs in assets/ (tests/CMakeLists.txt). Exits 0 on success.
#include "res_keys.h"
#include "resource.h"
#include "vfs.h"

#include <cstdio>
#include <set>
#include <string>

using toms::res::ResourceManager;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", std::string(msg).c_str()); g_iFail++; } } while (0)

int main() {
    ResourceManager l_oRm;
    l_oRm.f_SetRoot("media");
    CHECK(l_oRm.f_LoadManifest("resources.json"), "media/resources.json loads");

    // 1. The header is the manifest's (run python tools/make_res_keys.py after changing the manifest).
    std::set<std::string> l_setHeader(std::begin(toms::key::g_arrAll), std::end(toms::key::g_arrAll));
    const std::vector<std::string> l_vKeys = l_oRm.f_Keys();
    for (const std::string& l_strK : l_vKeys) CHECK(l_setHeader.count(l_strK), "key \"" + l_strK + "\" is not in res_keys.h: run tools/make_res_keys.py");
    for (const std::string& l_strK : l_setHeader) CHECK(l_oRm.f_Has(l_strK), "res_keys.h has \"" + l_strK + "\", the manifest does not: run tools/make_res_keys.py");
    std::set<std::string> l_setPheader(std::begin(toms::pattern::g_arrAll), std::end(toms::pattern::g_arrAll));
    for (const std::string& l_strP : l_oRm.f_PatternNames()) CHECK(l_setPheader.count(l_strP), "pattern \"" + l_strP + "\" is not in res_keys.h: run tools/make_res_keys.py");
    for (const std::string& l_strP : l_setPheader) CHECK(l_oRm.f_HasPattern(l_strP), "res_keys.h has pattern \"" + l_strP + "\", the manifest does not");

    // 2. Every key names what is there: a file, or a folder with files in it.
    for (const std::string& l_strK : l_vKeys) {
        const std::string l_strPath = l_oRm.f_Resolve(l_strK);
        if (l_oRm.f_Entry(l_strK)->m_bOptional) continue;   // may not be there yet
        if (l_oRm.f_Entry(l_strK)->m_strType == "folder") CHECK(!toms::f_VfsListDir(l_strPath).empty(), "folder " + l_strK + " (" + l_strPath + ") has files");
        else CHECK(toms::f_VfsExists(l_strPath), "file " + l_strK + " (" + l_strPath + ") is there");
    }

    // 3. The patterns give the files the game has (one known id each); an unknown pattern gives "".
    CHECK(toms::f_VfsExists(l_oRm.f_PathFor(toms::pattern::g_pszStage, "stage01")), "stage stage01");
    CHECK(toms::f_VfsExists(l_oRm.f_PathFor(toms::pattern::g_pszFloorStage, "F01")), "floor_stage F01");
    CHECK(toms::f_VfsExists(l_oRm.f_PathFor(toms::pattern::g_pszChapter, "ch_01")), "chapter ch_01");
    CHECK(toms::f_VfsExists(l_oRm.f_PathFor(toms::pattern::g_pszDialogue, "advisor")), "dialogue advisor");
    CHECK(toms::f_VfsExists(l_oRm.f_PathFor(toms::pattern::g_pszStyleAtlas, "dark16")), "style_atlas dark16");
    CHECK(l_oRm.f_PathFor(toms::pattern::g_pszStage, "stage01") == "data/stages/stage01.json", "a path out of the root is folded: " +
          l_oRm.f_PathFor(toms::pattern::g_pszStage, "stage01"));
    CHECK(l_oRm.f_PathFor("no_such_pattern", "x").empty(), "an unknown pattern gives \"\"");

    if (g_iFail) { std::printf("res_keys_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("res_keys_test: all passed (%zu keys, %zu patterns)\n", l_vKeys.size(), l_setPheader.size());
    return 0;
}
