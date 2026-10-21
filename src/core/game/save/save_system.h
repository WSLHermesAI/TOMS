// save_system.h — Meta save (permanent, cross-run) + Run save (current session) persistence.
// See docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §13 #26 and
// docs/architecture/IMPLEMENTATION_ROADMAP.md Milestone 1.
//
// Deliberately standalone: no dependency on Game/Player/Stage, so this stays headless-testable
// (save_test) without pulling in the renderer/Vulkan/GLFW. Game-side conversion helpers get added
// once something real reads/writes these files (Milestone 5's Stage Select "Continue") — until
// then this is schema + (de)serialization + atomic file I/O only.
#pragma once
#include "card_collection.h"   // toms::CardBook -- MetaSaveData::cards
#include <json.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace toms {

// S3 (docs/story/STORY_DATA_SCHEMA.md section 9): version 3 adds the story state -- flags, choices,
// counters, side-story states, floor progress, memory shards and death counts on the run, and
// cycleIndex / endingsSeen / hintsUnlocked on the meta save (they survive a rebirth).
// COMPATIBILITY IS A HARD RULE: a file written by an older version must still load, with the new
// fields defaulted (counters 0, cycleIndex 1, endingsSeen empty) -- never refuse a save.
constexpr int kMetaSaveSchemaVersion = 3;
constexpr int kRunSaveSchemaVersion  = 3;

// Permanent, cross-run progress. Survives even a full Run save wipe.
struct MetaSaveData {
    int schemaVersion = kMetaSaveSchemaVersion;
    std::set<std::string> storyFlags;
    int currentBeat = 0;   // highest main-story beat reached; see src/game/story_controller.h
    std::vector<std::string> unlockedStages;
    std::vector<std::string> missionsClaimed;   // "once" missions — never re-arm once here
    int permanentAtkBonus = 0, permanentDefBonus = 0, permanentHpBonus = 0;
    // ---- S3, meta scope: these survive rebirth (section 8/9) ----
    int cycleIndex = 1;                          // 1 = first life; rebirth increments it
    std::vector<std::string> endingsSeen;        // ending ids already reached (rebirth record UI)
    std::vector<std::string> hintsUnlocked;      // meta hints the player has been shown
    // ---- S5, meta scope (data/forge.json): STORY_BIBLE.md §8's rebirth table keeps blueprints
    // known while resetting the crafted gear itself, so this lives on the META save, not the run
    // save's skillsOwned-style run-scoped list.
    std::vector<std::string> forgeRecipesKnown;
    // Monster cards (card_collection.h; docs/30_GAMEPLAY_PLAN.md §3): per monster, wins, losses and copies held.
    // Meta scope: survives rebirth. Optional in the file (an older save has none).
    CardBook cards;
};

// Mirrors the fields of Player (src/game/game.h) that need to persist. Kept as its own struct
// (not a reuse of Player) so save_system has zero dependency on game.h.
struct PlayerSaveData {
    int hp = 120, maxhp = 120, atk = 12, def = 4, gold = 0, exp = 0, lv = 1;
    int key_yellow = 0, key_blue = 0, key_red = 0;
    std::vector<std::string> inv;
    // 2026-10 (the player menu, docs/20_PLAYER_MENU.md): optional, so an older save still loads --
    // missing attributes are rebuilt from data/stats.json, missing gear is none.
    std::map<std::string, int> attrs;            // data/stats.json's attributes by id
    std::vector<std::string> gear;               // owned weapons / armor / talents
    std::string weapon, armor, talent;           // what is worn ("" = nothing)
    int attrPoints = 0;                          // attribute points not placed yet
};

// The current session/run in progress.
struct RunSaveData {
    int schemaVersion = kRunSaveSchemaVersion;
    std::string currentStageId;
    PlayerSaveData player;
    // key = "<stageId>|<x>,<y>" -> status string ("Defeated"/"Collected"/"Opened"/...), per
    // architecture-doc §5.2's StageEntityStatus.
    std::map<std::string, std::string> entityStatus;
    // ---- S3, run scope (section 9's table; reset every run, see RunStoryState) ----
    std::map<std::string, std::string> choices;      // choiceId -> optionId
    std::map<std::string, int>         counters;     // insight / resolve / humanity (and any future one)
    std::map<std::string, std::string> sideStories;  // sideStoryId -> available/accepted/declined/completed/failed
    std::map<std::string, bool>        flags;        // run-scoped flags declared in data/story/flags.json
    std::string                        floor = "F01";// the 70-floor tower's current floor id
    std::vector<std::string>           clearedFloors;// feeds the `stageCleared` condition
    std::vector<std::string>           eventsStarted, eventsFinished;   // the event log (2026-10; optional)
    // NPCs and their events (docs/event_editor/08_NPC_DIALOGUE.md section 8; optional): the event variables set this
    // run (story/vars.json; the rest are at their default), and per NPC [talk index, talk count, last random line].
    std::map<std::string, int>              vars;
    std::map<std::string, std::vector<int>> npcTalk;
    std::map<std::string, int>              eventCounts;   // how many times each repeatable event ran
    std::vector<std::string>           shards;       // memory shards -- SURVIVE rebirth (section 8)
    int deathsTotal = 0, deathsNonBoss = 0;          // feeds ending e_13
    // ---- S4, run scope (the 功法三系 skill tree, data/skills.json) ----
    int skillPoints = 0;
    std::vector<std::string> skillsOwned;
    // ---- M8, run scope: the Super-Attack Gauge's per-run cap (docs/story/STORY_DATA_SCHEMA.md §8's
    // "必殺充能上限｜floor(舊值÷2)（最少1）") -- see RunStoryState::kDefaultSuperMax for the baseline.
    int superMax = 5;
};

// Migrations (docs/28_SAVE_SYSTEM.md): before a file is parsed, its json is brought up to the current schema one version
// at a time (v1 -> v2 -> v3 ...). Adding a field needs no step -- a missing field takes its default. A step is needed
// when a field is renamed, removed, split or changes meaning: add it to kMetaMigrations / kRunMigrations in
// save_system.cpp and raise the schema version. A file NEWER than this build is left as it is (fields this build does
// not know are kept in the json, never dropped by a migration) -- see SlotLoad::newer.
void migrateMetaJson(nlohmann::json& j);
void migrateRunJson(nlohmann::json& j);

nlohmann::json toJson(const MetaSaveData& m);
// Parses j into a MetaSaveData. If versionMismatch is non-null, it is set to true when the file's
// schemaVersion differs from kMetaSaveSchemaVersion -- DETECTION ONLY: an older file is still loaded,
// with the fields it predates left at their defaults (section 9's compatibility rule).
MetaSaveData metaFromJson(const nlohmann::json& j, bool* versionMismatch = nullptr);

nlohmann::json toJson(const RunSaveData& r);
RunSaveData runFromJson(const nlohmann::json& j, bool* versionMismatch = nullptr);

// Reads a UTF-8 JSON file. Returns a null json (and logs to stderr) on any failure — mirrors the
// safe-read convention already used by src/game/stage.h's parseStage.
nlohmann::json readJsonFileSafe(const std::string& path);

// Writes `j` to `path` atomically: serializes to `path + ".tmp"`, flushes it to the disk (so a power cut after the
// rename cannot leave an empty file), then renames it over `path`. With keepBackup, the file being replaced is first
// moved to `path + ".bak"` -- but only when it still parses, so a damaged file never replaces a good backup. Never
// throws; returns false if a step fails (e.g. the containing directory doesn't exist).
bool writeJsonAtomic(const std::string& path, const nlohmann::json& j, bool keepBackup = false);
std::string backupPath(const std::string& path);   // path + ".bak"

} // namespace toms
