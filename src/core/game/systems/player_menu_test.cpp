// Tests for the player menu (docs/gameplay/03_PLAYER_MENU.md): its rules (player_menu_rules.h) and the data it
// reads -- data/items.json (the all-item file), data/store.json, data/stats.json, the chapters' and
// missions' titles, and their text in data/text.json. Run in assets/ (tests/CMakeLists.txt).
#include "player_menu_rules.h"
#include "equipment_system.h"
#include "run_state.h"
#include "save_system.h"
#include "vfs.h"

#include <json.hpp>

#include <cstdio>
#include <set>
#include <string>

using namespace toms;

static int g_iChecks = 0;
static int g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static nlohmann::json f_ReadJson(const std::string& l_strPath) {
    std::string l_strText;
    if (!f_VfsReadAll(l_strPath, l_strText)) return nlohmann::json();
    try { return nlohmann::json::parse(l_strText); } catch (...) { return nlohmann::json(); }
}

int main() {
    // ---- stacking ----
    {
        auto l_vS = f_StackItems({"potion_red", "gem_atk", "potion_red", "potion_red", "scroll"});
        CHECK(l_vS.size() == 3, "3 different items, got %zu", l_vS.size());
        CHECK(l_vS.size() == 3 && l_vS[0].m_strId == "potion_red" && l_vS[0].m_iCount == 3, "first picked up first, with its count");
        CHECK(l_vS.size() == 3 && l_vS[1].m_strId == "gem_atk" && l_vS[1].m_iCount == 1, "a single copy is x1");
        CHECK(f_StackItems({}).empty(), "an empty backpack has no cells");
    }
    // ---- the 3-per-row cursor ----
    {
        int l_iSel = 0;
        CHECK(f_GridMove(l_iSel, 7, 1, 0) && l_iSel == 1, "right moves one cell");
        CHECK(f_GridMove(l_iSel, 7, 0, 1) && l_iSel == 4, "down moves one row (3 cells)");
        l_iSel = 4;
        CHECK(f_GridMove(l_iSel, 7, 0, 1) && l_iSel == 6, "down into a short last row lands on its last cell");
        l_iSel = 3;
        CHECK(!f_GridMove(l_iSel, 7, -1, 0) && l_iSel == 3, "left off the first column leaves the grid (switch filter)");
        l_iSel = 2;
        CHECK(!f_GridMove(l_iSel, 7, 1, 0) && l_iSel == 2, "right off the last column leaves the grid");
        l_iSel = 6;
        CHECK(!f_GridMove(l_iSel, 7, 1, 0), "right off the very last cell leaves the grid");
        l_iSel = 1;
        CHECK(f_GridMove(l_iSel, 7, 0, -1) && l_iSel == 1, "up on the first row stays");
        l_iSel = 0;
        CHECK(!f_GridMove(l_iSel, 0, 1, 0) && f_GridMove(l_iSel, 0, 0, 1), "an empty grid: sideways switches, up/down does nothing");
    }
    // ---- dropping ----
    CHECK(f_CanDrop(false, false), "an ordinary item can be dropped");
    CHECK(!f_CanDrop(true, false), "an important item cannot");
    CHECK(!f_CanDrop(false, true), "worn gear cannot");
    // ---- level-ups (lv * expPerLevel per level) ----
    {
        int l_iExp = 29, l_iLv = 1;
        CHECK(f_LevelUps(l_iExp, l_iLv, 30) == 0 && l_iLv == 1 && l_iExp == 29, "29 EXP is not a level at 30 per level");
        l_iExp = 30 + 60 + 5; l_iLv = 1;
        CHECK(f_LevelUps(l_iExp, l_iLv, 30) == 2 && l_iLv == 3 && l_iExp == 5, "95 EXP: Lv 1 -> 3 with 5 left, got lv %d exp %d", l_iLv, l_iExp);
        l_iExp = 10; l_iLv = 1;
        CHECK(f_LevelUps(l_iExp, l_iLv, 0) == 4 && l_iLv == 5 && l_iExp == 0, "expPerLevel 0 is read as 1 (1+2+3+4 = 10 EXP: Lv 5)");
        l_iExp = 10; l_iLv = 1;
        CHECK(f_LevelUps(l_iExp, l_iLv, 0, 3) == 2 && l_iLv == 3 && l_iExp == 7, "the cap: Lv 3 at most, the EXP past it kept");
        l_iExp = 1000; l_iLv = 99;
        CHECK(f_LevelUps(l_iExp, l_iLv, 1, 99) == 0 && l_iLv == 99, "Lv 99 is the top");
    }
    // ---- attribute points by level (stats.json levelUp.points) ----
    {
        const std::vector<LevelPoints> l_vT = {{20, 4}, {2, 3}, {50, 5}};
        CHECK(f_PointsForLevel(l_vT, 2, 0) == 3 && f_PointsForLevel(l_vT, 19, 0) == 3, "Lv 2-19: 3 points");
        CHECK(f_PointsForLevel(l_vT, 20, 0) == 4 && f_PointsForLevel(l_vT, 49, 0) == 4, "Lv 20-49: 4 points (rows in any order)");
        CHECK(f_PointsForLevel(l_vT, 99, 0) == 5, "Lv 50+: 5 points");
        CHECK(f_PointsForLevel(l_vT, 1, 7) == 7 && f_PointsForLevel({}, 10, 2) == 2, "below every row, or no table: the fallback");
        CHECK(f_PointsForLevel({{2, -1}}, 5, 0) == 0, "never negative");
    }

    // ---- the data ----
    const nlohmann::json l_jText = f_ReadJson("data/text.json");
    const nlohmann::json& l_jStrings = l_jText.is_object() && l_jText.contains("strings") ? l_jText["strings"] : nlohmann::json();
    CHECK(l_jStrings.is_object(), "data/text.json has strings");
    // English is the language every text must have (the owner, 2026-10-12: English only until translations are
    // asked for; every other language falls back to it), and it must really be English, not a copy of the Chinese.
    auto l_fnHasText = [&](const std::string& l_strKey) {
        if (!l_jStrings.is_object() || !l_jStrings.contains(l_strKey) || !l_jStrings[l_strKey].contains("en") || !l_jStrings[l_strKey]["en"].is_string())
            return false;
        for (unsigned char l_u8C : l_jStrings[l_strKey]["en"].get<std::string>())
            if (l_u8C >= 0xE3 && l_u8C <= 0xE9) return false;   // the lead byte of a CJK character (U+3000..U+9FFF)
        return true;
    };

    const nlohmann::json l_jStats = f_ReadJson("data/stats.json");
    std::set<std::string> l_setAttrIds;
    CHECK(l_jStats.contains("attributes") && l_jStats["attributes"].is_array() && !l_jStats["attributes"].empty(), "data/stats.json lists attributes");
    if (l_jStats.contains("attributes"))
        for (const auto& l_jA : l_jStats["attributes"]) {
            const std::string l_strId = l_jA.value("id", std::string());
            CHECK(!l_strId.empty() && l_setAttrIds.insert(l_strId).second, "attribute ids are present and unique ('%s')", l_strId.c_str());
            CHECK(l_fnHasText(l_jA.value("name", std::string())) && l_fnHasText(l_jA.value("desc", std::string())),
                  "attribute '%s' has its name and description in text.json (English)", l_strId.c_str());
            // P3: what a point does -- only stats Game::attrEffect's callers read, each with its Status-tab label
            static const std::set<std::string> l_setEffectStats = {"atk", "def", "maxhp", "hitZone", "cooldownMs", "skillPower", "goldGain"};
            if (l_jA.contains("effects"))
                for (const auto& l_jE : l_jA["effects"]) {
                    const std::string l_strStat = l_jE.value("stat", std::string());
                    CHECK(l_setEffectStats.count(l_strStat), "attribute '%s': effect stat '%s' is not one the game applies", l_strId.c_str(), l_strStat.c_str());
                    CHECK(l_jE.contains("per") && l_jE["per"].is_number(), "attribute '%s': effect '%s' has a number 'per'", l_strId.c_str(), l_strStat.c_str());
                    CHECK(l_fnHasText("stat.effect." + l_strStat), "the Status tab has a label for effect '%s'", l_strStat.c_str());
                }
        }
    for (const char* l_pszNeed : {"str", "dex", "agi"})
        CHECK(l_setAttrIds.count(l_pszNeed), "the owner asked for %s (Q11)", l_pszNeed);
    CHECK(l_jStats.contains("levelUp") && l_jStats["levelUp"].value("expPerLevel", 0) > 0, "levelUp.expPerLevel is set");

    const nlohmann::json l_jItems = f_ReadJson("data/items.json");
    CHECK(l_jItems.is_object() && l_jItems.size() > 10, "data/items.json holds the items");
    const std::set<std::string> l_setTypes = {"consumable", "key", "weapon", "armor", "talent", "story"};
    std::set<std::string> l_setStatKeys = {"hp", "maxhp", "atk", "def", "gold", "exp", "key_yellow", "key_blue", "key_red", "warp"};
    l_setStatKeys.insert(l_setAttrIds.begin(), l_setAttrIds.end());
    int l_iGear = 0;
    for (auto l_oIt = l_jItems.begin(); l_oIt != l_jItems.end(); ++l_oIt) {
        const std::string l_strId = l_oIt.key();
        if (l_strId.empty() || l_strId[0] == '_') continue;
        const nlohmann::json& l_jD = l_oIt.value();
        const std::string l_strType = l_jD.value("type", std::string());
        CHECK(l_setTypes.count(l_strType), "item '%s': unknown type '%s'", l_strId.c_str(), l_strType.c_str());
        CHECK(l_jD.contains("price") && l_jD["price"].is_number_integer() && l_jD["price"].get<int>() >= 0, "item '%s' has a price", l_strId.c_str());
        CHECK(l_jD.contains("sprite") && !l_jD.value("sprite", std::string()).empty(), "item '%s' has a sprite", l_strId.c_str());
        CHECK(l_fnHasText(l_jD.value("name", std::string())), "item '%s': its name key is in text.json", l_strId.c_str());
        CHECK(l_fnHasText(l_jD.value("desc", std::string())), "item '%s': its desc key is in text.json", l_strId.c_str());
        if (l_jD.contains("stats"))
            for (auto l_oS = l_jD["stats"].begin(); l_oS != l_jD["stats"].end(); ++l_oS)
                CHECK(l_setStatKeys.count(l_oS.key()), "item '%s': stat '%s' is not one the game applies", l_strId.c_str(), l_oS.key().c_str());
        if (l_strType == "key") CHECK(l_jD.value("important", false), "key '%s' is important (never dropped)", l_strId.c_str());
        if (l_strType == "weapon" || l_strType == "armor" || l_strType == "talent") {
            ++l_iGear;
            const EquipmentDefinition l_oE = f_EquipmentFromJson(l_jD);
            const EquipmentSlot l_eWant = l_strType == "weapon" ? EquipmentSlot::Weapon : l_strType == "armor" ? EquipmentSlot::Armor : EquipmentSlot::Talent;
            CHECK(l_oE.m_eSlot == l_eWant, "gear '%s' reads as its own slot", l_strId.c_str());
        }
    }
    CHECK(l_iGear >= 9, "the weapons, armor and talents of the old equipment.json are in items.json (%d)", l_iGear);
    {
        const EquipmentDefinition l_oD = f_EquipmentFromJson(l_jItems.value("war_hammer", nlohmann::json()));
        CHECK(l_oD.m_iStatAtk == 8, "war_hammer still gives ATK +8, got %d", l_oD.m_iStatAtk);
    }

    const nlohmann::json l_jStore = f_ReadJson("data/store.json");
    CHECK(l_jStore.contains("items") && l_jStore["items"].is_array() && !l_jStore["items"].empty(), "data/store.json sells something");
    if (l_jStore.contains("items"))
        for (const auto& l_jE : l_jStore["items"]) {
            const std::string l_strId = l_jE.value("item", std::string());
            CHECK(l_jItems.contains(l_strId), "the store sells '%s', which is not in items.json", l_strId.c_str());
            CHECK(l_jE.value("growth", 1) >= 1, "store entry '%s': growth >= 1", l_strId.c_str());
        }

    // The Events tab: every chapter and mission has a title and a description (Q14).
    for (const char* l_pszCh : {"ch_01", "ch_02", "ch_03", "ch_04"}) {
        const nlohmann::json l_jC = f_ReadJson(std::string("data/story/chapters/") + l_pszCh + ".json");
        CHECK(l_fnHasText(l_jC.value("title", std::string())) && l_fnHasText(l_jC.value("desc", std::string())),
              "chapter %s has its title and description in text.json", l_pszCh);
    }
    const nlohmann::json l_jMissions = f_ReadJson("data/missions.json");
    for (auto l_oIt = l_jMissions.begin(); l_oIt != l_jMissions.end(); ++l_oIt)
        CHECK(l_fnHasText(l_oIt.value().value("title", std::string())) && l_fnHasText(l_oIt.value().value("desc", std::string())),
              "mission %s has its title and description in text.json", l_oIt.key().c_str());

    // ---- P2: the event log's run state (started / finished, in order, saved with the run) ----
    {
        RunStoryState l_oRun;
        l_oRun.f_MarkEventStarted("ev_a");
        l_oRun.f_MarkEventFinished("ev_b");                 // finishing also starts
        l_oRun.f_MarkEventFinished("ev_a");
        l_oRun.f_MarkEventStarted("ev_a");                  // twice: no duplicate
        CHECK(l_oRun.f_EventStarted("ev_b") && l_oRun.f_EventFinished("ev_b"), "a finished event counts as started");
        CHECK(l_oRun.f_EventsStarted().size() == 2 && l_oRun.f_EventsStarted()[0] == "ev_a", "started keeps the order and no duplicates");
        CHECK(l_oRun.f_EventsFinished().size() == 2 && l_oRun.f_EventsFinished()[0] == "ev_b" && l_oRun.f_EventsFinished()[1] == "ev_a",
              "finished keeps the order events fired in");
        RunSaveData l_oR;
        l_oRun.f_WriteInto(l_oR);
        RunStoryState l_oLoaded;
        l_oLoaded.f_ReadFrom(f_RunFromJson(f_ToJson(l_oR)));
        CHECK(l_oLoaded.f_EventFinished("ev_a") && l_oLoaded.f_EventFinished("ev_b") && l_oLoaded.f_EventsFinished()[0] == "ev_b",
              "the event log survives a save and load");
        nlohmann::json l_jOld = f_ToJson(l_oR);
        l_jOld.erase("eventsStarted");                    // a save made before the event log kept "started"
        RunStoryState l_oOlder;
        l_oOlder.f_ReadFrom(f_RunFromJson(l_jOld));
        CHECK(l_oOlder.f_EventStarted("ev_a") && l_oOlder.f_EventStarted("ev_b"), "finished events count as started in an older save");
        l_oLoaded.f_Reset();
        CHECK(l_oLoaded.f_EventsStarted().empty() && l_oLoaded.f_EventsFinished().empty(), "a new run starts with an empty event log");
    }
    // ---- P2: every pool event has its title and description; links name real events ----
    {
        std::set<std::string> l_setIds;
        std::vector<nlohmann::json> l_vAll;
        for (const char* l_pszPool : {"pool_act01", "pool_act02", "pool_act03", "pool_act04", "pool_act05", "pool_act06",
                                 "pool_act07", "pool_act08", "pool_act09", "pool_act10", "pool_common"}) {
            const nlohmann::json l_jP = f_ReadJson(std::string("data/events/") + l_pszPool + ".json");
            CHECK(l_jP.contains("events") && l_jP["events"].is_array(), "data/events/%s.json has events", l_pszPool);
            if (l_jP.contains("events")) for (const auto& l_jE : l_jP["events"]) { l_setIds.insert(l_jE.value("eventId", std::string())); l_vAll.push_back(l_jE); }
        }
        for (const auto& l_jE : l_vAll) {
            const std::string l_strId = l_jE.value("eventId", std::string());
            CHECK(l_fnHasText(l_jE.value("title", l_strId + ".title")) && l_fnHasText(l_jE.value("desc", l_strId + ".desc")) &&
                      l_fnHasText(l_jE.value("text", l_strId + ".text")),
                  "event %s has its title, description and text in English in text.json", l_strId.c_str());
            if (l_jE.contains("next"))
                for (const auto& l_jN : l_jE["next"])
                    CHECK(l_jN.is_string() && l_setIds.count(l_jN.get<std::string>()), "event %s: a next id is not an event", l_strId.c_str());
        }
        CHECK(l_vAll.size() >= 126, "the eleven pools hold the events (%zu)", l_vAll.size());
        // Q10 (a): every event a floor lists is a real event (the _v2.._v5 variants included; W22)
        for (int l_iF = 1; l_iF <= 70; l_iF++) {
            char l_pszName[48];
            std::snprintf(l_pszName, sizeof l_pszName, "data/story/floors/F%02d.json", l_iF);
            const nlohmann::json l_jSpec = f_ReadJson(l_pszName);
            if (l_jSpec.contains("events"))
                for (const auto& l_jE : l_jSpec["events"])
                    CHECK(l_jE.is_string() && l_setIds.count(l_jE.get<std::string>()), "F%02d lists event %s, which no pool defines", l_iF,
                          l_jE.is_string() ? l_jE.get<std::string>().c_str() : "?");
        }
    }

    std::printf("player_menu_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}
