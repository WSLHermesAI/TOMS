// game_assets.cpp — asset/sprite/stage loading: texture atlas, sprite id order lookups,
// and stage JSON -> Stage grid (+entity placement). Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include "job_system.h"
#include "version_label.h"
#include "vfs.h"

// The stb_image implementation is in engine/stb_image_impl.cpp.
#include <stb_image.h>

using namespace toms::game_detail;

// The sprite atlas cooked by tools/atlas (project art_raw/atlas/game.atlasproj, which keeps its images in the atlas
// itself): media/atlas/game.atlas + game.png for the original art, media/styles/<id>/atlas/ for each art style.
// Sprites keep their own sizes; the .atlas carries child sprites, pivots, 9-slices and tags (spriteAtlas_). Every
// sprite in SPRITE_ORDER must be in it, on its first page.
bool Game::f_LoadPrebuiltSpriteAtlas(int l_iStyle) {
    const std::string l_strDir = toms::res::ResourceManager::f_DirOf(l_iStyle > 0 ? f_PathFor(toms::pattern::g_pszStyleAtlas, m_vArtStyles[l_iStyle].m_strId)
                                                                   : f_PathOf(toms::key::g_pszAtlasGame));
    std::string l_strText;
    if (!toms::f_VfsReadAll(l_strDir + "game.atlas", l_strText)) return false;
    toms::AtlasFile l_oAtlas;
    std::string l_strErr;
    if (!toms::f_ParseAtlas(l_strText, l_oAtlas, &l_strErr) || l_oAtlas.m_vPages.empty()) {
        std::fprintf(stderr, "[assets] %sgame.atlas: %s\n", l_strDir.c_str(), l_strErr.empty() ? "no pages" : l_strErr.c_str());
        return false;
    }
    for (int l_iI = 0; l_iI < g_iNSPRITES; l_iI++) {
        const toms::AtlasRegion* l_pR = l_oAtlas.f_Find(g_arrSPRITEORDER[l_iI]);
        if (!l_pR || l_pR->m_iPage != 0) {
            std::fprintf(stderr, "[assets] %sgame.atlas has no sprite '%s'%s\n", l_strDir.c_str(),
                         g_arrSPRITEORDER[l_iI], l_pR ? " on its first page" : "");
            return false;
        }
    }
    std::string l_strFile;
    int l_iW = 0, l_iH = 0, l_iCh = 0;
    unsigned char* l_pD = nullptr;
    if (toms::f_VfsReadAll(l_strDir + l_oAtlas.m_vPages[0].m_strFile, l_strFile))
        l_pD = stbi_load_from_memory((const stbi_uc*)l_strFile.data(), (int)l_strFile.size(), &l_iW, &l_iH, &l_iCh, 4);
    if (!l_pD) {
        std::fprintf(stderr, "[assets] cannot load %s%s\n", l_strDir.c_str(), l_oAtlas.m_vPages[0].m_strFile.c_str());
        return false;
    }
    std::vector<uint8_t> l_vPx(l_pD, l_pD + (size_t)l_iW * l_iH * 4);
    stbi_image_free(l_pD);
    if (l_oAtlas.m_vPages[0].m_iW != l_iW || l_oAtlas.m_vPages[0].m_iH != l_iH) {   // the image wins; UVs follow it
        std::fprintf(stderr, "[assets] %s is %dx%d, game.atlas says %dx%d\n", l_oAtlas.m_vPages[0].m_strFile.c_str(), l_iW, l_iH,
                     l_oAtlas.m_vPages[0].m_iW, l_oAtlas.m_vPages[0].m_iH);
        l_oAtlas.m_vPages[0].m_iW = l_iW;
        l_oAtlas.m_vPages[0].m_iH = l_iH;
        l_oAtlas.f_ComputeUVs();
    }
    m_mapIdToLayer.clear();   // rebuilt for this atlas (an art style's may hold other sprites than the last one)
    m_vSpriteUVs.assign(g_iNSPRITES, {});
    m_vSpriteTrim.assign(g_iNSPRITES, {0.0f, 0.0f, 1.0f, 1.0f});
    for (int l_iI = 0; l_iI < g_iNSPRITES; l_iI++) {
        const toms::AtlasRegion* l_pR = l_oAtlas.f_Find(g_arrSPRITEORDER[l_iI]);
        std::copy(l_pR->m_arrUv, l_pR->m_arrUv + 4, m_vSpriteUVs[l_iI].begin());
        if (l_pR->m_iOrigW > 0 && l_pR->m_iOrigH > 0)
            m_vSpriteTrim[l_iI] = {(float)l_pR->m_iOffX / l_pR->m_iOrigW, (float)l_pR->m_iOffY / l_pR->m_iOrigH, (float)l_pR->m_iW / l_pR->m_iOrigW, (float)l_pR->m_iH / l_pR->m_iOrigH};
        m_mapIdToLayer[g_arrSPRITEORDER[l_iI]] = l_iI;
    }
    // Every sprite's visible box (its pixels with alpha): how character clips are fitted to a character
    // (anim_clip.h fitCharacter, docs/engine/05_ANIMATION.md "Characters").
    m_mapSpriteBoxes.clear();
    for (const toms::AtlasRegion& l_oR : l_oAtlas.m_vRegions) {
        if (l_oR.m_iPage != 0 || l_oR.m_bChild || l_oR.m_bRotated || l_oR.m_iX < 0 || l_oR.m_iY < 0 || l_oR.m_iX + l_oR.m_iW > l_iW || l_oR.m_iY + l_oR.m_iH > l_iH) continue;
        m_mapSpriteBoxes[l_oR.m_strName] = toms::f_VisibleBox(l_vPx.data() + ((size_t)l_oR.m_iY * l_iW + l_oR.m_iX) * 4, l_iW, l_oR.m_iW, l_oR.m_iH, l_oR.m_iOffX, l_oR.m_iOffY,
                                                l_oR.m_iOrigW, l_oR.m_iOrigH);
    }
    // Every other sprite on the page (the new monsters, NPCs, items, doors, floor themes ...) is drawable by its name
    // too: a type's or an object's "sprite" can be any of them (docs/engine/14_AI_SPRITES.md). Only the first page is the
    // renderer's sprite texture, so sprites on a further page are not.
    for (const toms::AtlasRegion& l_oR : l_oAtlas.m_vRegions) {
        if (l_oR.m_iPage != 0 || m_mapIdToLayer.count(l_oR.m_strName)) continue;
        m_mapIdToLayer[l_oR.m_strName] = (int)m_vSpriteUVs.size();
        m_vSpriteUVs.push_back({l_oR.m_arrUv[0], l_oR.m_arrUv[1], l_oR.m_arrUv[2], l_oR.m_arrUv[3]});
        m_vSpriteTrim.push_back(l_oR.m_iOrigW > 0 && l_oR.m_iOrigH > 0
            ? std::array<float, 4>{(float)l_oR.m_iOffX / l_oR.m_iOrigW, (float)l_oR.m_iOffY / l_oR.m_iOrigH, (float)l_oR.m_iW / l_oR.m_iOrigW, (float)l_oR.m_iH / l_oR.m_iOrigH}
            : std::array<float, 4>{0.0f, 0.0f, 1.0f, 1.0f});
    }
    m_pRen->f_LoadSpriteAtlas(l_vPx, (uint32_t)l_iW, (uint32_t)l_iH, l_strDir + l_oAtlas.m_vPages[0].m_strFile);
    std::fprintf(stderr, "[assets] sprites: prebuilt atlas %sgame.atlas, %dx%d, %zu region(s), art style '%s'\n", l_strDir.c_str(),
                 l_iW, l_iH, l_oAtlas.m_vRegions.size(), m_vArtStyles[l_iStyle].m_strId.empty() ? "original" : m_vArtStyles[l_iStyle].m_strId.c_str());
    m_oSpriteAtlas = std::move(l_oAtlas);
    {   // its meshes (docs/engine/16_MESH_ANIMATION.md): beside the style's atlas, else the original art's (cut from the same
        // sprites; drawn inside each region, so a different packing only changes how closely they follow the shape)
        std::string l_strMeshError;
        const std::string l_strBase = f_PathOf(toms::key::g_pszAtlasGame);
        const std::string l_strOwn = l_strDir + "game.atlas";
        if (!toms::res::f_ReadMeshFile(toms::f_SpriteMeshPath(l_strOwn), m_oSpriteMeshes, l_strMeshError))
            std::fprintf(stderr, "[assets] %s\n", l_strMeshError.c_str());
        if (m_oSpriteMeshes.m_mapMeshes.empty() && l_strOwn != l_strBase && !toms::res::f_ReadMeshFile(toms::f_SpriteMeshPath(l_strBase), m_oSpriteMeshes, l_strMeshError))
            std::fprintf(stderr, "[assets] %s\n", l_strMeshError.c_str());
    }
    f_ProvideSpriteAtlas();
    m_iSpriteAtlasRevision++;   // the UI's sprite sheet (uiSpritesheet) follows from the next frame
    m_iLoadedArtStyle = l_iStyle;
    return true;
}

// Loads the sprite atlas for art style `style` (index into artStyles_; 0 = the original art) and uploads it,
// replacing the previous one: media/atlas/game.atlas, or media/styles/<id>/atlas/game.atlas for a style. Runs at
// startup (loadAssets) and again when the player is on the title screen with another style chosen
// (refreshArtStyle). The atlases are cooked from art_raw/ by tools/atlas (art_raw/atlas/game.atlasproj); the game
// ships only them, never the raw PNGs. A style whose atlas is missing falls back to the original art. On failure
// the previous atlas and loadedArtStyle_ stay as they were.
bool Game::f_LoadSpriteAtlas(int l_iStyle) {
    if (l_iStyle < 0 || l_iStyle >= (int)m_vArtStyles.size()) l_iStyle = 0;
    if (f_LoadPrebuiltSpriteAtlas(l_iStyle)) return true;
    if (l_iStyle > 0) {
        std::fprintf(stderr, "[assets] art style '%s' has no usable atlas: using the original art\n", m_vArtStyles[l_iStyle].m_strId.c_str());
        return f_LoadPrebuiltSpriteAtlas(0);
    }
    std::fprintf(stderr, "[assets] %s/atlas/game.atlas is missing or lacks the map sprites: rebuild it with\n"
                         "  atlaspack build art_raw/atlas/game.atlasproj --all-variants\n", m_strDataDir.c_str());
    return false;
}

// The chosen style (Settings) takes effect only while the title screen is up -- nothing from a run
// is on screen then. Called when the title reopens (returnToTitle) and right after the choice was
// made on the title's own Settings page (applyArtStyle). Restarting the game also applies it.
void Game::f_RefreshArtStyle() {
    if (!m_pRen || !m_oTitle.f_IsOpen()) return;
    const int l_iChosen = f_ChosenArtStyle();
    if (l_iChosen != m_iLoadedArtStyle) f_LoadSpriteAtlas(l_iChosen);
}

bool Game::f_LoadAssets(const std::string& l_strAssetDir) {
    m_strDataDir = l_strAssetDir;
    // Create the renderer: bgfx on every platform (src/game/compat/renderer.h). The host
    // (GameSession) owns the window and frees the renderer.
    m_pRen = new Renderer();
    m_pRen->f_Init(1280, 720);   // size ignored by the bgfx renderer: the host sets the backbuffer size
    // The resource cache (docs/engine/12_RESOURCE_MANAGER.md). Any ".../atlas/game.atlas" a clip or effect names -- the
    // original art's or a style's -- is the sprite atlas loaded now (provideSpriteAtlas), so they follow the art style.
    m_upRes = std::make_unique<toms::res::ResourceManager>(m_pRen);
    // Every file the game needs by key (assets/media/resources.json); a key or a path under assets/media is how code
    // asks for one (res_->get<T>). A missing one is said on screen.
    m_upRes->f_SetRoot(l_strAssetDir);
    m_upRes->f_SetOnMissing([this](const std::string& l_strName, const std::string& l_strPath, const std::string& l_strError) { f_ResourceMissing(l_strName, l_strPath, l_strError); });
    m_upRes->f_LoadManifest("resources.json");
    toms::f_RegisterBuiltinRenderObjects(m_oRenderFactory);   // sprite, clip, particle, text, group (the floor's looks)
    m_upRes->f_SetPathMapper([](const std::string& l_strPath) {
        const size_t l_nSlash = l_strPath.find_last_of("/\\");
        const std::string l_strName = l_strPath.substr(l_nSlash == std::string::npos ? 0 : l_nSlash + 1);
        const std::string l_strParent = l_nSlash == std::string::npos ? std::string() : l_strPath.substr(0, l_nSlash);
        if (l_strName == "game.atlas" && l_strParent.size() >= 5 && l_strParent.compare(l_strParent.size() - 5, 5, "atlas") == 0)
            return std::string(toms::res::ResourceManager::m_pszSpriteAtlas);
        return l_strPath;
    });
    // Art styles (Settings > art style; art_styles.h): start with the one settings.json names.
    m_vArtStyles = toms::f_ArtStylesFromJson(f_ReadJsonFile(f_PathOf(toms::key::g_pszStylesIndex)));
    if (!f_LoadSpriteAtlas(toms::f_ArtStyleIndex(m_vArtStyles,
            toms::f_LoadGameSettings(toms::f_SettingsPath()).m_strArtStyle)))
        return false;
    // (Text is RmlUi's: see assets/media/fonts/NotoSansCJKtc-TOMS.otf and tools/make_ui_font.py.)
    // The manifest's "preload" entries (the map's clips and their atlases), now that the sprite atlas is provided.
    if (const int l_iFailed = m_upRes->f_Preload()) std::fprintf(stderr, "[res] %d preloaded resource(s) missing\n", l_iFailed);

    // S1: character-level 佔格 table (docs/design/ART_AND_ABILITY_DESIGN.md F8). An absent file leaves
    // every entity at 1x1, i.e. exactly the pre-S1 behavior -- the table is purely additive.
    {
        nlohmann::json l_jFpj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataFootprints));
        if (l_jFpj.is_object()) {
            int l_iN = 0;
            for (auto l_oIt = l_jFpj.begin(); l_oIt != l_jFpj.end(); ++l_oIt) {
                if (l_oIt.key().empty() || l_oIt.key()[0] == '_') continue;   // "_comment"
                m_mapTypeFootprints[l_oIt.key()] = toms::f_FootprintSpecFromJson(l_oIt.value(), l_oIt.key());
                if (m_mapTypeFootprints[l_oIt.key()].m_oFp.f_Big()) ++l_iN;
            }
            fprintf(stderr, "[assets] footprints.json: %d type(s) bigger than 1x1\n", l_iN);
        }
    }
    // the battle scene's tuning (data/battle.json): bar speed and the wait after a tap
    {
        nlohmann::json l_jBj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataBattle));
        if (l_jBj.is_object()) {
            m_fBarSlowScale = std::max(0.0f, l_jBj.value("bar_slow_speed_scale", 1.0f));
            m_fBarFastScale = std::max(0.05f, l_jBj.value("bar_fast_speed_scale", 1.0f));
            m_iBarCooldownMs = std::max(0, l_jBj.value("bar_cooldown_ms", (int)CombatState::m_iBarCooldownMs));
            m_iAttackPauseMs = std::max(0, l_jBj.value("attack_pause_ms", m_iAttackPauseMs));
            m_iHitNumberDelayMs = std::max(0, l_jBj.value("hit_number_delay_ms", m_iHitNumberDelayMs));
            m_jBattleJson = l_jBj;
            m_oPaceSettings = toms::f_PaceFromJson(l_jBj);     // docs/gameplay/13 §6.3
            m_oFeel = toms::f_FeelFromJson(l_jBj);             // docs/gameplay/15 §3.8
            if (l_jBj.contains("battleSpeeds") && l_jBj["battleSpeeds"].is_array()) {
                m_vBattleSpeeds.clear();
                for (const auto& l_jS : l_jBj["battleSpeeds"]) if (l_jS.is_number() && l_jS.get<float>() > 0.0f) m_vBattleSpeeds.push_back(l_jS.get<float>());
                std::sort(m_vBattleSpeeds.begin(), m_vBattleSpeeds.end());
            }
            m_oCellRules = toms::f_CellRulesFromJson(l_jBj);
            m_oTutorial = toms::f_TutorialFromJson(l_jBj);     // the new game's trial fight (docs/gameplay/19 B6)
            m_oTouchSettings = toms::f_TouchPlayFromJson(l_jBj);   // the plays' numbers (docs/gameplay/16 §3)
            m_oGrade = toms::f_GradeFromJson(l_jBj);           // §3.6
            m_oGuard = toms::f_GuardFromJson(l_jBj);           // §3.2
            m_bComboHoldsMonsters = l_jBj.value("combo_holds_monsters", false);
            m_bComboRunsThroughAttacks = l_jBj.value("combo_runs_through_attacks", true);
            m_oBreakSettings = toms::f_BreakFromJson(l_jBj);   // §6.4
            fprintf(stderr, "[assets] battle.json: bars start x%.2f, top speed x%.2f, %d ms after a tap\n", m_fBarSlowScale,
                    m_fBarFastScale, m_iBarCooldownMs);
        }
    }
    m_oWeaponPlay = toms::f_LoadWeaponPlay(f_PathOf(toms::key::g_pszDataWeaponPlay));   // docs/gameplay/07_GAMEPLAY_PLAN.md §5.6
    m_oLooks = toms::f_LooksFromJson(f_ReadJsonFile(f_PathOf(toms::key::g_pszDataLooks)));   // the status map (docs/gameplay/11 §4.2)
    m_oMoves = toms::f_MovesFromJson(f_ReadJsonFile(f_PathOf(toms::key::g_pszDataMoves)));   // the monsters' moves (docs/gameplay/13 §6.1)
    m_oSpecialMoves = toms::f_SpecialMovesFromJson(f_ReadJsonFile(f_PathOf(toms::key::g_pszDataSpecialMoves)));   // moves and special guards (docs/gameplay/16 §6)
    m_oMiniGames = toms::f_LoadMiniGames(f_PathOf(toms::key::g_pszDataMinigames));      // docs/gameplay/07 phase 6
    {   // the published version: no file until the first web publish (tools\bump_version.ps1)
        const std::string l_strVersionPath = f_PathOf(toms::key::g_pszDataVersion);
        if (toms::f_VfsExists(l_strVersionPath)) m_strVersionLabel = toms::f_VersionLabel(f_ReadJsonFile(l_strVersionPath));
    }
    m_jCharacterFit = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataCharacterFit));      // character clips' nudges (docs/engine/05)
    fprintf(stderr, "[assets] weapon_play.json: %d weapon(s)\n", (int)m_oWeaponPlay.m_mapWeapons.size());
    // load enemy templates
    nlohmann::json l_jEj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataEnemies));
    for (auto& [l_strK,l_jV] : l_jEj.items()) m_mapEnemyTpl[l_strK] = l_jV;
    // data/items.json: every item in the game, one record each ("_comment" and other "_" keys are notes).
    // Its weapons, armor and talents are also the Equipment System's content (equipment_system.h).
    nlohmann::json l_jIj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataItems));
    for (auto& [l_strK,l_jV] : l_jIj.items()) {
        if (l_strK.empty() || l_strK[0] == '_' || !l_jV.is_object()) continue;
        m_mapItemDefs[l_strK] = l_jV;
        if (f_IsGearType(l_jV.value("type", std::string()))) {
            toms::EquipmentDefinition l_oD = toms::f_EquipmentFromJson(l_jV);
            l_oD.m_strId = l_strK;
            m_mapEquipmentDefs[l_strK] = l_oD;
        }
    }
    // data/story/counters.json: the story counters' state words, shown on the Status tab once a
    // counter reaches its displayAt (the run state keeps the numbers).
    {
        nlohmann::json l_jCj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataStoryCounters));
        if (l_jCj.is_object())
            for (auto& [l_strK, l_jV] : l_jCj.items())
                if (!l_strK.empty() && l_strK[0] != '_' && l_jV.is_object() && l_jV.contains("label")) m_mapCounterLabels[l_strK] = l_jV["label"];
    }
    f_LoadEventDefs();
    f_LoadNpcs();   // NPC files and event variables (docs/gameplay/event_editor/08_NPC_DIALOGUE.md)
    // data/store.json: which of those items the store sells, and how prices grow
    f_LoadStore();
    // data/stats.json: the player's attributes and the level-up numbers (docs/gameplay/03_PLAYER_MENU.md section 3).
    {
        nlohmann::json l_jSj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataStats));
        if (l_jSj.contains("attributes") && l_jSj["attributes"].is_array())
            for (const auto& l_jA : l_jSj["attributes"]) {
                AttrDef l_oD;
                l_oD.m_strId = l_jA.value("id", std::string());
                if (l_oD.m_strId.empty()) continue;
                l_oD.m_strShortName = l_jA.value("short", l_oD.m_strId);
                l_oD.m_strNameKey = l_jA.value("name", l_oD.m_strId);
                l_oD.m_strDescKey = l_jA.value("desc", std::string());
                l_oD.m_iBase = l_jA.value("base", 5);
                l_oD.m_iPerLevel = l_jA.value("perLevel", 1);
                if (l_jA.contains("effects") && l_jA["effects"].is_array())
                    for (const auto& l_jE : l_jA["effects"])
                        if (l_jE.is_object() && l_jE.contains("stat")) l_oD.m_vEffects.push_back({l_jE.value("stat", std::string()), l_jE.value("per", 0.0f)});
                m_vAttrDefs.push_back(l_oD);
            }
        if (l_jSj.contains("levelUp") && l_jSj["levelUp"].is_object()) {
            const auto& l_jL = l_jSj["levelUp"];
            m_oLevelUp.m_iExpPerLevel = std::max(1, l_jL.value("expPerLevel", m_oLevelUp.m_iExpPerLevel));
            m_oLevelUp.m_iAtk = l_jL.value("atk", m_oLevelUp.m_iAtk);
            m_oLevelUp.m_iDef = l_jL.value("def", m_oLevelUp.m_iDef);
            m_oLevelUp.m_iMaxhp = l_jL.value("maxhp", m_oLevelUp.m_iMaxhp);
            m_oLevelUp.m_iFreePoints = l_jL.value("freePoints", m_oLevelUp.m_iFreePoints);
            m_oLevelUp.m_iMaxLevel = std::max(1, l_jL.value("maxLevel", m_oLevelUp.m_iMaxLevel));
            if (l_jL.contains("points") && l_jL["points"].is_array())
                for (const auto& l_jR : l_jL["points"])
                    if (l_jR.is_object()) m_oLevelUp.m_vPoints.push_back({std::max(2, l_jR.value("from", 2)), l_jR.value("points", 0)});
        }
    }
    // Milestone 8: load the Mission System's content (schema/logic built in Milestone 4 --
    // mission_system.h -- but missionDefs_ stayed empty until this content-authoring pass).
    {
        nlohmann::json l_jMj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataMissions));
        for (auto& [l_strK, l_jV] : l_jMj.items()) m_mapMissionDefs[l_strK] = toms::f_MissionDefinitionFromJson(l_jV);
    }
    // S4: the skill tree's content (data/skills.json) -- same "load once at boot" shape as
    // equipment/missions above.
    m_mapSkillDefs = toms::f_LoadSkillDefs(f_PathOf(toms::key::g_pszDataSkills));
    // S5: the forge's content (data/forge.json) -- same "load once at boot" shape as skillDefs_.
    m_mapForgeDefs = toms::f_LoadForgeRecipes(f_PathOf(toms::key::g_pszDataForge));
    // S6: the village hub's content (data/hub.json) -- same "load once at boot" shape as skillDefs_/
    // forgeDefs_ above.
    m_mapHubDefs = toms::f_LoadHubLocations(f_PathOf(toms::key::g_pszDataHub));
    // S7 (equipment actives, first slice): same "load once at boot" shape as skillDefs_/forgeDefs_/
    // hubDefs_ above.
    m_mapActiveDefs = toms::f_LoadActiveDefinitions(f_PathOf(toms::key::g_pszDataActives));
    // M7 (first slice): the 15-ending table, same "load once at boot" shape as the above.
    m_oEndingsTable = toms::f_LoadEndings(f_PathOf(toms::key::g_pszDataStoryEndings));
    // M8 (first slice): the rebirth config, same "load once at boot" shape as the above.
    m_oCyclesConfig = toms::f_LoadCyclesConfig(f_PathOf(toms::key::g_pszDataStoryCycles));
    // init player
    m_oPl.m_iMaxhp = m_iStartingHp; m_oPl.m_iHp = m_iStartingHp; m_oPl.m_iAtk = m_iStartingAtk; m_oPl.m_iDef = m_iStartingDef; m_oPl.m_iGold = 0; m_oPl.m_iExp = 0; m_oPl.m_iLv = 1;
    m_oPl.m_vInv = {"potion_red", "potion_blue", "exp_up"};
    f_ResetAttrs();

    // S3.5: the 70-floor tower (data/story/floors/*.json) as the run's progression source. Loaded
    // once here; empty when the data is absent, and every use is guarded, so such a checkout plays
    // the eleven hand-authored stages exactly as before.
    m_oFloors = toms::FloorTable::f_LoadFromDirectory(f_PathOf(toms::key::g_pszFolderFloors));
    f_LoadStairLocks();   // Q7: the stairs the floors lock behind events (needs the event pools, loaded above)
    if (m_oFloors.f_Empty()) {
        fprintf(stderr, "[assets] floor table: absent -- playing the hand-authored stages only\n");
    } else {
        fprintf(stderr, "[assets] floor table: %d floors across %d acts (first %s, last %s)\n",
                m_oFloors.size(), m_oFloors.f_At(m_oFloors.size()).m_iActIndex,
                m_oFloors.f_At(1).m_strId.c_str(), m_oFloors.f_At(m_oFloors.size()).m_strId.c_str());
    }

    // ---- Title phase boot ----
    // Persisted preferences (language, slot count) + the string table, then show the title.
    // modalActive() includes the title, so the world the host loads next sits inert behind it
    // until the player chooses New Game or Continue. data/text.json is under data/, which the
    // TTF codepoint collector above already globs, so its glyphs are in the atlas before this
    // runs -- on both desktop and web, since the atlas is now built the same way on both.
    {
        toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
        bool l_bHaveSettings = false;
        m_oSettings = toms::f_LoadGameSettings(toms::f_SettingsPath(), &l_bHaveSettings);
        m_oLocale.f_LoadFromFile(f_PathOf(toms::key::g_pszDataText));
        // TOMS_LANGUAGE (the test runners: "en") picks the language without saving it; a saved language that is not
        // offered (text.json "offered") plays in English
        const char* l_pszForced = std::getenv("TOMS_LANGUAGE");
        if (!m_oLocale.f_SetLanguage(l_pszForced && *l_pszForced ? std::string(l_pszForced) : m_oSettings.m_strLanguage)) m_oLocale.f_SetLanguage("en");
        if (!(l_pszForced && *l_pszForced)) m_oSettings.m_strLanguage = m_oLocale.f_LanguageCode();
        m_oCam.f_SetMode(toms::Camera::f_ModeFromIndex(m_oSettings.m_iCameraMode));
        m_oCam.f_SetViewCols(m_oSettings.m_iViewCols);
        m_oTitle.f_SetSlotCount(m_oSettings.m_iSlotCount);
        m_oTitle.f_SetLanguageCount(m_oLocale.f_LanguageCount());
        m_oTitle.f_SetLanguageIndex(m_oLocale.f_LanguageIndex());
        m_oTitle.f_SetStyleCount((int)m_vArtStyles.size());
        m_oTitle.f_SetStyleIndex(f_ChosenArtStyle());
        m_oTitle.f_Open();
        f_RefreshSlots();
        if (!l_bHaveSettings) f_ApplyLanguage();   // write defaults once, so the file exists
    }
    // init SFX subsystem (no-op if no audio device/context; headless-safe).
    m_oAudio.f_Init([this](const std::string& l_strKey) { return f_PathOf(l_strKey.c_str()); });
    f_WireMissionEvents(); // Milestone 4: subscribe mission-progress handlers once per session
    f_RollDailyMissions(); // Milestone 5: daily-mission reset check, once per session start
    return true;
}

void Game::f_ResourceMissing(const std::string& l_strName, const std::string& l_strPath, const std::string& l_strError) {
    std::fprintf(stderr, "[res] missing: %s (%s): %s\n", l_strName.c_str(), l_strPath.c_str(), l_strError.c_str());
    std::string l_strMsg = m_oLocale.f_Tr("res.missing");
    l_strMsg = l_strMsg == "res.missing" || l_strMsg.empty() ? "Missing file: " + l_strName : toms::f_FillText(l_strMsg, {{"file", l_strName}});
    if (std::find(m_vResourceMessages.begin(), m_vResourceMessages.end(), l_strMsg) == m_vResourceMessages.end()) m_vResourceMessages.push_back(l_strMsg);
}

int Game::f_SpriteLayer(const std::string& l_strId) const {
    auto l_itIt = m_mapIdToLayer.find(l_strId);
    if (l_itIt == m_mapIdToLayer.end()) l_itIt = m_mapIdToLayer.find("floor");   // a sprite the atlas lacks draws as floor, as before
    return l_itIt == m_mapIdToLayer.end() ? 0 : l_itIt->second;
}

// UV rect for a sprite: from the prebuilt atlas, or its cell in the uniform grid atlas.
void Game::f_SpriteUV(int l_iLayer, float l_arrUv[4]) const {
    if (l_iLayer >= 0 && l_iLayer < (int)m_vSpriteUVs.size()) {
        std::copy(m_vSpriteUVs[l_iLayer].begin(), m_vSpriteUVs[l_iLayer].end(), l_arrUv);
        return;
    }
    int l_iCols = m_iSpriteGridCols;
    int l_iGx = l_iLayer % l_iCols, l_iGy = l_iLayer / l_iCols;
    float l_fU0 = (float)l_iGx / l_iCols, l_fV0 = (float)l_iGy / (float)((g_iNSPRITES + l_iCols - 1) / l_iCols);
    float l_fU1 = (float)(l_iGx + 1) / l_iCols, l_fV1 = (float)(l_iGy + 1) / (float)((g_iNSPRITES + l_iCols - 1) / l_iCols);
    l_arrUv[0]=l_fU0; l_arrUv[1]=l_fV0; l_arrUv[2]=l_fU1; l_arrUv[3]=l_fV1;
}

std::string Game::f_StatusKeyAt(int l_iX, int l_iY) const {
    for (const Entity& l_oE : m_oSt.m_vEntities)
        if (l_oE.m_iX == l_iX && l_oE.m_iY == l_iY && !l_oE.m_strUid.empty()) return toms::f_EntityStatusKey(m_oSt.m_strId, l_iX, l_iY, l_oE.m_strUid);
    return toms::f_EntityStatusKey(m_oSt.m_strId, l_iX, l_iY);
}

void Game::f_MigrateStatusKeys() {
    for (const Entity& l_oE : m_oSt.m_vEntities) {
        const std::string l_strNow = toms::f_EntityStatusKey(m_oSt.m_strId, l_oE.m_iX, l_oE.m_iY, l_oE.m_strUid);
        if (m_mapEntityStatus.count(l_strNow)) continue;
        // the keys this object may have been saved under: its position, by the floor id or by the stage file's name
        for (const std::string& l_strOld : {toms::f_EntityStatusKey(m_oSt.m_strId, l_oE.m_iX, l_oE.m_iY), toms::f_EntityStatusKey(m_strCurStage, l_oE.m_iX, l_oE.m_iY)}) {
            if (l_strOld == l_strNow) continue;
            auto l_itIt = m_mapEntityStatus.find(l_strOld);
            if (l_itIt == m_mapEntityStatus.end()) continue;
            m_mapEntityStatus[l_strNow] = l_itIt->second;
            m_mapEntityStatus.erase(l_itIt);
            break;
        }
    }
}

void Game::f_LoadStage(const std::string& l_strId, StageArrival l_eArrival) {
    const std::string l_strFromStage = m_oSt.m_strId;   // the floor being left: arriving by stairs lands on the stair back to it
    m_oFloor.f_Clear();   // the objects of the floor being left (a door opening on it stops)
    f_CancelWalk();   // a click-to-move route belongs to the old floor
    m_strCurStage = l_strId;
    // Resolve the stage JSON. Data ids in connect.up/down use "stage_02" (underscore)
    // while the shipped files are named "stage02.json" (no underscore) — and the
    // initial load uses "stage01". Normalize so both forms resolve instead of
    // opening a non-existent path (which makes ifstream fail -> parse throw -> crash
    // when the player steps on stairs / a warp tile).
    std::string l_strPath = f_PathFor(toms::pattern::g_pszStage, l_strId);
    if (!toms::f_VfsExists(l_strPath)) {
        std::string l_strNoUs = l_strId;
        l_strNoUs.erase(std::remove(l_strNoUs.begin(), l_strNoUs.end(), '_'), l_strNoUs.end());
        std::string l_strAlt = f_PathFor(toms::pattern::g_pszStage, l_strNoUs);
        if (toms::f_VfsExists(l_strAlt)) l_strPath = l_strAlt;
    }
    // a stage file by its path (tests: toms_game --stage=tests/stage/objects.stage.json)
    if (l_strId.size() > 5 && l_strId.compare(l_strId.size() - 5, 5, ".json") == 0 && toms::f_VfsExists(l_strId)) l_strPath = l_strId;
    // S3.5 (a): a floor id the tower knows (F01..F70) resolves through the floor table instead of
    // the bare data/stages/ path -- a normal floor to its generated grid, a boss floor to the
    // hand-authored map the spec names (section 3.2). Ids that are not floors (the eleven
    // hand-authored stages, e.g. when the floor data is absent) keep the old behaviour below.
    const toms::FloorInfo* l_pFloor = m_oFloors.f_Find(l_strId);
    bool l_bIsFloor = (l_pFloor != nullptr);
    if (l_bIsFloor) {
        std::string l_strRel = m_oFloors.f_MapRelPath(l_strId);
        std::string l_strCandidate = f_PathOf(toms::key::g_pszFolderAssets) + "/" + l_strRel;   // rel is under assets/
        if (toms::f_VfsExists(l_strCandidate)) l_strPath = l_strCandidate;
    }
    if (!l_bIsFloor) {
        // S2 fallback: generated floors also live in data/story/floors/<id>.stage.json (the file
        // layout docs/story/STORY_DATA_SCHEMA.md section 1.2 asks for); tried last so the hand-authored
        // data/stages/*.json files always win.
        if (!toms::f_VfsExists(l_strPath)) {
            std::string l_strFloorPath = f_PathFor(toms::pattern::g_pszFloorStage, l_strId);
            if (toms::f_VfsExists(l_strFloorPath)) l_strPath = l_strFloorPath;
        }
    }
    m_oSt = f_ParseStage(l_strPath, m_oLocale);
    m_strCurFloorId = l_bIsFloor ? l_strId : std::string();
    if (l_pFloor) {
        // The floor -- not the map file -- is the unit the run counts and names. A boss floor's map
        // is hand-authored and carries the act's own index/name (stage01 has index 1), so without
        // this the HUD would show the wrong floor number on every boss stage.
        m_oSt.m_strId = l_pFloor->m_strId;
        m_oSt.m_iIndex = l_pFloor->m_iSeq;                       // 1..70 -> the HUD's "n/totalStages"
        std::string l_strResolved = l_pFloor->m_strNameKey;
        if (l_strResolved.rfind("story.", 0) == 0) {
            std::string l_strTr = m_oLocale.f_Tr(l_strResolved);
            if (!l_strTr.empty() && l_strTr != l_strResolved) l_strResolved = l_strTr;   // missing key -> keep the map's name
            else l_strResolved = m_oSt.m_strName;
        }
        if (!l_strResolved.empty()) m_oSt.m_strName = l_strResolved;
        // Progression lives here (S3.5 a): the specs' nextFloor chain, translated into the two
        // fields the existing stair logic already reads. The generated grids place the goal on 'U'
        // (far from the entrance) and the way back on 'D' (beside it), which is exactly this.
        m_oSt.m_strUp = l_pFloor->m_strNextFloor;
        m_oSt.m_strDown = m_oFloors.f_Prev(l_pFloor->m_strId);
        // S3.5 (c)/(e): the floor's story keys, and the act whose palette this floor renders in.
        m_strStoryIntroKey = l_pFloor->m_strIntroKey;
        m_vStoryAmbientKeys = l_pFloor->m_vAmbientKeys;
        m_iStoryTurns = 0;
        m_iThemeActIndex = l_pFloor->m_iActIndex;
        m_fChapterCardMs = 0.0f;
        if (l_pFloor->m_iIndexInAct == 1 && !l_pFloor->m_strAct.empty()) {   // first floor of an act -> act card
            std::string l_strTitle = m_oLocale.f_Tr("story." + l_pFloor->m_strActKey + ".title");
            if (l_strTitle.rfind("story.", 0) != 0) {               // resolved (missing key -> the key)
                m_strChapterCardTitle = l_strTitle;
                m_fChapterCardMs = 2600.0f;
            }
        }
        // S4: the chapter's own grants (skills/skillPoints/hub/unlocks). Deliberately NOT gated on
        // indexInAct==1 like the act card above -- a save resumed mid-chapter (Continue landing on,
        // say, F05) would otherwise never receive ch_01's grant at all, since F05 never re-triggers
        // "the first floor of an act". applyChapterGrants' own flag makes this a no-op once already
        // granted, so calling it on every floor of every chapter is correct, just occasionally
        // (once per chapter per run) does real work instead of none.
        if (!l_pFloor->m_strAct.empty()) f_ApplyChapterGrants(l_pFloor->m_strAct);
    } else {
        m_strStoryIntroKey.clear();
        m_vStoryAmbientKeys.clear();
        m_iStoryTurns = 0;
        m_iThemeActIndex = 1;
        m_fChapterCardMs = 0.0f;
    }
    // Q7 / 05_FLOOR_LINKS: every stair as a list with its tile and lock. A floor's own `stairs` list wins over the
    // chain; st.up / st.down stay the first stair up / down.
    if (l_pFloor && l_pFloor->m_bHasStairs) {
        m_oSt.m_vStairs = l_pFloor->m_vStairs;
        m_oSt.m_strUp.clear();
        m_oSt.m_strDown.clear();
        for (const toms::StairLink& l_oS : m_oSt.m_vStairs) {
            std::string& l_strFirst = l_oS.m_bUp ? m_oSt.m_strUp : m_oSt.m_strDown;
            if (l_strFirst.empty()) l_strFirst = l_oS.m_strTo;
        }
    } else {
        m_oSt.m_vStairs = toms::f_DefaultStairLinks(m_oSt.m_strUp, m_oSt.m_strDown);
    }
    toms::f_PlaceStairLinks(m_oSt.m_vStairs, m_oSt.m_vTiles, m_oSt.m_mapLegend);
    for (const toms::StairLink& l_oS : m_oSt.m_vStairs)   // W38: remembered, so the Events tab can say where a stair is from any floor
        if (l_oS.m_iX >= 0) m_mapStairSpots[m_oSt.m_strId + ">" + l_oS.m_strTo] = {l_oS.m_iX, l_oS.m_iY};
    for (auto& l_oE : m_oSt.m_vEntities)   // a "stairs_up:F96" tile is a plain stair tile now
        for (const char* l_pszK : {"stairs_up", "stairs_down"})
            if (l_oE.m_strKind.rfind(std::string(l_pszK) + ":", 0) == 0) l_oE.m_strKind = l_pszK;
    // Milestone 7: parseStage() rebuilds every entity fresh from JSON on every call (stairs,
    // Stage Select, etc.), so re-apply any previously-persisted Defeated/Collected status here --
    // otherwise a monster the player already beat or an item they already picked up would come
    // back the next time this floor loads, contradicting the "cleared floors stay cleared"
    // decision. Doors are intentionally not covered (see entityStatus_'s declaration in game.h).
    // A door opened earlier stays open (openDoor): its tile is floor and its "door:" entry no longer blocks.
    f_MigrateStatusKeys();
    for (int l_iY = 0; l_iY < (int)m_oSt.m_vTiles.size(); ++l_iY)
        for (int l_iX = 0; l_iX < (int)m_oSt.m_vTiles[l_iY].size(); ++l_iX) {
            const char l_cC = m_oSt.m_vTiles[l_iY][l_iX];
            if ((l_cC == 'y' || l_cC == 'b' || l_cC == 'r') &&
                toms::f_GetEntityStatus(m_mapEntityStatus, f_StatusKeyAt(l_iX, l_iY)) == toms::EntityStatus::Opened) {
                m_oSt.m_vTiles[l_iY][l_iX] = '.';
                for (auto& l_oE : m_oSt.m_vEntities) if (l_oE.m_iX == l_iX && l_oE.m_iY == l_iY && l_oE.m_strKind.rfind("door:", 0) == 0) l_oE.m_bConsumed = true;
            }
        }
    for (auto& l_oE : m_oSt.m_vEntities) {
        bool l_bIsMonster = l_oE.m_strKind.rfind("monster:", 0) == 0;
        // an event fired on this tile does not come back (it would fire, and give its items, again); the same
        // event id may sit on other floors, so this goes by the tile, like items
        bool l_bIsItem = l_oE.m_strKind.rfind("item:", 0) == 0 || l_oE.m_strKind.rfind("event:", 0) == 0;
        const bool l_bIsGate = l_oE.m_strKind.rfind("gate:", 0) == 0;   // a gate opened with its key stays open
        if (!l_bIsMonster && !l_bIsItem && !l_bIsGate) continue;
        toms::EntityStatus l_eWant = l_bIsMonster ? toms::EntityStatus::Defeated : l_bIsGate ? toms::EntityStatus::Opened
                                                                                     : toms::EntityStatus::Collected;
        toms::EntityStatus l_eHave = toms::f_GetEntityStatus(m_mapEntityStatus, toms::f_EntityStatusKey(m_oSt.m_strId, l_oE.m_iX, l_oE.m_iY, l_oE.m_strUid));
        if (l_eHave == l_eWant) {
            l_oE.m_bConsumed = true;
            m_oSt.m_vTiles[l_oE.m_iY][l_oE.m_iX] = '.';
        }
    }
    // S1: resolve every entity's 佔格 now that both sources are known: the stage file's own
    // "footprints" entry (a per-floor decision) beats data/footprints.json (the character's tier),
    // which beats 1x1. Done here rather than in parseStage() because parseStage() knows nothing
    // about the type table -- and routed through the shared helper so footprint_test.cpp's data
    // validator resolves exactly the way the game does at runtime.
    int l_iBigCount = 0;
    for (auto& l_oE : m_oSt.m_vEntities) {
        auto l_itIt = m_mapTypeFootprints.find(l_oE.m_strKind);
        toms::FootprintSpec l_oTypeSpec = (l_itIt == m_mapTypeFootprints.end()) ? toms::FootprintSpec{} : l_itIt->second;
        toms::FootprintSpec l_oSpec = toms::f_ResolveFootprint(
            toms::FootprintSpec{l_oE.m_oFp, l_oE.m_bRoamer, l_oE.m_strDisplayName}, l_oE.m_bFpExplicit, l_oTypeSpec);
        l_oE.m_oFp = l_oSpec.m_oFp;
        l_oE.m_bRoamer = l_oSpec.m_bRoamer;
        l_oE.m_strDisplayName = l_oSpec.m_strName;
        if (l_oE.m_oFp.f_Big()) ++l_iBigCount;
    }
    if (l_iBigCount)
        fprintf(stderr, "[stage] %s: %d entity(ies) occupy more than 1 grid\n", m_oSt.m_strId.c_str(), l_iBigCount);

    // S1: (re)build the roamer brains for this floor. Always cleared -- the entity vector was just
    // rebuilt from JSON, so a stale index would drive a different monster. Seeds are derived from
    // the stage id + entity index, never from the clock: reloading a floor (stairs up/down, Stage
    // Select) reproduces the same wander, so a chase stays reproducible instead of being noise.
    m_vRoamers.clear();
    for (int l_iI = 0; l_iI < (int)m_oSt.m_vEntities.size(); ++l_iI) {
        const Entity& l_oE = m_oSt.m_vEntities[l_iI];
        if (!l_oE.m_bRoamer || l_oE.m_bConsumed) continue;
        uint32_t l_uH = 2166136261u;   // FNV-1a over the stage id
        for (char l_cCh : m_oSt.m_strId) { l_uH ^= (unsigned char)l_cCh; l_uH *= 16777619u; }
        m_vRoamers.emplace_back(l_iI, toms::Roamer(l_oE.m_iX, l_oE.m_iY, l_oE.m_oFp, l_uH ^ (uint32_t)(l_iI * 2654435761u)));
        fprintf(stderr, "[stage] roamer '%s' (%dx%d) at (%d,%d) on %s\n",
                l_oE.m_strId.c_str(), l_oE.m_oFp.m_iW, l_oE.m_oFp.m_iH, l_oE.m_iX, l_oE.m_iY, m_oSt.m_strId.c_str());
    }
    // Milestone 3: entering a floor for the first time advances the main-story beat — this is
    // exactly what connect.up already does today (docs/design/GAME_DESIGN_DOCUMENT.md §5's 1:1 floor<->beat
    // mapping); now it's also written into persisted story state instead of being purely
    // implicit in "which floor am I standing on."
    toms::f_AdvanceStoryBeat(m_oMeta, m_oSt.m_iIndex);
    // Milestone 5: reaching a floor unlocks it as a Stage Select entry (architecture-doc §10 /
    // §5.2's stageCleared) -- every floor the player has ever reached becomes individually
    // re-selectable from the hub. Uses st.id (the parsed, canonical id) rather than the raw
    // `id` argument, since the two can differ by underscore normalization above.
    if (std::find(m_oMeta.m_vUnlockedStages.begin(), m_oMeta.m_vUnlockedStages.end(), m_oSt.m_strId) == m_oMeta.m_vUnlockedStages.end())
        m_oMeta.m_vUnlockedStages.push_back(m_oSt.m_strId);
    // derive total stage count: the 70-floor tower when its data is present (S3.5 b, so the HUD
    // counter is honest), else the historical max index over data/stages/.
    if (f_FloorMode()) {
        m_iTotalStages = m_oFloors.size();
    } else {
    m_iTotalStages = 1;
    std::string l_strDir = f_PathOf(toms::key::g_pszFolderStages) + "/";
    {
        for (const std::string& l_strName : toms::f_VfsListDir(l_strDir)) {
            try {
                nlohmann::json l_jJ = f_ReadJsonFile(l_strDir + l_strName);
                int l_iIdx = l_jJ.value("index", 0);
                if (l_iIdx > m_iTotalStages) m_iTotalStages = l_iIdx;
            } catch (...) {}
        }
    }
    }
    // store unlock: when entering the configured unlock stage for the first time,
    // mark the shop as unlocked and pop a one-time "shop unlocked!" dialog.
    if (m_oSt.m_iIndex >= m_iStoreUnlockStage && !m_bStoreUnlocked) {
        m_bStoreUnlocked = true;
        m_bStoreUnlockDlg = true;
        m_bNewsStore = true;   // a dot on ≡ until the store is opened
    }

    // M9 (stair alignment): arriving via a specific staircase lands exactly on the matching
    // stairs tile -- every floor's stairs_down is now forced to sit where the previous floor's
    // stairs_up landed (see docs/story/STAIR_ALIGNMENT.md), so this is always a real, walkable
    // spot, not a guess. Fresh loads (new game, Stage Select, rebirth, a debug jump) keep the old
    // '@'-or-default behavior -- falling back to whichever stair the floor does have if it has
    // no '@' (true for every floor except F01, the chain's head).
    m_oPl.m_iX = 1; m_oPl.m_iY = (int)m_oSt.m_iHeight - 2;
    const char* l_pszWantKind = (l_eArrival == StageArrival::FromBelow) ? "stairs_down"
                         : (l_eArrival == StageArrival::FromAbove) ? "stairs_up" : nullptr;
    bool l_bPlaced = false;
    if (l_eArrival != StageArrival::Fresh) {   // the stair that leads back to the floor just left (05 section 3.3)
        const int l_iI = toms::f_ArrivalStair(m_oSt.m_vStairs, l_strFromStage, l_eArrival == StageArrival::FromBelow);
        if (l_iI >= 0) { m_oPl.m_iX = m_oSt.m_vStairs[l_iI].m_iX; m_oPl.m_iY = m_oSt.m_vStairs[l_iI].m_iY; l_bPlaced = true; }
    }
    if (!l_bPlaced && l_pszWantKind) {
        for (auto& l_oE : m_oSt.m_vEntities)
            if (l_oE.m_strKind == l_pszWantKind) { m_oPl.m_iX = l_oE.m_iX; m_oPl.m_iY = l_oE.m_iY; l_bPlaced = true; break; }
    }
    if (!l_bPlaced && m_oSt.m_iStartX >= 0) { m_oPl.m_iX = m_oSt.m_iStartX; m_oPl.m_iY = m_oSt.m_iStartY; l_bPlaced = true; }   // '@' (parseStage)
    if (!l_bPlaced) {
        for (auto& l_oE : m_oSt.m_vEntities)
            if (l_oE.m_strKind == "stairs_down" || l_oE.m_strKind == "stairs_up") { m_oPl.m_iX = l_oE.m_iX; m_oPl.m_iY = l_oE.m_iY; break; }
    }
    m_oFloor.f_Build(*this);   // a class per thing on the floor, with its looks (map_objects.h)
    // A floor change must never visibly pan in from wherever the camera was on the PREVIOUS
    // floor (different grid, different scale of "makes sense") -- jump straight to the new
    // floor's starting view instead of easing into it.
    m_oCam.f_Snap();
}
