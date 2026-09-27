// condition.h — the shared Condition/Flag Evaluator: one small, data-driven "can the player
// see/do X right now" engine, reused by dialogue choice gating, door/key checks, and (from
// Milestone 4/5 onward) mission prerequisites and Stage Select lock reasons.
// See docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §9 and docs/architecture/IMPLEMENTATION_ROADMAP.md
// Milestone 3.
//
// A condition is plain JSON — no custom AST/object model, matching this project's existing
// preference for simple, explicit JSON over embedded logic (e.g. equipment.json's `talent`
// enum, or dialogue's `action.give` verb). Two shapes:
//   leaf:        { "type": "<leafType>", ...fields... }
//   combinator:  { "all": [ <condition>, ... ] } | { "any": [...] } | { "not": <condition> }
// A null/absent condition evaluates to true — a choice/door/stage with no requirement is always
// available — so callers can pass whatever `requires` field they have, present or not.
#pragma once
#include <json.hpp>
#include <string>

namespace toms {

// The data a Condition can query. Implementations plug in real game state (see
// src/game/game.cpp's GameConditionContext) or a mock (condition_eval_test.cpp). Pure
// interface: zero dependency on Game/Player/Stage keeps this header engine-level and
// headlessly testable.
class ConditionContext {
public:
    virtual ~ConditionContext() = default;
    virtual bool storyFlagSet(const std::string& flag) const = 0;
    virtual int  storyBeat() const = 0;
    virtual bool itemHeld(const std::string& itemId, int count) const = 0;
    virtual int  statValue(const std::string& stat) const = 0;   // "atk"/"def"/"hp"/"lv"/"gold"/"exp"
    virtual bool missionComplete(const std::string& missionId) const = 0;
    virtual bool missionActive(const std::string& missionId) const = 0;
    virtual bool stageCleared(const std::string& stageId) const = 0;

    // ---- S3 (docs/story/STORY_DATA_SCHEMA.md section 2.2) --------------------------------------------
    // The four leaves the 70-floor story needs. They are DEFAULTED here rather than pure virtual so
    // every existing context (and every test mock) keeps compiling unchanged and simply answers
    // "no choice made / no counter / cycle 1" -- a mock that does not care cannot accidentally make
    // a gated screen appear.
    virtual bool choiceMade(const std::string& /*choiceId*/, const std::string& /*optionId*/) const {
        return false;
    }
    virtual std::string sideStoryState(const std::string& /*sideStoryId*/) const { return ""; }
    virtual int counter(const std::string& /*counter*/) const { return 0; }
    virtual int cycleIndex() const { return 1; }
    // M7 (docs/story/STORY_DATA_SCHEMA.md §2.2/§7): e_07's own condition ("記憶碎片不足"). Defaulted
    // to 0 for the same "an uninterested mock can't accidentally grant something" reason as the S3
    // leaves above.
    virtual int memoryShardCount() const { return 0; }
    // M7: e_13's own condition ("非首領戰死亡累積 ≥12"). RunStoryState::deathsNonBoss() is a
    // dedicated field, not one of the generic named counters (insight/resolve/humanity), so it
    // gets its own leaf rather than being smuggled through "counterAtLeast" under a fake name.
    virtual int deathsNonBoss() const { return 0; }
    // M9: storyFlagSet() above only ever reads the META save's flags (toms::hasStoryFlag) -- the
    // RUN-scoped flags a floor event tile sets on itself ("event_<id>", game_input.cpp) and the
    // ones a choice's own setFlags list writes (e.g. flag_page_returned) live in RunStoryState
    // instead and had no leaf able to read them back at all until a dialogue choice (ss_04) needed
    // to gate on one. Defaulted like the S3/M7 leaves above.
    virtual bool runFlagSet(const std::string& /*flag*/) const { return false; }
};

// Recursively evaluates `expr` against `ctx`. A null/non-object `expr` is vacuously true (no
// requirement). An unrecognized leaf `type`, or a malformed combinator (a missing or
// wrong-typed array), fails closed (returns false) rather than silently granting access.
bool evaluate(const nlohmann::json& expr, const ConditionContext& ctx);

} // namespace toms
