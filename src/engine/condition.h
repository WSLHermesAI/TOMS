// condition.h — the shared Condition/Flag Evaluator: one small, data-driven "can the player
// see/do X right now" engine, reused by dialogue choice gating, door/key checks, and (from
// Milestone 4/5 onward) mission prerequisites and Stage Select lock reasons.
// See docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §9 and docs/IMPLEMENTATION_ROADMAP.md
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
};

// Recursively evaluates `expr` against `ctx`. A null/non-object `expr` is vacuously true (no
// requirement). An unrecognized leaf `type`, or a malformed combinator (a missing or
// wrong-typed array), fails closed (returns false) rather than silently granting access.
bool evaluate(const nlohmann::json& expr, const ConditionContext& ctx);

} // namespace toms
