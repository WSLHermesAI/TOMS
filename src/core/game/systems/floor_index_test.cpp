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

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static nlohmann::json readJson(const std::string& path) {
    std::string text;
    if (!vfsReadAll(path, text)) return nlohmann::json();
    return nlohmann::json::parse(text, nullptr, false);
}

int main() {
    const nlohmann::json index = readJson("data/events/floor_index.json");
    CHECK(index.is_object() && index.contains("floors") && index.contains("events"),
          "data/events/floor_index.json exists (open the project folder in an HTML editor to rebuild it)");
    if (!index.is_object() || !index.contains("floors")) { std::printf("floor_index_test: %d checks, %d failed\n", g_checks, g_failed); return 1; }
    const nlohmann::json& floors = index["floors"];
    const nlohmann::json& byEvent = index["events"];

    const FloorTable table = FloorTable::loadFromDirectory("data/story/floors");
    CHECK(table.size() == 70 && (int)floors.size() == table.size(), "one entry per floor (%d floors, %zu entries)", table.size(), floors.size());
    for (const FloorInfo& f : table.all()) {
        CHECK(floors.contains(f.id), "%s has an entry", f.id.c_str());
        if (!floors.contains(f.id)) continue;
        const nlohmann::json& entry = floors[f.id];
        std::map<std::string, std::set<std::pair<int, int>>> indexed;
        for (const auto& e : entry["events"]) {
            auto& cells = indexed[e.value("id", std::string())];
            for (const auto& t : e["tiles"]) cells.insert({t[0].get<int>(), t[1].get<int>()});
        }
        // what the spec lists
        for (const std::string& id : f.eventIds) {
            CHECK(indexed.count(id), "%s lists %s, the index does not", f.id.c_str(), id.c_str());
            CHECK(byEvent.contains(id) && byEvent[id].is_array() &&
                      std::find(byEvent[id].begin(), byEvent[id].end(), f.id) != byEvent[id].end(),
                  "the reverse map names %s for %s", f.id.c_str(), id.c_str());
        }
        // what the grid places
        const nlohmann::json grid = readJson("data/" + entry.value("map", std::string()));
        CHECK(grid.is_object() && grid.contains("tiles"), "%s: its map %s loads", f.id.c_str(), entry.value("map", std::string()).c_str());
        if (!grid.is_object() || !grid.contains("tiles")) continue;
        std::map<std::string, std::set<std::pair<int, int>>> placed;
        const auto& legend = grid["legend"];
        for (int y = 0; y < (int)grid["tiles"].size(); y++) {
            const std::string row = grid["tiles"][y].get<std::string>();
            for (int x = 0; x < (int)row.size(); x++) {
                const std::string ch(1, row[x]);
                if (!legend.contains(ch)) continue;
                const std::string kind = legend[ch].get<std::string>();
                if (kind.rfind("event:", 0) == 0) placed[kind.substr(6)].insert({x, y});
            }
        }
        for (const auto& [id, cells] : placed)
            CHECK(indexed.count(id) && indexed[id] == cells, "%s places %s; the index has other tiles", f.id.c_str(), id.c_str());
        for (const auto& [id, cells] : indexed)
            CHECK(cells.empty() || placed.count(id), "the index places %s on %s, the grid does not", id.c_str(), f.id.c_str());
    }

    std::printf("floor_index_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
