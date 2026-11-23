// game_scene_draw.cpp — the per-frame world: draw() (the map, its entities and the player; the UI
// on top is RmlUi -- see game_ui.cpp) and the two ImGui developer tools (F1 debug overlay, F2
// styling spike). Split out of game.cpp 2026-09-13; UI drawing moved to RmlUi 2026-09-27.
#include "game_internal.h"

#include <stb_image.h>   // declarations only (game_assets.cpp has the implementation)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

using namespace toms::game_detail;

void Game::f_Draw() {
    m_pRen->begin();
    // Title phase (Boot screen): drawn INSTEAD of the world, then nothing else. There is
    // nothing meaningful to show behind it yet, and skipping the world draw keeps the title's
    // layout independent of whatever stage happens to be loaded underneath it.
    if (m_oTitle.f_IsOpen()) { f_DrawPreviewAnim(); m_pRen->end(); return; }
    // M7 (first slice): an active ending takes over the whole screen the same way the title does
    // -- nothing behind it is meaningful once a run has actually ended (see triggerEnding()).
    if (f_EndingActive()) { f_DrawPreviewAnim(); m_pRen->end(); return; }
    const bool l_bShowStore = f_StoreModal();
    // Milestone 7 bugfix (still relevant under Battle System v2): this used to fall back to
    // scanning cs.log for the substring "倒下" to keep the overlay open during the brief post-lose
    // result-pause (cs.active and cs.won are both already false by then). That substring also
    // appears in the WIN message ("...敵人倒下了！" -- the enemy fell down), so after a win was
    // dismissed (cs.won reset to false) the overlay would never actually close, since the old log
    // text still matched. cs.resultPauseMs is the actual, non-fragile signal for "a loss was just
    // resolved and needs its readability beat" -- see finishCombatLose() and update()'s own
    // comment on this countdown.
    const bool l_bShowBattle = !l_bShowStore && (m_iHideMask & 1) == 0 && (m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0);
    const bool l_bShowTalk   = !l_bShowStore && !l_bShowBattle && (m_iHideMask & 2) == 0 && m_bInDialogue;
    const bool l_bShowWalk   = !l_bShowStore && !l_bShowBattle && !l_bShowTalk;   // the player menu draws over the map

    // ---- walking scene: the floor in 3D (game_walk3d.cpp, docs/gameplay/24): the host drew its walls and ground; its
    // objects stand on it (each draws itself by status: map_objects.h). Still loading: nothing, the loading circle ----
    if (l_bShowWalk && m_bWalk3DOn) {
        m_pRen->f_SetNode(NODE_STAGE);
        f_DrawWalk3D();
        f_DrawStylingSpikeBackdrop();
    }
    // Battle / dialogue / inventory / store: the RmlUi documents fill the screen, no world behind. A weapon with battle
    // clips draws the player's character and the weapon where the battle screen's player picture would be.
    if (l_bShowBattle) f_DrawBattleRig();
    f_DrawLoadingCircle();
    f_DrawPreviewAnim(); m_pRen->end();
}

// The ground's marks, drawn on the map's pixels (the 3D floor lays them on its ground, game_walk3d.cpp): the door tiles
// (the walls, the plain floor and the stairs are the 3D model) and the click-to-move target.
void Game::f_DrawGroundMarks(const MapView& l_oV) {
    const float l_fOx = l_oV.m_fOx, l_fOy = l_oV.m_fOy, l_fTs = l_oV.m_fTs;
    static const float l_arrWhite[4] = {1, 1, 1, 1};
    for (int l_iY = 0; l_iY < m_oSt.m_iHeight; l_iY++) for (int l_iX = 0; l_iX < m_oSt.m_iWidth; l_iX++) {
        const char l_cC = m_oSt.f_At(l_iX, l_iY);
        if (l_cC == '#' || l_cC == 'U' || l_cC == 'D' || f_CellSprite(l_cC) == "floor") continue;   // the 3D floor has them
        const int l_iLayer = f_SpriteLayer(f_CellSprite(l_cC));
        m_pRen->f_DrawSprite(f_SpriteQuad(l_fOx + l_iX * l_fTs, l_fOy + l_iY * l_fTs, l_fTs, l_fTs, l_iLayer, l_arrWhite));
    }
    // Click-to-move destination: a soft gold plate on the target tile while walking there.
    if (f_Walking() && m_iWalkTargetX >= 0) {
        const float l_fM = l_fTs * 0.12f;
        Quad l_oQ; l_oQ.m_arrRect[0] = l_fOx + m_iWalkTargetX * l_fTs + l_fM; l_oQ.m_arrRect[1] = l_fOy + m_iWalkTargetY * l_fTs + l_fM;
        l_oQ.m_arrRect[2] = l_fTs - 2 * l_fM; l_oQ.m_arrRect[3] = l_fTs - 2 * l_fM;
        l_oQ.m_arrUv[0] = 0; l_oQ.m_arrUv[1] = 0; l_oQ.m_arrUv[2] = 1; l_oQ.m_arrUv[3] = 1; l_oQ.m_bSolid = true;
        l_oQ.m_arrTint[0] = 1.0f; l_oQ.m_arrTint[1] = 0.85f; l_oQ.m_arrTint[2] = 0.3f; l_oQ.m_arrTint[3] = 0.35f;
        m_pRen->f_DrawSprite(l_oQ);
    }
}

// A stair's sign, drawn in the square (x, y, size) that stands over it on the 3D floor (f_DrawWalk3D): Q7, a locked
// stair's padlock until its event is finished; else its way, chevrons rising (up) or sinking (down).
bool Game::f_DrawStairSign(const toms::StairLink& l_oS, float l_fX0, float l_fY0, float l_fTs) {
    auto l_fnSolid = [&](float l_fX, float l_fY, float l_fW, float l_fH, float l_fR, float l_fG, float l_fB, float l_fA) {
        Quad l_oQ; l_oQ.m_arrRect[0] = l_fX; l_oQ.m_arrRect[1] = l_fY; l_oQ.m_arrRect[2] = l_fW; l_oQ.m_arrRect[3] = l_fH;
        l_oQ.m_arrUv[0] = 0; l_oQ.m_arrUv[1] = 0; l_oQ.m_arrUv[2] = 1; l_oQ.m_arrUv[3] = 1; l_oQ.m_bSolid = true;
        l_oQ.m_arrTint[0] = l_fR; l_oQ.m_arrTint[1] = l_fG; l_oQ.m_arrTint[2] = l_fB; l_oQ.m_arrTint[3] = l_fA;
        m_pRen->f_DrawSprite(l_oQ);
    };
    const float l_fU = l_fTs / 16.0f;
    if (f_StairLocked(l_oS)) {
        l_fnSolid(l_fX0 + 5 * l_fU, l_fY0 + 3 * l_fU, 1.5f * l_fU, 5 * l_fU, 0.20f, 0.16f, 0.08f, 1.0f);       // shackle, left
        l_fnSolid(l_fX0 + 9.5f * l_fU, l_fY0 + 3 * l_fU, 1.5f * l_fU, 5 * l_fU, 0.20f, 0.16f, 0.08f, 1.0f);    // shackle, right
        l_fnSolid(l_fX0 + 5 * l_fU, l_fY0 + 2.5f * l_fU, 6 * l_fU, 1.5f * l_fU, 0.20f, 0.16f, 0.08f, 1.0f);    // shackle, top
        l_fnSolid(l_fX0 + 3.5f * l_fU, l_fY0 + 7 * l_fU, 9 * l_fU, 7 * l_fU, 0.20f, 0.16f, 0.08f, 1.0f);       // body outline
        l_fnSolid(l_fX0 + 4.5f * l_fU, l_fY0 + 8 * l_fU, 7 * l_fU, 5 * l_fU, 0.98f, 0.80f, 0.30f, 1.0f);       // body
        l_fnSolid(l_fX0 + 7.5f * l_fU, l_fY0 + 9 * l_fU, 1 * l_fU, 2.5f * l_fU, 0.20f, 0.16f, 0.08f, 1.0f);    // keyhole
        return true;
    }
    // Its way, so the steps cannot be misread (the owner, 2026-11-23): three chevrons that keep rising over a stair up
    // (gold, "^") or keep sinking over a stair down (sky blue, "v"), each fading in at one end and out at the other.
    // W38: a stair just opened shows them brighter, quicker and pulsing, until it is taken.
    const bool l_bOpened = f_StairJustOpened(m_oSt.m_strId, l_oS);
    const nlohmann::json l_jSign = m_jStage3D.is_object() ? m_jStage3D.value("stairSign", nlohmann::json::object()) : nlohmann::json::object();
    const float l_fSpeed = l_jSign.value("speed", 0.8f) * (l_bOpened ? 1.6f : 1.0f);
    const float l_fP = (float)std::fmod(m_dIdleClockMs / 1000.0 * l_fSpeed, 1.0);   // the chevrons' march, 0..1 a step
    const float l_fPulse = l_bOpened ? 0.8f + 0.2f * std::sin((float)std::fmod(m_dIdleClockMs, 600.0) / 600.0f * 6.2831853f) : 1.0f;
    const float l_arrUp[3] = {1.0f, 0.85f, 0.3f}, l_arrDown[3] = {0.45f, 0.78f, 1.0f};
    const float* l_pC = l_oS.m_bUp ? l_arrUp : l_arrDown;
    const float l_fCx = l_fX0 + l_fTs * 0.5f, l_fSq = 1.7f * l_fU, l_fRim = 0.45f * l_fU;
    const int l_iCount = 3;
    for (int l_iK = 0; l_iK < l_iCount; l_iK++) {
        const float l_fK = (float)l_iK + l_fP;                                       // 0 .. 3: along the way
        const float l_fFade = std::sin(3.14159265f * l_fK / (float)l_iCount) * l_fPulse;
        // up: from the bottom of the square to its top; down: the other way
        const float l_fApex = l_oS.m_bUp ? l_fY0 + 13.0f * l_fU - l_fK * 3.6f * l_fU : l_fY0 + 3.0f * l_fU + l_fK * 3.6f * l_fU;
        for (int l_iPass = 0; l_iPass < 2; l_iPass++)   // a dark rim first, then the colour
            for (int l_iI = 0; l_iI < 4; l_iI++) {
                const float l_fDy = (l_oS.m_bUp ? 1.0f : -1.0f) * (float)l_iI * l_fU;   // the arms go away from the apex
                for (int l_iSide = -1; l_iSide <= 1; l_iSide += 2) {
                    if (l_iI == 0 && l_iSide > 0) continue;   // (the apex once)
                    const float l_fX = l_fCx + (float)l_iSide * (float)l_iI * 1.15f * l_fU - l_fSq * 0.5f, l_fY = l_fApex + l_fDy - l_fSq * 0.5f;
                    if (l_iPass == 0) l_fnSolid(l_fX - l_fRim, l_fY - l_fRim, l_fSq + 2 * l_fRim, l_fSq + 2 * l_fRim, 0.12f, 0.09f, 0.05f, 0.85f * l_fFade);
                    else l_fnSolid(l_fX, l_fY, l_fSq, l_fSq, l_pC[0], l_pC[1], l_pC[2], l_fFade);
                }
            }
    }
    return true;
}

// The cache's error for a file, worded as the previews always reported it.
static std::string f_ResourceError(const std::string& l_strFile, const toms::res::Resource& l_oR) {
    return l_oR.f_Error() == "cannot read" ? "cannot read " + l_strFile : l_strFile + ": " + l_oR.f_Error();
}

// A preview's file comes from the command line: relative to the working folder, not to assets/media (the cache's root).
static std::string f_FromWorkingDir(const std::string& l_strFile) {
#if !defined(__ANDROID__)
    if (!l_strFile.empty() && l_strFile[0] != '/' && l_strFile[0] != '\\' && !(l_strFile.size() > 1 && l_strFile[1] == ':')) {
        std::error_code l_oEc;
        const std::filesystem::path l_oAbs = std::filesystem::absolute(toms::f_VfsPath(l_strFile), l_oEc);
        if (!l_oEc) return toms::f_VfsUtf8(l_oAbs);
    }
#endif
    return l_strFile;
}

bool Game::f_PlayPreviewAnim(const std::string& l_strFile, const std::string& l_strClip, std::string& l_strError) {
    if (!m_upRes) { l_strError = "the renderer is not up yet"; return false; }
    std::shared_ptr<toms::res::AnimRes> l_spA = m_upRes->f_Get<toms::res::AnimRes>(f_FromWorkingDir(l_strFile));
    m_upRes->f_Refresh(l_spA);   // the anim editor may have saved it since it was last played
    if (!l_spA->f_Ok()) { l_strError = f_ResourceError(l_strFile, *l_spA); return false; }
    if (!l_spA->f_Find(l_strClip)) { l_strError = l_strFile + " has no clip '" + l_strClip + "'"; return false; }
    m_spPreviewAnim = l_spA;
    m_strPreviewClip = l_strClip;
    m_iPreviewAnimVer = l_spA->f_Version();
    f_StartPreviewClip();
    m_vPreviewMissing.clear();
    std::fprintf(stderr, "[anim] preview %s#%s (%.2fs, %zu atlas(es))\n", l_strFile.c_str(), l_strClip.c_str(),
                 l_spA->f_Find(l_strClip)->f_Duration(), l_spA->f_Atlases().m_vEntries.size());
    return true;
}

void Game::f_StartPreviewClip() {
    const toms::anim::Clip* l_pC = m_spPreviewAnim ? m_spPreviewAnim->f_Find(m_strPreviewClip) : nullptr;
    if (l_pC && !m_strPreviewCharacter.empty()) {
        m_oPreviewFitted = f_FitClipToCharacter(*l_pC, m_strPreviewCharacter);
        l_pC = &m_oPreviewFitted;
    }
    // A held thing (--held): its clip of the same name, bound at the character's hand; the two play as one.
    if (!m_strPreviewHeldFile.empty() && m_upRes) {
        m_spPreviewHeld = m_upRes->f_Get<toms::res::AnimRes>(f_FromWorkingDir(m_strPreviewHeldFile));
        m_upRes->f_Refresh(m_spPreviewHeld);
        const toms::anim::Clip* l_pHeld = m_spPreviewHeld->f_Ok() ? m_spPreviewHeld->f_Find(m_strPreviewClip) : nullptr;
        if (!l_pHeld)
            std::fprintf(stderr, "[anim] --held=%s: %s\n", m_strPreviewHeldFile.c_str(),
                         m_spPreviewHeld->f_Ok() ? ("no clip '" + m_strPreviewClip + "'").c_str() : m_spPreviewHeld->f_Error().c_str());
        m_oPreviewRig.f_Play(l_pC, l_pHeld);
        m_oPreviewPlayer.f_Stop();
        return;
    }
    m_oPreviewPlayer.f_Play(l_pC);
    m_vPreviewCrowd.assign(size_t(std::max(0, m_iPreviewCrowdN)), toms::anim::AnimPlayer());
    for (size_t l_nI = 0; l_nI < m_vPreviewCrowd.size(); l_nI++) {   // each a little later: the crowd is not in step
        m_vPreviewCrowd[l_nI].f_Play(l_pC);
        m_vPreviewCrowd[l_nI].f_Update(int(l_nI * 37));
    }
}

toms::anim::Clip Game::f_FitClipToCharacter(const toms::anim::Clip& l_oClip, const std::string& l_strSprite) const {
    auto l_fnBoxOf = [this](const std::string& l_strRef, toms::SpriteBox& l_oOut) {
        auto l_itIt = m_mapSpriteBoxes.find(toms::anim::f_ParseSpriteRef(l_strRef).m_strName);
        if (l_itIt == m_mapSpriteBoxes.end()) return false;
        l_oOut = l_itIt->second;
        return true;
    };
    glm::vec2 l_v2Nudge(0.0f);
    if (m_jCharacterFit.is_object() && m_jCharacterFit.contains(l_strSprite)) {
        const nlohmann::json& l_jHand = m_jCharacterFit[l_strSprite].value("hand", nlohmann::json::array());
        if (l_jHand.is_array() && l_jHand.size() == 2) l_v2Nudge = {l_jHand[0].get<float>(), l_jHand[1].get<float>()};
    }
    return toms::anim::f_FitCharacter(l_oClip, l_strSprite, l_fnBoxOf, l_v2Nudge);
}

bool Game::f_PlayPreviewFx(const std::string& l_strFile, const std::string& l_strEffect, std::string& l_strError) {
    if (!m_upRes) { l_strError = "the renderer is not up yet"; return false; }
    std::shared_ptr<toms::res::ParticleRes> l_spF = m_upRes->f_Get<toms::res::ParticleRes>(f_FromWorkingDir(l_strFile));
    m_upRes->f_Refresh(l_spF);   // the particle editor may have saved it since
    if (!l_spF->f_Ok()) { l_strError = f_ResourceError(l_strFile, *l_spF); return false; }
    if (!l_spF->f_Find(l_strEffect)) { l_strError = l_strFile + " has no effect '" + l_strEffect + "'"; return false; }
    for (const toms::fx::Problem& l_oP : toms::fx::f_CheckParticles(l_spF->f_File(), &l_spF->f_Atlases()))
        std::fprintf(stderr, "[fx] %s %s: %s\n", l_oP.m_bWarning ? "warning" : "error", l_oP.m_strWhere.c_str(), l_oP.m_strText.c_str());
    m_spPreviewFxFile = l_spF;
    m_strPreviewFxName = l_strEffect;
    m_iPreviewFxVer = l_spF->f_Version();
    const toms::fx::Effect* l_pE = l_spF->f_Find(l_strEffect);
    // Placed before play(): the prewarm already makes world-space particles where the effect is.
    m_oPreviewFx.f_SetTransform(toms::anim::f_Placement(m_pRen->f_Width() / 2.0f, m_pRen->f_Height() / 2.0f));
    m_oPreviewFx.f_SetGpuSimulation(m_pRen, f_ParticleGpuThreshold());   // big emitters on the GPU (setting)
    m_oPreviewFx.f_Play(l_pE, l_pE->m_uSeed ? l_pE->m_uSeed : 1u);
    m_fPreviewFxIdle = 0;
    std::fprintf(stderr, "[fx] preview %s#%s (%zu emitter(s), %d on the GPU above %d particles, seed %u)\n", l_strFile.c_str(),
                 l_strEffect.c_str(), l_pE->m_vEmitters.size(), m_oPreviewFx.f_GpuEmitters(), f_ParticleGpuThreshold(), m_oPreviewFx.f_Seed());
    return true;
}

// The game's loaded sprite atlas, as the cache's kSpriteAtlas: clips and effects that name ".../atlas/game.atlas" (or
// no atlas) draw from it with the renderer's sprite texture, so they follow the chosen art style. Called whenever it
// is (re)loaded; the cached object stays the same and its version goes up.
void Game::f_ProvideSpriteAtlas() {
    if (!m_upRes) return;
    m_upRes->f_Provide<toms::res::AtlasRes>(toms::res::ResourceManager::m_pszSpriteAtlas,
                                       [&](toms::res::AtlasRes& l_oA) { l_oA.f_SetProvided(m_oSpriteAtlas, m_oSpriteMeshes); });
}

// ---- monster idle animations ----
// assets/media/anim/monster_idle.anim holds one looping clip per enemy, "idle_<enemy id>", made in anim_editor. A clip
// is authored around the monster's sprite with its pivot at the bottom centre (0, 0); here it is placed on the bottom
// centre of the monster's rect and scaled so the sprite fills that rect exactly as the static sprite did (a 2x1 golem
// stretches the clip the same way). A missing file or clip, or the runtime sprite grid (no prebuilt atlas), keeps the
// static sprite.
// ---- objects' looks (the stage editor's "objects" and the types' "look"; docs/gameplay/02_STAGE_EDITOR.md) ----
nlohmann::json Game::f_TypeLook(const std::string& l_strKind, const std::string& l_strId) const {
    const std::string l_strK = l_strKind.substr(0, l_strKind.find(':'));
    const nlohmann::json* l_pSrc = nullptr;
    if (l_strK == "monster") { auto l_itIt = m_mapEnemyTpl.find(l_strId); if (l_itIt != m_mapEnemyTpl.end()) l_pSrc = &l_itIt->second; }
    else if (l_strK == "item") l_pSrc = f_ItemDef(l_strId);
    else if (l_strK == "npc") {
        auto l_itIt = m_mapNpcDefs.find(l_strId);
        if (l_itIt != m_mapNpcDefs.end()) { nlohmann::json l_jL = l_itIt->second.m_jLook.is_object() ? l_itIt->second.m_jLook : nlohmann::json::object();
            if (!l_jL.contains("sprite") && !l_itIt->second.m_strSprite.empty()) l_jL["sprite"] = l_itIt->second.m_strSprite;
            return l_jL; }
    }
    // a monster's / item's "sprite" (enemies.json / items.json, "slime.png"), then its "look" (anim, tint, ...)
    nlohmann::json l_jL = nlohmann::json::object();
    if (l_pSrc && l_pSrc->contains("sprite") && (*l_pSrc)["sprite"].is_string()) l_jL["sprite"] = (*l_pSrc)["sprite"];
    if (l_pSrc && l_pSrc->contains("look") && (*l_pSrc)["look"].is_object()) for (auto& [l_strK, l_jV] : (*l_pSrc)["look"].items()) l_jL[l_strK] = l_jV;
    return l_jL;
}

nlohmann::json Game::f_LookOf(const Entity& l_oE) const {
    nlohmann::json l_jL = f_TypeLook(l_oE.m_strKind, l_oE.m_strId);
    if (l_oE.m_jLook.is_object()) for (auto& [l_strK, l_jV] : l_oE.m_jLook.items()) l_jL[l_strK] = l_jV;   // field by field: the object's own win
    return l_jL;
}

std::string Game::f_TypeSprite(const std::string& l_strKind, const std::string& l_strId) const {
    const nlohmann::json l_jL = f_TypeLook(l_strKind, l_strId);
    if (l_jL.contains("sprite") && l_jL["sprite"].is_string()) {
        std::string l_strS = l_jL["sprite"].get<std::string>();
        if (l_strS.size() > 4 && l_strS.compare(l_strS.size() - 4, 4, ".png") == 0) l_strS.resize(l_strS.size() - 4);
        if (m_oSpriteAtlas.f_Find(l_strS)) return l_strS;
    }
    return l_strKind.rfind("npc", 0) == 0 ? f_NpcSprite(l_strId) : f_EntSprite(l_strId);
}

bool Game::f_DrawClipInRect(const toms::anim::Clip& l_oClip, float l_fT, const toms::anim::AtlasSet& l_oSet, float l_fX, float l_fY, float l_fW, float l_fH,
                          float l_fSpriteW, float l_fSpriteH) {
    static const float l_arrWhite[4] = {1, 1, 1, 1};
    return f_DrawClipInRect(l_oClip, l_fT, l_oSet, l_fX, l_fY, l_fW, l_fH, l_fSpriteW, l_fSpriteH, l_arrWhite);
}

bool Game::f_DrawClipInRect(const toms::anim::Clip& l_oClip, float l_fT, const toms::anim::AtlasSet& l_oSet, float l_fX, float l_fY, float l_fW, float l_fH,
                          float l_fSpriteW, float l_fSpriteH, const float l_arrTint[4]) {
    toms::anim::f_Evaluate(l_oClip, l_fT, m_vIdlePoses);
    const glm::mat3 l_mat3Place =
        toms::anim::f_Flatten(toms::anim::f_Compose({l_fX + l_fW * 0.5f, l_fY + l_fH, 0}, glm::vec3(0), {l_fW / l_fSpriteW, l_fH / l_fSpriteH, 1}));
    m_oIdleDraws.f_Clear();
    const bool l_bTinted = l_arrTint && (l_arrTint[0] != 1 || l_arrTint[1] != 1 || l_arrTint[2] != 1 || l_arrTint[3] != 1);
    toms::anim::f_AppendDraws(l_oClip, m_vIdlePoses, l_oSet, l_mat3Place, l_bTinted ? l_arrTint : nullptr, m_oIdleDraws);
    if (m_oIdleDraws.f_Empty()) return false;
    m_oIdleDraws.f_Submit(m_pRen);
    return true;
}

bool Game::f_PlayPreviewMesh(const std::string& l_strSprite, std::string& l_strError) {
    const toms::AtlasRegion* l_pR = m_oSpriteAtlas.f_Find(l_strSprite);
    if (!l_pR) { l_strError = "no sprite '" + l_strSprite + "' in the game atlas"; return false; }
    if (l_pR->m_bRotated) { l_strError = "sprite '" + l_strSprite + "' is rotated in the atlas"; return false; }
    m_strPreviewMeshSprite = l_strSprite;
    m_dPreviewMeshMs = 0;
    std::fprintf(stderr, "[mesh] preview %s (%dx%d)\n", l_strSprite.c_str(), l_pR->m_iW, l_pR->m_iH);
    return true;
}

// --mesh-test: the sprite as a grid of triangles, each row swaying by a sine wave that grows towards the top (the
// bottom row stays put), 4x its size left of the centre; the plain sprite at the same size right of it. Both sit
// below the centre, clear of the floor-name banner.
void Game::f_DrawPreviewMesh() {
    const toms::AtlasRegion* l_pR = m_strPreviewMeshSprite.empty() ? nullptr : m_oSpriteAtlas.f_Find(m_strPreviewMeshSprite);
    if (!l_pR || l_pR->m_bRotated) return;
    constexpr int l_iCells = 8;
    // The sprite's whole original frame is laid out (4x); a trimmed region's pixels sit at their offset inside it.
    const float l_fFw = l_pR->m_iOrigW * 4.0f, l_fFh = l_pR->m_iOrigH * 4.0f;
    const float l_fW = l_pR->m_iW * 4.0f, l_fH = l_pR->m_iH * 4.0f, l_fOx = l_pR->m_iOffX * 4.0f, l_fOy = l_pR->m_iOffY * 4.0f;
    const float l_fX0 = m_pRen->f_Width() / 2.0f - l_fFw - 16.0f, l_fY0 = m_pRen->f_Height() * 0.65f - l_fFh / 2.0f;
    const float l_fT = (float)(m_dPreviewMeshMs / 1000.0);
    m_vPreviewMeshVerts.clear();
    for (int l_iJ = 0; l_iJ <= l_iCells; l_iJ++)
        for (int l_iI = 0; l_iI <= l_iCells; l_iI++) {
            const float l_fFx = (float)l_iI / l_iCells, l_fFy = (float)l_iJ / l_iCells;
            const float l_fDown = (l_fOy + l_fFy * l_fH) / l_fFh;   // 0 at the frame's top, 1 at its bottom: the feet stay put
            const float l_fSway = std::sin(l_fT * 4.0f + l_fDown * 3.0f) * l_fFw * 0.12f * (1.0f - l_fDown);
            m_vPreviewMeshVerts.push_back({l_fX0 + l_fOx + l_fFx * l_fW + l_fSway, l_fY0 + l_fOy + l_fFy * l_fH,
                                         l_pR->m_arrUv[0] + l_fFx * (l_pR->m_arrUv[2] - l_pR->m_arrUv[0]), l_pR->m_arrUv[1] + l_fFy * (l_pR->m_arrUv[3] - l_pR->m_arrUv[1])});
        }
    m_vPreviewMeshIdx.clear();
    for (int l_iJ = 0; l_iJ < l_iCells; l_iJ++)
        for (int l_iI = 0; l_iI < l_iCells; l_iI++) {
            const uint16_t l_u16A = (uint16_t)(l_iJ * (l_iCells + 1) + l_iI), l_u16B = (uint16_t)(l_u16A + 1);
            const uint16_t l_u16C = (uint16_t)(l_u16A + l_iCells + 1), l_u16D = (uint16_t)(l_u16C + 1);
            m_vPreviewMeshIdx.insert(m_vPreviewMeshIdx.end(), {l_u16A, l_u16B, l_u16D, l_u16A, l_u16D, l_u16C});
        }
    Mesh l_oM;
    l_oM.m_pVertices = m_vPreviewMeshVerts.data();
    l_oM.m_uVertexCount = (uint32_t)m_vPreviewMeshVerts.size();
    l_oM.m_pIndices = m_vPreviewMeshIdx.data();
    l_oM.m_uIndexCount = (uint32_t)m_vPreviewMeshIdx.size();
    m_pRen->f_DrawMesh(l_oM);
    Quad l_oQ;
    l_oQ.m_arrRect[0] = m_pRen->f_Width() / 2.0f + 16.0f + l_fOx; l_oQ.m_arrRect[1] = l_fY0 + l_fOy; l_oQ.m_arrRect[2] = l_fW; l_oQ.m_arrRect[3] = l_fH;
    std::copy(l_pR->m_arrUv, l_pR->m_arrUv + 4, l_oQ.m_arrUv);
    for (float& l_fC : l_oQ.m_arrTint) l_fC = 1.0f;
    m_pRen->f_DrawSprite(l_oQ);
}

// The preview clip and/or effect, centred on the screen, over the world (before ren->end()).
void Game::f_DrawPreviewAnim() {
    f_DrawPreviewMesh();
    if (m_oPreviewFx.f_Effect() && m_spPreviewFxFile) {
        const size_t l_nKnown = m_vPreviewMissing.size();
        m_oPreviewFx.f_SetTransform(toms::anim::f_Placement(m_pRen->f_Width() / 2.0f, m_pRen->f_Height() / 2.0f));
        m_oPreviewFx.f_Draw(m_pRen, m_spPreviewFxFile->f_Atlases(), glm::mat3(1.0f), nullptr, &m_vPreviewMissing);   // CPU quads + GPU emitters
        for (size_t l_nI = l_nKnown; l_nI < m_vPreviewMissing.size(); l_nI++)
            std::fprintf(stderr, "[fx] sprite '%s' is not in the atlas\n", m_vPreviewMissing[l_nI].c_str());
    }
    if (m_spPreviewAnim && (m_oPreviewRig.f_Character() || m_oPreviewRig.f_Held())) {   // --held: the character and what it holds
        static const toms::anim::AtlasSet l_oNone;
        m_oPreviewRig.f_Draw(m_pRen, m_spPreviewAnim->f_Atlases(), m_spPreviewHeld && m_spPreviewHeld->f_Ok() ? m_spPreviewHeld->f_Atlases() : l_oNone,
                         toms::anim::f_Placement(m_pRen->f_Width() / 2.0f, m_pRen->f_Height() / 2.0f));
        return;
    }
    if (!m_vPreviewCrowd.empty() && m_spPreviewAnim) {   // --anim-crowd: every one as the game draws a clip
        const auto l_oT0 = std::chrono::steady_clock::now();
        const int l_iN = int(m_vPreviewCrowd.size()), l_iCols = std::max(1, int(std::ceil(std::sqrt(double(l_iN)))));
        const int l_iRows = (l_iN + l_iCols - 1) / l_iCols;
        const float l_fCw = float(m_pRen->f_Width()) / float(l_iCols), l_fCh = float(m_pRen->f_Height()) / float(l_iRows);
        const float l_fS = std::min(l_fCw / 420.0f, l_fCh / 640.0f);
        for (int l_iI = 0; l_iI < l_iN; l_iI++)
            m_vPreviewCrowd[size_t(l_iI)].f_Draw(m_pRen, m_spPreviewAnim->f_Atlases(),
                                          toms::anim::f_Placement(l_fCw * (float(l_iI % l_iCols) + 0.5f), l_fCh * float(l_iI / l_iCols + 1) - 8.0f, l_fS));
        m_dPreviewDrawMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_oT0).count();
        return;
    }
    if (!m_oPreviewPlayer.f_Showing() || !m_spPreviewAnim) return;
    const size_t l_nKnown = m_vPreviewMissing.size();
    toms::anim::f_Evaluate(*m_oPreviewPlayer.f_Clip(), m_oPreviewPlayer.f_PoseTime(), m_vPreviewPoses);
    m_oPreviewDraws.f_Clear();
    toms::anim::f_AppendDraws(*m_oPreviewPlayer.f_Clip(), m_vPreviewPoses, m_spPreviewAnim->f_Atlases(),
                            toms::anim::f_Placement(m_pRen->f_Width() / 2.0f, m_pRen->f_Height() / 2.0f), nullptr, m_oPreviewDraws, &m_vPreviewMissing);
    m_oPreviewDraws.f_Submit(m_pRen);
    for (size_t l_nI = l_nKnown; l_nI < m_vPreviewMissing.size(); l_nI++)
        std::fprintf(stderr, "[anim] sprite '%s' is not in the atlas\n", m_vPreviewMissing[l_nI].c_str());
}

// ---------------------------------------------------------------------------------------------
// ImGui developer tools -- compiled on BOTH desktop and web: the font-scale setter, the F1 debug
// overlay and the F2 styling spike. The ImGui backend is bgfx (src/engine/src/imgui_bgfx.*),
// driven by GameSession on both platforms. Nothing here touches gameplay
// state a player sees unless they press F1/F2, so the browser build behaves exactly as before.
void Game::f_ApplyUiSettings() {
    ImGui::GetIO().FontGlobalScale = 1.5f;   // ImGui's default font reads small at this resolution
}

void Game::f_DrawDebugOverlay() {
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("TOMS Debug (F1 to toggle)");
    ImGui::Text("State: %s", toms::f_ToString(f_CurrentState()));
    ImGui::Text("Stage: %s", m_strCurStage.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Player stats (hand-trace bestiary fights live)");
    ImGui::SliderInt("HP", &m_oPl.m_iHp, 0, m_oPl.m_iMaxhp > 0 ? m_oPl.m_iMaxhp : 999);
    ImGui::SliderInt("Max HP", &m_oPl.m_iMaxhp, 1, 999);
    ImGui::SliderInt("ATK", &m_oPl.m_iAtk, 0, 99);
    ImGui::SliderInt("DEF", &m_oPl.m_iDef, 0, 99);
    ImGui::SliderInt("Level", &m_oPl.m_iLv, 1, 20);
    ImGui::SliderInt("EXP", &m_oPl.m_iExp, 0, 999);
    ImGui::SliderInt("Gold", &m_oPl.m_iGold, 0, 999);
    ImGui::Separator();
    if (m_pRen) {
        static const char* l_arrNodeNames[] = { "All", "Stage", "Char", "Talk", "Battle", "Store" };
        static int l_iNodeSel = 0;
        if (ImGui::Combo("Node filter (diagnostic)", &l_iNodeSel, l_arrNodeNames, IM_ARRAYSIZE(l_arrNodeNames)))
            m_pRen->f_SetNodeFilter((uint8_t)l_iNodeSel);
    }
    ImGui::Separator();
    ImGui::Separator();
    ImGui::TextWrapped("Last combat log: %s", m_oCs.m_strLog.empty() ? "(none)" : m_oCs.m_strLog.c_str());
    ImGui::Text("Inventory: %d item(s)", (int)m_oPl.m_vInv.size());
    ImGui::End();
}

// M2 styling spike: an undecorated, transparent-background ImGui window positioned at the exact
// same rect as a scene-graph-drawn backdrop (drawStylingSpikeBackdrop(), called from draw()).
// If this reads as one seamless panel on screen rather than two overlapping things, it confirms
// the hybrid rendering approach Milestone 5 is about to commit real screens to.
void Game::f_DrawStylingSpike() {
    if (!m_pRen) return;
    float l_fScreenW = (float)m_pRen->f_Width(), l_fScreenH = (float)m_pRen->f_Height();
    // Arbitrary "a panel-sized rect" for this spike -- unlike the pause menu's sub-page sizes
    // (see computeSubPageLayout() in game_helpers.h), nothing else in the codebase shares this
    // size, so it is named here rather than promoted to a shared constant.
    constexpr float l_fSpikeW = 420.0f, l_fSpikeH = 260.0f;
    float l_fW = l_fSpikeW, l_fH = l_fSpikeH;
    float l_fX = (l_fScreenW - l_fW) * 0.5f, l_fY = (l_fScreenH - l_fH) * 0.5f;
    m_arrStylingSpikeRect[0] = l_fX; m_arrStylingSpikeRect[1] = l_fY; m_arrStylingSpikeRect[2] = l_fW; m_arrStylingSpikeRect[3] = l_fH;
    m_bStylingSpikeVisible = true;

    ImGui::SetNextWindowPos(ImVec2(l_fX, l_fY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(l_fW, l_fH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags l_iFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("StylingSpike", nullptr, l_iFlags);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.7f, 1.0f), "M2 Styling Spike (F2 to toggle)");
    ImGui::TextWrapped("This text and button are drawn by ImGui with NoBackground. The panel "
                        "behind them is drawn by the scene-graph/renderer as a solid-tint quad "
                        "(standing in for real 9-slice art) at the exact same rect.");
    static int l_iClicks = 0;
    if (ImGui::Button("Click me")) l_iClicks++;
    ImGui::SameLine();
    ImGui::Text("clicks: %d", l_iClicks);
    ImGui::End();
}

void Game::f_DrawStylingSpikeBackdrop() {
    if (!m_bStylingSpikeVisible || !m_pRen) return;
    float l_fX = m_arrStylingSpikeRect[0], l_fY = m_arrStylingSpikeRect[1], l_fW = m_arrStylingSpikeRect[2], l_fH = m_arrStylingSpikeRect[3];
    // Outer rect (darker "border") + inset inner rect (lighter "fill") -- a cheap two-rect
    // stand-in for a real 9-slice panel; the point of this spike is the ImGui/scene-graph
    // alignment technique, not authoring actual 9-slice art.
    Quad l_oOuter; l_oOuter.m_arrRect[0]=l_fX; l_oOuter.m_arrRect[1]=l_fY; l_oOuter.m_arrRect[2]=l_fW; l_oOuter.m_arrRect[3]=l_fH;
    l_oOuter.m_arrUv[0]=0;l_oOuter.m_arrUv[1]=0;l_oOuter.m_arrUv[2]=1;l_oOuter.m_arrUv[3]=1; l_oOuter.m_bSolid=true;
    l_oOuter.m_arrTint[0]=0.15f; l_oOuter.m_arrTint[1]=0.13f; l_oOuter.m_arrTint[2]=0.22f; l_oOuter.m_arrTint[3]=0.96f;
    m_pRen->f_DrawSprite(l_oOuter);
    float l_fInset = 6.0f;
    Quad l_oInner; l_oInner.m_arrRect[0]=l_fX+l_fInset; l_oInner.m_arrRect[1]=l_fY+l_fInset; l_oInner.m_arrRect[2]=l_fW-2*l_fInset; l_oInner.m_arrRect[3]=l_fH-2*l_fInset;
    l_oInner.m_arrUv[0]=0;l_oInner.m_arrUv[1]=0;l_oInner.m_arrUv[2]=1;l_oInner.m_arrUv[3]=1; l_oInner.m_bSolid=true;
    l_oInner.m_arrTint[0]=0.10f; l_oInner.m_arrTint[1]=0.09f; l_oInner.m_arrTint[2]=0.16f; l_oInner.m_arrTint[3]=0.96f;
    m_pRen->f_DrawSprite(l_oInner);
}
