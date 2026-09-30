#include "event_graph.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "json.hpp"     // the project's single-header nlohmann copy (external/json)

namespace fs = std::filesystem;

namespace toms::editor {
namespace {

using nlohmann::json;

json readJson(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return json();
    try { return json::parse(std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>())); }
    catch (...) { return json(); }          // a malformed file is skipped, not fatal
}

std::string leafName(const fs::path& p) { return p.stem().string(); }

// F01 -> 1; anything unparseable -> 0 (treated as unordered, still shown)
int floorSeqOf(const std::string& stem) {
    int v = 0; bool digit = false;
    for (char c : stem) { if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); digit = true; } else if (digit) break; }
    return digit ? v : 0;
}

}  // namespace

const GraphNode* EventGraph::find(const std::string& id) const {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

int EventGraph::countOf(NodeKind k) const {
    return (int)std::count_if(nodes.begin(), nodes.end(), [k](const GraphNode& n) { return n.kind == k; });
}

EventGraph loadEventGraph(const std::string& root) {
    EventGraph g;
    const fs::path base = fs::path(root) / "assets" / "data";

    // ---- 1. pools and their events ----
    std::map<std::string, std::string> eventKind;          // eventId -> kind
    if (fs::is_directory(base / "events")) {
        std::vector<fs::path> pools;
        for (const auto& e : fs::directory_iterator(base / "events"))
            if (e.path().extension() == ".json") pools.push_back(e.path());
        std::sort(pools.begin(), pools.end());
        for (const auto& p : pools) {
            const std::string poolId = leafName(p);
            g.nodes.push_back({NodeKind::Pool, poolId, poolId, 0});
            const json j = readJson(p);
            if (!j.is_object() || !j.contains("events") || !j["events"].is_array()) continue;
            for (const auto& ev : j["events"]) {
                if (!ev.is_object()) continue;
                const std::string id = ev.value("eventId", std::string());
                if (id.empty()) continue;
                const std::string kind = ev.value("kind", std::string("?"));
                const std::string text = ev.value("text", std::string());
                if (!g.find(id)) {
                    g.nodes.push_back({NodeKind::Event, id, kind, 0});
                    if (!text.empty()) {
                        if (!g.find(text)) g.nodes.push_back({NodeKind::TextKey, text, text, 0});
                        g.edges.push_back({id, text, GraphEdge::Kind::EventText});
                    }
                }
                eventKind[id] = kind;
                g.edges.push_back({poolId, id, GraphEdge::Kind::PoolEvent});
            }
        }
    }

    // ---- 2. floors, and the events they can roll ----
    std::set<std::string> referenced;
    if (fs::is_directory(base / "story" / "floors")) {
        std::vector<fs::path> floors;
        for (const auto& e : fs::directory_iterator(base / "story" / "floors")) {
            const std::string stem = e.path().stem().string();
            // F01.json is a floor spec; F01.stage.json is the generated grid -- only the spec carries events
            if (e.path().extension() == ".json" && stem.rfind("F", 0) == 0 && stem.find('.') == std::string::npos)
                floors.push_back(e.path());
        }
        std::sort(floors.begin(), floors.end());
        for (const auto& p : floors) {
            const std::string floorId = leafName(p);
            const json j = readJson(p);
            const int seq = floorSeqOf(floorId);
            g.nodes.push_back({NodeKind::Floor, floorId, floorId, seq});
            if (!j.is_object() || !j.contains("events") || !j["events"].is_array()) continue;
            for (const auto& ev : j["events"]) {
                if (!ev.is_string()) continue;
                const std::string id = ev.get<std::string>();
                referenced.insert(id);
                g.edges.push_back({floorId, id, GraphEdge::Kind::FloorEvent});
                if (!g.find(id)) {
                    g.problems.push_back({GraphProblem::Kind::MissingEventId, id,
                                          floorId + " references an event id that no pool defines"});
                }
            }
        }
    }

    // ---- 3. i18n: which text keys actually resolve ----
    std::set<std::string> strings;
    {
        const json t = readJson(base / "text.json");
        if (t.is_object() && t.contains("strings") && t["strings"].is_object())
            for (auto it = t["strings"].begin(); it != t["strings"].end(); ++it) strings.insert(it.key());
    }
    for (const auto& n : g.nodes) {
        if (n.kind == NodeKind::Event) {
            if (!referenced.empty() && referenced.find(n.id) == referenced.end())
                g.problems.push_back({GraphProblem::Kind::OrphanEvent, n.id,
                                      "no floor references this event"});
        } else if (n.kind == NodeKind::TextKey) {
            if (!strings.empty() && strings.find(n.id) == strings.end())
                g.problems.push_back({GraphProblem::Kind::MissingTextKey, n.id,
                                      "no string in text.json for this key"});
        }
    }
    return g;
}

}  // namespace toms::editor
