// game_condition.h — GameConditionContext: adapts live Player + MetaSaveData state to the
// engine-level toms::ConditionContext interface (condition.h), so door/key gating and dialogue
// `requires` gating both go through the one shared evaluator instead of bespoke per-call-site
// checks (Milestone 3 — see docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md section 9).
//
// Lived in game.cpp's anonymous namespace until the 2026-09-13 split, which moved the code using it
// (game_input.cpp, game_story.cpp) into their own translation units.
#pragma once

#include "game.h"
#include "condition.h"         // toms::ConditionContext (the interface implemented here)
#include "story_controller.h"  // toms::hasStoryFlag / storyBeat
#include "mission_system.h"    // toms::MissionTracker
#include "save_system.h"       // toms::MetaSaveData (story flags / cleared stages)
#include "run_state.h"         // toms::RunStoryState (S3: choices/counters/side stories/floors)
#include <map>
#include <string>

namespace toms {
namespace game_detail {

// GameConditionContext adapts live Player + MetaSaveData state to the engine-level
// toms::ConditionContext interface (condition.h), so door/key gating and dialogue `requires`
// gating both go through the one shared evaluator instead of bespoke checks (Milestone 3 —
// see docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §9). Built from already-public Player
// fields + a MetaSaveData reference passed in by the Game methods that use it (movePlayer,
// enterNode), so it needs no friendship/new public API on Game.
class GameConditionContext : public toms::ConditionContext {
public:
    // `run` is REQUIRED, not defaulted: a default empty run state would silently answer "no choice
    // made yet" at any call site that forgot to pass it, which is exactly the kind of bug S3 exists
    // to prevent (the compiler now finds those sites instead).
    GameConditionContext(const Player& p, const toms::MetaSaveData& meta,
                          const std::map<std::string, toms::MissionTracker>& missions,
                          const toms::RunStoryState& run)
        : p_(p), meta_(meta), missions_(missions), run_(run) {}
    bool storyFlagSet(const std::string& flag) const override { return toms::hasStoryFlag(meta_, flag); }
    int  storyBeat() const override { return meta_.currentBeat; }
    bool itemHeld(const std::string& itemId, int count) const override {
        if (itemId == "key_yellow") return p_.key_yellow >= count;
        if (itemId == "key_blue")   return p_.key_blue   >= count;
        if (itemId == "key_red")    return p_.key_red    >= count;
        int c = 0; for (auto& s : p_.inv) if (s == itemId) c++;
        return c >= count;
    }
    int statValue(const std::string& stat) const override {
        if (stat == "atk")  return p_.atk;
        if (stat == "def")  return p_.def;
        if (stat == "hp")   return p_.hp;
        if (stat == "lv")   return p_.lv;
        if (stat == "gold") return p_.gold;
        if (stat == "exp")  return p_.exp;
        return 0;
    }
    // Milestone 4: real lookups against Game's live mission trackers (closes the stub these two
    // methods were in Milestone 3 — missionDefs_ may still be empty until Milestone 8 loads
    // content, but tracker *state* is real the moment a mission is started).
    bool missionComplete(const std::string& missionId) const override {
        auto it = missions_.find(missionId);
        return it != missions_.end() &&
               (it->second.state == toms::MissionState::Completed || it->second.state == toms::MissionState::Claimed);
    }
    bool missionActive(const std::string& missionId) const override {
        auto it = missions_.find(missionId);
        return it != missions_.end() && it->second.state == toms::MissionState::Active;
    }
    // Milestone 5: a stage counts as "cleared" once the player has reached it at least once
    // (see Game::loadStage's meta_.unlockedStages tracking) -- the simplest sensible definition
    // for this linear-climb game; whether a *replayed* floor should repopulate enemies is a
    // separate, still-open design question (architecture-doc §5.2, tracked in the roadmap's
    // Milestone 7 balance checklist), not decided here.
    bool stageCleared(const std::string& stageId) const override {
        // S3: a floor counts as cleared when the RUN says so (docs/STORY_DATA_SCHEMA.md section 9's
        // clearedFloors[]), falling back to the pre-S3 "reached at least once" rule for the eleven
        // hand-authored stages so existing content keeps behaving.
        if (run_.floorCleared(stageId)) return true;
        return std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), stageId) != meta_.unlockedStages.end();
    }
    // ---- S3 leaves (docs/STORY_DATA_SCHEMA.md section 2.2) ----
    bool choiceMade(const std::string& choiceId, const std::string& optionId) const override {
        return run_.choiceMade(choiceId, optionId);
    }
    std::string sideStoryState(const std::string& sideStoryId) const override {
        return run_.sideStoryState(sideStoryId);
    }
    int counter(const std::string& name) const override { return run_.counter(name); }
    int cycleIndex() const override { return meta_.cycleIndex; }
private:
    const Player& p_;
    const toms::MetaSaveData& meta_;
    const std::map<std::string, toms::MissionTracker>& missions_;
    const toms::RunStoryState& run_;
};

} // namespace game_detail
} // namespace toms
