// game_tower.cpp -- the tower in climbs (the owner, 2026-11-28; docs/gameplay/27_ATTACK_MOMENT_AND_TOWER_RUN.md §3, §6).
//
// After the title comes the main screen: the hero, the money, the stamina, the tower of the story's chapters. "Challenge
// the tower" picks a story, then a chapter; a climb starts on the chapter's first floor with full HP. Every fight costs
// stamina (perFight); a point comes back every everySec of real time, also while the game is closed (stamina.json in the
// save folder: the points now and the next point's time; no timer when full). A climb ends:
//   - cleared: the stairs up from the chapter's last floor (its boss beaten): the tally, the rank's crystals (and a
//     challenge's), the chapter marked cleared (the next one opens), back to the main screen;
//   - a death: half the gold and crystals found in this climb are lost; the tally, then continue (the chapter's first
//     floor again, full HP) or the main screen.
// The shop is outside: no store while climbing. A challenge chapter climbs a story chapter's floors with stronger monsters.
#include "text_keys.h"     // toms::text: every line of text by its key
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"    // toms::tower_state, toms::key_prefix, ... (no hard-coded name)
#include "ui_names.h"      // toms::ui: the main screen's events
#include "res_keys.h"      // toms::key, toms::pattern
#include "game_internal.h"

#include <chrono>
#include <ctime>
#include <fstream>

using namespace toms::game_detail;

namespace {
long long f_WallSeconds() { return (long long)std::time(nullptr); }
std::string f_MinSec(int l_iSec) {
    l_iSec = std::max(0, l_iSec);
    char l_arrB[16];
    std::snprintf(l_arrB, sizeof(l_arrB), "%d:%02d", l_iSec / 60, l_iSec % 60);
    return l_arrB;
}
}  // namespace

// ---- the rules, the chapters' floors, the stamina file ----

void Game::f_LoadTowerRules() {
    m_oTowerRules = toms::f_TowerRunFromJson(f_ReadJsonFile(f_PathOf(toms::key::g_pszDataTowerRun)));
    m_mapChapterFloors.clear();
    for (const toms::TowerStory& l_oSt : m_oTowerRules.m_vStories)
        for (const toms::TowerChapter& l_oC : l_oSt.m_vChapters) {
            if (m_mapChapterFloors.count(l_oC.m_strFloorsOf)) continue;
            const nlohmann::json l_jCh = f_ReadJsonFile(f_PathFor(toms::pattern::g_pszChapter, l_oC.m_strFloorsOf));
            std::vector<std::string> l_vF;
            for (const auto& l_jF : l_jCh.value(toms::field::g_pszFloors, nlohmann::json::array())) if (l_jF.is_string()) l_vF.push_back(l_jF.get<std::string>());
            m_mapChapterFloors[l_oC.m_strFloorsOf] = l_vF;
            m_mapChapterTitle[l_oC.m_strFloorsOf] = l_jCh.value(toms::field::g_pszTitle, std::string());
        }
    m_oStamina = toms::f_StaminaFromJson(f_ReadJsonFile(f_StaminaPath()), m_oTowerRules.m_oStamina);
    if (m_oStamina.f_Catch(m_oTowerRules.m_oStamina, f_WallSeconds())) f_SaveStamina();   // the time the game was closed
}

std::string Game::f_StaminaPath() const { return toms::f_DefaultSaveDir() + "/stamina.json"; }   // name-ok: the stamina file in the save folder

void Game::f_SaveStamina() {
    toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
    std::ofstream l_oOut(f_StaminaPath(), std::ios::binary);
    if (l_oOut) l_oOut << toms::f_StaminaToJson(m_oStamina).dump(2) << "\n";
}

// Every frame (a look once a second): the timer's point when its time comes, then the next timer; none when full.
void Game::f_StepStamina(int l_iDtMs) {
    m_iStaminaLookMs += l_iDtMs;
    if (m_iStaminaLookMs < 1000) return;
    m_iStaminaLookMs = 0;
    if (m_oStamina.f_Catch(m_oTowerRules.m_oStamina, f_WallSeconds())) f_SaveStamina();
    if (m_bNoStamina && m_oStamina.m_iNow >= m_oTowerRules.m_oStamina.m_iPerFight) m_bNoStamina = false;   // it came back while waiting
}

// A fight starts on the map: during a climb it costs stamina. False: not enough (the box says when the next point comes).
bool Game::f_PayFight() {
    if (!m_oClimb.m_bOn) return true;
    if (m_oStamina.f_Spend(m_oTowerRules.m_oStamina, f_WallSeconds(), m_oTowerRules.m_oStamina.m_iPerFight)) { f_SaveStamina(); return true; }
    m_bNoStamina = true;
    m_oAudio.f_Play(toms::key::g_pszSfxDeny);
    return false;
}

std::vector<std::string> Game::f_ChapterFloors(const std::string& l_strOf) const {
    const auto l_it = m_mapChapterFloors.find(l_strOf);
    return l_it == m_mapChapterFloors.end() ? std::vector<std::string>{} : l_it->second;
}

bool Game::f_ChapterCleared(const std::string& l_strId) const { return m_oRun.f_Flag(std::string(toms::key_prefix::g_pszTowerCleared) + l_strId); }

bool Game::f_ChapterOpen(const toms::TowerChapter& l_oC) const {
    return !f_ChapterFloors(l_oC.m_strFloorsOf).empty() && (l_oC.m_strAfter.empty() || f_ChapterCleared(l_oC.m_strAfter));
}

std::string Game::f_ChapterName(const toms::TowerChapter& l_oC) const {
    if (!l_oC.m_strTitle.empty()) return m_oLocale.f_Tr(l_oC.m_strTitle);
    const auto l_it = m_mapChapterTitle.find(l_oC.m_strFloorsOf);
    return l_it == m_mapChapterTitle.end() || l_it->second.empty() ? l_oC.m_strId : m_oLocale.f_Tr(l_it->second);
}

// ---- the main screen ----

void Game::f_OpenMainScreen() {
    m_bMainOpen = true;
    m_iMainPanel = 0;
    if (m_strMainStory.empty() && !m_oTowerRules.m_vStories.empty()) m_strMainStory = m_oTowerRules.m_vStories.front().m_strId;
    if (m_bInDialogue) m_bInDialogue = false;
    if (m_bInGameMenuOpen) f_ClosePlayerMenu();
    m_bStoreOpen = false;
}

void Game::f_CloseMainScreen() { m_bMainOpen = false; m_iMainPanel = 0; }

const toms::TowerStory* Game::f_MainStory() const {
    for (const toms::TowerStory& l_oSt : m_oTowerRules.m_vStories) if (l_oSt.m_strId == m_strMainStory) return &l_oSt;
    return m_oTowerRules.m_vStories.empty() ? nullptr : &m_oTowerRules.m_vStories.front();
}

void Game::f_StartClimb(const std::string& l_strChapter) {
    const toms::TowerStory* l_pSt = nullptr;
    const toms::TowerChapter* l_pC = toms::f_FindChapter(m_oTowerRules, l_strChapter, &l_pSt);
    if (!l_pC || !f_ChapterOpen(*l_pC)) { m_oAudio.f_Play(toms::key::g_pszSfxDeny); return; }
    const std::vector<std::string> l_vFloors = f_ChapterFloors(l_pC->m_strFloorsOf);
    m_oClimb = Climb{};
    m_oClimb.m_bOn = true;
    m_oClimb.m_strChapter = l_pC->m_strId;
    m_oClimb.m_vFloors = l_vFloors;
    m_oClimb.m_bChallenge = l_pSt && l_pSt->m_bChallenge;
    m_oClimb.m_fHpX = l_pC->m_fHpX; m_oClimb.m_fAtkX = l_pC->m_fAtkX; m_oClimb.m_fGoldX = l_pC->m_fGoldX;
    m_oClimb.m_iGold0 = m_oPl.m_iGold;
    m_oClimb.m_iCrystals0 = m_oPl.m_iCrystals;
    m_oClimb.m_llStart = f_WallSeconds();
    m_oTally = Tally{};
    m_bNoStamina = false;
    f_CloseMainScreen();
    m_oPl.m_iHp = f_EffectiveMaxHp();   // a climb starts with full HP
    f_LoadStage(l_vFloors.front());
    m_oClimb.m_setFloors.insert(l_vFloors.front());
    m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
    // the tutorial is chapter 1 (the owner, 2026-11-28): the start story (the morning, the weapon master's trial) on the story's
    // first chapter's first climb of a real game (a scenario has no slot)
    if (l_pSt && !l_pSt->m_bChallenge && !l_pSt->m_vChapters.empty() && l_pSt->m_vChapters.front().m_strId == l_pC->m_strId &&
        !m_bSkipStartStory && m_iActiveSlot > 0 && !m_oRun.f_Flag(toms::key_prefix::g_pszTowerPrologue)) {
        m_oRun.f_SetFlag(toms::key_prefix::g_pszTowerPrologue);
        f_StartWeaponTrial();
    }
    f_MarkProgressDirty();
    f_SaveCurrentRun();
}

// The tally's numbers: the climb's stats, its score and rank.
void Game::f_FillTally(bool l_bCleared) {
    m_oTally = Tally{};
    m_oTally.m_bOpen = true;
    m_oTally.m_bCleared = l_bCleared;
    toms::ClimbStats& l_oS = m_oTally.m_oStats;
    l_oS.m_iFloors = (int)m_oClimb.m_setFloors.size();
    l_oS.m_iMonsters = m_oClimb.m_iMonsters;
    l_oS.m_iBestCombo = m_oClimb.m_iBestCombo;
    l_oS.m_iPerfects = m_oClimb.m_iPerfects;
    l_oS.m_iDamageTaken = m_oClimb.m_iDamage;
    l_oS.m_iSeconds = (int)std::max(0LL, f_WallSeconds() - m_oClimb.m_llStart);
    m_oTally.m_iScore = toms::f_ClimbScore(l_oS, m_oTowerRules);
    const toms::RankRule* l_pRk = toms::f_RankFor(m_oTally.m_iScore, m_oTowerRules);
    m_oTally.m_strRank = l_pRk ? l_pRk->m_strRank : std::string();
    m_oTally.m_iGold = std::max(0, m_oPl.m_iGold - m_oClimb.m_iGold0);
    m_oTally.m_iCrystals = std::max(0, m_oPl.m_iCrystals - m_oClimb.m_iCrystals0);
}

void Game::f_ClimbDied() {
    f_FillTally(false);
    // half of what this climb found is lost (Q134, Q143)
    m_oTally.m_iLostGold = m_oTally.m_iGold - (int)std::lround(m_oTally.m_iGold * m_oTowerRules.m_fKeepGoldOnDeath);
    m_oTally.m_iLostCrystals = m_oTally.m_iCrystals - (int)std::lround(m_oTally.m_iCrystals * m_oTowerRules.m_fKeepCrystalsOnDeath);
    m_oPl.m_iGold = std::max(0, m_oPl.m_iGold - m_oTally.m_iLostGold);
    m_oPl.m_iCrystals = std::max(0, m_oPl.m_iCrystals - m_oTally.m_iLostCrystals);
    m_oTally.m_iGold -= m_oTally.m_iLostGold;
    m_oTally.m_iCrystals -= m_oTally.m_iLostCrystals;
    m_oPl.m_iHp = f_EffectiveMaxHp();
    f_BattleEvent(toms::battle_event::g_pszClimbEnd, -1, -1, 0, m_oClimb.m_strChapter);
    f_MarkProgressDirty();
}

void Game::f_ClimbCleared() {
    f_FillTally(true);
    const toms::TowerChapter* l_pC = toms::f_FindChapter(m_oTowerRules, m_oClimb.m_strChapter);
    const toms::RankRule* l_pRk = toms::f_RankFor(m_oTally.m_iScore, m_oTowerRules);
    m_oTally.m_iBonus = (l_pRk ? l_pRk->m_iCrystals : 0) + (l_pC ? l_pC->m_iCrystals : 0);
    m_oPl.m_iCrystals += m_oTally.m_iBonus;
    m_oTally.m_iCrystals += m_oTally.m_iBonus;
    m_oRun.f_SetFlag(std::string(toms::key_prefix::g_pszTowerCleared) + m_oClimb.m_strChapter);
    m_oPl.m_iHp = f_EffectiveMaxHp();
    f_BattleEvent(toms::battle_event::g_pszClimbEnd, -1, -1, 1, m_oClimb.m_strChapter);
    m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
    f_MarkProgressDirty();
    f_SaveCurrentRun();
}

void Game::f_TallyContinue() {   // a death: the chapter's first floor again
    const std::string l_strCh = m_oClimb.m_strChapter;
    m_oTally = Tally{};
    f_StartClimb(l_strCh);
}

void Game::f_TallyHome() {
    m_oTally = Tally{};
    m_oClimb = Climb{};
    m_bNoStamina = false;
    f_OpenMainScreen();
    f_SaveCurrentRun();
}

// The stairs during a climb: up from the chapter's last floor clears it; never out of the chapter's floors.
bool Game::f_ClimbStairs(const std::string& l_strTarget, bool l_bIsUp) {
    if (!m_oClimb.m_bOn || m_oClimb.m_vFloors.empty()) return false;
    if (l_bIsUp && m_strCurStage == m_oClimb.m_vFloors.back()) { f_ClimbCleared(); return true; }
    if (std::find(m_oClimb.m_vFloors.begin(), m_oClimb.m_vFloors.end(), l_strTarget) == m_oClimb.m_vFloors.end()) {
        m_vNotifications.push_back({m_oLocale.f_Tr(toms::text::g_pszTowerChapterStart), 2500});
        m_oAudio.f_Play(toms::key::g_pszSfxDeny);
        return true;
    }
    return false;
}

void Game::f_ClimbFightOver(bool l_bWon) {   // its numbers for the tally
    if (!m_oClimb.m_bOn) return;
    if (l_bWon) for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) if (!l_oF.m_bFled) m_oClimb.m_iMonsters++;
    m_oClimb.m_iBestCombo = std::max(m_oClimb.m_iBestCombo, m_oCs.m_iBestChain);
    m_oClimb.m_iDamage += m_oCs.m_iDamageTaken;
}

// ---- the main screen's buttons ----

void Game::f_MainEvent(const std::string& l_strName, int l_iArg) {
    if (l_strName == toms::ui::g_pszTallyContinue) { if (m_oTally.m_bOpen && !m_oTally.m_bCleared) f_TallyContinue(); return; }
    if (l_strName == toms::ui::g_pszTallyHome) { if (m_oTally.m_bOpen) f_TallyHome(); return; }
    if (l_strName == toms::ui::g_pszStaminaWait) { m_bNoStamina = false; return; }
    if (l_strName == toms::ui::g_pszStaminaHome) { if (m_oClimb.m_bOn) f_TallyHome(); m_bNoStamina = false; return; }
    if (!m_bMainOpen) return;
    const toms::TowerStory* l_pSt = f_MainStory();
    if (l_strName == toms::ui::g_pszMainClimb) { m_iMainPanel = 1; m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick); return; }
    if (l_strName == toms::ui::g_pszMainBack) { m_iMainPanel = m_iMainPanel == 2 ? 1 : 0; m_oAudio.f_Play(toms::key::g_pszSfxCloseUi); return; }
    if (l_strName == toms::ui::g_pszMainVillage) { m_iMainPanel = 3; m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick); return; }
    if (l_strName == toms::ui::g_pszMainBag) { f_OpenPlayerMenu((int)MenuTab::Items); return; }
    if (l_strName == toms::ui::g_pszMainQuests) { f_OpenPlayerMenu((int)MenuTab::Events); return; }
    if (l_strName == toms::ui::g_pszMainShop) {
        if (m_bStoreUnlocked) f_OpenStore();
        else { m_vNotifications.push_back({m_oLocale.f_Tr(toms::text::g_pszTowerSoon), 2000}); m_oAudio.f_Play(toms::key::g_pszSfxDeny); }
        return;
    }
    if (l_strName == toms::ui::g_pszMainSoon) { m_vNotifications.push_back({m_oLocale.f_Tr(toms::text::g_pszTowerSoon), 2000}); return; }
    if (l_strName == toms::ui::g_pszMainChapter) {   // a band of the tower: that chapter of the story shown
        if (l_pSt && l_iArg >= 0 && l_iArg < (int)l_pSt->m_vChapters.size()) f_StartClimb(l_pSt->m_vChapters[(size_t)l_iArg].m_strId);
        return;
    }
    if (l_strName == toms::ui::g_pszMainPick) {
        if (m_iMainPanel == 1 && l_iArg >= 0 && l_iArg < (int)m_oTowerRules.m_vStories.size()) {   // a story: its chapters
            m_strMainStory = m_oTowerRules.m_vStories[(size_t)l_iArg].m_strId;
            m_iMainPanel = 2;
            m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
        } else if (m_iMainPanel == 2 && l_pSt && l_iArg >= 0 && l_iArg < (int)l_pSt->m_vChapters.size()) {
            f_StartClimb(l_pSt->m_vChapters[(size_t)l_iArg].m_strId);
        } else if (m_iMainPanel == 3) {   // a place in the village
            const std::vector<std::string> l_vIds = f_VillagePlaces();
            if (l_iArg >= 0 && l_iArg < (int)l_vIds.size()) f_ActivateHubLocation(l_vIds[(size_t)l_iArg]);
        }
    }
}

std::vector<std::string> Game::f_VillagePlaces() const {
    std::vector<std::string> l_vIds;
    for (const auto& [l_strId, l_oD] : m_mapHubDefs) if (l_oD.m_strUnlockFlag.empty() || m_oRun.f_Flag(l_oD.m_strUnlockFlag)) l_vIds.push_back(l_strId);
    return l_vIds;
}

// ---- the main screen, drawn ----

void Game::f_FillMainUi(toms::UiMain& l_oM) const {
    const toms::Locale& l_oL = m_oLocale;
    l_oM.m_bHome = m_bMainOpen;
    l_oM.m_bTally = m_oTally.m_bOpen;
    l_oM.m_bNoStamina = m_bNoStamina && !m_oTally.m_bOpen;
    l_oM.m_bVisible = l_oM.m_bHome || l_oM.m_bTally || l_oM.m_bNoStamina;
    if (!l_oM.m_bVisible) return;
    const long long l_llNow = f_WallSeconds();
    const int l_iNext = m_oStamina.f_SecondsToNext(l_llNow);
    l_oM.m_strHomeLabel = l_oL.f_Tr(toms::text::g_pszTowerHome);
    if (l_oM.m_bNoStamina) {
        l_oM.m_strNoStaminaText = f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerNoStamina), toms::field::g_pszTime, f_MinSec(l_iNext));
        l_oM.m_strWaitLabel = l_oL.f_Tr(toms::text::g_pszTowerWait);
    }
    if (l_oM.m_bTally) {
        const Tally& l_oT = m_oTally;
        l_oM.m_bTallyCleared = l_oT.m_bCleared;
        l_oM.m_strTallyTitle = l_oL.f_Tr(l_oT.m_bCleared ? toms::text::g_pszTowerTallyCleared : toms::text::g_pszTowerTallyDied);
        l_oM.m_strTallyRank = l_oT.m_strRank;
        l_oM.m_strTallyScore = f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerTallyScore), toms::field::g_pszN, std::to_string(l_oT.m_iScore));
        auto l_fnRow = [&](const char* l_pszKey, const std::string& l_strV) { toms::UiTowerRow l_oR; l_oR.m_strLabel = l_oL.f_Tr(l_pszKey); l_oR.m_strValue = l_strV; l_oM.m_vTallyRows.push_back(l_oR); };
        l_fnRow(toms::text::g_pszTowerTallyFloors, std::to_string(l_oT.m_oStats.m_iFloors));
        l_fnRow(toms::text::g_pszTowerTallyMonsters, std::to_string(l_oT.m_oStats.m_iMonsters));
        l_fnRow(toms::text::g_pszTowerTallyCombo, std::to_string(l_oT.m_oStats.m_iBestCombo));
        l_fnRow(toms::text::g_pszTowerTallyPerfects, std::to_string(l_oT.m_oStats.m_iPerfects));
        l_fnRow(toms::text::g_pszTowerTallyDamage, std::to_string(l_oT.m_oStats.m_iDamageTaken));
        l_fnRow(toms::text::g_pszTowerTallyTime, f_MinSec(l_oT.m_oStats.m_iSeconds));
        l_fnRow(toms::text::g_pszTowerTallyGold, std::to_string(l_oT.m_iGold));
        l_fnRow(toms::text::g_pszTowerTallyCrystals, std::to_string(l_oT.m_iCrystals));
        if (!l_oT.m_bCleared)
            l_oM.m_strTallyNote = f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerTallyLost), toms::field::g_pszGold, std::to_string(l_oT.m_iLostGold)),
                                            toms::field::g_pszCrystals, std::to_string(l_oT.m_iLostCrystals));
        else if (l_oT.m_iBonus > 0) l_oM.m_strTallyNote = f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerTallyBonus), toms::field::g_pszN, std::to_string(l_oT.m_iBonus));
        l_oM.m_strContinueLabel = l_oL.f_Tr(toms::text::g_pszTowerContinue);
    }
    if (!l_oM.m_bHome) return;
    l_oM.m_strTitle = l_oL.f_Tr(toms::text::g_pszTowerTitle);
    l_oM.m_strSubtitle = l_oL.f_Tr(toms::text::g_pszTowerSubtitle);
    l_oM.m_strHeroLevel = f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerLevel), toms::field::g_pszN, std::to_string(m_oPl.m_iLv));
    const int l_iMax = std::max(1, f_EffectiveMaxHp());
    l_oM.m_strHeroHp = "HP " + std::to_string(m_oPl.m_iHp) + " / " + std::to_string(l_iMax);
    l_oM.m_fHeroHpPct = 100.0f * (float)std::clamp(m_oPl.m_iHp, 0, l_iMax) / (float)l_iMax;
    l_oM.m_strAtkLabel = l_oL.f_Tr(toms::text::g_pszTowerAtk);
    l_oM.m_strDefLabel = l_oL.f_Tr(toms::text::g_pszTowerDef);
    l_oM.m_strHpLabel = l_oL.f_Tr(toms::text::g_pszTowerHp);
    l_oM.m_strHeroAtk = std::to_string(f_EffectiveAtk());
    l_oM.m_strHeroDef = std::to_string(f_EffectiveDef());
    l_oM.m_strGoldText = std::to_string(m_oPl.m_iGold);
    l_oM.m_strCrystalsText = std::to_string(m_oPl.m_iCrystals);
    l_oM.m_strStaminaText = std::to_string(m_oStamina.m_iNow) + " / " + std::to_string(m_oTowerRules.m_oStamina.m_iMax);
    l_oM.m_strStaminaNext = l_iNext > 0 ? f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerStaminaNext), toms::field::g_pszTime, f_MinSec(l_iNext))
                                        : l_oL.f_Tr(toms::text::g_pszTowerStaminaFull);
    l_oM.m_strClimbLabel = l_oL.f_Tr(toms::text::g_pszTowerClimb);
    l_oM.m_strVillageLabel = l_oL.f_Tr(toms::text::g_pszTowerVillage);
    l_oM.m_strBagLabel = l_oL.f_Tr(toms::text::g_pszTowerBag);
    l_oM.m_strShopLabel = l_oL.f_Tr(toms::text::g_pszTowerShop);
    l_oM.m_strQuestsLabel = l_oL.f_Tr(toms::text::g_pszTowerQuests);
    l_oM.m_strRankLabel = l_oL.f_Tr(toms::text::g_pszTowerRanking);
    l_oM.m_strHeroesLabel = l_oL.f_Tr(toms::text::g_pszTowerHeroes);
    l_oM.m_strBackLabel = l_oL.f_Tr(toms::text::g_pszTowerBack);
    // a chapter's row: its name, its floors, its state
    auto l_fnChapter = [&](const toms::TowerChapter& l_oC, int l_iN) {
        toms::UiTowerRow l_oR;
        const std::vector<std::string> l_vF = f_ChapterFloors(l_oC.m_strFloorsOf);
        const bool l_bChallenge = !l_oC.m_strTitle.empty();
        l_oR.m_strLabel = (l_bChallenge ? std::string() : f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerChapterN), toms::field::g_pszN, std::to_string(l_iN)) + "  ") + f_ChapterName(l_oC);
        const std::string l_strFloors = l_vF.empty() ? std::string() :
            f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerFloors), toms::field::g_pszFrom, l_vF.front()), toms::field::g_pszTo, l_vF.back());
        const bool l_bOpen = f_ChapterOpen(l_oC), l_bDone = f_ChapterCleared(l_oC.m_strId);
        l_oR.m_strState = l_bDone ? toms::tower_state::g_pszCleared : l_bOpen ? toms::tower_state::g_pszNext : toms::tower_state::g_pszLocked;
        l_oR.m_bEnabled = l_bOpen;
        std::string l_strWhy;
        if (l_bDone) l_strWhy = l_oL.f_Tr(toms::text::g_pszTowerCleared);
        else if (!l_bOpen) {
            const toms::TowerChapter* l_pA = toms::f_FindChapter(m_oTowerRules, l_oC.m_strAfter);
            l_strWhy = l_pA && l_bChallenge ? f_TrParam(l_oL.f_Tr(toms::text::g_pszTowerLockedAfter), toms::field::g_pszName, f_ChapterName(*l_pA))
                                            : l_oL.f_Tr(toms::text::g_pszTowerLocked);
        } else l_strWhy = l_oL.f_Tr(toms::text::g_pszTowerNext);
        l_oR.m_strSub = l_strFloors + (l_strFloors.empty() ? "" : "  ·  ") + l_strWhy;
        return l_oR;
    };
    const toms::TowerStory* l_pSt = f_MainStory();
    if (l_pSt) {   // the tower: the story's chapters, the last on top
        for (int l_iK = (int)l_pSt->m_vChapters.size() - 1; l_iK >= 0; l_iK--) l_oM.m_vTower.push_back(l_fnChapter(l_pSt->m_vChapters[(size_t)l_iK], l_iK + 1));
    }
    l_oM.m_iPanel = m_iMainPanel;
    if (m_iMainPanel == 1) {
        l_oM.m_strPanelTitle = l_oL.f_Tr(toms::text::g_pszTowerPickStory);
        for (const toms::TowerStory& l_oSt : m_oTowerRules.m_vStories) {
            toms::UiTowerRow l_oR;
            l_oR.m_strLabel = l_oL.f_Tr(l_oSt.m_strTitle);
            int l_iDone = 0;
            for (const toms::TowerChapter& l_oC : l_oSt.m_vChapters) l_iDone += f_ChapterCleared(l_oC.m_strId) ? 1 : 0;
            l_oR.m_strSub = std::to_string(l_iDone) + " / " + std::to_string(l_oSt.m_vChapters.size());
            l_oM.m_vRows.push_back(l_oR);
        }
    } else if (m_iMainPanel == 2 && l_pSt) {
        l_oM.m_strPanelTitle = l_oL.f_Tr(l_pSt->m_strTitle) + " — " + l_oL.f_Tr(toms::text::g_pszTowerPickChapter);
        for (size_t l_nK = 0; l_nK < l_pSt->m_vChapters.size(); l_nK++) l_oM.m_vRows.push_back(l_fnChapter(l_pSt->m_vChapters[l_nK], (int)l_nK + 1));
    } else if (m_iMainPanel == 3) {
        l_oM.m_strPanelTitle = l_oL.f_Tr(toms::text::g_pszTowerVillage);
        for (const std::string& l_strId : f_VillagePlaces()) {
            const auto l_it = m_mapHubDefs.find(l_strId);
            toms::UiTowerRow l_oR;
            l_oR.m_strLabel = l_oL.f_Field(l_it->second.m_jName);
            l_oR.m_strSub = l_oL.f_Field(l_it->second.m_jDesc);
            l_oM.m_vRows.push_back(l_oR);
        }
        if (l_oM.m_vRows.empty()) { toms::UiTowerRow l_oR; l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszTowerNoPlaces); l_oR.m_bEnabled = false; l_oM.m_vRows.push_back(l_oR); }
    }
}
