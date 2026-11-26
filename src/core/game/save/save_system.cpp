#include "field_keys.h"    // toms::field: every data field by its constant
#include "save_system.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace toms {

// ---- migrations (see save_system.h) -----------------------------------------------------------------------------------
// Entry i upgrades a file from version i+1 to i+2. Versions 1 -> 3 only ADDED fields, so their steps are empty (the
// readers below default what is missing). Example of a real step, for when a field is renamed in version 4:
//     [](nlohmann::json& j) { if (j.contains("oldName")) { j["newName"] = j["oldName"]; j.erase("oldName"); } },
using Migration = std::function<void(nlohmann::json&)>;
static const std::vector<Migration> g_vMetaMigrations = {
    [](nlohmann::json&) {},   // 1 -> 2
    [](nlohmann::json&) {},   // 2 -> 3: cycleIndex, endingsSeen, hintsUnlocked (defaults)
};
static const std::vector<Migration> g_vRunMigrations = {
    [](nlohmann::json&) {},   // 1 -> 2
    [](nlohmann::json&) {},   // 2 -> 3: the story state (defaults)
};
static_assert(g_iMetaSaveSchemaVersion == 3 && g_iRunSaveSchemaVersion == 3,
              "a new schema version needs its step in kMetaMigrations / kRunMigrations");

static void f_Migrate(nlohmann::json& l_jJ, const std::vector<Migration>& l_vSteps, int l_iCurrent) {
    if (!l_jJ.is_object()) return;
    int l_iVer = std::max(1, l_jJ.value(toms::field::g_pszSchemaVersion, 1));
    while (l_iVer < l_iCurrent && l_iVer - 1 < (int)l_vSteps.size()) {
        l_vSteps[(size_t)l_iVer - 1](l_jJ);
        ++l_iVer;
    }
    // the parsed result says which version it came from (versionMismatch); the json itself is now `ver`
}

void f_MigrateMetaJson(nlohmann::json& l_jJ) { f_Migrate(l_jJ, g_vMetaMigrations, g_iMetaSaveSchemaVersion); }
void f_MigrateRunJson(nlohmann::json& l_jJ) { f_Migrate(l_jJ, g_vRunMigrations, g_iRunSaveSchemaVersion); }

nlohmann::json f_ToJson(const MetaSaveData& l_oM) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszSchemaVersion] = l_oM.m_iSchemaVersion;
    l_jJ[toms::field::g_pszStoryFlags] = std::vector<std::string>(l_oM.m_setStoryFlags.begin(), l_oM.m_setStoryFlags.end());
    l_jJ[toms::field::g_pszCurrentBeat] = l_oM.m_iCurrentBeat;
    l_jJ[toms::field::g_pszUnlockedStages] = l_oM.m_vUnlockedStages;
    l_jJ[toms::field::g_pszMissionsClaimed] = l_oM.m_vMissionsClaimed;
    l_jJ[toms::field::g_pszPermanentAtkBonus] = l_oM.m_iPermanentAtkBonus;
    l_jJ[toms::field::g_pszPermanentDefBonus] = l_oM.m_iPermanentDefBonus;
    l_jJ[toms::field::g_pszPermanentHpBonus] = l_oM.m_iPermanentHpBonus;
    // S3 (schemaVersion 3)
    l_jJ[toms::field::g_pszCycleIndex] = l_oM.m_iCycleIndex;
    l_jJ[toms::field::g_pszEndingsSeen] = l_oM.m_vEndingsSeen;
    l_jJ[toms::field::g_pszHintsUnlocked] = l_oM.m_vHintsUnlocked;
    // S5: forging blueprints known -- meta-scope, survives rebirth (see the field's own comment).
    l_jJ[toms::field::g_pszForgeRecipesKnown] = l_oM.m_vForgeRecipesKnown;
    if (!l_oM.m_mapCards.empty()) l_jJ[toms::field::g_pszCards] = f_CardBookToJson(l_oM.m_mapCards);   // monster cards (optional: an older file has none)
    if (!l_oM.m_strTrainDay.empty()) { l_jJ[toms::field::g_pszTrainDay] = l_oM.m_strTrainDay; l_jJ[toms::field::g_pszTrainRuns] = l_oM.m_iTrainRuns; }   // the training ground
    return l_jJ;
}

MetaSaveData f_MetaFromJson(const nlohmann::json& l_jIn, bool* l_pVersionMismatch) {
    MetaSaveData l_oM;
    if (!l_jIn.is_object()) { if (l_pVersionMismatch) *l_pVersionMismatch = true; return l_oM; }
    int l_iVer = l_jIn.value(toms::field::g_pszSchemaVersion, 0);
    nlohmann::json l_jJ = l_jIn;
    f_MigrateMetaJson(l_jJ);
    if (l_pVersionMismatch) *l_pVersionMismatch = (l_iVer != g_iMetaSaveSchemaVersion);
    l_oM.m_iSchemaVersion = l_iVer;
    if (l_jJ.contains(toms::field::g_pszStoryFlags) && l_jJ[toms::field::g_pszStoryFlags].is_array())
        for (auto& l_jF : l_jJ[toms::field::g_pszStoryFlags]) if (l_jF.is_string()) l_oM.m_setStoryFlags.insert(l_jF.get<std::string>());
    l_oM.m_iCurrentBeat = l_jJ.value(toms::field::g_pszCurrentBeat, 0);
    l_oM.m_vUnlockedStages  = l_jJ.value(toms::field::g_pszUnlockedStages, std::vector<std::string>{});
    l_oM.m_vMissionsClaimed = l_jJ.value(toms::field::g_pszMissionsClaimed, std::vector<std::string>{});
    l_oM.m_iPermanentAtkBonus = l_jJ.value(toms::field::g_pszPermanentAtkBonus, 0);
    l_oM.m_iPermanentDefBonus = l_jJ.value(toms::field::g_pszPermanentDefBonus, 0);
    l_oM.m_iPermanentHpBonus  = l_jJ.value(toms::field::g_pszPermanentHpBonus, 0);
    // S3: absent in a pre-v3 file -> the declared defaults (cycleIndex 1, empty lists). This is the
    // "must not refuse an old save" rule from docs/story/STORY_DATA_SCHEMA.md section 9; the caller sees
    // *versionMismatch so it can log/migrate, but loading never fails because of it.
    l_oM.m_iCycleIndex = std::max(1, l_jJ.value(toms::field::g_pszCycleIndex, 1));
    l_oM.m_vEndingsSeen    = l_jJ.value(toms::field::g_pszEndingsSeen, std::vector<std::string>{});
    l_oM.m_vHintsUnlocked  = l_jJ.value(toms::field::g_pszHintsUnlocked, std::vector<std::string>{});
    // S5: a pre-S5 file simply has none -- empty list is the documented migration default, same
    // rule as every other field above.
    l_oM.m_vForgeRecipesKnown = l_jJ.value(toms::field::g_pszForgeRecipesKnown, std::vector<std::string>{});
    l_oM.m_mapCards = f_CardBookFromJson(l_jJ.value(toms::field::g_pszCards, nlohmann::json::object()));
    l_oM.m_strTrainDay = l_jJ.value(toms::field::g_pszTrainDay, std::string());
    l_oM.m_iTrainRuns = std::max(0, l_jJ.value(toms::field::g_pszTrainRuns, 0));
    return l_oM;
}

nlohmann::json f_ToJson(const RunSaveData& l_oR) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszSchemaVersion] = l_oR.m_iSchemaVersion;
    l_jJ[toms::field::g_pszCurrentStageId] = l_oR.m_strCurrentStageId;
    nlohmann::json l_jP;
    l_jP[toms::field::g_pszHp] = l_oR.m_oPlayer.m_iHp; l_jP[toms::field::g_pszMaxhp] = l_oR.m_oPlayer.m_iMaxhp;
    l_jP[toms::field::g_pszAtk] = l_oR.m_oPlayer.m_iAtk; l_jP[toms::field::g_pszDef] = l_oR.m_oPlayer.m_iDef;
    l_jP[toms::field::g_pszGold] = l_oR.m_oPlayer.m_iGold; l_jP[toms::field::g_pszExp] = l_oR.m_oPlayer.m_iExp; l_jP[toms::field::g_pszLv] = l_oR.m_oPlayer.m_iLv;
    l_jP[toms::field::g_pszKeyYellow] = l_oR.m_oPlayer.m_iKeyYellow; l_jP[toms::field::g_pszKeyBlue] = l_oR.m_oPlayer.m_iKeyBlue; l_jP[toms::field::g_pszKeyRed] = l_oR.m_oPlayer.m_iKeyRed;
    l_jP[toms::field::g_pszInv] = l_oR.m_oPlayer.m_vInv;
    l_jP[toms::field::g_pszAttrs] = l_oR.m_oPlayer.m_mapAttrs;
    l_jP[toms::field::g_pszGear] = l_oR.m_oPlayer.m_vGear;
    l_jP[toms::field::g_pszWeapon] = l_oR.m_oPlayer.m_strWeapon; l_jP[toms::field::g_pszArmor] = l_oR.m_oPlayer.m_strArmor; l_jP[toms::field::g_pszTalent] = l_oR.m_oPlayer.m_strTalent;
    l_jP[toms::field::g_pszAttrPoints] = l_oR.m_oPlayer.m_iAttrPoints;
    l_jP[toms::field::g_pszWeaponExp] = l_oR.m_oPlayer.m_mapWeaponExp;
    l_jP[toms::field::g_pszCrystals] = l_oR.m_oPlayer.m_iCrystals;
    l_jJ[toms::field::g_pszPlayer] = l_jP;
    l_jJ[toms::field::g_pszEntityStatus] = l_oR.m_mapEntityStatus;
    // S3 (schemaVersion 3): the run's story state -- see RunStoryState for what writes/reads it.
    l_jJ[toms::field::g_pszChoices] = l_oR.m_mapChoices;
    l_jJ[toms::field::g_pszCounters] = l_oR.m_mapCounters;
    l_jJ[toms::field::g_pszSideStories] = l_oR.m_mapSideStories;
    l_jJ[toms::field::g_pszFlags] = l_oR.m_mapFlags;
    l_jJ[toms::field::g_pszFloor] = l_oR.m_strFloor;
    l_jJ[toms::field::g_pszClearedFloors] = l_oR.m_vClearedFloors;
    l_jJ[toms::field::g_pszShards] = l_oR.m_vShards;
    l_jJ[toms::field::g_pszDeathsTotal] = l_oR.m_iDeathsTotal;
    l_jJ[toms::field::g_pszDeathsNonBoss] = l_oR.m_iDeathsNonBoss;
    // S4: the skill tree's run-scoped progress -- see RunStoryState for what writes/reads it.
    l_jJ[toms::field::g_pszSkillPoints] = l_oR.m_iSkillPoints;
    l_jJ[toms::field::g_pszSkillsOwned] = l_oR.m_vSkillsOwned;
    l_jJ[toms::field::g_pszEventsStarted] = l_oR.m_vEventsStarted;
    l_jJ[toms::field::g_pszEventsFinished] = l_oR.m_vEventsFinished;
    if (!l_oR.m_mapVars.empty()) l_jJ[toms::field::g_pszVars] = l_oR.m_mapVars;
    if (!l_oR.m_mapNpcTalk.empty()) l_jJ[toms::field::g_pszNpcTalk] = l_oR.m_mapNpcTalk;
    if (!l_oR.m_mapEventCounts.empty()) l_jJ[toms::field::g_pszEventCounts] = l_oR.m_mapEventCounts;
    l_jJ[toms::field::g_pszSuperMax] = l_oR.m_iSuperMax;
    if (l_oR.m_jMissions.is_object() && !l_oR.m_jMissions.empty()) l_jJ[toms::field::g_pszMissions] = l_oR.m_jMissions;
    return l_jJ;
}

RunSaveData f_RunFromJson(const nlohmann::json& l_jIn, bool* l_pVersionMismatch) {
    RunSaveData l_oR;
    if (!l_jIn.is_object()) { if (l_pVersionMismatch) *l_pVersionMismatch = true; return l_oR; }
    int l_iVer = l_jIn.value(toms::field::g_pszSchemaVersion, 0);
    nlohmann::json l_jJ = l_jIn;
    f_MigrateRunJson(l_jJ);
    if (l_pVersionMismatch) *l_pVersionMismatch = (l_iVer != g_iRunSaveSchemaVersion);
    l_oR.m_iSchemaVersion = l_iVer;
    l_oR.m_strCurrentStageId = l_jJ.value(toms::field::g_pszCurrentStageId, std::string());
    if (l_jJ.contains(toms::field::g_pszPlayer) && l_jJ[toms::field::g_pszPlayer].is_object()) {
        auto& l_jP = l_jJ[toms::field::g_pszPlayer];
        l_oR.m_oPlayer.m_iHp = l_jP.value(toms::field::g_pszHp, 120);
        l_oR.m_oPlayer.m_iMaxhp = l_jP.value(toms::field::g_pszMaxhp, 120);
        l_oR.m_oPlayer.m_iAtk = l_jP.value(toms::field::g_pszAtk, 12);
        l_oR.m_oPlayer.m_iDef = l_jP.value(toms::field::g_pszDef, 4);
        l_oR.m_oPlayer.m_iGold = l_jP.value(toms::field::g_pszGold, 0);
        l_oR.m_oPlayer.m_iExp = l_jP.value(toms::field::g_pszExp, 0);
        l_oR.m_oPlayer.m_iLv = l_jP.value(toms::field::g_pszLv, 1);
        l_oR.m_oPlayer.m_iKeyYellow = l_jP.value(toms::field::g_pszKeyYellow, 0);
        l_oR.m_oPlayer.m_iKeyBlue = l_jP.value(toms::field::g_pszKeyBlue, 0);
        l_oR.m_oPlayer.m_iKeyRed = l_jP.value(toms::field::g_pszKeyRed, 0);
        l_oR.m_oPlayer.m_vInv = l_jP.value(toms::field::g_pszInv, std::vector<std::string>{});
        if (l_jP.contains(toms::field::g_pszAttrs) && l_jP[toms::field::g_pszAttrs].is_object())
            for (auto& [l_strK, l_jV] : l_jP[toms::field::g_pszAttrs].items())
                if (l_jV.is_number_integer()) l_oR.m_oPlayer.m_mapAttrs[l_strK] = l_jV.get<int>();
        l_oR.m_oPlayer.m_vGear = l_jP.value(toms::field::g_pszGear, std::vector<std::string>{});
        l_oR.m_oPlayer.m_strWeapon = l_jP.value(toms::field::g_pszWeapon, std::string());
        l_oR.m_oPlayer.m_strArmor = l_jP.value(toms::field::g_pszArmor, std::string());
        l_oR.m_oPlayer.m_strTalent = l_jP.value(toms::field::g_pszTalent, std::string());
        l_oR.m_oPlayer.m_iAttrPoints = l_jP.value(toms::field::g_pszAttrPoints, 0);
        l_oR.m_oPlayer.m_iCrystals = std::max(0, l_jP.value(toms::field::g_pszCrystals, 0));
        // (a save from before weapon EXP has "weaponUses": its wins count as EXP)
        const char* l_pszWeaponKey = l_jP.contains(toms::field::g_pszWeaponExp) ? "weaponExp" : "weaponUses";
        if (l_jP.contains(l_pszWeaponKey) && l_jP[l_pszWeaponKey].is_object())   // optional: an older save has none (every weapon Lv 1)
            for (auto& [l_strK, l_jV] : l_jP[l_pszWeaponKey].items())
                if (l_jV.is_number_integer()) l_oR.m_oPlayer.m_mapWeaponExp[l_strK] = std::max(0, l_jV.get<int>());
    }
    if (l_jJ.contains(toms::field::g_pszEntityStatus) && l_jJ[toms::field::g_pszEntityStatus].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszEntityStatus].items())
            if (l_jV.is_string()) l_oR.m_mapEntityStatus[l_strK] = l_jV.get<std::string>();
    // S3: a pre-v3 run file simply has none of these; defaults above (floor F01, everything empty)
    // are the documented migration. Loading must never be refused for a version difference.
    if (l_jJ.contains(toms::field::g_pszChoices) && l_jJ[toms::field::g_pszChoices].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszChoices].items()) if (l_jV.is_string()) l_oR.m_mapChoices[l_strK] = l_jV.get<std::string>();
    if (l_jJ.contains(toms::field::g_pszCounters) && l_jJ[toms::field::g_pszCounters].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszCounters].items()) if (l_jV.is_number_integer()) l_oR.m_mapCounters[l_strK] = l_jV.get<int>();
    if (l_jJ.contains(toms::field::g_pszSideStories) && l_jJ[toms::field::g_pszSideStories].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszSideStories].items()) if (l_jV.is_string()) l_oR.m_mapSideStories[l_strK] = l_jV.get<std::string>();
    if (l_jJ.contains(toms::field::g_pszFlags) && l_jJ[toms::field::g_pszFlags].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszFlags].items()) if (l_jV.is_boolean()) l_oR.m_mapFlags[l_strK] = l_jV.get<bool>();
    l_oR.m_strFloor = l_jJ.value(toms::field::g_pszFloor, std::string("F01"));
    l_oR.m_vClearedFloors = l_jJ.value(toms::field::g_pszClearedFloors, std::vector<std::string>{});
    l_oR.m_vShards = l_jJ.value(toms::field::g_pszShards, std::vector<std::string>{});
    l_oR.m_iDeathsTotal = l_jJ.value(toms::field::g_pszDeathsTotal, 0);
    l_oR.m_iDeathsNonBoss = l_jJ.value(toms::field::g_pszDeathsNonBoss, 0);
    // S4: a pre-S4 file simply has neither -- defaults (0 points, no skills) are the documented
    // migration, same rule as every other S3/S4 field above.
    l_oR.m_iSkillPoints = l_jJ.value(toms::field::g_pszSkillPoints, 0);
    l_oR.m_vSkillsOwned = l_jJ.value(toms::field::g_pszSkillsOwned, std::vector<std::string>{});
    l_oR.m_vEventsStarted = l_jJ.value(toms::field::g_pszEventsStarted, std::vector<std::string>{});
    l_oR.m_vEventsFinished = l_jJ.value(toms::field::g_pszEventsFinished, std::vector<std::string>{});
    if (l_jJ.contains(toms::field::g_pszVars) && l_jJ[toms::field::g_pszVars].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszVars].items()) if (l_jV.is_number_integer()) l_oR.m_mapVars[l_strK] = l_jV.get<int>();
    if (l_jJ.contains(toms::field::g_pszEventCounts) && l_jJ[toms::field::g_pszEventCounts].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszEventCounts].items()) if (l_jV.is_number_integer()) l_oR.m_mapEventCounts[l_strK] = l_jV.get<int>();
    if (l_jJ.contains(toms::field::g_pszNpcTalk) && l_jJ[toms::field::g_pszNpcTalk].is_object())
        for (auto& [l_strK, l_jV] : l_jJ[toms::field::g_pszNpcTalk].items())
            if (l_jV.is_array()) { std::vector<int>& l_vT = l_oR.m_mapNpcTalk[l_strK]; for (auto& l_jN : l_jV) if (l_jN.is_number_integer()) l_vT.push_back(l_jN.get<int>()); }
    l_oR.m_iSuperMax = l_jJ.value(toms::field::g_pszSuperMax, 0);   // 0: battle.json "superPoints"
    if (l_jJ.contains(toms::field::g_pszMissions) && l_jJ[toms::field::g_pszMissions].is_object()) l_oR.m_jMissions = l_jJ[toms::field::g_pszMissions];
    return l_oR;
}

nlohmann::json f_ReadJsonFileSafe(const std::string& l_strPath) {
    std::ifstream l_oF(l_strPath, std::ios::binary);
    if (!l_oF) { fprintf(stderr, "[save_system] cannot open %s\n", l_strPath.c_str()); return {}; }
    std::string l_strBuf((std::istreambuf_iterator<char>(l_oF)), std::istreambuf_iterator<char>());
    if (l_strBuf.empty()) { fprintf(stderr, "[save_system] empty file %s\n", l_strPath.c_str()); return {}; }
    try {
        return nlohmann::json::parse(l_strBuf);
    } catch (const std::exception& l_oE) {
        fprintf(stderr, "[save_system] parse error in %s: %s\n", l_strPath.c_str(), l_oE.what());
        return {};
    }
}

std::string f_BackupPath(const std::string& l_strPath) { return l_strPath + ".bak"; }

bool f_WriteJsonAtomic(const std::string& l_strPath, const nlohmann::json& l_jJ, bool l_bKeepBackup) {
    std::string l_strTmp = l_strPath + ".tmp";
    {
        const std::string l_strText = l_jJ.dump(2);
        FILE* l_pF = std::fopen(l_strTmp.c_str(), "wb");
        if (!l_pF) { fprintf(stderr, "[save_system] cannot open %s for write\n", l_strTmp.c_str()); return false; }
        bool l_bOk = std::fwrite(l_strText.data(), 1, l_strText.size(), l_pF) == l_strText.size() && std::fflush(l_pF) == 0;
        // Flushed to the disk itself, not only to the OS cache: otherwise the rename below can reach the disk
        // before the data does, and a power cut leaves an empty or partial file under the real name.
#ifdef _WIN32
        l_bOk = l_bOk && _commit(_fileno(l_pF)) == 0;
#else
        l_bOk = l_bOk && fsync(fileno(l_pF)) == 0;
#endif
        l_bOk = (std::fclose(l_pF) == 0) && l_bOk;
        if (!l_bOk) { fprintf(stderr, "[save_system] write failed for %s\n", l_strTmp.c_str()); return false; }
    }
    std::error_code l_oEc;
    // The file being replaced becomes the backup -- only if it is still good (a damaged file must not push a
    // good backup out). A failed backup step does not stop the save.
    if (l_bKeepBackup && std::filesystem::exists(l_strPath, l_oEc) && f_ReadJsonFileSafe(l_strPath).is_object())
        std::filesystem::rename(l_strPath, f_BackupPath(l_strPath), l_oEc);
    // std::filesystem::rename replaces an existing file on every platform; std::rename does not on
    // Windows, where it made every save after the first one fail.
    l_oEc.clear();
    std::filesystem::rename(l_strTmp, l_strPath, l_oEc);
    if (l_oEc) {
        fprintf(stderr, "[save_system] rename %s -> %s failed: %s\n", l_strTmp.c_str(), l_strPath.c_str(), l_oEc.message().c_str());
        return false;
    }
    return true;
}

} // namespace toms
