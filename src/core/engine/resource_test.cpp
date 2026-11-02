// resource_test.cpp -- the resource cache (resource.h, docs/engine/12_RESOURCE_MANAGER.md):
// one object per file, shared dependencies, failures cached once, the memory budget, stamps, reloading in place,
// textures freed with their last holder and never through a renderer that is gone. Runs in assets/ (tests/CMakeLists.txt),
// on the shipped atlases and clips plus copies in a temp folder (for the reload checks). No GPU: a counting renderer.
#include "resource.h"
#include "resource_types.h"
#include "vfs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)

// Counts textures made and freed; draws nothing.
class CountingRenderer : public IRenderer {
public:
    int m_iMade = 0, m_iFreed = 0;
    uint16_t m_u16Next = 1;
    void f_Init(uint32_t, uint32_t) override {}
    void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    uint16_t f_LoadTexture(const std::vector<uint8_t>&, uint32_t, uint32_t) override { m_iMade++; return m_u16Next++; }
    void f_ReleaseTexture(uint16_t) override { m_iFreed++; }
    void begin() override {}
    void f_DrawSprite(const Quad&) override {}
    void end() override {}
    uint32_t f_Width() const override { return 1024; }
    uint32_t f_Height() const override { return 768; }
};

namespace fs = std::filesystem;

void f_CopyFile(const fs::path& l_oFrom, const fs::path& l_oTo) {
    std::error_code l_oEc;
    fs::copy_file(l_oFrom, l_oTo, fs::copy_options::overwrite_existing, l_oEc);
}

void f_WriteText(const fs::path& l_oP, const std::string& l_strText) {
    std::ofstream l_oF(l_oP, std::ios::binary);
    l_oF << l_strText;
}

// bump the file's time and size so checkFiles() sees a change even within the same second
void f_Touch(const fs::path& l_oP, const std::string& l_strExtra) {
    std::ofstream l_oF(l_oP, std::ios::binary | std::ios::app);
    l_oF << l_strExtra;
    l_oF.close();
    std::error_code l_oEc;
    fs::last_write_time(l_oP, fs::last_write_time(l_oP, l_oEc) + std::chrono::seconds(2), l_oEc);
}
}  // namespace

int main() {
    using namespace toms::res;
    const std::string l_strIdle = "media/anim/monster_idle.anim", l_strDoor = "media/anim/door_open.anim";
    if (!toms::f_VfsExists(l_strIdle)) {
        std::printf("resource_test: run in assets/ (%s not found)\n", l_strIdle.c_str());
        return 1;
    }

    // ---- keys ----
    CHECK(ResourceManager::f_Key("Media\\Atlas/../atlas/./FX.atlas") == "media/atlas/fx.atlas", "key folds separators, dots and case");
    CHECK(ResourceManager::f_Normalize("a/b/../C.png") == "a/C.png", "normalize keeps the case");
    CHECK(ResourceManager::f_Join("media/anim/", "../atlas/fx.atlas") == "media/anim/../atlas/fx.atlas", "join relative");
    CHECK(ResourceManager::f_Join("media/anim/", "C:/x.atlas") == "C:/x.atlas", "join keeps an absolute path");

    CountingRenderer l_oRen;
    {
        ResourceManager l_oRm(&l_oRen);

        // ---- one object per file; dependencies shared ----
        auto l_spA1 = l_oRm.f_Get<AnimRes>(l_strIdle);
        auto l_spA2 = l_oRm.f_Get<AnimRes>("media/anim/../anim/monster_idle.anim");
        CHECK(l_spA1 == l_spA2, "the same file gives the same object");
        CHECK(l_spA1->f_Ok() && l_spA1->f_Find("idle_slime"), "the idle clips load");
        auto l_spD = l_oRm.f_Get<AnimRes>(l_strDoor);
        auto l_spFx1 = l_oRm.f_Get<AtlasRes>("media/atlas/fx.atlas");
        CHECK(l_spFx1->f_Ok(), "fx.atlas loads");
        bool l_bIdleHasFx = false, l_bDoorHasFx = false;
        for (const auto& l_oE : l_spA1->f_Atlases().m_vEntries) l_bIdleHasFx |= l_oE.m_pAtlas == &l_spFx1->f_Atlas();
        for (const auto& l_oE : l_spD->f_Atlases().m_vEntries) l_bDoorHasFx |= l_oE.m_pAtlas == &l_spFx1->f_Atlas();
        CHECK(l_bIdleHasFx && l_bDoorHasFx, "the idle and door clips share one fx atlas (it was loaded twice before)");
        const int l_iTexturesAfterBoth = l_oRen.m_iMade;
        CHECK(l_iTexturesAfterBoth == 2, "fx.png and game.png uploaded once each, not once per clip file");
        CHECK(!l_spFx1->f_PageTextures().empty() && l_spFx1->f_PageTextures()[0] != g_u16SpriteAtlasTexture, "the atlas page is a real texture");
        CHECK(std::string(l_spA1->f_Type()) == "AnimRes" && l_spA1->f_Name() == l_spA1->f_Path(), "a resource is a named, typed Object");

        // ---- failures: cached once, never null ----
        auto l_spMissing = l_oRm.f_Get<AtlasRes>("media/atlas/nope.atlas");
        CHECK(l_spMissing && !l_spMissing->f_Ok() && !l_spMissing->f_Error().empty(), "a missing file is a Failed resource with its error");
        CHECK(l_oRm.f_Get<AtlasRes>("media/atlas/nope.atlas") == l_spMissing, "the failure is cached (not read again)");

        // ---- the game's sprite atlas, provided ----
        toms::AtlasFile l_oSpriteAtlas;
        l_oSpriteAtlas.m_vRegions.push_back({});
        auto l_spProvided = l_oRm.f_Provide<AtlasRes>(ResourceManager::m_pszSpriteAtlas, [&](AtlasRes& l_oR) { l_oR.f_SetProvided(l_oSpriteAtlas); });
        CHECK(l_spProvided->f_Ok() && l_spProvided->f_PageTextures().empty(), "a provided atlas draws with the sprite texture");
        const int l_iPv = l_spProvided->f_Version();
        auto l_spAgain = l_oRm.f_Provide<AtlasRes>(ResourceManager::m_pszSpriteAtlas, [&](AtlasRes& l_oR) { l_oR.f_SetProvided(l_oSpriteAtlas); });
        CHECK(l_spAgain == l_spProvided && l_spProvided->f_Version() == l_iPv + 1, "providing again updates the same object");
        l_oRm.f_SetPathMapper([](const std::string& l_strP) {
            return l_strP.size() >= 16 && l_strP.compare(l_strP.size() - 16, 16, "atlas/game.atlas") == 0 ? std::string(ResourceManager::m_pszSpriteAtlas) : l_strP;
        });
        CHECK(l_oRm.f_Get<AtlasRes>("media/atlas/game.atlas") == l_spProvided, "the path mapper sends game.atlas to the provided atlas");
        l_oRm.f_SetPathMapper(nullptr);

        // ---- memory: unused kept until the budget ----
        l_spA1.reset(); l_spA2.reset(); l_spD.reset(); l_spFx1.reset(); l_spMissing.reset(); l_spAgain.reset();
        CHECK(l_oRm.f_UnusedBytes() > 0, "unused resources stay cached under the budget");
        CHECK(l_oRm.f_Get<AnimRes>(l_strIdle)->f_Ok() && l_oRen.m_iMade == l_iTexturesAfterBoth, "asking again costs nothing");
        l_oRm.f_SetBudget(0);
        CHECK(l_oRm.f_UnusedBytes() == 0, "a budget of 0 frees every unused resource");
        CHECK(l_oRen.m_iFreed == l_oRen.m_iMade, "their textures were freed with them");
        CHECK(l_oRm.f_Find<AtlasRes>(ResourceManager::m_pszSpriteAtlas) == l_spProvided, "a provided resource is never freed");
        l_oRm.f_SetBudget(64u << 20);

        // ---- stamps ----
        auto l_spBefore = l_oRm.f_Get<AtlasRes>("media/atlas/fx.atlas");   // base stamp
        const int l_iMade0 = l_oRen.m_iMade;
        {
            ResourceStamp l_oS(l_oRm, "preview");
            auto l_spInside = l_oRm.f_Get<AnimRes>(l_strDoor);                   // new in the stamp; fx.atlas was already loaded
            auto l_spHeld = l_oRm.f_Get<AtlasRes>("media/atlas/game.atlas"); // new, and still held when the stamp ends
            CHECK(l_spInside->f_Ok() && l_spHeld->f_Ok(), "loads inside a stamp");
            CHECK(l_oRen.m_iMade == l_iMade0 + 1, "only game.png was new");
            l_spInside.reset();
            {
                auto l_spKeepMe = l_spHeld;
                l_oRm.f_Keep(l_spKeepMe);
            }
            l_spHeld.reset();
        }
        CHECK(!l_oRm.f_Find<AnimRes>(l_strDoor), "what the stamp loaded and nobody holds is freed when it ends");
        CHECK(l_oRm.f_Find<AtlasRes>("media/atlas/fx.atlas") == l_spBefore, "what was loaded before the stamp stays");
        CHECK(l_oRm.f_Find<AtlasRes>("media/atlas/game.atlas") != nullptr, "keep() moved it out of the stamp");
        l_spBefore.reset();

        // ---- report ----
        bool l_bSawFx = false;
        for (const auto& l_oI : l_oRm.f_Report()) l_bSawFx |= l_oI.m_strType == "AtlasRes" && l_oI.m_strPath == "media/atlas/fx.atlas";
        CHECK(l_bSawFx && l_oRm.f_TotalBytes() > 0, "report lists what is loaded");
    }
    CHECK(l_oRen.m_iFreed == l_oRen.m_iMade, "every texture is freed when the manager goes");

    // ---- reloading in place ----
    {
        std::error_code l_oEc;
        const fs::path l_oDir = fs::temp_directory_path() / "toms_resource_test";
        fs::create_directories(l_oDir, l_oEc);
        f_CopyFile("media/atlas/fx.atlas", l_oDir / "fx.atlas");
        f_CopyFile("media/atlas/fx.png", l_oDir / "fx.png");
        const std::string l_strAtlasPath = (l_oDir / "fx.atlas").generic_string();
        CountingRenderer l_oR2;
        ResourceManager l_oRm(&l_oR2);
        auto l_spAtlas = l_oRm.f_Get<AtlasRes>(l_strAtlasPath);
        CHECK(l_spAtlas->f_Ok() && l_spAtlas->f_Find("dot"), "the copy loads");
        const int l_iV0 = l_spAtlas->f_Version(), l_iC0 = l_spAtlas->f_ContentVersion();
        CHECK(l_oRm.f_CheckFiles() == 0, "nothing changed: nothing reloaded");
        f_Touch(l_oDir / "fx.atlas", "\n");
        CHECK(l_oRm.f_CheckFiles() == 1, "a changed .atlas reloads");
        CHECK(l_oRm.f_Get<AtlasRes>(l_strAtlasPath) == l_spAtlas && l_spAtlas->f_Version() == l_iV0 + 1, "in place: same object, version + 1");
        f_Touch(l_oDir / "fx.png", std::string(1, '\0'));
        const int l_iFreedBefore = l_oR2.m_iFreed;
        CHECK(l_oRm.f_CheckFiles() == 1 && l_spAtlas->f_ContentVersion() > l_iC0 + 1, "a changed page bumps the atlas's content version");
        CHECK(l_oR2.m_iFreed == l_iFreedBefore + 1, "the old page texture was freed after the new one was up");
        f_WriteText(l_oDir / "fx.atlas", "size: 1, 1\n");   // a field before any page: parseAtlas fails
        f_Touch(l_oDir / "fx.atlas", "");
        l_oRm.f_CheckFiles();
        CHECK(l_spAtlas->f_Ok() && l_spAtlas->f_Find("dot"), "a broken reload keeps the old contents");

        // a renderer that goes first: textures still held are freed now, not later through a dead pointer
        l_oRm.f_DetachRenderer();
        CHECK(l_oR2.m_iFreed == l_oR2.m_iMade, "detachRenderer frees every texture");
        const int l_iFreedAfterDetach = l_oR2.m_iFreed;
        l_spAtlas.reset();
        l_oRm.f_ReleaseUnused();
        CHECK(l_oR2.m_iFreed == l_iFreedAfterDetach, "nothing is freed through the renderer after it was detached");
        fs::remove_all(l_oDir, l_oEc);
    }

    // ---- the manifest: keys, the root folder, type checks, the missing handler, preload ----
    {
        std::error_code l_oEc;
        const fs::path l_oDir = fs::temp_directory_path() / "toms_manifest_test";
        fs::create_directories(l_oDir / "atlas", l_oEc);
        f_CopyFile("media/atlas/fx.atlas", l_oDir / "atlas" / "fx.atlas");
        f_CopyFile("media/atlas/fx.png", l_oDir / "atlas" / "fx.png");
        f_WriteText(l_oDir / "resources.json", R"({"resources": {
            "atlas.fx":   {"type": "atlas", "path": "atlas/fx.atlas", "preload": true},
            "atlas.gone": {"type": "atlas", "path": "atlas/gone.atlas"},
            "anim.wrong": {"type": "anim",  "path": "atlas/fx.png"}}})");
        ResourceManager l_oRm;
        l_oRm.f_SetRoot(l_oDir.generic_string());
        std::vector<std::string> l_vMissing;
        l_oRm.f_SetOnMissing([&](const std::string& l_strName, const std::string&, const std::string&) { l_vMissing.push_back(l_strName); });
        CHECK(l_oRm.f_LoadManifest("resources.json"), "the manifest loads from the root folder");
        CHECK(l_oRm.f_Has("atlas.fx") && l_oRm.f_Keys().size() == 3, "its keys are known");
        CHECK(l_oRm.f_Preload() == 0 && l_oRm.f_Find<AtlasRes>("atlas.fx") != nullptr, "preload loads the entries marked preload");
        auto l_spByKey = l_oRm.f_Get<AtlasRes>("atlas.fx");
        auto l_spByPath = l_oRm.f_Get<AtlasRes>("atlas/fx.atlas");
        CHECK(l_spByKey->f_Ok() && l_spByKey == l_spByPath, "a key and its path give the same object");
        CHECK(l_oRm.f_Resolve("atlas/fx.atlas") == l_oRm.f_Resolve("atlas.fx"), "a path is read under the root, like the key's");
        CHECK(l_vMissing.empty(), "nothing missing so far");
        auto l_spGone = l_oRm.f_Get<AtlasRes>("atlas.gone");
        l_oRm.f_Get<AtlasRes>("atlas.gone");
        CHECK(!l_spGone->f_Ok() && l_vMissing.size() == 1 && l_vMissing[0] == "atlas.gone", "a missing file is told once, by its key");
        auto l_spWrong = l_oRm.f_Get<AnimRes>("anim.wrong");
        CHECK(!l_spWrong->f_Ok() && l_vMissing.size() == 2, "a manifest entry read as its type: an atlas is not an anim");
        auto l_spWrongType = l_oRm.f_Get<AnimRes>("atlas.fx");
        CHECK(!l_spWrongType->f_Ok() && l_spWrongType->f_Error().find("is a atlas") != std::string::npos, "asking for a key as another type says so");
        CHECK(!l_oRm.f_Get<AtlasRes>("no/such/file.atlas")->f_Ok() && l_vMissing.size() == 4, "a path that is not there is told too");
        CHECK(!l_oRm.f_LoadManifest("nope.json") && l_vMissing.size() == 5, "a missing manifest is told");
        fs::remove_all(l_oDir, l_oEc);
    }

    // ---- the shipped manifest: every entry is there and loads as its type ----
    {
        ResourceManager l_oRm;
        l_oRm.f_SetRoot("media");
        std::vector<std::string> l_vMissing;
        l_oRm.f_SetOnMissing([&](const std::string& l_strName, const std::string& l_strPath, const std::string& l_strError) {
            l_vMissing.push_back(l_strName + " (" + l_strPath + "): " + l_strError);
        });
        CHECK(l_oRm.f_LoadManifest("resources.json"), "assets/media/resources.json loads");
        CHECK(!l_oRm.f_Keys().empty(), "it names some resources");
        for (const std::string& l_strK : l_oRm.f_Keys()) {
            const auto* l_pE = l_oRm.f_Entry(l_strK);
            const std::string l_strT = l_pE ? l_pE->m_strType : std::string();
            CHECK(l_pE && (l_pE->m_bOptional || (l_strT == "folder" ? !toms::f_VfsListDir(l_oRm.f_Resolve(l_strK)).empty() : toms::f_VfsExists(l_oRm.f_Resolve(l_strK)))),
                  ("the manifest's file is there: " + l_strK).c_str());
            CHECK(l_strT == "texture" || l_strT == "atlas" || l_strT == "anim" || l_strT == "particle" || l_strT == "font" || l_strT == "json" ||
                  l_strT == "file" || l_strT == "folder", ("a known type: " + l_strK + " (" + l_strT + ")").c_str());
        }
        l_oRm.f_Preload();
        for (const std::string& l_strK : l_oRm.f_Keys())
            if (l_oRm.f_Entry(l_strK)->m_strType == "atlas") l_oRm.f_Get<AtlasRes>(l_strK);
            else if (l_oRm.f_Entry(l_strK)->m_strType == "anim") l_oRm.f_Get<AnimRes>(l_strK);
        for (const std::string& l_strM : l_vMissing) std::printf("  missing: %s\n", l_strM.c_str());
        CHECK(l_vMissing.empty(), "every shipped manifest entry loads");
    }

    // ---- UTF-8 paths (a non-ASCII folder) ----
    {
        std::error_code l_oEc;
        const fs::path l_oDir = fs::temp_directory_path() / toms::f_VfsPath("toms_資源_test");
        fs::create_directories(l_oDir, l_oEc);
        f_CopyFile("media/atlas/fx.atlas", l_oDir / "fx.atlas");
        f_CopyFile("media/atlas/fx.png", l_oDir / "fx.png");
        ResourceManager l_oRm;
        auto l_spAtlas = l_oRm.f_Get<AtlasRes>(toms::f_VfsUtf8(l_oDir / "fx.atlas"));
        CHECK(l_spAtlas->f_Ok() && l_spAtlas->f_Find("dot"), "a UTF-8 path with Chinese letters opens");
        fs::remove_all(l_oDir, l_oEc);
    }

    std::printf("resource_test: %s (%d failure(s))\n", g_iFail ? "FAILED" : "passed", g_iFail);
    return g_iFail ? 1 : 0;
}
