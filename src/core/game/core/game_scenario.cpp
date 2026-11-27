// game_scenario.cpp -- test scenarios: start the game straight in one scene from a setting file
// (docs/gameplay/08_TEST_SCENARIOS.md). toms_game --scenario=<folder or file> plays it; --scenario-test also runs the
// scenario's own "test" frames and keys and exits 3 when scenarioCheck fails (tests/scenarios/, ctest scenario.*).
//
// A scenario never writes a save: its run has no slot (newGame(-1)), and a save it starts from is read from the
// scenario's folder, never from the player's save folder.
#include "text_keys.h"     // toms::text: every line of text by its key
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"   // toms::state, toms::battle_event, ... (no hard-coded name)
#include "game.h"
#include "game_internal.h"

using namespace toms::game_detail;   // trParam

#include <filesystem>

namespace {

namespace fs = std::filesystem;

// "include" files first (each may include others), then this file's own fields on top: objects merge key by key,
// anything else is replaced. Paths are relative to the file that names them.
bool f_ReadScenarioJson(const fs::path& l_oFile, nlohmann::json& l_jOut, std::string& l_strError, int l_iDepth = 0) {
    if (l_iDepth > 8) { l_strError = "include nested too deep at " + l_oFile.string(); return false; }
    nlohmann::json l_jJ = toms::f_ReadJsonFileSafe(l_oFile.string());
    if (!l_jJ.is_object()) { l_strError = "cannot read " + l_oFile.string() + " (missing, or not a JSON object)"; return false; }
    // file names in this file are relative to it: made absolute here, so an included file's save or dialogue is
    // found next to that file, not next to the scenario that includes it
    auto l_fnAbsolute = [&](nlohmann::json& l_jV) { if (l_jV.is_string()) l_jV = (l_oFile.parent_path() / l_jV.get<std::string>()).string(); };
    if (l_jJ.contains(toms::field::g_pszSave)) l_fnAbsolute(l_jJ[toms::field::g_pszSave]);
    if (l_jJ.contains(toms::field::g_pszDialogue) && l_jJ[toms::field::g_pszDialogue].is_object() && l_jJ[toms::field::g_pszDialogue].contains(toms::field::g_pszFile)) l_fnAbsolute(l_jJ[toms::field::g_pszDialogue][toms::field::g_pszFile]);
    if (l_jJ.contains(toms::field::g_pszStage) && l_jJ[toms::field::g_pszStage].is_string()) {
        const std::string l_strSt = l_jJ[toms::field::g_pszStage].get<std::string>();
        if (l_strSt.size() > 5 && l_strSt.compare(l_strSt.size() - 5, 5, ".json") == 0) l_fnAbsolute(l_jJ[toms::field::g_pszStage]);
    }
    nlohmann::json l_jBase = nlohmann::json::object();
    if (l_jJ.contains(toms::field::g_pszInclude)) {
        const nlohmann::json l_jInc = l_jJ[toms::field::g_pszInclude].is_array() ? l_jJ[toms::field::g_pszInclude] : nlohmann::json::array({l_jJ[toms::field::g_pszInclude]});
        for (const auto& l_jName : l_jInc) {
            if (!l_jName.is_string()) continue;
            nlohmann::json l_jPart;
            if (!f_ReadScenarioJson(l_oFile.parent_path() / l_jName.get<std::string>(), l_jPart, l_strError, l_iDepth + 1)) return false;
            l_jBase.merge_patch(l_jPart);
        }
        l_jJ.erase(toms::field::g_pszInclude);
    }
    l_jBase.merge_patch(l_jJ);
    l_jOut = std::move(l_jBase);
    return true;
}

std::vector<std::string> f_Strings(const nlohmann::json& l_jJ, const char* l_pszKey) {
    std::vector<std::string> l_vOut;
    if (!l_jJ.contains(l_pszKey)) return l_vOut;
    const nlohmann::json& l_jV = l_jJ[l_pszKey];
    if (l_jV.is_string()) l_vOut.push_back(l_jV.get<std::string>());
    else if (l_jV.is_array()) for (const auto& l_jS : l_jV) if (l_jS.is_string()) l_vOut.push_back(l_jS.get<std::string>());
    return l_vOut;
}

int f_MenuTabIndex(const std::string& l_strName) {
    static const char* l_arrTabs[] = {"status", "gear", "items", "events", "cards", "system"};   // Game::MenuTab order
    for (int l_iI = 0; l_iI < 6; ++l_iI) if (l_strName == l_arrTabs[l_iI]) return l_iI;
    return -1;
}

} // namespace

bool Game::f_StartScenario(const std::string& l_strPathIn, std::string& l_strError) {
    fs::path l_oFile = l_strPathIn;
    std::error_code l_oEc;
    if (fs::is_directory(l_oFile, l_oEc)) l_oFile /= "scenario.json";
    nlohmann::json l_jSc;
    if (!f_ReadScenarioJson(l_oFile, l_jSc, l_strError)) return false;
    return f_StartScenarioJson(l_jSc, l_oFile.parent_path().filename().string(), l_strError);
}

bool Game::f_EnemyFromData(const std::string& l_strId, const nlohmann::json& l_jB, EnemyInst& l_oEn) const {
    auto l_itT = m_mapEnemyTpl.find(l_strId);
    if (l_itT == m_mapEnemyTpl.end()) return false;
    l_oEn = EnemyInst{};
    l_oEn.m_strId = l_strId; l_oEn.m_strName = m_oLocale.f_Field(l_itT->second[toms::field::g_pszName]);
    l_oEn.m_iHp = l_jB.value(toms::field::g_pszHp, l_itT->second.value(toms::field::g_pszHp, 1)); l_oEn.m_iAtk = l_jB.value(toms::field::g_pszAtk, l_itT->second.value(toms::field::g_pszAtk, 0));
    l_oEn.m_iDef = l_jB.value(toms::field::g_pszDef, l_itT->second.value(toms::field::g_pszDef, 0));
    l_oEn.m_iExp = l_itT->second.value(toms::field::g_pszExp, 0); l_oEn.m_iGold = l_itT->second.value(toms::field::g_pszGold, 0);
    l_oEn.m_iWeaponExp = std::max(0, l_itT->second.value(toms::field::g_pszWeaponExp, 1));
    l_oEn.m_iX = l_oEn.m_iY = -1;   // not on the map: winning removes nothing
    l_oEn.m_bBoss = l_itT->second.value(toms::field::g_pszBoss, false);
    l_oEn.m_iAtkIntervalMs = l_jB.value(toms::field::g_pszAtkIntervalMs, l_itT->second.value(toms::field::g_pszAtkIntervalMs, 4000));
    // the battle's rules for it (docs/gameplay/13), each overridable by the battle setup
    const nlohmann::json& l_jTp = l_itT->second;
    l_oEn.m_strMapId = l_strId;
    {
        const nlohmann::json l_jGrid = l_jB.value(toms::field::g_pszCellGrid, l_jTp.value(toms::field::g_pszCellGrid, nlohmann::json()));
        if (l_jGrid.is_array() && l_jGrid.size() == 2 && l_jGrid[0].is_number_integer() && l_jGrid[1].is_number_integer()) {
            l_oEn.m_iCellCols = std::clamp(l_jGrid[0].get<int>(), 1, toms::g_iCellMaxCount);
            l_oEn.m_iCellRows = std::clamp(l_jGrid[1].get<int>(), 1, toms::g_iCellMaxCount / l_oEn.m_iCellCols);
        }
    }
    l_oEn.m_strLook = l_jB.value(toms::field::g_pszLook, l_jTp.value(toms::field::g_pszLook, std::string("monster")));
    l_oEn.m_vMoves = toms::f_MoveChoicesFromJson(l_jB.contains(toms::field::g_pszMoves) ? l_jB[toms::field::g_pszMoves] : l_jTp.value(toms::field::g_pszMoves, nlohmann::json::array()));
    l_oEn.m_iBreakPoints = l_jB.value(toms::field::g_pszBreak, l_jTp.value(toms::field::g_pszBreak, 0));
    l_oEn.m_jMp = l_jB.contains(toms::field::g_pszMp) ? l_jB[toms::field::g_pszMp] : l_jTp.value(toms::field::g_pszMp, nlohmann::json());
    l_oEn.m_jEvents = l_jB.contains(toms::field::g_pszEvents) ? l_jB[toms::field::g_pszEvents] : l_jTp.value(toms::field::g_pszEvents, nlohmann::json::array());
    return true;
}

void Game::f_ArenaNextFight() {
    if (m_oArena.m_vEnemies.empty()) { m_oArena.m_bOn = false; return; }
    EnemyInst l_oEn;
    const std::string l_strId = m_oArena.m_vEnemies[m_oArena.m_nNext % m_oArena.m_vEnemies.size()];
    m_oArena.m_nNext++;
    if (!f_EnemyFromData(l_strId, m_oArena.m_jOverrides, l_oEn)) { m_oArena.m_bOn = false; return; }
    m_oPl.m_iHp = f_EffectiveMaxHp();   // every fight starts at full health
    m_oArena.m_iFights++;
    f_StartCombat(l_oEn);
    m_oCs.m_strLog = f_TrParam(f_TrParam(f_TrParam(m_oLocale.f_Tr(toms::text::g_pszArenaSummary), toms::field::g_pszN, std::to_string(m_oArena.m_iFights)),
                             "w", std::to_string(m_oArena.m_iWins)), "l", std::to_string(m_oArena.m_iLosses));
}

bool Game::f_ArenaEndFight(bool l_bWon) {
    if (!m_oArena.m_bOn) return false;
    m_oCs.m_bActive = false;
    m_oCs.m_bWon = l_bWon;   // a win waits for the player's tap; a loss shows briefly
    if (l_bWon) { m_oArena.m_iWins++; m_oCs.m_strLog = m_oLocale.f_Tr(toms::text::g_pszArenaWon); }
    else { m_oArena.m_iLosses++; m_oCs.m_strLog = m_oLocale.f_Tr(toms::text::g_pszArenaLost); m_oCs.m_iResultPauseMs = 1200; }
    if (l_bWon && m_oArena.m_bTraining) {   // the training ground (docs/gameplay/16 §5): more EXP and weapon EXP
        const nlohmann::json l_jTr = m_jBattleJson.value(toms::field::g_pszTraining, nlohmann::json::object());
        int l_iExp = 0;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_iExp += l_oF.m_oEnemy.m_iExp;
        l_iExp = (int)std::lround(l_iExp * std::max(0.0f, l_jTr.value(toms::field::g_pszExp, 2.0f)));
        int l_iUses = 0;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_iUses += l_oF.m_oEnemy.m_iWeaponExp;
        l_iUses = (int)std::lround(l_iUses * std::max(0.0f, l_jTr.value(toms::field::g_pszWeaponExp, 2.0f)));
        f_GainExp(l_iExp);
        m_mapWeaponExp[f_WeaponKey()] += l_iUses;
        m_oArena.m_iExpGained += l_iExp;
        m_oArena.m_iWeaponExpGained += l_iUses;
        m_oCs.m_strLog = f_TrParam(f_TrParam(m_oLocale.f_Tr(toms::text::g_pszTrainingWon), toms::field::g_pszExp, std::to_string(l_iExp)), toms::field::g_pszUses, std::to_string(l_iUses));
        f_MarkProgressDirty();
    }
    return true;
}

bool Game::f_StartScenarioJson(const nlohmann::json& l_jSc, const std::string& l_strDefaultName, std::string& l_strError) {
    m_strScenarioName = l_jSc.value(toms::field::g_pszName, l_strDefaultName);
    m_oArena = Arena{};
    m_strScenarioScene = l_jSc.value(toms::field::g_pszScene, std::string("stage"));
    m_jScenarioExpect = l_jSc.value(toms::field::g_pszExpect, nlohmann::json::object());
    // the attack's play (touch_play.h): scenarios play the track unless their battle sets "play" ("weapon": the weapon's)
    m_strPlayOverride = l_jSc.value(toms::field::g_pszBattle, nlohmann::json::object()).value(toms::field::g_pszPlay, std::string("track"));
    m_strUltimateOverride = l_jSc.value(toms::field::g_pszBattle, nlohmann::json::object()).value(toms::field::g_pszUltimate, std::string());
    m_fScenarioViewScale = std::clamp(l_jSc.value(toms::field::g_pszViewScale, 1.0f), 0.5f, 4.0f);   // the walking view (docs/gameplay/24)
    m_fScenarioBattleSpeed = std::max(0.1f, l_jSc.value(toms::field::g_pszBattle, nlohmann::json::object()).value(toms::field::g_pszSpeed, 1.0f));
    if (l_jSc.value(toms::field::g_pszBattle3d, false)) m_bBattle3DAllowed = true;   // the battle on its 3D stage (docs/gameplay/21), as in normal play
    const nlohmann::json l_jTest = l_jSc.value(toms::field::g_pszTest, nlohmann::json::object());
    m_iScenarioTestFrames = l_jTest.value(toms::field::g_pszFrames, 0);
    m_strScenarioTestKeys = l_jTest.value(toms::field::g_pszKeys, std::string());
    m_strScenarioTestDrags = l_jTest.value(toms::field::g_pszDrags, std::string());     // "x0:y0:x1:y1:frames@frame,..." (design px), like --drag
    m_strScenarioTestClicks = l_jTest.value(toms::field::g_pszClicks, std::string());   // "x:y@frame,..." (design px), like --clicks
    auto l_fnBad = [&](const std::string& l_strWhat) { l_strError = m_strScenarioName + ": " + l_strWhat; return false; };

    // 1. The run: from a save file in the scenario's folder, or a new game. Neither is ever written.
    if (m_strScenarioScene == "title") return true;   // the title as the game starts: nothing to set up
    if (l_jSc.contains(toms::field::g_pszSave)) {
        toms::MetaSaveData l_oM;
        toms::RunSaveData l_oR;
        int l_iPlaySec = 0;
        const fs::path l_oSave = l_jSc[toms::field::g_pszSave].get<std::string>();   // absolute (readScenarioJson)
        if (!toms::f_ReadSaveFile(l_oSave.string(), l_oM, l_oR, &l_iPlaySec)) return l_fnBad("cannot read the save " + l_oSave.string());
        f_ApplyLoadedRun(l_oM, l_oR, /*slot=*/0, l_iPlaySec);
        m_bSlotReadOnly = true;
    } else {
        f_NewGame(-1);
    }

    // 2. The floor: a floor id (F04), a stage id, or a stage file next to the scenario.
    if (l_jSc.contains(toms::field::g_pszStage)) {
        f_LoadStage(l_jSc[toms::field::g_pszStage].get<std::string>());   // a stage file's path is already absolute (readScenarioJson)
    }

    // 3. The story so far: stair locks, finished events, flags.
    if (l_jSc.contains(toms::field::g_pszStairLocks))
        for (const auto& l_jL : l_jSc[toms::field::g_pszStairLocks]) {   // "event": one, or "events": several (W38: all must be done)
            for (const std::string& l_strE : f_Strings(l_jL, toms::field::g_pszEvent)) f_DebugLockStairs(l_jL.value(toms::field::g_pszFloor, std::string()), l_strE);
            for (const std::string& l_strE : f_Strings(l_jL, toms::field::g_pszEvents)) f_DebugLockStairs(l_jL.value(toms::field::g_pszFloor, std::string()), l_strE);
        }
    for (const std::string& l_strF : f_Strings(l_jSc, toms::field::g_pszFlags)) f_DebugSetFlag(l_strF);
    for (const std::string& l_strE : f_Strings(l_jSc, toms::field::g_pszEventsDone)) f_DebugFinishEvent(l_strE);
    if (l_jSc.contains(toms::field::g_pszVars) && l_jSc[toms::field::g_pszVars].is_object())   // event variables: { "ledger_pages": 2 } (docs/gameplay/23)
        for (auto l_oIt = l_jSc[toms::field::g_pszVars].begin(); l_oIt != l_jSc[toms::field::g_pszVars].end(); ++l_oIt)
            if (l_oIt.value().is_number_integer()) m_oRun.f_SetVar(l_oIt.key(), l_oIt.value().get<int>());
    if (l_jSc.contains(toms::field::g_pszCounters) && l_jSc[toms::field::g_pszCounters].is_object())   // the story's counters: { "mercy": 4 } (docs/gameplay/23 §6)
        for (auto l_oIt = l_jSc[toms::field::g_pszCounters].begin(); l_oIt != l_jSc[toms::field::g_pszCounters].end(); ++l_oIt)
            if (l_oIt.value().is_number_integer()) m_oRun.f_SetCounter(l_oIt.key(), l_oIt.value().get<int>());
    if (l_jSc.contains(toms::field::g_pszMissions) && l_jSc[toms::field::g_pszMissions].is_object())   // quests and their state: { "m_c3_ledger": "active" }
        for (auto l_oIt = l_jSc[toms::field::g_pszMissions].begin(); l_oIt != l_jSc[toms::field::g_pszMissions].end(); ++l_oIt) {
            if (!l_oIt.value().is_string()) continue;
            toms::MissionTracker& l_oT = m_mapMissionTrackers[l_oIt.key()];
            l_oT.m_strMissionId = l_oIt.key();
            l_oT.m_eState = toms::f_MissionStateFromString(l_oIt.value().get<std::string>());
        }
    if (l_jSc.contains(toms::field::g_pszCards) && l_jSc[toms::field::g_pszCards].is_object())   // monster cards held: { "slime": 2 } (the training ground, 16 §5)
        for (auto l_oIt = l_jSc[toms::field::g_pszCards].begin(); l_oIt != l_jSc[toms::field::g_pszCards].end(); ++l_oIt)
            if (l_oIt.value().is_number_integer()) m_oMeta.m_mapCards[l_oIt.key()].m_iCopies = std::max(0, l_oIt.value().get<int>());

    // 4. The player: items and gear first (gear can change the stats), then the numbers as given. A new game starts with
    // the starting weapon (the bow) equipped; "unarmed": true takes it off.
    const nlohmann::json l_jP = l_jSc.value(toms::field::g_pszPlayer, nlohmann::json::object());
    if (l_jP.value(toms::field::g_pszUnarmed, false)) m_oEquipped.m_strWeaponId.clear();
    for (const std::string& l_strId : f_Strings(l_jP, toms::field::g_pszGive)) f_DebugGive(l_strId);
    for (const std::string& l_strId : f_Strings(l_jP, toms::field::g_pszEquip))
        if (!f_DebugEquip(l_strId)) return l_fnBad("no equipment \"" + l_strId + "\" in data/items.json");
    if (l_jP.contains(toms::field::g_pszX)) m_oPl.m_iX = l_jP[toms::field::g_pszX].get<int>();
    if (l_jP.contains(toms::field::g_pszY)) m_oPl.m_iY = l_jP[toms::field::g_pszY].get<int>();
    if (l_jP.contains(toms::field::g_pszMaxhp)) m_oPl.m_iMaxhp = l_jP[toms::field::g_pszMaxhp].get<int>();
    if (l_jP.contains(toms::field::g_pszHp)) m_oPl.m_iHp = l_jP[toms::field::g_pszHp].get<int>();
    if (l_jP.contains(toms::field::g_pszAtk)) m_oPl.m_iAtk = l_jP[toms::field::g_pszAtk].get<int>();
    if (l_jP.contains(toms::field::g_pszDef)) m_oPl.m_iDef = l_jP[toms::field::g_pszDef].get<int>();
    if (l_jP.contains(toms::field::g_pszGold)) m_oPl.m_iGold = l_jP[toms::field::g_pszGold].get<int>();
    if (l_jP.contains(toms::field::g_pszExp)) m_oPl.m_iExp = l_jP[toms::field::g_pszExp].get<int>();
    if (l_jP.contains(toms::field::g_pszLv)) m_oPl.m_iLv = l_jP[toms::field::g_pszLv].get<int>();
    if (l_jP.contains(toms::field::g_pszAttrPoints)) m_oPl.m_iAttrPoints = std::max(0, l_jP[toms::field::g_pszAttrPoints].get<int>());   // to place on the Status tab
    if (l_jP.contains(toms::field::g_pszKeys)) {
        const nlohmann::json& l_jK = l_jP[toms::field::g_pszKeys];
        m_oPl.m_iKeyYellow = l_jK.value(toms::field::g_pszYellow, m_oPl.m_iKeyYellow);
        m_oPl.m_iKeyBlue = l_jK.value(toms::field::g_pszBlue, m_oPl.m_iKeyBlue);
        m_oPl.m_iKeyRed = l_jK.value(toms::field::g_pszRed, m_oPl.m_iKeyRed);
    }
    if (l_jP.contains(toms::field::g_pszAttrs))
        for (auto l_oIt = l_jP[toms::field::g_pszAttrs].begin(); l_oIt != l_jP[toms::field::g_pszAttrs].end(); ++l_oIt) m_oPl.m_mapAttrs[l_oIt.key()] = l_oIt.value().get<int>();
    if (l_jP.contains(toms::field::g_pszInventory)) m_oPl.m_vInv = f_Strings(l_jP, toms::field::g_pszInventory);
    if (l_jP.contains(toms::field::g_pszWeaponExp) && l_jP[toms::field::g_pszWeaponExp].is_object())   // each weapon's EXP (docs/gameplay/17 §19)
        for (auto l_oIt = l_jP[toms::field::g_pszWeaponExp].begin(); l_oIt != l_jP[toms::field::g_pszWeaponExp].end(); ++l_oIt) m_mapWeaponExp[l_oIt.key()] = l_oIt.value().get<int>();
    if (l_jP.contains(toms::field::g_pszWeaponLevel) && l_jP[toms::field::g_pszWeaponLevel].is_object())   // or its level: the EXP that reaches it
        for (auto l_oIt = l_jP[toms::field::g_pszWeaponLevel].begin(); l_oIt != l_jP[toms::field::g_pszWeaponLevel].end(); ++l_oIt)
            m_mapWeaponExp[l_oIt.key()] = toms::f_WeaponExpForLevel(f_WeaponPlayOf(l_oIt.key()), l_oIt.value().get<int>());

    // 5. The scene.
    const std::string& l_strS = m_strScenarioScene;
    if (l_strS == "stage") {
        // the walking scene as it is now
    } else if (l_strS == "battle" || l_strS == "battleWon") {
        const nlohmann::json l_jB = l_jSc.value(toms::field::g_pszBattle, nlohmann::json::object());
        const std::string l_strId = l_jB.value(toms::field::g_pszEnemy, std::string());
        EnemyInst l_oEn;
        if (!f_EnemyFromData(l_strId, l_jB, l_oEn)) return l_fnBad("no enemy \"" + l_strId + "\" in data/enemies.json");
        // "party": the monsters beside it (ids, or {"enemy": id, ...overrides}; docs/gameplay/13 §4.6)
        std::vector<EnemyInst> l_vParty;
        for (const auto& l_jM : l_jB.value(toms::field::g_pszParty, nlohmann::json::array())) {
            const nlohmann::json l_jO = l_jM.is_object() ? l_jM : nlohmann::json::object();
            const std::string l_strMid = l_jM.is_string() ? l_jM.get<std::string>() : l_jO.value(toms::field::g_pszEnemy, std::string());
            EnemyInst l_oPm;
            if (!f_EnemyFromData(l_strMid, l_jO, l_oPm)) return l_fnBad("no enemy \"" + l_strMid + "\" in data/enemies.json");
            l_vParty.push_back(l_oPm);
        }
        m_strPaceOverride = l_jB.value(toms::field::g_pszPace, std::string());   // "cooldown" / "stamina" (docs/gameplay/13 §6.3)
        {   // "rules": data/battle.json's values for this battle ({"stamina": {...}, "break": {...}}), as the battle editor tries them
            nlohmann::json l_jRules = m_jBattleJson;
            if (l_jB.contains(toms::field::g_pszRules) && l_jB[toms::field::g_pszRules].is_object()) l_jRules.merge_patch(l_jB[toms::field::g_pszRules]);
            m_oPaceSettings = toms::f_PaceFromJson(l_jRules);
            m_oBreakSettings = toms::f_BreakFromJson(l_jRules);
            m_oFeel = toms::f_FeelFromJson(l_jRules);
            m_oCellRules = toms::f_CellRulesFromJson(l_jRules);
            m_oMonsterMp = toms::f_MonsterMpFromJson(l_jRules);
            m_oTouchSettings = toms::f_TouchPlayFromJson(l_jRules);
            m_jFeelFx = l_jRules.value(toms::field::g_pszFeelFx, nlohmann::json::object());   // the attack feel (docs/gameplay/20)
            m_jBodyScale = l_jRules.value(toms::field::g_pszBodyScale, nlohmann::json::object());
            m_jOnFoeDeath = l_jRules.value(toms::field::g_pszOnFoeDeath, nlohmann::json::object());
            m_oGrade = toms::f_GradeFromJson(l_jRules);
            m_oGuard = toms::f_GuardFromJson(l_jRules);
            m_bComboHoldsMonsters = l_jRules.value(toms::field::g_pszComboHoldsMonsters, false);
            m_bComboRunsThroughAttacks = l_jRules.value(toms::field::g_pszComboRunsThroughAttacks, true);
        }
        // a boss's sequence (docs/gameplay/25): "gauntlet": its guards, fought one at a time before it
        std::vector<EnemyInst> l_vGuards;
        for (const std::string& l_strG : f_Strings(l_jB, toms::field::g_pszGauntlet)) {
            EnemyInst l_oG;
            if (f_EnemyFromData(l_strG, l_jB.value(toms::field::g_pszGuards, nlohmann::json::object()), l_oG)) l_vGuards.push_back(l_oG);
        }
        if (!l_vGuards.empty()) f_StartGauntlet(l_oEn, l_vGuards);
        else { f_EndGauntlet(); f_StartCombat(l_oEn, l_vParty); }
        if (l_jB.value(toms::field::g_pszSuper, false)) m_oCs.m_iSuperCharge = m_oRun.f_SuperMax();   // the ultimate ready (docs/gameplay/16 §6)
        if (l_strS == "battleWon") f_FinishCombatWin();
    } else if (l_strS == "arena") {
        // the weapon test arena: "battle": { "enemies": [...] } (or "enemy"), fought in turn; the weapon is the
        // player's ("player": { "equip": [...] }; the starting bow unless "unarmed": true)
        const nlohmann::json l_jB = l_jSc.value(toms::field::g_pszBattle, nlohmann::json::object());
        m_oArena.m_vEnemies = f_Strings(l_jB, toms::field::g_pszEnemies);
        if (m_oArena.m_vEnemies.empty()) m_oArena.m_vEnemies = f_Strings(l_jB, toms::field::g_pszEnemy);
        if (m_oArena.m_vEnemies.empty()) return l_fnBad("\"battle\": { \"enemies\": [...] } is missing");
        for (const std::string& l_strId : m_oArena.m_vEnemies) {
            EnemyInst l_oProbe;
            if (!f_EnemyFromData(l_strId, l_jB, l_oProbe)) return l_fnBad("no enemy \"" + l_strId + "\" in data/enemies.json");
        }
        m_oArena.m_jOverrides = l_jB;
        m_oArena.m_bOn = true;
        f_ArenaNextFight();
    } else if (l_strS == "miniGame") {   // a reward event (docs/gameplay/07 phase 6): "miniGame": "<id in data/minigames.json>"
        f_OpenMiniGame(l_jSc.value(toms::field::g_pszMiniGame, std::string()));
        if (!m_oMiniGame.m_bOpen) return l_fnBad("no mini-game \"" + l_jSc.value(toms::field::g_pszMiniGame, std::string()) + "\" (data/minigames.json)");
    } else if (l_strS == "weaponTrial") {   // the start story (docs/gameplay/07 phase 3): the weapon master's dialogue
        if (l_jSc.value(toms::field::g_pszBattle, nlohmann::json::object()).contains(toms::field::g_pszHp)) m_oTutorial.m_jOverrides[toms::field::g_pszHp] = l_jSc[toms::field::g_pszBattle][toms::field::g_pszHp];   // a short tutorial fight
        f_StartWeaponTrial();
        // "tryWeapon": past the prologue and the weapon master, straight into the trial fight with that weapon
        const std::string l_strTry = l_jSc.value(toms::field::g_pszBattle, nlohmann::json::object()).value(toms::field::g_pszTryWeapon, std::string());
        if (!l_strTry.empty()) f_TryWeapon(l_strTry);
    } else if (l_strS == "dialogue") {
        const nlohmann::json l_jD = l_jSc.value(toms::field::g_pszDialogue, nlohmann::json::object());
        if (l_jD.contains(toms::field::g_pszFile)) {
            const fs::path l_oF = l_jD[toms::field::g_pszFile].get<std::string>();   // absolute (readScenarioJson)
            nlohmann::json l_jData = toms::f_ReadJsonFileSafe(l_oF.string());
            if (!l_jData.is_object()) return l_fnBad("cannot read the dialogue " + l_oF.string());
            m_strDlgNpc = l_jD.value(toms::field::g_pszId, l_oF.stem().string());
            m_strDlgSpeaker.clear(); m_strDlgTalkNpc.clear();
            f_OpenDialogue(l_jData, DlgMode::Plain);
        } else {
            const std::string l_strId = l_jD.value(toms::field::g_pszId, std::string());
            if (!f_ReadDialogue(l_strId).is_object()) return l_fnBad("no dialogue \"" + l_strId + "\" in data/dialogue/");
            f_StartDialogue(l_strId);
        }
        if (l_jD.contains(toms::field::g_pszNode)) f_EnterNode(l_jD[toms::field::g_pszNode].get<std::string>());
    } else if (l_strS == toms::scene::g_pszTalk) {
        const std::string l_strNpc = l_jSc.value(toms::field::g_pszTalk, nlohmann::json::object()).value(toms::field::g_pszNpc, std::string());
        if (l_strNpc.empty()) return l_fnBad("\"talk\": { \"npc\": ... } is missing");
        f_TalkTo(l_strNpc);
    } else if (l_strS == "store") {
        m_bStoreUnlocked = true;
        m_bStoreUnlockDlg = false;
        f_OpenStore();
    } else if (l_strS == "menu") {
        const std::string l_strTab = l_jSc.value(toms::field::g_pszMenu, nlohmann::json::object()).value(toms::field::g_pszTab, std::string("status"));
        const int l_iI = f_MenuTabIndex(l_strTab);
        if (l_iI < 0) return l_fnBad("menu tab \"" + l_strTab + "\" (status, gear, items, events, cards, system)");
        f_OpenPlayerMenu(l_iI);
    } else if (l_strS == "saveLoad") {
        const std::string l_strMode = l_jSc.value(toms::field::g_pszSaveLoad, nlohmann::json::object()).value(toms::field::g_pszMode, std::string("load"));
        f_OpenPlayerMenu((int)MenuTab::System);
        f_OpenSaveLoad(l_strMode == "save" ? 0 : 1);
    } else if (l_strS == "stageSelect") {
        f_OpenStageSelect();
    } else if (l_strS == "stairsConfirm") {
        const nlohmann::json l_jC = l_jSc.value(toms::field::g_pszStairs, nlohmann::json::object());
        f_RequestStageTransition(l_jC.value(toms::field::g_pszTo, std::string("F02")), l_jC.value(toms::field::g_pszUp, true));
    } else if (l_strS == "notice") {
        // the notice comes from the setup (a stair lock and its event finished); nothing more to open
    } else if (l_strS == "ending") {
        const std::string l_strId = l_jSc.value(toms::field::g_pszEnding, nlohmann::json::object()).value(toms::field::g_pszId, std::string());
        if (l_strId.empty()) return l_fnBad("\"ending\": { \"id\": ... } is missing");
        f_TriggerEnding(l_strId);
    } else if (l_strS == "hub") {
        const std::string l_strLoc = l_jSc.value(toms::field::g_pszHub, nlohmann::json::object()).value(toms::field::g_pszLocation, std::string());
        if (!f_ActivateHubLocation(l_strLoc)) return l_fnBad("hub location \"" + l_strLoc + "\" is unknown or locked");
    } else {
        return l_fnBad("unknown scene \"" + l_strS + "\" (docs/gameplay/08_TEST_SCENARIOS.md lists them)");
    }
    std::fprintf(stderr, "[scenario] %s: scene %s started\n", m_strScenarioName.c_str(), l_strS.c_str());
    return true;
}

bool Game::f_ScenarioCheck(std::string& l_strReport) const {
    const std::string l_strScene = m_jScenarioExpect.value(toms::field::g_pszScene, m_strScenarioScene);
    bool l_bOk = true;
    std::string l_strOut;
    auto l_fnFail = [&](const std::string& l_strWhy) { l_bOk = false; l_strOut += "\n  " + l_strWhy; };
    bool l_bShowing = false;
    if (l_strScene == "title") l_bShowing = m_oTitle.f_IsOpen();
    else if (l_strScene == "stage") l_bShowing = !m_oTitle.f_IsOpen() && !f_ModalActive();
    else if (l_strScene == "battle") l_bShowing = m_oCs.m_bActive;
    else if (l_strScene == "battleWon") l_bShowing = m_oCs.m_bWon;
    else if (l_strScene == "arena") l_bShowing = m_oArena.m_bOn && (m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0);
    else if (l_strScene == "dialogue" || l_strScene == toms::scene::g_pszTalk || l_strScene == "weaponTrial") l_bShowing = m_bInDialogue;
    else if (l_strScene == "miniGame") l_bShowing = m_oMiniGame.m_bOpen;
    else if (l_strScene == "store") l_bShowing = m_bStoreOpen;
    else if (l_strScene == "menu") l_bShowing = m_bInGameMenuOpen;
    else if (l_strScene == "saveLoad") l_bShowing = m_bSaveLoadOpen;
    else if (l_strScene == "stageSelect") l_bShowing = m_bStageSelectOpen;
    else if (l_strScene == "stairsConfirm") l_bShowing = m_bStairsConfirmOpen;
    else if (l_strScene == "notice") l_bShowing = !m_vNotices.empty();
    else if (l_strScene == "ending") l_bShowing = f_EndingActive();
    else if (l_strScene == "hub") l_bShowing = m_bInDialogue;
    if (!l_bShowing) l_fnFail("the scene \"" + l_strScene + "\" is not showing");
    if (m_jScenarioExpect.contains(toms::field::g_pszMenuTab)) {
        const int l_iWant = f_MenuTabIndex(m_jScenarioExpect[toms::field::g_pszMenuTab].get<std::string>());
        if ((int)m_eMenuTab != l_iWant) l_fnFail("the menu is not on tab " + m_jScenarioExpect[toms::field::g_pszMenuTab].get<std::string>());
    }
    auto l_fnNum = [&](const char* l_pszKey, int l_iHave) {
        if (m_jScenarioExpect.contains(l_pszKey) && m_jScenarioExpect[l_pszKey].get<int>() != l_iHave)
            l_fnFail(std::string(l_pszKey) + " is " + std::to_string(l_iHave) + ", expected " + std::to_string(m_jScenarioExpect[l_pszKey].get<int>()));
    };
    l_fnNum("gold", m_oPl.m_iGold); l_fnNum("hp", m_oPl.m_iHp); l_fnNum("lv", m_oPl.m_iLv); l_fnNum("crystals", m_oPl.m_iCrystals);
    l_fnNum("arenaFights", m_oArena.m_iFights); l_fnNum("arenaWins", m_oArena.m_iWins);
    if (m_jScenarioExpect.contains(toms::field::g_pszStage) && m_strCurStage != m_jScenarioExpect[toms::field::g_pszStage].get<std::string>())
        l_fnFail("the floor is " + m_strCurStage + ", expected " + m_jScenarioExpect[toms::field::g_pszStage].get<std::string>());
    for (const std::string& l_strId : f_Strings(m_jScenarioExpect, "inventoryHas"))
        if (std::find(m_oPl.m_vInv.begin(), m_oPl.m_vInv.end(), l_strId) == m_oPl.m_vInv.end()) l_fnFail("the inventory has no " + l_strId);
    if (m_jScenarioExpect.contains(toms::field::g_pszMissions) && m_jScenarioExpect[toms::field::g_pszMissions].is_object())   // quest id -> its state (docs/gameplay/23 §7)
        for (auto l_oIt = m_jScenarioExpect[toms::field::g_pszMissions].begin(); l_oIt != m_jScenarioExpect[toms::field::g_pszMissions].end(); ++l_oIt) {
            auto l_itT = m_mapMissionTrackers.find(l_oIt.key());
            const std::string l_strHave = l_itT == m_mapMissionTrackers.end() ? std::string("none") : std::string(toms::f_ToString(l_itT->second.m_eState));
            if (l_oIt.value().is_string() && l_strHave != l_oIt.value().get<std::string>())
                l_fnFail("the quest " + l_oIt.key() + " is " + l_strHave + ", expected " + l_oIt.value().get<std::string>());
        }
    for (const std::string& l_strF : f_Strings(m_jScenarioExpect, "flags"))
        if (!m_oRun.f_Flag(l_strF)) l_fnFail("the flag " + l_strF + " is not set");
    for (const std::string& l_strF : f_Strings(m_jScenarioExpect, "notFlags"))   // a flag that must stay unset (a locked story step)
        if (m_oRun.f_Flag(l_strF)) l_fnFail("the flag " + l_strF + " is set");
    if (m_jScenarioExpect.contains(toms::field::g_pszCards) && m_jScenarioExpect[toms::field::g_pszCards].is_object())   // monster id -> copies held
        for (auto l_oIt = m_jScenarioExpect[toms::field::g_pszCards].begin(); l_oIt != m_jScenarioExpect[toms::field::g_pszCards].end(); ++l_oIt) {
            auto l_itR = m_oMeta.m_mapCards.find(l_oIt.key());
            const int l_iHave = l_itR == m_oMeta.m_mapCards.end() ? 0 : l_itR->second.m_iCopies;
            if (l_iHave != l_oIt.value().get<int>())
                l_fnFail("cards of " + l_oIt.key() + ": " + std::to_string(l_iHave) + ", expected " + std::to_string(l_oIt.value().get<int>()));
        }
    if (m_jScenarioExpect.contains(toms::field::g_pszStairUpLocked)) {   // the current floor's first stair up (W38 tests)
        const toms::StairLink* l_pUp = nullptr;
        for (const toms::StairLink& l_oL : m_oSt.m_vStairs) if (l_oL.m_bUp) { l_pUp = &l_oL; break; }
        const bool l_bWant = m_jScenarioExpect[toms::field::g_pszStairUpLocked].get<bool>();
        if (!l_pUp) l_fnFail("the floor has no stair up");
        else if (f_StairLocked(*l_pUp) != l_bWant) l_fnFail(std::string("the stair up is ") + (l_bWant ? "open, expected locked" : "locked, expected open"));
        if (l_pUp && m_jScenarioExpect.contains(toms::field::g_pszStairUpMarked) &&
            f_StairJustOpened(m_oSt.m_strId, *l_pUp) != m_jScenarioExpect[toms::field::g_pszStairUpMarked].get<bool>())
            l_fnFail(std::string("the stair up is ") + (m_jScenarioExpect[toms::field::g_pszStairUpMarked].get<bool>() ? "not marked" : "marked") + " with the arrow");
    }
    if (m_jScenarioExpect.contains(toms::field::g_pszEquipped) && m_oEquipped.m_strWeaponId != m_jScenarioExpect[toms::field::g_pszEquipped].get<std::string>())
        l_fnFail("the worn weapon is \"" + m_oEquipped.m_strWeaponId + "\", expected \"" + m_jScenarioExpect[toms::field::g_pszEquipped].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains(toms::field::g_pszGearOwned)) {   // exactly these, in any order
        std::vector<std::string> l_vWant = m_jScenarioExpect[toms::field::g_pszGearOwned].get<std::vector<std::string>>(), l_vHave = m_vGearOwned;
        std::sort(l_vWant.begin(), l_vWant.end());
        std::sort(l_vHave.begin(), l_vHave.end());
        if (l_vWant != l_vHave) l_fnFail("the owned gear is not what was expected");
    }
    if (m_jScenarioExpect.contains(toms::field::g_pszMusic)) {   // the key of the music playing; "": none
        const std::string l_strWant = m_jScenarioExpect[toms::field::g_pszMusic].get<std::string>();
        if (m_oAudio.f_MusicKey() != l_strWant) l_fnFail("the music is \"" + m_oAudio.f_MusicKey() + "\", expected \"" + l_strWant + "\"");
        else if (!l_strWant.empty() && m_oAudio.f_Available() && !m_oAudio.f_MusicPlaying()) l_fnFail("the music \"" + l_strWant + "\" is not really playing");
    }
    if (m_jScenarioExpect.contains(toms::field::g_pszCellGrid) && (m_oCs.m_iCellCols != m_jScenarioExpect[toms::field::g_pszCellGrid][0].get<int>() || m_oCs.m_iCellRows != m_jScenarioExpect[toms::field::g_pszCellGrid][1].get<int>()))
        l_fnFail("the grid is " + std::to_string(m_oCs.m_iCellCols) + "x" + std::to_string(m_oCs.m_iCellRows));
    if (m_jScenarioExpect.contains(toms::field::g_pszHeroCell) && m_oCs.m_iHeroCell != m_jScenarioExpect[toms::field::g_pszHeroCell].get<int>())
        l_fnFail("the hero is on cell " + std::to_string(m_oCs.m_iHeroCell) + ", expected " + std::to_string(m_jScenarioExpect[toms::field::g_pszHeroCell].get<int>()));
    if (m_jScenarioExpect.contains(toms::field::g_pszTutorialStep) && m_oArena.m_iTutorialStep != m_jScenarioExpect[toms::field::g_pszTutorialStep].get<int>())
        l_fnFail("the tutorial is at step " + std::to_string(m_oArena.m_iTutorialStep) + ", expected " + std::to_string(m_jScenarioExpect[toms::field::g_pszTutorialStep].get<int>()));
    if (m_jScenarioExpect.contains(toms::field::g_pszTutorialTextHas)) {   // the tutorial box's words hold this (a text key: its words now)
        const toms::TutorialStep* l_pStep = f_TutorialStepNow();
        const std::string l_strGiven = m_jScenarioExpect[toms::field::g_pszTutorialTextHas].get<std::string>();
        const std::string l_strWant = m_oLocale.f_Has(l_strGiven) ? m_oLocale.f_Tr(l_strGiven) : l_strGiven;
        const std::string l_strText = l_pStep ? f_TutorialText(*l_pStep) : std::string();
        if (l_strText.find(l_strWant) == std::string::npos) l_fnFail("the tutorial says \"" + l_strText + "\", not \"" + l_strWant + "\"");
    }
    if (m_jScenarioExpect.contains(toms::field::g_pszEnding) && m_strActiveEndingId != m_jScenarioExpect[toms::field::g_pszEnding].get<std::string>())   // the ending shown (23 §6)
        l_fnFail("the ending shown is \"" + m_strActiveEndingId + "\", expected " + m_jScenarioExpect[toms::field::g_pszEnding].get<std::string>());
    if (m_jScenarioExpect.contains(toms::field::g_pszDialogueNode) && (!m_bInDialogue || m_strDlgNode != m_jScenarioExpect[toms::field::g_pszDialogueNode].get<std::string>()))
        l_fnFail("the dialogue is not at node \"" + m_jScenarioExpect[toms::field::g_pszDialogueNode].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains(toms::field::g_pszWeaponLevel) && m_jScenarioExpect[toms::field::g_pszWeaponLevel].is_object())   // weapon id -> its level
        for (auto l_oIt = m_jScenarioExpect[toms::field::g_pszWeaponLevel].begin(); l_oIt != m_jScenarioExpect[toms::field::g_pszWeaponLevel].end(); ++l_oIt)
            if (f_WeaponLevel(l_oIt.key()) != l_oIt.value().get<int>())
                l_fnFail("the weapon \"" + l_oIt.key() + "\" is Lv " + std::to_string(f_WeaponLevel(l_oIt.key())) + ", expected " +
                     std::to_string(l_oIt.value().get<int>()));
    if (m_jScenarioExpect.contains(toms::field::g_pszBackdrop) && !(m_spBackdrop && m_strBackdropAnim == m_jScenarioExpect[toms::field::g_pszBackdrop].get<std::string>()))
        l_fnFail("the battle's background is \"" + m_strBackdropAnim + "\", expected " + m_jScenarioExpect[toms::field::g_pszBackdrop].get<std::string>());
    if (m_jScenarioExpect.value(toms::field::g_pszStage3d, false)) {   // the 3D stage is drawn and the fighters stand on it (docs/gameplay/21 §4.3)
        const glm::vec4 l_v4H = f_FighterBox(-1), l_v4M = f_FighterBox(0);
        if (!m_bBattle3DOn) l_fnFail("no 3D stage (battle.json stages3d for this floor, mesh shaders, the files)");
        else if (!(l_v4H.w > l_v4M.w && l_v4H.y + l_v4H.w > l_v4M.y + l_v4M.w))
            l_fnFail("the hero should stand nearer than the monster: hero box " + std::to_string((int)l_v4H.w) + " px tall, feet at " +
                     std::to_string((int)(l_v4H.y + l_v4H.w)) + "; monster " + std::to_string((int)l_v4M.w) + " px, feet at " +
                     std::to_string((int)(l_v4M.y + l_v4M.w)));
    }
    if (m_jScenarioExpect.contains(toms::field::g_pszGauntlet) && m_jScenarioExpect[toms::field::g_pszGauntlet].is_object()) {   // a boss's sequence (docs/gameplay/25)
        const nlohmann::json& l_jG = m_jScenarioExpect[toms::field::g_pszGauntlet];
        const int l_iSteps = m_oGauntlet.m_bOn ? (int)m_oGauntlet.m_vSteps.size() : 0;
        if (l_jG.contains(toms::field::g_pszSteps) && l_iSteps != l_jG[toms::field::g_pszSteps].get<int>())
            l_fnFail("the boss sequence has " + std::to_string(l_iSteps) + " fights, expected " + std::to_string(l_jG[toms::field::g_pszSteps].get<int>()));
        if (l_jG.contains(toms::field::g_pszAt) && (!m_oGauntlet.m_bOn || m_oGauntlet.m_iStep != l_jG[toms::field::g_pszAt].get<int>()))
            l_fnFail("the boss sequence is at fight " + std::to_string(m_oGauntlet.m_iStep) + ", expected " + std::to_string(l_jG[toms::field::g_pszAt].get<int>()));
        if (l_jG.contains(toms::field::g_pszFoe) && (m_oCs.m_vFoes.empty() || m_oCs.m_vFoes[0].m_oEnemy.m_strId != l_jG[toms::field::g_pszFoe].get<std::string>()))
            l_fnFail("the monster fought is \"" + (m_oCs.m_vFoes.empty() ? std::string() : m_oCs.m_vFoes[0].m_oEnemy.m_strId) + "\", expected " + l_jG[toms::field::g_pszFoe].get<std::string>());
        if (l_jG.contains(toms::field::g_pszMoved) && (glm::length(m_v3BattleTravel) > 0.01f) != l_jG[toms::field::g_pszMoved].get<bool>())
            l_fnFail(std::string("the stage ") + (glm::length(m_v3BattleTravel) > 0.01f ? "moved on" : "did not move on") + " to the next monster");
    }
    if (m_jScenarioExpect.value(toms::field::g_pszWalk3d, false)) {   // the floor is drawn in 3D, and a click on the hero finds his tile (docs/gameplay/24)
        if (!m_bWalk3DOn) l_fnFail("the floor is not drawn in 3D (data/stage3d.json, the look's textures, mesh shaders)");
        else {
            const glm::vec4 l_v4C = m_mat4Walk3DViewProj * glm::vec4(m_oPl.m_iX + 0.5f, 0.0f, m_oPl.m_iY + 0.5f, 1.0f);
            const glm::vec2 l_v2Dev((l_v4C.x / l_v4C.w * 0.5f + 0.5f) * m_v2Walk3DDevice.x, (0.5f - l_v4C.y / l_v4C.w * 0.5f) * m_v2Walk3DDevice.y);
            const float l_fPx = (l_v2Dev.x - m_v4Walk3DDesign.x) / m_v4Walk3DDesign.z * (float)m_pRen->f_Width();
            const float l_fPy = (l_v2Dev.y - m_v4Walk3DDesign.y) / m_v4Walk3DDesign.w * (float)m_pRen->f_Height();
            int l_iTx = -1, l_iTy = -1;
            if (!f_ScreenToTile(l_fPx, l_fPy, l_iTx, l_iTy) || l_iTx != m_oPl.m_iX || l_iTy != m_oPl.m_iY)
                l_fnFail("a click on the hero (" + std::to_string((int)l_fPx) + ", " + std::to_string((int)l_fPy) + ") finds tile " +
                         std::to_string(l_iTx) + ", " + std::to_string(l_iTy) + ", not his " + std::to_string(m_oPl.m_iX) + ", " + std::to_string(m_oPl.m_iY));
        }
    }
    if (m_jScenarioExpect.contains(toms::field::g_pszFoeClips) && m_jScenarioExpect[toms::field::g_pszFoeClips].is_array())   // each monster's clip now
        for (size_t l_nI = 0; l_nI < m_jScenarioExpect[toms::field::g_pszFoeClips].size(); l_nI++) {
            const std::string l_strHas = l_nI < m_vFoeRigs.size() ? m_vFoeRigs[l_nI].m_strClip : std::string();
            if (l_strHas != m_jScenarioExpect[toms::field::g_pszFoeClips][l_nI].get<std::string>())
                l_fnFail("monster " + std::to_string(l_nI) + " plays \"" + l_strHas + "\", expected " + m_jScenarioExpect[toms::field::g_pszFoeClips][l_nI].get<std::string>());
        }
    if (m_jScenarioExpect.contains(toms::field::g_pszBattleClip) && m_strBattleRigClip != m_jScenarioExpect[toms::field::g_pszBattleClip].get<std::string>())
        l_fnFail("the battle picture plays the clip \"" + m_strBattleRigClip + "\", expected \"" + m_jScenarioExpect[toms::field::g_pszBattleClip].get<std::string>() + "\"");
    const std::string l_strLeadState = m_vFoeRigs.empty() ? std::string() : m_vFoeRigs[0].m_strState;
    const std::string l_strLeadClip = m_vFoeRigs.empty() ? std::string() : m_vFoeRigs[0].m_strClip;
    if (m_jScenarioExpect.contains(toms::field::g_pszEnemyAnim) && l_strLeadState != m_jScenarioExpect[toms::field::g_pszEnemyAnim].get<std::string>())
        l_fnFail("the monster shows \"" + l_strLeadState + "\", expected \"" + m_jScenarioExpect[toms::field::g_pszEnemyAnim].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains(toms::field::g_pszEnemyClip) && l_strLeadClip != m_jScenarioExpect[toms::field::g_pszEnemyClip].get<std::string>())
        l_fnFail("the monster plays the clip \"" + l_strLeadClip + "\", expected \"" + m_jScenarioExpect[toms::field::g_pszEnemyClip].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains(toms::field::g_pszFoesDown)) {   // how many of the battle's monsters have fallen
        int l_iDown = 0;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_iDown += l_oF.f_Down() ? 1 : 0;
        if (l_iDown != m_jScenarioExpect[toms::field::g_pszFoesDown].get<int>())
            l_fnFail(std::to_string(l_iDown) + " monsters down, expected " + std::to_string(m_jScenarioExpect[toms::field::g_pszFoesDown].get<int>()));
    }
    if (m_jScenarioExpect.contains(toms::field::g_pszBattleEvents) && m_jScenarioExpect[toms::field::g_pszBattleEvents].is_object())   // type -> how many
        for (auto l_oIt = m_jScenarioExpect[toms::field::g_pszBattleEvents].begin(); l_oIt != m_jScenarioExpect[toms::field::g_pszBattleEvents].end(); ++l_oIt) {
            int l_iN = 0;
            for (const toms::BattleEvent& l_oE : m_oCs.m_vEvents) l_iN += l_oE.m_strType == l_oIt.key() ? 1 : 0;
            if (l_iN != l_oIt.value().get<int>())
                l_fnFail(std::to_string(l_iN) + " battle events \"" + l_oIt.key() + "\", expected " + std::to_string(l_oIt.value().get<int>()));
        }
    if (m_jScenarioExpect.contains(toms::field::g_pszGrade) && (!m_oCs.m_oResult.m_bValid || m_oCs.m_oResult.m_strGrade != m_jScenarioExpect[toms::field::g_pszGrade].get<std::string>()))
        l_fnFail("the result's grade is " + (m_oCs.m_oResult.m_bValid ? m_oCs.m_oResult.m_strGrade : std::string("(no result)")) + ", expected " +
             m_jScenarioExpect[toms::field::g_pszGrade].get<std::string>());   // docs/gameplay/15 §3.6
    if (m_jScenarioExpect.contains(toms::field::g_pszPace) && f_BattlePace() != m_jScenarioExpect[toms::field::g_pszPace].get<std::string>())
        l_fnFail("the pace is " + f_BattlePace() + ", expected " + m_jScenarioExpect[toms::field::g_pszPace].get<std::string>());
    if (m_jScenarioExpect.contains(toms::field::g_pszTarget) && m_oCs.m_iTarget != m_jScenarioExpect[toms::field::g_pszTarget].get<int>())
        l_fnFail("the target is monster " + std::to_string(m_oCs.m_iTarget) + ", expected " + std::to_string(m_jScenarioExpect[toms::field::g_pszTarget].get<int>()));
    if (m_jScenarioExpect.contains(toms::field::g_pszBattleAnim) && m_strBattleRigState != m_jScenarioExpect[toms::field::g_pszBattleAnim].get<std::string>())
        l_fnFail("the battle picture shows \"" + m_strBattleRigState + "\", expected \"" + m_jScenarioExpect[toms::field::g_pszBattleAnim].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains(toms::field::g_pszCondition) && !f_Holds(m_jScenarioExpect[toms::field::g_pszCondition]))
        l_fnFail("the condition " + m_jScenarioExpect[toms::field::g_pszCondition].dump() + " does not hold");
    l_strReport = "[scenario] " + m_strScenarioName + ": " + (l_bOk ? "ok (" + l_strScene + ")" : "FAILED" + l_strOut);
    return l_bOk;
}
