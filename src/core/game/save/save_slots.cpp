#include "save_slots.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace toms {

const std::string& f_DefaultSaveDir() {
    // An override wins on every platform -- the host sets it before the first save is touched. Android
    // needs it: the app cannot write next to its own binary and there is no relative working directory,
    // so main_sdl.cpp points it at SDL_GetPrefPath(). Desktop and web are unaffected unless someone
    // deliberately sets TOMS_SAVE_DIR. A function-local static, so it is initialised on first use, i.e.
    // after the host has had a chance to set the variable.
    static const std::string l_strDir = [] {
        if (const char* l_pszE = std::getenv("TOMS_SAVE_DIR"); l_pszE && *l_pszE) return std::string(l_pszE);
#ifdef __EMSCRIPTEN__
        // Mounted by src/game/src/main_sdl.cpp's IDBFS block; falls back to the plain (session-only)
        // MEMFS directory when IndexedDB is unavailable.
        return std::string("/save");
#else
        return std::string("save");
#endif
    }();
    return l_strDir;
}

bool f_EnsureSaveDir(const std::string& l_strSaveDir) {
    std::error_code l_oEc;
    if (std::filesystem::exists(l_strSaveDir, l_oEc)) return true;
    std::filesystem::create_directories(l_strSaveDir, l_oEc);
    if (l_oEc) { fprintf(stderr, "[save_slots] cannot create %s: %s\n", l_strSaveDir.c_str(), l_oEc.message().c_str()); return false; }
    return true;
}

void f_FlushSaveDir() {
#ifdef __EMSCRIPTEN__
    // Async: schedules an IndexedDB flush of the IDBFS mount. Safe to call after every write.
    EM_ASM({ if (typeof Module !== 'undefined' && Module.__tomsSyncfs) Module.__tomsSyncfs(false); });
#endif
}

std::string f_SlotPath(const std::string& l_strSaveDir, int l_iSlot) {
    return l_strSaveDir + "/slot" + std::to_string(l_iSlot) + ".json";
}

std::string f_NowStamp() {
    std::time_t l_i64T = std::time(nullptr);
    std::tm l_oTmv{};
#ifdef _WIN32
    localtime_s(&l_oTmv, &l_i64T);
#else
    localtime_r(&l_i64T, &l_oTmv);
#endif
    char l_pszBuf[32];
    std::strftime(l_pszBuf, sizeof(l_pszBuf), "%Y-%m-%d %H:%M:%S", &l_oTmv);
    return std::string(l_pszBuf);
}

bool f_SlotExists(const std::string& l_strSaveDir, int l_iSlot) {
    std::error_code l_oEc;   // the backup counts: a save cut off between its two renames left only the backup
    return std::filesystem::exists(f_SlotPath(l_strSaveDir, l_iSlot), l_oEc) ||
           std::filesystem::exists(f_BackupPath(f_SlotPath(l_strSaveDir, l_iSlot)), l_oEc);
}

bool f_DeleteSlot(const std::string& l_strSaveDir, int l_iSlot) {
    std::error_code l_oEc;
    std::filesystem::remove(f_SlotPath(l_strSaveDir, l_iSlot), l_oEc);
    const bool l_bOk = !l_oEc;
    std::filesystem::remove(f_BackupPath(f_SlotPath(l_strSaveDir, l_iSlot)), l_oEc);   // a deleted slot leaves no backup to "restore"
    f_FlushSaveDir();
    return l_bOk;
}

// The slot's json: its file, or -- when that is missing or damaged -- the backup written before it (writeJsonAtomic).
static nlohmann::json f_ReadSlotJson(const std::string& l_strSaveDir, int l_iSlot, bool* l_pFromBackup) {
    if (l_pFromBackup) *l_pFromBackup = false;
    const std::string l_strPath = f_SlotPath(l_strSaveDir, l_iSlot);
    std::error_code l_oEc;
    if (std::filesystem::exists(l_strPath, l_oEc)) {
        nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
        if (l_jJ.is_object()) return l_jJ;
    }
    if (std::filesystem::exists(f_BackupPath(l_strPath), l_oEc)) {
        nlohmann::json l_jJ = f_ReadJsonFileSafe(f_BackupPath(l_strPath));
        if (l_jJ.is_object()) {
            if (l_pFromBackup) *l_pFromBackup = true;
            fprintf(stderr, "[save_slots] slot %d is damaged; using its backup\n", l_iSlot);
            return l_jJ;
        }
    }
    return {};
}

static bool f_JsonIsNewer(const nlohmann::json& l_jJ) {
    if (!l_jJ.is_object()) return false;
    if (l_jJ.value("saveVersion", 0) > g_iSlotSaveVersion) return true;
    const auto l_fnVer = [&](const char* l_pszPart) {
        return l_jJ.contains(l_pszPart) && l_jJ[l_pszPart].is_object() ? l_jJ[l_pszPart].value("schemaVersion", 0) : 0;
    };
    return l_fnVer("meta") > g_iMetaSaveSchemaVersion || l_fnVer("run") > g_iRunSaveSchemaVersion;
}

bool f_SlotIsNewer(const std::string& l_strSaveDir, int l_iSlot) { return f_JsonIsNewer(f_ReadSlotJson(l_strSaveDir, l_iSlot, nullptr)); }

bool f_WriteSlotSave(const std::string& l_strSaveDir, int l_iSlot,
                   const MetaSaveData& l_oMeta, const RunSaveData& l_oRun,
                   int l_iPlayTimeSec, const std::string& l_strSavedAt, const std::string& l_strStageName) {
    if (l_iSlot <= 0) return false;
    f_EnsureSaveDir(l_strSaveDir);
    // A save from a newer version holds fields this build does not know: writing over it would lose them.
    if (f_SlotIsNewer(l_strSaveDir, l_iSlot)) {
        fprintf(stderr, "[save_slots] slot %d was saved by a newer version of the game; not overwriting it\n", l_iSlot);
        return false;
    }
    nlohmann::json l_jJ;
    l_jJ["saveVersion"] = g_iSlotSaveVersion;
    l_jJ["savedAt"] = l_strSavedAt.empty() ? f_NowStamp() : l_strSavedAt;
    l_jJ["playTimeSec"] = l_iPlayTimeSec;
    l_jJ["stageName"] = l_strStageName;
    l_jJ["meta"] = f_ToJson(l_oMeta);
    l_jJ["run"] = f_ToJson(l_oRun);
    bool l_bOk = f_WriteJsonAtomic(f_SlotPath(l_strSaveDir, l_iSlot), l_jJ, /*keepBackup=*/true);
    if (l_bOk) f_FlushSaveDir();
    return l_bOk;
}

bool f_ReadSlotSave(const std::string& l_strSaveDir, int l_iSlot,
                  MetaSaveData& l_oMetaOut, RunSaveData& l_oRunOut,
                  int* l_pPlayTimeSec, std::string* l_pSavedAt, SlotLoadInfo* l_pInfo) {
    bool l_bFromBackup = false;
    nlohmann::json l_jJ = f_ReadSlotJson(l_strSaveDir, l_iSlot, &l_bFromBackup);
    if (l_jJ.is_null() || !l_jJ.is_object()) return false;
    if (l_pInfo) { l_pInfo->m_bFromBackup = l_bFromBackup; l_pInfo->m_bNewer = f_JsonIsNewer(l_jJ); }
    l_oMetaOut = f_MetaFromJson(l_jJ.value("meta", nlohmann::json::object()));
    l_oRunOut  = f_RunFromJson(l_jJ.value("run", nlohmann::json::object()));
    if (l_pPlayTimeSec) *l_pPlayTimeSec = l_jJ.value("playTimeSec", 0);
    if (l_pSavedAt) *l_pSavedAt = l_jJ.value("savedAt", std::string());
    return true;
}

bool f_ReadSaveFile(const std::string& l_strPath, MetaSaveData& l_oMetaOut, RunSaveData& l_oRunOut, int* l_pPlayTimeSec) {
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return false;
    l_oMetaOut = f_MetaFromJson(l_jJ.value("meta", nlohmann::json::object()));
    l_oRunOut  = f_RunFromJson(l_jJ.value("run", nlohmann::json::object()));
    if (l_pPlayTimeSec) *l_pPlayTimeSec = l_jJ.value("playTimeSec", 0);
    return true;
}

SlotSummary f_SummarizeSlot(const std::string& l_strSaveDir, int l_iSlot, int /*maxSlot*/) {
    SlotSummary l_oS;
    l_oS.m_iSlot = l_iSlot;
    nlohmann::json l_jJ = f_ReadSlotJson(l_strSaveDir, l_iSlot, &l_oS.m_bFromBackup);
    if (l_jJ.is_null() || !l_jJ.is_object()) {
        // exists stays false; a file that is there but unreadable is "damaged", never "empty" (an empty slot is
        // offered for a new game, which would write over what might still be recovered by hand)
        std::error_code l_oEc;
        l_oS.m_bDamaged = std::filesystem::exists(f_SlotPath(l_strSaveDir, l_iSlot), l_oEc) ||
                    std::filesystem::exists(f_BackupPath(f_SlotPath(l_strSaveDir, l_iSlot)), l_oEc);
        return l_oS;
    }
    l_oS.m_bExists = true;
    l_oS.m_bNewer = f_JsonIsNewer(l_jJ);
    l_oS.m_strSavedAt = l_jJ.value("savedAt", std::string());
    l_oS.m_iPlayTimeSec = l_jJ.value("playTimeSec", 0);
    l_oS.m_strStageName = l_jJ.value("stageName", std::string());
    const nlohmann::json& l_jRun = l_jJ.contains("run") ? l_jJ["run"] : nlohmann::json::object();
    l_oS.m_strStageId = l_jRun.value("currentStageId", std::string());
    if (l_jRun.contains("player") && l_jRun["player"].is_object()) {
        const auto& l_jP = l_jRun["player"];
        l_oS.m_iLv = l_jP.value("lv", 1);
        l_oS.m_iHp = l_jP.value("hp", 0);
        l_oS.m_iMaxhp = l_jP.value("maxhp", 0);
        l_oS.m_iAtk = l_jP.value("atk", 0);
        l_oS.m_iDef = l_jP.value("def", 0);
        l_oS.m_iGold = l_jP.value("gold", 0);
    }
    return l_oS;
}

int f_FirstEmptySlot(const std::string& l_strSaveDir, int l_iSlotCount) {
    for (int l_iI = 1; l_iI <= l_iSlotCount; l_iI++)
        if (!f_SlotExists(l_strSaveDir, l_iI)) return l_iI;
    return -1;
}

int f_NewestSlot(const std::string& l_strSaveDir, int l_iSlotCount) {
    int l_iBest = -1;
    std::string l_strBestStamp;
    for (int l_iI = 1; l_iI <= l_iSlotCount; l_iI++) {
        nlohmann::json l_jJ = f_ReadSlotJson(l_strSaveDir, l_iI, nullptr);
        if (l_jJ.is_null() || !l_jJ.is_object()) continue;
        std::string l_strStamp = l_jJ.value("savedAt", std::string());
        // Timestamps are fixed-width "YYYY-MM-DD HH:MM:SS", so a string compare is chronological.
        if (l_iBest == -1 || l_strStamp > l_strBestStamp) { l_iBest = l_iI; l_strBestStamp = l_strStamp; }
    }
    return l_iBest;
}

} // namespace toms
