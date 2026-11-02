#include "condition.h"

namespace toms {

bool f_Evaluate(const nlohmann::json& l_jExpr, const ConditionContext& l_oCtx) {
    if (l_jExpr.is_null()) return true;      // no requirement -> always available
    if (!l_jExpr.is_object()) return false;  // malformed -> fail closed

    if (l_jExpr.contains("all")) {
        if (!l_jExpr["all"].is_array()) return false;
        for (auto& l_jSub : l_jExpr["all"]) if (!f_Evaluate(l_jSub, l_oCtx)) return false;
        return true;
    }
    if (l_jExpr.contains("any")) {
        if (!l_jExpr["any"].is_array()) return false;
        for (auto& l_jSub : l_jExpr["any"]) if (f_Evaluate(l_jSub, l_oCtx)) return true;
        return false;
    }
    if (l_jExpr.contains("not")) {
        return !f_Evaluate(l_jExpr["not"], l_oCtx);
    }

    std::string l_strType = l_jExpr.value("type", std::string());
    if (l_strType == "storyBeatAtLeast") return l_oCtx.f_StoryBeat() >= l_jExpr.value("value", 0);
    if (l_strType == "storyFlagSet")     return l_oCtx.f_StoryFlagSet(l_jExpr.value("flag", std::string()));
    if (l_strType == "runFlagSet")       return l_oCtx.f_RunFlagSet(l_jExpr.value("flag", std::string()));
    if (l_strType == "itemHeld")         return l_oCtx.f_ItemHeld(l_jExpr.value("itemId", std::string()), l_jExpr.value("count", 1));
    if (l_strType == "statAtLeast")      return l_oCtx.f_StatValue(l_jExpr.value("stat", std::string())) >= l_jExpr.value("value", 0);
    if (l_strType == "missionComplete")  return l_oCtx.f_MissionComplete(l_jExpr.value("missionId", std::string()));
    if (l_strType == "missionActive")    return l_oCtx.f_MissionActive(l_jExpr.value("missionId", std::string()));
    if (l_strType == "stageCleared")     return l_oCtx.f_StageCleared(l_jExpr.value("stageId", std::string()));

    // S3 leaves (docs/story/STORY_DATA_SCHEMA.md section 2.2). Each reads one fact from the run's story
    // state through the context; nothing here knows what a choice or a counter means.
    if (l_strType == "choiceMade")
        return l_oCtx.f_ChoiceMade(l_jExpr.value("choiceId", std::string()), l_jExpr.value("optionId", std::string()));
    if (l_strType == "sideStoryState")
        return l_oCtx.f_SideStoryState(l_jExpr.value("sideStoryId", std::string())) == l_jExpr.value("state", std::string());
    if (l_strType == "counterAtLeast")
        return l_oCtx.f_Counter(l_jExpr.value("counter", std::string())) >= l_jExpr.value("min", 0);
    if (l_strType == "cycleIndexAtLeast")
        return l_oCtx.f_CycleIndex() >= l_jExpr.value("min", 1);
    // M7 (docs/story/STORY_DATA_SCHEMA.md §7.1): the endings resolver's guaranteed fallback rule
    // needs an explicit, context-independent "always true" leaf -- a null/absent condition already
    // means this (see the file comment), but `endings.json`'s own schema spells it out as a real
    // leaf for clarity in a list that's otherwise all real conditions.
    if (l_strType == "always") return true;
    if (l_strType == "memoryShards") return l_oCtx.f_MemoryShardCount() >= l_jExpr.value("min", 0);
    if (l_strType == "deathsNonBoss") return l_oCtx.f_DeathsNonBoss() >= l_jExpr.value("min", 0);
    // NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 8).
    if (l_strType == "varAtLeast") return l_oCtx.f_VarValue(l_jExpr.value("var", std::string())) >= f_VarNumber(l_jExpr.value("value", nlohmann::json(0)));
    if (l_strType == "varEquals")  return l_oCtx.f_VarValue(l_jExpr.value("var", std::string())) == f_VarNumber(l_jExpr.value("value", nlohmann::json(0)));
    if (l_strType == "talkCountAtLeast") return l_oCtx.f_TalkCount(l_jExpr.value("npc", std::string())) >= l_jExpr.value("value", 0);
    if (l_strType == "onStage")    return l_oCtx.f_CurrentStage() == l_jExpr.value("stageId", std::string());

    return false;   // unknown leaf type -> fail closed
}

} // namespace toms
