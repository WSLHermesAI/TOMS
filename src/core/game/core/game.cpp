// game.cpp — implementation of Game.
#include "game.h"
#include "node.h"   // 2D scene-graph Node (parent/child + local/world transform)
#include "scene.h"  // render binding: GameObject / SpriteNode / TextNode / FullScreenSplash

// Shared file-local helpers (C4 / trParam / readJsonFile / cellSprite / entSprite) now live in
// game_helpers.h as inline definitions, since the 2026-09-13 refactor split
// the rest of this file into game_*.cpp units that need the same helpers.
#include "game_helpers.h"
#include "game_condition.h"   // GameConditionContext (condition.h adapter)

using namespace toms::game_detail;

// The scene graph's TextNode has no text renderer in this game (all text is RmlUi).
namespace toms { TextNode::DrawFn TextNode::m_fnDraw = nullptr; }
#include "renderer.h"   // Renderer = the bgfx renderer (src/game/compat/renderer.h), desktop and web
#include "event_bus.h"   // toms::EventBus — see Game::resolveCombatRound / Game::movePlayer
#include "condition.h"   // toms::evaluate / toms::ConditionContext — see GameConditionContext below
#include "story_controller.h" // toms::advanceStoryBeat / setStoryFlag / hasStoryFlag
#include "encounter.h"   // toms::resolveEncounterKind / toms::EncounterKind — see Game::movePlayer
#ifndef __EMSCRIPTEN__
#include "imgui.h"       // core ImGui API only -- the bgfx backend is src/engine/src/imgui_bgfx.*
#endif

#include <json.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <ctime>


Quad Game::f_SpriteQuad(float l_fX, float l_fY, float l_fW, float l_fH, int l_iLayer, const float l_arrTint[4]) {
    Quad l_oQ; l_oQ.m_arrRect[0]=l_fX; l_oQ.m_arrRect[1]=l_fY; l_oQ.m_arrRect[2]=l_fW; l_oQ.m_arrRect[3]=l_fH;
    f_SpriteUV(l_iLayer, l_oQ.m_arrUv);
    // A trimmed sprite in the prebuilt atlas covers only part of its tile: shrink the quad to match.
    if (l_iLayer >= 0 && l_iLayer < (int)m_vSpriteTrim.size()) {
        const std::array<float, 4>& l_arrT = m_vSpriteTrim[l_iLayer];
        l_oQ.m_arrRect[0] = l_fX + l_arrT[0] * l_fW; l_oQ.m_arrRect[1] = l_fY + l_arrT[1] * l_fH; l_oQ.m_arrRect[2] = l_arrT[2] * l_fW; l_oQ.m_arrRect[3] = l_arrT[3] * l_fH;
    }
    l_oQ.m_arrTint[0]=l_arrTint[0]; l_oQ.m_arrTint[1]=l_arrTint[1]; l_oQ.m_arrTint[2]=l_arrTint[2]; l_oQ.m_arrTint[3]=l_arrTint[3];
    return l_oQ;
}


// ---------- maze camera ----------
// The camera's own math lives in src/game/camera.cpp (toms::Camera). What remains here is glue:
// hand it the pixel size of the drawing area, the grid size and the focus tile, then ease it.

// Adapter over toms::Camera::viewportTiles() so nothing else has to know the renderer exists.
void Game::f_CameraViewportTiles(float& l_fTs, int& l_iCols, int& l_iRows) const {
    const float l_fW = m_pRen ? (float)m_pRen->f_Width()  : 1024.0f;
    const float l_fH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
    m_oCam.f_ViewportTiles(l_fW, l_fH, l_fTs, l_iCols, l_iRows);
}

void Game::f_SetCameraModeIndex(int l_iM) {
    m_oCam.f_SetMode(toms::Camera::f_ModeFromIndex(l_iM));
    m_oSettings.m_iCameraMode = m_oCam.f_ModeIndex();
    toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
    toms::f_SaveGameSettings(toms::f_SettingsPath(), m_oSettings);
    // Re-target immediately so the effect is visible right away instead of waiting for the
    // player to take a step; ease into it rather than snapping (consistent with "Rooms" mode's
    // own slide, and it's a nicer transition than a hard cut for "Follow" too).
    m_oCam.f_Retarget(m_oPl.m_iX, m_oPl.m_iY, m_oSt.m_iWidth, m_oSt.m_iHeight,
                  m_pRen ? (float)m_pRen->f_Width() : 1024.0f, m_pRen ? (float)m_pRen->f_Height() : 768.0f);
}


void Game::f_CanvasChanged() {
    m_oCam.f_Retarget(m_oPl.m_iX, m_oPl.m_iY, m_oSt.m_iWidth, m_oSt.m_iHeight, m_pRen ? (float)m_pRen->f_Width() : 1024.0f, m_pRen ? (float)m_pRen->f_Height() : 768.0f);
    m_oCam.f_Snap();
}

std::vector<int> Game::f_StoreTabIndices() const {
    // The tab is the item's type (data/items.json): 0 consumables, 1 weapons, 2 armor, 3 talents.
    std::vector<int> l_vOut;
    for (int l_iI = 0; l_iI < (int)m_vStoreItems.size(); l_iI++) {
        const std::string l_strType = f_ItemType(m_vStoreItems[l_iI].m_strId);
        const int l_iTab = l_strType == "weapon" ? 1 : l_strType == "armor" ? 2 : l_strType == "talent" ? 3 : 0;
        if (l_iTab == m_iStoreTab) l_vOut.push_back(l_iI);
    }
    return l_vOut;
}


// Milestone 9: opens the Yes/No confirm instead of transitioning immediately -- see
// stairsConfirmOpen_'s declaration in game.h for why.
void Game::f_RequestStageTransition(const std::string& l_strTarget, bool l_bIsUp) {
    m_strStairsConfirmGoEvent.clear();
    m_bStairsConfirmOpen = true;
    m_bStairsConfirmIsUp = l_bIsUp;
    m_strStairsConfirmTarget = l_strTarget;
    m_oAudio.f_Play("confirm_click");
}

void Game::f_ConfirmStageTransition() {
    if (!m_bStairsConfirmOpen) return;
    std::string l_strTarget = m_strStairsConfirmTarget;
    bool l_bIsUp = m_bStairsConfirmIsUp;
    m_bStairsConfirmOpen = false;
    m_strStairsConfirmTarget.clear();
    // Q7: taking stairs a finished event opened finishes their "go to the stairs" event.
    const std::string l_strGo = m_strStairsConfirmGoEvent;
    m_strStairsConfirmGoEvent.clear();
    if (!l_strGo.empty() && m_oRun.f_EventStarted(l_strGo) && !m_oRun.f_EventFinished(l_strGo)) f_FinishEvent(l_strGo);
    // M9 (stair alignment): climbing up lands on the new floor's OWN stairs_down (the matching
    // physical stairwell coming from below); going down lands on its stairs_up (arriving from
    // above) -- see docs/story/STAIR_ALIGNMENT.md.
    f_LoadStage(l_strTarget, l_bIsUp ? StageArrival::FromBelow : StageArrival::FromAbove);
    // A floor change is the run's natural checkpoint: write/refresh the slot immediately
    // (rather than waiting for the throttled autosave) so the Continue list is always current.
    f_SaveCurrentRun();
}

void Game::f_CancelStageTransition() {
    m_bStairsConfirmOpen = false;
    m_strStairsConfirmTarget.clear();
    m_oAudio.f_Play("close_ui");
}




#ifndef __EMSCRIPTEN__


#endif


void Game::f_NewGame(int l_iSlot) {
    // A new game is a genuinely fresh run: default stats and wiped story/entity/meta progress.
    m_oPl = Player();
    m_oPl.m_iMaxhp = m_iStartingHp; m_oPl.m_iHp = m_iStartingHp; m_oPl.m_iAtk = m_iStartingAtk; m_oPl.m_iDef = m_iStartingDef; m_oPl.m_iGold = 0; m_oPl.m_iExp = 0; m_oPl.m_iLv = 1;
    m_oPl.m_vInv = {"potion_red", "potion_blue", "exp_up"};
    f_ResetAttrs();
    m_oEquipped = toms::EquippedSet{};
    m_vGearOwned.clear();
    m_mapWeaponUses.clear();               // every weapon back to Lv 1
    f_OwnGear(m_pszStartingWeapon, true);   // the bow, worn from the start (not "new" in the gear menu)
    m_bNewsStore = m_bNewsGear = m_bNewsEvents = false;
    m_nEventLogSeen = 0;
    m_oPl.m_iX = 1; m_oPl.m_iY = 1;
    m_mapEntityStatus.clear();
    m_oRun.f_Reset();   // S3: a new game is a fresh run -- choices/counters/side stories/floors/deaths
    m_mapMissionTrackers.clear();
    m_oMeta = toms::MetaSaveData{};
    m_vNotifications.clear();
    m_oCs = CombatState{};
    m_bInDialogue = false; m_vDlgChoices.clear(); m_strDlgNode = "root";
    if (m_bInGameMenuOpen) f_ClosePlayerMenu();
    m_bStoreOpen = false; m_bStoreUnlockDlg = false; m_bStoreFromMenu = false;
    m_bStageSelectOpen = false; m_bStairsConfirmOpen = false;
    m_iPlayTimeSec = 0;
    m_bSlotReadOnly = false;
    if (l_iSlot < 0) {
        // A test scenario (docs/gameplay/08_TEST_SCENARIOS.md): no slot, and nothing is ever written.
        m_iActiveSlot = 0;
        m_bSlotReadOnly = true;
    } else if (l_iSlot > 0) {
        // Explicit slot (the Continue page's "start a new game in this empty slot?" answer).
        m_iActiveSlot = l_iSlot;
    } else {
        // Prefer a free slot; when every slot is taken, reuse slot 1 rather than refusing to
        // start (the Continue page still lists all of them, so nothing is silently destroyed).
        int l_iFree = toms::f_FirstEmptySlot(toms::f_DefaultSaveDir(), m_oTitle.f_SlotCount());
        m_iActiveSlot = (l_iFree > 0) ? l_iFree : 1;
    }
    // S3.5: with the tower data present the run starts on its first floor (F01), so progression,
    // the counter and the save all speak floor ids; otherwise the historical starting stage.
    f_LoadStage(f_FloorMode() ? m_oFloors.f_At(1).m_strId : std::string("stage01"));
    if (l_iSlot >= 0) f_SaveCurrentRun();
    m_oTitle.f_Close();
    m_oTitle.f_SetRunInProgress(true);
}


void Game::f_Update(int l_iDtMs) {
    m_dIdleClockMs += l_iDtMs;   // the looping clips on the map (each object at its own phase: map_objects.cpp)
    // Hot reload (setHotReload: the editors, debug builds): files changed on disk are read again, in place. The clips
    // and effects that were playing from a reloaded file start over from its new contents.
    if (m_bHotReload && m_upRes && (m_iHotReloadMs += l_iDtMs) >= 1000) {
        m_iHotReloadMs = 0;
        m_upRes->f_CheckFiles();
    }
    if (m_spPreviewAnim && m_spPreviewAnim->f_Version() != m_iPreviewAnimVer) {
        m_iPreviewAnimVer = m_spPreviewAnim->f_Version();
        f_StartPreviewClip();
    }
    if (m_spPreviewFxFile && m_spPreviewFxFile->f_Version() != m_iPreviewFxVer) {
        m_iPreviewFxVer = m_spPreviewFxFile->f_Version();
        if (const toms::fx::Effect* l_pE = m_spPreviewFxFile->f_Find(m_strPreviewFxName)) m_oPreviewFx.f_Play(l_pE, l_pE->m_uSeed ? l_pE->m_uSeed : 1u);
        else m_oPreviewFx.f_Stop(true);
    }
    m_oFloor.f_Update(l_iDtMs);   // the floor's objects: a door's opening clip plays out, ...
    if (!m_vResourceMessages.empty() && !m_oTitle.f_IsOpen()) {   // missing files, said once the map shows (resourceMissing)
        for (const std::string& l_strM : m_vResourceMessages) m_vNotifications.push_back({l_strM, 6000});
        m_vResourceMessages.clear();
    }
    if (!m_strPreviewMeshSprite.empty()) m_dPreviewMeshMs += l_iDtMs;   // --mesh-test: the wave's clock
    const int l_iBattleDt = f_BattleFrame(l_iDtMs);   // the battle's time: 0 in a hit stop, slower in slow motion (docs/gameplay/15)
    f_UpdateBattleRig(l_iBattleDt);   // the bow and the player's character on the battle screen (battleRigState)
    f_UpdateMonsterRig(l_iBattleDt);  // the monster: idle, attack, hurt, die (monster_battle.anim)
    f_UpdateBattleFx(l_iDtMs);    // the weapon's particles (battle.particle)
    if (m_oPreviewRig.f_Character() || m_oPreviewRig.f_Held()) {   // --anim with --held: both clips, one clock
        m_oPreviewRig.f_Update(l_iDtMs);
        for (const std::string& l_strE : m_oPreviewRig.f_TakeEvents()) std::fprintf(stderr, "[anim] event %s\n", l_strE.c_str());
    }
    for (toms::anim::AnimPlayer& l_oP : m_vPreviewCrowd) { l_oP.f_Update(l_iDtMs); l_oP.f_TakeEvents(); }   // --anim-crowd
    if (m_oPreviewPlayer.f_Clip()) {   // --anim preview: advances with the game clock, logs its events
        m_oPreviewPlayer.f_Update(l_iDtMs);
        for (const std::string& l_strE : m_oPreviewPlayer.f_TakeEvents()) std::fprintf(stderr, "[anim] event %s\n", l_strE.c_str());
    }
    if (m_oPreviewFx.f_Effect()) {     // --fx preview: a one-shot effect plays again after a short pause
        m_oPreviewFx.f_Update(l_iDtMs / 1000.0f);
        if (m_oPreviewFx.f_Finished() && (m_fPreviewFxIdle += l_iDtMs / 1000.0f) >= 0.5f) {
            m_oPreviewFx.f_Play(m_oPreviewFx.f_Effect(), m_oPreviewFx.f_Seed());
            m_fPreviewFxIdle = 0;
        }
    }
    // S3.5 (c): the act card fades out on its own; everything it shows is also on screen elsewhere
    // (the HUD carries the floor), so a player who ignores it loses nothing.
    if (m_fChapterCardMs > 0.0f) m_fChapterCardMs = std::max(0.0f, m_fChapterCardMs - (float)l_iDtMs);
    // The player menu's Events tab: an entry that appears or finishes lights a dot on ≡ (and on the
    // tab) and says so once. A load or a new game only takes the count (eventLogSeen_ = 0 there).
    if (!m_oTitle.f_IsOpen()) {
        size_t l_nSig = 0;
        for (const LogEntry& l_oE : f_EventLog(true)) l_nSig += l_oE.m_bDone ? 1000 : 1;
        if (m_nEventLogSeen == 0) m_nEventLogSeen = l_nSig + 1;
        else if (l_nSig + 1 != m_nEventLogSeen) {
            m_nEventLogSeen = l_nSig + 1;
            if (!(m_bInGameMenuOpen && m_eMenuTab == MenuTab::Events)) m_bNewsEvents = true;
            const std::string l_strMsg = m_oLocale.f_Tr("events.updated");   // once, however many entries changed together
            if (std::none_of(m_vNotifications.begin(), m_vNotifications.end(), [&](const auto& l_oN) { return l_oN.first == l_strMsg; }))
                f_PushNotification(l_strMsg);
        }
    }
    // Title phase: the run's clock only advances while actually playing, and a changed run is
    // flushed to its slot on a throttle (see kAutosaveIntervalMs) rather than on every event --
    // one atomic write per few seconds instead of one per pickup.
    if (m_oTitle.f_IsOpen()) {
        if (m_bSaveDirty && !m_oTitle.f_RunInProgress()) m_bSaveDirty = false;   // nothing to save yet
    } else {
        m_iPlayTimeSec += l_iDtMs / 1000;
        if (m_bSaveDirty) {
            m_iSaveFlushMs += l_iDtMs;
            if (m_iSaveFlushMs >= m_iAutosaveIntervalMs) { m_iSaveFlushMs = 0; f_SaveCurrentRun(); }
        } else {
            m_iSaveFlushMs = 0;
        }
    }
    // Battle System v2: both bars auto-move continuously and independently, and the enemy fires
    // on its own fixed real-time clock -- none of this waits on player input, so it all just ticks
    // every frame combat is active. See CombatState's comment in game.h for the full model.
    m_oTimers.f_Update(l_iDtMs);   // delayed calls (the battle's damage numbers); a gone owner's never run
    for (auto& l_oP : m_oCs.m_vPopups) { l_oP.m_iAgeMs += l_iDtMs; l_oP.m_oRise.f_Update(l_iDtMs); }
    m_oCs.m_vPopups.erase(std::remove_if(m_oCs.m_vPopups.begin(), m_oCs.m_vPopups.end(),
                                   [](const CombatState::Popup& l_oP) { return l_oP.m_oRise.f_Done(); }), m_oCs.m_vPopups.end());
    if (m_oMiniGame.m_bOpen && m_oMiniGame.m_iPhase == 1) {   // a reward event's play (docs/gameplay/07 phase 6)
        m_oMiniGame.m_oPlay.f_Update(l_iDtMs);
        if (m_oMiniGame.m_oPlay.f_Done()) f_FinishMiniGame();
    }
  {   // the battle runs on its own time this frame (Game::battleFrame: hit stop, slow motion)
    const int l_iDtMs = m_oCs.m_iFrameDt;
    // An attack is playing (docs/gameplay/07_GAMEPLAY_PLAN.md §4): both sides' clocks wait for it.
    const bool l_bAttackPlaying = m_oCs.m_bActive && m_oCs.m_iAttackPauseMs > 0;
    if (l_bAttackPlaying) m_oCs.m_iAttackPauseMs = std::max(0, m_oCs.m_iAttackPauseMs - l_iDtMs);
    // a running combo's marker; the owner 2026-10-30: it runs through a monster's attack pause too
    // (data/battle.json "combo_runs_through_attacks": false brings back the stop)
    auto l_fnStepCombo = [&] {
        if (m_oCs.m_oTouch.f_Running() && m_oCs.m_oTouch.f_Started()) { f_StepTouchPlay(l_iDtMs); return; }   // the weapon's own play
        m_oCs.m_oCombo.f_Update(l_iDtMs);
        for (int l_iP : m_oCs.m_oCombo.f_TakeEntered()) f_ComboHint(l_iP, /*hit=*/false);   // "tap now"
        if (m_oCs.m_oCombo.f_Done()) f_ResolveCombo();
    };
    if (m_oCs.m_bActive && m_oCs.m_oSkill.m_bOn) {   // a move or a special guard (docs/gameplay/16 §6): the fight waits for its show
        m_oCs.m_iClockMs += l_iDtMs;
        f_UpdateSkill(l_iDtMs);
    } else {
    if (l_bAttackPlaying && m_bComboRunsThroughAttacks && !m_oCs.m_oAtkBar.m_bCooling && f_AttackRunning()) l_fnStepCombo();
    if (m_oCs.m_bActive && l_bAttackPlaying) {   // a run of hits keeps its rhythm through the attack pause (docs/gameplay/16 §4.1)
        bool l_bVolley = false;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_bVolley = l_bVolley || (l_oF.m_iVolleyHit > 0 && !l_oF.f_Down() && l_oF.m_iStaggerMs <= 0);
        if (l_bVolley) m_oCs.m_iClockMs += l_iDtMs;   // (its "!" and the guard's timing run on the battle's time)
        for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size() && m_oCs.m_bActive && l_bVolley; l_nI++) {
            CombatState::Foe& l_oF = m_oCs.m_vFoes[l_nI];
            if (l_oF.m_iVolleyHit <= 0 || l_oF.f_Down() || l_oF.m_iStaggerMs > 0) continue;
            l_oF.m_iVolleyMs -= l_iDtMs;
            if (l_oF.m_iVolleyMs <= 0) f_ResolveEnemyClockFire((int)l_nI);
        }
    }
    if (m_oCs.m_bActive && !l_bAttackPlaying) {
        auto l_fnStepBar = [&](CombatState::AutoBar& l_oBar, const toms::PowerBarParams& l_oZ) {
            if (l_oBar.m_bCooling) {
                l_oBar.m_iCooldownMs -= l_iDtMs;
                if (l_oBar.m_iCooldownMs <= 0) { l_oBar.m_bCooling = false; l_oBar.m_fPos = 0.0f; l_oBar.m_iDir = 1; l_oBar.m_fLegMs = 0.0f; }
                return;
            }
            l_oBar.m_fLegMs += (float)l_iDtMs;
            float l_fT = l_oZ.m_fRampTime > 0.0f ? std::min(1.0f, l_oBar.m_fLegMs / 1000.0f / l_oZ.m_fRampTime) : 1.0f;
            // cubic ease-in, same curve as a manual charge; data/battle.json scales its slow start and its top speed apart
            const float l_fV0 = l_oZ.m_fV0 * m_fBarSlowScale, l_fVmax = std::max(l_fV0, l_oZ.m_fVmax * m_fBarFastScale);
            float l_fSpeed = l_fV0 + (l_fVmax - l_fV0) * (l_fT * l_fT * l_fT);
            float l_fSpan = 2.0f * l_oZ.m_fRedOuter;
            l_oBar.m_fPos += (float)l_oBar.m_iDir * l_fSpeed * (l_iDtMs / 1000.0f);
            if (l_oBar.m_fPos > l_fSpan) { l_oBar.m_fPos = l_fSpan; l_oBar.m_iDir = -1; l_oBar.m_fLegMs = 0.0f; }
            if (l_oBar.m_fPos < 0.0f)  { l_oBar.m_fPos = 0.0f;  l_oBar.m_iDir = 1;  l_oBar.m_fLegMs = 0.0f; }
        };
        if (m_oCs.m_bActive) m_oCs.m_iClockMs += l_iDtMs;   // the battle's own time (its event log)
        // Battle v3 (docs/gameplay/07 phase 1): the attack side is the point play. Cooling after an attack; then charge builds up
        // while it waits; a running combo moves its marker, and the enemy waits for it (the owner's rule, §4).
        if (m_oCs.m_oAtkBar.m_bCooling) {
            m_oCs.m_oAtkBar.m_iCooldownMs -= l_iDtMs;
            if (m_oCs.m_oAtkBar.m_iCooldownMs <= 0) { m_oCs.m_oAtkBar.m_bCooling = false; m_oCs.m_iChargeMs = 0; }
            if (!m_oCs.m_oTouch.f_Running()) m_oCs.m_oTouch.f_Update(l_iDtMs);   // the last play's marks fade
        } else if (f_AttackRunning()) {
            l_fnStepCombo();
        } else {
            const std::string l_strKey = f_WeaponKey();
            m_oCs.m_iChargeMs = std::min(m_oCs.m_iChargeMs + l_iDtMs, std::max(1, f_WeaponPlayOf(l_strKey).m_iChargeMs));
            // the weapon's own play: armed when the attack is ready (the bow waits over the target for its pull and
            // follows the target until then; the sword and the staff start at once, 16 §3)
            const bool l_bCanPay = m_oCs.m_ePace != toms::Pace::Stamina || m_oCs.m_fStamina >= (float)m_oPaceSettings.m_iAttackCost;
            const glm::vec4 l_v4Area = f_TouchArea();
            const bool l_bMoved = std::fabs(l_v4Area.x - m_v4TouchArmedArea.x) + std::fabs(l_v4Area.y - m_v4TouchArmedArea.y) > 4.0f;
            if (m_oCs.m_bActive && !m_oCs.m_bWinPending && l_bCanPay && m_bBattleBoxesKnown && f_BattlePlayKind() != toms::PlayKind::Track &&
                (!m_oCs.m_oTouch.f_Running() || (!m_oCs.m_oTouch.f_Started() && l_bMoved)))
                f_ArmTouchPlay(false, toms::f_ComboSettings(f_WeaponPlayOf(l_strKey), f_WeaponLevel(l_strKey)).m_iPoints);
        }
        l_fnStepBar(m_oCs.m_oDefBar, toms::f_EffectiveDefenseBar(m_oEquipped, m_mapEquipmentDefs));

        // stamina refills while no attack runs (the stamina pace, docs/gameplay/13 §6.3)
        if (m_oCs.m_ePace == toms::Pace::Stamina && !f_AttackRunning())
            m_oCs.m_fStamina = std::min((float)m_oPaceSettings.m_iStaminaMax, m_oCs.m_fStamina + m_oPaceSettings.m_fStaminaRegenPerS * l_iDtMs / 1000.0f);
        // each monster standing keeps its own clock to its next move (docs/gameplay/13 §4.6, §6.1)
        if (m_oCs.m_bActive) f_StepFoes(l_iDtMs);
    }
    }   // (no skill running)
  }
    // The weapon test arena: when a fight is over (a win dismissed, a loss shown), the next one starts.
    if (m_oArena.m_bOn && !m_oCs.m_bActive && !m_oCs.m_bWon && m_oCs.m_iResultPauseMs <= 0 && !m_oTitle.f_IsOpen()) {
        if (m_oArena.m_bTrial) f_EndWeaponTrialFight();   // the start story: back to the weapon master
        else if (m_oArena.m_bTraining && m_oArena.m_iFights >= m_oArena.m_iFightsMax) f_EndTraining();   // the training ground's run is over
        else f_ArenaNextFight();
    }
    // The result-pause countdown must run regardless of cs.active: finishCombatLose() sets
    // active=false in the same call that starts this pause, so gating on cs.active would freeze
    // it forever and leave a stale "you fell" message stuck in cs.log (which -- see showBattle's
    // condition -- would keep the battle overlay stuck open permanently after a loss).
    if (m_oCs.m_iResultPauseMs > 0) {
        m_oCs.m_iResultPauseMs -= l_iDtMs;
        if (m_oCs.m_iResultPauseMs <= 0) {
            m_oCs.m_iResultPauseMs = 0;
            // A win keeps its victory text showing until the player dismisses it (cs.won,
            // cleared elsewhere on tap/interact); a loss clears its message now, once the brief
            // pause has had a chance to show it, so showBattle's condition stops holding the
            // battle overlay open.
            if (!m_oCs.m_bActive && !m_oCs.m_bWon) m_oCs.m_strLog = "";
        }
    }
    // store UI timers (toast / shake) tick down regardless of combat
    if (m_iToastTimer > 0) { m_iToastTimer -= l_iDtMs; if (m_iToastTimer < 0) m_iToastTimer = 0; }
    if (m_iShakeTimer > 0) { m_iShakeTimer -= l_iDtMs; if (m_iShakeTimer < 0) m_iShakeTimer = 0; }
    // Milestone 5: notification toasts tick down and expire.
    for (auto& l_pairN : m_vNotifications) l_pairN.second -= l_iDtMs;
    m_vNotifications.erase(
        std::remove_if(m_vNotifications.begin(), m_vNotifications.end(),
                        [](const std::pair<std::string,int>& l_pairN) { return l_pairN.second <= 0; }),
        m_vNotifications.end());
    // Milestone 9 polish: "keep moving while held" -- the actual repeat timer, driven from
    // here regardless of which input (keyboard or the virtual/touch d-pad) is holding a
    // direction; see setMoveHeldX/Y's declaration in game.h. Each axis repeats independently
    // so a held diagonal (e.g. up+right) keeps moving diagonally. movePlayer() is already a
    // safe no-op while modalActive(), so ticking this even during a dialogue/battle/etc. that
    // started mid-hold is harmless.
    auto l_fnTickMoveAxis = [&](MoveHoldAxis& l_oA, int l_iDx, int l_iDy) {
        if (l_oA.m_iDir == 0) return;
        l_oA.m_iHoldMs += l_iDtMs;
        int l_iThreshold = l_oA.m_bRepeating ? m_iMoveRepeatMs : m_iMoveInitialDelayMs;
        if (l_oA.m_iHoldMs >= l_iThreshold) {
            f_MovePlayer(l_iDx, l_iDy);
            l_oA.m_iHoldMs -= l_iThreshold;
            l_oA.m_bRepeating = true;
        }
    };
    l_fnTickMoveAxis(m_oMoveHoldX, m_oMoveHoldX.m_iDir, 0);
    l_fnTickMoveAxis(m_oMoveHoldY, 0, m_oMoveHoldY.m_iDir);
    // Click-to-move (see walkTo()): one step per kMoveRepeatMs; anything modal ends the walk.
    if (!m_vWalkPath.empty()) {
        if (f_ModalActive()) f_CancelWalk();
        else {
            m_iWalkTimerMs += l_iDtMs;
            if (m_iWalkTimerMs >= m_iMoveRepeatMs) { m_iWalkTimerMs -= m_iMoveRepeatMs; f_WalkStep(); }
        }
    }

    // Maze camera: retarget on the player's tile, then ease toward it (both modes slide --
    // Rooms' target only actually MOVES when the player crosses into a different section, so it
    // reads as "smoothly snap to the new room" for that mode and as continuous following for
    // Follow mode). All of that lives in toms::Camera (src/game/camera.cpp); the easing is
    // frame-rate-independent exponential smoothing, not a fixed-duration tween.
    if (!m_oTitle.f_IsOpen()) {
        m_oCam.f_Retarget(m_oPl.m_iX, m_oPl.m_iY, m_oSt.m_iWidth, m_oSt.m_iHeight,
                      m_pRen ? (float)m_pRen->f_Width() : 1024.0f, m_pRen ? (float)m_pRen->f_Height() : 768.0f);
        m_oCam.f_Update((float)l_iDtMs / 1000.0f);
    }
}

void Game::f_SaveFrame(const std::string& l_strPath) {
    m_pRen->f_SavePNG(l_strPath);
}
