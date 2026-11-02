// run_state.h — the per-run story state introduced by S3 (docs/story/STORY_DATA_SCHEMA.md sections 2.3/9).
//
// One object holds everything the 70-floor story accumulates *within a run*: which option the player
// took at each main-line choice, the three accumulators (insight / resolve / humanity), how far each
// of the ten flagship side stories got, run-scoped flags, memory shards, the current floor id, the
// floors cleared (which is what the `stageCleared` condition reads) and the death counters.
//
// It exists as its own class for three reasons:
//   * the condition evaluator needs ONE place to ask "did the player choose X / is the counter high
//     enough / is this the second cycle" -- see the four S3 leaves in condition.h;
//   * the save file (RunSaveData, schemaVersion 3) is exactly this data, so persistence is one
//     writeInto()/readFrom() pair instead of field-by-field copying in the game code;
//   * rebirth (docs/story/STORY_DATA_SCHEMA.md section 8) is defined as "reset this, keep shards + meta",
//     so the rules live next to the data they describe.
//
// Pure logic: no Game/Player/Renderer dependency, so it is unit-testable (run_state_test.cpp).
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "save_system.h"

namespace toms {

// Declaration of one accumulator, from data/story/counters.json: `displayAt` is the threshold at
// which the HUD shows the state *word* (never the number), and `clamp` bounds it so the rebirth
// maths in section 8 can never produce a negative or absurd value.
// An event variable from story/vars.json (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 8): its value before anything
// sets it, and its range ("bool" variables are 0..1).
struct VarDecl {
    int m_iDef = 0;
    int m_iLo = -1000000;
    int m_iHi = 1000000;
};

// One NPC's talk state this run (section 4): which chat line plays next, how many times the player has talked to
// it, and the last line a "random" rotation played (-1 = none).
struct NpcTalkState {
    int m_iIndex = 0;
    int m_iCount = 0;
    int m_iLastRandom = -1;
};

struct CounterDecl {
    int m_iLo = -20;
    int m_iHi = 20;
    int m_iDisplayAt = 6;
};

class RunStoryState {
public:
    // ---- main-line choices (docs section 4.1) -------------------------------------------------
    // `reversible: false` (every main-line choice) means the FIRST answer sticks: a later dialogue
    // pass re-stating the choice cannot silently rewrite the run's history.
    void f_MakeChoice(const std::string& l_strChoiceId, const std::string& l_strOptionId, bool l_bReversible = false);
    bool f_ChoiceMade(const std::string& l_strChoiceId, const std::string& l_strOptionId) const;
    std::string f_Choice(const std::string& l_strChoiceId) const;   // "" when never made

    // ---- accumulators (insight / resolve / humanity) ------------------------------------------
    int f_Counter(const std::string& l_strName) const;
    void f_SetCounter(const std::string& l_strName, int l_iValue);
    void f_AddCounter(const std::string& l_strName, int l_iDelta);
    void f_SetCounterDecls(const std::map<std::string, CounterDecl>& l_mapDecls) { m_mapDecls = l_mapDecls; }
    // True when the counter has reached its declared display threshold -- the HUD then shows the
    // state word from counters.json, not the value (section 2.3).
    bool f_CounterVisible(const std::string& l_strName) const;

    // ---- side stories (available / accepted / declined / completed / failed) -------------------
    void f_SetSideStoryState(const std::string& l_strId, const std::string& l_strState);
    std::string f_SideStoryState(const std::string& l_strId) const { return f_Lookup(m_mapSideStories, l_strId); }

    // ---- run flags (declared in data/story/flags.json; `setFlags` from choice options) ---------
    void f_SetFlag(const std::string& l_strFlag, bool l_bOn = true);
    bool f_Flag(const std::string& l_strFlag) const;

    // ---- memory shards: the one part of the run that REBIRTH KEEPS (section 8) ----------------
    void f_AddShard(const std::string& l_strShardId);
    bool f_HasShard(const std::string& l_strShardId) const;
    int  f_ShardCount() const { return (int)m_vShards.size(); }

    // ---- skills (S4: the 功法三系 tree, data/skills.json) --------------------------------------
    // The "is this actually purchasable" rule (cost/prerequisites) lives in skill_system.h's
    // canUnlockSkill, not here -- this class only owns the two numbers that rule reads (points,
    // owned) and the mutation once a caller has already decided a purchase (or a chapter's free
    // grant, cost 0) is valid. Kept as a flat vector, not a set, to match shards_'s existing shape
    // -- one fewer container convention in this file, and the size stays tiny (single digits).
    void f_AddSkillPoints(int l_iDelta) { m_iSkillPoints = std::max(0, m_iSkillPoints + l_iDelta); }
    int f_SkillPoints() const { return m_iSkillPoints; }
    // M8 (docs/story/STORY_DATA_SCHEMA.md §8): "必殺充能上限｜floor(舊值÷2)（最少1）" -- a per-run
    // value (not the old compile-time CombatState::kSuperThreshold) so Game::rebirth() can carry it
    // forward at half power. A brand-new run gets kDefaultSuperMax via reset(); Game::rebirth()
    // overwrites it with the halved value right after.
    static constexpr int m_iDefaultSuperMax = 5;
    int f_SuperMax() const { return m_iSuperMax; }
    void f_SetSuperMax(int l_iV) { m_iSuperMax = std::max(1, l_iV); }
    // Unconditional: the caller (Game::applyChapterGrants / the skill-tree UI) is expected to have
    // already checked canUnlockSkill -- this just applies it (spend the points, own the skill) and
    // is itself idempotent (a second call for an already-owned id is a safe no-op).
    void f_UnlockSkill(const std::string& l_strSkillId, int l_iCost);
    bool f_HasSkill(const std::string& l_strSkillId) const;
    const std::vector<std::string>& f_SkillsOwned() const { return m_vSkillsOwned; }

    // ---- floor progress (feeds the `stageCleared` condition) ----------------------------------
    void f_SetFloor(const std::string& l_strFloorId) { m_strFloor = l_strFloorId; }
    const std::string& f_Floor() const { return m_strFloor; }
    void f_MarkFloorCleared(const std::string& l_strFloorId);
    bool f_FloorCleared(const std::string& l_strFloorId) const;
    const std::vector<std::string>& f_ClearedFloors() const { return m_vClearedFloors; }

    // ---- events: the player's event log (docs/gameplay/event_editor/06_EVENT_LOGIC.md section 7) --------
    // Started = the player has met the event (reached its tile, or a finished event's `next` points at
    // it); finished = it fired. Both keep the order they happened in (the log lists the newest first).
    // Finishing also starts it, so "finished" is always a subset of "started".
    void f_MarkEventStarted(const std::string& l_strEventId);
    void f_MarkEventFinished(const std::string& l_strEventId);
    bool f_EventStarted(const std::string& l_strEventId) const;
    bool f_EventFinished(const std::string& l_strEventId) const;
    const std::vector<std::string>& f_EventsStarted() const { return m_vEventsStarted; }
    const std::vector<std::string>& f_EventsFinished() const { return m_vEventsFinished; }

    // ---- event variables and NPC talk state (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 8) ---
    // A variable never set reads its declared default (0 when undeclared); setting clamps to the declared range.
    int f_Var(const std::string& l_strName) const;
    void f_SetVar(const std::string& l_strName, int l_iValue);
    void f_AddVar(const std::string& l_strName, int l_iDelta) { f_SetVar(l_strName, f_Var(l_strName) + l_iDelta); }
    void f_SetVarDecls(const std::map<std::string, VarDecl>& l_mapDecls) { m_mapVarDecls = l_mapDecls; }
    const std::map<std::string, VarDecl>& f_VarDecls() const { return m_mapVarDecls; }
    const std::map<std::string, int>& f_Vars() const { return m_mapVars; }
    NpcTalkState& f_NpcTalk(const std::string& l_strNpc) { return m_mapNpcTalk[l_strNpc]; }
    // How many times each event ran (repeatable events, "maxTimes"). A finished event counts at least once.
    int f_EventCount(const std::string& l_strEventId) const;
    void f_CountEvent(const std::string& l_strEventId) { ++m_mapEventCounts[l_strEventId]; }
    NpcTalkState f_NpcTalk(const std::string& l_strNpc) const;

    // ---- deaths (ending e_13) ------------------------------------------------------------------
    void f_NoteDeath(bool l_bBoss);
    int f_DeathsTotal() const { return m_iDeathsTotal; }
    int f_DeathsNonBoss() const { return m_iDeathsNonBoss; }

    // ---- lifecycle -----------------------------------------------------------------------------
    // A brand-new run (New Game). `keepShards` is the rebirth case: section 8 carries the shards and
    // the meta save, and resets every other item/flag/choice/counter.
    void f_Reset(bool l_bKeepShards = false);

    // ---- persistence: RunSaveData is this data (section 9) -------------------------------------
    void f_WriteInto(RunSaveData& l_oR) const;
    void f_ReadFrom(const RunSaveData& l_oR);

    // Convenience for tests/debug output.
    std::string f_DebugSummary() const;

private:
    static std::string f_Lookup(const std::map<std::string, std::string>& l_mapM, const std::string& l_strK) {
        auto l_itIt = l_mapM.find(l_strK);
        return (l_itIt == l_mapM.end()) ? std::string() : l_itIt->second;
    }

    std::map<std::string, std::string> m_mapChoices;
    std::map<std::string, int>         m_mapCounters;
    std::map<std::string, std::string> m_mapSideStories;
    std::map<std::string, bool>        m_mapFlags;
    std::vector<std::string>           m_vShards;
    int                                 m_iSkillPoints = 0;
    std::vector<std::string>           m_vSkillsOwned;
    int                                 m_iSuperMax = m_iDefaultSuperMax;
    std::string                        m_strFloor = "F01";
    std::vector<std::string>           m_vClearedFloors;
    std::vector<std::string>           m_vEventsStarted, m_vEventsFinished;
    std::map<std::string, int>         m_mapVars;
    std::map<std::string, VarDecl>     m_mapVarDecls;
    std::map<std::string, NpcTalkState> m_mapNpcTalk;
    std::map<std::string, int>         m_mapEventCounts;
    int m_iDeathsTotal = 0, m_iDeathsNonBoss = 0;
    std::map<std::string, CounterDecl> m_mapDecls;
};

} // namespace toms
