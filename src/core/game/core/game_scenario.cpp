// game_scenario.cpp -- test scenarios: start the game straight in one scene from a setting file
// (docs/gameplay/08_TEST_SCENARIOS.md). toms_game --scenario=<folder or file> plays it; --scenario-test also runs the
// scenario's own "test" frames and keys and exits 3 when scenarioCheck fails (tests/scenarios/, ctest scenario.*).
//
// A scenario never writes a save: its run has no slot (newGame(-1)), and a save it starts from is read from the
// scenario's folder, never from the player's save folder.
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
    if (l_jJ.contains("save")) l_fnAbsolute(l_jJ["save"]);
    if (l_jJ.contains("dialogue") && l_jJ["dialogue"].is_object() && l_jJ["dialogue"].contains("file")) l_fnAbsolute(l_jJ["dialogue"]["file"]);
    if (l_jJ.contains("stage") && l_jJ["stage"].is_string()) {
        const std::string l_strSt = l_jJ["stage"].get<std::string>();
        if (l_strSt.size() > 5 && l_strSt.compare(l_strSt.size() - 5, 5, ".json") == 0) l_fnAbsolute(l_jJ["stage"]);
    }
    nlohmann::json l_jBase = nlohmann::json::object();
    if (l_jJ.contains("include")) {
        const nlohmann::json l_jInc = l_jJ["include"].is_array() ? l_jJ["include"] : nlohmann::json::array({l_jJ["include"]});
        for (const auto& l_jName : l_jInc) {
            if (!l_jName.is_string()) continue;
            nlohmann::json l_jPart;
            if (!f_ReadScenarioJson(l_oFile.parent_path() / l_jName.get<std::string>(), l_jPart, l_strError, l_iDepth + 1)) return false;
            l_jBase.merge_patch(l_jPart);
        }
        l_jJ.erase("include");
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
    l_oEn.m_strId = l_strId; l_oEn.m_strName = m_oLocale.f_Field(l_itT->second["name"]);
    l_oEn.m_iHp = l_jB.value("hp", l_itT->second.value("hp", 1)); l_oEn.m_iAtk = l_jB.value("atk", l_itT->second.value("atk", 0));
    l_oEn.m_iDef = l_jB.value("def", l_itT->second.value("def", 0));
    l_oEn.m_iExp = l_itT->second.value("exp", 0); l_oEn.m_iGold = l_itT->second.value("gold", 0);
    l_oEn.m_iWeaponExp = std::max(0, l_itT->second.value("weaponExp", 1));
    l_oEn.m_iX = l_oEn.m_iY = -1;   // not on the map: winning removes nothing
    l_oEn.m_bBoss = l_itT->second.value("boss", false);
    l_oEn.m_iAtkIntervalMs = l_jB.value("atk_interval_ms", l_itT->second.value("atk_interval_ms", 4000));
    // the battle's rules for it (docs/gameplay/13), each overridable by the battle setup
    const nlohmann::json& l_jTp = l_itT->second;
    l_oEn.m_strMapId = l_strId;
    l_oEn.m_strLook = l_jB.value("look", l_jTp.value("look", std::string("monster")));
    l_oEn.m_vMoves = toms::f_MoveChoicesFromJson(l_jB.contains("moves") ? l_jB["moves"] : l_jTp.value("moves", nlohmann::json::array()));
    l_oEn.m_iBreakPoints = l_jB.value("break", l_jTp.value("break", 0));
    l_oEn.m_jEvents = l_jB.contains("events") ? l_jB["events"] : l_jTp.value("events", nlohmann::json::array());
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
    m_oCs.m_strLog = f_TrParam(f_TrParam(f_TrParam(m_oLocale.f_Tr("arena.summary"), "n", std::to_string(m_oArena.m_iFights)),
                             "w", std::to_string(m_oArena.m_iWins)), "l", std::to_string(m_oArena.m_iLosses));
}

bool Game::f_ArenaEndFight(bool l_bWon) {
    if (!m_oArena.m_bOn) return false;
    m_oCs.m_bActive = false;
    m_oCs.m_bWon = l_bWon;   // a win waits for the player's tap; a loss shows briefly
    if (l_bWon) { m_oArena.m_iWins++; m_oCs.m_strLog = m_oLocale.f_Tr("arena.won"); }
    else { m_oArena.m_iLosses++; m_oCs.m_strLog = m_oLocale.f_Tr("arena.lost"); m_oCs.m_iResultPauseMs = 1200; }
    if (l_bWon && m_oArena.m_bTraining) {   // the training ground (docs/gameplay/16 §5): more EXP and weapon EXP
        const nlohmann::json l_jTr = m_jBattleJson.value("training", nlohmann::json::object());
        int l_iExp = 0;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_iExp += l_oF.m_oEnemy.m_iExp;
        l_iExp = (int)std::lround(l_iExp * std::max(0.0f, l_jTr.value("exp", 2.0f)));
        int l_iUses = 0;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_iUses += l_oF.m_oEnemy.m_iWeaponExp;
        l_iUses = (int)std::lround(l_iUses * std::max(0.0f, l_jTr.value("weaponExp", 2.0f)));
        f_GainExp(l_iExp);
        m_mapWeaponExp[f_WeaponKey()] += l_iUses;
        m_oArena.m_iExpGained += l_iExp;
        m_oArena.m_iWeaponExpGained += l_iUses;
        m_oCs.m_strLog = f_TrParam(f_TrParam(m_oLocale.f_Tr("training.won"), "exp", std::to_string(l_iExp)), "uses", std::to_string(l_iUses));
        f_MarkProgressDirty();
    }
    return true;
}

bool Game::f_StartScenarioJson(const nlohmann::json& l_jSc, const std::string& l_strDefaultName, std::string& l_strError) {
    m_strScenarioName = l_jSc.value("name", l_strDefaultName);
    m_oArena = Arena{};
    m_strScenarioScene = l_jSc.value("scene", std::string("stage"));
    m_jScenarioExpect = l_jSc.value("expect", nlohmann::json::object());
    // the attack's play (touch_play.h): scenarios play the track unless their battle sets "play" ("weapon": the weapon's)
    m_strPlayOverride = l_jSc.value("battle", nlohmann::json::object()).value("play", std::string("track"));
    m_strUltimateOverride = l_jSc.value("battle", nlohmann::json::object()).value("ultimate", std::string());
    m_fScenarioBattleSpeed = std::max(0.1f, l_jSc.value("battle", nlohmann::json::object()).value("speed", 1.0f));
    const nlohmann::json l_jTest = l_jSc.value("test", nlohmann::json::object());
    m_iScenarioTestFrames = l_jTest.value("frames", 0);
    m_strScenarioTestKeys = l_jTest.value("keys", std::string());
    m_strScenarioTestDrags = l_jTest.value("drags", std::string());     // "x0:y0:x1:y1:frames@frame,..." (design px), like --drag
    m_strScenarioTestClicks = l_jTest.value("clicks", std::string());   // "x:y@frame,..." (design px), like --clicks
    auto l_fnBad = [&](const std::string& l_strWhat) { l_strError = m_strScenarioName + ": " + l_strWhat; return false; };

    // 1. The run: from a save file in the scenario's folder, or a new game. Neither is ever written.
    if (m_strScenarioScene == "title") return true;   // the title as the game starts: nothing to set up
    if (l_jSc.contains("save")) {
        toms::MetaSaveData l_oM;
        toms::RunSaveData l_oR;
        int l_iPlaySec = 0;
        const fs::path l_oSave = l_jSc["save"].get<std::string>();   // absolute (readScenarioJson)
        if (!toms::f_ReadSaveFile(l_oSave.string(), l_oM, l_oR, &l_iPlaySec)) return l_fnBad("cannot read the save " + l_oSave.string());
        f_ApplyLoadedRun(l_oM, l_oR, /*slot=*/0, l_iPlaySec);
        m_bSlotReadOnly = true;
    } else {
        f_NewGame(-1);
    }

    // 2. The floor: a floor id (F04), a stage id, or a stage file next to the scenario.
    if (l_jSc.contains("stage")) {
        f_LoadStage(l_jSc["stage"].get<std::string>());   // a stage file's path is already absolute (readScenarioJson)
    }

    // 3. The story so far: stair locks, finished events, flags.
    if (l_jSc.contains("stairLocks"))
        for (const auto& l_jL : l_jSc["stairLocks"]) {   // "event": one, or "events": several (W38: all must be done)
            for (const std::string& l_strE : f_Strings(l_jL, "event")) f_DebugLockStairs(l_jL.value("floor", std::string()), l_strE);
            for (const std::string& l_strE : f_Strings(l_jL, "events")) f_DebugLockStairs(l_jL.value("floor", std::string()), l_strE);
        }
    for (const std::string& l_strF : f_Strings(l_jSc, "flags")) f_DebugSetFlag(l_strF);
    for (const std::string& l_strE : f_Strings(l_jSc, "eventsDone")) f_DebugFinishEvent(l_strE);
    if (l_jSc.contains("cards") && l_jSc["cards"].is_object())   // monster cards held: { "slime": 2 } (the training ground, 16 §5)
        for (auto l_oIt = l_jSc["cards"].begin(); l_oIt != l_jSc["cards"].end(); ++l_oIt)
            if (l_oIt.value().is_number_integer()) m_oMeta.m_mapCards[l_oIt.key()].m_iCopies = std::max(0, l_oIt.value().get<int>());

    // 4. The player: items and gear first (gear can change the stats), then the numbers as given. A new game starts with
    // the starting weapon (the bow) equipped; "unarmed": true takes it off.
    const nlohmann::json l_jP = l_jSc.value("player", nlohmann::json::object());
    if (l_jP.value("unarmed", false)) m_oEquipped.m_strWeaponId.clear();
    for (const std::string& l_strId : f_Strings(l_jP, "give")) f_DebugGive(l_strId);
    for (const std::string& l_strId : f_Strings(l_jP, "equip"))
        if (!f_DebugEquip(l_strId)) return l_fnBad("no equipment \"" + l_strId + "\" in data/items.json");
    if (l_jP.contains("x")) m_oPl.m_iX = l_jP["x"].get<int>();
    if (l_jP.contains("y")) m_oPl.m_iY = l_jP["y"].get<int>();
    if (l_jP.contains("maxhp")) m_oPl.m_iMaxhp = l_jP["maxhp"].get<int>();
    if (l_jP.contains("hp")) m_oPl.m_iHp = l_jP["hp"].get<int>();
    if (l_jP.contains("atk")) m_oPl.m_iAtk = l_jP["atk"].get<int>();
    if (l_jP.contains("def")) m_oPl.m_iDef = l_jP["def"].get<int>();
    if (l_jP.contains("gold")) m_oPl.m_iGold = l_jP["gold"].get<int>();
    if (l_jP.contains("exp")) m_oPl.m_iExp = l_jP["exp"].get<int>();
    if (l_jP.contains("lv")) m_oPl.m_iLv = l_jP["lv"].get<int>();
    if (l_jP.contains("attrPoints")) m_oPl.m_iAttrPoints = std::max(0, l_jP["attrPoints"].get<int>());   // to place on the Status tab
    if (l_jP.contains("keys")) {
        const nlohmann::json& l_jK = l_jP["keys"];
        m_oPl.m_iKeyYellow = l_jK.value("yellow", m_oPl.m_iKeyYellow);
        m_oPl.m_iKeyBlue = l_jK.value("blue", m_oPl.m_iKeyBlue);
        m_oPl.m_iKeyRed = l_jK.value("red", m_oPl.m_iKeyRed);
    }
    if (l_jP.contains("attrs"))
        for (auto l_oIt = l_jP["attrs"].begin(); l_oIt != l_jP["attrs"].end(); ++l_oIt) m_oPl.m_mapAttrs[l_oIt.key()] = l_oIt.value().get<int>();
    if (l_jP.contains("inventory")) m_oPl.m_vInv = f_Strings(l_jP, "inventory");
    if (l_jP.contains("weaponExp") && l_jP["weaponExp"].is_object())   // each weapon's EXP (docs/gameplay/17 §19)
        for (auto l_oIt = l_jP["weaponExp"].begin(); l_oIt != l_jP["weaponExp"].end(); ++l_oIt) m_mapWeaponExp[l_oIt.key()] = l_oIt.value().get<int>();
    if (l_jP.contains("weaponLevel") && l_jP["weaponLevel"].is_object())   // or its level: the EXP that reaches it
        for (auto l_oIt = l_jP["weaponLevel"].begin(); l_oIt != l_jP["weaponLevel"].end(); ++l_oIt)
            m_mapWeaponExp[l_oIt.key()] = toms::f_WeaponExpForLevel(f_WeaponPlayOf(l_oIt.key()), l_oIt.value().get<int>());

    // 5. The scene.
    const std::string& l_strS = m_strScenarioScene;
    if (l_strS == "stage") {
        // the walking scene as it is now
    } else if (l_strS == "battle" || l_strS == "battleWon") {
        const nlohmann::json l_jB = l_jSc.value("battle", nlohmann::json::object());
        const std::string l_strId = l_jB.value("enemy", std::string());
        EnemyInst l_oEn;
        if (!f_EnemyFromData(l_strId, l_jB, l_oEn)) return l_fnBad("no enemy \"" + l_strId + "\" in data/enemies.json");
        // "party": the monsters beside it (ids, or {"enemy": id, ...overrides}; docs/gameplay/13 §4.6)
        std::vector<EnemyInst> l_vParty;
        for (const auto& l_jM : l_jB.value("party", nlohmann::json::array())) {
            const nlohmann::json l_jO = l_jM.is_object() ? l_jM : nlohmann::json::object();
            const std::string l_strMid = l_jM.is_string() ? l_jM.get<std::string>() : l_jO.value("enemy", std::string());
            EnemyInst l_oPm;
            if (!f_EnemyFromData(l_strMid, l_jO, l_oPm)) return l_fnBad("no enemy \"" + l_strMid + "\" in data/enemies.json");
            l_vParty.push_back(l_oPm);
        }
        m_strPaceOverride = l_jB.value("pace", std::string());   // "cooldown" / "stamina" (docs/gameplay/13 §6.3)
        {   // "rules": data/battle.json's values for this battle ({"stamina": {...}, "break": {...}}), as the battle editor tries them
            nlohmann::json l_jRules = m_jBattleJson;
            if (l_jB.contains("rules") && l_jB["rules"].is_object()) l_jRules.merge_patch(l_jB["rules"]);
            m_oPaceSettings = toms::f_PaceFromJson(l_jRules);
            m_oBreakSettings = toms::f_BreakFromJson(l_jRules);
            m_oFeel = toms::f_FeelFromJson(l_jRules);
            m_oCellRules = toms::f_CellRulesFromJson(l_jRules);
            m_oTouchSettings = toms::f_TouchPlayFromJson(l_jRules);
            m_oGrade = toms::f_GradeFromJson(l_jRules);
            m_oGuard = toms::f_GuardFromJson(l_jRules);
            m_bComboHoldsMonsters = l_jRules.value("combo_holds_monsters", false);
            m_bComboRunsThroughAttacks = l_jRules.value("combo_runs_through_attacks", true);
        }
        f_StartCombat(l_oEn, l_vParty);
        if (l_jB.value("super", false)) m_oCs.m_iSuperCharge = m_oRun.f_SuperMax();   // the ultimate ready (docs/gameplay/16 §6)
        if (l_strS == "battleWon") f_FinishCombatWin();
    } else if (l_strS == "arena") {
        // the weapon test arena: "battle": { "enemies": [...] } (or "enemy"), fought in turn; the weapon is the
        // player's ("player": { "equip": [...] }; the starting bow unless "unarmed": true)
        const nlohmann::json l_jB = l_jSc.value("battle", nlohmann::json::object());
        m_oArena.m_vEnemies = f_Strings(l_jB, "enemies");
        if (m_oArena.m_vEnemies.empty()) m_oArena.m_vEnemies = f_Strings(l_jB, "enemy");
        if (m_oArena.m_vEnemies.empty()) return l_fnBad("\"battle\": { \"enemies\": [...] } is missing");
        for (const std::string& l_strId : m_oArena.m_vEnemies) {
            EnemyInst l_oProbe;
            if (!f_EnemyFromData(l_strId, l_jB, l_oProbe)) return l_fnBad("no enemy \"" + l_strId + "\" in data/enemies.json");
        }
        m_oArena.m_jOverrides = l_jB;
        m_oArena.m_bOn = true;
        f_ArenaNextFight();
    } else if (l_strS == "miniGame") {   // a reward event (docs/gameplay/07 phase 6): "miniGame": "<id in data/minigames.json>"
        f_OpenMiniGame(l_jSc.value("miniGame", std::string()));
        if (!m_oMiniGame.m_bOpen) return l_fnBad("no mini-game \"" + l_jSc.value("miniGame", std::string()) + "\" (data/minigames.json)");
    } else if (l_strS == "weaponTrial") {   // the start story (docs/gameplay/07 phase 3): the weapon master's dialogue
        f_StartWeaponTrial();
    } else if (l_strS == "dialogue") {
        const nlohmann::json l_jD = l_jSc.value("dialogue", nlohmann::json::object());
        if (l_jD.contains("file")) {
            const fs::path l_oF = l_jD["file"].get<std::string>();   // absolute (readScenarioJson)
            nlohmann::json l_jData = toms::f_ReadJsonFileSafe(l_oF.string());
            if (!l_jData.is_object()) return l_fnBad("cannot read the dialogue " + l_oF.string());
            m_strDlgNpc = l_jD.value("id", l_oF.stem().string());
            m_strDlgSpeaker.clear(); m_strDlgTalkNpc.clear();
            f_OpenDialogue(l_jData, DlgMode::Plain);
        } else {
            const std::string l_strId = l_jD.value("id", std::string());
            if (!f_ReadDialogue(l_strId).is_object()) return l_fnBad("no dialogue \"" + l_strId + "\" in data/dialogue/");
            f_StartDialogue(l_strId);
        }
        if (l_jD.contains("node")) f_EnterNode(l_jD["node"].get<std::string>());
    } else if (l_strS == "talk") {
        const std::string l_strNpc = l_jSc.value("talk", nlohmann::json::object()).value("npc", std::string());
        if (l_strNpc.empty()) return l_fnBad("\"talk\": { \"npc\": ... } is missing");
        f_TalkTo(l_strNpc);
    } else if (l_strS == "store") {
        m_bStoreUnlocked = true;
        m_bStoreUnlockDlg = false;
        f_OpenStore();
    } else if (l_strS == "menu") {
        const std::string l_strTab = l_jSc.value("menu", nlohmann::json::object()).value("tab", std::string("status"));
        const int l_iI = f_MenuTabIndex(l_strTab);
        if (l_iI < 0) return l_fnBad("menu tab \"" + l_strTab + "\" (status, gear, items, events, cards, system)");
        f_OpenPlayerMenu(l_iI);
    } else if (l_strS == "saveLoad") {
        const std::string l_strMode = l_jSc.value("saveLoad", nlohmann::json::object()).value("mode", std::string("load"));
        f_OpenPlayerMenu((int)MenuTab::System);
        f_OpenSaveLoad(l_strMode == "save" ? 0 : 1);
    } else if (l_strS == "stageSelect") {
        f_OpenStageSelect();
    } else if (l_strS == "stairsConfirm") {
        const nlohmann::json l_jC = l_jSc.value("stairs", nlohmann::json::object());
        f_RequestStageTransition(l_jC.value("to", std::string("F02")), l_jC.value("up", true));
    } else if (l_strS == "notice") {
        // the notice comes from the setup (a stair lock and its event finished); nothing more to open
    } else if (l_strS == "ending") {
        const std::string l_strId = l_jSc.value("ending", nlohmann::json::object()).value("id", std::string());
        if (l_strId.empty()) return l_fnBad("\"ending\": { \"id\": ... } is missing");
        f_TriggerEnding(l_strId);
    } else if (l_strS == "hub") {
        const std::string l_strLoc = l_jSc.value("hub", nlohmann::json::object()).value("location", std::string());
        if (!f_ActivateHubLocation(l_strLoc)) return l_fnBad("hub location \"" + l_strLoc + "\" is unknown or locked");
    } else {
        return l_fnBad("unknown scene \"" + l_strS + "\" (docs/gameplay/08_TEST_SCENARIOS.md lists them)");
    }
    std::fprintf(stderr, "[scenario] %s: scene %s started\n", m_strScenarioName.c_str(), l_strS.c_str());
    return true;
}

bool Game::f_ScenarioCheck(std::string& l_strReport) const {
    const std::string l_strScene = m_jScenarioExpect.value("scene", m_strScenarioScene);
    bool l_bOk = true;
    std::string l_strOut;
    auto l_fnFail = [&](const std::string& l_strWhy) { l_bOk = false; l_strOut += "\n  " + l_strWhy; };
    bool l_bShowing = false;
    if (l_strScene == "title") l_bShowing = m_oTitle.f_IsOpen();
    else if (l_strScene == "stage") l_bShowing = !m_oTitle.f_IsOpen() && !f_ModalActive();
    else if (l_strScene == "battle") l_bShowing = m_oCs.m_bActive;
    else if (l_strScene == "battleWon") l_bShowing = m_oCs.m_bWon;
    else if (l_strScene == "arena") l_bShowing = m_oArena.m_bOn && (m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0);
    else if (l_strScene == "dialogue" || l_strScene == "talk" || l_strScene == "weaponTrial") l_bShowing = m_bInDialogue;
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
    if (m_jScenarioExpect.contains("menuTab")) {
        const int l_iWant = f_MenuTabIndex(m_jScenarioExpect["menuTab"].get<std::string>());
        if ((int)m_eMenuTab != l_iWant) l_fnFail("the menu is not on tab " + m_jScenarioExpect["menuTab"].get<std::string>());
    }
    auto l_fnNum = [&](const char* l_pszKey, int l_iHave) {
        if (m_jScenarioExpect.contains(l_pszKey) && m_jScenarioExpect[l_pszKey].get<int>() != l_iHave)
            l_fnFail(std::string(l_pszKey) + " is " + std::to_string(l_iHave) + ", expected " + std::to_string(m_jScenarioExpect[l_pszKey].get<int>()));
    };
    l_fnNum("gold", m_oPl.m_iGold); l_fnNum("hp", m_oPl.m_iHp); l_fnNum("lv", m_oPl.m_iLv); l_fnNum("crystals", m_oPl.m_iCrystals);
    l_fnNum("arenaFights", m_oArena.m_iFights); l_fnNum("arenaWins", m_oArena.m_iWins);
    if (m_jScenarioExpect.contains("stage") && m_strCurStage != m_jScenarioExpect["stage"].get<std::string>())
        l_fnFail("the floor is " + m_strCurStage + ", expected " + m_jScenarioExpect["stage"].get<std::string>());
    for (const std::string& l_strId : f_Strings(m_jScenarioExpect, "inventoryHas"))
        if (std::find(m_oPl.m_vInv.begin(), m_oPl.m_vInv.end(), l_strId) == m_oPl.m_vInv.end()) l_fnFail("the inventory has no " + l_strId);
    for (const std::string& l_strF : f_Strings(m_jScenarioExpect, "flags"))
        if (!m_oRun.f_Flag(l_strF)) l_fnFail("the flag " + l_strF + " is not set");
    if (m_jScenarioExpect.contains("cards") && m_jScenarioExpect["cards"].is_object())   // monster id -> copies held
        for (auto l_oIt = m_jScenarioExpect["cards"].begin(); l_oIt != m_jScenarioExpect["cards"].end(); ++l_oIt) {
            auto l_itR = m_oMeta.m_mapCards.find(l_oIt.key());
            const int l_iHave = l_itR == m_oMeta.m_mapCards.end() ? 0 : l_itR->second.m_iCopies;
            if (l_iHave != l_oIt.value().get<int>())
                l_fnFail("cards of " + l_oIt.key() + ": " + std::to_string(l_iHave) + ", expected " + std::to_string(l_oIt.value().get<int>()));
        }
    if (m_jScenarioExpect.contains("stairUpLocked")) {   // the current floor's first stair up (W38 tests)
        const toms::StairLink* l_pUp = nullptr;
        for (const toms::StairLink& l_oL : m_oSt.m_vStairs) if (l_oL.m_bUp) { l_pUp = &l_oL; break; }
        const bool l_bWant = m_jScenarioExpect["stairUpLocked"].get<bool>();
        if (!l_pUp) l_fnFail("the floor has no stair up");
        else if (f_StairLocked(*l_pUp) != l_bWant) l_fnFail(std::string("the stair up is ") + (l_bWant ? "open, expected locked" : "locked, expected open"));
        if (l_pUp && m_jScenarioExpect.contains("stairUpMarked") &&
            f_StairJustOpened(m_oSt.m_strId, *l_pUp) != m_jScenarioExpect["stairUpMarked"].get<bool>())
            l_fnFail(std::string("the stair up is ") + (m_jScenarioExpect["stairUpMarked"].get<bool>() ? "not marked" : "marked") + " with the arrow");
    }
    if (m_jScenarioExpect.contains("equipped") && m_oEquipped.m_strWeaponId != m_jScenarioExpect["equipped"].get<std::string>())
        l_fnFail("the worn weapon is \"" + m_oEquipped.m_strWeaponId + "\", expected \"" + m_jScenarioExpect["equipped"].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains("gearOwned")) {   // exactly these, in any order
        std::vector<std::string> l_vWant = m_jScenarioExpect["gearOwned"].get<std::vector<std::string>>(), l_vHave = m_vGearOwned;
        std::sort(l_vWant.begin(), l_vWant.end());
        std::sort(l_vHave.begin(), l_vHave.end());
        if (l_vWant != l_vHave) l_fnFail("the owned gear is not what was expected");
    }
    if (m_jScenarioExpect.contains("dialogueNode") && (!m_bInDialogue || m_strDlgNode != m_jScenarioExpect["dialogueNode"].get<std::string>()))
        l_fnFail("the dialogue is not at node \"" + m_jScenarioExpect["dialogueNode"].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains("weaponLevel") && m_jScenarioExpect["weaponLevel"].is_object())   // weapon id -> its level
        for (auto l_oIt = m_jScenarioExpect["weaponLevel"].begin(); l_oIt != m_jScenarioExpect["weaponLevel"].end(); ++l_oIt)
            if (f_WeaponLevel(l_oIt.key()) != l_oIt.value().get<int>())
                l_fnFail("the weapon \"" + l_oIt.key() + "\" is Lv " + std::to_string(f_WeaponLevel(l_oIt.key())) + ", expected " +
                     std::to_string(l_oIt.value().get<int>()));
    if (m_jScenarioExpect.contains("backdrop") && !(m_spBackdrop && m_strBackdropAnim == m_jScenarioExpect["backdrop"].get<std::string>()))
        l_fnFail("the battle's background is \"" + m_strBackdropAnim + "\", expected " + m_jScenarioExpect["backdrop"].get<std::string>());
    if (m_jScenarioExpect.contains("foeClips") && m_jScenarioExpect["foeClips"].is_array())   // each monster's clip now
        for (size_t l_nI = 0; l_nI < m_jScenarioExpect["foeClips"].size(); l_nI++) {
            const std::string l_strHas = l_nI < m_vFoeRigs.size() ? m_vFoeRigs[l_nI].m_strClip : std::string();
            if (l_strHas != m_jScenarioExpect["foeClips"][l_nI].get<std::string>())
                l_fnFail("monster " + std::to_string(l_nI) + " plays \"" + l_strHas + "\", expected " + m_jScenarioExpect["foeClips"][l_nI].get<std::string>());
        }
    if (m_jScenarioExpect.contains("battleClip") && m_strBattleRigClip != m_jScenarioExpect["battleClip"].get<std::string>())
        l_fnFail("the battle picture plays the clip \"" + m_strBattleRigClip + "\", expected \"" + m_jScenarioExpect["battleClip"].get<std::string>() + "\"");
    const std::string l_strLeadState = m_vFoeRigs.empty() ? std::string() : m_vFoeRigs[0].m_strState;
    const std::string l_strLeadClip = m_vFoeRigs.empty() ? std::string() : m_vFoeRigs[0].m_strClip;
    if (m_jScenarioExpect.contains("enemyAnim") && l_strLeadState != m_jScenarioExpect["enemyAnim"].get<std::string>())
        l_fnFail("the monster shows \"" + l_strLeadState + "\", expected \"" + m_jScenarioExpect["enemyAnim"].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains("enemyClip") && l_strLeadClip != m_jScenarioExpect["enemyClip"].get<std::string>())
        l_fnFail("the monster plays the clip \"" + l_strLeadClip + "\", expected \"" + m_jScenarioExpect["enemyClip"].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains("foesDown")) {   // how many of the battle's monsters have fallen
        int l_iDown = 0;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_iDown += l_oF.f_Down() ? 1 : 0;
        if (l_iDown != m_jScenarioExpect["foesDown"].get<int>())
            l_fnFail(std::to_string(l_iDown) + " monsters down, expected " + std::to_string(m_jScenarioExpect["foesDown"].get<int>()));
    }
    if (m_jScenarioExpect.contains("battleEvents") && m_jScenarioExpect["battleEvents"].is_object())   // type -> how many
        for (auto l_oIt = m_jScenarioExpect["battleEvents"].begin(); l_oIt != m_jScenarioExpect["battleEvents"].end(); ++l_oIt) {
            int l_iN = 0;
            for (const toms::BattleEvent& l_oE : m_oCs.m_vEvents) l_iN += l_oE.m_strType == l_oIt.key() ? 1 : 0;
            if (l_iN != l_oIt.value().get<int>())
                l_fnFail(std::to_string(l_iN) + " battle events \"" + l_oIt.key() + "\", expected " + std::to_string(l_oIt.value().get<int>()));
        }
    if (m_jScenarioExpect.contains("grade") && (!m_oCs.m_oResult.m_bValid || m_oCs.m_oResult.m_strGrade != m_jScenarioExpect["grade"].get<std::string>()))
        l_fnFail("the result's grade is " + (m_oCs.m_oResult.m_bValid ? m_oCs.m_oResult.m_strGrade : std::string("(no result)")) + ", expected " +
             m_jScenarioExpect["grade"].get<std::string>());   // docs/gameplay/15 §3.6
    if (m_jScenarioExpect.contains("pace") && f_BattlePace() != m_jScenarioExpect["pace"].get<std::string>())
        l_fnFail("the pace is " + f_BattlePace() + ", expected " + m_jScenarioExpect["pace"].get<std::string>());
    if (m_jScenarioExpect.contains("target") && m_oCs.m_iTarget != m_jScenarioExpect["target"].get<int>())
        l_fnFail("the target is monster " + std::to_string(m_oCs.m_iTarget) + ", expected " + std::to_string(m_jScenarioExpect["target"].get<int>()));
    if (m_jScenarioExpect.contains("battleAnim") && m_strBattleRigState != m_jScenarioExpect["battleAnim"].get<std::string>())
        l_fnFail("the battle picture shows \"" + m_strBattleRigState + "\", expected \"" + m_jScenarioExpect["battleAnim"].get<std::string>() + "\"");
    if (m_jScenarioExpect.contains("condition") && !f_Holds(m_jScenarioExpect["condition"]))
        l_fnFail("the condition " + m_jScenarioExpect["condition"].dump() + " does not hold");
    l_strReport = "[scenario] " + m_strScenarioName + ": " + (l_bOk ? "ok (" + l_strScene + ")" : "FAILED" + l_strOut);
    return l_bOk;
}
