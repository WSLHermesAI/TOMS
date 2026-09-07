// save_system.h — Meta save (permanent, cross-run) + Run save (current session) persistence.
// See docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §13 #26 and
// docs/IMPLEMENTATION_ROADMAP.md Milestone 1.
//
// Deliberately standalone: no dependency on Game/Player/Stage, so this stays headless-testable
// (save_test) without pulling in the renderer/Vulkan/GLFW. Game-side conversion helpers get added
// once something real reads/writes these files (Milestone 5's Stage Select "Continue") — until
// then this is schema + (de)serialization + atomic file I/O only.
#pragma once
#include <json.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace toms {

constexpr int kMetaSaveSchemaVersion = 1;
constexpr int kRunSaveSchemaVersion  = 1;

// Permanent, cross-run progress. Survives even a full Run save wipe.
struct MetaSaveData {
    int schemaVersion = kMetaSaveSchemaVersion;
    std::set<std::string> storyFlags;
    int currentBeat = 0;   // highest main-story beat reached; see src/game/story_controller.h
    std::vector<std::string> unlockedStages;
    std::vector<std::string> missionsClaimed;   // "once" missions — never re-arm once here
    int permanentAtkBonus = 0, permanentDefBonus = 0, permanentHpBonus = 0;
};

// Mirrors the fields of Player (src/game/game.h) that need to persist. Kept as its own struct
// (not a reuse of Player) so save_system has zero dependency on game.h.
struct PlayerSaveData {
    int hp = 120, maxhp = 120, atk = 12, def = 4, gold = 0, exp = 0, lv = 1;
    int key_yellow = 0, key_blue = 0, key_red = 0;
    std::vector<std::string> inv;
};

// The current session/run in progress.
struct RunSaveData {
    int schemaVersion = kRunSaveSchemaVersion;
    std::string currentStageId;
    PlayerSaveData player;
    // key = "<stageId>|<x>,<y>" -> status string ("Defeated"/"Collected"/"Opened"/...), per
    // architecture-doc §5.2's StageEntityStatus.
    std::map<std::string, std::string> entityStatus;
};

nlohmann::json toJson(const MetaSaveData& m);
// Parses j into a MetaSaveData. If versionMismatch is non-null, it is set to true when the
// file's schemaVersion doesn't match kMetaSaveSchemaVersion — detection only, no migration logic
// exists yet (that's future scope, not Milestone 1's).
MetaSaveData metaFromJson(const nlohmann::json& j, bool* versionMismatch = nullptr);

nlohmann::json toJson(const RunSaveData& r);
RunSaveData runFromJson(const nlohmann::json& j, bool* versionMismatch = nullptr);

// Reads a UTF-8 JSON file. Returns a null json (and logs to stderr) on any failure — mirrors the
// safe-read convention already used by src/game/stage.h's parseStage.
nlohmann::json readJsonFileSafe(const std::string& path);

// Writes `j` to `path` atomically: serializes to `path + ".tmp"`, then renames over `path`. Never
// throws; returns false if either step fails (e.g. the containing directory doesn't exist).
bool writeJsonAtomic(const std::string& path, const nlohmann::json& j);

} // namespace toms
