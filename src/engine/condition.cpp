#include "condition.h"

namespace toms {

bool evaluate(const nlohmann::json& expr, const ConditionContext& ctx) {
    if (expr.is_null()) return true;      // no requirement -> always available
    if (!expr.is_object()) return false;  // malformed -> fail closed

    if (expr.contains("all")) {
        if (!expr["all"].is_array()) return false;
        for (auto& sub : expr["all"]) if (!evaluate(sub, ctx)) return false;
        return true;
    }
    if (expr.contains("any")) {
        if (!expr["any"].is_array()) return false;
        for (auto& sub : expr["any"]) if (evaluate(sub, ctx)) return true;
        return false;
    }
    if (expr.contains("not")) {
        return !evaluate(expr["not"], ctx);
    }

    std::string type = expr.value("type", std::string());
    if (type == "storyBeatAtLeast") return ctx.storyBeat() >= expr.value("value", 0);
    if (type == "storyFlagSet")     return ctx.storyFlagSet(expr.value("flag", std::string()));
    if (type == "itemHeld")         return ctx.itemHeld(expr.value("itemId", std::string()), expr.value("count", 1));
    if (type == "statAtLeast")      return ctx.statValue(expr.value("stat", std::string())) >= expr.value("value", 0);
    if (type == "missionComplete")  return ctx.missionComplete(expr.value("missionId", std::string()));
    if (type == "missionActive")    return ctx.missionActive(expr.value("missionId", std::string()));
    if (type == "stageCleared")     return ctx.stageCleared(expr.value("stageId", std::string()));

    return false;   // unknown leaf type -> fail closed
}

} // namespace toms
