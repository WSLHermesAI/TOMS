// save_slots.h — Continue page: numbered save files, each holding a Meta + Run snapshot.
//
// save_system.h defines the *schema* (what a Meta/Run save contains) and atomic file I/O;
// this file adds the *slot* layer the title phase needs: which files exist, what to show for
// each in the Continue list, and reading one back to resume a run.
//
// Layout:
//   desktop:  save/slot1.json, save/slot2.json, ...   (relative to the game's CWD)
//   browser:  /save/slot1.json, ...                   (Emscripten IDBFS, persisted to IndexedDB
//                                                      by flushSaveDir(); see src/game/src/main_sdl.cpp)
#pragma once
#include "save_system.h"
#include <string>

namespace toms {

// Directory save files live in for this platform. Never empty.
const std::string& f_DefaultSaveDir();
// The player's settings file (game_settings.h) in it: the one place its name is written.
inline std::string f_SettingsPath() { return f_DefaultSaveDir() + "/settings.json"; }
// Creates the save directory if needed. Returns false if it could not be created.
bool f_EnsureSaveDir(const std::string& l_strSaveDir);
// Flushes pending writes to persistent storage. No-op on desktop; on the browser this kicks
// off an async IDBFS sync (see jsSyncfs), so a page reload does not lose the save.
void f_FlushSaveDir();

// "<saveDir>/slot<N>.json" — also accepts a full path (used by tests).
std::string f_SlotPath(const std::string& l_strSaveDir, int l_iSlot);
// Local timestamp, "YYYY-MM-DD HH:MM:SS" (used for the Continue row's "saved" column).
std::string f_NowStamp();

bool f_SlotExists(const std::string& l_strSaveDir, int l_iSlot);
bool f_DeleteSlot(const std::string& l_strSaveDir, int l_iSlot);

// The slot file's own format version (its header: savedAt, playTimeSec, meta, run). Raised only when that header
// changes; the meta / run parts carry their own schemaVersion.
constexpr int g_iSlotSaveVersion = 1;

// What the Continue page shows for one slot.
struct SlotSummary {
    int m_iSlot = 0;
    bool m_bExists = false;       // loadable (from the file or, failing that, its backup)
    bool m_bDamaged = false;      // a file is there but neither it nor its backup can be read: never offered as empty
    bool m_bFromBackup = false;   // the slot file was unreadable and the backup (the save before it) is shown instead
    bool m_bNewer = false;        // written by a newer version of the game: loads, but is never overwritten
    std::string m_strStageId;
    std::string m_strStageName;     // from data/stages/<id>.json's "name", when readable
    int m_iLv = 1;
    int m_iHp = 0, m_iMaxhp = 0, m_iAtk = 0, m_iDef = 0, m_iGold = 0;
    std::string m_strSavedAt;
    int m_iPlayTimeSec = 0;
};

bool f_WriteSlotSave(const std::string& l_strSaveDir, int l_iSlot,
                   const MetaSaveData& l_oMeta, const RunSaveData& l_oRun,
                   int l_iPlayTimeSec, const std::string& l_strSavedAt, const std::string& l_strStageName);
// How a slot was read (readSlotSave's optional report).
struct SlotLoadInfo {
    bool m_bFromBackup = false;   // the slot file was damaged; its backup was loaded
    bool m_bNewer = false;        // the file comes from a newer version of the game (writeSlotSave will not replace it)
};
bool f_ReadSlotSave(const std::string& l_strSaveDir, int l_iSlot,
                  MetaSaveData& l_oMetaOut, RunSaveData& l_oRunOut,
                  int* l_pPlayTimeSec = nullptr, std::string* l_pSavedAt = nullptr, SlotLoadInfo* l_pInfo = nullptr);
// A save in the slot format (what writeSlotSave writes) read from any file -- test scenarios start from one
// (docs/gameplay/08_TEST_SCENARIOS.md). No backup fallback: a scenario's file is test data, not a player's save.
bool f_ReadSaveFile(const std::string& l_strPath, MetaSaveData& l_oMetaOut, RunSaveData& l_oRunOut, int* l_pPlayTimeSec = nullptr);
// True when the slot holds a save from a newer version of the game (its file or, if that is damaged, its backup).
bool f_SlotIsNewer(const std::string& l_strSaveDir, int l_iSlot);
// Reads just the header/player fields the Continue list needs (no full parse of entityStatus).
SlotSummary f_SummarizeSlot(const std::string& l_strSaveDir, int l_iSlot, int l_iMaxSlot = 0);
// Lowest unused slot in [1..slotCount], or -1 when every slot is taken.
int f_FirstEmptySlot(const std::string& l_strSaveDir, int l_iSlotCount);
// Slot with the newest savedAt stamp, or -1 when there are none.
int f_NewestSlot(const std::string& l_strSaveDir, int l_iSlotCount);

} // namespace toms
