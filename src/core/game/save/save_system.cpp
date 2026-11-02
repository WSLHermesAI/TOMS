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
    int l_iVer = std::max(1, l_jJ.value("schemaVersion", 1));
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
    l_jJ["schemaVersion"] = l_oM.m_iSchemaVersion;
    l_jJ["storyFlags"] = std::vector<std::string>(l_oM.m_setStoryFlags.begin(), l_oM.m_setStoryFlags.end());
    l_jJ["currentBeat"] = l_oM.m_iCurrentBeat;
    l_jJ["unlockedStages"] = l_oM.m_vUnlockedStages;
    l_jJ["missionsClaimed"] = l_oM.m_vMissionsClaimed;
    l_jJ["permanentAtkBonus"] = l_oM.m_iPermanentAtkBonus;
    l_jJ["permanentDefBonus"] = l_oM.m_iPermanentDefBonus;
    l_jJ["permanentHpBonus"] = l_oM.m_iPermanentHpBonus;
    // S3 (schemaVersion 3)
    l_jJ["cycleIndex"] = l_oM.m_iCycleIndex;
    l_jJ["endingsSeen"] = l_oM.m_vEndingsSeen;
    l_jJ["hintsUnlocked"] = l_oM.m_vHintsUnlocked;
    // S5: forging blueprints known -- meta-scope, survives rebirth (see the field's own comment).
    l_jJ["forgeRecipesKnown"] = l_oM.m_vForgeRecipesKnown;
    if (!l_oM.m_mapCards.empty()) l_jJ["cards"] = f_CardBookToJson(l_oM.m_mapCards);   // monster cards (optional: an older file has none)
    if (!l_oM.m_strTrainDay.empty()) { l_jJ["trainDay"] = l_oM.m_strTrainDay; l_jJ["trainRuns"] = l_oM.m_iTrainRuns; }   // the training ground
    return l_jJ;
}

MetaSaveData f_MetaFromJson(const nlohmann::json& l_jIn, bool* l_pVersionMismatch) {
    MetaSaveData l_oM;
    if (!l_jIn.is_object()) { if (l_pVersionMismatch) *l_pVersionMismatch = true; return l_oM; }
    int l_iVer = l_jIn.value("schemaVersion", 0);
    nlohmann::json l_jJ = l_jIn;
    f_MigrateMetaJson(l_jJ);
    if (l_pVersionMismatch) *l_pVersionMismatch = (l_iVer != g_iMetaSaveSchemaVersion);
    l_oM.m_iSchemaVersion = l_iVer;
    if (l_jJ.contains("storyFlags") && l_jJ["storyFlags"].is_array())
        for (auto& l_jF : l_jJ["storyFlags"]) if (l_jF.is_string()) l_oM.m_setStoryFlags.insert(l_jF.get<std::string>());
    l_oM.m_iCurrentBeat = l_jJ.value("currentBeat", 0);
    l_oM.m_vUnlockedStages  = l_jJ.value("unlockedStages", std::vector<std::string>{});
    l_oM.m_vMissionsClaimed = l_jJ.value("missionsClaimed", std::vector<std::string>{});
    l_oM.m_iPermanentAtkBonus = l_jJ.value("permanentAtkBonus", 0);
    l_oM.m_iPermanentDefBonus = l_jJ.value("permanentDefBonus", 0);
    l_oM.m_iPermanentHpBonus  = l_jJ.value("permanentHpBonus", 0);
    // S3: absent in a pre-v3 file -> the declared defaults (cycleIndex 1, empty lists). This is the
    // "must not refuse an old save" rule from docs/story/STORY_DATA_SCHEMA.md section 9; the caller sees
    // *versionMismatch so it can log/migrate, but loading never fails because of it.
    l_oM.m_iCycleIndex = std::max(1, l_jJ.value("cycleIndex", 1));
    l_oM.m_vEndingsSeen    = l_jJ.value("endingsSeen", std::vector<std::string>{});
    l_oM.m_vHintsUnlocked  = l_jJ.value("hintsUnlocked", std::vector<std::string>{});
    // S5: a pre-S5 file simply has none -- empty list is the documented migration default, same
    // rule as every other field above.
    l_oM.m_vForgeRecipesKnown = l_jJ.value("forgeRecipesKnown", std::vector<std::string>{});
    l_oM.m_mapCards = f_CardBookFromJson(l_jJ.value("cards", nlohmann::json::object()));
    l_oM.m_strTrainDay = l_jJ.value("trainDay", std::string());
    l_oM.m_iTrainRuns = std::max(0, l_jJ.value("trainRuns", 0));
    return l_oM;
}

nlohmann::json f_ToJson(const RunSaveData& l_oR) {
    nlohmann::json l_jJ;
    l_jJ["schemaVersion"] = l_oR.m_iSchemaVersion;
    l_jJ["currentStageId"] = l_oR.m_strCurrentStageId;
    nlohmann::json l_jP;
    l_jP["hp"] = l_oR.m_oPlayer.m_iHp; l_jP["maxhp"] = l_oR.m_oPlayer.m_iMaxhp;
    l_jP["atk"] = l_oR.m_oPlayer.m_iAtk; l_jP["def"] = l_oR.m_oPlayer.m_iDef;
    l_jP["gold"] = l_oR.m_oPlayer.m_iGold; l_jP["exp"] = l_oR.m_oPlayer.m_iExp; l_jP["lv"] = l_oR.m_oPlayer.m_iLv;
    l_jP["key_yellow"] = l_oR.m_oPlayer.m_iKeyYellow; l_jP["key_blue"] = l_oR.m_oPlayer.m_iKeyBlue; l_jP["key_red"] = l_oR.m_oPlayer.m_iKeyRed;
    l_jP["inv"] = l_oR.m_oPlayer.m_vInv;
    l_jP["attrs"] = l_oR.m_oPlayer.m_mapAttrs;
    l_jP["gear"] = l_oR.m_oPlayer.m_vGear;
    l_jP["weapon"] = l_oR.m_oPlayer.m_strWeapon; l_jP["armor"] = l_oR.m_oPlayer.m_strArmor; l_jP["talent"] = l_oR.m_oPlayer.m_strTalent;
    l_jP["attrPoints"] = l_oR.m_oPlayer.m_iAttrPoints;
    l_jP["weaponUses"] = l_oR.m_oPlayer.m_mapWeaponUses;
    l_jP["crystals"] = l_oR.m_oPlayer.m_iCrystals;
    l_jJ["player"] = l_jP;
    l_jJ["entityStatus"] = l_oR.m_mapEntityStatus;
    // S3 (schemaVersion 3): the run's story state -- see RunStoryState for what writes/reads it.
    l_jJ["choices"] = l_oR.m_mapChoices;
    l_jJ["counters"] = l_oR.m_mapCounters;
    l_jJ["sideStories"] = l_oR.m_mapSideStories;
    l_jJ["flags"] = l_oR.m_mapFlags;
    l_jJ["floor"] = l_oR.m_strFloor;
    l_jJ["clearedFloors"] = l_oR.m_vClearedFloors;
    l_jJ["shards"] = l_oR.m_vShards;
    l_jJ["deathsTotal"] = l_oR.m_iDeathsTotal;
    l_jJ["deathsNonBoss"] = l_oR.m_iDeathsNonBoss;
    // S4: the skill tree's run-scoped progress -- see RunStoryState for what writes/reads it.
    l_jJ["skillPoints"] = l_oR.m_iSkillPoints;
    l_jJ["skillsOwned"] = l_oR.m_vSkillsOwned;
    l_jJ["eventsStarted"] = l_oR.m_vEventsStarted;
    l_jJ["eventsFinished"] = l_oR.m_vEventsFinished;
    if (!l_oR.m_mapVars.empty()) l_jJ["vars"] = l_oR.m_mapVars;
    if (!l_oR.m_mapNpcTalk.empty()) l_jJ["npcTalk"] = l_oR.m_mapNpcTalk;
    if (!l_oR.m_mapEventCounts.empty()) l_jJ["eventCounts"] = l_oR.m_mapEventCounts;
    l_jJ["superMax"] = l_oR.m_iSuperMax;
    return l_jJ;
}

RunSaveData f_RunFromJson(const nlohmann::json& l_jIn, bool* l_pVersionMismatch) {
    RunSaveData l_oR;
    if (!l_jIn.is_object()) { if (l_pVersionMismatch) *l_pVersionMismatch = true; return l_oR; }
    int l_iVer = l_jIn.value("schemaVersion", 0);
    nlohmann::json l_jJ = l_jIn;
    f_MigrateRunJson(l_jJ);
    if (l_pVersionMismatch) *l_pVersionMismatch = (l_iVer != g_iRunSaveSchemaVersion);
    l_oR.m_iSchemaVersion = l_iVer;
    l_oR.m_strCurrentStageId = l_jJ.value("currentStageId", std::string());
    if (l_jJ.contains("player") && l_jJ["player"].is_object()) {
        auto& l_jP = l_jJ["player"];
        l_oR.m_oPlayer.m_iHp = l_jP.value("hp", 120);
        l_oR.m_oPlayer.m_iMaxhp = l_jP.value("maxhp", 120);
        l_oR.m_oPlayer.m_iAtk = l_jP.value("atk", 12);
        l_oR.m_oPlayer.m_iDef = l_jP.value("def", 4);
        l_oR.m_oPlayer.m_iGold = l_jP.value("gold", 0);
        l_oR.m_oPlayer.m_iExp = l_jP.value("exp", 0);
        l_oR.m_oPlayer.m_iLv = l_jP.value("lv", 1);
        l_oR.m_oPlayer.m_iKeyYellow = l_jP.value("key_yellow", 0);
        l_oR.m_oPlayer.m_iKeyBlue = l_jP.value("key_blue", 0);
        l_oR.m_oPlayer.m_iKeyRed = l_jP.value("key_red", 0);
        l_oR.m_oPlayer.m_vInv = l_jP.value("inv", std::vector<std::string>{});
        if (l_jP.contains("attrs") && l_jP["attrs"].is_object())
            for (auto& [l_strK, l_jV] : l_jP["attrs"].items())
                if (l_jV.is_number_integer()) l_oR.m_oPlayer.m_mapAttrs[l_strK] = l_jV.get<int>();
        l_oR.m_oPlayer.m_vGear = l_jP.value("gear", std::vector<std::string>{});
        l_oR.m_oPlayer.m_strWeapon = l_jP.value("weapon", std::string());
        l_oR.m_oPlayer.m_strArmor = l_jP.value("armor", std::string());
        l_oR.m_oPlayer.m_strTalent = l_jP.value("talent", std::string());
        l_oR.m_oPlayer.m_iAttrPoints = l_jP.value("attrPoints", 0);
        l_oR.m_oPlayer.m_iCrystals = std::max(0, l_jP.value("crystals", 0));
        if (l_jP.contains("weaponUses") && l_jP["weaponUses"].is_object())   // optional: an older save has none (every weapon Lv 1)
            for (auto& [l_strK, l_jV] : l_jP["weaponUses"].items())
                if (l_jV.is_number_integer()) l_oR.m_oPlayer.m_mapWeaponUses[l_strK] = std::max(0, l_jV.get<int>());
    }
    if (l_jJ.contains("entityStatus") && l_jJ["entityStatus"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["entityStatus"].items())
            if (l_jV.is_string()) l_oR.m_mapEntityStatus[l_strK] = l_jV.get<std::string>();
    // S3: a pre-v3 run file simply has none of these; defaults above (floor F01, everything empty)
    // are the documented migration. Loading must never be refused for a version difference.
    if (l_jJ.contains("choices") && l_jJ["choices"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["choices"].items()) if (l_jV.is_string()) l_oR.m_mapChoices[l_strK] = l_jV.get<std::string>();
    if (l_jJ.contains("counters") && l_jJ["counters"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["counters"].items()) if (l_jV.is_number_integer()) l_oR.m_mapCounters[l_strK] = l_jV.get<int>();
    if (l_jJ.contains("sideStories") && l_jJ["sideStories"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["sideStories"].items()) if (l_jV.is_string()) l_oR.m_mapSideStories[l_strK] = l_jV.get<std::string>();
    if (l_jJ.contains("flags") && l_jJ["flags"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["flags"].items()) if (l_jV.is_boolean()) l_oR.m_mapFlags[l_strK] = l_jV.get<bool>();
    l_oR.m_strFloor = l_jJ.value("floor", std::string("F01"));
    l_oR.m_vClearedFloors = l_jJ.value("clearedFloors", std::vector<std::string>{});
    l_oR.m_vShards = l_jJ.value("shards", std::vector<std::string>{});
    l_oR.m_iDeathsTotal = l_jJ.value("deathsTotal", 0);
    l_oR.m_iDeathsNonBoss = l_jJ.value("deathsNonBoss", 0);
    // S4: a pre-S4 file simply has neither -- defaults (0 points, no skills) are the documented
    // migration, same rule as every other S3/S4 field above.
    l_oR.m_iSkillPoints = l_jJ.value("skillPoints", 0);
    l_oR.m_vSkillsOwned = l_jJ.value("skillsOwned", std::vector<std::string>{});
    l_oR.m_vEventsStarted = l_jJ.value("eventsStarted", std::vector<std::string>{});
    l_oR.m_vEventsFinished = l_jJ.value("eventsFinished", std::vector<std::string>{});
    if (l_jJ.contains("vars") && l_jJ["vars"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["vars"].items()) if (l_jV.is_number_integer()) l_oR.m_mapVars[l_strK] = l_jV.get<int>();
    if (l_jJ.contains("eventCounts") && l_jJ["eventCounts"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["eventCounts"].items()) if (l_jV.is_number_integer()) l_oR.m_mapEventCounts[l_strK] = l_jV.get<int>();
    if (l_jJ.contains("npcTalk") && l_jJ["npcTalk"].is_object())
        for (auto& [l_strK, l_jV] : l_jJ["npcTalk"].items())
            if (l_jV.is_array()) { std::vector<int>& l_vT = l_oR.m_mapNpcTalk[l_strK]; for (auto& l_jN : l_jV) if (l_jN.is_number_integer()) l_vT.push_back(l_jN.get<int>()); }
    l_oR.m_iSuperMax = l_jJ.value("superMax", 5);
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
        ok = ok && fsync(fileno(f)) == 0;
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
