// game_assets.cpp — asset/sprite/stage loading: texture atlas, sprite id order lookups,
// and stage JSON -> Stage grid (+entity placement). Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include "job_system.h"
#include "vfs.h"

// The stb_image implementation is in engine/stb_image_impl.cpp.
#include <stb_image.h>

using namespace toms::game_detail;

// A prebuilt atlas made by tools/atlas (project media/atlas/game.atlasproj, which keeps its images
// in the atlas itself): media/atlas/game.atlas + game.png for the original art,
// media/styles/<id>/atlas/ for each art style. It replaces the grid
// built in loadSpriteAtlas: sprites keep their own sizes, the .atlas carries child sprites, pivots,
// 9-slices and tags (spriteAtlas_), and the web build fetches one image instead of thirty. Every
// sprite in SPRITE_ORDER must be in it, on its first page -- otherwise the grid is used, as before.
bool Game::loadPrebuiltSpriteAtlas(int style) {
    const std::string dir = style > 0 ? dataDir + "/styles/" + artStyles_[style].id + "/atlas/" : dataDir + "/atlas/";
    std::string text;
    if (!toms::vfsReadAll(dir + "game.atlas", text)) return false;   // none: the grid, quietly
    toms::AtlasFile atlas;
    std::string err;
    if (!toms::parseAtlas(text, atlas, &err) || atlas.pages.empty()) {
        std::fprintf(stderr, "[assets] %sgame.atlas: %s -- using the sprite grid\n", dir.c_str(), err.empty() ? "no pages" : err.c_str());
        return false;
    }
    for (int i = 0; i < N_SPRITES; i++) {
        const toms::AtlasRegion* r = atlas.find(SPRITE_ORDER[i]);
        if (!r || r->page != 0) {
            std::fprintf(stderr, "[assets] %sgame.atlas has no sprite '%s'%s -- using the sprite grid\n", dir.c_str(),
                         SPRITE_ORDER[i], r ? " on its first page" : "");
            return false;
        }
    }
    std::string file;
    int w = 0, h = 0, ch = 0;
    unsigned char* d = nullptr;
    if (toms::vfsReadAll(dir + atlas.pages[0].file, file))
        d = stbi_load_from_memory((const stbi_uc*)file.data(), (int)file.size(), &w, &h, &ch, 4);
    if (!d) {
        std::fprintf(stderr, "[assets] cannot load %s%s -- using the sprite grid\n", dir.c_str(), atlas.pages[0].file.c_str());
        return false;
    }
    std::vector<uint8_t> px(d, d + (size_t)w * h * 4);
    stbi_image_free(d);
    if (atlas.pages[0].w != w || atlas.pages[0].h != h) {   // the image wins; UVs follow it
        std::fprintf(stderr, "[assets] %s is %dx%d, game.atlas says %dx%d\n", atlas.pages[0].file.c_str(), w, h,
                     atlas.pages[0].w, atlas.pages[0].h);
        atlas.pages[0].w = w;
        atlas.pages[0].h = h;
        atlas.computeUVs();
    }
    spriteUVs_.assign(N_SPRITES, {});
    spriteTrim_.assign(N_SPRITES, {0.0f, 0.0f, 1.0f, 1.0f});
    for (int i = 0; i < N_SPRITES; i++) {
        const toms::AtlasRegion* r = atlas.find(SPRITE_ORDER[i]);
        std::copy(r->uv, r->uv + 4, spriteUVs_[i].begin());
        if (r->origW > 0 && r->origH > 0)
            spriteTrim_[i] = {(float)r->offX / r->origW, (float)r->offY / r->origH, (float)r->w / r->origW, (float)r->h / r->origH};
        idToLayer[SPRITE_ORDER[i]] = i;
    }
    ren->loadSpriteAtlas(px, (uint32_t)w, (uint32_t)h, dir + atlas.pages[0].file);
    std::fprintf(stderr, "[assets] sprites: prebuilt atlas %sgame.atlas, %dx%d, %zu region(s), art style '%s'\n", dir.c_str(),
                 w, h, atlas.regions.size(), artStyles_[style].id.empty() ? "original" : artStyles_[style].id.c_str());
    spriteAtlas_ = std::move(atlas);
    spriteAtlasRevision_++;   // the UI's sprite sheet (uiSpritesheet) follows from the next frame
    loadedArtStyle_ = style;
    return true;
}

// Builds the sprite atlas for art style `style` (index into artStyles_; 0 = the original art) and
// uploads it, replacing the previous atlas. A style replaces some or all sprites
// (assets/media/styles/<id>/sprites/<sprite>.png); the others keep the original art. Runs at startup
// (loadAssets) and again when the player is on the title screen with another style chosen
// (refreshArtStyle). On failure the previous atlas and loadedArtStyle_ stay as they were.
bool Game::loadSpriteAtlas(int style) {
    const std::string& assetDir = dataDir;
    if (style < 0 || style >= (int)artStyles_.size()) style = 0;
    const std::string styleDir = style > 0 ? assetDir + "/styles/" + artStyles_[style].id + "/sprites/" : std::string();
    if (loadPrebuiltSpriteAtlas(style)) return true;
    // load sprites into a single uniform atlas (COLS columns). The cell size is the sprites' own
    // pixel size (32x32 in assets/media; a style may ship 64x64 -- tools/art/make_variant.py).
    const int COLS = 9;
    spriteGridCols = COLS;
    // The PNGs decode in parallel (JobSystem; inline where there are no threads): each job writes
    // only its own slot, and stb_image keeps its error state per thread.
    std::vector<std::vector<uint8_t>> layers(N_SPRITES);
    std::vector<int> widths(N_SPRITES, 0), heights(N_SPRITES, 0);
    std::vector<char> styled(N_SPRITES, 0);
    toms::JobSystem::parallelFor(N_SPRITES, [&](int i) {
        std::string file;   // vfs: inside the APK on Android, a plain file elsewhere
        if (!styleDir.empty() && toms::vfsReadAll(styleDir + SPRITE_ORDER[i] + ".png", file)) styled[i] = 1;
        else if (!toms::vfsReadAll(assetDir + "/sprites/" + SPRITE_ORDER[i] + ".png", file)) return;
        int w, h, ch;
        if (unsigned char* d = stbi_load_from_memory((const stbi_uc*)file.data(), (int)file.size(), &w, &h, &ch, 4)) {
            layers[i].assign(d, d + w * h * 4);
            widths[i] = w; heights[i] = h;
            stbi_image_free(d);
        }
    });
    int SW = 0, SH = 0;
    std::vector<std::string> replaced;
    for (int i = 0; i < N_SPRITES; i++) {
        if (layers[i].empty()) { std::fprintf(stderr, "load fail %s/sprites/%s.png\n", assetDir.c_str(), SPRITE_ORDER[i]); return false; }
        idToLayer[SPRITE_ORDER[i]] = i;
        if (styled[i]) replaced.push_back(SPRITE_ORDER[i]);
        SW = std::max(SW, widths[i]); SH = std::max(SH, heights[i]);
    }
    // One atlas cell size: a smaller sprite (an original 32px one next to a 64px style) is scaled
    // up with nearest-neighbour, which keeps pixel art crisp.
    for (int i = 0; i < N_SPRITES; i++) {
        if (widths[i] == SW && heights[i] == SH) continue;
        std::fprintf(stderr, "[assets] %s.png is %dx%d: scaled to %dx%d\n", SPRITE_ORDER[i], widths[i], heights[i], SW, SH);
        std::vector<uint8_t> big((size_t)SW * SH * 4);
        for (int y = 0; y < SH; y++)
            for (int x = 0; x < SW; x++) {
                const uint8_t* src = &layers[i][((size_t)(y * heights[i] / SH) * widths[i] + (x * widths[i] / SW)) * 4];
                std::copy(src, src + 4, &big[((size_t)y * SW + x) * 4]);
            }
        layers[i].swap(big);
    }
    std::fprintf(stderr, "[assets] sprites: %d x %dx%d px, art style '%s' (%d replaced)\n", N_SPRITES, SW, SH,
                 artStyles_[style].id.empty() ? "original" : artStyles_[style].id.c_str(), (int)replaced.size());
    ren->loadSprites(layers, SW, SH);
    spriteUVs_.clear();
    spriteTrim_.clear();
    spriteAtlas_ = toms::AtlasFile();   // no prebuilt atlas: the UI has no sprite sheet either
    spriteAtlasRevision_++;
    loadedArtStyle_ = style;
    return true;
}

// The chosen style (Settings) takes effect only while the title screen is up -- nothing from a run
// is on screen then. Called when the title reopens (returnToTitle) and right after the choice was
// made on the title's own Settings page (applyArtStyle). Restarting the game also applies it.
void Game::refreshArtStyle() {
    if (!ren || !title_.isOpen()) return;
    const int chosen = chosenArtStyle();
    if (chosen != loadedArtStyle_) loadSpriteAtlas(chosen);
}

bool Game::loadAssets(const std::string& assetDir) {
    dataDir = assetDir;
    // Create the renderer: bgfx on every platform (src/game/compat/renderer.h). The host
    // (GameSession) owns the window and frees the renderer.
    ren = new Renderer();
    ren->init(1280, 720);   // size ignored by the bgfx renderer: the host sets the backbuffer size
    // Art styles (Settings > art style; art_styles.h): start with the one settings.json names.
    artStyles_ = toms::artStylesFromJson(readJsonFile(assetDir + "/styles/styles.json"));
    if (!loadSpriteAtlas(toms::artStyleIndex(artStyles_,
            toms::loadGameSettings(toms::defaultSaveDir() + "/settings.json").artStyle)))
        return false;
    // (Text is RmlUi's: see assets/media/fonts/NotoSansCJKtc-TOMS.otf and tools/make_ui_font.py.)
    loadIdleAnims();

    // S1: character-level 佔格 table (docs/design/ART_AND_ABILITY_DESIGN.md F8). An absent file leaves
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
    // the battle scene's tuning (data/battle.json): bar speed and the wait after a tap
    {
        nlohmann::json bj = readJsonFile(assetDir + "/../data/battle.json");
        if (bj.is_object()) {
            barSlowScale_ = std::max(0.0f, bj.value("bar_slow_speed_scale", 1.0f));
            barFastScale_ = std::max(0.05f, bj.value("bar_fast_speed_scale", 1.0f));
            barCooldownMs_ = std::max(0, bj.value("bar_cooldown_ms", (int)CombatState::kBarCooldownMs));
            fprintf(stderr, "[assets] battle.json: bars start x%.2f, top speed x%.2f, %d ms after a tap\n", barSlowScale_,
                    barFastScale_, barCooldownMs_);
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
    // S4: the skill tree's content (data/skills.json) -- same "load once at boot" shape as
    // equipment/missions above.
    skillDefs_ = toms::loadSkillDefs(assetDir + "/../data/skills.json");
    // S5: the forge's content (data/forge.json) -- same "load once at boot" shape as skillDefs_.
    forgeDefs_ = toms::loadForgeRecipes(assetDir + "/../data/forge.json");
    // S6: the village hub's content (data/hub.json) -- same "load once at boot" shape as skillDefs_/
    // forgeDefs_ above.
    hubDefs_ = toms::loadHubLocations(assetDir + "/../data/hub.json");
    // S7 (equipment actives, first slice): same "load once at boot" shape as skillDefs_/forgeDefs_/
    // hubDefs_ above.
    activeDefs_ = toms::loadActiveDefinitions(assetDir + "/../data/actives.json");
    // M7 (first slice): the 15-ending table, same "load once at boot" shape as the above.
    endingsTable_ = toms::loadEndings(assetDir + "/../data/story/endings.json");
    // M8 (first slice): the rebirth config, same "load once at boot" shape as the above.
    cyclesConfig_ = toms::loadCyclesConfig(assetDir + "/../data/story/cycles.json");
    // init player
    pl.maxhp = kStartingHp; pl.hp = kStartingHp; pl.atk = kStartingAtk; pl.def = kStartingDef; pl.gold = 0; pl.exp = 0; pl.lv = 1;
    pl.inv = {"potion_red", "potion_blue", "exp_up"};

    // S3.5: the 70-floor tower (data/story/floors/*.json) as the run's progression source. Loaded
    // once here; empty when the data is absent, and every use is guarded, so such a checkout plays
    // the eleven hand-authored stages exactly as before.
    floors_ = toms::FloorTable::loadFromDirectory(dataDir + "/../data/story/floors");
    if (floors_.empty()) {
        fprintf(stderr, "[assets] floor table: absent -- playing the hand-authored stages only\n");
    } else {
        fprintf(stderr, "[assets] floor table: %d floors across %d acts (first %s, last %s)\n",
                floors_.size(), floors_.at(floors_.size()).actIndex,
                floors_.at(1).id.c_str(), floors_.at(floors_.size()).id.c_str());
    }

    // ---- Title phase boot ----
    // Persisted preferences (language, slot count) + the string table, then show the title.
    // modalActive() includes the title, so the world the host loads next sits inert behind it
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
        title_.setStyleCount((int)artStyles_.size());
        title_.setStyleIndex(chosenArtStyle());
        title_.open();
        refreshSlots();
        if (!haveSettings) applyLanguage();   // write defaults once, so the file exists
    }
    // init SFX subsystem (no-op if no audio device/context; headless-safe).
    audio.init(assetDir + "/sfx");
    wireMissionEvents(); // Milestone 4: subscribe mission-progress handlers once per session
    rollDailyMissions(); // Milestone 5: daily-mission reset check, once per session start
    return true;
}

int Game::spriteLayer(const std::string& id) const {
    auto it = idToLayer.find(id);
    if (it == idToLayer.end()) it = idToLayer.find("floor");   // a sprite the atlas lacks draws as floor, as before
    return it == idToLayer.end() ? 0 : it->second;
}

// UV rect for a sprite: from the prebuilt atlas, or its cell in the uniform grid atlas.
void Game::spriteUV(int layer, float uv[4]) const {
    if (layer >= 0 && layer < (int)spriteUVs_.size()) {
        std::copy(spriteUVs_[layer].begin(), spriteUVs_[layer].end(), uv);
        return;
    }
    int cols = spriteGridCols;
    int gx = layer % cols, gy = layer / cols;
    float u0 = (float)gx / cols, v0 = (float)gy / (float)((N_SPRITES + cols - 1) / cols);
    float u1 = (float)(gx + 1) / cols, v1 = (float)(gy + 1) / (float)((N_SPRITES + cols - 1) / cols);
    uv[0]=u0; uv[1]=v0; uv[2]=u1; uv[3]=v1;
}

void Game::loadStage(const std::string& id, StageArrival arrival) {
    mapFx_.clear();   // a door opening on the floor being left
    cancelWalk();   // a click-to-move route belongs to the old floor
    curStage = id;
    // Resolve the stage JSON. Data ids in connect.up/down use "stage_02" (underscore)
    // while the shipped files are named "stage02.json" (no underscore) — and the
    // initial load uses "stage01". Normalize so both forms resolve instead of
    // opening a non-existent path (which makes ifstream fail -> parse throw -> crash
    // when the player steps on stairs / a warp tile).
    std::string path = dataDir + "/../data/stages/" + id + ".json";
    if (!toms::vfsExists(path)) {
        std::string noUs = id;
        noUs.erase(std::remove(noUs.begin(), noUs.end(), '_'), noUs.end());
        std::string alt = dataDir + "/../data/stages/" + noUs + ".json";
        if (toms::vfsExists(alt)) path = alt;
    }
    // S3.5 (a): a floor id the tower knows (F01..F70) resolves through the floor table instead of
    // the bare data/stages/ path -- a normal floor to its generated grid, a boss floor to the
    // hand-authored map the spec names (section 3.2). Ids that are not floors (the eleven
    // hand-authored stages, e.g. when the floor data is absent) keep the old behaviour below.
    const toms::FloorInfo* floor = floors_.find(id);
    bool isFloor = (floor != nullptr);
    if (isFloor) {
        std::string rel = floors_.mapRelPath(id);
        std::string candidate = dataDir + "/../" + rel;
        if (toms::vfsExists(candidate)) path = candidate;
    }
    if (!isFloor) {
        // S2 fallback: generated floors also live in data/story/floors/<id>.stage.json (the file
        // layout docs/story/STORY_DATA_SCHEMA.md section 1.2 asks for); tried last so the hand-authored
        // data/stages/*.json files always win.
        if (!toms::vfsExists(path)) {
            std::string floorPath = dataDir + "/../data/story/floors/" + id + ".stage.json";
            if (toms::vfsExists(floorPath)) path = floorPath;
        }
    }
    st = parseStage(path, locale_);
    curFloorId_ = isFloor ? id : std::string();
    if (floor) {
        // The floor -- not the map file -- is the unit the run counts and names. A boss floor's map
        // is hand-authored and carries the act's own index/name (stage01 has index 1), so without
        // this the HUD would show the wrong floor number on every boss stage.
        st.id = floor->id;
        st.index = floor->seq;                       // 1..70 -> the HUD's "n/totalStages"
        std::string resolved = floor->nameKey;
        if (resolved.rfind("story.", 0) == 0) {
            std::string tr = locale_.tr(resolved);
            if (!tr.empty() && tr != resolved) resolved = tr;   // missing key -> keep the map's name
            else resolved = st.name;
        }
        if (!resolved.empty()) st.name = resolved;
        // Progression lives here (S3.5 a): the specs' nextFloor chain, translated into the two
        // fields the existing stair logic already reads. The generated grids place the goal on 'U'
        // (far from the entrance) and the way back on 'D' (beside it), which is exactly this.
        st.up = floor->nextFloor;
        st.down = floors_.prev(floor->id);
        // S3.5 (c)/(e): the floor's story keys, and the act whose palette this floor renders in.
        storyIntroKey_ = floor->introKey;
        storyAmbientKeys_ = floor->ambientKeys;
        storyTurns_ = 0;
        themeActIndex_ = floor->actIndex;
        chapterCardMs_ = 0.0f;
        if (floor->indexInAct == 1 && !floor->act.empty()) {   // first floor of an act -> act card
            std::string title = locale_.tr("story." + floor->actKey + ".title");
            if (title.rfind("story.", 0) != 0) {               // resolved (missing key -> the key)
                chapterCardTitle_ = title;
                chapterCardMs_ = 2600.0f;
            }
        }
        // S4: the chapter's own grants (skills/skillPoints/hub/unlocks). Deliberately NOT gated on
        // indexInAct==1 like the act card above -- a save resumed mid-chapter (Continue landing on,
        // say, F05) would otherwise never receive ch_01's grant at all, since F05 never re-triggers
        // "the first floor of an act". applyChapterGrants' own flag makes this a no-op once already
        // granted, so calling it on every floor of every chapter is correct, just occasionally
        // (once per chapter per run) does real work instead of none.
        if (!floor->act.empty()) applyChapterGrants(floor->act);
    } else {
        storyIntroKey_.clear();
        storyAmbientKeys_.clear();
        storyTurns_ = 0;
        themeActIndex_ = 1;
        chapterCardMs_ = 0.0f;
    }
    // Milestone 7: parseStage() rebuilds every entity fresh from JSON on every call (stairs,
    // Stage Select, etc.), so re-apply any previously-persisted Defeated/Collected status here --
    // otherwise a monster the player already beat or an item they already picked up would come
    // back the next time this floor loads, contradicting the "cleared floors stay cleared"
    // decision. Doors are intentionally not covered (see entityStatus_'s declaration in game.h).
    // A door opened earlier stays open (openDoor): its tile is floor and its "door:" entry no longer blocks.
    for (int y = 0; y < (int)st.tiles.size(); ++y)
        for (int x = 0; x < (int)st.tiles[y].size(); ++x) {
            const char c = st.tiles[y][x];
            if ((c == 'y' || c == 'b' || c == 'r') &&
                toms::getEntityStatus(entityStatus_, toms::entityStatusKey(st.id, x, y)) == toms::EntityStatus::Opened) {
                st.tiles[y][x] = '.';
                for (auto& e : st.entities) if (e.x == x && e.y == y && e.kind.rfind("door:", 0) == 0) e.consumed = true;
            }
        }
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
    // exactly what connect.up already does today (docs/design/GAME_DESIGN_DOCUMENT.md §5's 1:1 floor<->beat
    // mapping); now it's also written into persisted story state instead of being purely
    // implicit in "which floor am I standing on."
    toms::advanceStoryBeat(meta_, st.index);
    // Milestone 5: reaching a floor unlocks it as a Stage Select entry (architecture-doc §10 /
    // §5.2's stageCleared) -- every floor the player has ever reached becomes individually
    // re-selectable from the hub. Uses st.id (the parsed, canonical id) rather than the raw
    // `id` argument, since the two can differ by underscore normalization above.
    if (std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), st.id) == meta_.unlockedStages.end())
        meta_.unlockedStages.push_back(st.id);
    // derive total stage count: the 70-floor tower when its data is present (S3.5 b, so the HUD
    // counter is honest), else the historical max index over data/stages/.
    if (floorMode()) {
        totalStages = floors_.size();
    } else {
    totalStages = 1;
    std::string dir = dataDir + "/../data/stages/";
    {
        for (const std::string& name : toms::vfsListDir(dir)) {
            try {
                nlohmann::json j = readJsonFile(dir + name);
                int idx = j.value("index", 0);
                if (idx > totalStages) totalStages = idx;
            } catch (...) {}
        }
    }
    }
    // store unlock: when entering the configured unlock stage for the first time,
    // mark the shop as unlocked and pop a one-time "shop unlocked!" dialog.
    if (st.index >= storeUnlockStage_ && !storeUnlocked_) {
        storeUnlocked_ = true;
        storeUnlockDlg = true;
    }

    // M9 (stair alignment): arriving via a specific staircase lands exactly on the matching
    // stairs tile -- every floor's stairs_down is now forced to sit where the previous floor's
    // stairs_up landed (see docs/story/STAIR_ALIGNMENT.md), so this is always a real, walkable
    // spot, not a guess. Fresh loads (new game, Stage Select, rebirth, a debug jump) keep the old
    // '@'-or-default behavior -- falling back to whichever stair the floor does have if it has
    // no '@' (true for every floor except F01, the chain's head).
    pl.x = 1; pl.y = (int)st.height - 2;
    const char* wantKind = (arrival == StageArrival::FromBelow) ? "stairs_down"
                         : (arrival == StageArrival::FromAbove) ? "stairs_up" : nullptr;
    bool placed = false;
    if (wantKind) {
        for (auto& e : st.entities)
            if (e.kind == wantKind) { pl.x = e.x; pl.y = e.y; placed = true; break; }
    }
    if (!placed) {
        for (auto& e : st.entities)
            if (e.raw == "@") { pl.x = e.x; pl.y = e.y; placed = true; break; }
    }
    if (!placed) {
        for (auto& e : st.entities)
            if (e.kind == "stairs_down" || e.kind == "stairs_up") { pl.x = e.x; pl.y = e.y; break; }
    }
    // A floor change must never visibly pan in from wherever the camera was on the PREVIOUS
    // floor (different grid, different scale of "makes sense") -- jump straight to the new
    // floor's starting view instead of easing into it.
    cam_.snap();
}
