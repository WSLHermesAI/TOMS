// game.cpp — implementation of Game.
#include "game.h"
#include <cstdio>    // std::snprintf (title screen's play-time column)
#include "node.h"   // 2D scene-graph Node (parent/child + local/world transform)
#include "scene.h"  // render binding: GameObject / SpriteNode / TextNode / FullScreenSplash

// TextNode draws through Game's font; bind it once to the live Game instance.
namespace {
Game* g_textGame = nullptr;
// Substitutes one "{key}" placeholder in a locale_.tr() string with `val` -- the same
// pattern already used for "{slot}" in the title-screen confirm dialog, generalized so
// combat-log/toast strings with an embedded dynamic value don't each hand-roll find/replace.
std::string trParam(std::string s, const std::string& key, const std::string& val) {
    std::string token = "{" + key + "}";
    size_t p = s.find(token);
    if (p != std::string::npos) s.replace(p, token.size(), val);
    return s;
}
}
void toms_TextNodeDraw(const std::string& s, float x, float y, float sz, const float* t) {
    if (g_textGame) g_textGame->drawTextPublic(s, x, y, sz, t);
}
namespace toms { TextNode::DrawFn TextNode::Draw = ::toms_TextNodeDraw; }
#ifdef __EMSCRIPTEN__
  #ifdef WEBGPU
    #include "renderer_webgpu.h"   // WebGPU backend (browser build only)
  #else
    #include "renderer_webgl.h"   // WebGL2 backend (browser build only)
  #endif
#else
#include "renderer.h"         // Vulkan backend (desktop build only)
#endif
#ifndef __EMSCRIPTEN__
#include "vk_util.h"   // Vulkan helpers — desktop build only
#endif
#include "event_bus.h"   // toms::EventBus — see Game::resolveCombatRound / Game::movePlayer
#include "condition.h"   // toms::evaluate / toms::ConditionContext — see GameConditionContext below
#include "story_controller.h" // toms::advanceStoryBeat / setStoryFlag / hasStoryFlag
#include "encounter.h"   // toms::resolveEncounterKind / toms::EncounterKind — see Game::movePlayer
#ifndef __EMSCRIPTEN__
#include "imgui.h"       // core ImGui API only -- backend plumbing lives in imgui_layer.h/.cpp
#endif

// GameConditionContext adapts live Player + MetaSaveData state to the engine-level
// toms::ConditionContext interface (condition.h), so door/key gating and dialogue `requires`
// gating both go through the one shared evaluator instead of bespoke checks (Milestone 3 —
// see docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §9). Built from already-public Player
// fields + a MetaSaveData reference passed in by the Game methods that use it (movePlayer,
// enterNode), so it needs no friendship/new public API on Game.
namespace {
class GameConditionContext : public toms::ConditionContext {
public:
    GameConditionContext(const Player& p, const toms::MetaSaveData& meta,
                          const std::map<std::string, toms::MissionTracker>& missions)
        : p_(p), meta_(meta), missions_(missions) {}
    bool storyFlagSet(const std::string& flag) const override { return toms::hasStoryFlag(meta_, flag); }
    int  storyBeat() const override { return meta_.currentBeat; }
    bool itemHeld(const std::string& itemId, int count) const override {
        if (itemId == "key_yellow") return p_.key_yellow >= count;
        if (itemId == "key_blue")   return p_.key_blue   >= count;
        if (itemId == "key_red")    return p_.key_red    >= count;
        int c = 0; for (auto& s : p_.inv) if (s == itemId) c++;
        return c >= count;
    }
    int statValue(const std::string& stat) const override {
        if (stat == "atk")  return p_.atk;
        if (stat == "def")  return p_.def;
        if (stat == "hp")   return p_.hp;
        if (stat == "lv")   return p_.lv;
        if (stat == "gold") return p_.gold;
        if (stat == "exp")  return p_.exp;
        return 0;
    }
    // Milestone 4: real lookups against Game's live mission trackers (closes the stub these two
    // methods were in Milestone 3 — missionDefs_ may still be empty until Milestone 8 loads
    // content, but tracker *state* is real the moment a mission is started).
    bool missionComplete(const std::string& missionId) const override {
        auto it = missions_.find(missionId);
        return it != missions_.end() &&
               (it->second.state == toms::MissionState::Completed || it->second.state == toms::MissionState::Claimed);
    }
    bool missionActive(const std::string& missionId) const override {
        auto it = missions_.find(missionId);
        return it != missions_.end() && it->second.state == toms::MissionState::Active;
    }
    // Milestone 5: a stage counts as "cleared" once the player has reached it at least once
    // (see Game::loadStage's meta_.unlockedStages tracking) -- the simplest sensible definition
    // for this linear-climb game; whether a *replayed* floor should repopulate enemies is a
    // separate, still-open design question (architecture-doc §5.2, tracked in the roadmap's
    // Milestone 7 balance checklist), not decided here.
    bool stageCleared(const std::string& stageId) const override {
        return std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), stageId) != meta_.unlockedStages.end();
    }
private:
    const Player& p_;
    const toms::MetaSaveData& meta_;
    const std::map<std::string, toms::MissionTracker>& missions_;
};
} // namespace
#include <json.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <ctime>

// Robust JSON file load. NOTE: Emscripten's libc++ std::ifstream is unreliable
// for preloaded files (tellg reports the right size but read/>> return empty), so
// we use C stdio (fopen/fread) which reads preloaded data correctly, then json::parse.
static nlohmann::json readJsonFile(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) { fprintf(stderr, "[readJsonFile] cannot open %s\n", path.c_str()); return {}; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); fprintf(stderr, "[readJsonFile] empty %s\n", path.c_str()); return {}; }
    std::string buf((size_t)sz, '\0');
    size_t rd = fread(&buf[0], 1, (size_t)sz, fp);
    fclose(fp);
    if (rd == 0) { fprintf(stderr, "[readJsonFile] read 0 bytes %s\n", path.c_str()); return {}; }
    try { return nlohmann::json::parse(buf); }
    catch (const std::exception& e) {
        fprintf(stderr, "[readJsonFile] parse error %s: %s\n", path.c_str(), e.what());
        return {};
    }
}
#include <algorithm>
#include <filesystem>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static const float* C4(float a,float b,float c,float d){ static float v[4]; v[0]=a; v[1]=b; v[2]=c; v[3]=d; return v; }

// ---- sprite id order (atlas grid position; must match gen_textures manifest) ----
static const char* SPRITE_ORDER[] = {
    "floor","wall","stairs_up","stairs_down","door_yellow","door_blue","door_red",
    "gem_atk","gem_def","potion_red","potion_blue","coin",
    "key_yellow","key_blue","key_red",
    "player","slime","bat","golem","skeleton","wraith","demon","boss_demonlord",
    "npc_sorcerer","npc_villager","npc_princess","npc_king","npc_handmaiden",
    "exp_up","scroll"          // item icons (added for the inventory UI)
};
static const int N_SPRITES = 30;

bool Game::loadAssets(const std::string& assetDir) {
    dataDir = assetDir;
    // Create the backend renderer. Desktop = Vulkan; Emscripten = WebGL2 or WebGPU.
#ifndef __EMSCRIPTEN__
    ren = new Renderer();                  // Vulkan (Windows / Linux)
#else
  #ifdef WEBGPU
    ren = new WebGPURenderer();           // WebGPU (browser)
  #else
    ren = new WebGLRenderer();            // WebGL2 (browser, default)
  #endif
#endif
    ren->init(1280, 720);   // 16:9; window is locked to this aspect (see VulkanContext::init)
    // load sprites into a single 32x32-uniform atlas (GRID_COLS x GRID_ROWS grid)
    const int SW = 32, SH = 32, COLS = 9, ROWS = 3;
    spriteGridCols = COLS;
    std::vector<std::vector<uint8_t>> layers;
    layers.reserve(N_SPRITES);
    for (int i = 0; i < N_SPRITES; i++) {
        std::string name = SPRITE_ORDER[i];
        std::string path = assetDir + "/sprites/" + name + ".png";
        int w,h,ch; unsigned char* d = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!d) { std::fprintf(stderr, "load fail %s\n", path.c_str()); return false; }
        layers.emplace_back(d, d + w*h*4);
        idToLayer[name] = i;
        stbi_image_free(d);
    }
    // upload full sprite atlas (backend packs the grid + uploads)
    ren->loadSprites(layers, SW, SH);
    // ---- build the font atlas ----
    // Runtime atlas build. Desktop: stb_truetype from bundled TTFs (wqy-zenhei for Han,
    // Noto Sans JP/KR paired in for Kana/Hangul -- see Font::buildFromFiles). Web: an
    // offscreen Canvas 2D context using the browser's own system fonts (see
    // Font::buildFromCanvas) -- no TTF file ships in the .data bundle at all, since every
    // major browser already carries full CJK/Latin/etc. font coverage. Both produce the
    // identical atlas layout/metric convention, so everything below this block (and all
    // of Game::drawText()/measureText()) is unaware of which backend built it.
    {
        // collect every codepoint used by shipped JSON + HUD labels
        std::vector<std::string> jsonFiles;
        for (auto& p : std::filesystem::recursive_directory_iterator(dataDir + "/../data"))
            if (p.path().extension() == ".json") jsonFiles.push_back(p.path().string());
        // Comprehensive pre-bake: every glyph the game can display (dialogue +
        // store + HUD + gamepad labels) so drawText never triggers a runtime
        // glyph bake -> no mid-frame font-texture re-upload -> no WebGL abort.
        std::string hud = " #'()*+,-./0123456789:<>@ABCDEFGHIKLOPRSTUVXYZ^_abcdefghijklmnopqrstuvwxy·—…→▶、。「」『』一上下不世並中主久之也了予亡交人什仇仍他付以件份但低住你使來侍便保信們倒值做傳價先入全公共兵具再凋凡出切列別利到則前副力加動勝化匙十升卡印即卷去反取受口可史右司吃合同向否吧吸吾告周命和咒咕唯商啟嘶嚕囚回國圖土在地堅塔墓外大奪女她如姆字存學它守安官定室宮家容寄寶封將對小少展層屬嵌巨巫已希師帶幣平年序店座廳廷弱強形影後徑得從復必怎怕思性怨怪恐恢恨惡意感懂懼成我戰所才打承把拉拯拳持接提揭援損撲撼擇擊擋攀收攻放效救敗教敢散敵數方於明星是晶暗書曾最會有望本村林枚果枯格森樓標機橫檻歐正此歸殿毅每民水永求決沃沉泉法注洞活流消淨深源準滿災為焉煉燃營物獲獻王玩現瓶生用留疊白的目直看真睡知石碎確示祝神祭禁禍福禦穩穴窟立章第等糊紅純紙級終給經緣繼續翅習老者而聖能自與莉莊萊萎著藍藏藥處蝙蝠血行被要見親角解言託記試話該語謝證護變讓買購贈走起足路跳踏身軍軸輕輪迎送透這逝進遇道達選還那重量金錢鍵鑰鑲長門閉開閣關防降陛除雙離零需露靈頂須頭願驗骷髏體高鬥魂魔麼黃黎黑點！（），：；？";
        std::vector<uint32_t> cps = Font::collectFromFiles(jsonFiles, hud);
        font_ = std::make_shared<Font>("game-font");
#ifdef __EMSCRIPTEN__
        if (!font_->buildFromCanvas(cps, 32, 24)) {
            std::fprintf(stderr, "font build failed (canvas)\n"); return false;
        }
#else
        std::string ttf = assetDir + "/wqy-zenhei.ttc";
        if (const char* e = std::getenv("TOMS_FONT")) ttf = e;
        else if (!std::filesystem::exists(ttf))
            ttf = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc";
        std::vector<std::string> fontFiles = {
            ttf,
            assetDir + "/fonts/NotoSansJP-Regular.ttf",
            assetDir + "/fonts/NotoSansKR-Regular.ttf",
        };
        if (!font_->buildFromFiles(fontFiles, cps, 32, 24)) {
            std::fprintf(stderr, "font build failed: %s\n", ttf.c_str()); return false;
        }
#endif
        ren->loadFont(font_->atlas(), font_->atlasW(), font_->atlasH());
        fontW = (int)font_->atlasW(); fontH = (int)font_->atlasH();
        fontCols = 32; fontCell = 32;
        for (uint32_t cp : cps) {
            const std::array<float,4>* uv = font_->uv(cp);
            if (uv) fontMap[cp] = *uv;
        }
    }

    // load enemy templates
    nlohmann::json ej = readJsonFile(assetDir+"/../data/enemies.json");
    for (auto& [k,v] : ej.items()) enemyTpl[k] = v;
    // load item definitions
    nlohmann::json ij = readJsonFile(assetDir+"/../data/items.json");
    for (auto& [k,v] : ij.items()) itemDefs[k] = v;
    // load store definitions (data/store.json) -> unlock stage + items + cost rule
    loadStore(assetDir);
    // Milestone 8: load the Equipment System's content (schema/logic built in Milestone 6 --
    // equipment_system.h -- but equipmentDefs_ stayed empty, and nothing was ever purchasable,
    // until this content-authoring pass wired it into the store below).
    {
        nlohmann::json eqj = readJsonFile(assetDir + "/../data/equipment.json");
        for (auto& [k, v] : eqj.items()) equipmentDefs_[k] = toms::equipmentFromJson(v);
    }
    // Milestone 8: load the Mission System's content (schema/logic built in Milestone 4 --
    // mission_system.h -- but missionDefs_ stayed empty until this content-authoring pass).
    {
        nlohmann::json mj = readJsonFile(assetDir + "/../data/missions.json");
        for (auto& [k, v] : mj.items()) missionDefs_[k] = toms::missionDefinitionFromJson(v);
    }
    // init player
    pl.maxhp = 120; pl.hp = 120; pl.atk = 12; pl.def = 4; pl.gold = 0; pl.exp = 0; pl.lv = 1;
    pl.inv = {"potion_red", "potion_blue", "exp_up"};

    // ---- Title phase boot ----
    // Persisted preferences (language, slot count) + the string table, then show the title.
    // modalActive() includes the title, so the world main.cpp loads next sits inert behind it
    // until the player chooses New Game or Continue. data/text.json is under data/, which the
    // TTF codepoint collector above already globs, so its glyphs are in the atlas before this
    // runs -- on both desktop and web, since the atlas is now built the same way on both.
    {
        toms::ensureSaveDir(toms::defaultSaveDir());
        bool haveSettings = false;
        settings_ = toms::loadGameSettings(toms::defaultSaveDir() + "/settings.json", &haveSettings);
        locale_.loadFromFile(dataDir + "/../data/text.json");
        locale_.setLanguage(settings_.language);
        title_.setSlotCount(settings_.slotCount);
        title_.setLanguageCount(locale_.languageCount());
        title_.setLanguageIndex(locale_.languageIndex());
        title_.open();
        refreshSlots();
        if (!haveSettings) applyLanguage();   // write defaults once, so the file exists
    }
    // init SFX subsystem (no-op if no audio device/context; headless-safe).
    audio.init(assetDir + "/sfx");
    g_textGame = this;   // bind TextNode text drawing to this instance
    wireMissionEvents(); // Milestone 4: subscribe mission-progress handlers once per session
    rollDailyMissions(); // Milestone 5: daily-mission reset check, once per session start
    return true;
}

int Game::spriteLayer(const std::string& id) const {
    auto it = idToLayer.find(id);
    return it == idToLayer.end() ? 0 : it->second;
}

// UV rect for a sprite in the uniform grid atlas.
void Game::spriteUV(int layer, float uv[4]) const {
    int cols = spriteGridCols;
    int gx = layer % cols, gy = layer / cols;
    float u0 = (float)gx / cols, v0 = (float)gy / (float)((N_SPRITES + cols - 1) / cols);
    float u1 = (float)(gx + 1) / cols, v1 = (float)(gy + 1) / (float)((N_SPRITES + cols - 1) / cols);
    uv[0]=u0; uv[1]=v0; uv[2]=u1; uv[3]=v1;
}

Quad Game::spriteQuad(float x, float y, float w, float h, int layer, const float tint[4]) {
    Quad q; q.rect[0]=x; q.rect[1]=y; q.rect[2]=w; q.rect[3]=h;
    spriteUV(layer, q.uv);
    q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
    return q;
}

void Game::loadStage(const std::string& id) {
    curStage = id;
    // Resolve the stage JSON. Data ids in connect.up/down use "stage_02" (underscore)
    // while the shipped files are named "stage02.json" (no underscore) — and the
    // initial load uses "stage01". Normalize so both forms resolve instead of
    // opening a non-existent path (which makes ifstream fail -> parse throw -> crash
    // when the player steps on stairs / a warp tile).
    std::string path = dataDir + "/../data/stages/" + id + ".json";
    if (!std::filesystem::exists(path)) {
        std::string noUs = id;
        noUs.erase(std::remove(noUs.begin(), noUs.end(), '_'), noUs.end());
        std::string alt = dataDir + "/../data/stages/" + noUs + ".json";
        if (std::filesystem::exists(alt)) path = alt;
    }
    st = parseStage(path, locale_);
    // Milestone 7: parseStage() rebuilds every entity fresh from JSON on every call (stairs,
    // Stage Select, etc.), so re-apply any previously-persisted Defeated/Collected status here --
    // otherwise a monster the player already beat or an item they already picked up would come
    // back the next time this floor loads, contradicting the "cleared floors stay cleared"
    // decision. Doors are intentionally not covered (see entityStatus_'s declaration in game.h).
    for (auto& e : st.entities) {
        bool isMonster = e.kind.rfind("monster:", 0) == 0;
        bool isItem = e.kind.rfind("item:", 0) == 0;
        if (!isMonster && !isItem) continue;
        toms::EntityStatus want = isMonster ? toms::EntityStatus::Defeated : toms::EntityStatus::Collected;
        toms::EntityStatus have = toms::getEntityStatus(entityStatus_, toms::entityStatusKey(st.id, e.x, e.y));
        if (have == want) {
            e.consumed = true;
            st.tiles[e.y][e.x] = '.';
        }
    }
    // Milestone 3: entering a floor for the first time advances the main-story beat — this is
    // exactly what connect.up already does today (GAME_DESIGN_DOCUMENT.md §5's 1:1 floor<->beat
    // mapping); now it's also written into persisted story state instead of being purely
    // implicit in "which floor am I standing on."
    toms::advanceStoryBeat(meta_, st.index);
    // Milestone 5: reaching a floor unlocks it as a Stage Select entry (architecture-doc §10 /
    // §5.2's stageCleared) -- every floor the player has ever reached becomes individually
    // re-selectable from the hub. Uses st.id (the parsed, canonical id) rather than the raw
    // `id` argument, since the two can differ by underscore normalization above.
    if (std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), st.id) == meta_.unlockedStages.end())
        meta_.unlockedStages.push_back(st.id);
    // derive total stage count from the stages directory (max index)
    totalStages = 1;
    std::string dir = dataDir + "/../data/stages/";
    if (std::filesystem::exists(dir)) {
        for (auto& e : std::filesystem::directory_iterator(dir)) {
            try {
                nlohmann::json j = readJsonFile(e.path().string());
                int idx = j.value("index", 0);
                if (idx > totalStages) totalStages = idx;
            } catch (...) {}
        }
    }
    // store unlock: when entering the configured unlock stage for the first time,
    // mark the shop as unlocked and pop a one-time "shop unlocked!" dialog.
    if (st.index >= storeUnlockStage_ && !storeUnlocked_) {
        storeUnlocked_ = true;
        storeUnlockDlg = true;
    }

    // place player at '@' or default
    pl.x = 1; pl.y = (int)st.height - 2;
    for (auto& e : st.entities)
        if (e.raw == "@") { pl.x = e.x; pl.y = e.y; }
}

// Proper UTF-8 decode -> code points, then draw each glyph from font atlas.
static std::u32string utf8_to_utf32(const std::string& s) {
    std::u32string out; size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { i++; continue; }
        for (int k = 1; k < len && i + k < s.size(); k++)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp); i += len;
    }
    return out;
}

void Game::drawText(const std::string& s, float x, float y, float size, const float tint[4]) {
    float cx = x;
    std::u32string cps = utf8_to_utf32(s);
    bool fontChanged = false;
    // First glyph's top bearing establishes the baseline reference (FM79979
    // FreetypeGlypth.cpp L145: YOffset = -firstGlyph->Offset.y). All glyphs are
    // then placed top-aligned to that, at their NATURAL size — no cell-centering,
    // no shrink. This is exactly how the reference lays text out.
    float firstTop = 0;
    const std::array<float,4>* m0 = nullptr;
    if (font_ && !cps.empty()) {
        m0 = font_->glyphMetrics(cps[0]);
        if (m0) firstTop = (*m0)[1];   // topBearing
    }
    for (uint32_t cp : cps) {
        auto it = fontMap.find(cp);
        if (it == fontMap.end()) {
            // realtime fallback: try to pull the glyph from the primary font or a
            // system font and bake it into the atlas on the fly.
            if (font_ && font_->ensure(cp)) {
                const std::array<float,4>* uv = font_->uv(cp);
                if (uv) {
                    fontMap[cp] = *uv; it = fontMap.find(cp); fontChanged = true;
                    if (cp == cps[0]) { m0 = font_->glyphMetrics(cp); if (m0) firstTop = (*m0)[1]; }
                }
            }
        }
        if (it == fontMap.end()) { cx += size; continue; }   // truly unknown glyph
        auto& uv = it->second;
        // Design metrics from the font are stored in ATLAS pixels (relative to the
        // bake cell, cell_). To place the quad at the requested `size` we must
        // scale them up to display pixels: scale = size / cell_. This is exactly
        // what the reference does (its Size.x/Size.y are at m_iFontSize, then the
        // whole vertex buffer is scaled by m_fScale). Without this, every glyph
        // renders at cell_ px instead of `size`, and the per-glyph topBearing (which
        // differs for e.g. 'e' vs 'h') shifts glyphs by the wrong amount -> they
        // fall off the baseline and clip.
        float leftB = 0, topB = 0, sx = size, sy = size;
        float s = (font_ ? size / (float)font_->cellSize() : 1.0f);
        if (font_) {
            if (auto* m = font_->glyphMetrics(cp)) {
                leftB = (*m)[0]*s; topB = (*m)[1]*s; sx = (*m)[2]*s; sy = (*m)[3]*s;
            }
        }
        float qw = sx, qh = sy;                 // natural glyph size (display px)
        // Vertical placement matches FM79979 RenderFont: vertex.y = YOffset +
        // glyphOffset.y = -firstOffset.y + glyphOffset.y. In our stored metrics
        // (glyphM_[1] = -top_stb = top_freetype = -Offset.y_ref) this is
        //   qy = y + (glyphM_[first][1] - glyphM_[cp][1]) * s
        // (NOT y - firstTop*s + glyphM_[cp][1]*s, which would negate the bearing).
        float qx = cx + leftB;                  // advance then place by bearing
        float qy = y + (firstTop*s) - topB;     // baseline-aligned (ref sign)
        Quad q; q.rect[0]=qx; q.rect[1]=qy; q.rect[2]=qw; q.rect[3]=qh;
        q.uv[0]=uv[0]; q.uv[1]=uv[1]; q.uv[2]=uv[2]; q.uv[3]=uv[3];
        q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
        ren->drawText(q);
        cx += leftB + sx;                       // reference advance = Offset.x + Size.x
    }
    // If any glyph was added to the atlas this frame, re-upload it so it shows up.
    if (fontChanged && font_) {
        ren->updateFont(font_->atlas(), font_->atlasW(), font_->atlasH());
    }
}

// Public wrapper so TextNode (render-bound text) can draw through Game's font.
void Game::drawTextPublic(const std::string& s, float x, float y, float sz, const float* t) {
    drawText(s, x, y, sz, t);
}

float Game::measureText(const std::string& s, float size) const {
    float w = 0;
    std::u32string cps = utf8_to_utf32(s);
    float s2 = (font_ ? size / (float)font_->cellSize() : 1.0f);
    for (uint32_t cp : cps) {
        auto it = fontMap.find(cp);
        float leftB = 0, sx = (font_ ? (float)font_->cellSize() : 32.0f);
        if (font_ && it != fontMap.end()) {
            if (auto* m = font_->glyphMetrics(cp)) { leftB = (*m)[0]; sx = (*m)[2]; }
        }
        w += (leftB + sx) * s2;   // matches drawText advance (Offset.x + Size.x), scaled to display px
    }
    return w;
}

void Game::drawBar(float x, float y, float w, float h, float frac, const float col[4]) {
    // background
    Quad bg; bg.rect[0]=x; bg.rect[1]=y; bg.rect[2]=w; bg.rect[3]=h;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.2f;bg.tint[1]=0.2f;bg.tint[2]=0.25f;bg.tint[3]=1; ren->drawSprite(bg);
    float fw = w * std::max(0.0f, std::min(1.0f, frac));
    if (fw > 0) {
        Quad fg; fg.rect[0]=x; fg.rect[1]=y; fg.rect[2]=fw; fg.rect[3]=h;
        fg.uv[0]=0;fg.uv[1]=0;fg.uv[2]=1;fg.uv[3]=1; fg.solid=true;
        fg.tint[0]=col[0];fg.tint[1]=col[1];fg.tint[2]=col[2];fg.tint[3]=1; ren->drawSprite(fg);
    }
}

// Milestone 6: renders the Attack/Defense Power Bar -- five solid-colour zones (red/blue/green/
// blue/red, matching FIGHT_SCENE_DESIGN.md §2's symmetric layout) scaled to fit [x, x+w], plus a
// thin marker at `position` (in the bar's own [0, 2*redOuter] units). Same raw-Quad technique as
// drawBar()/the dialogue box background -- deliberately NOT ImGui, so the marker's frame timing
// is never at the mercy of ImGui's own frame pacing (architecture-doc §2.3 Phase 5).
void Game::drawPowerBar(float x, float y, float w, float h, const toms::PowerBarParams& bar, float position) {
    float span = 2.0f * bar.redOuter;
    if (span <= 0.0f) return;
    float c = bar.redOuter;
    auto seg = [&](float fromUnits, float toUnits, float r, float g, float b) {
        float x0 = x + std::max(0.0f, fromUnits) / span * w;
        float x1 = x + std::min(span, toUnits) / span * w;
        if (x1 <= x0) return;
        Quad q; q.rect[0]=x0; q.rect[1]=y; q.rect[2]=x1-x0; q.rect[3]=h;
        q.uv[0]=0;q.uv[1]=0;q.uv[2]=1;q.uv[3]=1; q.solid=true;
        q.tint[0]=r; q.tint[1]=g; q.tint[2]=b; q.tint[3]=1.0f;
        ren->drawSprite(q);
    };
    seg(0.0f,            c - bar.blueOuter, 0.75f, 0.25f, 0.25f);   // red (left)
    seg(c - bar.blueOuter, c - bar.greenHalf, 0.3f, 0.45f, 0.85f);  // blue (left)
    seg(c - bar.greenHalf, c + bar.greenHalf, 0.3f, 0.8f, 0.4f);    // green (center)
    seg(c + bar.greenHalf, c + bar.blueOuter, 0.3f, 0.45f, 0.85f);  // blue (right)
    seg(c + bar.blueOuter, span,              0.75f, 0.25f, 0.25f); // red (right)

    float mx = x + std::max(0.0f, std::min(span, position)) / span * w;
    Quad marker; marker.rect[0]=mx-2.0f; marker.rect[1]=y-6.0f; marker.rect[2]=4.0f; marker.rect[3]=h+12.0f;
    marker.uv[0]=0;marker.uv[1]=0;marker.uv[2]=1;marker.uv[3]=1; marker.solid=true;
    marker.tint[0]=1;marker.tint[1]=1;marker.tint[2]=1;marker.tint[3]=1;
    ren->drawSprite(marker);
}

// map a stage cell char to a sprite id (floor/wall/door/stairs)
static std::string cellSprite(char c) {
    switch (c) {
        case '#': return "wall";
        case 'y': return "door_yellow"; case 'b': return "door_blue"; case 'r': return "door_red";
        case 'U': return "stairs_up"; case 'D': return "stairs_down";
        default: return "floor";
    }
}
// entity id -> sprite id
static std::string entSprite(const std::string& id) {
    if (id == "slime") return "slime"; if (id=="bat") return "bat"; if (id=="golem") return "golem";
    if (id=="skeleton") return "skeleton"; if (id=="wraith") return "wraith"; if (id=="demon") return "demon";
    if (id=="demonlord_vorkath") return "boss_demonlord";
    if (id=="gem_atk") return "gem_atk"; if (id=="gem_def") return "gem_def";
    if (id=="potion_red") return "potion_red"; if (id=="potion_blue") return "potion_blue"; if (id=="coin") return "coin";
    if (id=="key_yellow") return "key_yellow"; if (id=="key_blue") return "key_blue"; if (id=="key_red") return "key_red";
    if (id=="villager") return "npc_villager"; if (id=="sorcerer") return "npc_sorcerer";
    if (id=="princess") return "npc_princess"; if (id=="king") return "npc_king"; if (id=="handmaiden") return "npc_handmaiden";
    return "floor";
}

void Game::draw() {
    ren->begin();
    // Title phase (Boot screen): drawn INSTEAD of the world, then nothing else. There is
    // nothing meaningful to show behind it yet, and skipping the world draw keeps the title's
    // layout independent of whatever stage happens to be loaded underneath it.
    if (title_.isOpen()) {
        ren->setNode(NODE_UNSPEC);
        drawTitleScreen();
        ren->end();
        return;
    }
    float W = (float)ren->width(), H = (float)ren->height();
    // Milestone 9: stage grids are no longer a fixed 13x11 -- Wilson's-algorithm mazes
    // grow with floor depth (see tools/gen_mazes.py), so the tile pixel size must shrink
    // to fit a bigger grid into the same fixed design canvas instead of overflowing it
    // (there's no camera/scroll system here -- see the drawing loop below, which still
    // draws every tile of the grid in one pass). oy=60/bottom margin=50 mirror the fixed
    // HUD text (top) and the story_note line (bottom) drawn around this grid, below.
    int gw = st.width, gh = st.height;
    float oy = 60.0f, bottomMargin = 50.0f;
    float ts = std::min(W / (float)gw, (H - oy - bottomMargin) / (float)gh);
    ts = std::min(ts, 48.0f);   // never bigger than the original fixed size (stage01 is
                                // the same 13x11 grid as before M9, so it renders
                                // identically to what the owner already visually confirmed)
    ts = std::max(ts, 20.0f);   // stay legible even for the largest generated floor
    float ox = (W - gw*ts)/2.0f;
    static const float white[4] = {1,1,1,1};
    float tint[4] = {1,1,1,1};

    const bool showStore = storeModal();
    // Milestone 7 bugfix: this used to fall back to scanning cs.log for the substring "倒下" to
    // keep the overlay open during the brief post-lose result-pause (cs.active and cs.won are
    // both already false by then). That substring also appears in the WIN message ("...敵人倒下
    //了！" -- the enemy fell down), so after a win was dismissed (cs.won reset to false) the
    // overlay would never actually close, since the old log text still matched. cs.resultPauseMs
    // is the actual, non-fragile signal for "a result was just resolved and needs its readability
    // beat" -- it's set to 300 by both resolveAttackRelease/resolveDefenseRelease and ticks down
    // in update() regardless of cs.active/cs.won (see update()'s own comment on that).
    const bool showBattle = !showStore && (hideMask & 1) == 0 && (cs.active || cs.won || cs.resultPauseMs > 0);
    const bool showTalk   = !showStore && !showBattle && (hideMask & 2) == 0 && inDialogue;
    const bool showInv    = !showStore && !showBattle && !showTalk && (hideMask & 4) == 0 && invOpen;
    const bool showWalk   = !showStore && !showBattle && !showTalk && !showInv;

    // ---- walking scene: STAGE + CHARACTER ----
    if (showWalk) {
        ren->setNode(NODE_STAGE);
        for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
            char c = st.at(x,y);
            int layer = spriteLayer(cellSprite(c));
            ren->drawSprite(spriteQuad(ox + x*ts, oy + y*ts, ts, ts, layer, white));
        }
        // Entity/player sprites were inset by a fixed 8px into their 48px tile before
        // Milestone 9; now that ts varies per stage, the inset scales with it (same
        // ~1/6 ratio, so this renders identically to before at ts=48).
        float inset = ts / 6.0f, sSize = ts - 2.0f * inset;
        for (auto& e : st.entities) {
            if (e.consumed) continue;
            int layer = spriteLayer(entSprite(e.id));
            ren->drawSprite(spriteQuad(ox + e.x*ts + inset, oy + e.y*ts + inset, sSize, sSize, layer, white));
        }
        ren->drawSprite(spriteQuad(ox + pl.x*ts + inset, oy + pl.y*ts + inset, sSize, sSize, spriteLayer("player"), white));

        ren->setNode(NODE_CHAR);
        drawText(locale_.tr("game.title") + " — " + st.name + " (" + std::to_string(st.index) + "/" + std::to_string(totalStages) + ")", 16, 16, 22, tint);
        float bx = 16, by = 44;
        drawBar(bx, by, 200, 14, (float)pl.hp/pl.maxhp, C4(0.9f,0.2f,0.2f,1));
        drawText("HP " + std::to_string(pl.hp) + "/" + std::to_string(pl.maxhp), bx+210, by, 18, tint);
        drawText("ATK " + std::to_string(pl.atk) + "  DEF " + std::to_string(pl.def) + "  LV " + std::to_string(pl.lv), bx, by+20, 18, tint);
        drawText("GOLD " + std::to_string(pl.gold) + "  EXP " + std::to_string(pl.exp) + "  " + locale_.tr("hud.keys") + " Y"+std::to_string(pl.key_yellow)+" B"+std::to_string(pl.key_blue)+" R"+std::to_string(pl.key_red) + "  " + locale_.tr("hud.items") + "x" + std::to_string(pl.inv.size()) + " (I)", bx, by+42, 16, tint);
        drawText(st.story_note, 16, H-30, 16, C4(0.8f,0.85f,1.0f,1));

        ren->setNode(NODE_STORE);
        if (!storeOpen) drawStoreIcon();
        if (!storeOpen && !inGameMenuOpen_) drawMenuIcon();
        storeBtnRects_.clear();
        drawStoreToast();
        drawGamepad();
        drawStylingSpikeBackdrop();
        if (stairsConfirmOpen_) drawStairsConfirmDialog();
        if (inGameMenuOpen_) drawInGameMenu();
        ren->end();
        return;
    }

    // ---- battle scene ----
    if (showBattle) {
        ren->setNode(NODE_BATTLE);
        drawFocusSplash();
        float cx = W/2 - 250;
        drawText(locale_.tr("battle.title") + " " + cs.enemy.name, cx, 120, 26, C4(1,0.6f,0.4f,1));
        ren->drawSprite(spriteQuad(cx, 170, 96, 96, spriteLayer("player"), white));
        ren->drawSprite(spriteQuad(cx+350, 170, 96, 96, spriteLayer(cs.enemy.boss?"boss_demonlord":entSprite(cs.enemy.id)), white));
        drawBar(cx, 280, 200, 16, (float)cs.playerHP/pl.maxhp, C4(0.3f,0.9f,0.4f,1));
        drawText(locale_.tr("battle.you") + " HP " + std::to_string(cs.playerHP), cx+210, 280, 18, tint);
        drawBar(cx+350, 280, 200, 16, (float)std::max(0,cs.enemyHP)/cs.enemy.hp, C4(0.9f,0.3f,0.3f,1));
        drawText(cs.enemy.name + " HP " + std::to_string(std::max(0,cs.enemyHP)), cx+560, 280, 18, tint);
        drawText(cs.log, cx, 320, 18, tint);
        // Milestone 6: the active Power Bar -- Attack Bar first each round, Defense Bar only if
        // the enemy survived it (MAIN_BATTLE_SCENE_DESIGN.md §2-3). Live while charging (marker
        // position recomputed from the accumulated hold duration every frame), frozen at
        // cs.lastPosition during the brief result-pause after a release.
        if (cs.active) {
            bool isDefensePhase = cs.phase == CombatState::Phase::AwaitDefensePress ||
                                   cs.phase == CombatState::Phase::DefenseCharging ||
                                   cs.phase == CombatState::Phase::DefenseResultPause;
            toms::PowerBarParams bar = isDefensePhase ? toms::effectiveDefenseBar(equipped_, equipmentDefs_)
                                                       : toms::effectiveAttackBar(equipped_, equipmentDefs_);
            float pos = cs.charging ? toms::simulatePosition(bar, cs.chargeMs / 1000.0f) : cs.lastPosition;
            drawText(isDefensePhase ? locale_.tr("battle.defense_prompt") : locale_.tr("battle.attack_prompt"),
                      cx, 345, 15, C4(0.9f, 0.9f, 1.0f, 1));
            drawPowerBar(cx, 365, 500, 22, bar, pos);
            // On-canvas action button: the browser build has no keyboard, so without this there is
            // nothing in the battle scene to press -- the power bar swept forever and the fight
            // never resolved. Press and hold it to charge, release to strike (identical to holding
            // Enter/Space; handleTouch treats the whole scene as the same surface).
            // Glyph advances are now tight/proportional (see Font::buildFromFiles), same as
            // desktop, so a longer translated label just measures wider instead of overflowing a
            // fixed cell grid -- no per-language length cap needed here any more.
            float bW = 400, bH = 56, bX = cx + (500 - bW) * 0.5f, bY = 402;
            combatBtnRect_[0] = (int)bX; combatBtnRect_[1] = (int)bY;
            combatBtnRect_[2] = (int)bW; combatBtnRect_[3] = (int)bH;
            bool held = cs.charging;
            Quad cb; cb.rect[0]=bX; cb.rect[1]=bY; cb.rect[2]=bW; cb.rect[3]=bH;
            cb.uv[0]=0; cb.uv[1]=0; cb.uv[2]=1; cb.uv[3]=1; cb.solid=true;
            if (held) { cb.tint[0]=0.85f; cb.tint[1]=0.62f; cb.tint[2]=0.20f; cb.tint[3]=1; }
            else      { cb.tint[0]=0.24f; cb.tint[1]=0.32f; cb.tint[2]=0.46f; cb.tint[3]=1; }
            ren->drawSprite(cb);
            std::string btnLabel;
            if (held)                btnLabel = locale_.tr("battle.btn_charging");
            else if (isDefensePhase) btnLabel = locale_.tr("battle.btn_hold_defense");
            else                     btnLabel = locale_.tr("battle.btn_hold_attack");
            drawText(btnLabel, bX + 44, bY + 18, 16, C4(1,1,1,1));
        } else {
            drawText(locale_.tr("battle.continue_hint"), cx, 350, 16, C4(1,1,0.6f,1));
        }
        drawStylingSpikeBackdrop();
        ren->end();
        return;
    }

    // ---- dialogue scene ----
    if (showTalk) {
        ren->setNode(NODE_TALK);
        drawFocusSplash();
        Quad box; box.rect[0]=40; box.rect[1]=H-200; box.rect[2]=W-80; box.rect[3]=170;
        box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1; box.solid=true;
        box.tint[0]=0.1f;box.tint[1]=0.12f;box.tint[2]=0.2f;box.tint[3]=0.95f; ren->drawSprite(box);
        std::string txt = locale_.field(dlgData["nodes"][dlgNode]["text"]);
        drawText(txt, 60, H-180, 20, tint);
        for (size_t i = 0; i < dlgChoices.size(); i++) {
            float ty = H-140 + (float)i*24;
            if ((int)i == dlgSel) drawText("▶ " + dlgChoices[i].label, 60, ty, 18, C4(1,1.0f,0.6f,1));
            else                   drawText("  " + dlgChoices[i].label, 60, ty, 18, C4(1,0.9f,0.5f,1));
        }
        drawStylingSpikeBackdrop();
        ren->end();
        return;
    }

    // ---- inventory scene ----
    if (showInv) {
        ren->setNode(NODE_CHAR);
        drawInventory();
        drawStylingSpikeBackdrop();
        ren->end();
        return;
    }

    // ---- store scene ----
    ren->setNode(NODE_STORE);
    if (!storeOpen) drawStoreIcon();
    storeBtnRects_.clear();
    if (storeUnlockDlg) drawStoreUnlockDialog();
    else if (storeOpen) drawStoreUI();
    drawStoreToast();
    drawGamepad();

    drawStylingSpikeBackdrop();
    ren->end();
}

// ---- on-canvas virtual gamepad (web build, touch only) ----
// Single source of truth: same rects used for drawing AND hit-testing.
// Buffer space is 1024x768, y-down (matches the rest of draw()).
struct GPadBtn { int id; float x,y,w,h; const char* label; float col[4]; int dx,dy; };
static const GPadBtn GP[] = {
    {0, 104,510, 72,72, "^", 0.40f,0.45f,0.55f,0.70f, 0,-1}, // up
    {1, 104,626, 72,72, "v", 0.40f,0.45f,0.55f,0.70f, 0, 1}, // down
    {2,  20,568, 72,72, "<", 0.40f,0.45f,0.55f,0.70f,-1, 0}, // left
    {3, 188,568, 72,72, ">", 0.40f,0.45f,0.55f,0.70f, 1, 0}, // right
    {4, 880,626, 68,68, "A", 0.30f,0.70f,0.35f,0.80f, 0, 0}, // interact / use
    {5, 792,566, 64,64, "B", 0.75f,0.30f,0.30f,0.80f, 0, 0}, // drop
    {6, 880,526, 56,56, "I", 0.30f,0.35f,0.75f,0.80f, 0, 0}, // inventory
    {7,  20,490, 48,48, "P", 0.25f,0.25f,0.25f,0.85f, 0, 0}, // toggle: show/hide gamepad
};
static const int GP_N = 8;

void Game::drawGamepad() {
    // Hide the whole gamepad (including its P toggle) while a top-layer UI is
    // active (battle / dialogue / inventory / store). Those UIs are driven by
    // direct touch/keyboard on their own elements, so the on-canvas D-pad + A
    // would just clutter the screen and could be mis-tapped.
    if (modalActive() || cs.won) return;
    ren->setNode(NODE_CHAR);
    float t[4] = {1,1,1,1};
    // P toggle button (drawn in normal play; modals already returned above).
    {
        const GPadBtn& b = GP[7];
        Quad q; q.rect[0]=b.x; q.rect[1]=b.y; q.rect[2]=b.w; q.rect[3]=b.h;
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=b.col[0]; q.tint[1]=b.col[1]; q.tint[2]=b.col[2]; q.tint[3]=b.col[3];
        ren->drawSprite(q);
        drawText(b.label, b.x + b.w/2 - 9, b.y + b.h/2 - 13, 28, t);
    }
    if (!gpOn) return;                 // gamepad hidden: only the toggle remains
    for (int i = 0; i < 7; i++) {
        if ((i == 5 || i == 6) && !inventoryOpen()) continue;  // B/drop + I/close only show inside inventory
        const GPadBtn& b = GP[i];
        Quad q; q.rect[0]=b.x; q.rect[1]=b.y; q.rect[2]=b.w; q.rect[3]=b.h;
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=b.col[0]; q.tint[1]=b.col[1]; q.tint[2]=b.col[2]; q.tint[3]=b.col[3];
        ren->drawSprite(q);
        drawText(b.label, b.x + b.w/2 - 11, b.y + b.h/2 - 13, 30, t);
    }
}

void Game::handleTouch(float px, float py, int phase) {
    // Guard: ignore invalid coordinates (NaN from a zero-size canvas rect, or
    // out-of-range). Prevents bad state / out-of-bounds in the hit-test below.
    if (!(px == px) || !(py == py)) return;            // NaN check
    if (px < 0 || px > 1024 || py < 0 || py > 768) return;
    // Title phase: every tap goes to the title's own hit-testing (menu rows / save slots /
    // language rows / Back). Checked first — while it is up, it is the only interactive screen.
    if (title_.isOpen()) { if (phase == 0) titleClick(px, py); return; }
    // --- Store overlay: route ALL taps to storeClick. Store buttons (icon / buy / close)
    //     are NOT gamepad rects, so this must run BEFORE the gamepad hit-test below,
    //     otherwise taps on store UI hit `id<0` and are dropped. ---
    if (storeModal()) { if (phase == 0) storeClick(px, py); return; }
    // In-game menu (Save/Settings/Back to Title): swallow every tap while it is up, same
    // pattern as the store overlay above.
    if (inGameMenuOpen_) { if (phase == 0) inGameMenuClick(px, py); return; }
    // Normal play: a tap on the gear icon (top-right, left of the store icon) opens the
    // in-game menu; a tap on the store icon opens the shop.
    if (!modalActive() && phase == 0) {
        if (px >= menuIconRect_[0] && px <= menuIconRect_[0]+menuIconRect_[2] &&
            py >= menuIconRect_[1] && py <= menuIconRect_[1]+menuIconRect_[3]) {
            openInGameMenu();
            return;
        }
        storeClick(px, py);
        if (storeOpen) return;   // icon tapped -> store opened; consume this tap
    }
    int id = -1;
    for (int i = 0; i < GP_N; i++) {
        const GPadBtn& b = GP[i];
        if (px >= b.x && px <= b.x + b.w && py >= b.y && py <= b.y + b.h) { id = i; break; }
    }
    if (id == 7) { if (phase == 0) gpOn = !gpOn; return; }  // toggle show/hide
    // ---- Battle scene: press-and-hold is the only input that can drive the Power Bar, and on the
    // web build it is the only input there is (no keyboard). Deliberately handled this early:
    //  * the old block sat AFTER `if (phase == 2) return;` below, so the RELEASE was swallowed --
    //    the web build started charging on a tap and then hung with the bar sweeping forever
    //    ("stuck at battle scene");
    //  * it also sat after `if (id < 0) return;`, so any tap that missed the virtual d-pad rects
    //    (i.e. almost the entire battle scene, including the action button) was dropped.
    // cs.won is excluded here so the post-victory "(按任意鍵繼續)" pause is still dismissed by the
    // tap-anywhere branch further down.
    if (cs.active && !cs.won && (phase == 0 || phase == 2)) {
        if (phase == 0) battleChargeStart();
        else            battleChargeRelease();
        return;
    }
    // Milestone 9 polish: releasing a d-pad button stops "keep moving while held" (see
    // setMoveHeldX/Y's declaration in game.h). Checked here, before the generic phase==2
    // return right below (which would otherwise swallow it), and unconditionally regardless
    // of what else might be open -- harmless when nothing is currently held, and this is the
    // one case where a release must never be dropped (an un-cleared held direction would
    // otherwise keep stepping forever).
    if (id >= 0 && id <= 3 && phase == 2) { stopMoveHeld(); return; }
    if (phase == 2) return;                 // touchend on a game button: nothing
    // Victory screen: tap anywhere to dismiss (cs.won is set on win and must be cleared
    // or the combat overlay keeps painting forever -> "stuck after defeating enemy").
    if (cs.won && phase == 0) { dismissVictory(); return; }
    // Milestone 9: stairs confirm -- Yes/No buttons aren't gamepad rects, so `id` stays
    // -1 for a desktop mouse click on them; must be checked before `if (id < 0) return;`
    // below (the exact bug this fixes for dialogue too, right after this block).
    if (stairsConfirmOpen_) {
        if (phase == 0) {
            auto hit = [&](const int r[4]) { return px>=r[0] && px<=r[0]+r[2] && py>=r[1] && py<=r[1]+r[3]; };
            if (hit(stairsConfirmYesRect_)) { confirmStageTransition(); return; }
            if (hit(stairsConfirmNoRect_))  { cancelStageTransition(); return; }
        }
        return;
    }
    // HUD stats line (carries the "(I)" inventory indicator) — tap to open inventory.
    if (!inventoryOpen() && !inDialogue && !modalActive() && phase == 0) {
        if (py >= 74 && py <= 104 && px >= 16 && px <= 560) { toggleInventory(); return; }
    }
    if (inventoryOpen()) {
        if (phase == 0) {
            auto hit = [&](const int r[4]) {
                return px >= r[0] && px <= r[0] + r[2] && py >= r[1] && py <= r[1] + r[3];
            };
            for (size_t i = 0; i + 3 < invCardRects_.size(); i += 4) {
                int r[4] = { (int)invCardRects_[i+0], (int)invCardRects_[i+1], (int)invCardRects_[i+2], (int)invCardRects_[i+3] };
                if (hit(r)) { invSel = (int)(i / 4); return; }
            }
            if (hit(invUseRect_)) { invUseSelected(); return; }
            if (hit(invDropRect_)) { invDropSelected(); return; }
            if (hit(invCloseRect_)) { toggleInventory(); return; }
        }
        if (id >= 0) {
            const GPadBtn& b = GP[id];
            if (id <= 3) invMoveSel(b.dx, b.dy);
            else if (id == 4 && phase == 0) invUseSelected();
            else if (id == 5 && phase == 0) invDropSelected();
            else if (id == 6 && phase == 0) toggleInventory();
        }
        return;
    }
    // Milestone 9 bugfix: dialogue tap-to-select never actually worked from a desktop
    // mouse. `if (id < 0) return;` used to run BEFORE this block -- a click on a
    // dialogue choice line isn't inside any gamepad-button rect, so `id` was always -1
    // there and every such click was silently dropped before ever reaching this code.
    // Moved above that guard; the gamepad D-pad fallback below still needs `id`, which
    // is already computed further up, so nothing else about it changes.
    if (inDialogue) {                        // dialogue: tap a choice line to select+confirm
        int n = (int)dlgChoices.size();
        // Tap directly on a choice line (drawn at y = H-140 + i*24, x >= 60) selects & confirms it.
        if (phase == 0 && n > 0) {
            float W = (float)ren->width(), H = (float)ren->height();
            for (int i = 0; i < n; i++) {
                float ty = H - 140 + (float)i * 24;
                if (py >= ty - 12 && py <= ty + 12 && px >= 56 && px <= W - 56) {
                    dlgSel = i; chooseDialogue(i); return;
                }
            }
            // tap elsewhere on the dialogue box = advance to next (keep current selection)
            chooseDialogue(dlgSel); return;
        }
        // (gamepad D-pad still works too)
        if (id == 0 && phase == 0 && n > 0) dlgSel = (dlgSel - 1 + n) % n;   // up = prev choice
        else if (id == 1 && phase == 0 && n > 0) dlgSel = (dlgSel + 1) % n;  // down = next choice
        else if (id == 4 && phase == 0) chooseDialogue(dlgSel);               // A = select
        else if (id == 5 && phase == 0) inDialogue = false;                  // B = close
        return;
    }
    if (id < 0) return;
    // (The battle scene's hold-to-charge input is handled much earlier, before the generic
    // `phase == 2` return above -- see the comment there.)
    if (modalActive()) {                     // other modal (store handled above): block world input
        return;
    }
    const GPadBtn& b = GP[id];
    // Milestone 9 polish: press-and-hold now keeps stepping (setMoveHeldX/Y + update()'s
    // repeat timer) instead of exactly one tile per tap; the matching release is handled
    // above, before the generic phase==2 return. GP[0]/[1] are the Y axis (up/down), GP[2]/[3]
    // the X axis (left/right) -- each button only ever sets one axis (see GP[]'s own dx/dy).
    if (id <= 3 && phase == 0) {
        if (b.dy != 0) setMoveHeldY(b.dy); else setMoveHeldX(b.dx);
    }
    else if (id == 4 && phase == 0) interact();
}

void Game::applyItem(const std::string& id) {
    // Item definitions come from data/items.json (loaded in loadAssets).
    auto it = itemDefs.find(id);
    if (it == itemDefs.end()) { return; }
    const nlohmann::json& eff = it->second["effect"];
    if (eff.contains("hp"))    pl.hp    = std::min(pl.maxhp, pl.hp + (int)eff["hp"]);
    if (eff.contains("atk"))   pl.atk   += (int)eff["atk"];
    if (eff.contains("def"))   pl.def   += (int)eff["def"];
    if (eff.contains("gold"))  pl.gold  += (int)eff["gold"];
    if (eff.contains("key_yellow")) pl.key_yellow += (int)eff["key_yellow"];
    if (eff.contains("key_blue"))   pl.key_blue   += (int)eff["key_blue"];
    if (eff.contains("key_red"))    pl.key_red    += (int)eff["key_red"];
    if (eff.contains("exp")) {
        pl.exp += (int)eff["exp"];
        int need = pl.lv * 30;
        while (pl.exp >= need) {
            pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30;
            pushNotification(locale_.tr("battle.levelup") + std::to_string(pl.lv));
        }
    }
    if (eff.contains("warp")) {
        std::string dst = it->second.value("warp_to", std::string(""));
        if (!dst.empty() && std::filesystem::exists(dataDir + "/../data/stages/" + dst + ".json"))
            loadStage(dst);
    }
    audio.play("get_item");
}

// ---------- inventory UI ----------
void Game::toggleInventory() {
    invOpen = !invOpen;
    if (invSel >= (int)pl.inv.size()) invSel = (int)pl.inv.size() - 1;
    if (invSel < 0) invSel = 0;
}
void Game::invMoveSel(int dx, int dy) {
    if (!invOpen || pl.inv.empty()) return;
    int cols = 3;
    int n = (int)pl.inv.size();
    int r = invSel / cols, c = invSel % cols;
    c += dx; r += dy;
    if (c < 0) c = 0; if (c >= cols) c = cols - 1;
    if (r < 0) r = 0;
    int maxr = (n + cols - 1) / cols - 1;
    if (r > maxr) r = maxr;
    int idx = r * cols + c;
    if (idx >= n) idx = n - 1;
    invSel = idx;
}
bool Game::invUseSelected() {
    if (!invOpen || invSel < 0 || invSel >= (int)pl.inv.size()) return false;
    std::string id = pl.inv[invSel];
    applyItem(id);                 // applies effect (exp/warp/hp/atk/def/gold)
    pl.inv.erase(pl.inv.begin() + invSel);
    if (invSel >= (int)pl.inv.size()) invSel = (int)pl.inv.size() - 1;
    if (invSel < 0) invSel = 0;
    return true;
}
void Game::invDropSelected() {
    if (!invOpen || invSel < 0 || invSel >= (int)pl.inv.size()) return;
    pl.inv.erase(pl.inv.begin() + invSel);
    if (invSel >= (int)pl.inv.size()) invSel = (int)pl.inv.size() - 1;
    if (invSel < 0) invSel = 0;
}
int Game::spriteForItem(const std::string& id) const {
    auto it = itemDefs.find(id);
    if (it == itemDefs.end()) return 0;
    std::string sp = it->second.value("sprite", std::string("coin.png"));
    if (sp.size() > 4 && sp.substr(sp.size()-4) == ".png") sp = sp.substr(0, sp.size()-4);
    int layer = spriteLayer(sp);
    return layer;
}
std::string Game::itemName(const std::string& id) const {
    auto it = itemDefs.find(id);
    if (it == itemDefs.end() || !it->second.contains("name")) return id;
    std::string s = locale_.field(it->second["name"]);
    return s.empty() ? id : s;
}
std::string Game::itemDesc(const std::string& id) const {
    auto it = itemDefs.find(id);
    if (it == itemDefs.end() || !it->second.contains("desc")) return "";
    return locale_.field(it->second["desc"]);
}
void Game::drawFocusSplash() {
    if (std::getenv("TOMS_NOSPLASH")) return;  // TEST: toggle splash off to isolate bug
    float W = (float)ren->width(), H = (float)ren->height();
    // full-screen black splash (transparent 50%) so the player focuses on the
    // active modal scene (combat / dialogue / inventory).
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.8f; ren->drawSprite(dim);
}
// store modal uses a full-screen black splash at alpha 0.5 (per design) and is the
// topmost layer, drawn last in Game::draw(); so it sits above all other overlays.
void Game::drawStoreSplash() {
    if (std::getenv("TOMS_NOSPLASH")) return;
    float W = (float)ren->width(), H = (float)ren->height();
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.5f; ren->drawSprite(dim);
}

void Game::drawInventory() {
    if (!invOpen) {
        invCardRects_.clear();
        return;
    }

    float W = (float)ren->width(), H = (float)ren->height();
    drawFocusSplash();

    auto effectSummary = [&](const std::string& id) -> std::string {
        auto it = itemDefs.find(id);
        if (it == itemDefs.end() || !it->second.contains("effect")) return locale_.tr("inventory.no_effect");
        const nlohmann::json& eff = it->second["effect"];
        std::vector<std::string> parts;
        auto add = [&](const char* key, const char* label) {
            if (eff.contains(key)) {
                int v = eff[key].get<int>();
                parts.push_back(std::string(label) + (v >= 0 ? "+" : "") + std::to_string(v));
            }
        };
        add("str", "STR");
        add("def", "DEF");
        add("hp",  "HP");
        add("mp",  "MP");
        add("exp", "EXP");
        add("gold","Gold");
        if (eff.contains("warp")) parts.push_back("Warp");
        if (parts.empty()) parts.push_back(locale_.tr("inventory.no_effect"));
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) out += " • ";
            out += parts[i];
        }
        return out;
    };

    // Main modal panel
    float pw = std::min(960.0f, W - 40.0f);
    float ph = std::min(580.0f, H - 40.0f);
    float px = (W - pw) * 0.5f;
    float py = (H - ph) * 0.5f;

    Quad outer; outer.rect[0]=px; outer.rect[1]=py; outer.rect[2]=pw; outer.rect[3]=ph;
    outer.uv[0]=0; outer.uv[1]=0; outer.uv[2]=1; outer.uv[3]=1; outer.solid=true;
    outer.tint[0]=0.10f; outer.tint[1]=0.12f; outer.tint[2]=0.18f; outer.tint[3]=0.96f;
    ren->drawSprite(outer);

    Quad titleBar; titleBar.rect[0]=px; titleBar.rect[1]=py; titleBar.rect[2]=pw; titleBar.rect[3]=58;
    titleBar.uv[0]=0; titleBar.uv[1]=0; titleBar.uv[2]=1; titleBar.uv[3]=1; titleBar.solid=true;
    titleBar.tint[0]=0.14f; titleBar.tint[1]=0.17f; titleBar.tint[2]=0.25f; titleBar.tint[3]=1.0f;
    ren->drawSprite(titleBar);

    drawText(locale_.tr("inventory.title"), px + 22, py + 18, 26, C4(1,0.92f,0.55f,1));
    drawText(locale_.tr("inventory.hint"), px + 22, py + 38, 14, C4(0.82f,0.88f,1,1));

    const std::vector<std::string>& inv = pl.inv;
    int n = (int)inv.size();
    if (n <= 0) {
        Quad empty; empty.rect[0]=px+22; empty.rect[1]=py+80; empty.rect[2]=pw-44; empty.rect[3]=ph-102;
        empty.uv[0]=0; empty.uv[1]=0; empty.uv[2]=1; empty.uv[3]=1; empty.solid=true;
        empty.tint[0]=0.08f; empty.tint[1]=0.09f; empty.tint[2]=0.13f; empty.tint[3]=0.92f;
        ren->drawSprite(empty);
        drawText(locale_.tr("inventory.empty_title"), px+40, py+120, 22, C4(1,1,1,1));
        drawText(locale_.tr("inventory.empty_hint"), px+40, py+152, 16, C4(0.8f,0.85f,1,1));
        invCardRects_.clear();
        invCloseRect_[0] = (int)(px + pw - 120);
        invCloseRect_[1] = (int)(py + ph - 58);
        invCloseRect_[2] = 90;
        invCloseRect_[3] = 34;
        Quad cbtn; cbtn.rect[0]=invCloseRect_[0]; cbtn.rect[1]=invCloseRect_[1]; cbtn.rect[2]=invCloseRect_[2]; cbtn.rect[3]=invCloseRect_[3];
        cbtn.uv[0]=0; cbtn.uv[1]=0; cbtn.uv[2]=1; cbtn.uv[3]=1; cbtn.solid=true;
        cbtn.tint[0]=0.5f; cbtn.tint[1]=0.2f; cbtn.tint[2]=0.2f; cbtn.tint[3]=1;
        ren->drawSprite(cbtn);
        drawText(locale_.tr("inventory.close"), invCloseRect_[0] + 22, invCloseRect_[1] + 9, 16, C4(1,1,1,1));
        return;
    }

    // Layout close to the demo: left item list, right detail panel + action buttons.
    float leftX = px + 22, leftY = py + 80;
    float leftW = pw * 0.58f - 30.0f;
    float leftH = ph - 104.0f;
    float rightX = leftX + leftW + 24.0f;
    float rightY = leftY;
    float rightW = pw - (rightX - px) - 22.0f;
    float rightH = leftH;

    Quad lpanel; lpanel.rect[0]=leftX; lpanel.rect[1]=leftY; lpanel.rect[2]=leftW; lpanel.rect[3]=leftH;
    lpanel.uv[0]=0; lpanel.uv[1]=0; lpanel.uv[2]=1; lpanel.uv[3]=1; lpanel.solid=true;
    lpanel.tint[0]=0.08f; lpanel.tint[1]=0.10f; lpanel.tint[2]=0.15f; lpanel.tint[3]=0.96f; ren->drawSprite(lpanel);
    Quad rpanel; rpanel.rect[0]=rightX; rpanel.rect[1]=rightY; rpanel.rect[2]=rightW; rpanel.rect[3]=rightH;
    rpanel.uv[0]=0; rpanel.uv[1]=0; rpanel.uv[2]=1; rpanel.uv[3]=1; rpanel.solid=true;
    rpanel.tint[0]=0.09f; rpanel.tint[1]=0.11f; rpanel.tint[2]=0.18f; rpanel.tint[3]=0.98f; ren->drawSprite(rpanel);

    // Left list of items
    invCardRects_.clear();
    const float cardH = 66.0f;
    const float gap = 10.0f;
    const float cardW = leftW - 20.0f;
    const float sx = leftX + 10.0f;
    float sy = leftY + 10.0f;
    for (int i = 0; i < n; ++i) {
        const std::string& id = inv[i];
        float cy = sy + i * (cardH + gap);
        if (cy + cardH > leftY + leftH - 10.0f) break;   // fixed demo-style window
        invCardRects_.push_back(sx);
        invCardRects_.push_back(cy);
        invCardRects_.push_back(cardW);
        invCardRects_.push_back(cardH);
        bool sel = (i == invSel);
        Quad card; card.rect[0]=sx; card.rect[1]=cy; card.rect[2]=cardW; card.rect[3]=cardH;
        card.uv[0]=0; card.uv[1]=0; card.uv[2]=1; card.uv[3]=1; card.solid=true;
        if (sel) { card.tint[0]=0.22f; card.tint[1]=0.25f; card.tint[2]=0.38f; card.tint[3]=1; }
        else     { card.tint[0]=0.14f; card.tint[1]=0.16f; card.tint[2]=0.24f; card.tint[3]=0.98f; }
        ren->drawSprite(card);
        if (sel) {
            Quad hi; hi.rect[0]=sx-2; hi.rect[1]=cy-2; hi.rect[2]=cardW+4; hi.rect[3]=cardH+4;
            hi.uv[0]=0; hi.uv[1]=0; hi.uv[2]=1; hi.uv[3]=1; hi.solid=true;
            hi.tint[0]=1; hi.tint[1]=0.85f; hi.tint[2]=0.2f; hi.tint[3]=1; ren->drawSprite(hi);
        }
        int layer = spriteForItem(id);
        ren->drawSprite(spriteQuad(sx + 10.0f, cy + 10.0f, 46.0f, 46.0f, layer, C4(1,1,1,1)));
        drawText(itemName(id), sx + 66.0f, cy + 9.0f, 18, C4(1,1,1,1));
        drawText(effectSummary(id), sx + 66.0f, cy + 34.0f, 13, C4(0.7f,1,0.78f,1));
        drawText("x" + std::to_string(1), sx + cardW - 24.0f, cy + 22.0f, 18, C4(1,0.95f,0.5f,1));
    }

    // Clamp selection to visible item count.
    if (invSel >= n) invSel = n - 1;
    if (invSel < 0) invSel = 0;
    const std::string& sid = inv[invSel];

    // Detail pane.
    drawText(locale_.tr("inventory.detail_title"), rightX + 18, rightY + 16, 20, C4(1,0.95f,0.55f,1));
    ren->drawSprite(spriteQuad(rightX + 18, rightY + 54, 76, 76, spriteForItem(sid), C4(1,1,1,1)));
    drawText(itemName(sid), rightX + 108, rightY + 56, 26, C4(1,1,1,1));
    drawText("ID: " + sid, rightX + 108, rightY + 86, 14, C4(0.75f,0.8f,0.95f,1));
    drawText(locale_.tr("inventory.icon_label") + ": " + itemDefs[sid].value("sprite", std::string("")), rightX + 18, rightY + 136, 13, C4(0.7f,0.8f,0.95f,1));
    drawText(itemDesc(sid), rightX + 18, rightY + 162, 16, C4(0.88f,0.92f,1,1));
    drawText(locale_.tr("inventory.stats_label"), rightX + 18, rightY + 240, 13, C4(0.7f,0.8f,0.95f,1));

    const nlohmann::json& eff = itemDefs[sid]["effect"];
    float effY = rightY + 264;
    auto pill = [&](const std::string& s, float x, float y, float w) {
        Quad q; q.rect[0]=x; q.rect[1]=y; q.rect[2]=w; q.rect[3]=28;
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=0.12f; q.tint[1]=0.24f; q.tint[2]=0.16f; q.tint[3]=1;
        ren->drawSprite(q);
        drawText(s, x + 10, y + 7, 14, C4(0.85f,1,0.88f,1));
    };
    int pillRow = 0;
    if (eff.contains("str")) { pill("STR +" + std::to_string((int)eff["str"]), rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("def")) { pill("DEF +" + std::to_string((int)eff["def"]), rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("hp"))  { pill("HP +" + std::to_string((int)eff["hp"]),  rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("mp"))  { pill("MP +" + std::to_string((int)eff["mp"]),  rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("exp")) { pill("EXP +" + std::to_string((int)eff["exp"]), rightX + 18, effY + pillRow*34, 120); pillRow++; }
    if (eff.contains("gold")){ pill("Gold +" + std::to_string((int)eff["gold"]),rightX + 18, effY + pillRow*34, 126); pillRow++; }
    if (eff.contains("warp")){ pill("Warp", rightX + 18, effY + pillRow*34, 90); pillRow++; }
    if (pillRow == 0) pill(locale_.tr("inventory.no_effect"), rightX + 18, effY, 90);

    // Action buttons.
    float by = rightY + rightH - 48;
    float bw = 88, bh = 34;
    invUseRect_[0] = (int)(rightX + 18); invUseRect_[1] = (int)by; invUseRect_[2] = (int)bw; invUseRect_[3] = (int)bh;
    invDropRect_[0] = (int)(rightX + 116); invDropRect_[1] = (int)by; invDropRect_[2] = (int)bw; invDropRect_[3] = (int)bh;
    invCloseRect_[0] = (int)(rightX + rightW - 104); invCloseRect_[1] = (int)by; invCloseRect_[2] = 86; invCloseRect_[3] = (int)bh;

    auto drawBtn = [&](const int r[4], const std::string& txt, float cr, float cg, float cb) {
        Quad q; q.rect[0]=r[0]; q.rect[1]=r[1]; q.rect[2]=r[2]; q.rect[3]=r[3];
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=cr; q.tint[1]=cg; q.tint[2]=cb; q.tint[3]=1;
        ren->drawSprite(q);
        drawText(txt, (float)r[0] + 16, (float)r[1] + 9, 15, C4(1,1,1,1));
    };
    drawBtn(invUseRect_, locale_.tr("inventory.use"),   0.20f, 0.50f, 0.30f);
    drawBtn(invDropRect_, locale_.tr("inventory.drop"),  0.60f, 0.28f, 0.26f);
    drawBtn(invCloseRect_, locale_.tr("inventory.close"), 0.28f, 0.28f, 0.34f);

    drawText(locale_.tr("inventory.footer_hint"), rightX + 18, rightY + rightH - 76, 13, C4(0.75f,0.82f,0.95f,1));
}


// ---------- store system ----------
void Game::loadStore(const std::string& assetDir) {
    nlohmann::json j = readJsonFile(assetDir + "/../data/store.json");
    if (j.is_null() || j.empty()) { return; }
    if (j.contains("store_title")) storeTitle_ = j["store_title"];
    if (j.contains("unlockstage")) storeUnlockStage_ = j["unlockstage"].get<int>();
    for (auto& it : j["items"]) {
        StoreItemDef d;
        d.id = it.value("id", "");
        d.name = it.contains("name") ? it["name"] : nlohmann::json(d.id);
        d.sprite = it.value("sprite", "");
        // strip a trailing ".png" if present so it matches the atlas sprite id
        if (d.sprite.size() > 4 && d.sprite.substr(d.sprite.size()-4) == ".png")
            d.sprite = d.sprite.substr(0, d.sprite.size()-4);
        d.icon_path = it.value("icon_path", "");
        d.desc = it.contains("desc") ? it["desc"] : nlohmann::json("");
        if (it.contains("effect")) d.effect = it["effect"];
        d.effect_text = it.contains("effect_text") ? it["effect_text"] : nlohmann::json("");
        d.cost_base = it.value("cost_base", 2);
        d.cost_multiplier = it.value("cost_multiplier", 2);
        d.purchases = 0;
        // Milestone 8: an equipment-selling entry names its item instead of carrying an `effect`.
        d.equipmentId = it.value("equipmentId", std::string());
        storeItems_.push_back(d);
    }
}

void Game::openStore() {
    if (!storeUnlocked_) return;
    storeOpen = true;
    storeSel_ = 0;
    audio.play("confirm_click");
}
void Game::closeStore() {
    storeOpen = false;
    audio.play("close_ui");
}

// Milestone 5: scans data/stages/ once for every stage's id/name/index, so the Stage Select hub
// can list all of them (locked or not) without re-parsing JSON every frame.
void Game::ensureStageListLoaded() {
    if (stageListLoaded_) return;
    stageListLoaded_ = true;
    std::string dir = dataDir + "/../data/stages/";
    if (!std::filesystem::exists(dir)) return;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        try {
            nlohmann::json j = readJsonFile(e.path().string());
            if (j.is_null() || !j.contains("id")) continue;
            StageInfo info;
            info.id = j.value("id", std::string());
            info.name = j.contains("name") ? locale_.field(j["name"]) : info.id;
            info.index = j.value("index", 0);
            info.fileStem = e.path().stem().string();
            // Milestone 8: recommended-stats blurb, authored per stage JSON's optional top-level
            // "preview" string -- empty for a file that doesn't set one (none did before M8).
            info.preview = j.contains("preview") ? locale_.field(j["preview"]) : std::string();
            if (!info.id.empty()) stageList_.push_back(info);
        } catch (...) {}
    }
    std::sort(stageList_.begin(), stageList_.end(),
              [](const StageInfo& a, const StageInfo& b) { return a.index < b.index; });
}

void Game::openStageSelect() {
    ensureStageListLoaded();
    stageSelectOpen_ = true;
    audio.play("confirm_click");
}
void Game::closeStageSelect() {
    stageSelectOpen_ = false;
    audio.play("close_ui");
}

void Game::storeCardRects(std::vector<float>& rects, int n) const {
    rects.clear();
    float W = (float)ren->width(), H = (float)ren->height();
    float cw = 300, ch = 300, gap = 24;
    float totalW = n * cw + (n - 1) * gap;
    float ox = (W - totalW) / 2.0f;
    float oy = (H - ch) / 2.0f - 10;
    for (int i = 0; i < n; i++) {
        rects.push_back(ox + i * (cw + gap));
        rects.push_back(oy);
        rects.push_back(cw);
        rects.push_back(ch);
    }
}

std::vector<int> Game::storeTabIndices() const {
    std::vector<int> out;
    for (int i = 0; i < (int)storeItems_.size(); i++) {
        const StoreItemDef& d = storeItems_[i];
        int tab;
        if (d.equipmentId.empty()) {
            tab = 0;   // potions / plain consumables
        } else {
            auto it = equipmentDefs_.find(d.equipmentId);
            toms::EquipmentSlot slot = (it != equipmentDefs_.end()) ? it->second.slot : toms::EquipmentSlot::Weapon;
            tab = (slot == toms::EquipmentSlot::Weapon) ? 1 : (slot == toms::EquipmentSlot::Armor) ? 2 : 3;
        }
        if (tab == storeTab_) out.push_back(i);
    }
    return out;
}

void Game::drawStoreIcon() {
    float W = (float)ren->width();
    // top-right of the HUD bar (after GOLD/EXP text). Put it at x = W-60.
    int x = (int)(W - 56), y = 14, s = 42;
    storeIconRect[0] = x; storeIconRect[1] = y; storeIconRect[2] = s; storeIconRect[3] = s;
    // background panel
    Quad bg; bg.rect[0]=x-4; bg.rect[1]=y-4; bg.rect[2]=s+8; bg.rect[3]=s+8;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    if (storeUnlocked_) { bg.tint[0]=0.16f; bg.tint[1]=0.22f; bg.tint[2]=0.34f; bg.tint[3]=1; }
    else { bg.tint[0]=0.12f; bg.tint[1]=0.12f; bg.tint[2]=0.14f; bg.tint[3]=0.5f; } // locked = dim
    ren->drawSprite(bg);
    // icon: a shop/coin sprite. Use SP_GOLD (coin) as the store glyph.
    ren->drawSprite(spriteQuad((float)x, (float)y, (float)s, (float)s, spriteLayer("coin"), C4(1,1,1, storeUnlocked_?1.0f:0.4f)));
    // label
    drawText(locale_.tr("store.icon_label") + (storeUnlocked_ ? "" : "🔒"), (float)x-4, (float)y + s + 2, 12, C4(1,1,1, storeUnlocked_?1:0.4f));
}

void Game::drawStoreUnlockDialog() {
    float W = (float)ren->width(), H = (float)ren->height();
    drawStoreSplash();
    // centered dialog box
    float bw = 420, bh = 180;
    float bx = (W - bw)/2, by = (H - bh)/2;
    Quad box; box.rect[0]=bx; box.rect[1]=by; box.rect[2]=bw; box.rect[3]=bh;
    box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1; box.solid=true;
    box.tint[0]=0.12f;box.tint[1]=0.16f;box.tint[2]=0.26f;box.tint[3]=0.97f; ren->drawSprite(box);
    drawText(locale_.tr("store.unlocked_title"), bx+30, by+34, 24, C4(1,0.9f,0.5f,1));
    drawText(locale_.tr("store.unlocked_body"), bx+30, by+74, 15, C4(1,1,1,1));
    // confirm button (bottom-right of box)
    float btnW = 120, btnH = 40;
    float btnX = bx + bw - btnW - 24, btnY = by + bh - btnH - 20;
    storeUnlockBtnRect_[0] = (int)btnX; storeUnlockBtnRect_[1] = (int)btnY;
    storeUnlockBtnRect_[2] = (int)btnW; storeUnlockBtnRect_[3] = (int)btnH;
    Quad btn; btn.rect[0]=btnX; btn.rect[1]=btnY; btn.rect[2]=btnW; btn.rect[3]=btnH;
    btn.uv[0]=0;btn.uv[1]=0;btn.uv[2]=1;btn.uv[3]=1; btn.solid=true;
    btn.tint[0]=0.2f;btn.tint[1]=0.5f;btn.tint[2]=0.3f;btn.tint[3]=1; ren->drawSprite(btn);
    drawText(locale_.tr("store.confirm_hint"), btnX+12, btnY+11, 15, C4(1,1,1,1));
}

// Milestone 9: confirm-before-transition dialog (see stairsConfirmOpen_'s declaration
// in game.h). Drawn as an overlay on top of the walking scene, not its own scene --
// the map/HUD stay visible (dimmed) behind it, unlike the store dialogs' own splash.
void Game::drawStairsConfirmDialog() {
    float W = (float)ren->width(), H = (float)ren->height();
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.55f; ren->drawSprite(dim);

    float bw = 380, bh = 160;
    float bx = (W-bw)/2, by = (H-bh)/2;
    Quad box; box.rect[0]=bx; box.rect[1]=by; box.rect[2]=bw; box.rect[3]=bh;
    box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1; box.solid=true;
    box.tint[0]=0.12f;box.tint[1]=0.16f;box.tint[2]=0.26f;box.tint[3]=0.97f; ren->drawSprite(box);

    drawText(stairsConfirmIsUp_ ? locale_.tr("stairs.confirm_up") : locale_.tr("stairs.confirm_down"), bx+30, by+30, 22, C4(1,0.95f,0.6f,1));

    float btnW=130, btnH=40, gap=20;
    float by2 = by+bh-btnH-24;
    float yesX = bx + (bw-(btnW*2+gap))/2, noX = yesX+btnW+gap;
    stairsConfirmYesRect_[0]=(int)yesX; stairsConfirmYesRect_[1]=(int)by2; stairsConfirmYesRect_[2]=(int)btnW; stairsConfirmYesRect_[3]=(int)btnH;
    stairsConfirmNoRect_[0]=(int)noX;   stairsConfirmNoRect_[1]=(int)by2; stairsConfirmNoRect_[2]=(int)btnW; stairsConfirmNoRect_[3]=(int)btnH;

    Quad yes; yes.rect[0]=yesX; yes.rect[1]=by2; yes.rect[2]=btnW; yes.rect[3]=btnH;
    yes.uv[0]=0;yes.uv[1]=0;yes.uv[2]=1;yes.uv[3]=1; yes.solid=true;
    yes.tint[0]=0.2f;yes.tint[1]=0.5f;yes.tint[2]=0.3f;yes.tint[3]=1; ren->drawSprite(yes);
    drawText(locale_.tr("menu.yes") + " (Enter)", yesX+18, by2+11, 16, C4(1,1,1,1));

    Quad no; no.rect[0]=noX; no.rect[1]=by2; no.rect[2]=btnW; no.rect[3]=btnH;
    no.uv[0]=0;no.uv[1]=0;no.uv[2]=1;no.uv[3]=1; no.solid=true;
    no.tint[0]=0.5f;no.tint[1]=0.2f;no.tint[2]=0.2f;no.tint[3]=1; ren->drawSprite(no);
    drawText(locale_.tr("menu.no") + " (Esc)", noX+22, by2+11, 16, C4(1,1,1,1));
}

// ---------- in-game menu (walking-phase HUD gear icon) ----------
// Save / Settings (language) / Back to Title. A lighter-weight sibling of the title's own
// Menu/Settings flow, built directly as Game state to match every other in-game modal here.

void Game::drawMenuIcon() {
    float W = (float)ren->width();
    // Left of the store icon (store sits at x = W-56) so the two HUD buttons sit side by side
    // without overlapping, same y/size as drawStoreIcon().
    int x = (int)(W - 112), y = 14, s = 42;
    menuIconRect_[0] = x; menuIconRect_[1] = y; menuIconRect_[2] = s; menuIconRect_[3] = s;
    Quad bg; bg.rect[0]=x-4; bg.rect[1]=y-4; bg.rect[2]=s+8; bg.rect[3]=s+8;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.16f; bg.tint[1]=0.16f; bg.tint[2]=0.20f; bg.tint[3]=1;
    ren->drawSprite(bg);
    // No dedicated gear sprite exists in the atlas -- reuse the coin sprite's slot would be
    // confusing (already the store icon), so draw a plain glyph label instead, same as other
    // icon-less HUD buttons in this file.
    drawText("=", (float)x + s*0.5f - 6.0f, (float)y + 6.0f, 26, C4(1,1,1,1));
    drawText(locale_.tr("ingame_menu.title"), (float)x-10, (float)y + s + 2, 12, C4(1,1,1,1));
}

void Game::openInGameMenu() {
    inGameMenuOpen_ = true;
    inGameMenuPage_ = InGameMenuPage::Main;
    inGameMenuSel_ = 0;
    inGameLangConfirmOpen_ = false;
    audio.play("confirm_click");
}

void Game::closeInGameMenu() {
    inGameMenuOpen_ = false;
    inGameMenuPage_ = InGameMenuPage::Main;
    inGameLangConfirmOpen_ = false;
    audio.play("close_ui");
}

void Game::inGameMenuMove(int delta) {
    if (delta == 0 || !inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) { inGameLangConfirmYes_ = !inGameLangConfirmYes_; return; }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        int n = 3;   // Save / Settings / Back to Title
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    } else {
        int n = std::max(1, locale_.languageCount());
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    }
}

void Game::inGameMenuActivate() {
    if (!inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) {
        const bool yes = inGameLangConfirmYes_;
        const int idx = inGameLangConfirmIdx_;
        inGameLangConfirmOpen_ = false;
        if (yes) { title_.setLanguageIndex(idx); applyLanguage(); }
        audio.play("confirm_click");
        return;
    }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        switch (inGameMenuSel_) {
            case 0:   // Save
                saveCurrentRun();
                toastMsg_ = locale_.tr("save.saved");
                toastTimer_ = 1600;
                audio.play("confirm_click");
                break;
            case 1:   // Settings
                inGameMenuPage_ = InGameMenuPage::Settings;
                inGameMenuSel_ = locale_.languageIndex();
                audio.play("confirm_click");
                break;
            case 2:   // Back to Title
                returnToTitle();
                break;
        }
    } else {
        // Settings page: activating a language row opens the "switch to XXX?" dialog,
        // mirroring the title screen's own confirm dialog (see AskLanguageChange).
        if (inGameMenuSel_ >= 0 && inGameMenuSel_ < locale_.languageCount()) {
            inGameLangConfirmOpen_ = true;
            inGameLangConfirmIdx_ = inGameMenuSel_;
            inGameLangConfirmYes_ = true;
            audio.play("confirm_click");
        }
    }
}

void Game::inGameMenuBack() {
    if (!inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) { inGameLangConfirmOpen_ = false; audio.play("close_ui"); return; }
    if (inGameMenuPage_ == InGameMenuPage::Settings) {
        inGameMenuPage_ = InGameMenuPage::Main;
        inGameMenuSel_ = 1;   // land back on the "Settings" row
        audio.play("close_ui");
        return;
    }
    closeInGameMenu();
}

void Game::inGameMenuClick(float x, float y) {
    if (!inGameMenuOpen_) return;
    auto hit = [&](const int r[4]) { return x>=r[0] && x<=r[0]+r[2] && y>=r[1] && y<=r[1]+r[3]; };
    if (inGameLangConfirmOpen_) {
        if (hit(igmLangYesRect_)) { inGameLangConfirmYes_ = true; inGameMenuActivate(); }
        else if (hit(igmLangNoRect_)) { inGameLangConfirmYes_ = false; inGameMenuActivate(); }
        return;   // the dialog swallows all other taps
    }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        if (hit(igmSaveRect_)) { inGameMenuSel_ = 0; inGameMenuActivate(); return; }
        if (hit(igmSettingsRect_)) { inGameMenuSel_ = 1; inGameMenuActivate(); return; }
        if (hit(igmBackToTitleRect_)) { inGameMenuSel_ = 2; inGameMenuActivate(); return; }
        if (hit(igmCloseRect_)) { closeInGameMenu(); return; }
    } else {
        int n = std::min((int)igmLangRowRects_.size()/4, locale_.languageCount());
        for (int i = 0; i < n; i++) {
            const float* r4 = &igmLangRowRects_[i*4];
            int r[4] = {(int)r4[0], (int)r4[1], (int)r4[2], (int)r4[3]};
            if (hit(r)) { inGameMenuSel_ = i; inGameMenuActivate(); return; }
        }
        if (hit(igmBackRect_)) { inGameMenuBack(); return; }
    }
}

// Saves, tears down transient modal/run-in-progress state, and reopens the title -- the
// inverse of newGame()/applyLoadedRun() (see their resets for what this mirrors). Unlike
// newGame(), this deliberately leaves pl/meta_/entityStatus_/activeSlot_/curStage alone: the
// run is only SUSPENDED, not discarded, so Continue can pick it back up.
void Game::returnToTitle() {
    saveCurrentRun();   // flush before leaving so nothing is lost
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    invOpen = false; storeOpen = false; storeUnlockDlg = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    closeInGameMenu();
    missionTrackers_.clear();
    notifications_.clear();
    title_.setRunInProgress(false);
    title_.open();
    refreshSlots();
}

void Game::drawInGameMenu() {
    if (!ren || !inGameMenuOpen_) return;
    const float W = (float)ren->width(), H = (float)ren->height();
    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};
    static const float white[4] = {1,1,1,1};

    Quad scrim;
    scrim.rect[0]=0; scrim.rect[1]=0; scrim.rect[2]=W; scrim.rect[3]=H;
    scrim.uv[0]=0;scrim.uv[1]=0;scrim.uv[2]=1;scrim.uv[3]=1; scrim.solid=true;
    scrim.tint[0]=0; scrim.tint[1]=0; scrim.tint[2]=0; scrim.tint[3]=0.62f;
    ren->drawSprite(scrim);

    auto panel = [&](float x, float y, float w, float h) {
        Quad p; p.rect[0]=x; p.rect[1]=y; p.rect[2]=w; p.rect[3]=h;
        p.uv[0]=0;p.uv[1]=0;p.uv[2]=1;p.uv[3]=1; p.solid=true;
        p.tint[0]=0.13f; p.tint[1]=0.14f; p.tint[2]=0.21f; p.tint[3]=0.98f;
        ren->drawSprite(p);
        Quad rule; rule.rect[0]=x; rule.rect[1]=y; rule.rect[2]=w; rule.rect[3]=3.0f;
        rule.uv[0]=0;rule.uv[1]=0;rule.uv[2]=1;rule.uv[3]=1; rule.solid=true;
        rule.tint[0]=gold[0]; rule.tint[1]=gold[1]; rule.tint[2]=gold[2]; rule.tint[3]=0.9f;
        ren->drawSprite(rule);
    };
    auto button = [&](int rectOut[4], float x, float y, float w, float h,
                       const std::string& label, bool selected) {
        rectOut[0]=(int)x; rectOut[1]=(int)y; rectOut[2]=(int)w; rectOut[3]=(int)h;
        Quad q; q.rect[0]=x; q.rect[1]=y; q.rect[2]=w; q.rect[3]=h;
        q.uv[0]=0;q.uv[1]=0;q.uv[2]=1;q.uv[3]=1; q.solid=true;
        if (selected) { q.tint[0]=0.24f; q.tint[1]=0.30f; q.tint[2]=0.44f; q.tint[3]=1; }
        else          { q.tint[0]=0.16f; q.tint[1]=0.17f; q.tint[2]=0.24f; q.tint[3]=0.96f; }
        ren->drawSprite(q);
        drawText(label, x + 18, y + (h-20)*0.5f, 18, white);
    };

    if (inGameLangConfirmOpen_) {
        const float bw=560, bh=210, bx=(W-bw)*0.5f, by=(H-bh)*0.5f;
        panel(bx, by, bw, bh);
        const auto& langs = locale_.languages();
        int idx = inGameLangConfirmIdx_;
        std::string langName = (idx >= 0 && idx < (int)langs.size()) ? langs[idx].name : "";
        std::string question = trParam(locale_.tr("settings.language_confirm"), "lang", langName);
        drawText(question, bx + (bw - measureText(question, 24)) * 0.5f, by + 70.0f, 24, gold);
        const float btnW=160, btnH=56, gap=40, groupW=btnW*2+gap;
        const float bx0 = bx + (bw-groupW)*0.5f, byy = by + bh - btnH - 26.0f;
        const bool yes = inGameLangConfirmYes_;
        button(igmLangYesRect_, bx0, byy, btnW, btnH, locale_.tr("menu.yes"), yes);
        button(igmLangNoRect_,  bx0+btnW+gap, byy, btnW, btnH, locale_.tr("menu.no"), !yes);
        return;
    }

    if (inGameMenuPage_ == InGameMenuPage::Main) {
        const float bw=420, bh=380, bx=(W-bw)*0.5f, by=(H-bh)*0.5f;
        panel(bx, by, bw, bh);
        const std::string head = locale_.tr("ingame_menu.title");
        drawText(head, bx + (bw - measureText(head, 28)) * 0.5f, by + 28.0f, 28, gold);
        const float rw = bw - 60, rh = 64, rx = bx + 30, gap = 16;
        float ry = by + 90;
        button(igmSaveRect_, rx, ry, rw, rh, locale_.tr("ingame_menu.save"), inGameMenuSel_ == 0);
        ry += rh + gap;
        button(igmSettingsRect_, rx, ry, rw, rh, locale_.tr("menu.settings"), inGameMenuSel_ == 1);
        ry += rh + gap;
        button(igmBackToTitleRect_, rx, ry, rw, rh, locale_.tr("ingame_menu.back_to_title"), inGameMenuSel_ == 2);
        const float cw=140, ch=48, cx=bx+(bw-cw)*0.5f, cy=by+bh-ch-20;
        button(igmCloseRect_, cx, cy, cw, ch, locale_.tr("inventory.close"), false);
    } else {
        const auto& langs = locale_.languages();
        int n = std::max(1, (int)langs.size());
        const float rh = 48, gap = 10;
        const float bw = 420, bh = 130.0f + n * (rh + gap) + 70.0f;
        const float bx = (W-bw)*0.5f, by = (H-bh)*0.5f;
        panel(bx, by, bw, bh);
        const std::string head = locale_.tr("settings.header");
        drawText(head, bx + (bw - measureText(head, 26)) * 0.5f, by + 26.0f, 26, gold);
        const std::string langLabel = locale_.tr("settings.language");
        drawText(langLabel, bx + (bw - measureText(langLabel, 16)) * 0.5f, by + 62.0f, 16, dim);
        const float rw = bw - 60, rx = bx + 30;
        float ry = by + 96;
        igmLangRowRects_.assign((size_t)n * 4, 0.0f);
        int scratch[4];   // button() wants an int[4] out-param; the real rect is stored below
        for (int i = 0; i < (int)langs.size(); i++) {
            const bool active = (i == locale_.languageIndex());
            std::string label = std::string(active ? "[x] " : "[ ] ") + langs[i].name;
            button(scratch, rx, ry, rw, rh, label, inGameMenuSel_ == i);
            igmLangRowRects_[i*4+0] = rx; igmLangRowRects_[i*4+1] = ry;
            igmLangRowRects_[i*4+2] = rw; igmLangRowRects_[i*4+3] = rh;
            ry += rh + gap;
        }
        const float cw=140, ch=44, cx=bx+(bw-cw)*0.5f;
        button(igmBackRect_, cx, ry + 10, cw, ch, locale_.tr("menu.back"), false);
    }
}

void Game::drawStoreUI() {
    float W = (float)ren->width(), H = (float)ren->height();
    drawStoreSplash();
    // panel background
    Quad bg; bg.rect[0]=40; bg.rect[1]=40; bg.rect[2]=W-80; bg.rect[3]=H-80;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.08f;bg.tint[1]=0.1f;bg.tint[2]=0.16f;bg.tint[3]=0.96f; ren->drawSprite(bg);
    // title + gold
    drawText(locale_.field(storeTitle_), 60, 64, 26, C4(1,0.9f,0.5f,1));
    drawText(locale_.tr("store.gold_label") + ": " + std::to_string(pl.gold), W-260, 64, 20, C4(1,0.95f,0.4f,1));
    drawText(locale_.tr("store.controls_hint"), 60, 94, 14, C4(0.8f,0.85f,1,0.9f));

    // Milestone 8: tab bar (see storeTabIndices()'s declaration in game.h for why tabs exist at
    // all -- keeps every tab's card count at the layout's original, unchanged capacity of ~3).
    const std::string kTabLabels[4] = {
        locale_.tr("store.tab.potion"), locale_.tr("store.tab.weapon"),
        locale_.tr("store.tab.armor"), locale_.tr("store.tab.talent"),
    };
    storeTabRects_.clear();
    {
        float tbw = 100, tbh = 34, tgap = 12;
        float tx = 60, ty = 110;
        for (int t = 0; t < 4; t++) {
            storeTabRects_.push_back(tx); storeTabRects_.push_back(ty);
            storeTabRects_.push_back(tbw); storeTabRects_.push_back(tbh);
            Quad tb; tb.rect[0]=tx; tb.rect[1]=ty; tb.rect[2]=tbw; tb.rect[3]=tbh;
            tb.uv[0]=0;tb.uv[1]=0;tb.uv[2]=1;tb.uv[3]=1; tb.solid=true;
            bool activeTab = (t == storeTab_);
            if (activeTab) { tb.tint[0]=0.24f; tb.tint[1]=0.30f; tb.tint[2]=0.44f; tb.tint[3]=1; }
            else           { tb.tint[0]=0.13f; tb.tint[1]=0.14f; tb.tint[2]=0.20f; tb.tint[3]=0.9f; }
            ren->drawSprite(tb);
            drawText(kTabLabels[t], tx + 22, ty + 8, 17, activeTab ? C4(1,0.95f,0.6f,1) : C4(0.8f,0.82f,0.9f,1));
            tx += tbw + tgap;
        }
    }

    std::vector<int> idx = storeTabIndices();
    std::vector<float> rects; storeCardRects(rects, (int)idx.size());
    storeBtnRects_.clear();
    for (int i = 0; i < (int)idx.size(); i++) {
        float cx = rects[i*4], cy = rects[i*4+1], cw = rects[i*4+2], ch = rects[i*4+3];
        const StoreItemDef& d = storeItems_[idx[i]];
        int cost = d.liveCost();
        // card bg
        Quad card; card.rect[0]=cx; card.rect[1]=cy; card.rect[2]=cw; card.rect[3]=ch;
        card.uv[0]=0;card.uv[1]=0;card.uv[2]=1;card.uv[3]=1; card.solid=true;
        bool sel = (i == storeSel_);
        if (sel) { card.tint[0]=0.22f; card.tint[1]=0.26f; card.tint[2]=0.38f; card.tint[3]=1; }
        else     { card.tint[0]=0.14f; card.tint[1]=0.16f; card.tint[2]=0.24f; card.tint[3]=0.96f; }
        ren->drawSprite(card);
        // selected highlight border
        if (sel) {
            Quad hi; hi.rect[0]=cx-3; hi.rect[1]=cy-3; hi.rect[2]=cw+6; hi.rect[3]=ch+6;
            hi.uv[0]=0;hi.uv[1]=0;hi.uv[2]=1;hi.uv[3]=1; hi.solid=true;
            hi.tint[0]=1;hi.tint[1]=0.85f;hi.tint[2]=0.2f;hi.tint[3]=1; ren->drawSprite(hi);
        }
        // icon (sprite)
        ren->drawSprite(spriteQuad(cx + cw/2 - 46, cy + 22, 92, 92, spriteLayer(d.sprite.empty()?"coin":d.sprite), C4(1,1,1,1)));
        drawText(locale_.field(d.name), cx + 16, cy + 124, 26, C4(1,1,1,1));
        drawText(locale_.field(d.desc), cx + 16, cy + 162, 16, C4(0.85f,0.9f,1,1));
        drawText(locale_.tr("store.effect_label") + ": " + locale_.field(d.effect_text), cx + 16, cy + 188, 18, C4(0.6f,1,0.7f,1));
        // Milestone 8: an equipment card shows whether it's the one currently in its slot instead
        // of a purchase counter (buying it re-equips it -- "已購買 x3" would be misleading).
        bool isEquipped = !d.equipmentId.empty() &&
            (d.equipmentId == equipped_.weaponId || d.equipmentId == equipped_.armorId || d.equipmentId == equipped_.talentId);
        if (!d.equipmentId.empty())
            drawText(isEquipped ? locale_.tr("store.equipped") : locale_.tr("store.tap_to_equip"), cx + 16, cy + 216, 15, isEquipped ? C4(0.5f,1,0.6f,1) : C4(0.7f,0.7f,0.8f,1));
        else
            drawText(locale_.tr("store.purchased_label") + " x" + std::to_string(d.purchases), cx + 16, cy + 216, 15, C4(0.7f,0.7f,0.8f,1));
        // price tag
        drawText(locale_.tr("store.price_label") + ": " + std::to_string(cost) + " G", cx + 16, cy + 240, 22, C4(1,0.95f,0.4f,1));
        // buy button
        float btnW = cw - 32, btnH = 38;
        float btnX = cx + 16, btnY = cy + ch - btnH - 12;
        storeBtnRects_.push_back(btnX); storeBtnRects_.push_back(btnY); storeBtnRects_.push_back(btnW); storeBtnRects_.push_back(btnH);
        Quad btn; btn.rect[0]=btnX; btn.rect[1]=btnY; btn.rect[2]=btnW; btn.rect[3]=btnH;
        btn.uv[0]=0;btn.uv[1]=0;btn.uv[2]=1;btn.uv[3]=1; btn.solid=true;
        btn.tint[0]=0.2f;btn.tint[1]=0.5f;btn.tint[2]=0.3f;btn.tint[3]=1; ren->drawSprite(btn);
        drawText(locale_.tr("store.buy_button"), btnX + 16, btnY + 10, 16, C4(1,1,1,1));
    }
    // close button (top-right corner of panel)
    float cw2 = 90, ch2 = 34;
    storeCloseRect_[0] = (int)(W-40-cw2); storeCloseRect_[1] = 56; storeCloseRect_[2] = (int)cw2; storeCloseRect_[3] = (int)ch2;
    Quad cbtn; cbtn.rect[0]=storeCloseRect_[0]; cbtn.rect[1]=storeCloseRect_[1]; cbtn.rect[2]=cw2; cbtn.rect[3]=ch2;
    cbtn.uv[0]=0;cbtn.uv[1]=0;cbtn.uv[2]=1;cbtn.uv[3]=1; cbtn.solid=true;
    cbtn.tint[0]=0.5f;cbtn.tint[1]=0.2f;cbtn.tint[2]=0.2f;cbtn.tint[3]=1; ren->drawSprite(cbtn);
    drawText(locale_.tr("inventory.close") + " X", storeCloseRect_[0]+14, storeCloseRect_[1]+9, 16, C4(1,1,1,1));
}

void Game::drawStoreToast() {
    if (toastTimer_ <= 0 || toastMsg_.empty()) return;
    float W = (float)ren->width(), H = (float)ren->height();
    float tw = 260, th = 48;
    float tx = (W - tw)/2, ty = H - 160;
    // "not enough gold" -> simple shake effect on the toast
    if (shakeTimer_ > 0) tx += (float)(6.0f * std::sin((float)shakeTimer_ * 0.06f));
    Quad t; t.rect[0]=tx; t.rect[1]=ty; t.rect[2]=tw; t.rect[3]=th;
    t.uv[0]=0;t.uv[1]=0;t.uv[2]=1;t.uv[3]=1; t.solid=true;
    t.tint[0]=0.4f;t.tint[1]=0.1f;t.tint[2]=0.1f;t.tint[3]=0.95f; ren->drawSprite(t);
    drawText(toastMsg_, tx + 20, ty + 14, 18, C4(1,0.8f,0.8f,1));
}

void Game::storeClick(float x, float y) {
    // unlock dialog takes priority
    if (storeUnlockDlg) {
        // confirm button?
        if (x >= storeUnlockBtnRect_[0] && x <= storeUnlockBtnRect_[0]+storeUnlockBtnRect_[2] &&
            y >= storeUnlockBtnRect_[1] && y <= storeUnlockBtnRect_[1]+storeUnlockBtnRect_[3]) {
            storeUnlockDlg = false; audio.play("confirm_click");
        }
        return; // modal: swallow all other clicks
    }
    if (storeOpen) {
        // close button
        if (x >= storeCloseRect_[0] && x <= storeCloseRect_[0]+storeCloseRect_[2] &&
            y >= storeCloseRect_[1] && y <= storeCloseRect_[1]+storeCloseRect_[3]) {
            closeStore(); return;
        }
        // Milestone 8: tab buttons (checked before the buy buttons, which are per-tab now).
        for (int t = 0; t + 3 < (int)storeTabRects_.size() / 4 * 4; t += 4) {
            float bx = storeTabRects_[t], by = storeTabRects_[t+1], bw = storeTabRects_[t+2], bh = storeTabRects_[t+3];
            if (x >= bx && x <= bx+bw && y >= by && y <= by+bh) {
                storeTab_ = t / 4; storeSel_ = 0; audio.play("confirm_click"); return;
            }
        }
        // buy buttons -- storeBtnRects_ is rebuilt per-tab each frame in drawStoreUI(), so index i
        // here maps through the same tab filter to the real storeItems_ index.
        std::vector<int> idx = storeTabIndices();
        int nBtn = (int)storeBtnRects_.size() / 4;
        for (int i = 0; i < nBtn && i < (int)idx.size(); i++) {
            float bx = storeBtnRects_[i*4], by = storeBtnRects_[i*4+1], bw = storeBtnRects_[i*4+2], bh = storeBtnRects_[i*4+3];
            if (x >= bx && x <= bx+bw && y >= by && y <= by+bh) {
                buyStoreItem(idx[i]); return;
            }
        }
        // clicking a card selects it (and if it's the buy area handled above). Clicking
        // outside the panel closes the store.
        bool insidePanel = (x >= 40 && x <= (float)ren->width()-40 && y >= 40 && y <= (float)ren->height()-40);
        if (!insidePanel) closeStore();
        return;
    }
    // not in any store overlay -> clicking the HUD icon opens the store (only if unlocked)
    if (storeUnlocked_ &&
        x >= storeIconRect[0] && x <= storeIconRect[0]+storeIconRect[2] &&
        y >= storeIconRect[1] && y <= storeIconRect[1]+storeIconRect[3]) {
        openStore();
    }
}

void Game::storeKey(int key) {
    // unlock dialog: Enter/Esc/Space confirms
    if (storeUnlockDlg) {
        if (key == 13 || key == 27 || key == 32) { storeUnlockDlg = false; audio.play("confirm_click"); }
        return;
    }
    if (!storeOpen) return;
    // Milestone 8: storeSel_ is an index into the current tab's filtered list, not storeItems_
    // directly (see storeTabIndices()) -- map through it before buying.
    std::vector<int> idxList = storeTabIndices();
    int n = (int)idxList.size();
    if (key == 27) { closeStore(); return; }                       // Esc closes
    if (key == 13 || key == 32) {                                  // Enter/Space buys
        if (storeSel_ >= 0 && storeSel_ < n) buyStoreItem(idxList[storeSel_]);
        return;
    }
    if (key == 262 || key == 263) {                                // right/left arrow
        storeSel_ += (key == 262) ? 1 : -1;
        if (storeSel_ < 0) storeSel_ = n-1; if (storeSel_ >= n) storeSel_ = 0;
        return;
    }
    if (key >= '1' && key <= '9') {                                // number keys select
        int sel = key - '1';
        if (sel < n) storeSel_ = sel;
    }
}

void Game::buyStoreItem(int idx) {
    if (idx < 0 || idx >= (int)storeItems_.size()) return;
    StoreItemDef& d = storeItems_[idx];
    int cost = d.liveCost();
    if (pl.gold < cost) {
        // not enough gold -> toast + shake (simple effect), no purchase
        toastMsg_ = trParam(locale_.tr("store.toast_insufficient_gold"), "cost", std::to_string(cost));
        toastTimer_ = 1600;
        shakeTimer_ = 350;
        audio.play("deny");
        return;
    }
    pl.gold -= cost;
    // Milestone 8: an equipment entry replaces whatever was in that slot instead of applying a
    // generic {hp/str/def} effect -- equipping is a swap, not a stacking buff, so re-buying the
    // same item (or a different one in the same slot) is a harmless, idempotent re-equip.
    if (!d.equipmentId.empty()) {
        auto it = equipmentDefs_.find(d.equipmentId);
        if (it != equipmentDefs_.end()) {
            switch (it->second.slot) {
                case toms::EquipmentSlot::Weapon: equipped_.weaponId = d.equipmentId; break;
                case toms::EquipmentSlot::Armor:  equipped_.armorId  = d.equipmentId; break;
                case toms::EquipmentSlot::Talent: equipped_.talentId = d.equipmentId; break;
            }
        }
    } else {
        // apply effect immediately (per spec: bought item is used right away)
        const nlohmann::json& eff = d.effect;
        if (eff.contains("hp"))  pl.hp   = std::min(pl.maxhp, pl.hp + (int)eff["hp"]);
        if (eff.contains("str")) pl.atk  += (int)eff["str"];
        if (eff.contains("def")) pl.def  += (int)eff["def"];
    }
    d.purchases++;
    toastMsg_ = trParam(locale_.tr("store.toast_purchase_success"), "name", locale_.field(d.name));
    toastTimer_ = 1400;
    audio.play("get_item");
    markProgressDirty();   // a purchase changes gold/stats/equipment: autosave will flush it
}


// Milestone 9 polish: see setMoveHeldX/Y's declaration in game.h. A fresh press (or switching
// directions on the same axis) fires one immediate step, same feel as the old one-tap-one-tile
// behavior; the repeat while still held is ticked in update().
void Game::setMoveHeldX(int dir) {
    if (dir == moveHoldX_.dir) return;
    moveHoldX_.dir = dir; moveHoldX_.holdMs = 0; moveHoldX_.repeating = false;
    if (dir != 0) movePlayer(dir, 0);
}
void Game::setMoveHeldY(int dir) {
    if (dir == moveHoldY_.dir) return;
    moveHoldY_.dir = dir; moveHoldY_.holdMs = 0; moveHoldY_.repeating = false;
    if (dir != 0) movePlayer(0, dir);
}

void Game::movePlayer(int dx, int dy) {
    if (modalActive()) return;   // any modal overlay (combat/dialogue/inventory) blocks world input
    int nx = pl.x + dx, ny = pl.y + dy;
    char c = st.at(nx, ny);
    if (c == '#') return;
    // Doors need keys -- gate routed through the shared Condition Evaluator (condition.h) so
    // this uses the same "can the player do X" engine as dialogue `requires` gating, per
    // Milestone 3's retrofit. Same outcome as the three bespoke inline checks this replaces:
    // each door color needs >=1 of its matching key, expressed declaratively instead.
    static const std::map<char, std::string> kDoorKeyItem = {
        {'y', "key_yellow"}, {'b', "key_blue"}, {'r', "key_red"}
    };
    if (auto doorIt = kDoorKeyItem.find(c); doorIt != kDoorKeyItem.end()) {
        nlohmann::json req = { {"type", "itemHeld"}, {"itemId", doorIt->second}, {"count", 1} };
        if (!toms::evaluate(req, GameConditionContext(pl, meta_, missionTrackers_))) return;
    }
    if (c == 'y') pl.key_yellow--;
    if (c == 'b') pl.key_blue--;
    if (c == 'r') pl.key_red--;
    pl.x = nx; pl.y = ny;
    audio.play("walk");
    // check entity at new cell
    for (auto& e : st.entities) {
        if (e.consumed) continue;
        if (e.x == nx && e.y == ny) {
            if (e.kind.rfind("monster:",0)==0) {
                engageMonster(e);
                return;
            } else if (e.kind.rfind("item:",0)==0) {
                // Keys/coins apply immediately (not stored in the 9-grid UI).
                // Usable items (gems/potions/exp/scroll) go into the inventory.
                bool immediate = (e.id.rfind("key_",0)==0) || e.id=="coin";
                if (immediate) applyItem(e.id);
                else pl.inv.push_back(e.id);
                e.consumed = true;
                st.tiles[e.y][e.x] = '.'; // clear from grid
                // Milestone 7: persist the clear so it survives a reload of this floor (stairs
                // or the Stage Select hub) -- see entityStatus_'s declaration in game.h.
                toms::setEntityStatus(entityStatus_, toms::entityStatusKey(curStage, e.x, e.y), toms::EntityStatus::Collected);
                toms::globalEventBus().publish(toms::ItemCollected{e.id, curStage});
            } else if (e.kind.rfind("npc:",0)==0) {
                // start dialogue
                dlgNpc = "enemy_"+e.id; // fallback; real npc ids below
                if (e.id=="villager") dlgNpc="villager_elder";
                else if (e.id=="sorcerer") dlgNpc="sorcerer_teacher";
                else if (e.id=="king") dlgNpc="king_lieutenant";
                else if (e.id=="princess") dlgNpc=(curStage=="stage_11")?"princess_victory":"princess_liora";
                else if (e.id=="handmaiden") dlgNpc="handmaiden";
                startDialogue(dlgNpc);
            } else if (c=='U' && !st.up.empty()) { requestStageTransition(st.up, true); return; }
            else if (c=='D' && !st.down.empty()) { requestStageTransition(st.down, false); return; }
        }
    }
    // stairs check (cell char)
    if (c=='U' && !st.up.empty()) requestStageTransition(st.up, true);
    else if (c=='D' && !st.down.empty()) requestStageTransition(st.down, false);
}

// Milestone 9: opens the Yes/No confirm instead of transitioning immediately -- see
// stairsConfirmOpen_'s declaration in game.h for why.
void Game::requestStageTransition(const std::string& target, bool isUp) {
    stairsConfirmOpen_ = true;
    stairsConfirmIsUp_ = isUp;
    stairsConfirmTarget_ = target;
    audio.play("confirm_click");
}

void Game::confirmStageTransition() {
    if (!stairsConfirmOpen_) return;
    std::string target = stairsConfirmTarget_;
    stairsConfirmOpen_ = false;
    stairsConfirmTarget_.clear();
    loadStage(target);
    // A floor change is the run's natural checkpoint: write/refresh the slot immediately
    // (rather than waiting for the throttled autosave) so the Continue list is always current.
    saveCurrentRun();
}

void Game::cancelStageTransition() {
    stairsConfirmOpen_ = false;
    stairsConfirmTarget_.clear();
    audio.play("close_ui");
}

void Game::startDialogue(const std::string& npc) {
    dlgData = readJsonFile(dataDir + "/../data/dialogue/" + npc + ".json");
    if (dlgData.is_null() || dlgData.empty()) { inDialogue=false; return; }
    if (!dlgData.contains("start") || !dlgData["start"].is_string()) { inDialogue = false; return; }
    inDialogue = true; dlgNode = dlgData["start"];
    dlgSel = 0;
    enterNode(dlgNode);
    audio.play("dialogue_popup");
}
void Game::enterNode(const std::string& node) {
    dlgChoices.clear();
    if (!dlgData.contains("nodes") || !dlgData["nodes"].contains(node)) {
        inDialogue = false; return;   // missing node -> close safely (no nlohmann throw / abort)
    }
    const auto& n = dlgData["nodes"][node];
    if (!n.is_object() || !n.contains("choices")) return;
    for (auto& c : n["choices"]) {
        if (!c.is_object()) continue;
        // Milestone 3: a choice's optional `requires` gates whether it appears at all, evaluated
        // via the shared Condition Evaluator (condition.h). No shipped dialogue file sets this
        // yet, so `contains("requires")` is false for all of them today -- purely additive.
        if (c.contains("requires") && !toms::evaluate(c["requires"], GameConditionContext(pl, meta_, missionTrackers_)))
            continue;
        std::string label = c.contains("label") ? locale_.field(c["label"]) : "";
        std::string next  = c.contains("next")  && !c["next"].is_null()  ? (std::string)c["next"]  : "";
        nlohmann::json action = c.contains("action") ? c["action"] : nlohmann::json();
        dlgChoices.push_back({label, next, action});
    }
    dlgSel = 0;
}
void Game::chooseDialogue(int idx) {
    if (!inDialogue) return;
    if (idx < 0 || idx >= (int)dlgChoices.size()) return;
    markProgressDirty();   // dialogue choices move story flags/beats, which the meta save holds
    audio.play("confirm_click");
    auto ch = dlgChoices[idx];   // copy: runDialogueAction (e.g. enterBattle) may resize dlgChoices
    toms::globalEventBus().publish(toms::ChoiceMade{dlgNpc, ch.label});
    if (!ch.action.is_null()) runDialogueAction(ch.action);
    if (!inDialogue) return;    // an action (e.g. enterBattle) may have already ended the dialogue
    if (ch.next.empty()) { inDialogue = false; return; }
    dlgNode = ch.next; enterNode(dlgNode);
}

// Milestone 4: executes a dialogue choice's optional `action` verb. Kept as a small, explicit
// enum-style switch rather than a scripting hook, matching this project's existing preference
// (see MAIN_BATTLE_SCENE_DESIGN.md §4.4's `talent` enum / dialogue's own `action.give`
// precedent in the design docs).
void Game::runDialogueAction(const nlohmann::json& action) {
    if (!action.is_object()) return;
    std::string type = action.value("type", std::string());
    if (type == "give") {
        std::string itemId = action.value("itemId", std::string());
        if (!itemId.empty()) applyItem(itemId);
    } else if (type == "setStoryFlag") {
        std::string flag = action.value("flag", std::string());
        if (!flag.empty()) toms::setStoryFlag(meta_, flag);
    } else if (type == "enterBattle") {
        // Only meaningful after a dialogue_gate encounter (movePlayer) stashed an enemy.
        if (hasPendingEncounter_) {
            inDialogue = false;
            hasPendingEncounter_ = false;
            startCombat(pendingEncounterEnemy_);
        }
    } else if (type == "startMission") {
        std::string missionId = action.value("missionId", std::string());
        if (!missionId.empty()) startMission(missionId);
    } else if (type == "claimMission") {
        std::string missionId = action.value("missionId", std::string());
        if (!missionId.empty()) claimMission(missionId);
    }
}

void Game::startMission(const std::string& id) {
    auto& t = missionTrackers_[id];   // creates a fresh (Locked, progress=0) tracker if absent
    if (t.missionId.empty()) t.missionId = id;
    t.state = toms::MissionState::Active;
}

// Milestone 8: grants the reward and flips Completed -> Claimed. Strictly gated on the tracker
// already being Completed, so a dialogue choice that stays visible after claiming (see the
// `requires: missionComplete` convention used in this session's authored content -- missionComplete()
// returns true for both Completed and Claimed) can never grant the reward twice.
void Game::claimMission(const std::string& id) {
    auto trackerIt = missionTrackers_.find(id);
    if (trackerIt == missionTrackers_.end() || trackerIt->second.state != toms::MissionState::Completed) return;
    auto defIt = missionDefs_.find(id);
    if (defIt != missionDefs_.end()) {
        const toms::MissionDefinition& def = defIt->second;
        pl.exp += def.rewardExp;
        pl.gold += def.rewardGold;
        // Mirrors movePlayer()'s item-pickup split: keys/coins apply immediately, everything
        // else (gems/potions/exp/scroll) goes into the inventory to be used later, not consumed
        // on the spot -- a reward potion should sit in the backpack like a picked-up one would.
        if (!def.rewardItemId.empty()) {
            bool immediate = (def.rewardItemId.rfind("key_",0)==0) || def.rewardItemId=="coin";
            if (immediate) applyItem(def.rewardItemId);
            else pl.inv.push_back(def.rewardItemId);
        }
        int need = pl.lv * 30;
        while (pl.exp >= need) {
            pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30;
            pushNotification(locale_.tr("battle.levelup") + std::to_string(pl.lv));
        }
    }
    trackerIt->second.state = toms::MissionState::Claimed;
    pushNotification(locale_.tr("mission.completed_prefix") + id);
}

// Subscribes mission-progress handlers to the global EventBus once (called from loadAssets).
// missionDefs_ is empty until Milestone 8 loads data/missions.json, so these handlers are inert
// today -- they become live the moment content defines a matching objective, with zero coupling
// back into Battle/Item code (which only ever calls publish(), unchanged since Milestone 1).
void Game::pushNotification(const std::string& msg) {
    notifications_.push_back({msg, 3000});   // 3 seconds on screen
}

namespace {
// Milestone 5: local-date rollover for daily missions (architecture-doc §8.1's "local device
// midnight" default). Not itself unit-tested (wall-clock dependent) -- the pure logic it feeds,
// toms::rollDailyReset, already is (mission_test.cpp).
std::string todayDateStringLocal() {
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
} // namespace

void Game::rollDailyMissions() {
    std::string today = todayDateStringLocal();
    for (auto& [id, tracker] : missionTrackers_) {
        auto it = missionDefs_.find(id);
        if (it == missionDefs_.end() || it->second.kind != toms::MissionKind::Daily) continue;
        toms::MissionState before = tracker.state;
        toms::rollDailyReset(tracker, it->second, today);
        if (tracker.state == toms::MissionState::Available && before != toms::MissionState::Available)
            pushNotification(locale_.tr("mission.daily_reset_prefix") + id);
    }
}

void Game::wireMissionEvents() {
    toms::globalEventBus().subscribe<toms::EnemyDefeated>([this](const toms::EnemyDefeated& e) {
        for (auto& [id, tracker] : missionTrackers_) {
            auto it = missionDefs_.find(id);
            if (it != missionDefs_.end()) toms::applyProgressEvent(tracker, it->second, "defeat", e.enemyId);
        }
    });
    toms::globalEventBus().subscribe<toms::ItemCollected>([this](const toms::ItemCollected& e) {
        for (auto& [id, tracker] : missionTrackers_) {
            auto it = missionDefs_.find(id);
            if (it != missionDefs_.end()) toms::applyProgressEvent(tracker, it->second, "collect", e.itemId);
        }
    });
}

void Game::interact() {
    if (modalActive()) return;   // any modal overlay blocks world interaction
    // find NPC on player's cell or adjacent
    for (auto& e : st.entities) {
        if (e.consumed) continue;
        if (e.kind.rfind("npc:",0)!=0) continue;
        if (e.x==pl.x && e.y==pl.y) {
            std::string npc;
            if (e.id=="villager") npc="villager_elder";
            else if (e.id=="sorcerer") npc="sorcerer_teacher";
            else if (e.id=="king") npc="king_lieutenant";
            else if (e.id=="princess") npc=(curStage=="stage_11")?"princess_victory":"princess_liora";
            else if (e.id=="handmaiden") npc="handmaiden";
            else continue;
            startDialogue(npc);
            return;
        }
    }
}

toms::GameState Game::currentState() const {
    using toms::GameState;
    // Title phase (Boot): reported as MainMenu, with its Settings page mapping to
    // GameState::Settings — game_state.h's table already allows MainMenu <-> Settings.
    if (title_.isOpen())
        return (title_.page() == toms::TitlePage::Settings) ? GameState::Settings : GameState::MainMenu;
    if (cs.active) return GameState::DirectBattle;
    if (inDialogue) return GameState::Dialogue;
    if (storeOpen || storeUnlockDlg) return GameState::Merchant;
    return GameState::Explore;   // includes inventory-open, an overlay atop Explore
}

#ifndef __EMSCRIPTEN__
void Game::applyUiSettings() {
    ImGui::GetIO().FontGlobalScale = uiFontScale_;
}

void Game::drawDebugOverlay() {
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("TOMS Debug (F1 to toggle)");
    ImGui::Text("State: %s", toms::toString(currentState()));
    ImGui::Text("Stage: %s", curStage.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Player stats (hand-trace bestiary fights live)");
    ImGui::SliderInt("HP", &pl.hp, 0, pl.maxhp > 0 ? pl.maxhp : 999);
    ImGui::SliderInt("Max HP", &pl.maxhp, 1, 999);
    ImGui::SliderInt("ATK", &pl.atk, 0, 99);
    ImGui::SliderInt("DEF", &pl.def, 0, 99);
    ImGui::SliderInt("Level", &pl.lv, 1, 20);
    ImGui::SliderInt("EXP", &pl.exp, 0, 999);
    ImGui::SliderInt("Gold", &pl.gold, 0, 999);
    ImGui::Separator();
    if (ren) {
        static const char* nodeNames[] = { "All", "Stage", "Char", "Talk", "Battle", "Store" };
        static int nodeSel = 0;
        if (ImGui::Combo("Node filter (diagnostic)", &nodeSel, nodeNames, IM_ARRAYSIZE(nodeNames)))
            ren->setNodeFilter((uint8_t)nodeSel);
    }
    ImGui::Separator();
    ImGui::TextWrapped("Last combat log: %s", cs.log.empty() ? "(none)" : cs.log.c_str());
    ImGui::Text("Inventory: %d item(s)", (int)pl.inv.size());
    ImGui::End();
}

// M2 styling spike: an undecorated, transparent-background ImGui window positioned at the exact
// same rect as a scene-graph-drawn backdrop (drawStylingSpikeBackdrop(), called from draw()).
// If this reads as one seamless panel on screen rather than two overlapping things, it confirms
// the hybrid rendering approach Milestone 5 is about to commit real screens to.
void Game::drawStylingSpike() {
    if (!ren) return;
    float W = (float)ren->width(), H = (float)ren->height();
    float w = 420, h = 260;
    float x = (W - w) * 0.5f, y = (H - h) * 0.5f;
    stylingSpikeRect_[0] = x; stylingSpikeRect_[1] = y; stylingSpikeRect_[2] = w; stylingSpikeRect_[3] = h;
    stylingSpikeVisible_ = true;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("StylingSpike", nullptr, flags);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.7f, 1.0f), "M2 Styling Spike (F2 to toggle)");
    ImGui::TextWrapped("This text and button are drawn by ImGui with NoBackground. The panel "
                        "behind them is drawn by the scene-graph/renderer as a solid-tint quad "
                        "(standing in for real 9-slice art) at the exact same rect.");
    static int clicks = 0;
    if (ImGui::Button("Click me")) clicks++;
    ImGui::SameLine();
    ImGui::Text("clicks: %d", clicks);
    ImGui::End();
}

// Milestone 5: transient toast notifications, stacked top-right, each with its own fade-free
// fixed 3-second lifetime (see pushNotification()/update()'s countdown). Purely additive --
// nothing else reads/depends on this window.
void Game::drawNotifications() {
    if (notifications_.empty() || !ren) return;
    float W = (float)ren->width();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize;
    float y = 90.0f;   // below the HUD stat block and store icon
    for (size_t i = 0; i < notifications_.size(); i++) {
        ImGui::SetNextWindowPos(ImVec2(W - 260.0f, y), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.14f, 0.10f, 0.9f));
        std::string winId = "##toast" + std::to_string(i);
        ImGui::Begin(winId.c_str(), nullptr, flags);
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.6f, 1.0f), "%s", notifications_[i].first.c_str());
        ImGui::End();
        ImGui::PopStyleColor();
        y += 48.0f;
    }
}

// Milestone 5: the Stage Select hub -- every floor the player has ever reached is individually
// selectable; a locked stage shows why (architecture-doc §10). Tab to open (see main.cpp),
// blocks background input while open (modalActive() includes stageSelectOpen_).
void Game::drawStageSelect() {
    if (!ren) return;
    float W = (float)ren->width(), H = (float)ren->height();
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W - 120.0f, H - 100.0f), ImGuiCond_Always);
    ImGui::Begin(locale_.tr("stageselect.title").c_str(), nullptr,
                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("%s", locale_.tr("stageselect.hint").c_str());
    ImGui::Separator();
    // UI settings: font size, applied globally via applyUiSettings() (called every frame from
    // main.cpp). In-memory only for now -- see uiFontScale_'s declaration in game.h for why.
    if (ImGui::CollapsingHeader(locale_.tr("stageselect.ui_settings_header").c_str())) {
        ImGui::SliderFloat(locale_.tr("stageselect.font_size_label").c_str(), &uiFontScale_, 0.5f, 2.5f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::Button(locale_.tr("stageselect.reset_button").c_str())) uiFontScale_ = 1.5f;
    }
    ImGui::Separator();
    for (size_t i = 0; i < stageList_.size(); i++) {
        const StageInfo& info = stageList_[i];
        bool reached = std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), info.id) != meta_.unlockedStages.end();
        bool locked = false;
        if (info.index > 1 && i > 0) {
            const StageInfo& prev = stageList_[i - 1];
            locked = std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), prev.id) == meta_.unlockedStages.end();
        }
        bool isNew = !locked && !reached;

        ImGui::PushID((int)i);
        ImGui::BeginDisabled(locked);
        std::string label = std::to_string(info.index) + ". " + info.name;
        if (reached) label += "  " + locale_.tr("stageselect.reached_tag");
        if (isNew)   label += "  " + locale_.tr("stageselect.new_tag");
        if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
            loadStage(info.id);
            closeStageSelect();
        }
        ImGui::EndDisabled();
        // ImGuiHoveredFlags_AllowWhenDisabled: BeginDisabled() suppresses hover reporting by
        // default, so the lock-reason tooltip needs this explicit override to show at all.
        if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", trParam(locale_.tr("stageselect.locked_hint"), "floor", std::to_string(info.index - 1)).c_str());
        // Milestone 8: recommended-stats preview text, shown under every row that has one
        // (locked rows too -- it's exactly the info a player needs to decide whether to go grind
        // first, per the roadmap's own "Stage Select preview text (recommended stats)" ask).
        if (!info.preview.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.8f, 0.9f, 0.85f));
            ImGui::TextWrapped("  %s", info.preview.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    ImGui::End();
}
#endif

// Draws the backdrop rect for the M2 styling spike (see drawStylingSpike() above). Declared
// unguarded so Game::draw() -- shared between the desktop and web builds -- can call it
// unconditionally: stylingSpikeVisible_ can only ever become true via the desktop-only
// setStylingSpikeVisible()/drawStylingSpike(), so this is a correct no-op on the web build.
void Game::drawStylingSpikeBackdrop() {
    if (!stylingSpikeVisible_ || !ren) return;
    float x = stylingSpikeRect_[0], y = stylingSpikeRect_[1], w = stylingSpikeRect_[2], h = stylingSpikeRect_[3];
    // Outer rect (darker "border") + inset inner rect (lighter "fill") -- a cheap two-rect
    // stand-in for a real 9-slice panel; the point of this spike is the ImGui/scene-graph
    // alignment technique, not authoring actual 9-slice art.
    Quad outer; outer.rect[0]=x; outer.rect[1]=y; outer.rect[2]=w; outer.rect[3]=h;
    outer.uv[0]=0;outer.uv[1]=0;outer.uv[2]=1;outer.uv[3]=1; outer.solid=true;
    outer.tint[0]=0.15f; outer.tint[1]=0.13f; outer.tint[2]=0.22f; outer.tint[3]=0.96f;
    ren->drawSprite(outer);
    float inset = 6.0f;
    Quad inner; inner.rect[0]=x+inset; inner.rect[1]=y+inset; inner.rect[2]=w-2*inset; inner.rect[3]=h-2*inset;
    inner.uv[0]=0;inner.uv[1]=0;inner.uv[2]=1;inner.uv[3]=1; inner.solid=true;
    inner.tint[0]=0.10f; inner.tint[1]=0.09f; inner.tint[2]=0.16f; inner.tint[3]=0.96f;
    ren->drawSprite(inner);
}

// Shared by movePlayer() (bumping a monster tile) and debugStartNearestBattle() (verification
// harnesses): builds the EnemyInst from the entity and starts the fight -- or its dialogue gate.
void Game::engageMonster(const Entity& e) {
    EnemyInst en;
    auto& t = enemyTpl[e.id];
    en.id=e.id; en.name=locale_.field(t["name"]); en.hp=t["hp"]; en.atk=t["atk"]; en.def=t["def"];
    en.exp=t["exp"]; en.gold=t["gold"]; en.x=e.x; en.y=e.y; en.boss=t.value("boss",false);
    // Milestone 4 (Encounter Resolution): resolveEncounterKind returns DirectBattle for every
    // monster tile in every shipped stage today (no stage sets encounter_overrides yet), so this is
    // byte-for-byte the same behavior as before unless/until a stage opts a tile into dialogue_gate.
    toms::EncounterKind ek = toms::resolveEncounterKind(e.kind, e.encounterOverride);
    if (ek == toms::EncounterKind::DialogueGate) {
        pendingEncounterEnemy_ = en;
        hasPendingEncounter_ = true;
        dlgNpc = "enemy_" + e.id;   // so ChoiceMade{dlgNpc,...} carries the right id
        startDialogue(dlgNpc);
    } else {
        startCombat(en);
    }
}

// Verification hook for the browser build's deploy harness (exposed as jsDebugBattle in
// emscripten_main.cpp): start a fight with the nearest un-consumed monster so the battle scene and
// its input can be driven without walking the maze first. Returns true when a fight is running.
bool Game::debugStartNearestBattle() {
    if (cs.active) return true;
    int bestD = 1 << 30;
    const Entity* best = nullptr;
    for (const auto& e : st.entities) {
        if (e.consumed || e.kind.rfind("monster:", 0) != 0) continue;
        int d = std::abs(e.x - pl.x) + std::abs(e.y - pl.y);
        if (d < bestD) { bestD = d; best = &e; }
    }
    if (!best) return false;
    engageMonster(*best);
    return cs.active;
}

void Game::startCombat(const EnemyInst& e) {
    cs.enemy = e; cs.playerHP = pl.hp; cs.enemyHP = e.hp; cs.round = 0; cs.active = true; cs.won = false;
    cs.phase = CombatState::Phase::AwaitAttackPress;
    cs.charging = false; cs.chargeMs = 0; cs.resultPauseMs = 0;
    cs.lastPosition = 0.0f; cs.lastPower = 0.0f; cs.lastDamage = 0;
    cs.log = locale_.tr("battle.start");
}

// Shared win/lose resolution -- identical to the pre-Milestone-6 auto-combat's own win/lose
// handling (rewards, level-up, boss warp, respawn), just extracted so both
// resolveAttackRelease/resolveDefenseRelease can reach it without duplicating it.
void Game::finishCombatWin() {
    cs.active = false; cs.won = true;
    pl.hp = cs.playerHP;
    pl.gold += cs.enemy.gold; pl.exp += cs.enemy.exp;
    int need = pl.lv * 30;
    while (pl.exp >= need) {
        pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30;
        pushNotification(locale_.tr("battle.levelup") + std::to_string(pl.lv));
    }
    // remove monster entity from stage
    for (auto& e : st.entities) if (e.x==cs.enemy.x && e.y==cs.enemy.y && e.id==cs.enemy.id) e.consumed=true;
    st.tiles[cs.enemy.y][cs.enemy.x] = '.';
    // Milestone 7: persist the clear so it survives a reload of this floor (stairs or the Stage
    // Select hub) -- see entityStatus_'s declaration in game.h.
    toms::setEntityStatus(entityStatus_, toms::entityStatusKey(curStage, cs.enemy.x, cs.enemy.y), toms::EntityStatus::Defeated);
    toms::globalEventBus().publish(toms::EnemyDefeated{cs.enemy.id, curStage});
    if (cs.enemy.boss) { cs.log = locale_.tr("battle.boss_win"); loadStage("stage_11"); }
    markProgressDirty();   // gold/exp/level/entity-status all changed: autosave will flush it
}

void Game::finishCombatLose() {
    cs.active = false; cs.won = false;
    pl.hp = pl.maxhp/2; // respawn at stage start
    loadStage(curStage); // reset monsters/items
    markProgressDirty();
}

// Attack Bar released: apply damage to the enemy (FIGHT_SCENE_DESIGN.md §4's power_mult curve,
// with the equipped weapon's maxMult and Berserker's Red-zone-deals-0 rule per
// MAIN_BATTLE_SCENE_DESIGN.md §4.2). If this kills the enemy, the round ends here -- no Defense
// Bar this round, matching "the enemy retaliates after every hit except the killing blow."
void Game::resolveAttackRelease(float heldSeconds) {
    toms::PowerBarParams bar = toms::effectiveAttackBar(equipped_, equipmentDefs_);
    float pos = toms::simulatePosition(bar, heldSeconds);
    float power = toms::powerFromPosition(bar, pos);
    float maxMult = toms::effectiveMaxMult(equipped_, equipmentDefs_);
    // Milestone 8: applyEquipmentStats() (built and tested in Milestone 6) was never actually
    // called anywhere -- a weapon's flat ATK bonus (or a speed-build's ATK penalty) only ever
    // affected the Power Bar's geometry/maxMult, never the base damage number itself.
    int effAtk = pl.atk, effDef = pl.def;
    toms::applyEquipmentStats(equipped_, equipmentDefs_, effAtk, effDef);
    int baseHit = std::max(1, effAtk - cs.enemy.def);
    int dmg = toms::computeAttackDamage(baseHit, power, maxMult);
    if (toms::zoneFromPosition(bar, pos) == toms::PowerZone::Red && toms::talentZerosRedZoneAttacks(equipped_.talentId))
        dmg = 0;   // Berserker: a high ceiling, but Red-zone releases deal nothing

    cs.enemyHP -= dmg;
    cs.lastPosition = pos; cs.lastPower = power; cs.lastDamage = dmg;
    cs.round++;
    audio.play("player_attack");

    if (cs.enemyHP <= 0) {
        cs.log = trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg));
        finishCombatWin();
    } else {
        cs.log = trParam(locale_.tr("battle.hit"), "dmg", std::to_string(dmg));
    }
    cs.phase = CombatState::Phase::AttackResultPause;
    cs.resultPauseMs = 300;
}

// Defense Bar released: apply damage to the player (mitigation curve, Perfect Guard, and
// Guardian's flat mitigation floor per MAIN_BATTLE_SCENE_DESIGN.md §4.2).
void Game::resolveDefenseRelease(float heldSeconds) {
    toms::PowerBarParams bar = toms::effectiveDefenseBar(equipped_, equipmentDefs_);
    float pos = toms::simulatePosition(bar, heldSeconds);
    float power = toms::powerFromPosition(bar, pos);
    int effAtk = pl.atk, effDef = pl.def;
    toms::applyEquipmentStats(equipped_, equipmentDefs_, effAtk, effDef);
    int incoming = std::max(1, cs.enemy.atk - effDef);
    float floorMitigation = toms::talentDefenseMitigationFloor(equipped_.talentId);
    int dmg = toms::computeDefenseDamage(incoming, power, floorMitigation);

    cs.playerHP -= dmg;
    cs.lastPosition = pos; cs.lastPower = power; cs.lastDamage = dmg;
    if (dmg > 0) audio.play("enemy_attack");

    if (cs.playerHP <= 0) {
        cs.log = locale_.tr("battle.player_down");
        finishCombatLose();
    } else {
        cs.log = dmg == 0 ? locale_.tr("battle.perfect_guard") : trParam(locale_.tr("battle.guard_hit"), "dmg", std::to_string(dmg));
    }
    cs.phase = CombatState::Phase::DefenseResultPause;
    cs.resultPauseMs = 300;
}

void Game::battleChargeStart() {
    if (!cs.active || cs.charging) return;
    if (cs.phase == CombatState::Phase::AwaitAttackPress || cs.phase == CombatState::Phase::AwaitDefensePress) {
        cs.charging = true;
        cs.chargeMs = 0;
        cs.phase = (cs.phase == CombatState::Phase::AwaitAttackPress) ? CombatState::Phase::AttackCharging
                                                                       : CombatState::Phase::DefenseCharging;
    }
    // Ignored during {Attack,Defense}Charging (already charging) or a ResultPause (must wait
    // for the readability beat to finish) -- matches the phase diagram in game.h's comment.
}

void Game::battleChargeRelease() {
    if (!cs.active || !cs.charging) return;
    cs.charging = false;
    float heldSeconds = cs.chargeMs / 1000.0f;
    if (cs.phase == CombatState::Phase::AttackCharging) resolveAttackRelease(heldSeconds);
    else if (cs.phase == CombatState::Phase::DefenseCharging) resolveDefenseRelease(heldSeconds);
}

// =====================================================================================
// Title phase — New Game / Continue / Settings (see src/game/title_screen.h)
// =====================================================================================

// "HH:MM:SS" for the Continue list's play-time column.
static std::string fmtPlayTime(int sec) {
    if (sec < 0) sec = 0;
    char b[32];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    return std::string(b);
}

std::string Game::stageDisplayName(const std::string& id) const {
    // Match either identifier: the game loads stages by FILE STEM ("stage01"), while the id
    // inside the JSON is "stage_01" -- so a pure id comparison silently missed and the Continue
    // list showed the raw stem instead of the stage's real name.
    for (const auto& s : stageList_)
        if (s.id == id || s.fileStem == id) return s.name.empty() ? id : s.name;
    return id;
}

// Snapshots the live game state into the Run-save schema (save_system.h) for writing to a slot.
toms::RunSaveData Game::runSaveFromState() const {
    toms::RunSaveData r;
    r.currentStageId = curStage;
    r.player.hp = pl.hp; r.player.maxhp = pl.maxhp;
    r.player.atk = pl.atk; r.player.def = pl.def;
    r.player.gold = pl.gold; r.player.exp = pl.exp; r.player.lv = pl.lv;
    r.player.key_yellow = pl.key_yellow; r.player.key_blue = pl.key_blue; r.player.key_red = pl.key_red;
    r.player.inv = pl.inv;
    r.entityStatus = entityStatus_;
    return r;
}

void Game::saveCurrentRun() {
    if (activeSlot_ <= 0) return;          // no slot chosen yet (title still up at boot)
    ensureStageListLoaded();
    const std::string dir = toms::defaultSaveDir();
    if (toms::writeSlotSave(dir, activeSlot_, meta_, runSaveFromState(), playTimeSec_,
                            toms::nowStamp(), stageDisplayName(curStage))) {
        saveDirty_ = false;
    }
    if (title_.isOpen()) refreshSlots();
}

void Game::refreshSlots() {
    std::vector<toms::SlotSummary> v;
    const std::string dir = toms::defaultSaveDir();
    for (int i = 1; i <= title_.slotCount(); i++) v.push_back(toms::summarizeSlot(dir, i));
    title_.setSummaries(std::move(v));
}

void Game::applyLanguage() {
    // The Settings page owns the selection -- mirror it into the persisted preference. Reading
    // it back from title_ (rather than re-applying the old settings_.language) is what makes
    // the choice actually stick: without this the freshly-picked language was overwritten by
    // the stale preference on the very same call, so the UI snapped back.
    const int idx = title_.languageIndex();
    if (idx >= 0 && idx < locale_.languageCount())
        settings_.language = locale_.languages()[idx].code;
    locale_.setLanguage(settings_.language);
    settings_.language = locale_.languageCode();   // normalize (e.g. an unknown code -> default)
    title_.setLanguageIndex(locale_.languageIndex());
    toms::ensureSaveDir(toms::defaultSaveDir());
    toms::saveGameSettings(toms::defaultSaveDir() + "/settings.json", settings_);
}

void Game::applyLoadedRun(const toms::MetaSaveData& m, const toms::RunSaveData& r,
                          int slot, int playSec) {
    meta_ = m;
    entityStatus_ = r.entityStatus;
    // Anything that belongs to the *previous* run must not leak into the loaded one.
    missionTrackers_.clear();
    notifications_.clear();
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    invOpen = false; storeOpen = false; storeUnlockDlg = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    pl.hp = r.player.hp; pl.maxhp = r.player.maxhp;
    pl.atk = r.player.atk; pl.def = r.player.def;
    pl.gold = r.player.gold; pl.exp = r.player.exp; pl.lv = r.player.lv;
    pl.key_yellow = r.player.key_yellow; pl.key_blue = r.player.key_blue; pl.key_red = r.player.key_red;
    pl.inv = r.player.inv;
    activeSlot_ = slot;
    playTimeSec_ = playSec;
    loadStage(r.currentStageId.empty() ? std::string("stage01") : r.currentStageId);
    title_.close();
    title_.setRunInProgress(true);
}

bool Game::continueFromSlot(int slot) {
    if (slot <= 0) return false;
    toms::MetaSaveData m;
    toms::RunSaveData r;
    int playSec = 0;
    if (!toms::readSlotSave(toms::defaultSaveDir(), slot, m, r, &playSec, nullptr)) {
        // Deliberately silent: the Continue row already reads "[空]/[Empty]" and the page shows
        // "no saves yet -- start a new game first", and the toast path draws with ImGui's
        // default font, which has no CJK glyphs (it would render as a "?" box). The title stays
        // open, so there is nothing to resume from.
        refreshSlots();
        return false;
    }
    applyLoadedRun(m, r, slot, playSec);
    return true;
}

void Game::newGame() { newGame(0); }

void Game::newGame(int slot) {
    // A new game is a genuinely fresh run: default stats and wiped story/entity/meta progress.
    pl = Player();
    pl.maxhp = 120; pl.hp = 120; pl.atk = 12; pl.def = 4; pl.gold = 0; pl.exp = 0; pl.lv = 1;
    pl.inv = {"potion_red", "potion_blue", "exp_up"};
    pl.x = 1; pl.y = 1;
    entityStatus_.clear();
    missionTrackers_.clear();
    meta_ = toms::MetaSaveData{};
    notifications_.clear();
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    invOpen = false; storeOpen = false; storeUnlockDlg = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    playTimeSec_ = 0;
    if (slot > 0) {
        // Explicit slot (the Continue page's "start a new game in this empty slot?" answer).
        activeSlot_ = slot;
    } else {
        // Prefer a free slot; when every slot is taken, reuse slot 1 rather than refusing to
        // start (the Continue page still lists all of them, so nothing is silently destroyed).
        int free = toms::firstEmptySlot(toms::defaultSaveDir(), title_.slotCount());
        activeSlot_ = (free > 0) ? free : 1;
    }
    loadStage("stage01");
    saveCurrentRun();
    title_.close();
    title_.setRunInProgress(true);
}

void Game::handleTitleAction(toms::TitleAction a) {
    switch (a) {
        case toms::TitleAction::NewGame:             newGame(); break;
        case toms::TitleAction::StartNewGameInSlot:  newGame(title_.pendingSlot()); break;
        case toms::TitleAction::LoadSlot:            continueFromSlot(title_.pendingSlot()); break;
        case toms::TitleAction::OpenContinue:        refreshSlots(); break;   // always show current files
        case toms::TitleAction::SetLanguage:         applyLanguage(); break;
        // The confirm dialogs are drawn and answered by the title itself; these are informational.
        case toms::TitleAction::AskNewGameInSlot:
        case toms::TitleAction::DismissNewGameConfirm:
        case toms::TitleAction::AskLanguageChange:
        case toms::TitleAction::DismissLanguageConfirm:
        case toms::TitleAction::OpenSettings:
        case toms::TitleAction::Back:
        case toms::TitleAction::None:                break;
    }
}

void Game::titleMove(int dx, int dy) {
    if (!title_.isOpen()) return;
    toms::TitleAction a = toms::TitleAction::None;
    if (dy != 0) a = title_.moveVertical(dy);
    if (dx != 0) {
        toms::TitleAction h = title_.moveHorizontal(dx);   // Settings: left/right switches language
        if (a == toms::TitleAction::None) a = h;
    }
    handleTitleAction(a);
}

void Game::titleConfirm() {
    if (title_.isOpen()) handleTitleAction(title_.activate());
}

void Game::titleCancel() {
    if (title_.isOpen()) handleTitleAction(title_.cancel());
}

void Game::titleClick(float x, float y) {
    if (!title_.isOpen()) return;
    handleTitleAction(title_.click(x, y, titleLayout_));
}

// One title row: framed panel + left accent bar + label (+ optional dim sub-label), with the
// highlight reserved for the row the cursor is on. Solid-tint quads only -- the title screen
// ships no art, so it renders identically on every backend for free.
void Game::drawTitleButton(const toms::TitleRow& r, const std::string& label, const std::string& sub,
                           bool selected, const float accent[4], float pulse) {
    static const float hi[4]  = {1.00f, 0.97f, 0.86f, 1.0f};
    static const float dflt[4]= {0.86f, 0.88f, 0.94f, 1.0f};
    static const float dim[4] = {0.60f, 0.64f, 0.76f, 1.0f};

    // Subtle breathing on the selected row (panel brightness, accent alpha, cursor slide) so the
    // screen is visibly live even when nothing is being pressed -- a static title reads as a crash.
    const float glow = selected ? (0.90f + 0.18f * pulse) : 1.0f;
    const float accentA = selected ? (0.68f + 0.32f * pulse) : 0.35f;

    Quad panel;
    panel.rect[0] = r.x; panel.rect[1] = r.y; panel.rect[2] = r.w; panel.rect[3] = r.h;
    panel.uv[0] = 0; panel.uv[1] = 0; panel.uv[2] = 1; panel.uv[3] = 1;
    panel.solid = true;
    panel.tint[0] = (selected ? 0.20f : 0.11f) * glow;
    panel.tint[1] = (selected ? 0.21f : 0.12f) * glow;
    panel.tint[2] = (selected ? 0.30f : 0.18f) * glow;
    panel.tint[3] = 0.96f;
    ren->drawSprite(panel);

    Quad bar;
    bar.rect[0] = r.x; bar.rect[1] = r.y; bar.rect[2] = 6.0f; bar.rect[3] = r.h;
    bar.uv[0] = 0; bar.uv[1] = 0; bar.uv[2] = 1; bar.uv[3] = 1;
    bar.solid = true;
    bar.tint[0] = accent[0]; bar.tint[1] = accent[1]; bar.tint[2] = accent[2];
    bar.tint[3] = accentA;
    ren->drawSprite(bar);

    const float labelSize = 26.0f;
    const bool hasSub = !sub.empty();
    const float ly = hasSub ? r.y + 7.0f : r.y + (r.h - labelSize) * 0.5f;
    drawText(label, r.x + 22.0f, ly, labelSize, selected ? hi : dflt);
    if (hasSub) drawText(sub, r.x + 22.0f, ly + labelSize + 1.0f, 15.0f, dim);
    if (selected) {
        float cursor[4] = { accent[0], accent[1], accent[2], accentA };
        drawText(">", r.x - 26.0f - 6.0f * pulse, ly, labelSize, cursor);
    }
}

void Game::drawTitleScreen() {
    if (!ren) return;
    const float W = (float)ren->width(), H = (float)ren->height();
    // Same layout function titleClick() hit-tests against -> a tap always lands on what was drawn.
    titleLayout_ = toms::computeTitleLayout((int)W, (int)H, title_.slotCount(), locale_.languageCount());

    // Animation clock for the whole page: a slow ~1.4 s pulse drives the selected row's glow, its
    // accent alpha and the sliding cursor, and gently breathes the two frame rules. Without it the
    // title screen is a perfectly static image, which players read as "it crashed".
    const float pulse = 0.5f + 0.5f * std::sin(titleAnimMs_ * 0.0045f);

    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};

    // Full-screen backdrop + two horizontal rules: a framed look with no art assets.
    Quad bg;
    bg.rect[0] = 0; bg.rect[1] = 0; bg.rect[2] = W; bg.rect[3] = H;
    bg.uv[0] = 0; bg.uv[1] = 0; bg.uv[2] = 1; bg.uv[3] = 1;
    bg.solid = true;
    bg.tint[0] = 0.05f; bg.tint[1] = 0.05f; bg.tint[2] = 0.09f; bg.tint[3] = 1.0f;
    ren->drawSprite(bg);

    Quad rule;
    rule.rect[0] = 0; rule.rect[2] = W; rule.rect[3] = 2.0f;
    rule.uv[0] = 0; rule.uv[1] = 0; rule.uv[2] = 1; rule.uv[3] = 1;
    rule.solid = true;
    rule.tint[0] = gold[0]; rule.tint[1] = gold[1]; rule.tint[2] = gold[2];
    rule.tint[3] = 0.40f + 0.30f * pulse;
    rule.rect[1] = 74.0f;  ren->drawSprite(rule);
    rule.rect[1] = H - 62.0f; ren->drawSprite(rule);

    const std::string title = locale_.tr("game.title");
    float tw = measureText(title, 52);
    drawText(title, (W - tw) * 0.5f, 92.0f, 52, gold);
    const std::string subtitle = locale_.tr("game.subtitle");
    float sw = measureText(subtitle, 17);
    drawText(subtitle, (W - sw) * 0.5f, 152.0f, 17, dim);

    switch (title_.page()) {
        case toms::TitlePage::Menu: {
            static const char* keys[3] = {"menu.new_game", "menu.continue", "menu.settings"};
            static const char* desc[3] = {"menu.new_game.desc", "menu.continue.desc", "menu.settings.desc"};
            for (int i = 0; i < toms::TitleLayout::kMaxMenuRows; i++)
                drawTitleButton(titleLayout_.menuRow[i], locale_.tr(keys[i]), locale_.tr(desc[i]),
                                title_.menuSelection() == i, gold, pulse);
            break;
        }
        case toms::TitlePage::Continue: {
            // The renderer draws ALL sprites first, then ALL text glyphs in one later pass
            // (see WebGLRenderer::end()/flush() -- two texture batches, sprites then text), so
            // text can never be visually covered by a later sprite (the dialog's scrim/panel):
            // it always ends up on top. Skip drawing the rows this dialog would otherwise sit
            // on top of, rather than relying on the scrim to hide their labels.
            if (!title_.newGameConfirmOpen()) {
                const std::string head = locale_.tr("continue.header");
                drawText(head, (W - measureText(head, 30)) * 0.5f, 196.0f, 30, gold);
                const auto& sums = title_.summaries();
                bool any = false;
                for (int i = 0; i < titleLayout_.slotRowCount; i++) {
                    std::string label = locale_.tr("continue.slot") + " " + std::to_string(i + 1);
                    std::string sub;
                    if (i < (int)sums.size() && sums[i].exists) {
                        const toms::SlotSummary& s = sums[i];
                        any = true;
                        label += "   " + (s.stageName.empty() ? s.stageId : s.stageName)
                               + "   " + locale_.tr("hud.level") + " " + std::to_string(s.lv)
                               + "   HP " + std::to_string(s.hp) + "/" + std::to_string(s.maxhp)
                               + "   " + std::to_string(s.gold) + "G";
                        sub = locale_.tr("continue.saved_at") + " " + s.savedAt
                            + "    " + locale_.tr("continue.play_time") + " " + fmtPlayTime(s.playTimeSec);
                    } else {
                        label += "   [" + locale_.tr("continue.empty") + "]";
                    }
                    drawTitleButton(titleLayout_.slotRow[i], label, sub,
                                    title_.slotSelection() == i, gold, pulse);
                }
                if (!any) {
                    const std::string hint = locale_.tr("continue.hint");
                    drawText(hint, (W - measureText(hint, 18)) * 0.5f, H - 178.0f, 18, dim);
                }
                drawTitleButton(titleLayout_.backButton, locale_.tr("menu.back"), "", false, dim, pulse);
            }
            break;
        }
        case toms::TitlePage::Settings: {
            // Same reasoning as Continue above: hide the rows the language-confirm dialog
            // would otherwise (ineffectively) sit on top of.
            if (!title_.languageConfirmOpen()) {
                const std::string head = locale_.tr("settings.header");
                drawText(head, (W - measureText(head, 30)) * 0.5f, 262.0f, 30, gold);
                const std::string langLabel = locale_.tr("settings.language");
                drawText(langLabel, (W - measureText(langLabel, 20)) * 0.5f, 300.0f, 20, dim);
                const auto& langs = locale_.languages();
                for (int i = 0; i < titleLayout_.langRowCount && i < (int)langs.size(); i++) {
                    const bool active = (i == locale_.languageIndex());
                    std::string label = std::string(active ? "[x] " : "[ ] ") + langs[i].name;
                    drawTitleButton(titleLayout_.langRow[i], label, "",
                                    title_.settingsSelection() == i, gold, pulse);
                }
                // Touch/mobile has no Esc key -- without a drawn Back button, Settings was a
                // dead end on the web build (the hit-test already worked, it was just invisible).
                drawTitleButton(titleLayout_.backButton, locale_.tr("menu.back"), "", false, dim, pulse);
            }
            break;
        }
    }

    const std::string hint = (title_.page() == toms::TitlePage::Settings)
                             ? locale_.tr("settings.hint") : locale_.tr("menu.hint");
    drawText(hint, (W - measureText(hint, 16)) * 0.5f, H - 44.0f, 16, dim);

    // The confirm prompts are the topmost thing on the title (see drawTitleConfirmDialog() /
    // drawLanguageConfirmDialog()). The two pages they belong to are mutually exclusive, so at
    // most one of these is ever open at once.
    if (title_.newGameConfirmOpen()) drawTitleConfirmDialog();
    if (title_.languageConfirmOpen()) drawLanguageConfirmDialog();
}

// "Use this slot to start the game?" -- shown when an EMPTY Continue slot is activated, so the
// button never just does nothing. Yes starts a fresh run in that specific slot.
void Game::drawTitleConfirmDialog() {
    if (!ren || !title_.newGameConfirmOpen()) return;
    const float W = (float)ren->width();
    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};

    // Dim the list behind the prompt so it reads as modal.
    Quad scrim;
    scrim.rect[0] = 0; scrim.rect[1] = 0; scrim.rect[2] = W; scrim.rect[3] = (float)ren->height();
    scrim.uv[0] = 0; scrim.uv[1] = 0; scrim.uv[2] = 1; scrim.uv[3] = 1;
    scrim.solid = true;
    scrim.tint[0] = 0.0f; scrim.tint[1] = 0.0f; scrim.tint[2] = 0.0f; scrim.tint[3] = 0.62f;
    ren->drawSprite(scrim);

    const toms::TitleRow& box = titleLayout_.confirmBox;
    Quad panel;
    panel.rect[0] = box.x; panel.rect[1] = box.y; panel.rect[2] = box.w; panel.rect[3] = box.h;
    panel.uv[0] = 0; panel.uv[1] = 0; panel.uv[2] = 1; panel.uv[3] = 1;
    panel.solid = true;
    panel.tint[0] = 0.13f; panel.tint[1] = 0.14f; panel.tint[2] = 0.21f; panel.tint[3] = 0.98f;
    ren->drawSprite(panel);

    Quad rule;
    rule.rect[0] = box.x; rule.rect[1] = box.y; rule.rect[2] = box.w; rule.rect[3] = 3.0f;
    rule.uv[0] = 0; rule.uv[1] = 0; rule.uv[2] = 1; rule.uv[3] = 1;
    rule.solid = true;
    rule.tint[0] = gold[0]; rule.tint[1] = gold[1]; rule.tint[2] = gold[2]; rule.tint[3] = 0.9f;
    ren->drawSprite(rule);

    // "{slot}" placeholder substitution -- keeps the wording in data/text.json instead of
    // hardcoding the sentence per language here.
    std::string question = trParam(locale_.tr("continue.new_game_confirm"), "slot",
                                    std::to_string(title_.newGameConfirmSlot()));
    drawText(question, box.x + (box.w - measureText(question, 26)) * 0.5f, box.y + 34.0f, 26, gold);

    const std::string body = locale_.tr("continue.new_game_confirm.body");
    drawText(body, box.x + (box.w - measureText(body, 16)) * 0.5f, box.y + 86.0f, 16, dim);

    const bool yes = title_.newGameConfirmYesSelected();
    const float pulse = 0.5f + 0.5f * std::sin(titleAnimMs_ * 0.0045f);   // same live pulse as the list
    drawTitleButton(titleLayout_.confirmYes, locale_.tr("menu.yes"), "", yes, gold, pulse);
    drawTitleButton(titleLayout_.confirmNo,  locale_.tr("menu.no"),  "", !yes, gold, pulse);
}

// "Switch to XXX language?" -- shown when a language row is activated/tapped, instead of
// applying it instantly (a single mis-tap used to have no way back on a touch device with no
// keyboard/Esc). Reuses the same confirmBox/confirmYes/confirmNo layout as
// drawTitleConfirmDialog(): the two dialogs only ever appear on different pages (Continue vs.
// Settings), so they never need to coexist.
void Game::drawLanguageConfirmDialog() {
    if (!ren || !title_.languageConfirmOpen()) return;
    const float W = (float)ren->width();
    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};

    Quad scrim;
    scrim.rect[0] = 0; scrim.rect[1] = 0; scrim.rect[2] = W; scrim.rect[3] = (float)ren->height();
    scrim.uv[0] = 0; scrim.uv[1] = 0; scrim.uv[2] = 1; scrim.uv[3] = 1;
    scrim.solid = true;
    scrim.tint[0] = 0.0f; scrim.tint[1] = 0.0f; scrim.tint[2] = 0.0f; scrim.tint[3] = 0.62f;
    ren->drawSprite(scrim);

    const toms::TitleRow& box = titleLayout_.confirmBox;
    Quad panel;
    panel.rect[0] = box.x; panel.rect[1] = box.y; panel.rect[2] = box.w; panel.rect[3] = box.h;
    panel.uv[0] = 0; panel.uv[1] = 0; panel.uv[2] = 1; panel.uv[3] = 1;
    panel.solid = true;
    panel.tint[0] = 0.13f; panel.tint[1] = 0.14f; panel.tint[2] = 0.21f; panel.tint[3] = 0.98f;
    ren->drawSprite(panel);

    Quad rule;
    rule.rect[0] = box.x; rule.rect[1] = box.y; rule.rect[2] = box.w; rule.rect[3] = 3.0f;
    rule.uv[0] = 0; rule.uv[1] = 0; rule.uv[2] = 1; rule.uv[3] = 1;
    rule.solid = true;
    rule.tint[0] = gold[0]; rule.tint[1] = gold[1]; rule.tint[2] = gold[2]; rule.tint[3] = 0.9f;
    ren->drawSprite(rule);

    // Name the target language in ITS OWN endonym (matches the row label's own style, e.g.
    // "日本語" not "Japanese") so it reads correctly regardless of the CURRENT UI language.
    const auto& langs = locale_.languages();
    int idx = title_.languageConfirmIndex();
    std::string langName = (idx >= 0 && idx < (int)langs.size()) ? langs[idx].name : "";
    std::string question = trParam(locale_.tr("settings.language_confirm"), "lang", langName);
    drawText(question, box.x + (box.w - measureText(question, 24)) * 0.5f, box.y + 70.0f, 24, gold);

    const bool yes = title_.languageConfirmYesSelected();
    const float pulse = 0.5f + 0.5f * std::sin(titleAnimMs_ * 0.0045f);
    drawTitleButton(titleLayout_.confirmYes, locale_.tr("menu.yes"), "", yes, gold, pulse);
    drawTitleButton(titleLayout_.confirmNo,  locale_.tr("menu.no"),  "", !yes, gold, pulse);
}

void Game::update(int dtMs) {
    // Title-screen animation clock: advances in every state (the title is drawn long before any
    // gameplay exists) and wraps so a float never drifts into precision loss on a long session.
    titleAnimMs_ += (float)dtMs;
    if (titleAnimMs_ > 3600000.0f) titleAnimMs_ -= 3600000.0f;
    // Title phase: the run's clock only advances while actually playing, and a changed run is
    // flushed to its slot on a throttle (see kAutosaveIntervalMs) rather than on every event --
    // one atomic write per few seconds instead of one per pickup.
    if (title_.isOpen()) {
        if (saveDirty_ && !title_.runInProgress()) saveDirty_ = false;   // nothing to save yet
    } else {
        playTimeSec_ += dtMs / 1000;
        if (saveDirty_) {
            saveFlushMs_ += dtMs;
            if (saveFlushMs_ >= kAutosaveIntervalMs) { saveFlushMs_ = 0; saveCurrentRun(); }
        } else {
            saveFlushMs_ = 0;
        }
    }
    if (cs.active && cs.charging) cs.chargeMs += dtMs;
    // The result-pause countdown must run regardless of cs.active: a release that ends the
    // fight (win or lose) sets cs.active=false in the SAME call that starts the pause, so
    // gating this on cs.active would freeze resultPauseMs forever and leave a stale "倒下"
    // message stuck in cs.log (which -- see showBattle's condition -- would keep the battle
    // overlay stuck open permanently after a loss).
    if (cs.resultPauseMs > 0) {
        cs.resultPauseMs -= dtMs;
        if (cs.resultPauseMs <= 0) {
            cs.resultPauseMs = 0;
            if (!cs.active) {
                // Combat just ended. A win keeps its victory text showing until the player
                // dismisses it (cs.won, cleared elsewhere on tap/interact); a loss clears its
                // message now, once the brief result-pause has had a chance to show it, so
                // showBattle's "log contains 倒下" condition stops holding the battle overlay
                // open. (The pre-Milestone-6 code set and immediately cleared this same string
                // within one synchronous call, so it was never actually visible -- this is a
                // small, deliberate improvement, not an accidental behavior change: see
                // docs/PROGRESS_REPORT.md's Milestone 6 log entry.)
                if (!cs.won) cs.log = "";
            } else if (cs.phase == CombatState::Phase::AttackResultPause) {
                cs.phase = CombatState::Phase::AwaitDefensePress;
            } else if (cs.phase == CombatState::Phase::DefenseResultPause) {
                cs.phase = CombatState::Phase::AwaitAttackPress;
            }
        }
    }
    // store UI timers (toast / shake) tick down regardless of combat
    if (toastTimer_ > 0) { toastTimer_ -= dtMs; if (toastTimer_ < 0) toastTimer_ = 0; }
    if (shakeTimer_ > 0) { shakeTimer_ -= dtMs; if (shakeTimer_ < 0) shakeTimer_ = 0; }
    // Milestone 5: notification toasts tick down and expire.
    for (auto& n : notifications_) n.second -= dtMs;
    notifications_.erase(
        std::remove_if(notifications_.begin(), notifications_.end(),
                        [](const std::pair<std::string,int>& n) { return n.second <= 0; }),
        notifications_.end());
    // Milestone 9 polish: "keep moving while held" -- the actual repeat timer, driven from
    // here regardless of which input (keyboard or the virtual/touch d-pad) is holding a
    // direction; see setMoveHeldX/Y's declaration in game.h. Each axis repeats independently
    // so a held diagonal (e.g. up+right) keeps moving diagonally. movePlayer() is already a
    // safe no-op while modalActive(), so ticking this even during a dialogue/battle/etc. that
    // started mid-hold is harmless.
    auto tickMoveAxis = [&](MoveHoldAxis& a, int dx, int dy) {
        if (a.dir == 0) return;
        a.holdMs += dtMs;
        int threshold = a.repeating ? kMoveRepeatMs : kMoveInitialDelayMs;
        if (a.holdMs >= threshold) {
            movePlayer(dx, dy);
            a.holdMs -= threshold;
            a.repeating = true;
        }
    };
    tickMoveAxis(moveHoldX_, moveHoldX_.dir, 0);
    tickMoveAxis(moveHoldY_, 0, moveHoldY_.dir);
}

void Game::saveFrame(const std::string& path) {
    ren->savePNG(path);
}
