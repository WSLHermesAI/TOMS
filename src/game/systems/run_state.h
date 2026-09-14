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
struct CounterDecl {
    int lo = -20;
    int hi = 20;
    int displayAt = 6;
};

class RunStoryState {
public:
    // ---- main-line choices (docs section 4.1) -------------------------------------------------
    // `reversible: false` (every main-line choice) means the FIRST answer sticks: a later dialogue
    // pass re-stating the choice cannot silently rewrite the run's history.
    void makeChoice(const std::string& choiceId, const std::string& optionId, bool reversible = false);
    bool choiceMade(const std::string& choiceId, const std::string& optionId) const;
    std::string choice(const std::string& choiceId) const;   // "" when never made

    // ---- accumulators (insight / resolve / humanity) ------------------------------------------
    int counter(const std::string& name) const;
    void setCounter(const std::string& name, int value);
    void addCounter(const std::string& name, int delta);
    void setCounterDecls(const std::map<std::string, CounterDecl>& decls) { decls_ = decls; }
    // True when the counter has reached its declared display threshold -- the HUD then shows the
    // state word from counters.json, not the value (section 2.3).
    bool counterVisible(const std::string& name) const;

    // ---- side stories (available / accepted / declined / completed / failed) -------------------
    void setSideStoryState(const std::string& id, const std::string& state);
    std::string sideStoryState(const std::string& id) const { return lookup(sideStories_, id); }

    // ---- run flags (declared in data/story/flags.json; `setFlags` from choice options) ---------
    void setFlag(const std::string& flag, bool on = true);
    bool flag(const std::string& flag) const;

    // ---- memory shards: the one part of the run that REBIRTH KEEPS (section 8) ----------------
    void addShard(const std::string& shardId);
    bool hasShard(const std::string& shardId) const;
    int  shardCount() const { return (int)shards_.size(); }

    // ---- skills (S4: the 功法三系 tree, data/skills.json) --------------------------------------
    // The "is this actually purchasable" rule (cost/prerequisites) lives in skill_system.h's
    // canUnlockSkill, not here -- this class only owns the two numbers that rule reads (points,
    // owned) and the mutation once a caller has already decided a purchase (or a chapter's free
    // grant, cost 0) is valid. Kept as a flat vector, not a set, to match shards_'s existing shape
    // -- one fewer container convention in this file, and the size stays tiny (single digits).
    void addSkillPoints(int delta) { skillPoints_ = std::max(0, skillPoints_ + delta); }
    int skillPoints() const { return skillPoints_; }
    // Unconditional: the caller (Game::applyChapterGrants / the skill-tree UI) is expected to have
    // already checked canUnlockSkill -- this just applies it (spend the points, own the skill) and
    // is itself idempotent (a second call for an already-owned id is a safe no-op).
    void unlockSkill(const std::string& skillId, int cost);
    bool hasSkill(const std::string& skillId) const;
    const std::vector<std::string>& skillsOwned() const { return skillsOwned_; }

    // ---- floor progress (feeds the `stageCleared` condition) ----------------------------------
    void setFloor(const std::string& floorId) { floor_ = floorId; }
    const std::string& floor() const { return floor_; }
    void markFloorCleared(const std::string& floorId);
    bool floorCleared(const std::string& floorId) const;
    const std::vector<std::string>& clearedFloors() const { return clearedFloors_; }

    // ---- deaths (ending e_13) ------------------------------------------------------------------
    void noteDeath(bool boss);
    int deathsTotal() const { return deathsTotal_; }
    int deathsNonBoss() const { return deathsNonBoss_; }

    // ---- lifecycle -----------------------------------------------------------------------------
    // A brand-new run (New Game). `keepShards` is the rebirth case: section 8 carries the shards and
    // the meta save, and resets every other item/flag/choice/counter.
    void reset(bool keepShards = false);

    // ---- persistence: RunSaveData is this data (section 9) -------------------------------------
    void writeInto(RunSaveData& r) const;
    void readFrom(const RunSaveData& r);

    // Convenience for tests/debug output.
    std::string debugSummary() const;

private:
    static std::string lookup(const std::map<std::string, std::string>& m, const std::string& k) {
        auto it = m.find(k);
        return (it == m.end()) ? std::string() : it->second;
    }

    std::map<std::string, std::string> choices_;
    std::map<std::string, int>         counters_;
    std::map<std::string, std::string> sideStories_;
    std::map<std::string, bool>        flags_;
    std::vector<std::string>           shards_;
    int                                 skillPoints_ = 0;
    std::vector<std::string>           skillsOwned_;
    std::string                        floor_ = "F01";
    std::vector<std::string>           clearedFloors_;
    int deathsTotal_ = 0, deathsNonBoss_ = 0;
    std::map<std::string, CounterDecl> decls_;
};

} // namespace toms
