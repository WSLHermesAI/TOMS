// stair_links.h -- a floor's stairs as a list, some locked by an event (docs/gameplay/event_editor/05_FLOOR_LINKS.md).
//
// A floor spec may carry `"stairs": [ { "dir": "up", "to": "F51" }, { "dir": "up", "to": "F96",
// "unlock": { "eventDone": "ev_x" } } ]`. Without it a floor keeps the old chain: up to `nextFloor`, down to the
// floor before it. Each stair needs a tile: the legend's "stairs_up:<to>" / "stairs_down:<to>" entry names the
// stair to a given floor; the plain 'U' / 'D' tiles go, in order, to the stairs that have no tile of their own.
//
// Locked stairs (the owner's Q7 answer, 2026-10-12) stay visible with a lock icon, and cannot be taken until
// their events are finished: `"unlock": { "eventDone": "ev_a" }` (one), `{ "eventsDone": ["ev_a", "ev_b"] }`
// (several, all of them -- the owner's Q8 answer, 2026-10-21; W38), or both keys together. Finishing it opens a dialog that names the floor, and starts a "go to the stairs"
// event (goStairsEventId) that is finished when the player takes those stairs.
//
// Pure data, no Game: unit-tested in stair_links_test.cpp.
#pragma once
#include <json.hpp>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace toms {

struct StairLink {
    bool m_bUp = true;              // "dir": "up" (else "down")
    std::string m_strTo;              // the floor it leads to
    std::vector<std::string> m_vUnlockEvents;   // "unlock": eventDone + eventsDone -- all must be finished; empty: open
    int m_iX = -1, m_iY = -1;          // its tile on the floor's grid (-1: the grid has no tile for it)
};

// A stair's `unlock` object: "eventDone" (one id) and "eventsDone" (a list), in that order, without repeats.
inline std::vector<std::string> f_UnlockEventsFromJson(const nlohmann::json& l_jUnlock) {
    std::vector<std::string> l_vOut;
    auto l_fnAdd = [&](const nlohmann::json& l_jV) {
        if (!l_jV.is_string() || l_jV.get<std::string>().empty()) return;
        for (const std::string& l_strE : l_vOut) if (l_strE == l_jV.get<std::string>()) return;
        l_vOut.push_back(l_jV.get<std::string>());
    };
    if (!l_jUnlock.is_object()) return l_vOut;
    if (l_jUnlock.contains("eventDone")) l_fnAdd(l_jUnlock["eventDone"]);
    if (l_jUnlock.contains("eventsDone") && l_jUnlock["eventsDone"].is_array())
        for (const auto& l_jV : l_jUnlock["eventsDone"]) l_fnAdd(l_jV);
    return l_vOut;
}

// The spec's `stairs` list. Entries without a `to` are dropped; an unknown `dir` reads as "up".
inline std::vector<StairLink> f_StairLinksFromJson(const nlohmann::json& l_jArr) {
    std::vector<StairLink> l_vOut;
    if (!l_jArr.is_array()) return l_vOut;
    for (const auto& l_jJ : l_jArr) {
        if (!l_jJ.is_object() || !l_jJ.contains("to") || !l_jJ["to"].is_string()) continue;
        StairLink l_oS;
        l_oS.m_bUp = l_jJ.value("dir", std::string("up")) != "down";
        l_oS.m_strTo = l_jJ["to"].get<std::string>();
        if (l_jJ.contains("unlock") && l_jJ["unlock"].is_object()) l_oS.m_vUnlockEvents = f_UnlockEventsFromJson(l_jJ["unlock"]);
        l_vOut.push_back(l_oS);
    }
    return l_vOut;
}

// A floor with no `stairs` list: its old up / down pair (either may be "": no such stair).
inline std::vector<StairLink> f_DefaultStairLinks(const std::string& l_strUp, const std::string& l_strDown) {
    std::vector<StairLink> l_vOut;
    if (!l_strUp.empty()) { StairLink l_oS; l_oS.m_bUp = true; l_oS.m_strTo = l_strUp; l_vOut.push_back(l_oS); }
    if (!l_strDown.empty()) { StairLink l_oS; l_oS.m_bUp = false; l_oS.m_strTo = l_strDown; l_vOut.push_back(l_oS); }
    return l_vOut;
}

// Gives every stair its tile. A legend entry "stairs_up:F96" marks the stair up to F96; those tiles are turned
// into plain 'U' / 'D' (so drawing and walking treat them like any stair). The remaining 'U' / 'D' tiles, read
// row by row, go to the remaining stairs of that direction in list order.
inline void f_PlaceStairLinks(std::vector<StairLink>& l_vLinks, std::vector<std::string>& l_vTiles,
                            const std::map<std::string, std::string>& l_mapLegend) {
    std::map<char, std::pair<bool, std::string>> l_mapNamed;   // tile char -> (up, to)
    for (const auto& [l_strCh, l_strKind] : l_mapLegend) {
        if (l_strCh.size() != 1) continue;
        for (bool l_bUp : {true, false}) {
            const std::string l_strPrefix = l_bUp ? "stairs_up:" : "stairs_down:";
            if (l_strKind.rfind(l_strPrefix, 0) == 0 && l_strKind.size() > l_strPrefix.size()) l_mapNamed[l_strCh[0]] = {l_bUp, l_strKind.substr(l_strPrefix.size())};
        }
    }
    std::vector<std::pair<int, int>> l_arrPlain[2];            // [up ? 0 : 1] -> 'U' / 'D' cells in reading order
    for (int l_iY = 0; l_iY < (int)l_vTiles.size(); l_iY++)
        for (int l_iX = 0; l_iX < (int)l_vTiles[l_iY].size(); l_iX++) {
            const char l_cC = l_vTiles[l_iY][l_iX];
            auto l_itN = l_mapNamed.find(l_cC);
            if (l_itN != l_mapNamed.end()) {
                for (StairLink& l_oS : l_vLinks)
                    if (l_oS.m_iX < 0 && l_oS.m_bUp == l_itN->second.first && l_oS.m_strTo == l_itN->second.second) { l_oS.m_iX = l_iX; l_oS.m_iY = l_iY; break; }
                l_vTiles[l_iY][l_iX] = l_itN->second.first ? 'U' : 'D';
            } else if (l_cC == 'U') l_arrPlain[0].push_back({l_iX, l_iY});
            else if (l_cC == 'D') l_arrPlain[1].push_back({l_iX, l_iY});
        }
    size_t l_arrNext[2] = {0, 0};
    for (StairLink& l_oS : l_vLinks) {
        if (l_oS.m_iX >= 0) continue;
        const int l_iK = l_oS.m_bUp ? 0 : 1;
        if (l_arrNext[l_iK] < l_arrPlain[l_iK].size()) { l_oS.m_iX = l_arrPlain[l_iK][l_arrNext[l_iK]].first; l_oS.m_iY = l_arrPlain[l_iK][l_arrNext[l_iK]].second; l_arrNext[l_iK]++; }
    }
}

inline const StairLink* f_StairAt(const std::vector<StairLink>& l_vLinks, int l_iX, int l_iY) {
    for (const StairLink& l_oS : l_vLinks) if (l_oS.m_iX == l_iX && l_oS.m_iY == l_iY) return &l_oS;
    return nullptr;
}

// Where a player coming from `fromFloor` lands (section 3.3): the stair that leads back there, else the first
// stair of the other direction (a climb lands on a stair down), else -1.
inline int f_ArrivalStair(const std::vector<StairLink>& l_vLinks, const std::string& l_strFromFloor, bool l_bClimbedUp) {
    for (size_t l_nI = 0; l_nI < l_vLinks.size(); l_nI++)
        if (l_vLinks[l_nI].m_iX >= 0 && l_vLinks[l_nI].m_strTo == l_strFromFloor && l_vLinks[l_nI].m_bUp != l_bClimbedUp) return (int)l_nI;
    for (size_t l_nI = 0; l_nI < l_vLinks.size(); l_nI++)
        if (l_vLinks[l_nI].m_iX >= 0 && l_vLinks[l_nI].m_strTo == l_strFromFloor) return (int)l_nI;
    for (size_t l_nI = 0; l_nI < l_vLinks.size(); l_nI++)
        if (l_vLinks[l_nI].m_iX >= 0 && l_vLinks[l_nI].m_bUp != l_bClimbedUp) return (int)l_nI;
    return -1;
}

// The "go to the stairs" event a finished unlock starts: one per stair (floor + target).
inline std::string f_GoStairsEventId(const std::string& l_strFloor, const std::string& l_strTo) { return "stairs_" + l_strFloor + "_" + l_strTo; }

// "F50" -> "50" for the player's text; any other id stays as it is.
inline std::string f_FloorLabel(const std::string& l_strId) {
    if (l_strId.size() >= 2 && (l_strId[0] == 'F' || l_strId[0] == 'f')) {
        size_t l_nI = 1;
        while (l_nI < l_strId.size() && std::isdigit((unsigned char)l_strId[l_nI])) l_nI++;
        if (l_nI == l_strId.size()) {
            size_t l_nZ = 1;
            while (l_nZ + 1 < l_strId.size() && l_strId[l_nZ] == '0') l_nZ++;
            return l_strId.substr(l_nZ);
        }
    }
    return l_strId;
}

// Replaces "{name}" with its value in a translated line ("The stairs on floor {from} ...").
inline std::string f_FillText(std::string l_strS, const std::map<std::string, std::string>& l_mapVars) {
    for (const auto& [l_strK, l_strV] : l_mapVars) {
        const std::string l_strTag = "{" + l_strK + "}";
        for (size_t l_nP = l_strS.find(l_strTag); l_nP != std::string::npos; l_nP = l_strS.find(l_strTag, l_nP + l_strV.size())) l_strS.replace(l_nP, l_strTag.size(), l_strV);
    }
    return l_strS;
}

}  // namespace toms
