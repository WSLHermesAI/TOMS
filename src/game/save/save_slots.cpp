#include "save_slots.h"
#include <cstdio>
#include <ctime>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace toms {

const std::string& defaultSaveDir() {
#ifdef __EMSCRIPTEN__
    // Mounted by emscripten_main.cpp's IDBFS block; falls back to the plain (session-only)
    // MEMFS directory when IndexedDB is unavailable.
    static const std::string dir = "/save";
#else
    static const std::string dir = "save";
#endif
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
    std::error_code ec;
    return std::filesystem::exists(slotPath(saveDir, slot), ec);
}

bool deleteSlot(const std::string& saveDir, int slot) {
    std::error_code ec;
    std::filesystem::remove(slotPath(saveDir, slot), ec);
    flushSaveDir();
    return !ec;
}

bool writeSlotSave(const std::string& saveDir, int slot,
                   const MetaSaveData& meta, const RunSaveData& run,
                   int playTimeSec, const std::string& savedAt, const std::string& stageName) {
    if (slot <= 0) return false;
    ensureSaveDir(saveDir);
    nlohmann::json j;
    j["saveVersion"] = 1;
    j["savedAt"] = savedAt.empty() ? nowStamp() : savedAt;
    j["playTimeSec"] = playTimeSec;
    j["stageName"] = stageName;
    j["meta"] = toJson(meta);
    j["run"] = toJson(run);
    bool ok = writeJsonAtomic(slotPath(saveDir, slot), j);
    if (ok) flushSaveDir();
    return ok;
}

bool readSlotSave(const std::string& saveDir, int slot,
                  MetaSaveData& metaOut, RunSaveData& runOut,
                  int* playTimeSec, std::string* savedAt) {
    nlohmann::json j = readJsonFileSafe(slotPath(saveDir, slot));
    if (j.is_null() || !j.is_object()) return false;
    metaOut = metaFromJson(j.value("meta", nlohmann::json::object()));
    runOut  = runFromJson(j.value("run", nlohmann::json::object()));
    if (playTimeSec) *playTimeSec = j.value("playTimeSec", 0);
    if (savedAt) *savedAt = j.value("savedAt", std::string());
    return true;
}

SlotSummary summarizeSlot(const std::string& saveDir, int slot, int /*maxSlot*/) {
    SlotSummary s;
    s.slot = slot;
    nlohmann::json j = readJsonFileSafe(slotPath(saveDir, slot));
    if (j.is_null() || !j.is_object()) return s;   // exists stays false
    s.exists = true;
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
        nlohmann::json j = readJsonFileSafe(slotPath(saveDir, i));
        if (j.is_null() || !j.is_object()) continue;
        std::string stamp = j.value("savedAt", std::string());
        // Timestamps are fixed-width "YYYY-MM-DD HH:MM:SS", so a string compare is chronological.
        if (best == -1 || stamp > bestStamp) { best = i; bestStamp = stamp; }
    }
    return best;
}

} // namespace toms
