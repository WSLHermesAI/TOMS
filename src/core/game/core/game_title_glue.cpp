// game_title_glue.cpp — title phase + save slots glue: title menu state, slot summaries,
// new game / continue, autosave, language switching, and the title pages' drawing.
// Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

// =====================================================================================
// Title phase — New Game / Continue / Settings (see src/game/title_screen.h)
// =====================================================================================

std::string Game::f_StageDisplayName(const std::string& l_strId) const {
    // Match either identifier: the game loads stages by FILE STEM ("stage01"), while the id
    // inside the JSON is "stage_01" -- so a pure id comparison silently missed and the Continue
    // list showed the raw stem instead of the stage's real name.
    for (const auto& l_oS : m_vStageList)
        if (l_oS.m_strId == l_strId || l_oS.m_strFileStem == l_strId) return l_oS.m_strName.empty() ? l_strId : l_oS.m_strName;
    return l_strId;
}

// Snapshots the live game state into the Run-save schema (save_system.h) for writing to a slot.
toms::RunSaveData Game::f_RunSaveFromState() const {
    toms::RunSaveData l_oR;
    l_oR.m_strCurrentStageId = m_strCurStage;
    l_oR.m_oPlayer.m_iHp = m_oPl.m_iHp; l_oR.m_oPlayer.m_iMaxhp = m_oPl.m_iMaxhp;
    l_oR.m_oPlayer.m_iAtk = m_oPl.m_iAtk; l_oR.m_oPlayer.m_iDef = m_oPl.m_iDef;
    l_oR.m_oPlayer.m_iGold = m_oPl.m_iGold; l_oR.m_oPlayer.m_iExp = m_oPl.m_iExp; l_oR.m_oPlayer.m_iLv = m_oPl.m_iLv;
    l_oR.m_oPlayer.m_iKeyYellow = m_oPl.m_iKeyYellow; l_oR.m_oPlayer.m_iKeyBlue = m_oPl.m_iKeyBlue; l_oR.m_oPlayer.m_iKeyRed = m_oPl.m_iKeyRed;
    l_oR.m_oPlayer.m_vInv = m_oPl.m_vInv;
    l_oR.m_oPlayer.m_mapAttrs = m_oPl.m_mapAttrs;
    l_oR.m_oPlayer.m_iAttrPoints = m_oPl.m_iAttrPoints;
    l_oR.m_oPlayer.m_vGear = m_vGearOwned;
    l_oR.m_oPlayer.m_mapWeaponUses = m_mapWeaponUses;
    l_oR.m_oPlayer.m_iCrystals = m_oPl.m_iCrystals;
    l_oR.m_oPlayer.m_strWeapon = m_oEquipped.m_strWeaponId; l_oR.m_oPlayer.m_strArmor = m_oEquipped.m_strArmorId; l_oR.m_oPlayer.m_strTalent = m_oEquipped.m_strTalentId;
    l_oR.m_mapEntityStatus = m_mapEntityStatus;
    // S3: the run's story state travels with the run save (schemaVersion 3) -- choices, counters,
    // side-story states, run flags, shards, floor progress and deaths.
    m_oRun.f_WriteInto(l_oR);
    return l_oR;
}

void Game::f_SaveCurrentRun() {
    if (m_iActiveSlot <= 0) return;          // no slot chosen yet (title still up at boot)
    if (m_bSlotReadOnly) { m_bSaveDirty = false; return; }   // a newer game's save: never written over
    f_EnsureStageListLoaded();
    const std::string l_strDir = toms::f_DefaultSaveDir();
    if (toms::f_WriteSlotSave(l_strDir, m_iActiveSlot, m_oMeta, f_RunSaveFromState(), m_iPlayTimeSec,
                            toms::f_NowStamp(), f_StageDisplayName(m_strCurStage))) {
        m_bSaveDirty = false;
    }
    if (m_oTitle.f_IsOpen()) f_RefreshSlots();
}

void Game::f_RefreshSlots() {
    std::vector<toms::SlotSummary> l_vV;
    const std::string l_strDir = toms::f_DefaultSaveDir();
    for (int l_iI = 1; l_iI <= m_oTitle.f_SlotCount(); l_iI++) l_vV.push_back(toms::f_SummarizeSlot(l_strDir, l_iI));
    m_oTitle.f_SetSummaries(std::move(l_vV));
}

void Game::f_ApplyLanguage() {
    // The Settings page owns the selection -- mirror it into the persisted preference. Reading
    // it back from title_ (rather than re-applying the old settings_.language) is what makes
    // the choice actually stick: without this the freshly-picked language was overwritten by
    // the stale preference on the very same call, so the UI snapped back.
    const int l_iIdx = m_oTitle.f_LanguageIndex();
    if (l_iIdx >= 0 && l_iIdx < m_oLocale.f_LanguageCount())
        m_oSettings.m_strLanguage = m_oLocale.f_Languages()[l_iIdx].m_strCode;
    m_oLocale.f_SetLanguage(m_oSettings.m_strLanguage);
    m_oSettings.m_strLanguage = m_oLocale.f_LanguageCode();   // normalize (e.g. an unknown code -> default)
    m_oTitle.f_SetLanguageIndex(m_oLocale.f_LanguageIndex());
    toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
    toms::f_SaveGameSettings(toms::f_SettingsPath(), m_oSettings);
}

void Game::f_ApplyArtStyle(int l_iIdx) {
    // Persisted, then loaded at once if the title screen is up; chosen during a run it waits for
    // the way back to the title (returnToTitle -> refreshArtStyle), so a run never changes look
    // under the player. The confirm dialog that led here says which of the two happens.
    if (l_iIdx < 0 || l_iIdx >= (int)m_vArtStyles.size()) l_iIdx = 0;
    m_oSettings.m_strArtStyle = m_vArtStyles[l_iIdx].m_strId;
    m_oTitle.f_SetStyleIndex(l_iIdx);
    toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
    toms::f_SaveGameSettings(toms::f_SettingsPath(), m_oSettings);
    f_RefreshArtStyle();
}

void Game::f_ApplyLoadedRun(const toms::MetaSaveData& l_oM, const toms::RunSaveData& l_oR,
                          int l_iSlot, int l_iPlaySec) {
    m_oMeta = l_oM;
    m_mapEntityStatus = l_oR.m_mapEntityStatus;
    m_oRun.f_ReadFrom(l_oR);   // S3: restore the run's story state (defaults for a pre-v3 file)
    // Anything that belongs to the *previous* run must not leak into the loaded one.
    m_mapMissionTrackers.clear();
    m_vNotifications.clear();
    m_oCs = CombatState{};
    m_bInDialogue = false; m_vDlgChoices.clear(); m_strDlgNode = "root";
    if (m_bInGameMenuOpen) f_ClosePlayerMenu();
    m_bStoreOpen = false; m_bStoreUnlockDlg = false; m_bStoreFromMenu = false;
    m_bStageSelectOpen = false; m_bStairsConfirmOpen = false;
    m_oPl.m_iHp = l_oR.m_oPlayer.m_iHp; m_oPl.m_iMaxhp = l_oR.m_oPlayer.m_iMaxhp;
    m_oPl.m_iAtk = l_oR.m_oPlayer.m_iAtk; m_oPl.m_iDef = l_oR.m_oPlayer.m_iDef;
    m_oPl.m_iGold = l_oR.m_oPlayer.m_iGold; m_oPl.m_iExp = l_oR.m_oPlayer.m_iExp; m_oPl.m_iLv = l_oR.m_oPlayer.m_iLv;
    m_oPl.m_iCrystals = l_oR.m_oPlayer.m_iCrystals;
    m_oPl.m_iKeyYellow = l_oR.m_oPlayer.m_iKeyYellow; m_oPl.m_iKeyBlue = l_oR.m_oPlayer.m_iKeyBlue; m_oPl.m_iKeyRed = l_oR.m_oPlayer.m_iKeyRed;
    m_oPl.m_vInv = l_oR.m_oPlayer.m_vInv;
    f_ResetAttrs();                                  // a save from before the attributes: base + levels
    for (const auto& [l_strK, l_iV] : l_oR.m_oPlayer.m_mapAttrs) m_oPl.m_mapAttrs[l_strK] = l_iV;
    m_oPl.m_iAttrPoints = std::max(0, l_oR.m_oPlayer.m_iAttrPoints);
    m_mapAttrDraft.clear();   // a draft belongs to the run it was made in
    m_vGearOwned.clear();
    for (const std::string& l_strG : l_oR.m_oPlayer.m_vGear) if (m_mapEquipmentDefs.count(l_strG)) m_vGearOwned.push_back(l_strG);
    m_oEquipped = toms::EquippedSet{};
    m_mapWeaponUses = l_oR.m_oPlayer.m_mapWeaponUses;
    for (const std::string& l_strG : {l_oR.m_oPlayer.m_strWeapon, l_oR.m_oPlayer.m_strArmor, l_oR.m_oPlayer.m_strTalent})
        if (!l_strG.empty()) f_OwnGear(l_strG, true);          // worn gear is owned gear, even in a hand-edited save
    m_bNewsGear = m_bNewsEvents = false;
    m_nEventLogSeen = 0;
    m_iActiveSlot = l_iSlot;
    m_iPlayTimeSec = l_iPlaySec;
    f_LoadStage(l_oR.m_strCurrentStageId.empty() ? std::string("stage01") : l_oR.m_strCurrentStageId);
    m_oTitle.f_Close();
    m_oTitle.f_SetRunInProgress(true);
}

bool Game::f_ContinueFromSlot(int l_iSlot) {
    if (l_iSlot <= 0) return false;
    toms::MetaSaveData l_oM;
    toms::RunSaveData l_oR;
    int l_iPlaySec = 0;
    toms::SlotLoadInfo l_oInfo;
    if (!toms::f_ReadSlotSave(toms::f_DefaultSaveDir(), l_iSlot, l_oM, l_oR, &l_iPlaySec, nullptr, &l_oInfo)) {
        // Deliberately silent: the Continue row already reads "[空]/[Empty]" and the page shows
        // "no saves yet -- start a new game first", and the toast path draws with ImGui's
        // default font, which has no CJK glyphs (it would render as a "?" box). The title stays
        // open, so there is nothing to resume from.
        f_RefreshSlots();
        return false;
    }
    f_ApplyLoadedRun(l_oM, l_oR, l_iSlot, l_iPlaySec);
    m_bSlotReadOnly = l_oInfo.m_bNewer;
    if (l_oInfo.m_bFromBackup) m_vNotifications.push_back({m_oLocale.f_Tr("save.restored"), 5000});
    if (l_oInfo.m_bNewer) m_vNotifications.push_back({m_oLocale.f_Tr("save.newer"), 6000});
    return true;
}

void Game::f_NewGame() { f_NewGame(0); }

void Game::f_HandleTitleAction(toms::TitleAction l_eA) {
    switch (l_eA) {
        case toms::TitleAction::NewGame:             // then the start story (docs/gameplay/07 phase 3)
            f_NewGame();
            if (!m_bSkipStartStory) f_StartWeaponTrial();
            break;
        case toms::TitleAction::StartNewGameInSlot:
            f_NewGame(m_oTitle.f_PendingSlot());
            if (!m_bSkipStartStory) f_StartWeaponTrial();
            break;
        case toms::TitleAction::LoadSlot:            f_ContinueFromSlot(m_oTitle.f_PendingSlot()); break;
        case toms::TitleAction::OpenContinue:        f_RefreshSlots(); break;   // always show current files
        case toms::TitleAction::SetLanguage:         f_ApplyLanguage(); break;
        case toms::TitleAction::SetArtStyle:         f_ApplyArtStyle(m_oTitle.f_StyleIndex()); break;
        // The confirm dialogs are drawn and answered by the title itself; these are informational.
        case toms::TitleAction::AskNewGameInSlot:
        case toms::TitleAction::DismissNewGameConfirm:
        case toms::TitleAction::AskLanguageChange:
        case toms::TitleAction::DismissLanguageConfirm:
        case toms::TitleAction::AskStyleChange:
        case toms::TitleAction::DismissStyleConfirm:
        case toms::TitleAction::OpenSettings:
        case toms::TitleAction::Back:
        case toms::TitleAction::None:                break;
    }
}

void Game::f_TitleMove(int l_iDx, int l_iDy) {
    if (!m_oTitle.f_IsOpen()) return;
    toms::TitleAction l_eA = toms::TitleAction::None;
    if (l_iDy != 0) l_eA = m_oTitle.f_MoveVertical(l_iDy);
    if (l_iDx != 0) {
        toms::TitleAction l_eH = m_oTitle.f_MoveHorizontal(l_iDx);   // Settings: left/right switches language
        if (l_eA == toms::TitleAction::None) l_eA = l_eH;
    }
    f_HandleTitleAction(l_eA);
}

void Game::f_TitleConfirm() {
    if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_Activate());
}

void Game::f_TitleCancel() {
    if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_Cancel());
}
