// render_object_test.cpp -- render objects made from JSON (render_object.h, docs/engine/12_RESOURCE_MANAGER.md §4.7):
// each built-in type, the errors (unknown type, missing file or sprite, where in the tree), the red "missing" box,
// clones sharing resources, prototypes, toJson round trips, the tree's visibility rule and the stage editor's look.
// Runs in assets/ on the shipped atlas and clips; the particle file is written to a temp folder. No GPU: a renderer
// that records the quads.
#include "render_object.h"
#include "vfs.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)

class RecordingRenderer : public IRenderer {
public:
    std::vector<Quad> m_vQuads;
    uint16_t m_u16Next = 1;
    void f_Init(uint32_t, uint32_t) override {}
    void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    uint16_t f_LoadTexture(const std::vector<uint8_t>&, uint32_t, uint32_t) override { return m_u16Next++; }
    void f_ReleaseTexture(uint16_t) override {}
    void begin() override {}
    void f_DrawSprite(const Quad& l_oQ) override { m_vQuads.push_back(l_oQ); }
    void end() override {}
    uint32_t f_Width() const override { return 1024; }
    uint32_t f_Height() const override { return 768; }
};

bool f_Contains(const std::string& l_strS, const std::string& l_strPart) { return l_strS.find(l_strPart) != std::string::npos; }
}  // namespace

int main() {
    using nlohmann::json;
    using namespace toms;
    if (!f_VfsExists("media/atlas/fx.atlas")) {
        std::printf("render_object_test: run in assets/\n");
        return 1;
    }
    RecordingRenderer l_oRen;
    res::ResourceManager l_oRm(&l_oRen);
    RenderObjectFactory l_oF;
    f_RegisterBuiltinRenderObjects(l_oF);
    CHECK(l_oF.f_Has("sprite") && l_oF.f_Has("clip") && l_oF.f_Has("particle") && l_oF.f_Has("text") && l_oF.f_Has("group") && l_oF.f_Has("missing"),
          "the built-in types are registered");

    // ---- sprite ----
    std::string l_strErr;
    auto l_spS = l_oF.f_Make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"pos", {100, 50}}, {"tint", {1, 0.5, 0.5, 1}}}, l_oRm, &l_strErr);
    CHECK(l_spS && l_strErr.empty(), "a sprite from JSON");
    CHECK(l_spS && std::string(l_spS->f_Type()) == "SpriteObject" && std::string(l_spS->f_TypeName()) == "sprite", "its real class behind RenderObject");
    if (l_spS) {
        l_oRen.m_vQuads.clear();
        l_spS->f_RenderTree(&l_oRen);
        CHECK(l_oRen.m_vQuads.size() == 1, "a sprite draws one quad");
        if (!l_oRen.m_vQuads.empty()) {
            const Quad& l_oQ = l_oRen.m_vQuads[0];
            auto l_spFx = l_oRm.f_Get<res::AtlasRes>("media/atlas/fx.atlas");
            CHECK(l_oQ.m_u16Texture == l_spFx->f_PageTextures()[0], "with its atlas page's texture");
            CHECK(l_oQ.m_arrRect[0] < 100 && l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2] > 100 && l_oQ.m_arrRect[1] < 50 && l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3] > 50, "around its position");
            CHECK(l_oQ.m_arrTint[1] == 0.5f, "tinted");
        }
    }

    // ---- errors ----
    CHECK(!l_oF.f_Make({{"type", "teapot"}}, l_oRm, &l_strErr) && f_Contains(l_strErr, "unknown type 'teapot'"), "an unknown type says so");
    CHECK(!l_oF.f_Make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "zz"}}, l_oRm, &l_strErr) && f_Contains(l_strErr, "no sprite 'zz'"),
          "a missing sprite says which");
    CHECK(!l_oF.f_Make({{"type", "clip"}, {"anim", "media/anim/nope.anim"}, {"clip", "x"}}, l_oRm, &l_strErr) && f_Contains(l_strErr, "cannot load"),
          "a missing file says so");
    CHECK(!l_oF.f_Make({{"type", "group"}, {"children", {{{"type", "group"}}, {{"type", "teapot"}}}}}, l_oRm, &l_strErr) && f_Contains(l_strErr, "children[1]"),
          "an error in a child says where");
    auto l_spM = l_oF.f_MakeOrMissing({{"type", "teapot"}, {"pos", {10, 10}}}, l_oRm, &l_strErr);
    CHECK(l_spM && std::string(l_spM->f_TypeName()) == "missing" && !l_spM->f_Ok() && !l_strErr.empty(), "makeOrMissing never gives null");
    l_oRen.m_vQuads.clear();
    if (l_spM) l_spM->f_RenderTree(&l_oRen);
    CHECK(l_oRen.m_vQuads.size() == 1 && l_oRen.m_vQuads[0].m_bSolid, "a mistake draws a red box");

    // ---- clip ----
    auto l_spC = l_oF.f_Make({{"type", "clip"}, {"anim", "media/anim/monster_idle.anim#idle_slime"}, {"loop", true}, {"pos", {64, 64}}}, l_oRm, &l_strErr);
    CHECK(l_spC && l_spC->f_Ok(), "a clip from \"file#clip\"");
    if (l_spC) {
        l_spC->f_Update(0.25f);
        l_oRen.m_vQuads.clear();
        l_spC->f_RenderTree(&l_oRen);
        CHECK(!l_oRen.m_vQuads.empty(), "a clip draws its sprites");
        auto l_spC2 = std::static_pointer_cast<ClipObject>(l_spC->f_Clone());
        CHECK(l_spC2 != l_spC && l_spC2->f_Anim() == std::static_pointer_cast<ClipObject>(l_spC)->f_Anim(), "a clone shares the clip file");
        CHECK(l_spC2->f_Pos() == l_spC->f_Pos(), "a clone has the same place");
        l_spC->f_Update(10.0f);
        CHECK(std::static_pointer_cast<ClipObject>(l_spC)->f_Player().f_Showing(), "a looping clip keeps playing");
    }

    // ---- particle (a file written for the test) ----
    namespace fs = std::filesystem;
    std::error_code l_oEc;
    const fs::path l_oDir = fs::temp_directory_path() / "toms_render_object_test";
    fs::create_directories(l_oDir, l_oEc);
    {
        fx::ParticleFile l_oPf;
        l_oPf.m_vAtlases.push_back({"fx", fs::absolute("media/atlas/fx.atlas").generic_string()});
        fx::Effect l_oE;
        l_oE.m_strName = "spark";
        l_oE.m_uSeed = 7;
        fx::Emitter l_oEm;
        l_oEm.m_strName = "dots";
        l_oEm.m_fRate = 60;
        l_oEm.m_strSprite = "fx:dot";
        l_oE.m_vEmitters.push_back(l_oEm);
        l_oPf.m_vEffects.push_back(l_oE);
        std::ofstream(l_oDir / "test.prt", std::ios::binary) << fx::f_ParticlesToJson(l_oPf);
    }
    const std::string l_strPrt = (l_oDir / "test.prt").generic_string();
    auto l_spP = l_oF.f_Make({{"type", "particle"}, {"file", l_strPrt}, {"effect", "spark"}, {"pos", {200, 200}}}, l_oRm, &l_strErr);
    CHECK(l_spP && l_spP->f_Ok(), "a particle effect from JSON");
    if (l_spP) {
        l_oRen.m_vQuads.clear();
        l_spP->f_RenderTree(&l_oRen);    // starts it where it is
        l_spP->f_Update(0.5f);
        l_oRen.m_vQuads.clear();
        l_spP->f_RenderTree(&l_oRen);
        CHECK(!l_oRen.m_vQuads.empty(), "an effect draws its particles");
    }
    // ---- the view (a camera): everything draws through it; an effect's particles stay under it ----
    {
        auto l_spV = l_oF.f_Make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"pos", {100, 50}}}, l_oRm, &l_strErr);
        auto l_spE = l_oF.f_Make({{"type", "particle"}, {"file", l_strPrt}, {"effect", "spark"}, {"pos", {200, 200}}}, l_oRm, &l_strErr);
        if (l_spV && l_spE) {
            l_oRen.m_vQuads.clear();
            l_spV->f_RenderTree(&l_oRen);
            const float l_fX0 = l_oRen.m_vQuads.empty() ? 0 : l_oRen.m_vQuads[0].m_arrRect[0];
            l_spV->f_SetView(anim::f_Placement(30, 0));
            l_oRen.m_vQuads.clear();
            l_spV->f_RenderTree(&l_oRen);
            CHECK(!l_oRen.m_vQuads.empty() && std::fabs(l_oRen.m_vQuads[0].m_arrRect[0] - (l_fX0 + 30)) < 0.01f, "a sprite draws through its view");
            l_spE->f_SetView(anim::f_Placement(0, 0));
            l_spE->f_RenderTree(&l_oRen);   // born at (200, 200) on the "map"
            l_spE->f_Update(0.2f);
            l_spE->f_SetView(anim::f_Placement(500, 0));   // the camera moves: the particles come with it
            l_oRen.m_vQuads.clear();
            l_spE->f_RenderTree(&l_oRen);
            bool l_bAllMoved = !l_oRen.m_vQuads.empty();
            for (const Quad& l_oQ : l_oRen.m_vQuads) l_bAllMoved = l_bAllMoved && l_oQ.m_arrRect[0] > 600;
            CHECK(l_bAllMoved, "an effect's particles move with the view (they live on the map, not on the screen)");
        }
    }
    // ---- a solid sprite, and a clip set to a time from outside ----
    {
        auto l_spSo = l_oF.f_Make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"solid", true}}, l_oRm, &l_strErr);
        CHECK(l_spSo && l_spSo->f_ToJson().value("solid", false) && std::static_pointer_cast<SpriteObject>(l_spSo->f_Clone())->m_bSolid,
              "\"solid\" is read, written and cloned");
        if (l_spSo) {
            l_oRen.m_vQuads.clear();
            l_spSo->f_RenderTree(&l_oRen);
            CHECK(!l_oRen.m_vQuads.empty() && l_oRen.m_vQuads[0].m_bSolid, "a solid sprite draws one flat colour");
        }
        auto l_spCt = std::static_pointer_cast<ClipObject>(l_oF.f_Make({{"type", "clip"}, {"anim", "media/anim/door_open.anim#door_open_yellow"}}, l_oRm, &l_strErr));
        if (l_spCt) {
            const float l_fLen = l_spCt->f_Duration();
            CHECK(l_fLen > 0, "a clip's duration");
            l_spCt->f_SetTime(l_fLen * 0.5f);
            CHECK(std::fabs(l_spCt->f_Player().f_Time() - l_fLen * 0.5f) < 0.001f, "setTime: the clip shows that time");
            l_spCt->f_SetTime(0.0f);
            CHECK(l_spCt->f_Player().f_Time() == 0.0f, "setTime: back to the start (no update needed)");
        }
    }
    CHECK(l_oRm.f_Get<res::ParticleRes>(l_strPrt)->f_Atlases().m_vEntries.size() == 1 &&
              l_oRm.f_Get<res::ParticleRes>(l_strPrt)->f_Atlases().m_vEntries[0].m_pAtlas == &l_oRm.f_Get<res::AtlasRes>("media/atlas/fx.atlas")->f_Atlas(),
          "the effect's atlas is the same fx atlas the sprite and clip use");

    // ---- text ----
    const std::string l_strFnt = "../tests/font/connect_score.fnt";
    if (f_VfsExists(l_strFnt)) {
        auto l_spT = l_oF.f_Make({{"type", "text"}, {"font", l_strFnt}, {"text", "123"}, {"pos", {10, 10}}, {"align", "right"}}, l_oRm, &l_strErr);
        CHECK(l_spT != nullptr, "a text from JSON");
        if (l_spT) {
            l_oRen.m_vQuads.clear();
            l_spT->f_RenderTree(&l_oRen);
            CHECK(l_oRen.m_vQuads.size() == 3, "three digits, three quads");
        }
    }

    // ---- group, children, visibility ----
    json l_jGroupJson = {{"type", "group"}, {"name", "badge"}, {"pos", {300, 100}}, {"scale", 2},
                      {"children", {{{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}},
                                    {{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"pos", {20, 0}}}}}};
    auto l_spG = l_oF.f_Make(l_jGroupJson, l_oRm, &l_strErr);
    CHECK(l_spG && l_spG->f_Children().size() == 2 && l_spG->f_Name() == "badge", "a group with two children");
    if (l_spG) {
        l_oRen.m_vQuads.clear();
        l_spG->f_RenderTree(&l_oRen);
        CHECK(l_oRen.m_vQuads.size() == 2, "a group draws its children");
        if (l_oRen.m_vQuads.size() == 2) CHECK(l_oRen.m_vQuads[1].m_arrRect[0] - l_oRen.m_vQuads[0].m_arrRect[0] > 39.0f, "children are placed by the parent's scale");
        l_spG->f_SetVisible(false);
        l_oRen.m_vQuads.clear();
        l_spG->f_RenderTree(&l_oRen);
        CHECK(l_oRen.m_vQuads.empty(), "a hidden parent hides its children");
        l_spG->f_SetVisible(true);

        // ---- toJson round trip ----
        const json l_jOut = l_spG->f_ToJson();
        auto l_spG2 = l_oF.f_Make(l_jOut, l_oRm, &l_strErr);
        CHECK(l_spG2 && l_spG2->f_ToJson() == l_jOut, "toJson makes the same object again");
        CHECK(l_jOut.value("scale", 0.0f) == 2.0f && l_jOut["children"].size() == 2, "toJson keeps the common fields and children");
    }

    // ---- prototypes ----
    const json l_jSlimeLook = {{"type", "clip"}, {"anim", "media/anim/monster_idle.anim"}, {"clip", "idle_slime"}, {"loop", true}};
    auto l_spI1 = l_oF.f_Instance(l_jSlimeLook, l_oRm, &l_strErr);
    auto l_spI2 = l_oF.f_Instance(l_jSlimeLook, l_oRm, &l_strErr);
    CHECK(l_spI1 && l_spI2 && l_spI1 != l_spI2, "instances are separate objects");
    CHECK(l_spI1 && l_spI2 && std::static_pointer_cast<ClipObject>(l_spI1)->f_Anim() == std::static_pointer_cast<ClipObject>(l_spI2)->f_Anim(),
          "instances share the clip file");
    if (l_spI1) l_spI1->f_SetPos(5, 5);
    CHECK(l_spI2 && l_spI2->f_Pos() == glm::vec2(0.0f), "moving one instance leaves the other");

    // ---- the stage editor's look ----
    const json l_jL1 = f_LookToRenderJson({{"anim", "anim/monster_idle.anim#idle_bat"}, {"tint", {1, 0, 0}}}, "media");
    CHECK(l_jL1.value("type", "") == "clip" && l_jL1.value("anim", "") == "media/anim/monster_idle.anim" && l_jL1.value("clip", "") == "idle_bat",
          "a look's anim becomes a clip");
    CHECK(l_jL1.contains("tint") && l_jL1["tint"].size() == 4, "its tint comes along (alpha 1 added)");
    const json l_jL2 = f_LookToRenderJson({{"sprite", "slime.png"}}, "media");
    CHECK(l_jL2.value("type", "") == "sprite" && l_jL2.value("name", "") == "slime" && l_jL2.value("atlas", "") == res::ResourceManager::m_pszSpriteAtlas,
          "a look's sprite draws from the game's sprite atlas");
    CHECK(f_LookToRenderJson(json::object(), "media").is_null(), "an empty look is nothing");
    CHECK(l_oF.f_Make(l_jL1, l_oRm, &l_strErr) != nullptr, "the look's clip is made");

    fs::remove_all(l_oDir, l_oEc);
    std::printf("render_object_test: %s (%d failure(s))\n", g_iFail ? "FAILED" : "passed", g_iFail);
    return g_iFail ? 1 : 0;
}
