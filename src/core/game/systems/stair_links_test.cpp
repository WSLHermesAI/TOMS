// Tests for stair_links.h: a floor's stairs list, its tiles, locks and arrival (docs/gameplay/event_editor/05_FLOOR_LINKS.md,
// the owner's Q7 answer). Pure data; the shipped floor specs are checked too (run in assets/, tests/CMakeLists.txt).
#include "stair_links.h"
#include "floor_table.h"
#include "vfs.h"

#include <json.hpp>

#include <cstdio>
#include <set>
#include <string>

using namespace toms;

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    // ---- the spec's list ----
    const nlohmann::json spec = nlohmann::json::parse(R"([
        { "dir": "up", "to": "F51" },
        { "dir": "down", "to": "F49" },
        { "dir": "up", "to": "F96", "unlock": { "eventDone": "ev_common_relic_road" } },
        { "dir": "up" },
        { "dir": "up", "to": "F97", "unlock": { "eventsDone": ["ev_a", "ev_b", "ev_a", ""] } },
        { "dir": "up", "to": "F98", "unlock": { "eventDone": "ev_a", "eventsDone": ["ev_b", "ev_a"] } }
    ])");
    std::vector<StairLink> links = stairLinksFromJson(spec);
    CHECK(links.size() == 5, "an entry without `to` is dropped (%zu)", links.size());
    CHECK(links[0].up && links[0].to == "F51" && links[0].unlockEvents.empty(), "an open stair up");
    CHECK(!links[1].up && links[1].to == "F49", "a stair down");
    CHECK(links[2].unlockEvents == std::vector<std::string>{"ev_common_relic_road"}, "a stair locked by an event");
    // W38: several events, all needed -- a list, repeats and empty ids dropped; eventDone first, then eventsDone
    CHECK((links[3].unlockEvents == std::vector<std::string>{"ev_a", "ev_b"}), "eventsDone: several events, no repeats");
    CHECK((links[4].unlockEvents == std::vector<std::string>{"ev_a", "ev_b"}), "eventDone + eventsDone together");
    CHECK(unlockEventsFromJson(nlohmann::json::object()).empty(), "an empty unlock: open");
    CHECK(stairLinksFromJson(nlohmann::json::object()).empty(), "not a list: no stairs");

    const std::vector<StairLink> chain = defaultStairLinks("F02", "");
    CHECK(chain.size() == 1 && chain[0].up && chain[0].to == "F02", "the first floor: only a stair up");
    CHECK(defaultStairLinks("F03", "F01").size() == 2, "a middle floor: up and down");

    // ---- tiles: a named stair, then the plain U / D in reading order ----
    std::vector<std::string> tiles = {
        "#######",
        "#U...V#",
        "#..D..#",
        "#######",
    };
    const std::map<std::string, std::string> legend = {{"U", "stairs_up"}, {"D", "stairs_down"}, {"V", "stairs_up:F96"}};
    placeStairLinks(links, tiles, legend);
    CHECK(links[0].x == 1 && links[0].y == 1, "F51 takes the plain U (%d,%d)", links[0].x, links[0].y);
    CHECK(links[1].x == 3 && links[1].y == 2, "F49 takes the D (%d,%d)", links[1].x, links[1].y);
    CHECK(links[2].x == 5 && links[2].y == 1, "F96 takes its own tile V (%d,%d)", links[2].x, links[2].y);
    CHECK(tiles[1][5] == 'U', "the named tile becomes a plain stair up");
    CHECK(stairAt(links, 5, 1) == &links[2] && stairAt(links, 2, 2) == nullptr, "stairAt finds the stair on a tile");

    std::vector<StairLink> noTile = defaultStairLinks("F02", "F00");
    std::vector<std::string> onlyUp = {"#U#"};
    placeStairLinks(noTile, onlyUp, {});
    CHECK(noTile[0].x == 1 && noTile[1].x == -1, "a stair with no tile on the grid stays at -1");

    // ---- arrival: the stair that leads back ----
    CHECK(arrivalStair(links, "F49", true) == 1, "climbing up from F49 lands on the stair down to F49");
    CHECK(arrivalStair(links, "F96", false) == 2, "coming down from F96 lands on the stair up to F96");
    CHECK(arrivalStair(links, "F12", true) == 1, "a one-way link: the first stair of the other direction");
    CHECK(arrivalStair(noTile, "F00", true) == -1, "no placed stair leads back and none goes down: -1 (the caller falls back)");

    // ---- text ----
    CHECK(goStairsEventId("F50", "F96") == "stairs_F50_F96", "the go-to-stairs event id");
    CHECK(floorLabel("F05") == "5" && floorLabel("F50") == "50" && floorLabel("F00") == "0" && floorLabel("stage_11") == "stage_11",
          "floor labels for the player");
    CHECK(fillText("floor {from} to {to}, {from}", {{"from", "3"}, {"to", "4"}}) == "floor 3 to 4, 3", "fillText fills every tag");

    // ---- the shipped floors: every lock names a real event, every stair a real floor ----
    {
        FloorTable floors = FloorTable::loadFromDirectory("data/story/floors");
        std::set<std::string> events;
        for (const char* pool : {"pool_act01", "pool_act02", "pool_act03", "pool_act04", "pool_act05", "pool_act06",
                                 "pool_act07", "pool_act08", "pool_act09", "pool_act10", "pool_common"}) {
            std::string text;
            if (!vfsReadAll(std::string("data/events/") + pool + ".json", text)) continue;
            const nlohmann::json p = nlohmann::json::parse(text, nullptr, false);
            if (p.is_object() && p.contains("events"))
                for (const auto& e : p["events"]) events.insert(e.value("eventId", std::string()));
        }
        CHECK(floors.size() == 70 && events.size() >= 126, "the floors and the pools load (%d floors, %zu events)", floors.size(), events.size());
        for (const FloorInfo& f : floors.all())
            for (const StairLink& s : f.stairs) {
                CHECK(floors.find(s.to) != nullptr, "%s: a stair leads to %s, which is not a floor", f.id.c_str(), s.to.c_str());
                for (const std::string& ev : s.unlockEvents)
                    CHECK(events.count(ev), "%s: a stair waits for %s, which no pool defines", f.id.c_str(), ev.c_str());
            }
    }

    std::printf("stair_links_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
