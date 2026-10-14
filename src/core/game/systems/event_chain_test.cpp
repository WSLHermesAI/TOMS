// event_chain_test -- event logic in the data (docs/event_editor/06_EVENT_LOGIC.md) and key gates.
//
//   * every event `action` names a real item / counter, every `requires` that waits for an event
//     (runFlagSet "event_<id>") names a real event;
//   * every gate ("gate:<item id>" in a floor's legend) names a story item that some event gives or some
//     floor places, so it can be opened;
//   * the bell chain (pool_act01): ev_bell_cracked (F03) -> ev_bell_ringer_key (F05, gives the key) ->
//     ev_bell_ringer_room (F04, behind the gate) -> ev_bell_rope (F06) + ev_bell_clapper (F02) ->
//     ev_bell_rings (F03, needs both). On F04 the room is reached only through its gate.
// Run in assets/ (tests/CMakeLists.txt).
#include "vfs.h"

#include <json.hpp>

#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_checks = 0, g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

nlohmann::json readJson(const std::string& path) {
    std::string text;
    if (!toms::vfsReadAll(path, text)) return nlohmann::json();
    return nlohmann::json::parse(text, nullptr, false);
}

// The events a condition waits for (runFlagSet "event_<id>"), through all / any / not.
void waitsFor(const nlohmann::json& c, std::vector<std::string>& out) {
    if (!c.is_object()) return;
    for (const char* k : {"all", "any"})
        if (c.contains(k) && c[k].is_array()) for (const auto& s : c[k]) waitsFor(s, out);
    if (c.contains("not")) waitsFor(c["not"], out);
    if (c.value("type", std::string()) == "runFlagSet") {
        const std::string f = c.value("flag", std::string());
        if (f.rfind("event_", 0) == 0) out.push_back(f.substr(6));
    }
}

struct Map {
    std::vector<std::string> tiles;
    std::map<char, std::string> legend;
    char at(int x, int y) const {
        return y >= 0 && y < (int)tiles.size() && x >= 0 && x < (int)tiles[y].size() ? tiles[y][x] : '#';
    }
};

Map loadMap(const std::string& floor) {
    Map m;
    const nlohmann::json j = readJson("data/story/floors/" + floor + ".stage.json");
    if (!j.is_object()) return m;
    m.tiles = j["tiles"].get<std::vector<std::string>>();
    for (auto& [k, v] : j["legend"].items()) if (!k.empty() && v.is_string()) m.legend[k[0]] = v.get<std::string>();
    return m;
}

// Tiles reachable from (sx, sy): walls block, and so does any tile `blocks` says. Everything else (monsters,
// doors with keys on the floor, items) is passed in the end.
std::set<std::pair<int, int>> reach(const Map& m, int sx, int sy, const std::function<bool(char)>& blocks) {
    std::set<std::pair<int, int>> seen{{sx, sy}};
    std::vector<std::pair<int, int>> q{{sx, sy}};
    for (size_t i = 0; i < q.size(); ++i) {
        const auto [x, y] = q[i];
        for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            const int nx = x + dx, ny = y + dy;
            const char c = m.at(nx, ny);
            if (c == '#' || blocks(c) || seen.count({nx, ny})) continue;
            seen.insert({nx, ny});
            q.push_back({nx, ny});
        }
    }
    return seen;
}

bool find(const Map& m, const std::string& semantic, int& ox, int& oy) {
    for (int y = 0; y < (int)m.tiles.size(); ++y)
        for (int x = 0; x < (int)m.tiles[y].size(); ++x) {
            auto it = m.legend.find(m.tiles[y][x]);
            if (it != m.legend.end() && it->second == semantic) { ox = x; oy = y; return true; }
        }
    return false;
}

}  // namespace

int main() {
    const nlohmann::json items = readJson("data/items.json");
    const nlohmann::json counters = readJson("data/story/counters.json");
    CHECK(items.is_object() && counters.is_object(), "data/items.json and data/story/counters.json load");

    // ---- every event: actions and waits name real things
    std::map<std::string, nlohmann::json> events;
    for (const std::string& name : toms::vfsListDir("data/events")) {
        if (name.rfind("pool_", 0) != 0) continue;
        const nlohmann::json p = readJson("data/events/" + name);
        if (p.contains("events")) for (const auto& e : p["events"]) events[e.value("eventId", std::string())] = e;
    }
    CHECK(events.size() >= 132, "the pools load (%zu events)", events.size());
    std::set<std::string> given;   // items some event gives
    for (const auto& [id, e] : events) {
        if (e.contains("actions")) {
            CHECK(e["actions"].is_array(), "%s: actions is a list", id.c_str());
            for (const auto& a : e["actions"]) {
                const std::string type = a.value("type", std::string());
                if (type == "give") {
                    const std::string item = a.value("itemId", std::string());
                    CHECK(items.contains(item), "%s gives %s, which data/items.json lacks", id.c_str(), item.c_str());
                    given.insert(item);
                } else if (type == "addCounter") {
                    CHECK(counters.contains(a.value("counter", std::string())), "%s adds to an unknown counter", id.c_str());
                } else {
                    CHECK(type == "setFlag", "%s: unknown action '%s' (give / addCounter / setFlag)", id.c_str(), type.c_str());
                }
            }
        }
        std::vector<std::string> waits;
        if (e.contains("requires")) waitsFor(e["requires"], waits);
        for (const std::string& w : waits) CHECK(events.count(w), "%s waits for %s, which no pool defines", id.c_str(), w.c_str());
    }

    // ---- every gate: a story item that can be had (an event gives it or a floor places it)
    std::set<std::string> placed;
    std::vector<std::pair<std::string, std::string>> gates;   // floor, item
    for (int f = 1; f <= 70; ++f) {
        char fid[8];
        std::snprintf(fid, sizeof fid, "F%02d", f);
        const Map m = loadMap(fid);
        for (const auto& [c, sem] : m.legend) {
            if (sem.rfind("item:", 0) == 0) placed.insert(sem.substr(5));
            if (sem.rfind("gate:", 0) == 0) gates.push_back({fid, sem.substr(5)});
        }
    }
    for (const auto& [floor, item] : gates) {
        CHECK(items.contains(item) && items[item].value("type", std::string()) == "story",
              "%s: gate:%s needs a story item of that id in data/items.json", floor.c_str(), item.c_str());
        CHECK(given.count(item) || placed.count(item), "%s: nothing gives or places %s, so its gate never opens",
              floor.c_str(), item.c_str());
    }

    // ---- the bell chain
    const std::vector<std::string> chain = {"ev_bell_cracked", "ev_bell_ringer_key", "ev_bell_ringer_room",
                                            "ev_bell_rope", "ev_bell_clapper", "ev_bell_rings"};
    for (const std::string& id : chain) CHECK(events.count(id), "the bell chain has %s", id.c_str());
    if (events.size() && events.count("ev_bell_rings")) {
        auto nextOf = [&](const std::string& id) { return events[id].value("next", nlohmann::json::array()); };
        auto waitsOf = [&](const std::string& id) {
            std::vector<std::string> w;
            if (events[id].contains("requires")) waitsFor(events[id]["requires"], w);
            return w;
        };
        CHECK(!events["ev_bell_cracked"].contains("requires"), "the chain starts with no condition");
        CHECK(nextOf("ev_bell_ringer_room").size() == 2, "the room wakes two events");
        CHECK(waitsOf("ev_bell_rings") == std::vector<std::string>({"ev_bell_rope", "ev_bell_clapper"}),
              "the final event waits for both");
        CHECK(waitsOf("ev_bell_ringer_room") == std::vector<std::string>({"ev_bell_ringer_key"}),
              "the room waits for the key event");
        bool givesKey = false;
        for (const auto& a : events["ev_bell_ringer_key"].value("actions", nlohmann::json::array()))
            givesKey |= a.value("type", std::string()) == "give" && a.value("itemId", std::string()) == "key_bellward";
        CHECK(givesKey, "the bell-ringer gives key_bellward");
    }
    // where each one is, low -> top -> low: F03, F05, F04, F06, F02, F03
    const std::vector<std::pair<std::string, std::string>> where = {
        {"F03", "ev_bell_cracked"}, {"F05", "ev_bell_ringer_key"}, {"F04", "ev_bell_ringer_room"},
        {"F06", "ev_bell_rope"},    {"F02", "ev_bell_clapper"},    {"F03", "ev_bell_rings"}};
    for (const auto& [floor, ev] : where) {
        int x = 0, y = 0;
        CHECK(find(loadMap(floor), "event:" + ev, x, y), "%s places %s", floor.c_str(), ev.c_str());
    }
    // F04: the room behind the gate. From the stairs down (where the player arrives from F03), the room's event
    // is out of reach while the gate is shut, and reached once it is open.
    {
        const Map m = loadMap("F04");
        int sx = 0, sy = 0, gx = 0, gy = 0, ex = 0, ey = 0;
        const bool ok = find(m, "stairs_down", sx, sy) && find(m, "gate:key_bellward", gx, gy) &&
                        find(m, "event:ev_bell_ringer_room", ex, ey);
        CHECK(ok, "F04 has its stairs down, the bell gate and the room's event");
        if (ok) {
            const char gate = m.tiles[gy][gx];
            const auto shut = reach(m, sx, sy, [&](char c) { return c == gate; });
            const auto open = reach(m, sx, sy, [](char) { return false; });
            CHECK(!shut.count({ex, ey}), "F04: the bell-ringer's room is closed while the gate is shut");
            CHECK(open.count({ex, ey}), "F04: the room is reached once the gate is open");
            CHECK(shut.count({gx - 1, gy}) || shut.count({gx + 1, gy}) || shut.count({gx, gy - 1}) || shut.count({gx, gy + 1}),
                  "F04: the gate itself can be walked up to");
        }
    }

    std::printf("event_chain_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
