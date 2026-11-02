// save_test.cpp — headless verification of the Meta/Run save schema (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./save_test
#include "save_system.h"
#include "save_slots.h"
#include "entity_status.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

int main() {
    // 1. Meta save round-trips through JSON (set/vector fields, permanent bonuses).
    {
        MetaSaveData l_oM;
        l_oM.m_setStoryFlags = {"beat_04_scholar_hint_given", "beat_02_tutorial_done"};
        l_oM.m_iCurrentBeat = 4;
        l_oM.m_vUnlockedStages = {"stage_01", "stage_02", "stage_03"};
        l_oM.m_vMissionsClaimed = {"m_intro_once"};
        l_oM.m_iPermanentAtkBonus = 4; l_oM.m_iPermanentDefBonus = 2; l_oM.m_iPermanentHpBonus = 20;

        auto l_jJ = f_ToJson(l_oM);
        bool l_bMismatch = true;
        MetaSaveData l_oM2 = f_MetaFromJson(l_jJ, &l_bMismatch);
        CHECK(!l_bMismatch, "meta round-trip: schemaVersion matches");
        CHECK(l_oM2.m_setStoryFlags == l_oM.m_setStoryFlags, "meta round-trip: storyFlags preserved");
        CHECK(l_oM2.m_iCurrentBeat == 4, "meta round-trip: currentBeat preserved");
        CHECK(l_oM2.m_vUnlockedStages == l_oM.m_vUnlockedStages, "meta round-trip: unlockedStages preserved");
        CHECK(l_oM2.m_vMissionsClaimed == l_oM.m_vMissionsClaimed, "meta round-trip: missionsClaimed preserved");
        CHECK(l_oM2.m_iPermanentAtkBonus == 4 && l_oM2.m_iPermanentDefBonus == 2 && l_oM2.m_iPermanentHpBonus == 20,
              "meta round-trip: permanent bonuses preserved");
    }

    // 2. Run save round-trips, including the nested player struct + entity-status map.
    {
        RunSaveData l_oR;
        l_oR.m_strCurrentStageId = "stage_04";
        l_oR.m_oPlayer.m_iHp = 88; l_oR.m_oPlayer.m_iMaxhp = 130; l_oR.m_oPlayer.m_iAtk = 18; l_oR.m_oPlayer.m_iDef = 7;
        l_oR.m_oPlayer.m_iGold = 42; l_oR.m_oPlayer.m_iExp = 15; l_oR.m_oPlayer.m_iLv = 2;
        l_oR.m_oPlayer.m_iKeyYellow = 1;
        l_oR.m_oPlayer.m_vInv = {"gem_atk", "potion_red"};
        l_oR.m_mapEntityStatus["stage_04|5,3"] = "Defeated";
        l_oR.m_mapEntityStatus["stage_04|2,2"] = "Collected";

        auto l_jJ = f_ToJson(l_oR);
        bool l_bMismatch = true;
        RunSaveData l_oR2 = f_RunFromJson(l_jJ, &l_bMismatch);
        CHECK(!l_bMismatch, "run round-trip: schemaVersion matches");
        CHECK(l_oR2.m_strCurrentStageId == "stage_04", "run round-trip: currentStageId preserved");
        CHECK(l_oR2.m_oPlayer.m_iHp == 88 && l_oR2.m_oPlayer.m_iMaxhp == 130 && l_oR2.m_oPlayer.m_iAtk == 18 && l_oR2.m_oPlayer.m_iDef == 7,
              "run round-trip: player core stats preserved");
        CHECK(l_oR2.m_oPlayer.m_iGold == 42 && l_oR2.m_oPlayer.m_iExp == 15 && l_oR2.m_oPlayer.m_iLv == 2,
              "run round-trip: player gold/exp/level preserved");
        CHECK(l_oR2.m_oPlayer.m_iKeyYellow == 1, "run round-trip: key count preserved");
        CHECK(l_oR2.m_oPlayer.m_vInv == l_oR.m_oPlayer.m_vInv, "run round-trip: inventory preserved");
        CHECK(l_oR2.m_mapEntityStatus.at("stage_04|5,3") == "Defeated", "run round-trip: entity status 1 preserved");
        CHECK(l_oR2.m_mapEntityStatus.at("stage_04|2,2") == "Collected", "run round-trip: entity status 2 preserved");
    }

    // 3. An unrecognized schemaVersion is flagged, not silently accepted.
    {
        nlohmann::json l_jJ;
        l_jJ["schemaVersion"] = 999;
        l_jJ["currentStageId"] = "stage_01";
        bool l_bMismatch = false;
        RunSaveData l_oR = f_RunFromJson(l_jJ, &l_bMismatch);
        CHECK(l_bMismatch, "an unknown schemaVersion (999) is flagged as a mismatch");
        (void)l_oR;
    }

    // 4. Atomic write + safe read round-trip through an actual file on disk, and the .tmp
    //    file must not linger after a successful write.
    {
        std::string l_strPath = "save_test_tmp_meta.json";
        std::error_code l_oEc;
        std::filesystem::remove(l_strPath, l_oEc);
        std::filesystem::remove(l_strPath + ".tmp", l_oEc);

        MetaSaveData l_oM;
        l_oM.m_vUnlockedStages = {"stage_01"};
        CHECK(f_WriteJsonAtomic(l_strPath, f_ToJson(l_oM)), "writeJsonAtomic succeeds");
        CHECK(!std::filesystem::exists(l_strPath + ".tmp"), ".tmp file does not linger after a successful write");
        CHECK(std::filesystem::exists(l_strPath), "target file exists after writeJsonAtomic");

        nlohmann::json l_jJ2 = f_ReadJsonFileSafe(l_strPath);
        CHECK(!l_jJ2.is_null(), "readJsonFileSafe reads back the written file");
        MetaSaveData l_oM2 = f_MetaFromJson(l_jJ2);
        CHECK(l_oM2.m_vUnlockedStages == l_oM.m_vUnlockedStages, "file round-trip: unlockedStages preserved");

        std::filesystem::remove(l_strPath, l_oEc);
    }

    // 5. Reading a missing file returns a null json rather than throwing/crashing.
    {
        nlohmann::json l_jJ = f_ReadJsonFileSafe("does_not_exist_1234.json");
        CHECK(l_jJ.is_null(), "reading a nonexistent file returns a null json, not a throw");
    }

    // 6-10: the data-loss guards (docs/gameplay/05_SAVE_SYSTEM.md), on slots in a scratch folder.
    const std::string l_strDir = "save_test_slots";
    std::error_code l_oEc;
    std::filesystem::remove_all(l_strDir, l_oEc);
    MetaSaveData l_oMeta;
    RunSaveData l_oRun;
    l_oRun.m_strCurrentStageId = "F02";
    const auto l_fnOverwriteWith = [](const std::string& l_strPath, const std::string& l_strText) {
        std::ofstream l_oF(l_strPath, std::ios::binary | std::ios::trunc);
        l_oF << l_strText;
    };

    // 6. A second save keeps the first one as the backup.
    {
        l_oRun.m_oPlayer.m_iGold = 10;
        CHECK(f_WriteSlotSave(l_strDir, 1, l_oMeta, l_oRun, 60, "2026-10-18 10:00:00", "F02"), "slot save 1 written");
        l_oRun.m_oPlayer.m_iGold = 20;
        CHECK(f_WriteSlotSave(l_strDir, 1, l_oMeta, l_oRun, 70, "2026-10-18 10:01:00", "F02"), "slot save 2 written");
        nlohmann::json l_jBak = f_ReadJsonFileSafe(f_BackupPath(f_SlotPath(l_strDir, 1)));
        CHECK(l_jBak.is_object() && l_jBak["run"]["player"].value("gold", 0) == 10, "the backup holds the save before the last one");
        CHECK(!std::filesystem::exists(f_SlotPath(l_strDir, 1) + ".tmp"), "no .tmp left behind");
    }

    // 7. A damaged slot file falls back to its backup, and is reported as such.
    {
        l_fnOverwriteWith(f_SlotPath(l_strDir, 1), "{\"saveVersion\": 1, \"run\": {");   // cut off mid-write
        MetaSaveData l_oM2;
        RunSaveData l_oR2;
        SlotLoadInfo l_oInfo;
        CHECK(f_ReadSlotSave(l_strDir, 1, l_oM2, l_oR2, nullptr, nullptr, &l_oInfo), "a damaged slot loads from its backup");
        CHECK(l_oInfo.m_bFromBackup && l_oR2.m_oPlayer.m_iGold == 10, "the backup's progress is what loads");
        SlotSummary l_oS = f_SummarizeSlot(l_strDir, 1);
        CHECK(l_oS.m_bExists && l_oS.m_bFromBackup && !l_oS.m_bDamaged, "the Continue list shows it, marked as from the backup");
        // the next save does not push the good backup out with the damaged file
        l_oRun.m_oPlayer.m_iGold = 30;
        CHECK(f_WriteSlotSave(l_strDir, 1, l_oMeta, l_oRun, 80, "2026-10-18 10:02:00", "F02"), "saving over a damaged file works");
        CHECK(f_ReadJsonFileSafe(f_BackupPath(f_SlotPath(l_strDir, 1)))["run"]["player"].value("gold", 0) == 10,
              "a damaged file never replaces the backup");
    }

    // 8. File and backup both unreadable: "damaged", never "empty" (so no new game is offered over it).
    {
        l_fnOverwriteWith(f_SlotPath(l_strDir, 2), "not json");
        SlotSummary l_oS = f_SummarizeSlot(l_strDir, 2);
        CHECK(!l_oS.m_bExists && l_oS.m_bDamaged, "an unreadable slot is damaged, not empty");
        CHECK(f_FirstEmptySlot(l_strDir, 3) == 3, "a damaged slot is not picked as the first empty one");
        CHECK(!f_SummarizeSlot(l_strDir, 3).m_bDamaged, "a slot with no file is just empty");
    }

    // 9. A save from a newer version loads, but is never written over.
    {
        nlohmann::json l_jJ = f_ReadJsonFileSafe(f_SlotPath(l_strDir, 1));
        l_jJ["run"]["schemaVersion"] = g_iRunSaveSchemaVersion + 1;
        l_jJ["run"]["futureField"] = 7;
        f_WriteJsonAtomic(f_SlotPath(l_strDir, 1), l_jJ);
        CHECK(f_SlotIsNewer(l_strDir, 1) && f_SummarizeSlot(l_strDir, 1).m_bNewer, "a newer save is recognised");
        MetaSaveData l_oM2;
        RunSaveData l_oR2;
        SlotLoadInfo l_oInfo;
        CHECK(f_ReadSlotSave(l_strDir, 1, l_oM2, l_oR2, nullptr, nullptr, &l_oInfo) && l_oInfo.m_bNewer, "a newer save still loads");
        CHECK(!f_WriteSlotSave(l_strDir, 1, l_oMeta, l_oRun, 90, "", "F02"), "a newer save is not overwritten");
        CHECK(f_ReadJsonFileSafe(f_SlotPath(l_strDir, 1))["run"].value("futureField", 0) == 7, "its unknown fields are kept");
    }

    // 10. Migration: an old file (version 1, fields missing) loads with defaults; an object's save key follows its uid.
    {
        nlohmann::json l_jOld = {{"schemaVersion", 1}, {"currentStageId", "stage01"}, {"player", {{"hp", 50}}}};
        bool l_bMismatch = false;
        RunSaveData l_oR2 = f_RunFromJson(l_jOld, &l_bMismatch);
        CHECK(l_bMismatch && l_oR2.m_oPlayer.m_iHp == 50 && l_oR2.m_strFloor == "F01" && l_oR2.m_iSuperMax == 5, "a version-1 run loads with defaults");
        CHECK(f_EntityStatusKey("F03", 4, 5, "o12") == "F03#o12", "an object with a uid is keyed by it");
        CHECK(f_EntityStatusKey("F03", 4, 5, "") == "F03|4,5", "an object without one is keyed by its position");
        f_DeleteSlot(l_strDir, 1);
        CHECK(!std::filesystem::exists(f_BackupPath(f_SlotPath(l_strDir, 1))), "deleting a slot removes its backup too");
    }
    std::filesystem::remove_all(l_strDir, l_oEc);

    if (g_iFail == 0) { printf("save_test: ALL PASS\n"); return 0; }
    printf("save_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
