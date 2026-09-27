// save_test.cpp — headless verification of the Meta/Run save schema (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./save_test
#include "save_system.h"
#include <cstdio>
#include <filesystem>

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

    if (g_fail == 0) { printf("save_test: ALL PASS (18 checks)\n"); return 0; }
    printf("save_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
