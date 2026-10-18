// save_test.cpp — headless verification of the Meta/Run save schema (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./save_test
#include "save_system.h"
#include "save_slots.h"
#include "entity_status.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    // 1. Meta save round-trips through JSON (set/vector fields, permanent bonuses).
    {
        MetaSaveData m;
        m.storyFlags = {"beat_04_scholar_hint_given", "beat_02_tutorial_done"};
        m.currentBeat = 4;
        m.unlockedStages = {"stage_01", "stage_02", "stage_03"};
        m.missionsClaimed = {"m_intro_once"};
        m.permanentAtkBonus = 4; m.permanentDefBonus = 2; m.permanentHpBonus = 20;

        auto j = toJson(m);
        bool mismatch = true;
        MetaSaveData m2 = metaFromJson(j, &mismatch);
        CHECK(!mismatch, "meta round-trip: schemaVersion matches");
        CHECK(m2.storyFlags == m.storyFlags, "meta round-trip: storyFlags preserved");
        CHECK(m2.currentBeat == 4, "meta round-trip: currentBeat preserved");
        CHECK(m2.unlockedStages == m.unlockedStages, "meta round-trip: unlockedStages preserved");
        CHECK(m2.missionsClaimed == m.missionsClaimed, "meta round-trip: missionsClaimed preserved");
        CHECK(m2.permanentAtkBonus == 4 && m2.permanentDefBonus == 2 && m2.permanentHpBonus == 20,
              "meta round-trip: permanent bonuses preserved");
    }

    // 2. Run save round-trips, including the nested player struct + entity-status map.
    {
        RunSaveData r;
        r.currentStageId = "stage_04";
        r.player.hp = 88; r.player.maxhp = 130; r.player.atk = 18; r.player.def = 7;
        r.player.gold = 42; r.player.exp = 15; r.player.lv = 2;
        r.player.key_yellow = 1;
        r.player.inv = {"gem_atk", "potion_red"};
        r.entityStatus["stage_04|5,3"] = "Defeated";
        r.entityStatus["stage_04|2,2"] = "Collected";

        auto j = toJson(r);
        bool mismatch = true;
        RunSaveData r2 = runFromJson(j, &mismatch);
        CHECK(!mismatch, "run round-trip: schemaVersion matches");
        CHECK(r2.currentStageId == "stage_04", "run round-trip: currentStageId preserved");
        CHECK(r2.player.hp == 88 && r2.player.maxhp == 130 && r2.player.atk == 18 && r2.player.def == 7,
              "run round-trip: player core stats preserved");
        CHECK(r2.player.gold == 42 && r2.player.exp == 15 && r2.player.lv == 2,
              "run round-trip: player gold/exp/level preserved");
        CHECK(r2.player.key_yellow == 1, "run round-trip: key count preserved");
        CHECK(r2.player.inv == r.player.inv, "run round-trip: inventory preserved");
        CHECK(r2.entityStatus.at("stage_04|5,3") == "Defeated", "run round-trip: entity status 1 preserved");
        CHECK(r2.entityStatus.at("stage_04|2,2") == "Collected", "run round-trip: entity status 2 preserved");
    }

    // 3. An unrecognized schemaVersion is flagged, not silently accepted.
    {
        nlohmann::json j;
        j["schemaVersion"] = 999;
        j["currentStageId"] = "stage_01";
        bool mismatch = false;
        RunSaveData r = runFromJson(j, &mismatch);
        CHECK(mismatch, "an unknown schemaVersion (999) is flagged as a mismatch");
        (void)r;
    }

    // 4. Atomic write + safe read round-trip through an actual file on disk, and the .tmp
    //    file must not linger after a successful write.
    {
        std::string path = "save_test_tmp_meta.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path + ".tmp", ec);

        MetaSaveData m;
        m.unlockedStages = {"stage_01"};
        CHECK(writeJsonAtomic(path, toJson(m)), "writeJsonAtomic succeeds");
        CHECK(!std::filesystem::exists(path + ".tmp"), ".tmp file does not linger after a successful write");
        CHECK(std::filesystem::exists(path), "target file exists after writeJsonAtomic");

        nlohmann::json j2 = readJsonFileSafe(path);
        CHECK(!j2.is_null(), "readJsonFileSafe reads back the written file");
        MetaSaveData m2 = metaFromJson(j2);
        CHECK(m2.unlockedStages == m.unlockedStages, "file round-trip: unlockedStages preserved");

        std::filesystem::remove(path, ec);
    }

    // 5. Reading a missing file returns a null json rather than throwing/crashing.
    {
        nlohmann::json j = readJsonFileSafe("does_not_exist_1234.json");
        CHECK(j.is_null(), "reading a nonexistent file returns a null json, not a throw");
    }

    // 6-10: the data-loss guards (docs/28_SAVE_SYSTEM.md), on slots in a scratch folder.
    const std::string dir = "save_test_slots";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    MetaSaveData meta;
    RunSaveData run;
    run.currentStageId = "F02";
    const auto overwriteWith = [](const std::string& path, const std::string& text) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << text;
    };

    // 6. A second save keeps the first one as the backup.
    {
        run.player.gold = 10;
        CHECK(writeSlotSave(dir, 1, meta, run, 60, "2026-10-18 10:00:00", "F02"), "slot save 1 written");
        run.player.gold = 20;
        CHECK(writeSlotSave(dir, 1, meta, run, 70, "2026-10-18 10:01:00", "F02"), "slot save 2 written");
        nlohmann::json bak = readJsonFileSafe(backupPath(slotPath(dir, 1)));
        CHECK(bak.is_object() && bak["run"]["player"].value("gold", 0) == 10, "the backup holds the save before the last one");
        CHECK(!std::filesystem::exists(slotPath(dir, 1) + ".tmp"), "no .tmp left behind");
    }

    // 7. A damaged slot file falls back to its backup, and is reported as such.
    {
        overwriteWith(slotPath(dir, 1), "{\"saveVersion\": 1, \"run\": {");   // cut off mid-write
        MetaSaveData m2;
        RunSaveData r2;
        SlotLoadInfo info;
        CHECK(readSlotSave(dir, 1, m2, r2, nullptr, nullptr, &info), "a damaged slot loads from its backup");
        CHECK(info.fromBackup && r2.player.gold == 10, "the backup's progress is what loads");
        SlotSummary s = summarizeSlot(dir, 1);
        CHECK(s.exists && s.fromBackup && !s.damaged, "the Continue list shows it, marked as from the backup");
        // the next save does not push the good backup out with the damaged file
        run.player.gold = 30;
        CHECK(writeSlotSave(dir, 1, meta, run, 80, "2026-10-18 10:02:00", "F02"), "saving over a damaged file works");
        CHECK(readJsonFileSafe(backupPath(slotPath(dir, 1)))["run"]["player"].value("gold", 0) == 10,
              "a damaged file never replaces the backup");
    }

    // 8. File and backup both unreadable: "damaged", never "empty" (so no new game is offered over it).
    {
        overwriteWith(slotPath(dir, 2), "not json");
        SlotSummary s = summarizeSlot(dir, 2);
        CHECK(!s.exists && s.damaged, "an unreadable slot is damaged, not empty");
        CHECK(firstEmptySlot(dir, 3) == 3, "a damaged slot is not picked as the first empty one");
        CHECK(!summarizeSlot(dir, 3).damaged, "a slot with no file is just empty");
    }

    // 9. A save from a newer version loads, but is never written over.
    {
        nlohmann::json j = readJsonFileSafe(slotPath(dir, 1));
        j["run"]["schemaVersion"] = kRunSaveSchemaVersion + 1;
        j["run"]["futureField"] = 7;
        writeJsonAtomic(slotPath(dir, 1), j);
        CHECK(slotIsNewer(dir, 1) && summarizeSlot(dir, 1).newer, "a newer save is recognised");
        MetaSaveData m2;
        RunSaveData r2;
        SlotLoadInfo info;
        CHECK(readSlotSave(dir, 1, m2, r2, nullptr, nullptr, &info) && info.newer, "a newer save still loads");
        CHECK(!writeSlotSave(dir, 1, meta, run, 90, "", "F02"), "a newer save is not overwritten");
        CHECK(readJsonFileSafe(slotPath(dir, 1))["run"].value("futureField", 0) == 7, "its unknown fields are kept");
    }

    // 10. Migration: an old file (version 1, fields missing) loads with defaults; an object's save key follows its uid.
    {
        nlohmann::json old = {{"schemaVersion", 1}, {"currentStageId", "stage01"}, {"player", {{"hp", 50}}}};
        bool mismatch = false;
        RunSaveData r2 = runFromJson(old, &mismatch);
        CHECK(mismatch && r2.player.hp == 50 && r2.floor == "F01" && r2.superMax == 5, "a version-1 run loads with defaults");
        CHECK(entityStatusKey("F03", 4, 5, "o12") == "F03#o12", "an object with a uid is keyed by it");
        CHECK(entityStatusKey("F03", 4, 5, "") == "F03|4,5", "an object without one is keyed by its position");
        deleteSlot(dir, 1);
        CHECK(!std::filesystem::exists(backupPath(slotPath(dir, 1))), "deleting a slot removes its backup too");
    }
    std::filesystem::remove_all(dir, ec);

    if (g_fail == 0) { printf("save_test: ALL PASS\n"); return 0; }
    printf("save_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
