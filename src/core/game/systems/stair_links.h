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
    bool up = true;              // "dir": "up" (else "down")
    std::string to;              // the floor it leads to
    std::vector<std::string> unlockEvents;   // "unlock": eventDone + eventsDone -- all must be finished; empty: open
    int x = -1, y = -1;          // its tile on the floor's grid (-1: the grid has no tile for it)
};

// A stair's `unlock` object: "eventDone" (one id) and "eventsDone" (a list), in that order, without repeats.
inline std::vector<std::string> unlockEventsFromJson(const nlohmann::json& unlock) {
    std::vector<std::string> out;
    auto add = [&](const nlohmann::json& v) {
        if (!v.is_string() || v.get<std::string>().empty()) return;
        for (const std::string& e : out) if (e == v.get<std::string>()) return;
        out.push_back(v.get<std::string>());
    };
    if (!unlock.is_object()) return out;
    if (unlock.contains("eventDone")) add(unlock["eventDone"]);
    if (unlock.contains("eventsDone") && unlock["eventsDone"].is_array())
        for (const auto& v : unlock["eventsDone"]) add(v);
    return out;
}

// The spec's `stairs` list. Entries without a `to` are dropped; an unknown `dir` reads as "up".
inline std::vector<StairLink> stairLinksFromJson(const nlohmann::json& arr) {
    std::vector<StairLink> out;
    if (!arr.is_array()) return out;
    for (const auto& j : arr) {
        if (!j.is_object() || !j.contains("to") || !j["to"].is_string()) continue;
        StairLink s;
        s.up = j.value("dir", std::string("up")) != "down";
        s.to = j["to"].get<std::string>();
        if (j.contains("unlock") && j["unlock"].is_object()) s.unlockEvents = unlockEventsFromJson(j["unlock"]);
        out.push_back(s);
    }
    return out;
}

// A floor with no `stairs` list: its old up / down pair (either may be "": no such stair).
inline std::vector<StairLink> defaultStairLinks(const std::string& up, const std::string& down) {
    std::vector<StairLink> out;
    if (!up.empty()) { StairLink s; s.up = true; s.to = up; out.push_back(s); }
    if (!down.empty()) { StairLink s; s.up = false; s.to = down; out.push_back(s); }
    return out;
}

// Gives every stair its tile. A legend entry "stairs_up:F96" marks the stair up to F96; those tiles are turned
// into plain 'U' / 'D' (so drawing and walking treat them like any stair). The remaining 'U' / 'D' tiles, read
// row by row, go to the remaining stairs of that direction in list order.
inline void placeStairLinks(std::vector<StairLink>& links, std::vector<std::string>& tiles,
                            const std::map<std::string, std::string>& legend) {
    std::map<char, std::pair<bool, std::string>> named;   // tile char -> (up, to)
    for (const auto& [ch, kind] : legend) {
        if (ch.size() != 1) continue;
        for (bool up : {true, false}) {
            const std::string prefix = up ? "stairs_up:" : "stairs_down:";
            if (kind.rfind(prefix, 0) == 0 && kind.size() > prefix.size()) named[ch[0]] = {up, kind.substr(prefix.size())};
        }
    }
    std::vector<std::pair<int, int>> plain[2];            // [up ? 0 : 1] -> 'U' / 'D' cells in reading order
    for (int y = 0; y < (int)tiles.size(); y++)
        for (int x = 0; x < (int)tiles[y].size(); x++) {
            const char c = tiles[y][x];
            auto n = named.find(c);
            if (n != named.end()) {
                for (StairLink& s : links)
                    if (s.x < 0 && s.up == n->second.first && s.to == n->second.second) { s.x = x; s.y = y; break; }
                tiles[y][x] = n->second.first ? 'U' : 'D';
            } else if (c == 'U') plain[0].push_back({x, y});
            else if (c == 'D') plain[1].push_back({x, y});
        }
    size_t next[2] = {0, 0};
    for (StairLink& s : links) {
        if (s.x >= 0) continue;
        const int k = s.up ? 0 : 1;
        if (next[k] < plain[k].size()) { s.x = plain[k][next[k]].first; s.y = plain[k][next[k]].second; next[k]++; }
    }
}

inline const StairLink* stairAt(const std::vector<StairLink>& links, int x, int y) {
    for (const StairLink& s : links) if (s.x == x && s.y == y) return &s;
    return nullptr;
}

// Where a player coming from `fromFloor` lands (section 3.3): the stair that leads back there, else the first
// stair of the other direction (a climb lands on a stair down), else -1.
inline int arrivalStair(const std::vector<StairLink>& links, const std::string& fromFloor, bool climbedUp) {
    for (size_t i = 0; i < links.size(); i++)
        if (links[i].x >= 0 && links[i].to == fromFloor && links[i].up != climbedUp) return (int)i;
    for (size_t i = 0; i < links.size(); i++)
        if (links[i].x >= 0 && links[i].to == fromFloor) return (int)i;
    for (size_t i = 0; i < links.size(); i++)
        if (links[i].x >= 0 && links[i].up != climbedUp) return (int)i;
    return -1;
}

// The "go to the stairs" event a finished unlock starts: one per stair (floor + target).
inline std::string goStairsEventId(const std::string& floor, const std::string& to) { return "stairs_" + floor + "_" + to; }

// "F50" -> "50" for the player's text; any other id stays as it is.
inline std::string floorLabel(const std::string& id) {
    if (id.size() >= 2 && (id[0] == 'F' || id[0] == 'f')) {
        size_t i = 1;
        while (i < id.size() && std::isdigit((unsigned char)id[i])) i++;
        if (i == id.size()) {
            size_t z = 1;
            while (z + 1 < id.size() && id[z] == '0') z++;
            return id.substr(z);
        }
    }
    return id;
}

// Replaces "{name}" with its value in a translated line ("The stairs on floor {from} ...").
inline std::string fillText(std::string s, const std::map<std::string, std::string>& vars) {
    for (const auto& [k, v] : vars) {
        const std::string tag = "{" + k + "}";
        for (size_t p = s.find(tag); p != std::string::npos; p = s.find(tag, p + v.size())) s.replace(p, tag.size(), v);
    }
    return s;
}

}  // namespace toms
