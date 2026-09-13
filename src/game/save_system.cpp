#include "save_system.h"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace toms {

nlohmann::json toJson(const MetaSaveData& m) {
    nlohmann::json j;
    j["schemaVersion"] = m.schemaVersion;
    j["storyFlags"] = std::vector<std::string>(m.storyFlags.begin(), m.storyFlags.end());
    j["currentBeat"] = m.currentBeat;
    j["unlockedStages"] = m.unlockedStages;
    j["missionsClaimed"] = m.missionsClaimed;
    j["permanentAtkBonus"] = m.permanentAtkBonus;
    j["permanentDefBonus"] = m.permanentDefBonus;
    j["permanentHpBonus"] = m.permanentHpBonus;
    // S3 (schemaVersion 3)
    j["cycleIndex"] = m.cycleIndex;
    j["endingsSeen"] = m.endingsSeen;
    j["hintsUnlocked"] = m.hintsUnlocked;
    return j;
}

MetaSaveData metaFromJson(const nlohmann::json& j, bool* versionMismatch) {
    MetaSaveData m;
    if (!j.is_object()) { if (versionMismatch) *versionMismatch = true; return m; }
    int ver = j.value("schemaVersion", 0);
    if (versionMismatch) *versionMismatch = (ver != kMetaSaveSchemaVersion);
    m.schemaVersion = ver;
    if (j.contains("storyFlags") && j["storyFlags"].is_array())
        for (auto& f : j["storyFlags"]) if (f.is_string()) m.storyFlags.insert(f.get<std::string>());
    m.currentBeat = j.value("currentBeat", 0);
    m.unlockedStages  = j.value("unlockedStages", std::vector<std::string>{});
    m.missionsClaimed = j.value("missionsClaimed", std::vector<std::string>{});
    m.permanentAtkBonus = j.value("permanentAtkBonus", 0);
    m.permanentDefBonus = j.value("permanentDefBonus", 0);
    m.permanentHpBonus  = j.value("permanentHpBonus", 0);
    // S3: absent in a pre-v3 file -> the declared defaults (cycleIndex 1, empty lists). This is the
    // "must not refuse an old save" rule from docs/STORY_DATA_SCHEMA.md section 9; the caller sees
    // *versionMismatch so it can log/migrate, but loading never fails because of it.
    m.cycleIndex = std::max(1, j.value("cycleIndex", 1));
    m.endingsSeen    = j.value("endingsSeen", std::vector<std::string>{});
    m.hintsUnlocked  = j.value("hintsUnlocked", std::vector<std::string>{});
    return m;
}

nlohmann::json toJson(const RunSaveData& r) {
    nlohmann::json j;
    j["schemaVersion"] = r.schemaVersion;
    j["currentStageId"] = r.currentStageId;
    nlohmann::json p;
    p["hp"] = r.player.hp; p["maxhp"] = r.player.maxhp;
    p["atk"] = r.player.atk; p["def"] = r.player.def;
    p["gold"] = r.player.gold; p["exp"] = r.player.exp; p["lv"] = r.player.lv;
    p["key_yellow"] = r.player.key_yellow; p["key_blue"] = r.player.key_blue; p["key_red"] = r.player.key_red;
    p["inv"] = r.player.inv;
    j["player"] = p;
    j["entityStatus"] = r.entityStatus;
    // S3 (schemaVersion 3): the run's story state -- see RunStoryState for what writes/reads it.
    j["choices"] = r.choices;
    j["counters"] = r.counters;
    j["sideStories"] = r.sideStories;
    j["flags"] = r.flags;
    j["floor"] = r.floor;
    j["clearedFloors"] = r.clearedFloors;
    j["shards"] = r.shards;
    j["deathsTotal"] = r.deathsTotal;
    j["deathsNonBoss"] = r.deathsNonBoss;
    return j;
}

RunSaveData runFromJson(const nlohmann::json& j, bool* versionMismatch) {
    RunSaveData r;
    if (!j.is_object()) { if (versionMismatch) *versionMismatch = true; return r; }
    int ver = j.value("schemaVersion", 0);
    if (versionMismatch) *versionMismatch = (ver != kRunSaveSchemaVersion);
    r.schemaVersion = ver;
    r.currentStageId = j.value("currentStageId", std::string());
    if (j.contains("player") && j["player"].is_object()) {
        auto& p = j["player"];
        r.player.hp = p.value("hp", 120);
        r.player.maxhp = p.value("maxhp", 120);
        r.player.atk = p.value("atk", 12);
        r.player.def = p.value("def", 4);
        r.player.gold = p.value("gold", 0);
        r.player.exp = p.value("exp", 0);
        r.player.lv = p.value("lv", 1);
        r.player.key_yellow = p.value("key_yellow", 0);
        r.player.key_blue = p.value("key_blue", 0);
        r.player.key_red = p.value("key_red", 0);
        r.player.inv = p.value("inv", std::vector<std::string>{});
    }
    if (j.contains("entityStatus") && j["entityStatus"].is_object())
        for (auto& [k, v] : j["entityStatus"].items())
            if (v.is_string()) r.entityStatus[k] = v.get<std::string>();
    // S3: a pre-v3 run file simply has none of these; defaults above (floor F01, everything empty)
    // are the documented migration. Loading must never be refused for a version difference.
    if (j.contains("choices") && j["choices"].is_object())
        for (auto& [k, v] : j["choices"].items()) if (v.is_string()) r.choices[k] = v.get<std::string>();
    if (j.contains("counters") && j["counters"].is_object())
        for (auto& [k, v] : j["counters"].items()) if (v.is_number_integer()) r.counters[k] = v.get<int>();
    if (j.contains("sideStories") && j["sideStories"].is_object())
        for (auto& [k, v] : j["sideStories"].items()) if (v.is_string()) r.sideStories[k] = v.get<std::string>();
    if (j.contains("flags") && j["flags"].is_object())
        for (auto& [k, v] : j["flags"].items()) if (v.is_boolean()) r.flags[k] = v.get<bool>();
    r.floor = j.value("floor", std::string("F01"));
    r.clearedFloors = j.value("clearedFloors", std::vector<std::string>{});
    r.shards = j.value("shards", std::vector<std::string>{});
    r.deathsTotal = j.value("deathsTotal", 0);
    r.deathsNonBoss = j.value("deathsNonBoss", 0);
    return r;
}

nlohmann::json readJsonFileSafe(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "[save_system] cannot open %s\n", path.c_str()); return {}; }
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) { fprintf(stderr, "[save_system] empty file %s\n", path.c_str()); return {}; }
    try {
        return nlohmann::json::parse(buf);
    } catch (const std::exception& e) {
        fprintf(stderr, "[save_system] parse error in %s: %s\n", path.c_str(), e.what());
        return {};
    }
}

bool writeJsonAtomic(const std::string& path, const nlohmann::json& j) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { fprintf(stderr, "[save_system] cannot open %s for write\n", tmp.c_str()); return false; }
        f << j.dump(2);
        if (!f) { fprintf(stderr, "[save_system] write failed for %s\n", tmp.c_str()); return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        fprintf(stderr, "[save_system] rename %s -> %s failed\n", tmp.c_str(), path.c_str());
        return false;
    }
    return true;
}

} // namespace toms
