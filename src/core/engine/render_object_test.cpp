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
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

class RecordingRenderer : public IRenderer {
public:
    std::vector<Quad> quads;
    uint16_t next = 1;
    void init(uint32_t, uint32_t) override {}
    void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    uint16_t loadTexture(const std::vector<uint8_t>&, uint32_t, uint32_t) override { return next++; }
    void releaseTexture(uint16_t) override {}
    void begin() override {}
    void drawSprite(const Quad& q) override { quads.push_back(q); }
    void end() override {}
    uint32_t width() const override { return 1024; }
    uint32_t height() const override { return 768; }
};

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
}  // namespace

int main() {
    using nlohmann::json;
    using namespace toms;
    if (!vfsExists("media/atlas/fx.atlas")) {
        std::printf("render_object_test: run in assets/\n");
        return 1;
    }
    RecordingRenderer ren;
    res::ResourceManager rm(&ren);
    RenderObjectFactory f;
    registerBuiltinRenderObjects(f);
    CHECK(f.has("sprite") && f.has("clip") && f.has("particle") && f.has("text") && f.has("group") && f.has("missing"),
          "the built-in types are registered");

    // ---- sprite ----
    std::string err;
    auto s = f.make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"pos", {100, 50}}, {"tint", {1, 0.5, 0.5, 1}}}, rm, &err);
    CHECK(s && err.empty(), "a sprite from JSON");
    CHECK(s && std::string(s->Type()) == "SpriteObject" && std::string(s->typeName()) == "sprite", "its real class behind RenderObject");
    if (s) {
        ren.quads.clear();
        s->RenderTree(&ren);
        CHECK(ren.quads.size() == 1, "a sprite draws one quad");
        if (!ren.quads.empty()) {
            const Quad& q = ren.quads[0];
            auto fx = rm.get<res::AtlasRes>("media/atlas/fx.atlas");
            CHECK(q.texture == fx->pageTextures()[0], "with its atlas page's texture");
            CHECK(q.rect[0] < 100 && q.rect[0] + q.rect[2] > 100 && q.rect[1] < 50 && q.rect[1] + q.rect[3] > 50, "around its position");
            CHECK(q.tint[1] == 0.5f, "tinted");
        }
    }

    // ---- errors ----
    CHECK(!f.make({{"type", "teapot"}}, rm, &err) && contains(err, "unknown type 'teapot'"), "an unknown type says so");
    CHECK(!f.make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "zz"}}, rm, &err) && contains(err, "no sprite 'zz'"),
          "a missing sprite says which");
    CHECK(!f.make({{"type", "clip"}, {"anim", "media/anim/nope.anim"}, {"clip", "x"}}, rm, &err) && contains(err, "cannot load"),
          "a missing file says so");
    CHECK(!f.make({{"type", "group"}, {"children", {{{"type", "group"}}, {{"type", "teapot"}}}}}, rm, &err) && contains(err, "children[1]"),
          "an error in a child says where");
    auto m = f.makeOrMissing({{"type", "teapot"}, {"pos", {10, 10}}}, rm, &err);
    CHECK(m && std::string(m->typeName()) == "missing" && !m->ok() && !err.empty(), "makeOrMissing never gives null");
    ren.quads.clear();
    if (m) m->RenderTree(&ren);
    CHECK(ren.quads.size() == 1 && ren.quads[0].solid, "a mistake draws a red box");

    // ---- clip ----
    auto c = f.make({{"type", "clip"}, {"anim", "media/anim/monster_idle.anim#idle_slime"}, {"loop", true}, {"pos", {64, 64}}}, rm, &err);
    CHECK(c && c->ok(), "a clip from \"file#clip\"");
    if (c) {
        c->update(0.25f);
        ren.quads.clear();
        c->RenderTree(&ren);
        CHECK(!ren.quads.empty(), "a clip draws its sprites");
        auto c2 = std::static_pointer_cast<ClipObject>(c->clone());
        CHECK(c2 != c && c2->anim() == std::static_pointer_cast<ClipObject>(c)->anim(), "a clone shares the clip file");
        CHECK(c2->pos() == c->pos(), "a clone has the same place");
        c->update(10.0f);
        CHECK(std::static_pointer_cast<ClipObject>(c)->player().showing(), "a looping clip keeps playing");
    }

    // ---- particle (a file written for the test) ----
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "toms_render_object_test";
    fs::create_directories(dir, ec);
    {
        fx::ParticleFile pf;
        pf.atlases.push_back({"fx", fs::absolute("media/atlas/fx.atlas").generic_string()});
        fx::Effect e;
        e.name = "spark";
        e.seed = 7;
        fx::Emitter em;
        em.name = "dots";
        em.rate = 60;
        em.sprite = "fx:dot";
        e.emitters.push_back(em);
        pf.effects.push_back(e);
        std::ofstream(dir / "test.prt", std::ios::binary) << fx::particlesToJson(pf);
    }
    const std::string prt = (dir / "test.prt").generic_string();
    auto p = f.make({{"type", "particle"}, {"file", prt}, {"effect", "spark"}, {"pos", {200, 200}}}, rm, &err);
    CHECK(p && p->ok(), "a particle effect from JSON");
    if (p) {
        ren.quads.clear();
        p->RenderTree(&ren);    // starts it where it is
        p->update(0.5f);
        ren.quads.clear();
        p->RenderTree(&ren);
        CHECK(!ren.quads.empty(), "an effect draws its particles");
    }
    // ---- the view (a camera): everything draws through it; an effect's particles stay under it ----
    {
        auto v = f.make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"pos", {100, 50}}}, rm, &err);
        auto e = f.make({{"type", "particle"}, {"file", prt}, {"effect", "spark"}, {"pos", {200, 200}}}, rm, &err);
        if (v && e) {
            ren.quads.clear();
            v->RenderTree(&ren);
            const float x0 = ren.quads.empty() ? 0 : ren.quads[0].rect[0];
            v->setView(anim::placement(30, 0));
            ren.quads.clear();
            v->RenderTree(&ren);
            CHECK(!ren.quads.empty() && std::fabs(ren.quads[0].rect[0] - (x0 + 30)) < 0.01f, "a sprite draws through its view");
            e->setView(anim::placement(0, 0));
            e->RenderTree(&ren);   // born at (200, 200) on the "map"
            e->update(0.2f);
            e->setView(anim::placement(500, 0));   // the camera moves: the particles come with it
            ren.quads.clear();
            e->RenderTree(&ren);
            bool allMoved = !ren.quads.empty();
            for (const Quad& q : ren.quads) allMoved = allMoved && q.rect[0] > 600;
            CHECK(allMoved, "an effect's particles move with the view (they live on the map, not on the screen)");
        }
    }
    // ---- a solid sprite, and a clip set to a time from outside ----
    {
        auto so = f.make({{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"solid", true}}, rm, &err);
        CHECK(so && so->toJson().value("solid", false) && std::static_pointer_cast<SpriteObject>(so->clone())->solid,
              "\"solid\" is read, written and cloned");
        if (so) {
            ren.quads.clear();
            so->RenderTree(&ren);
            CHECK(!ren.quads.empty() && ren.quads[0].solid, "a solid sprite draws one flat colour");
        }
        auto ct = std::static_pointer_cast<ClipObject>(f.make({{"type", "clip"}, {"anim", "media/anim/door_open.anim#door_open_yellow"}}, rm, &err));
        if (ct) {
            const float len = ct->duration();
            CHECK(len > 0, "a clip's duration");
            ct->setTime(len * 0.5f);
            CHECK(std::fabs(ct->player().time() - len * 0.5f) < 0.001f, "setTime: the clip shows that time");
            ct->setTime(0.0f);
            CHECK(ct->player().time() == 0.0f, "setTime: back to the start (no update needed)");
        }
    }
    CHECK(rm.get<res::ParticleRes>(prt)->atlases().entries.size() == 1 &&
              rm.get<res::ParticleRes>(prt)->atlases().entries[0].atlas == &rm.get<res::AtlasRes>("media/atlas/fx.atlas")->atlas(),
          "the effect's atlas is the same fx atlas the sprite and clip use");

    // ---- text ----
    const std::string fnt = "../tests/font/connect_score.fnt";
    if (vfsExists(fnt)) {
        auto t = f.make({{"type", "text"}, {"font", fnt}, {"text", "123"}, {"pos", {10, 10}}, {"align", "right"}}, rm, &err);
        CHECK(t != nullptr, "a text from JSON");
        if (t) {
            ren.quads.clear();
            t->RenderTree(&ren);
            CHECK(ren.quads.size() == 3, "three digits, three quads");
        }
    }

    // ---- group, children, visibility ----
    json groupJson = {{"type", "group"}, {"name", "badge"}, {"pos", {300, 100}}, {"scale", 2},
                      {"children", {{{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}},
                                    {{"type", "sprite"}, {"atlas", "media/atlas/fx.atlas"}, {"name", "dot"}, {"pos", {20, 0}}}}}};
    auto g = f.make(groupJson, rm, &err);
    CHECK(g && g->children().size() == 2 && g->Name() == "badge", "a group with two children");
    if (g) {
        ren.quads.clear();
        g->RenderTree(&ren);
        CHECK(ren.quads.size() == 2, "a group draws its children");
        if (ren.quads.size() == 2) CHECK(ren.quads[1].rect[0] - ren.quads[0].rect[0] > 39.0f, "children are placed by the parent's scale");
        g->SetVisible(false);
        ren.quads.clear();
        g->RenderTree(&ren);
        CHECK(ren.quads.empty(), "a hidden parent hides its children");
        g->SetVisible(true);

        // ---- toJson round trip ----
        const json out = g->toJson();
        auto g2 = f.make(out, rm, &err);
        CHECK(g2 && g2->toJson() == out, "toJson makes the same object again");
        CHECK(out.value("scale", 0.0f) == 2.0f && out["children"].size() == 2, "toJson keeps the common fields and children");
    }

    // ---- prototypes ----
    const json slimeLook = {{"type", "clip"}, {"anim", "media/anim/monster_idle.anim"}, {"clip", "idle_slime"}, {"loop", true}};
    auto i1 = f.instance(slimeLook, rm, &err);
    auto i2 = f.instance(slimeLook, rm, &err);
    CHECK(i1 && i2 && i1 != i2, "instances are separate objects");
    CHECK(i1 && i2 && std::static_pointer_cast<ClipObject>(i1)->anim() == std::static_pointer_cast<ClipObject>(i2)->anim(),
          "instances share the clip file");
    if (i1) i1->setPos(5, 5);
    CHECK(i2 && i2->pos() == glm::vec2(0.0f), "moving one instance leaves the other");

    // ---- the stage editor's look ----
    const json l1 = lookToRenderJson({{"anim", "anim/monster_idle.anim#idle_bat"}, {"tint", {1, 0, 0}}}, "media");
    CHECK(l1.value("type", "") == "clip" && l1.value("anim", "") == "media/anim/monster_idle.anim" && l1.value("clip", "") == "idle_bat",
          "a look's anim becomes a clip");
    CHECK(l1.contains("tint") && l1["tint"].size() == 4, "its tint comes along (alpha 1 added)");
    const json l2 = lookToRenderJson({{"sprite", "slime.png"}}, "media");
    CHECK(l2.value("type", "") == "sprite" && l2.value("name", "") == "slime" && l2.value("atlas", "") == res::ResourceManager::kSpriteAtlas,
          "a look's sprite draws from the game's sprite atlas");
    CHECK(lookToRenderJson(json::object(), "media").is_null(), "an empty look is nothing");
    CHECK(f.make(l1, rm, &err) != nullptr, "the look's clip is made");

    fs::remove_all(dir, ec);
    std::printf("render_object_test: %s (%d failure(s))\n", g_fail ? "FAILED" : "passed", g_fail);
    return g_fail ? 1 : 0;
}
