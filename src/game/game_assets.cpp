// game_assets.cpp — asset/sprite/stage loading: texture atlas, sprite id order lookups,
// and stage JSON -> Stage grid (+entity placement). Split out of game.cpp 2026-09-13.
#include "game_internal.h"

// Exactly one translation unit in this binary may define the STB image implementation. It lived in
// game.cpp before the split; texture.cpp carries its own copy for the texture_test target only.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace toms::game_detail;

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

    // S1: character-level 佔格 table (docs/ART_AND_ABILITY_DESIGN.md F8). An absent file leaves
    // every entity at 1x1, i.e. exactly the pre-S1 behavior -- the table is purely additive.
    {
        nlohmann::json fpj = readJsonFile(assetDir + "/../data/footprints.json");
        if (fpj.is_object()) {
            int n = 0;
            for (auto it = fpj.begin(); it != fpj.end(); ++it) {
                if (it.key().empty() || it.key()[0] == '_') continue;   // "_comment"
                typeFootprints_[it.key()] = toms::footprintSpecFromJson(it.value(), it.key());
                if (typeFootprints_[it.key()].fp.big()) ++n;
            }
            fprintf(stderr, "[assets] footprints.json: %d type(s) bigger than 1x1\n", n);
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
        cam_.setMode(toms::Camera::modeFromIndex(settings_.cameraMode));
        cam_.setViewCols(settings_.viewCols);
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
    // S1: resolve every entity's 佔格 now that both sources are known: the stage file's own
    // "footprints" entry (a per-floor decision) beats data/footprints.json (the character's tier),
    // which beats 1x1. Done here rather than in parseStage() because parseStage() knows nothing
    // about the type table -- and routed through the shared helper so footprint_test.cpp's data
    // validator resolves exactly the way the game does at runtime.
    int bigCount = 0;
    for (auto& e : st.entities) {
        auto it = typeFootprints_.find(e.kind);
        toms::FootprintSpec typeSpec = (it == typeFootprints_.end()) ? toms::FootprintSpec{} : it->second;
        toms::FootprintSpec spec = toms::resolveFootprint(
            toms::FootprintSpec{e.fp, e.roamer, e.displayName}, e.fpExplicit, typeSpec);
        e.fp = spec.fp;
        e.roamer = spec.roamer;
        e.displayName = spec.name;
        if (e.fp.big()) ++bigCount;
    }
    if (bigCount)
        fprintf(stderr, "[stage] %s: %d entity(ies) occupy more than 1 grid\n", st.id.c_str(), bigCount);

    // S1: (re)build the roamer brains for this floor. Always cleared -- the entity vector was just
    // rebuilt from JSON, so a stale index would drive a different monster. Seeds are derived from
    // the stage id + entity index, never from the clock: reloading a floor (stairs up/down, Stage
    // Select) reproduces the same wander, so a chase stays reproducible instead of being noise.
    roamers_.clear();
    for (int i = 0; i < (int)st.entities.size(); ++i) {
        const Entity& e = st.entities[i];
        if (!e.roamer || e.consumed) continue;
        uint32_t h = 2166136261u;   // FNV-1a over the stage id
        for (char ch : st.id) { h ^= (unsigned char)ch; h *= 16777619u; }
        roamers_.emplace_back(i, toms::Roamer(e.x, e.y, e.fp, h ^ (uint32_t)(i * 2654435761u)));
        fprintf(stderr, "[stage] roamer '%s' (%dx%d) at (%d,%d) on %s\n",
                e.id.c_str(), e.fp.w, e.fp.h, e.x, e.y, st.id.c_str());
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
    // A floor change must never visibly pan in from wherever the camera was on the PREVIOUS
    // floor (different grid, different scale of "makes sense") -- jump straight to the new
    // floor's starting view instead of easing into it.
    cam_.snap();
}
