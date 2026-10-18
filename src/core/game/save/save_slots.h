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
const std::string& defaultSaveDir();
// Creates the save directory if needed. Returns false if it could not be created.
bool ensureSaveDir(const std::string& saveDir);
// Flushes pending writes to persistent storage. No-op on desktop; on the browser this kicks
// off an async IDBFS sync (see jsSyncfs), so a page reload does not lose the save.
void flushSaveDir();

// "<saveDir>/slot<N>.json" — also accepts a full path (used by tests).
std::string slotPath(const std::string& saveDir, int slot);
// Local timestamp, "YYYY-MM-DD HH:MM:SS" (used for the Continue row's "saved" column).
std::string nowStamp();

bool slotExists(const std::string& saveDir, int slot);
bool deleteSlot(const std::string& saveDir, int slot);

// The slot file's own format version (its header: savedAt, playTimeSec, meta, run). Raised only when that header
// changes; the meta / run parts carry their own schemaVersion.
constexpr int kSlotSaveVersion = 1;

// What the Continue page shows for one slot.
struct SlotSummary {
    int slot = 0;
    bool exists = false;       // loadable (from the file or, failing that, its backup)
    bool damaged = false;      // a file is there but neither it nor its backup can be read: never offered as empty
    bool fromBackup = false;   // the slot file was unreadable and the backup (the save before it) is shown instead
    bool newer = false;        // written by a newer version of the game: loads, but is never overwritten
    std::string stageId;
    std::string stageName;     // from data/stages/<id>.json's "name", when readable
    int lv = 1;
    int hp = 0, maxhp = 0, atk = 0, def = 0, gold = 0;
    std::string savedAt;
    int playTimeSec = 0;
};

bool writeSlotSave(const std::string& saveDir, int slot,
                   const MetaSaveData& meta, const RunSaveData& run,
                   int playTimeSec, const std::string& savedAt, const std::string& stageName);
// How a slot was read (readSlotSave's optional report).
struct SlotLoadInfo {
    bool fromBackup = false;   // the slot file was damaged; its backup was loaded
    bool newer = false;        // the file comes from a newer version of the game (writeSlotSave will not replace it)
};
bool readSlotSave(const std::string& saveDir, int slot,
                  MetaSaveData& metaOut, RunSaveData& runOut,
                  int* playTimeSec = nullptr, std::string* savedAt = nullptr, SlotLoadInfo* info = nullptr);
// True when the slot holds a save from a newer version of the game (its file or, if that is damaged, its backup).
bool slotIsNewer(const std::string& saveDir, int slot);
// Reads just the header/player fields the Continue list needs (no full parse of entityStatus).
SlotSummary summarizeSlot(const std::string& saveDir, int slot, int maxSlot = 0);
// Lowest unused slot in [1..slotCount], or -1 when every slot is taken.
int firstEmptySlot(const std::string& saveDir, int slotCount);
// Slot with the newest savedAt stamp, or -1 when there are none.
int newestSlot(const std::string& saveDir, int slotCount);

} // namespace toms
