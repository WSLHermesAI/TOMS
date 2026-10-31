// game_assets.cpp — asset/sprite/stage loading: texture atlas, sprite id order lookups,
// and stage JSON -> Stage grid (+entity placement). Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include "job_system.h"
#include "vfs.h"

// The stb_image implementation is in engine/stb_image_impl.cpp.
#include <stb_image.h>

using namespace toms::game_detail;

// The sprite atlas cooked by tools/atlas (project art_raw/atlas/game.atlasproj, which keeps its images in the atlas
// itself): media/atlas/game.atlas + game.png for the original art, media/styles/<id>/atlas/ for each art style.
// Sprites keep their own sizes; the .atlas carries child sprites, pivots, 9-slices and tags (spriteAtlas_). Every
// sprite in SPRITE_ORDER must be in it, on its first page.
bool Game::loadPrebuiltSpriteAtlas(int style) {
    const std::string dir = toms::res::ResourceManager::dirOf(style > 0 ? pathFor(toms::pattern::kStyleAtlas, artStyles_[style].id)
                                                                   : pathOf(toms::key::kAtlasGame));
    std::string text;
    if (!toms::vfsReadAll(dir + "game.atlas", text)) return false;
    toms::AtlasFile atlas;
    std::string err;
    if (!toms::parseAtlas(text, atlas, &err) || atlas.pages.empty()) {
        std::fprintf(stderr, "[assets] %sgame.atlas: %s\n", dir.c_str(), err.empty() ? "no pages" : err.c_str());
        return false;
    }
    for (int i = 0; i < N_SPRITES; i++) {
        const toms::AtlasRegion* r = atlas.find(SPRITE_ORDER[i]);
        if (!r || r->page != 0) {
            std::fprintf(stderr, "[assets] %sgame.atlas has no sprite '%s'%s\n", dir.c_str(),
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
        std::fprintf(stderr, "[assets] cannot load %s%s\n", dir.c_str(), atlas.pages[0].file.c_str());
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
    idToLayer.clear();   // rebuilt for this atlas (an art style's may hold other sprites than the last one)
    spriteUVs_.assign(N_SPRITES, {});
    spriteTrim_.assign(N_SPRITES, {0.0f, 0.0f, 1.0f, 1.0f});
    for (int i = 0; i < N_SPRITES; i++) {
        const toms::AtlasRegion* r = atlas.find(SPRITE_ORDER[i]);
        std::copy(r->uv, r->uv + 4, spriteUVs_[i].begin());
        if (r->origW > 0 && r->origH > 0)
            spriteTrim_[i] = {(float)r->offX / r->origW, (float)r->offY / r->origH, (float)r->w / r->origW, (float)r->h / r->origH};
        idToLayer[SPRITE_ORDER[i]] = i;
    }
    // Every sprite's visible box (its pixels with alpha): how character clips are fitted to a character
    // (anim_clip.h fitCharacter, docs/engine/05_ANIMATION.md "Characters").
    spriteBoxes_.clear();
    for (const toms::AtlasRegion& r : atlas.regions) {
        if (r.page != 0 || r.child || r.rotated || r.x < 0 || r.y < 0 || r.x + r.w > w || r.y + r.h > h) continue;
        spriteBoxes_[r.name] = toms::visibleBox(px.data() + ((size_t)r.y * w + r.x) * 4, w, r.w, r.h, r.offX, r.offY,
                                                r.origW, r.origH);
    }
    // Every other sprite on the page (the new monsters, NPCs, items, doors, floor themes ...) is drawable by its name
    // too: a type's or an object's "sprite" can be any of them (docs/engine/14_AI_SPRITES.md). Only the first page is the
    // renderer's sprite texture, so sprites on a further page are not.
    for (const toms::AtlasRegion& r : atlas.regions) {
        if (r.page != 0 || idToLayer.count(r.name)) continue;
        idToLayer[r.name] = (int)spriteUVs_.size();
        spriteUVs_.push_back({r.uv[0], r.uv[1], r.uv[2], r.uv[3]});
        spriteTrim_.push_back(r.origW > 0 && r.origH > 0
            ? std::array<float, 4>{(float)r.offX / r.origW, (float)r.offY / r.origH, (float)r.w / r.origW, (float)r.h / r.origH}
            : std::array<float, 4>{0.0f, 0.0f, 1.0f, 1.0f});
    }
    ren->loadSpriteAtlas(px, (uint32_t)w, (uint32_t)h, dir + atlas.pages[0].file);
    std::fprintf(stderr, "[assets] sprites: prebuilt atlas %sgame.atlas, %dx%d, %zu region(s), art style '%s'\n", dir.c_str(),
                 w, h, atlas.regions.size(), artStyles_[style].id.empty() ? "original" : artStyles_[style].id.c_str());
    spriteAtlas_ = std::move(atlas);
    {   // its meshes (docs/engine/16_MESH_ANIMATION.md): beside the style's atlas, else the original art's (cut from the same
        // sprites; drawn inside each region, so a different packing only changes how closely they follow the shape)
        std::string meshError;
        const std::string base = pathOf(toms::key::kAtlasGame);
        const std::string own = dir + "game.atlas";
        if (!toms::res::readMeshFile(toms::spriteMeshPath(own), spriteMeshes_, meshError))
            std::fprintf(stderr, "[assets] %s\n", meshError.c_str());
        if (spriteMeshes_.meshes.empty() && own != base && !toms::res::readMeshFile(toms::spriteMeshPath(base), spriteMeshes_, meshError))
            std::fprintf(stderr, "[assets] %s\n", meshError.c_str());
    }
    provideSpriteAtlas();
    spriteAtlasRevision_++;   // the UI's sprite sheet (uiSpritesheet) follows from the next frame
    loadedArtStyle_ = style;
    return true;
}

// Loads the sprite atlas for art style `style` (index into artStyles_; 0 = the original art) and uploads it,
// replacing the previous one: media/atlas/game.atlas, or media/styles/<id>/atlas/game.atlas for a style. Runs at
// startup (loadAssets) and again when the player is on the title screen with another style chosen
// (refreshArtStyle). The atlases are cooked from art_raw/ by tools/atlas (art_raw/atlas/game.atlasproj); the game
// ships only them, never the raw PNGs. A style whose atlas is missing falls back to the original art. On failure
// the previous atlas and loadedArtStyle_ stay as they were.
bool Game::loadSpriteAtlas(int style) {
    if (style < 0 || style >= (int)artStyles_.size()) style = 0;
    if (loadPrebuiltSpriteAtlas(style)) return true;
    if (style > 0) {
        std::fprintf(stderr, "[assets] art style '%s' has no usable atlas: using the original art\n", artStyles_[style].id.c_str());
        return loadPrebuiltSpriteAtlas(0);
    }
    std::fprintf(stderr, "[assets] %s/atlas/game.atlas is missing or lacks the map sprites: rebuild it with\n"
                         "  atlaspack build art_raw/atlas/game.atlasproj --all-variants\n", dataDir.c_str());
    return false;
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
    // The resource cache (docs/engine/12_RESOURCE_MANAGER.md). Any ".../atlas/game.atlas" a clip or effect names -- the
    // original art's or a style's -- is the sprite atlas loaded now (provideSpriteAtlas), so they follow the art style.
    res_ = std::make_unique<toms::res::ResourceManager>(ren);
    // Every file the game needs by key (assets/media/resources.json); a key or a path under assets/media is how code
    // asks for one (res_->get<T>). A missing one is said on screen.
    res_->setRoot(assetDir);
    res_->setOnMissing([this](const std::string& name, const std::string& path, const std::string& error) { resourceMissing(name, path, error); });
    res_->loadManifest("resources.json");
    toms::registerBuiltinRenderObjects(renderFactory_);   // sprite, clip, particle, text, group (the floor's looks)
    res_->setPathMapper([](const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
        const std::string parent = slash == std::string::npos ? std::string() : path.substr(0, slash);
        if (name == "game.atlas" && parent.size() >= 5 && parent.compare(parent.size() - 5, 5, "atlas") == 0)
            return std::string(toms::res::ResourceManager::kSpriteAtlas);
        return path;
    });
    // Art styles (Settings > art style; art_styles.h): start with the one settings.json names.
    artStyles_ = toms::artStylesFromJson(readJsonFile(pathOf(toms::key::kStylesIndex)));
    if (!loadSpriteAtlas(toms::artStyleIndex(artStyles_,
            toms::loadGameSettings(toms::settingsPath()).artStyle)))
        return false;
    // (Text is RmlUi's: see assets/media/fonts/NotoSansCJKtc-TOMS.otf and tools/make_ui_font.py.)
    // The manifest's "preload" entries (the map's clips and their atlases), now that the sprite atlas is provided.
    if (const int failed = res_->preload()) std::fprintf(stderr, "[res] %d preloaded resource(s) missing\n", failed);

    // S1: character-level 佔格 table (docs/design/ART_AND_ABILITY_DESIGN.md F8). An absent file leaves
    // every entity at 1x1, i.e. exactly the pre-S1 behavior -- the table is purely additive.
    {
        nlohmann::json fpj = readJsonFile(pathOf(toms::key::kDataFootprints));
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
        nlohmann::json bj = readJsonFile(pathOf(toms::key::kDataBattle));
        if (bj.is_object()) {
            barSlowScale_ = std::max(0.0f, bj.value("bar_slow_speed_scale", 1.0f));
            barFastScale_ = std::max(0.05f, bj.value("bar_fast_speed_scale", 1.0f));
            barCooldownMs_ = std::max(0, bj.value("bar_cooldown_ms", (int)CombatState::kBarCooldownMs));
            attackPauseMs_ = std::max(0, bj.value("attack_pause_ms", attackPauseMs_));
            hitNumberDelayMs_ = std::max(0, bj.value("hit_number_delay_ms", hitNumberDelayMs_));
            battleJson_ = bj;
            paceSettings_ = toms::paceFromJson(bj);     // docs/gameplay/13 §6.3
            feel_ = toms::feelFromJson(bj);             // docs/gameplay/15 §3.8
            grade_ = toms::gradeFromJson(bj);           // §3.6
            guard_ = toms::guardFromJson(bj);           // §3.2
            comboHoldsMonsters_ = bj.value("combo_holds_monsters", false);
            comboRunsThroughAttacks_ = bj.value("combo_runs_through_attacks", true);
            breakSettings_ = toms::breakFromJson(bj);   // §6.4
            fprintf(stderr, "[assets] battle.json: bars start x%.2f, top speed x%.2f, %d ms after a tap\n", barSlowScale_,
                    barFastScale_, barCooldownMs_);
        }
    }
    weaponPlay_ = toms::loadWeaponPlay(pathOf(toms::key::kDataWeaponPlay));   // docs/gameplay/07_GAMEPLAY_PLAN.md §5.6
    looks_ = toms::looksFromJson(readJsonFile(pathOf(toms::key::kDataLooks)));   // the status map (docs/gameplay/11 §4.2)
    moves_ = toms::movesFromJson(readJsonFile(pathOf(toms::key::kDataMoves)));   // the monsters' moves (docs/gameplay/13 §6.1)
    miniGames_ = toms::loadMiniGames(pathOf(toms::key::kDataMinigames));      // docs/gameplay/07 phase 6
    characterFit_ = readJsonFile(pathOf(toms::key::kDataCharacterFit));      // character clips' nudges (docs/engine/05)
    fprintf(stderr, "[assets] weapon_play.json: %d weapon(s)\n", (int)weaponPlay_.weapons.size());
    // load enemy templates
    nlohmann::json ej = readJsonFile(pathOf(toms::key::kDataEnemies));
    for (auto& [k,v] : ej.items()) enemyTpl[k] = v;
    // data/items.json: every item in the game, one record each ("_comment" and other "_" keys are notes).
    // Its weapons, armor and talents are also the Equipment System's content (equipment_system.h).
    nlohmann::json ij = readJsonFile(pathOf(toms::key::kDataItems));
    for (auto& [k,v] : ij.items()) {
        if (k.empty() || k[0] == '_' || !v.is_object()) continue;
        itemDefs[k] = v;
        if (isGearType(v.value("type", std::string()))) {
            toms::EquipmentDefinition d = toms::equipmentFromJson(v);
            d.id = k;
            equipmentDefs_[k] = d;
        }
    }
    // data/story/counters.json: the story counters' state words, shown on the Status tab once a
    // counter reaches its displayAt (the run state keeps the numbers).
    {
        nlohmann::json cj = readJsonFile(pathOf(toms::key::kDataStoryCounters));
        if (cj.is_object())
            for (auto& [k, v] : cj.items())
                if (!k.empty() && k[0] != '_' && v.is_object() && v.contains("label")) counterLabels_[k] = v["label"];
    }
    loadEventDefs();
    loadNpcs();   // NPC files and event variables (docs/gameplay/event_editor/08_NPC_DIALOGUE.md)
    // data/store.json: which of those items the store sells, and how prices grow
    loadStore();
    // data/stats.json: the player's attributes and the level-up numbers (docs/gameplay/03_PLAYER_MENU.md section 3).
    {
        nlohmann::json sj = readJsonFile(pathOf(toms::key::kDataStats));
        if (sj.contains("attributes") && sj["attributes"].is_array())
            for (const auto& a : sj["attributes"]) {
                AttrDef d;
                d.id = a.value("id", std::string());
                if (d.id.empty()) continue;
                d.shortName = a.value("short", d.id);
                d.nameKey = a.value("name", d.id);
                d.descKey = a.value("desc", std::string());
                d.base = a.value("base", 5);
                d.perLevel = a.value("perLevel", 1);
                if (a.contains("effects") && a["effects"].is_array())
                    for (const auto& e : a["effects"])
                        if (e.is_object() && e.contains("stat")) d.effects.push_back({e.value("stat", std::string()), e.value("per", 0.0f)});
                attrDefs_.push_back(d);
            }
        if (sj.contains("levelUp") && sj["levelUp"].is_object()) {
            const auto& l = sj["levelUp"];
            levelUp_.expPerLevel = std::max(1, l.value("expPerLevel", levelUp_.expPerLevel));
            levelUp_.atk = l.value("atk", levelUp_.atk);
            levelUp_.def = l.value("def", levelUp_.def);
            levelUp_.maxhp = l.value("maxhp", levelUp_.maxhp);
            levelUp_.freePoints = l.value("freePoints", levelUp_.freePoints);
            levelUp_.maxLevel = std::max(1, l.value("maxLevel", levelUp_.maxLevel));
            if (l.contains("points") && l["points"].is_array())
                for (const auto& r : l["points"])
                    if (r.is_object()) levelUp_.points.push_back({std::max(2, r.value("from", 2)), r.value("points", 0)});
        }
    }
    // Milestone 8: load the Mission System's content (schema/logic built in Milestone 4 --
    // mission_system.h -- but missionDefs_ stayed empty until this content-authoring pass).
    {
        nlohmann::json mj = readJsonFile(pathOf(toms::key::kDataMissions));
        for (auto& [k, v] : mj.items()) missionDefs_[k] = toms::missionDefinitionFromJson(v);
    }
    // S4: the skill tree's content (data/skills.json) -- same "load once at boot" shape as
    // equipment/missions above.
    skillDefs_ = toms::loadSkillDefs(pathOf(toms::key::kDataSkills));
    // S5: the forge's content (data/forge.json) -- same "load once at boot" shape as skillDefs_.
    forgeDefs_ = toms::loadForgeRecipes(pathOf(toms::key::kDataForge));
    // S6: the village hub's content (data/hub.json) -- same "load once at boot" shape as skillDefs_/
    // forgeDefs_ above.
    hubDefs_ = toms::loadHubLocations(pathOf(toms::key::kDataHub));
    // S7 (equipment actives, first slice): same "load once at boot" shape as skillDefs_/forgeDefs_/
    // hubDefs_ above.
    activeDefs_ = toms::loadActiveDefinitions(pathOf(toms::key::kDataActives));
    // M7 (first slice): the 15-ending table, same "load once at boot" shape as the above.
    endingsTable_ = toms::loadEndings(pathOf(toms::key::kDataStoryEndings));
    // M8 (first slice): the rebirth config, same "load once at boot" shape as the above.
    cyclesConfig_ = toms::loadCyclesConfig(pathOf(toms::key::kDataStoryCycles));
    // init player
    pl.maxhp = kStartingHp; pl.hp = kStartingHp; pl.atk = kStartingAtk; pl.def = kStartingDef; pl.gold = 0; pl.exp = 0; pl.lv = 1;
    pl.inv = {"potion_red", "potion_blue", "exp_up"};
    resetAttrs();

    // S3.5: the 70-floor tower (data/story/floors/*.json) as the run's progression source. Loaded
    // once here; empty when the data is absent, and every use is guarded, so such a checkout plays
    // the eleven hand-authored stages exactly as before.
    floors_ = toms::FloorTable::loadFromDirectory(pathOf(toms::key::kFolderFloors));
    loadStairLocks();   // Q7: the stairs the floors lock behind events (needs the event pools, loaded above)
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
        settings_ = toms::loadGameSettings(toms::settingsPath(), &haveSettings);
        locale_.loadFromFile(pathOf(toms::key::kDataText));
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
    audio.init(pathOf(toms::key::kFolderSfx));
    wireMissionEvents(); // Milestone 4: subscribe mission-progress handlers once per session
    rollDailyMissions(); // Milestone 5: daily-mission reset check, once per session start
    return true;
}

void Game::resourceMissing(const std::string& name, const std::string& path, const std::string& error) {
    std::fprintf(stderr, "[res] missing: %s (%s): %s\n", name.c_str(), path.c_str(), error.c_str());
    std::string msg = locale_.tr("res.missing");
    msg = msg == "res.missing" || msg.empty() ? "Missing file: " + name : toms::fillText(msg, {{"file", name}});
    if (std::find(resourceMessages_.begin(), resourceMessages_.end(), msg) == resourceMessages_.end()) resourceMessages_.push_back(msg);
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

std::string Game::statusKeyAt(int x, int y) const {
    for (const Entity& e : st.entities)
        if (e.x == x && e.y == y && !e.uid.empty()) return toms::entityStatusKey(st.id, x, y, e.uid);
    return toms::entityStatusKey(st.id, x, y);
}

void Game::migrateStatusKeys() {
    for (const Entity& e : st.entities) {
        const std::string now = toms::entityStatusKey(st.id, e.x, e.y, e.uid);
        if (entityStatus_.count(now)) continue;
        // the keys this object may have been saved under: its position, by the floor id or by the stage file's name
        for (const std::string& old : {toms::entityStatusKey(st.id, e.x, e.y), toms::entityStatusKey(curStage, e.x, e.y)}) {
            if (old == now) continue;
            auto it = entityStatus_.find(old);
            if (it == entityStatus_.end()) continue;
            entityStatus_[now] = it->second;
            entityStatus_.erase(it);
            break;
        }
    }
}

void Game::loadStage(const std::string& id, StageArrival arrival) {
    const std::string fromStage = st.id;   // the floor being left: arriving by stairs lands on the stair back to it
    floor_.clear();   // the objects of the floor being left (a door opening on it stops)
    cancelWalk();   // a click-to-move route belongs to the old floor
    curStage = id;
    // Resolve the stage JSON. Data ids in connect.up/down use "stage_02" (underscore)
    // while the shipped files are named "stage02.json" (no underscore) — and the
    // initial load uses "stage01". Normalize so both forms resolve instead of
    // opening a non-existent path (which makes ifstream fail -> parse throw -> crash
    // when the player steps on stairs / a warp tile).
    std::string path = pathFor(toms::pattern::kStage, id);
    if (!toms::vfsExists(path)) {
        std::string noUs = id;
        noUs.erase(std::remove(noUs.begin(), noUs.end(), '_'), noUs.end());
        std::string alt = pathFor(toms::pattern::kStage, noUs);
        if (toms::vfsExists(alt)) path = alt;
    }
    // a stage file by its path (tests: toms_game --stage=tests/stage/objects.stage.json)
    if (id.size() > 5 && id.compare(id.size() - 5, 5, ".json") == 0 && toms::vfsExists(id)) path = id;
    // S3.5 (a): a floor id the tower knows (F01..F70) resolves through the floor table instead of
    // the bare data/stages/ path -- a normal floor to its generated grid, a boss floor to the
    // hand-authored map the spec names (section 3.2). Ids that are not floors (the eleven
    // hand-authored stages, e.g. when the floor data is absent) keep the old behaviour below.
    const toms::FloorInfo* floor = floors_.find(id);
    bool isFloor = (floor != nullptr);
    if (isFloor) {
        std::string rel = floors_.mapRelPath(id);
        std::string candidate = pathOf(toms::key::kFolderAssets) + "/" + rel;   // rel is under assets/
        if (toms::vfsExists(candidate)) path = candidate;
    }
    if (!isFloor) {
        // S2 fallback: generated floors also live in data/story/floors/<id>.stage.json (the file
        // layout docs/story/STORY_DATA_SCHEMA.md section 1.2 asks for); tried last so the hand-authored
        // data/stages/*.json files always win.
        if (!toms::vfsExists(path)) {
            std::string floorPath = pathFor(toms::pattern::kFloorStage, id);
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
    // Q7 / 05_FLOOR_LINKS: every stair as a list with its tile and lock. A floor's own `stairs` list wins over the
    // chain; st.up / st.down stay the first stair up / down.
    if (floor && floor->hasStairs) {
        st.stairs = floor->stairs;
        st.up.clear();
        st.down.clear();
        for (const toms::StairLink& s : st.stairs) {
            std::string& first = s.up ? st.up : st.down;
            if (first.empty()) first = s.to;
        }
    } else {
        st.stairs = toms::defaultStairLinks(st.up, st.down);
    }
    toms::placeStairLinks(st.stairs, st.tiles, st.legend);
    for (const toms::StairLink& s : st.stairs)   // W38: remembered, so the Events tab can say where a stair is from any floor
        if (s.x >= 0) stairSpots_[st.id + ">" + s.to] = {s.x, s.y};
    for (auto& e : st.entities)   // a "stairs_up:F96" tile is a plain stair tile now
        for (const char* k : {"stairs_up", "stairs_down"})
            if (e.kind.rfind(std::string(k) + ":", 0) == 0) e.kind = k;
    // Milestone 7: parseStage() rebuilds every entity fresh from JSON on every call (stairs,
    // Stage Select, etc.), so re-apply any previously-persisted Defeated/Collected status here --
    // otherwise a monster the player already beat or an item they already picked up would come
    // back the next time this floor loads, contradicting the "cleared floors stay cleared"
    // decision. Doors are intentionally not covered (see entityStatus_'s declaration in game.h).
    // A door opened earlier stays open (openDoor): its tile is floor and its "door:" entry no longer blocks.
    migrateStatusKeys();
    for (int y = 0; y < (int)st.tiles.size(); ++y)
        for (int x = 0; x < (int)st.tiles[y].size(); ++x) {
            const char c = st.tiles[y][x];
            if ((c == 'y' || c == 'b' || c == 'r') &&
                toms::getEntityStatus(entityStatus_, statusKeyAt(x, y)) == toms::EntityStatus::Opened) {
                st.tiles[y][x] = '.';
                for (auto& e : st.entities) if (e.x == x && e.y == y && e.kind.rfind("door:", 0) == 0) e.consumed = true;
            }
        }
    for (auto& e : st.entities) {
        bool isMonster = e.kind.rfind("monster:", 0) == 0;
        // an event fired on this tile does not come back (it would fire, and give its items, again); the same
        // event id may sit on other floors, so this goes by the tile, like items
        bool isItem = e.kind.rfind("item:", 0) == 0 || e.kind.rfind("event:", 0) == 0;
        const bool isGate = e.kind.rfind("gate:", 0) == 0;   // a gate opened with its key stays open
        if (!isMonster && !isItem && !isGate) continue;
        toms::EntityStatus want = isMonster ? toms::EntityStatus::Defeated : isGate ? toms::EntityStatus::Opened
                                                                                     : toms::EntityStatus::Collected;
        toms::EntityStatus have = toms::getEntityStatus(entityStatus_, toms::entityStatusKey(st.id, e.x, e.y, e.uid));
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
    std::string dir = pathOf(toms::key::kFolderStages) + "/";
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
        newsStore_ = true;   // a dot on ≡ until the store is opened
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
    if (arrival != StageArrival::Fresh) {   // the stair that leads back to the floor just left (05 section 3.3)
        const int i = toms::arrivalStair(st.stairs, fromStage, arrival == StageArrival::FromBelow);
        if (i >= 0) { pl.x = st.stairs[i].x; pl.y = st.stairs[i].y; placed = true; }
    }
    if (!placed && wantKind) {
        for (auto& e : st.entities)
            if (e.kind == wantKind) { pl.x = e.x; pl.y = e.y; placed = true; break; }
    }
    if (!placed && st.startX >= 0) { pl.x = st.startX; pl.y = st.startY; placed = true; }   // '@' (parseStage)
    if (!placed) {
        for (auto& e : st.entities)
            if (e.kind == "stairs_down" || e.kind == "stairs_up") { pl.x = e.x; pl.y = e.y; break; }
    }
    floor_.build(*this);   // a class per thing on the floor, with its looks (map_objects.h)
    // A floor change must never visibly pan in from wherever the camera was on the PREVIOUS
    // floor (different grid, different scale of "makes sense") -- jump straight to the new
    // floor's starting view instead of easing into it.
    cam_.snap();
}
