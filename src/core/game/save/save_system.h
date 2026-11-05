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
constexpr int g_iMetaSaveSchemaVersion = 3;
constexpr int g_iRunSaveSchemaVersion  = 3;

// Permanent, cross-run progress. Survives even a full Run save wipe.
struct MetaSaveData {
    int m_iSchemaVersion = g_iMetaSaveSchemaVersion;
    std::set<std::string> m_setStoryFlags;
    int m_iCurrentBeat = 0;   // highest main-story beat reached; see src/game/story_controller.h
    std::vector<std::string> m_vUnlockedStages;
    std::vector<std::string> m_vMissionsClaimed;   // "once" missions — never re-arm once here
    int m_iPermanentAtkBonus = 0, m_iPermanentDefBonus = 0, m_iPermanentHpBonus = 0;
    // ---- S3, meta scope: these survive rebirth (section 8/9) ----
    int m_iCycleIndex = 1;                          // 1 = first life; rebirth increments it
    std::vector<std::string> m_vEndingsSeen;        // ending ids already reached (rebirth record UI)
    std::vector<std::string> m_vHintsUnlocked;      // meta hints the player has been shown
    // ---- S5, meta scope (data/forge.json): STORY_BIBLE.md §8's rebirth table keeps blueprints
    // known while resetting the crafted gear itself, so this lives on the META save, not the run
    // save's skillsOwned-style run-scoped list.
    std::vector<std::string> m_vForgeRecipesKnown;
    // Monster cards (card_collection.h; docs/gameplay/07_GAMEPLAY_PLAN.md §3): per monster, wins, losses and copies held.
    // Meta scope: survives rebirth. Optional in the file (an older save has none).
    CardBook m_mapCards;
    // The training ground (docs/gameplay/16 §5): its runs on the day it was last used ("2026-10-31"), for the daily limit.
    std::string m_strTrainDay;
    int m_iTrainRuns = 0;
};

// Mirrors the fields of Player (src/game/game.h) that need to persist. Kept as its own struct
// (not a reuse of Player) so save_system has zero dependency on game.h.
struct PlayerSaveData {
    int m_iHp = 120, m_iMaxhp = 120, m_iAtk = 12, m_iDef = 4, m_iGold = 0, m_iExp = 0, m_iLv = 1;
    int m_iKeyYellow = 0, m_iKeyBlue = 0, m_iKeyRed = 0;
    std::vector<std::string> m_vInv;
    // 2026-10 (the player menu, docs/gameplay/03_PLAYER_MENU.md): optional, so an older save still loads --
    // missing attributes are rebuilt from data/stats.json, missing gear is none.
    std::map<std::string, int> m_mapAttrs;            // data/stats.json's attributes by id
    std::vector<std::string> m_vGear;               // owned weapons / armor / talents
    std::string m_strWeapon, m_strArmor, m_strTalent;           // what is worn ("" = nothing)
    int m_iAttrPoints = 0;                          // attribute points not placed yet
    // 2026-10-22 (Battle v3, docs/gameplay/07 phase 1): wins with each weapon ("unarmed" for none) -- its level comes from them.
    std::map<std::string, int> m_mapWeaponExp;
    int m_iCrystals = 0;                            // 2026-10-22 (docs/gameplay/07 phase 6): the second currency; optional
};

// The current session/run in progress.
struct RunSaveData {
    int m_iSchemaVersion = g_iRunSaveSchemaVersion;
    std::string m_strCurrentStageId;
    PlayerSaveData m_oPlayer;
    // key = "<stageId>|<x>,<y>" -> status string ("Defeated"/"Collected"/"Opened"/...), per
    // architecture-doc §5.2's StageEntityStatus.
    std::map<std::string, std::string> m_mapEntityStatus;
    // ---- S3, run scope (section 9's table; reset every run, see RunStoryState) ----
    std::map<std::string, std::string> m_mapChoices;      // choiceId -> optionId
    std::map<std::string, int>         m_mapCounters;     // insight / resolve / humanity (and any future one)
    std::map<std::string, std::string> m_mapSideStories;  // sideStoryId -> available/accepted/declined/completed/failed
    std::map<std::string, bool>        m_mapFlags;        // run-scoped flags declared in data/story/flags.json
    std::string                        m_strFloor = "F01";// the 70-floor tower's current floor id
    std::vector<std::string>           m_vClearedFloors;// feeds the `stageCleared` condition
    std::vector<std::string>           m_vEventsStarted, m_vEventsFinished;   // the event log (2026-10; optional)
    // NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 8; optional): the event variables set this
    // run (story/vars.json; the rest are at their default), and per NPC [talk index, talk count, last random line].
    std::map<std::string, int>              m_mapVars;
    std::map<std::string, std::vector<int>> m_mapNpcTalk;
    std::map<std::string, int>              m_mapEventCounts;   // how many times each repeatable event ran
    std::vector<std::string>           m_vShards;       // memory shards -- SURVIVE rebirth (section 8)
    int m_iDeathsTotal = 0, m_iDeathsNonBoss = 0;          // feeds ending e_13
    // ---- S4, run scope (the 功法三系 skill tree, data/skills.json) ----
    int m_iSkillPoints = 0;
    std::vector<std::string> m_vSkillsOwned;
    // ---- M8, run scope: the Super-Attack Gauge's per-run cap (docs/story/STORY_DATA_SCHEMA.md §8's
    // "必殺充能上限｜floor(舊值÷2)（最少1）") -- see RunStoryState::kDefaultSuperMax for the baseline.
    int m_iSuperMax = 5;
};

// Migrations (docs/gameplay/05_SAVE_SYSTEM.md): before a file is parsed, its json is brought up to the current schema one version
// at a time (v1 -> v2 -> v3 ...). Adding a field needs no step -- a missing field takes its default. A step is needed
// when a field is renamed, removed, split or changes meaning: add it to kMetaMigrations / kRunMigrations in
// save_system.cpp and raise the schema version. A file NEWER than this build is left as it is (fields this build does
// not know are kept in the json, never dropped by a migration) -- see SlotLoad::newer.
void f_MigrateMetaJson(nlohmann::json& l_jJ);
void f_MigrateRunJson(nlohmann::json& l_jJ);

nlohmann::json f_ToJson(const MetaSaveData& l_oM);
// Parses j into a MetaSaveData. If versionMismatch is non-null, it is set to true when the file's
// schemaVersion differs from kMetaSaveSchemaVersion -- DETECTION ONLY: an older file is still loaded,
// with the fields it predates left at their defaults (section 9's compatibility rule).
MetaSaveData f_MetaFromJson(const nlohmann::json& l_jJ, bool* l_pVersionMismatch = nullptr);

nlohmann::json f_ToJson(const RunSaveData& l_oR);
RunSaveData f_RunFromJson(const nlohmann::json& l_jJ, bool* l_pVersionMismatch = nullptr);

// Reads a UTF-8 JSON file. Returns a null json (and logs to stderr) on any failure — mirrors the
// safe-read convention already used by src/game/stage.h's parseStage.
nlohmann::json f_ReadJsonFileSafe(const std::string& l_strPath);

// Writes `j` to `path` atomically: serializes to `path + ".tmp"`, flushes it to the disk (so a power cut after the
// rename cannot leave an empty file), then renames it over `path`. With keepBackup, the file being replaced is first
// moved to `path + ".bak"` -- but only when it still parses, so a damaged file never replaces a good backup. Never
// throws; returns false if a step fails (e.g. the containing directory doesn't exist).
bool f_WriteJsonAtomic(const std::string& l_strPath, const nlohmann::json& l_jJ, bool l_bKeepBackup = false);
std::string f_BackupPath(const std::string& l_strPath);   // path + ".bak"

} // namespace toms
