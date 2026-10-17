// resource_test.cpp -- the resource cache (resource.h, docs/25_RESOURCE_MANAGER.md):
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
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

// Counts textures made and freed; draws nothing.
class CountingRenderer : public IRenderer {
public:
    int made = 0, freed = 0;
    uint16_t next = 1;
    void init(uint32_t, uint32_t) override {}
    void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    uint16_t loadTexture(const std::vector<uint8_t>&, uint32_t, uint32_t) override { made++; return next++; }
    void releaseTexture(uint16_t) override { freed++; }
    void begin() override {}
    void drawSprite(const Quad&) override {}
    void end() override {}
    uint32_t width() const override { return 1024; }
    uint32_t height() const override { return 768; }
};

namespace fs = std::filesystem;

void copyFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

void writeText(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    f << text;
}

// bump the file's time and size so checkFiles() sees a change even within the same second
void touch(const fs::path& p, const std::string& extra) {
    std::ofstream f(p, std::ios::binary | std::ios::app);
    f << extra;
    f.close();
    std::error_code ec;
    fs::last_write_time(p, fs::last_write_time(p, ec) + std::chrono::seconds(2), ec);
}
}  // namespace

int main() {
    using namespace toms::res;
    const std::string idle = "media/anim/monster_idle.anim", door = "media/anim/door_open.anim";
    if (!toms::vfsExists(idle)) {
        std::printf("resource_test: run in assets/ (%s not found)\n", idle.c_str());
        return 1;
    }

    // ---- keys ----
    CHECK(ResourceManager::key("Media\\Atlas/../atlas/./FX.atlas") == "media/atlas/fx.atlas", "key folds separators, dots and case");
    CHECK(ResourceManager::normalize("a/b/../C.png") == "a/C.png", "normalize keeps the case");
    CHECK(ResourceManager::join("media/anim/", "../atlas/fx.atlas") == "media/anim/../atlas/fx.atlas", "join relative");
    CHECK(ResourceManager::join("media/anim/", "C:/x.atlas") == "C:/x.atlas", "join keeps an absolute path");

    CountingRenderer ren;
    {
        ResourceManager rm(&ren);

        // ---- one object per file; dependencies shared ----
        auto a1 = rm.get<AnimRes>(idle);
        auto a2 = rm.get<AnimRes>("media/anim/../anim/monster_idle.anim");
        CHECK(a1 == a2, "the same file gives the same object");
        CHECK(a1->ok() && a1->find("idle_slime"), "the idle clips load");
        auto d = rm.get<AnimRes>(door);
        auto fx1 = rm.get<AtlasRes>("media/atlas/fx.atlas");
        CHECK(fx1->ok(), "fx.atlas loads");
        bool idleHasFx = false, doorHasFx = false;
        for (const auto& e : a1->atlases().entries) idleHasFx |= e.atlas == &fx1->atlas();
        for (const auto& e : d->atlases().entries) doorHasFx |= e.atlas == &fx1->atlas();
        CHECK(idleHasFx && doorHasFx, "the idle and door clips share one fx atlas (it was loaded twice before)");
        const int texturesAfterBoth = ren.made;
        CHECK(texturesAfterBoth == 2, "fx.png and game.png uploaded once each, not once per clip file");
        CHECK(!fx1->pageTextures().empty() && fx1->pageTextures()[0] != kSpriteAtlasTexture, "the atlas page is a real texture");
        CHECK(std::string(a1->Type()) == "AnimRes" && a1->Name() == a1->path(), "a resource is a named, typed Object");

        // ---- failures: cached once, never null ----
        auto missing = rm.get<AtlasRes>("media/atlas/nope.atlas");
        CHECK(missing && !missing->ok() && !missing->error().empty(), "a missing file is a Failed resource with its error");
        CHECK(rm.get<AtlasRes>("media/atlas/nope.atlas") == missing, "the failure is cached (not read again)");

        // ---- the game's sprite atlas, provided ----
        toms::AtlasFile spriteAtlas;
        spriteAtlas.regions.push_back({});
        auto provided = rm.provide<AtlasRes>(ResourceManager::kSpriteAtlas, [&](AtlasRes& r) { r.setProvided(spriteAtlas); });
        CHECK(provided->ok() && provided->pageTextures().empty(), "a provided atlas draws with the sprite texture");
        const int pv = provided->version();
        auto again = rm.provide<AtlasRes>(ResourceManager::kSpriteAtlas, [&](AtlasRes& r) { r.setProvided(spriteAtlas); });
        CHECK(again == provided && provided->version() == pv + 1, "providing again updates the same object");
        rm.setPathMapper([](const std::string& p) {
            return p.size() >= 16 && p.compare(p.size() - 16, 16, "atlas/game.atlas") == 0 ? std::string(ResourceManager::kSpriteAtlas) : p;
        });
        CHECK(rm.get<AtlasRes>("media/atlas/game.atlas") == provided, "the path mapper sends game.atlas to the provided atlas");
        rm.setPathMapper(nullptr);

        // ---- memory: unused kept until the budget ----
        a1.reset(); a2.reset(); d.reset(); fx1.reset(); missing.reset(); again.reset();
        CHECK(rm.unusedBytes() > 0, "unused resources stay cached under the budget");
        CHECK(rm.get<AnimRes>(idle)->ok() && ren.made == texturesAfterBoth, "asking again costs nothing");
        rm.setBudget(0);
        CHECK(rm.unusedBytes() == 0, "a budget of 0 frees every unused resource");
        CHECK(ren.freed == ren.made, "their textures were freed with them");
        CHECK(rm.find<AtlasRes>(ResourceManager::kSpriteAtlas) == provided, "a provided resource is never freed");
        rm.setBudget(64u << 20);

        // ---- stamps ----
        auto before = rm.get<AtlasRes>("media/atlas/fx.atlas");   // base stamp
        const int made0 = ren.made;
        {
            ResourceStamp s(rm, "preview");
            auto inside = rm.get<AnimRes>(door);                   // new in the stamp; fx.atlas was already loaded
            auto held = rm.get<AtlasRes>("media/atlas/game.atlas"); // new, and still held when the stamp ends
            CHECK(inside->ok() && held->ok(), "loads inside a stamp");
            CHECK(ren.made == made0 + 1, "only game.png was new");
            inside.reset();
            {
                auto keepMe = held;
                rm.keep(keepMe);
            }
            held.reset();
        }
        CHECK(!rm.find<AnimRes>(door), "what the stamp loaded and nobody holds is freed when it ends");
        CHECK(rm.find<AtlasRes>("media/atlas/fx.atlas") == before, "what was loaded before the stamp stays");
        CHECK(rm.find<AtlasRes>("media/atlas/game.atlas") != nullptr, "keep() moved it out of the stamp");
        before.reset();

        // ---- report ----
        bool sawFx = false;
        for (const auto& i : rm.report()) sawFx |= i.type == "AtlasRes" && i.path == "media/atlas/fx.atlas";
        CHECK(sawFx && rm.totalBytes() > 0, "report lists what is loaded");
    }
    CHECK(ren.freed == ren.made, "every texture is freed when the manager goes");

    // ---- reloading in place ----
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path() / "toms_resource_test";
        fs::create_directories(dir, ec);
        copyFile("media/atlas/fx.atlas", dir / "fx.atlas");
        copyFile("media/atlas/fx.png", dir / "fx.png");
        const std::string atlasPath = (dir / "fx.atlas").generic_string();
        CountingRenderer r2;
        ResourceManager rm(&r2);
        auto atlas = rm.get<AtlasRes>(atlasPath);
        CHECK(atlas->ok() && atlas->find("dot"), "the copy loads");
        const int v0 = atlas->version(), c0 = atlas->contentVersion();
        CHECK(rm.checkFiles() == 0, "nothing changed: nothing reloaded");
        touch(dir / "fx.atlas", "\n");
        CHECK(rm.checkFiles() == 1, "a changed .atlas reloads");
        CHECK(rm.get<AtlasRes>(atlasPath) == atlas && atlas->version() == v0 + 1, "in place: same object, version + 1");
        touch(dir / "fx.png", std::string(1, '\0'));
        const int freedBefore = r2.freed;
        CHECK(rm.checkFiles() == 1 && atlas->contentVersion() > c0 + 1, "a changed page bumps the atlas's content version");
        CHECK(r2.freed == freedBefore + 1, "the old page texture was freed after the new one was up");
        writeText(dir / "fx.atlas", "size: 1, 1\n");   // a field before any page: parseAtlas fails
        touch(dir / "fx.atlas", "");
        rm.checkFiles();
        CHECK(atlas->ok() && atlas->find("dot"), "a broken reload keeps the old contents");

        // a renderer that goes first: textures still held are freed now, not later through a dead pointer
        rm.detachRenderer();
        CHECK(r2.freed == r2.made, "detachRenderer frees every texture");
        const int freedAfterDetach = r2.freed;
        atlas.reset();
        rm.releaseUnused();
        CHECK(r2.freed == freedAfterDetach, "nothing is freed through the renderer after it was detached");
        fs::remove_all(dir, ec);
    }

    // ---- the manifest: keys, the root folder, type checks, the missing handler, preload ----
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path() / "toms_manifest_test";
        fs::create_directories(dir / "atlas", ec);
        copyFile("media/atlas/fx.atlas", dir / "atlas" / "fx.atlas");
        copyFile("media/atlas/fx.png", dir / "atlas" / "fx.png");
        writeText(dir / "resources.json", R"({"resources": {
            "atlas.fx":   {"type": "atlas", "path": "atlas/fx.atlas", "preload": true},
            "atlas.gone": {"type": "atlas", "path": "atlas/gone.atlas"},
            "anim.wrong": {"type": "anim",  "path": "atlas/fx.png"}}})");
        ResourceManager rm;
        rm.setRoot(dir.generic_string());
        std::vector<std::string> missing;
        rm.setOnMissing([&](const std::string& name, const std::string&, const std::string&) { missing.push_back(name); });
        CHECK(rm.loadManifest("resources.json"), "the manifest loads from the root folder");
        CHECK(rm.has("atlas.fx") && rm.keys().size() == 3, "its keys are known");
        CHECK(rm.preload() == 0 && rm.find<AtlasRes>("atlas.fx") != nullptr, "preload loads the entries marked preload");
        auto byKey = rm.get<AtlasRes>("atlas.fx");
        auto byPath = rm.get<AtlasRes>("atlas/fx.atlas");
        CHECK(byKey->ok() && byKey == byPath, "a key and its path give the same object");
        CHECK(rm.resolve("atlas/fx.atlas") == rm.resolve("atlas.fx"), "a path is read under the root, like the key's");
        CHECK(missing.empty(), "nothing missing so far");
        auto gone = rm.get<AtlasRes>("atlas.gone");
        rm.get<AtlasRes>("atlas.gone");
        CHECK(!gone->ok() && missing.size() == 1 && missing[0] == "atlas.gone", "a missing file is told once, by its key");
        auto wrong = rm.get<AnimRes>("anim.wrong");
        CHECK(!wrong->ok() && missing.size() == 2, "a manifest entry read as its type: an atlas is not an anim");
        auto wrongType = rm.get<AnimRes>("atlas.fx");
        CHECK(!wrongType->ok() && wrongType->error().find("is a atlas") != std::string::npos, "asking for a key as another type says so");
        CHECK(!rm.get<AtlasRes>("no/such/file.atlas")->ok() && missing.size() == 4, "a path that is not there is told too");
        CHECK(!rm.loadManifest("nope.json") && missing.size() == 5, "a missing manifest is told");
        fs::remove_all(dir, ec);
    }

    // ---- the shipped manifest: every entry is there and loads as its type ----
    {
        ResourceManager rm;
        rm.setRoot("media");
        std::vector<std::string> missing;
        rm.setOnMissing([&](const std::string& name, const std::string& path, const std::string& error) {
            missing.push_back(name + " (" + path + "): " + error);
        });
        CHECK(rm.loadManifest("resources.json"), "assets/media/resources.json loads");
        CHECK(!rm.keys().empty(), "it names some resources");
        for (const std::string& k : rm.keys()) {
            const auto* e = rm.entry(k);
            CHECK(e && toms::vfsExists(rm.resolve(k)), ("the manifest's file is there: " + k).c_str());
            const std::string t = e ? e->type : std::string();
            CHECK(t == "texture" || t == "atlas" || t == "anim" || t == "particle" || t == "font" || t == "json",
                  ("a known type: " + k + " (" + t + ")").c_str());
        }
        rm.preload();
        for (const std::string& k : rm.keys())
            if (rm.entry(k)->type == "atlas") rm.get<AtlasRes>(k);
            else if (rm.entry(k)->type == "anim") rm.get<AnimRes>(k);
        for (const std::string& m : missing) std::printf("  missing: %s\n", m.c_str());
        CHECK(missing.empty(), "every shipped manifest entry loads");
    }

    // ---- UTF-8 paths (a non-ASCII folder) ----
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path() / toms::vfsPath("toms_資源_test");
        fs::create_directories(dir, ec);
        copyFile("media/atlas/fx.atlas", dir / "fx.atlas");
        copyFile("media/atlas/fx.png", dir / "fx.png");
        ResourceManager rm;
        auto atlas = rm.get<AtlasRes>(toms::vfsUtf8(dir / "fx.atlas"));
        CHECK(atlas->ok() && atlas->find("dot"), "a UTF-8 path with Chinese letters opens");
        fs::remove_all(dir, ec);
    }

    std::printf("resource_test: %s (%d failure(s))\n", g_fail ? "FAILED" : "passed", g_fail);
    return g_fail ? 1 : 0;
}
