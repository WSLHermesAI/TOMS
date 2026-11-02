// event_chain_test -- event logic in the data (docs/gameplay/event_editor/06_EVENT_LOGIC.md) and key gates.
//
//   * every event `action` is a valid story action (story_actions.h) naming a real item / counter, every `requires` that waits for an event
//     (runFlagSet "event_<id>") names a real event;
//   * every gate ("gate:<item id>" in a floor's legend) names a story item that some event gives or some
//     floor places, so it can be opened;
//   * the bell chain (pool_act01): ev_bell_cracked (F03) -> ev_bell_ringer_key (F05, gives the key) ->
//     ev_bell_ringer_room (F04, behind the gate) -> ev_bell_rope (F06) + ev_bell_clapper (F02) ->
//     ev_bell_rings (F03, needs both). On F04 the room is reached only through its gate.
// Run in assets/ (tests/CMakeLists.txt).
#include "story_actions.h"
#include "vfs.h"

#include <json.hpp>

#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_iChecks = 0, g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

nlohmann::json f_ReadJson(const std::string& l_strPath) {
    std::string l_strText;
    if (!toms::f_VfsReadAll(l_strPath, l_strText)) return nlohmann::json();
    return nlohmann::json::parse(l_strText, nullptr, false);
}

// The events a condition waits for (runFlagSet "event_<id>"), through all / any / not.
void f_WaitsFor(const nlohmann::json& l_jC, std::vector<std::string>& l_vOut) {
    if (!l_jC.is_object()) return;
    for (const char* l_pszK : {"all", "any"})
        if (l_jC.contains(l_pszK) && l_jC[l_pszK].is_array()) for (const auto& l_jS : l_jC[l_pszK]) f_WaitsFor(l_jS, l_vOut);
    if (l_jC.contains("not")) f_WaitsFor(l_jC["not"], l_vOut);
    if (l_jC.value("type", std::string()) == "runFlagSet") {
        const std::string l_strF = l_jC.value("flag", std::string());
        if (l_strF.rfind("event_", 0) == 0) l_vOut.push_back(l_strF.substr(6));
    }
}

struct Map {
    std::vector<std::string> m_vTiles;
    std::map<char, std::string> m_mapLegend;
    char f_At(int l_iX, int l_iY) const {
        return l_iY >= 0 && l_iY < (int)m_vTiles.size() && l_iX >= 0 && l_iX < (int)m_vTiles[l_iY].size() ? m_vTiles[l_iY][l_iX] : '#';
    }
};

Map f_LoadMap(const std::string& l_strFloor) {
    Map l_oM;
    const nlohmann::json l_jJ = f_ReadJson("data/story/floors/" + l_strFloor + ".stage.json");
    if (!l_jJ.is_object()) return l_oM;
    l_oM.m_vTiles = l_jJ["tiles"].get<std::vector<std::string>>();
    for (auto& [l_strK, l_jV] : l_jJ["legend"].items()) if (!l_strK.empty() && l_jV.is_string()) l_oM.m_mapLegend[l_strK[0]] = l_jV.get<std::string>();
    return l_oM;
}

// Tiles reachable from (sx, sy): walls block, and so does any tile `blocks` says. Everything else (monsters,
// doors with keys on the floor, items) is passed in the end.
std::set<std::pair<int, int>> f_Reach(const Map& l_oM, int l_iSx, int l_iSy, const std::function<bool(char)>& l_fnBlocks) {
    std::set<std::pair<int, int>> l_setSeen{{l_iSx, l_iSy}};
    std::vector<std::pair<int, int>> l_vQ{{l_iSx, l_iSy}};
    for (size_t l_nI = 0; l_nI < l_vQ.size(); ++l_nI) {
        const auto [l_iX, l_iY] = l_vQ[l_nI];
        for (const auto& [l_iDx, l_iDy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            const int l_iNx = l_iX + l_iDx, l_iNy = l_iY + l_iDy;
            const char l_cC = l_oM.f_At(l_iNx, l_iNy);
            if (l_cC == '#' || l_fnBlocks(l_cC) || l_setSeen.count({l_iNx, l_iNy})) continue;
            l_setSeen.insert({l_iNx, l_iNy});
            l_vQ.push_back({l_iNx, l_iNy});
        }
    }
    return l_setSeen;
}

bool f_Find(const Map& l_oM, const std::string& l_strSemantic, int& l_iOx, int& l_iOy) {
    for (int l_iY = 0; l_iY < (int)l_oM.m_vTiles.size(); ++l_iY)
        for (int l_iX = 0; l_iX < (int)l_oM.m_vTiles[l_iY].size(); ++l_iX) {
            auto l_itIt = l_oM.m_mapLegend.find(l_oM.m_vTiles[l_iY][l_iX]);
            if (l_itIt != l_oM.m_mapLegend.end() && l_itIt->second == l_strSemantic) { l_iOx = l_iX; l_iOy = l_iY; return true; }
        }
    return false;
}

}  // namespace

int main() {
    const nlohmann::json l_jItems = f_ReadJson("data/items.json");
    const nlohmann::json l_jCounters = f_ReadJson("data/story/counters.json");
    CHECK(l_jItems.is_object() && l_jCounters.is_object(), "data/items.json and data/story/counters.json load");

    // ---- every event: actions and waits name real things
    std::map<std::string, nlohmann::json> l_mapEvents;
    for (const std::string& l_strName : toms::f_VfsListDir("data/events")) {
        if (l_strName.rfind("pool_", 0) != 0) continue;
        const nlohmann::json l_jP = f_ReadJson("data/events/" + l_strName);
        if (l_jP.contains("events")) for (const auto& l_jE : l_jP["events"]) l_mapEvents[l_jE.value("eventId", std::string())] = l_jE;
    }
    CHECK(l_mapEvents.size() >= 132, "the pools load (%zu events)", l_mapEvents.size());
    std::set<std::string> l_setGiven;   // items some event gives
    for (const auto& [l_strId, l_jE] : l_mapEvents) {
        if (l_jE.contains("actions")) {
            CHECK(l_jE["actions"].is_array(), "%s: actions is a list", l_strId.c_str());
            // the one action vocabulary (story_actions.h; docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 7)
            toms::story::Catalog l_oCat;
            for (auto& [l_strK, l_jV] : l_jItems.items()) if (!l_strK.empty() && l_strK[0] != '_') l_oCat.m_setItems.insert(l_strK);
            for (auto& [l_strK, l_jV] : l_jCounters.items()) if (!l_strK.empty() && l_strK[0] != '_') l_oCat.m_setCounters.insert(l_strK);
            for (const std::string& l_strP : toms::story::f_CheckActions(l_jE["actions"], l_oCat, l_strId)) CHECK(false, "%s", l_strP.c_str());
            for (const auto& l_jA : l_jE["actions"])
                if (l_jA.value("type", std::string()) == "give") l_setGiven.insert(l_jA.value("itemId", std::string()));
        }
        std::vector<std::string> l_vWaits;
        if (l_jE.contains("requires")) f_WaitsFor(l_jE["requires"], l_vWaits);
        for (const std::string& l_strW : l_vWaits) CHECK(l_mapEvents.count(l_strW), "%s waits for %s, which no pool defines", l_strId.c_str(), l_strW.c_str());
    }

    // ---- every gate: a story item that can be had (an event gives it or a floor places it)
    std::set<std::string> l_setPlaced;
    std::vector<std::pair<std::string, std::string>> l_vGates;   // floor, item
    for (int l_iF = 1; l_iF <= 70; ++l_iF) {
        char l_pszFid[8];
        std::snprintf(l_pszFid, sizeof l_pszFid, "F%02d", l_iF);
        const Map l_oM = f_LoadMap(l_pszFid);
        for (const auto& [l_cC, l_strSem] : l_oM.m_mapLegend) {
            if (l_strSem.rfind("item:", 0) == 0) l_setPlaced.insert(l_strSem.substr(5));
            if (l_strSem.rfind("gate:", 0) == 0) l_vGates.push_back({l_pszFid, l_strSem.substr(5)});
        }
    }
    for (const auto& [l_strFloor, l_strItem] : l_vGates) {
        CHECK(l_jItems.contains(l_strItem) && l_jItems[l_strItem].value("type", std::string()) == "story",
              "%s: gate:%s needs a story item of that id in data/items.json", l_strFloor.c_str(), l_strItem.c_str());
        CHECK(l_setGiven.count(l_strItem) || l_setPlaced.count(l_strItem), "%s: nothing gives or places %s, so its gate never opens",
              l_strFloor.c_str(), l_strItem.c_str());
    }

    // ---- the bell chain
    const std::vector<std::string> l_vChain = {"ev_bell_cracked", "ev_bell_ringer_key", "ev_bell_ringer_room",
                                            "ev_bell_rope", "ev_bell_clapper", "ev_bell_rings"};
    for (const std::string& l_strId : l_vChain) CHECK(l_mapEvents.count(l_strId), "the bell chain has %s", l_strId.c_str());
    if (l_mapEvents.size() && l_mapEvents.count("ev_bell_rings")) {
        auto l_fnNextOf = [&](const std::string& l_strId) { return l_mapEvents[l_strId].value("next", nlohmann::json::array()); };
        auto l_fnWaitsOf = [&](const std::string& l_strId) {
            std::vector<std::string> l_vW;
            if (l_mapEvents[l_strId].contains("requires")) f_WaitsFor(l_mapEvents[l_strId]["requires"], l_vW);
            return l_vW;
        };
        CHECK(!l_mapEvents["ev_bell_cracked"].contains("requires"), "the chain starts with no condition");
        CHECK(l_fnNextOf("ev_bell_ringer_room").size() == 2, "the room wakes two events");
        CHECK(l_fnWaitsOf("ev_bell_rings") == std::vector<std::string>({"ev_bell_rope", "ev_bell_clapper"}),
              "the final event waits for both");
        CHECK(l_fnWaitsOf("ev_bell_ringer_room") == std::vector<std::string>({"ev_bell_ringer_key"}),
              "the room waits for the key event");
        bool l_bGivesKey = false;
        for (const auto& l_jA : l_mapEvents["ev_bell_ringer_key"].value("actions", nlohmann::json::array()))
            l_bGivesKey |= l_jA.value("type", std::string()) == "give" && l_jA.value("itemId", std::string()) == "key_bellward";
        CHECK(l_bGivesKey, "the bell-ringer gives key_bellward");
    }
    // where each one is, low -> top -> low: F03, F05, F04, F06, F02, F03
    const std::vector<std::pair<std::string, std::string>> l_vWhere = {
        {"F03", "ev_bell_cracked"}, {"F05", "ev_bell_ringer_key"}, {"F04", "ev_bell_ringer_room"},
        {"F06", "ev_bell_rope"},    {"F02", "ev_bell_clapper"},    {"F03", "ev_bell_rings"}};
    for (const auto& [l_strFloor, l_strEv] : l_vWhere) {
        int l_iX = 0, l_iY = 0;
        CHECK(f_Find(f_LoadMap(l_strFloor), "event:" + l_strEv, l_iX, l_iY), "%s places %s", l_strFloor.c_str(), l_strEv.c_str());
    }
    // F04: the room behind the gate. From the stairs down (where the player arrives from F03), the room's event
    // is out of reach while the gate is shut, and reached once it is open.
    {
        const Map l_oM = f_LoadMap("F04");
        int l_iSx = 0, l_iSy = 0, l_iGx = 0, l_iGy = 0, l_iEx = 0, l_iEy = 0;
        const bool l_bOk = f_Find(l_oM, "stairs_down", l_iSx, l_iSy) && f_Find(l_oM, "gate:key_bellward", l_iGx, l_iGy) &&
                        f_Find(l_oM, "event:ev_bell_ringer_room", l_iEx, l_iEy);
        CHECK(l_bOk, "F04 has its stairs down, the bell gate and the room's event");
        if (l_bOk) {
            const char l_cGate = l_oM.m_vTiles[l_iGy][l_iGx];
            const auto l_setShut = f_Reach(l_oM, l_iSx, l_iSy, [&](char l_cC) { return l_cC == l_cGate; });
            const auto l_setOpen = f_Reach(l_oM, l_iSx, l_iSy, [](char) { return false; });
            CHECK(!l_setShut.count({l_iEx, l_iEy}), "F04: the bell-ringer's room is closed while the gate is shut");
            CHECK(l_setOpen.count({l_iEx, l_iEy}), "F04: the room is reached once the gate is open");
            CHECK(l_setShut.count({l_iGx - 1, l_iGy}) || l_setShut.count({l_iGx + 1, l_iGy}) || l_setShut.count({l_iGx, l_iGy - 1}) || l_setShut.count({l_iGx, l_iGy + 1}),
                  "F04: the gate itself can be walked up to");
        }
    }

    std::printf("event_chain_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}
