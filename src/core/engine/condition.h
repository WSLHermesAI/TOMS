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
    virtual bool f_StoryFlagSet(const std::string& l_strFlag) const = 0;
    virtual int  f_StoryBeat() const = 0;
    virtual bool f_ItemHeld(const std::string& l_strItemId, int l_iCount) const = 0;
    virtual int  f_StatValue(const std::string& l_strStat) const = 0;   // "atk"/"def"/"hp"/"lv"/"gold"/"exp"
    virtual bool f_MissionComplete(const std::string& l_strMissionId) const = 0;
    virtual bool f_MissionActive(const std::string& l_strMissionId) const = 0;
    virtual bool f_StageCleared(const std::string& l_strStageId) const = 0;

    // ---- S3 (docs/story/STORY_DATA_SCHEMA.md section 2.2) --------------------------------------------
    // The four leaves the 70-floor story needs. They are DEFAULTED here rather than pure virtual so
    // every existing context (and every test mock) keeps compiling unchanged and simply answers
    // "no choice made / no counter / cycle 1" -- a mock that does not care cannot accidentally make
    // a gated screen appear.
    virtual bool f_ChoiceMade(const std::string& /*choiceId*/, const std::string& /*optionId*/) const {
        return false;
    }
    virtual std::string f_SideStoryState(const std::string& /*sideStoryId*/) const { return ""; }
    virtual int f_Counter(const std::string& /*counter*/) const { return 0; }
    virtual int f_CycleIndex() const { return 1; }
    // M7 (docs/story/STORY_DATA_SCHEMA.md §2.2/§7): e_07's own condition ("記憶碎片不足"). Defaulted
    // to 0 for the same "an uninterested mock can't accidentally grant something" reason as the S3
    // leaves above.
    virtual int f_MemoryShardCount() const { return 0; }
    // M7: e_13's own condition ("非首領戰死亡累積 ≥12"). RunStoryState::deathsNonBoss() is a
    // dedicated field, not one of the generic named counters (insight/resolve/humanity), so it
    // gets its own leaf rather than being smuggled through "counterAtLeast" under a fake name.
    virtual int f_DeathsNonBoss() const { return 0; }
    // M9: storyFlagSet() above only ever reads the META save's flags (toms::hasStoryFlag) -- the
    // RUN-scoped flags a floor event tile sets on itself ("event_<id>", game_input.cpp) and the
    // ones a choice's own setFlags list writes (e.g. flag_page_returned) live in RunStoryState
    // instead and had no leaf able to read them back at all until a dialogue choice (ss_04) needed
    // to gate on one. Defaulted like the S3/M7 leaves above.
    virtual bool f_RunFlagSet(const std::string& /*flag*/) const { return false; }
    // NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): event variables (story/vars.json; a bool is
    // 0 / 1), how many times the player has talked to an NPC this run, and the stage the player is on.
    // Defaulted like the leaves above.
    virtual int f_VarValue(const std::string& /*var*/) const { return 0; }
    virtual int f_TalkCount(const std::string& /*npc*/) const { return 0; }
    virtual std::string f_CurrentStage() const { return ""; }
};

// A JSON value as an event variable's number: true / false are 1 / 0 (story/vars.json "bool" variables).
inline int f_VarNumber(const nlohmann::json& l_jV) {
    if (l_jV.is_boolean()) return l_jV.get<bool>() ? 1 : 0;
    if (l_jV.is_number()) return l_jV.get<int>();
    return 0;
}

// Recursively evaluates `expr` against `ctx`. A null/non-object `expr` is vacuously true (no
// requirement). An unrecognized leaf `type`, or a malformed combinator (a missing or
// wrong-typed array), fails closed (returns false) rather than silently granting access.
bool f_Evaluate(const nlohmann::json& l_jExpr, const ConditionContext& l_oCtx);

} // namespace toms
