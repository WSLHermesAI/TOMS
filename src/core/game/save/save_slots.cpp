#include "save_slots.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace toms {

const std::string& defaultSaveDir() {
    // An override wins on every platform -- the host sets it before the first save is touched. Android
    // needs it: the app cannot write next to its own binary and there is no relative working directory,
    // so main_sdl.cpp points it at SDL_GetPrefPath(). Desktop and web are unaffected unless someone
    // deliberately sets TOMS_SAVE_DIR. A function-local static, so it is initialised on first use, i.e.
    // after the host has had a chance to set the variable.
    static const std::string dir = [] {
        if (const char* e = std::getenv("TOMS_SAVE_DIR"); e && *e) return std::string(e);
#ifdef __EMSCRIPTEN__
        // Mounted by src/game/src/main_sdl.cpp's IDBFS block; falls back to the plain (session-only)
        // MEMFS directory when IndexedDB is unavailable.
        return std::string("/save");
#else
        return std::string("save");
#endif
    }();
    return dir;
}

bool ensureSaveDir(const std::string& saveDir) {
    std::error_code ec;
    if (std::filesystem::exists(saveDir, ec)) return true;
    std::filesystem::create_directories(saveDir, ec);
    if (ec) { fprintf(stderr, "[save_slots] cannot create %s: %s\n", saveDir.c_str(), ec.message().c_str()); return false; }
    return true;
}

void flushSaveDir() {
#ifdef __EMSCRIPTEN__
    // Async: schedules an IndexedDB flush of the IDBFS mount. Safe to call after every write.
    EM_ASM({ if (typeof Module !== 'undefined' && Module.__tomsSyncfs) Module.__tomsSyncfs(false); });
#endif
}

std::string slotPath(const std::string& saveDir, int slot) {
    return saveDir + "/slot" + std::to_string(slot) + ".json";
}

std::string nowStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return std::string(buf);
}

bool slotExists(const std::string& saveDir, int slot) {
    std::error_code ec;   // the backup counts: a save cut off between its two renames left only the backup
    return std::filesystem::exists(slotPath(saveDir, slot), ec) ||
           std::filesystem::exists(backupPath(slotPath(saveDir, slot)), ec);
}

bool deleteSlot(const std::string& saveDir, int slot) {
    std::error_code ec;
    std::filesystem::remove(slotPath(saveDir, slot), ec);
    const bool ok = !ec;
    std::filesystem::remove(backupPath(slotPath(saveDir, slot)), ec);   // a deleted slot leaves no backup to "restore"
    flushSaveDir();
    return ok;
}

// The slot's json: its file, or -- when that is missing or damaged -- the backup written before it (writeJsonAtomic).
static nlohmann::json readSlotJson(const std::string& saveDir, int slot, bool* fromBackup) {
    if (fromBackup) *fromBackup = false;
    const std::string path = slotPath(saveDir, slot);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        nlohmann::json j = readJsonFileSafe(path);
        if (j.is_object()) return j;
    }
    if (std::filesystem::exists(backupPath(path), ec)) {
        nlohmann::json j = readJsonFileSafe(backupPath(path));
        if (j.is_object()) {
            if (fromBackup) *fromBackup = true;
            fprintf(stderr, "[save_slots] slot %d is damaged; using its backup\n", slot);
            return j;
        }
    }
    return {};
}

static bool jsonIsNewer(const nlohmann::json& j) {
    if (!j.is_object()) return false;
    if (j.value("saveVersion", 0) > kSlotSaveVersion) return true;
    const auto ver = [&](const char* part) {
        return j.contains(part) && j[part].is_object() ? j[part].value("schemaVersion", 0) : 0;
    };
    return ver("meta") > kMetaSaveSchemaVersion || ver("run") > kRunSaveSchemaVersion;
}

bool slotIsNewer(const std::string& saveDir, int slot) { return jsonIsNewer(readSlotJson(saveDir, slot, nullptr)); }

bool writeSlotSave(const std::string& saveDir, int slot,
                   const MetaSaveData& meta, const RunSaveData& run,
                   int playTimeSec, const std::string& savedAt, const std::string& stageName) {
    if (slot <= 0) return false;
    ensureSaveDir(saveDir);
    // A save from a newer version holds fields this build does not know: writing over it would lose them.
    if (slotIsNewer(saveDir, slot)) {
        fprintf(stderr, "[save_slots] slot %d was saved by a newer version of the game; not overwriting it\n", slot);
        return false;
    }
    nlohmann::json j;
    j["saveVersion"] = kSlotSaveVersion;
    j["savedAt"] = savedAt.empty() ? nowStamp() : savedAt;
    j["playTimeSec"] = playTimeSec;
    j["stageName"] = stageName;
    j["meta"] = toJson(meta);
    j["run"] = toJson(run);
    bool ok = writeJsonAtomic(slotPath(saveDir, slot), j, /*keepBackup=*/true);
    if (ok) flushSaveDir();
    return ok;
}

bool readSlotSave(const std::string& saveDir, int slot,
                  MetaSaveData& metaOut, RunSaveData& runOut,
                  int* playTimeSec, std::string* savedAt, SlotLoadInfo* info) {
    bool fromBackup = false;
    nlohmann::json j = readSlotJson(saveDir, slot, &fromBackup);
    if (j.is_null() || !j.is_object()) return false;
    if (info) { info->fromBackup = fromBackup; info->newer = jsonIsNewer(j); }
    metaOut = metaFromJson(j.value("meta", nlohmann::json::object()));
    runOut  = runFromJson(j.value("run", nlohmann::json::object()));
    if (playTimeSec) *playTimeSec = j.value("playTimeSec", 0);
    if (savedAt) *savedAt = j.value("savedAt", std::string());
    return true;
}

SlotSummary summarizeSlot(const std::string& saveDir, int slot, int /*maxSlot*/) {
    SlotSummary s;
    s.slot = slot;
    nlohmann::json j = readSlotJson(saveDir, slot, &s.fromBackup);
    if (j.is_null() || !j.is_object()) {
        // exists stays false; a file that is there but unreadable is "damaged", never "empty" (an empty slot is
        // offered for a new game, which would write over what might still be recovered by hand)
        std::error_code ec;
        s.damaged = std::filesystem::exists(slotPath(saveDir, slot), ec) ||
                    std::filesystem::exists(backupPath(slotPath(saveDir, slot)), ec);
        return s;
    }
    s.exists = true;
    s.newer = jsonIsNewer(j);
    s.savedAt = j.value("savedAt", std::string());
    s.playTimeSec = j.value("playTimeSec", 0);
    s.stageName = j.value("stageName", std::string());
    const nlohmann::json& run = j.contains("run") ? j["run"] : nlohmann::json::object();
    s.stageId = run.value("currentStageId", std::string());
    if (run.contains("player") && run["player"].is_object()) {
        const auto& p = run["player"];
        s.lv = p.value("lv", 1);
        s.hp = p.value("hp", 0);
        s.maxhp = p.value("maxhp", 0);
        s.atk = p.value("atk", 0);
        s.def = p.value("def", 0);
        s.gold = p.value("gold", 0);
    }
    return s;
}

int firstEmptySlot(const std::string& saveDir, int slotCount) {
    for (int i = 1; i <= slotCount; i++)
        if (!slotExists(saveDir, i)) return i;
    return -1;
}

int newestSlot(const std::string& saveDir, int slotCount) {
    int best = -1;
    std::string bestStamp;
    for (int i = 1; i <= slotCount; i++) {
        nlohmann::json j = readSlotJson(saveDir, i, nullptr);
        if (j.is_null() || !j.is_object()) continue;
        std::string stamp = j.value("savedAt", std::string());
        // Timestamps are fixed-width "YYYY-MM-DD HH:MM:SS", so a string compare is chronological.
        if (best == -1 || stamp > bestStamp) { best = i; bestStamp = stamp; }
    }
    return best;
}

} // namespace toms
