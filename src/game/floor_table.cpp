// floor_table.cpp — see floor_table.h.
#include "floor_table.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace toms {

namespace {

// "ch_07" -> 7, so the act's ordinal comes from the same string the chapters use.
int actOrdinal(const std::string& act) {
    int value = 0;
    bool haveDigit = false;
    for (char c : act) {
        if (c >= '0' && c <= '9') { value = value * 10 + (c - '0'); haveDigit = true; }
        else if (haveDigit) break;               // stop at the end of the numeric run
    }
    return haveDigit ? value : 1;
}

nlohmann::json readJson(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return nlohmann::json();
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.empty()) return nlohmann::json();
    try {
        return nlohmann::json::parse(text);
    } catch (...) {
        return nlohmann::json();                 // a malformed spec is skipped, not fatal
    }
}

} // namespace

FloorTable FloorTable::fromSpecs(const std::vector<nlohmann::json>& specs) {
    FloorTable table;
    std::map<std::string, FloorInfo> byId;
    std::vector<std::string> order;              // chain order, filled while walking nextFloor

    for (const auto& j : specs) {
        if (!j.is_object() || !j.contains("id")) continue;
        FloorInfo f;
        f.id = j.value("id", std::string());
        if (f.id.empty()) continue;
        f.act = j.value("act", std::string());
        f.actIndex = actOrdinal(f.act);
        f.indexInAct = j.value("indexInAct", 1);
        f.role = j.value("role", std::string("normal"));
        f.seal = j.value("seal", std::string());
        f.handAuthoredStage = j.value("handAuthoredStage", std::string());
        if (j.contains("nextFloor") && j["nextFloor"].is_string()) f.nextFloor = j["nextFloor"].get<std::string>();
        if (j.contains("name")) f.nameKey = j["name"].is_string() ? j["name"].get<std::string>() : std::string();
        if (j.contains("events") && j["events"].is_array()) f.eventCount = (int)j["events"].size();
        byId[f.id] = f;
    }
    if (byId.empty()) return table;

    // Order by walking the nextFloor chain from the first floor (the one nothing points at). That
    // way `seq` -- and therefore the HUD counter -- always agrees with the way the run actually
    // progresses, instead of trusting the file names to be in order.
    std::map<std::string, int> incoming;
    for (auto& kv : byId) if (!kv.second.nextFloor.empty()) incoming[kv.second.nextFloor]++;
    std::string cursor;
    for (auto& kv : byId) {
        if (incoming.count(kv.first) == 0) { cursor = kv.first; break; }   // the entrance
    }
    if (cursor.empty()) {                        // cyclic/degenerate data: fall back to id order
        for (auto& kv : byId) order.push_back(kv.first);
        std::sort(order.begin(), order.end());
    } else {
        while (!cursor.empty() && byId.count(cursor) && order.size() <= byId.size()) {
            order.push_back(cursor);
            cursor = byId[cursor].nextFloor;
        }
        // Any spec the chain did not visit (a broken link would strand a floor) is appended in id
        // order rather than dropped: an unreachable floor must show up in the table so a test can
        // report it, not silently vanish from the tower.
        std::vector<std::string> rest;
        for (auto& kv : byId)
            if (std::find(order.begin(), order.end(), kv.first) == order.end()) rest.push_back(kv.first);
        std::sort(rest.begin(), rest.end());
        order.insert(order.end(), rest.begin(), rest.end());
    }

    int seq = 1;
    for (const std::string& id : order) {
        FloorInfo f = byId[id];
        f.seq = seq++;
        table.floors_.push_back(f);
    }
    return table;
}

FloorTable FloorTable::loadFromDirectory(const std::string& dir) {
    std::vector<nlohmann::json> specs;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return FloorTable();
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.empty() || name[0] != 'F') continue;
        if (name.size() >= 11 && name.compare(name.size() - 11, 11, ".stage.json") == 0) continue;  // the map
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        nlohmann::json j = readJson(entry.path().string());
        if (j.is_object()) specs.push_back(j);
    }
    return fromSpecs(specs);
}

const FloorInfo& FloorTable::at(int seq) const {
    static const FloorInfo kEmpty;
    if (floors_.empty()) return kEmpty;
    if (seq < 1) seq = 1;
    if (seq > (int)floors_.size()) seq = (int)floors_.size();
    return floors_[seq - 1];
}

const FloorInfo* FloorTable::find(const std::string& id) const {
    for (const FloorInfo& f : floors_) if (f.id == id) return &f;
    return nullptr;
}

std::string FloorTable::next(const std::string& id) const {
    const FloorInfo* f = find(id);
    return f ? f->nextFloor : std::string();
}

std::string FloorTable::prev(const std::string& id) const {
    for (int i = 0; i < (int)floors_.size(); i++) {
        if (floors_[i].id == id) return i == 0 ? std::string() : floors_[i - 1].id;
    }
    return std::string();
}

int FloorTable::seqOf(const std::string& id) const {
    for (const FloorInfo& f : floors_) if (f.id == id) return f.seq;
    return 0;
}

std::string FloorTable::mapRelPath(const std::string& id) const {
    const FloorInfo* f = find(id);
    if (!f) return std::string();
    if (!f->handAuthoredStage.empty()) return "data/stages/" + f->handAuthoredStage;
    return "data/story/floors/" + f->id + ".stage.json";
}

} // namespace toms
