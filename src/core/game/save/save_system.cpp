#include "save_system.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace toms {

// ---- migrations (see save_system.h) -----------------------------------------------------------------------------------
// Entry i upgrades a file from version i+1 to i+2. Versions 1 -> 3 only ADDED fields, so their steps are empty (the
// readers below default what is missing). Example of a real step, for when a field is renamed in version 4:
//     [](nlohmann::json& j) { if (j.contains("oldName")) { j["newName"] = j["oldName"]; j.erase("oldName"); } },
using Migration = std::function<void(nlohmann::json&)>;
static const std::vector<Migration> kMetaMigrations = {
    [](nlohmann::json&) {},   // 1 -> 2
    [](nlohmann::json&) {},   // 2 -> 3: cycleIndex, endingsSeen, hintsUnlocked (defaults)
};
static const std::vector<Migration> kRunMigrations = {
    [](nlohmann::json&) {},   // 1 -> 2
    [](nlohmann::json&) {},   // 2 -> 3: the story state (defaults)
};
static_assert(kMetaSaveSchemaVersion == 3 && kRunSaveSchemaVersion == 3,
              "a new schema version needs its step in kMetaMigrations / kRunMigrations");

static void migrate(nlohmann::json& j, const std::vector<Migration>& steps, int current) {
    if (!j.is_object()) return;
    int ver = std::max(1, j.value("schemaVersion", 1));
    while (ver < current && ver - 1 < (int)steps.size()) {
        steps[(size_t)ver - 1](j);
        ++ver;
    }
    // the parsed result says which version it came from (versionMismatch); the json itself is now `ver`
}

void migrateMetaJson(nlohmann::json& j) { migrate(j, kMetaMigrations, kMetaSaveSchemaVersion); }
void migrateRunJson(nlohmann::json& j) { migrate(j, kRunMigrations, kRunSaveSchemaVersion); }

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
    // S5: forging blueprints known -- meta-scope, survives rebirth (see the field's own comment).
    j["forgeRecipesKnown"] = m.forgeRecipesKnown;
    if (!m.cards.empty()) j["cards"] = cardBookToJson(m.cards);   // monster cards (optional: an older file has none)
    return j;
}

MetaSaveData metaFromJson(const nlohmann::json& in, bool* versionMismatch) {
    MetaSaveData m;
    if (!in.is_object()) { if (versionMismatch) *versionMismatch = true; return m; }
    int ver = in.value("schemaVersion", 0);
    nlohmann::json j = in;
    migrateMetaJson(j);
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
    // "must not refuse an old save" rule from docs/story/STORY_DATA_SCHEMA.md section 9; the caller sees
    // *versionMismatch so it can log/migrate, but loading never fails because of it.
    m.cycleIndex = std::max(1, j.value("cycleIndex", 1));
    m.endingsSeen    = j.value("endingsSeen", std::vector<std::string>{});
    m.hintsUnlocked  = j.value("hintsUnlocked", std::vector<std::string>{});
    // S5: a pre-S5 file simply has none -- empty list is the documented migration default, same
    // rule as every other field above.
    m.forgeRecipesKnown = j.value("forgeRecipesKnown", std::vector<std::string>{});
    m.cards = cardBookFromJson(j.value("cards", nlohmann::json::object()));
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
    p["attrs"] = r.player.attrs;
    p["gear"] = r.player.gear;
    p["weapon"] = r.player.weapon; p["armor"] = r.player.armor; p["talent"] = r.player.talent;
    p["attrPoints"] = r.player.attrPoints;
    p["weaponUses"] = r.player.weaponUses;
    p["crystals"] = r.player.crystals;
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
    // S4: the skill tree's run-scoped progress -- see RunStoryState for what writes/reads it.
    j["skillPoints"] = r.skillPoints;
    j["skillsOwned"] = r.skillsOwned;
    j["eventsStarted"] = r.eventsStarted;
    j["eventsFinished"] = r.eventsFinished;
    if (!r.vars.empty()) j["vars"] = r.vars;
    if (!r.npcTalk.empty()) j["npcTalk"] = r.npcTalk;
    if (!r.eventCounts.empty()) j["eventCounts"] = r.eventCounts;
    j["superMax"] = r.superMax;
    return j;
}

RunSaveData runFromJson(const nlohmann::json& in, bool* versionMismatch) {
    RunSaveData r;
    if (!in.is_object()) { if (versionMismatch) *versionMismatch = true; return r; }
    int ver = in.value("schemaVersion", 0);
    nlohmann::json j = in;
    migrateRunJson(j);
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
        if (p.contains("attrs") && p["attrs"].is_object())
            for (auto& [k, v] : p["attrs"].items())
                if (v.is_number_integer()) r.player.attrs[k] = v.get<int>();
        r.player.gear = p.value("gear", std::vector<std::string>{});
        r.player.weapon = p.value("weapon", std::string());
        r.player.armor = p.value("armor", std::string());
        r.player.talent = p.value("talent", std::string());
        r.player.attrPoints = p.value("attrPoints", 0);
        r.player.crystals = std::max(0, p.value("crystals", 0));
        if (p.contains("weaponUses") && p["weaponUses"].is_object())   // optional: an older save has none (every weapon Lv 1)
            for (auto& [k, v] : p["weaponUses"].items())
                if (v.is_number_integer()) r.player.weaponUses[k] = std::max(0, v.get<int>());
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
    // S4: a pre-S4 file simply has neither -- defaults (0 points, no skills) are the documented
    // migration, same rule as every other S3/S4 field above.
    r.skillPoints = j.value("skillPoints", 0);
    r.skillsOwned = j.value("skillsOwned", std::vector<std::string>{});
    r.eventsStarted = j.value("eventsStarted", std::vector<std::string>{});
    r.eventsFinished = j.value("eventsFinished", std::vector<std::string>{});
    if (j.contains("vars") && j["vars"].is_object())
        for (auto& [k, v] : j["vars"].items()) if (v.is_number_integer()) r.vars[k] = v.get<int>();
    if (j.contains("eventCounts") && j["eventCounts"].is_object())
        for (auto& [k, v] : j["eventCounts"].items()) if (v.is_number_integer()) r.eventCounts[k] = v.get<int>();
    if (j.contains("npcTalk") && j["npcTalk"].is_object())
        for (auto& [k, v] : j["npcTalk"].items())
            if (v.is_array()) { std::vector<int>& t = r.npcTalk[k]; for (auto& n : v) if (n.is_number_integer()) t.push_back(n.get<int>()); }
    r.superMax = j.value("superMax", 5);
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

std::string backupPath(const std::string& path) { return path + ".bak"; }

bool writeJsonAtomic(const std::string& path, const nlohmann::json& j, bool keepBackup) {
    std::string tmp = path + ".tmp";
    {
        const std::string text = j.dump(2);
        FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) { fprintf(stderr, "[save_system] cannot open %s for write\n", tmp.c_str()); return false; }
        bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size() && std::fflush(f) == 0;
        // Flushed to the disk itself, not only to the OS cache: otherwise the rename below can reach the disk
        // before the data does, and a power cut leaves an empty or partial file under the real name.
#ifdef _WIN32
        ok = ok && _commit(_fileno(f)) == 0;
#else
        ok = ok && fsync(fileno(f)) == 0;
#endif
        ok = (std::fclose(f) == 0) && ok;
        if (!ok) { fprintf(stderr, "[save_system] write failed for %s\n", tmp.c_str()); return false; }
    }
    std::error_code ec;
    // The file being replaced becomes the backup -- only if it is still good (a damaged file must not push a
    // good backup out). A failed backup step does not stop the save.
    if (keepBackup && std::filesystem::exists(path, ec) && readJsonFileSafe(path).is_object())
        std::filesystem::rename(path, backupPath(path), ec);
    // std::filesystem::rename replaces an existing file on every platform; std::rename does not on
    // Windows, where it made every save after the first one fail.
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        fprintf(stderr, "[save_system] rename %s -> %s failed: %s\n", tmp.c_str(), path.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

} // namespace toms
