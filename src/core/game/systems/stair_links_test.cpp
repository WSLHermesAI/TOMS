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

static int g_iChecks = 0;
static int g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    // ---- the spec's list ----
    const nlohmann::json l_jSpec = nlohmann::json::parse(R"([
        { "dir": "up", "to": "F51" },
        { "dir": "down", "to": "F49" },
        { "dir": "up", "to": "F96", "unlock": { "eventDone": "ev_common_relic_road" } },
        { "dir": "up" },
        { "dir": "up", "to": "F97", "unlock": { "eventsDone": ["ev_a", "ev_b", "ev_a", ""] } },
        { "dir": "up", "to": "F98", "unlock": { "eventDone": "ev_a", "eventsDone": ["ev_b", "ev_a"] } }
    ])");
    std::vector<StairLink> l_vLinks = f_StairLinksFromJson(l_jSpec);
    CHECK(l_vLinks.size() == 5, "an entry without `to` is dropped (%zu)", l_vLinks.size());
    CHECK(l_vLinks[0].m_bUp && l_vLinks[0].m_strTo == "F51" && l_vLinks[0].m_vUnlockEvents.empty(), "an open stair up");
    CHECK(!l_vLinks[1].m_bUp && l_vLinks[1].m_strTo == "F49", "a stair down");
    CHECK(l_vLinks[2].m_vUnlockEvents == std::vector<std::string>{"ev_common_relic_road"}, "a stair locked by an event");
    // W38: several events, all needed -- a list, repeats and empty ids dropped; eventDone first, then eventsDone
    CHECK((l_vLinks[3].m_vUnlockEvents == std::vector<std::string>{"ev_a", "ev_b"}), "eventsDone: several events, no repeats");
    CHECK((l_vLinks[4].m_vUnlockEvents == std::vector<std::string>{"ev_a", "ev_b"}), "eventDone + eventsDone together");
    CHECK(f_UnlockEventsFromJson(nlohmann::json::object()).empty(), "an empty unlock: open");
    CHECK(f_StairLinksFromJson(nlohmann::json::object()).empty(), "not a list: no stairs");

    const std::vector<StairLink> l_vChain = f_DefaultStairLinks("F02", "");
    CHECK(l_vChain.size() == 1 && l_vChain[0].m_bUp && l_vChain[0].m_strTo == "F02", "the first floor: only a stair up");
    CHECK(f_DefaultStairLinks("F03", "F01").size() == 2, "a middle floor: up and down");

    // ---- tiles: a named stair, then the plain U / D in reading order ----
    std::vector<std::string> l_vTiles = {
        "#######",
        "#U...V#",
        "#..D..#",
        "#######",
    };
    const std::map<std::string, std::string> l_mapLegend = {{"U", "stairs_up"}, {"D", "stairs_down"}, {"V", "stairs_up:F96"}};
    f_PlaceStairLinks(l_vLinks, l_vTiles, l_mapLegend);
    CHECK(l_vLinks[0].m_iX == 1 && l_vLinks[0].m_iY == 1, "F51 takes the plain U (%d,%d)", l_vLinks[0].m_iX, l_vLinks[0].m_iY);
    CHECK(l_vLinks[1].m_iX == 3 && l_vLinks[1].m_iY == 2, "F49 takes the D (%d,%d)", l_vLinks[1].m_iX, l_vLinks[1].m_iY);
    CHECK(l_vLinks[2].m_iX == 5 && l_vLinks[2].m_iY == 1, "F96 takes its own tile V (%d,%d)", l_vLinks[2].m_iX, l_vLinks[2].m_iY);
    CHECK(l_vTiles[1][5] == 'U', "the named tile becomes a plain stair up");
    CHECK(f_StairAt(l_vLinks, 5, 1) == &l_vLinks[2] && f_StairAt(l_vLinks, 2, 2) == nullptr, "stairAt finds the stair on a tile");

    std::vector<StairLink> l_vNoTile = f_DefaultStairLinks("F02", "F00");
    std::vector<std::string> l_vOnlyUp = {"#U#"};
    f_PlaceStairLinks(l_vNoTile, l_vOnlyUp, {});
    CHECK(l_vNoTile[0].m_iX == 1 && l_vNoTile[1].m_iX == -1, "a stair with no tile on the grid stays at -1");

    // ---- arrival: the stair that leads back ----
    CHECK(f_ArrivalStair(l_vLinks, "F49", true) == 1, "climbing up from F49 lands on the stair down to F49");
    CHECK(f_ArrivalStair(l_vLinks, "F96", false) == 2, "coming down from F96 lands on the stair up to F96");
    CHECK(f_ArrivalStair(l_vLinks, "F12", true) == 1, "a one-way link: the first stair of the other direction");
    CHECK(f_ArrivalStair(l_vNoTile, "F00", true) == -1, "no placed stair leads back and none goes down: -1 (the caller falls back)");

    // ---- text ----
    CHECK(f_GoStairsEventId("F50", "F96") == "stairs_F50_F96", "the go-to-stairs event id");
    CHECK(f_FloorLabel("F05") == "5" && f_FloorLabel("F50") == "50" && f_FloorLabel("F00") == "0" && f_FloorLabel("stage_11") == "stage_11",
          "floor labels for the player");
    CHECK(f_FillText("floor {from} to {to}, {from}", {{"from", "3"}, {"to", "4"}}) == "floor 3 to 4, 3", "fillText fills every tag");

    // ---- the shipped floors: every lock names a real event, every stair a real floor ----
    {
        FloorTable l_oFloors = FloorTable::f_LoadFromDirectory("data/story/floors");
        std::set<std::string> l_setEvents;
        for (const char* l_pszPool : {"pool_act01", "pool_act02", "pool_act03", "pool_act04", "pool_act05", "pool_act06",
                                 "pool_act07", "pool_act08", "pool_act09", "pool_act10", "pool_common"}) {
            std::string l_strText;
            if (!f_VfsReadAll(std::string("data/events/") + l_pszPool + ".json", l_strText)) continue;
            const nlohmann::json l_jP = nlohmann::json::parse(l_strText, nullptr, false);
            if (l_jP.is_object() && l_jP.contains("events"))
                for (const auto& l_jE : l_jP["events"]) l_setEvents.insert(l_jE.value("eventId", std::string()));
        }
        CHECK(l_oFloors.size() == 70 && l_setEvents.size() >= 126, "the floors and the pools load (%d floors, %zu events)", l_oFloors.size(), l_setEvents.size());
        for (const FloorInfo& l_oF : l_oFloors.f_All())
            for (const StairLink& l_oS : l_oF.m_vStairs) {
                CHECK(l_oFloors.f_Find(l_oS.m_strTo) != nullptr, "%s: a stair leads to %s, which is not a floor", l_oF.m_strId.c_str(), l_oS.m_strTo.c_str());
                for (const std::string& l_strEv : l_oS.m_vUnlockEvents)
                    CHECK(l_setEvents.count(l_strEv), "%s: a stair waits for %s, which no pool defines", l_oF.m_strId.c_str(), l_strEv.c_str());
            }
    }

    std::printf("stair_links_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}
