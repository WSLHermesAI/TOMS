// Tests that data/events/floor_index.json (written by the HTML editors, tools/web_editors/floor_index.js; the owner's
// Q10 answer) still matches the
// floors: every event a spec lists and every event tile its grid places is in the floor's entry, at the same tiles,
// and the reverse map names the floor. Run in assets/ (tests/CMakeLists.txt). When it fails after editing floors,
// pools or chapters by hand: open the project folder in the stage or event editor (it rebuilds the index)
#include "floor_table.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
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
    return nlohmann::json::parse(l_strText, nullptr, false);
}

int main() {
    const nlohmann::json l_jIndex = f_ReadJson("data/events/floor_index.json");
    CHECK(l_jIndex.is_object() && l_jIndex.contains("floors") && l_jIndex.contains("events"),
          "data/events/floor_index.json exists (open the project folder in an HTML editor to rebuild it)");
    if (!l_jIndex.is_object() || !l_jIndex.contains("floors")) { std::printf("floor_index_test: %d checks, %d failed\n", g_iChecks, g_iFailed); return 1; }
    const nlohmann::json& l_jFloors = l_jIndex["floors"];
    const nlohmann::json& l_jByEvent = l_jIndex["events"];

    const FloorTable l_oTable = FloorTable::f_LoadFromDirectory("data/story/floors");
    CHECK(l_oTable.size() == 70 && (int)l_jFloors.size() == l_oTable.size(), "one entry per floor (%d floors, %zu entries)", l_oTable.size(), l_jFloors.size());
    for (const FloorInfo& l_oF : l_oTable.f_All()) {
        CHECK(l_jFloors.contains(l_oF.m_strId), "%s has an entry", l_oF.m_strId.c_str());
        if (!l_jFloors.contains(l_oF.m_strId)) continue;
        const nlohmann::json& l_jEntry = l_jFloors[l_oF.m_strId];
        std::map<std::string, std::set<std::pair<int, int>>> l_mapIndexed;
        for (const auto& l_jE : l_jEntry["events"]) {
            auto& l_setCells = l_mapIndexed[l_jE.value("id", std::string())];
            for (const auto& l_jT : l_jE["tiles"]) l_setCells.insert({l_jT[0].get<int>(), l_jT[1].get<int>()});
        }
        // what the spec lists
        for (const std::string& l_strId : l_oF.m_vEventIds) {
            CHECK(l_mapIndexed.count(l_strId), "%s lists %s, the index does not", l_oF.m_strId.c_str(), l_strId.c_str());
            CHECK(l_jByEvent.contains(l_strId) && l_jByEvent[l_strId].is_array() &&
                      std::find(l_jByEvent[l_strId].begin(), l_jByEvent[l_strId].end(), l_oF.m_strId) != l_jByEvent[l_strId].end(),
                  "the reverse map names %s for %s", l_oF.m_strId.c_str(), l_strId.c_str());
        }
        // what the grid places
        const nlohmann::json l_jGrid = f_ReadJson("data/" + l_jEntry.value("map", std::string()));
        CHECK(l_jGrid.is_object() && l_jGrid.contains("tiles"), "%s: its map %s loads", l_oF.m_strId.c_str(), l_jEntry.value("map", std::string()).c_str());
        if (!l_jGrid.is_object() || !l_jGrid.contains("tiles")) continue;
        std::map<std::string, std::set<std::pair<int, int>>> l_mapPlaced;
        const auto& l_jLegend = l_jGrid["legend"];
        for (int l_iY = 0; l_iY < (int)l_jGrid["tiles"].size(); l_iY++) {
            const std::string l_strRow = l_jGrid["tiles"][l_iY].get<std::string>();
            for (int l_iX = 0; l_iX < (int)l_strRow.size(); l_iX++) {
                const std::string l_strCh(1, l_strRow[l_iX]);
                if (!l_jLegend.contains(l_strCh)) continue;
                const std::string l_strKind = l_jLegend[l_strCh].get<std::string>();
                if (l_strKind.rfind("event:", 0) == 0) l_mapPlaced[l_strKind.substr(6)].insert({l_iX, l_iY});
            }
        }
        for (const auto& [l_strId, l_setCells] : l_mapPlaced)
            CHECK(l_mapIndexed.count(l_strId) && l_mapIndexed[l_strId] == l_setCells, "%s places %s; the index has other tiles", l_oF.m_strId.c_str(), l_strId.c_str());
        for (const auto& [l_strId, l_setCells] : l_mapIndexed)
            CHECK(l_setCells.empty() || l_mapPlaced.count(l_strId), "the index places %s on %s, the grid does not", l_strId.c_str(), l_oF.m_strId.c_str());
    }

    std::printf("floor_index_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}
