// story_actions.cpp -- see story_actions.h.
#include "story_actions.h"

#include "condition.h"   // toms::varNumber

#include <algorithm>

namespace toms::story {

namespace {

std::string f_Str(const nlohmann::json& l_jA, const char* l_pszKey) {
    auto l_oIt = l_jA.find(l_pszKey);
    return l_oIt != l_jA.end() && l_oIt->is_string() ? l_oIt->get<std::string>() : std::string();
}

int f_Num(const nlohmann::json& l_jA, const char* l_pszKey, int l_iDef) {
    auto l_oIt = l_jA.find(l_pszKey);
    if (l_oIt == l_jA.end()) return l_iDef;
    if (l_oIt->is_boolean() || l_oIt->is_number()) return toms::f_VarNumber(*l_oIt);
    return l_iDef;
}

template <class F>
void f_ForEach(const nlohmann::json& l_jActions, F l_oF) {
    if (l_jActions.is_object()) { l_oF(l_jActions); return; }
    if (!l_jActions.is_array()) return;
    for (const auto& l_jA : l_jActions) l_oF(l_jA);
}

} // namespace

const std::vector<std::string>& f_KnownStats() {
    static const std::vector<std::string> l_vS = {"hp", "maxhp", "atk", "def", "gold", "exp", "lv"};
    return l_vS;
}

RunReport f_RunActions(const nlohmann::json& l_jActions, StoryEffects& l_oFx) {
    RunReport l_oRep;
    f_ForEach(l_jActions, [&](const nlohmann::json& l_jA) {
        if (!l_jA.is_object()) { l_oRep.m_vProblems.push_back("an action that is not an object"); return; }
        const std::string l_strType = f_Str(l_jA, "type");
        bool l_bOk = true;
        if (l_strType == "addStat")           l_bOk = l_oFx.f_AddStat(f_Str(l_jA, "stat"), f_Num(l_jA, "delta", 0));
        else if (l_strType == "setStat")      l_bOk = l_oFx.f_SetStat(f_Str(l_jA, "stat"), f_Num(l_jA, "value", 0));
        else if (l_strType == "give")         l_bOk = l_oFx.f_Give(f_Str(l_jA, "itemId"), std::max(1, f_Num(l_jA, "count", 1)));
        else if (l_strType == "take")         l_bOk = l_oFx.f_Take(f_Str(l_jA, "itemId"), std::max(1, f_Num(l_jA, "count", 1)));
        else if (l_strType == "setVar")       l_oFx.f_SetVar(f_Str(l_jA, "var"), f_Num(l_jA, "value", 0));
        else if (l_strType == "addVar")       l_oFx.f_AddVar(f_Str(l_jA, "var"), f_Num(l_jA, "delta", 1));
        else if (l_strType == "setFlag")      l_oFx.f_SetFlag(f_Str(l_jA, "flag"));
        else if (l_strType == "setStoryFlag") l_oFx.f_SetStoryFlag(f_Str(l_jA, "flag"));
        else if (l_strType == "addCounter")   l_oFx.f_AddCounter(f_Str(l_jA, "counter"), f_Num(l_jA, "delta", 0));
        else if (l_strType == "fireEvent")    l_oFx.f_FireEvent(f_Str(l_jA, "eventId"));
        else if (l_strType == "message")      l_oFx.f_Message(f_Str(l_jA, "text"));
        else                             l_bOk = l_oFx.f_Other(l_jA);
        if (l_bOk) ++l_oRep.m_iRan;
        else l_oRep.m_vProblems.push_back(l_strType + ": " + l_jA.dump());
    });
    return l_oRep;
}

std::vector<std::string> f_CheckActions(const nlohmann::json& l_jActions, const Catalog& l_oCat, const std::string& l_strWhere) {
    std::vector<std::string> l_vOut;
    auto l_fnBad = [&](const std::string& l_strMsg) { l_vOut.push_back(l_strWhere + ": " + l_strMsg); };
    auto l_fnKnown = [&](const std::set<std::string>& l_setSet, const std::string& l_strName, const char* l_pszWhat) {
        if (l_strName.empty()) l_fnBad(std::string("no ") + l_pszWhat);
        else if (!l_setSet.empty() && !l_setSet.count(l_strName)) l_fnBad(std::string("unknown ") + l_pszWhat + " '" + l_strName + "'");
    };
    if (!l_jActions.is_null() && !l_jActions.is_array() && !l_jActions.is_object()) { l_fnBad("actions must be a list"); return l_vOut; }
    f_ForEach(l_jActions, [&](const nlohmann::json& l_jA) {
        if (!l_jA.is_object()) { l_fnBad("an action that is not an object"); return; }
        const std::string l_strType = f_Str(l_jA, "type");
        if (l_strType == "addStat" || l_strType == "setStat") {
            const std::string l_strS = f_Str(l_jA, "stat");
            if (l_strS.empty()) l_fnBad(l_strType + ": no stat");
            else if (!l_oCat.m_setStats.empty() ? !l_oCat.m_setStats.count(l_strS)
                     : std::find(f_KnownStats().begin(), f_KnownStats().end(), l_strS) == f_KnownStats().end())
                l_fnBad(l_strType + ": unknown stat '" + l_strS + "'");
            const char* l_pszField = l_strType == "addStat" ? "delta" : "value";
            if (!l_jA.contains(l_pszField) || !l_jA[l_pszField].is_number_integer()) l_fnBad(l_strType + ": '" + l_pszField + "' must be a whole number");
        } else if (l_strType == "give" || l_strType == "take") {
            l_fnKnown(l_oCat.m_setItems, f_Str(l_jA, "itemId"), "item");
            if (l_jA.contains("count") && !(l_jA["count"].is_number_integer() && l_jA["count"].get<int>() >= 1)) l_fnBad(l_strType + ": count must be 1 or more");
        } else if (l_strType == "setVar" || l_strType == "addVar") {
            l_fnKnown(l_oCat.m_setVars, f_Str(l_jA, "var"), "variable");
            const char* l_pszField = l_strType == "setVar" ? "value" : "delta";
            if (l_strType == "setVar" && !l_jA.contains(l_pszField)) l_fnBad("setVar: no value");
            if (l_jA.contains(l_pszField) && !(l_jA[l_pszField].is_number_integer() || l_jA[l_pszField].is_boolean())) l_fnBad(l_strType + ": '" + l_pszField + "' must be a number or true / false");
        } else if (l_strType == "setFlag") {
            l_fnKnown(l_oCat.m_setFlags, f_Str(l_jA, "flag"), "flag");
        } else if (l_strType == "setStoryFlag") {
            if (f_Str(l_jA, "flag").empty()) l_fnBad("setStoryFlag: no flag");
        } else if (l_strType == "addCounter") {
            l_fnKnown(l_oCat.m_setCounters, f_Str(l_jA, "counter"), "counter");
        } else if (l_strType == "fireEvent") {
            l_fnKnown(l_oCat.m_setEvents, f_Str(l_jA, "eventId"), "event");
        } else if (l_strType == "message") {
            if (f_Str(l_jA, "text").empty()) l_fnBad("message: no text key");
        } else if (!l_oCat.m_setOtherVerbs.count(l_strType)) {
            l_fnBad("unknown action '" + l_strType + "'");
        }
    });
    return l_vOut;
}

bool f_GrantsReward(const nlohmann::json& l_jActions) {
    bool l_bAny = false;
    f_ForEach(l_jActions, [&](const nlohmann::json& l_jA) {
        const std::string l_strType = l_jA.is_object() ? f_Str(l_jA, "type") : std::string();
        if (l_strType == "addStat" || l_strType == "setStat" || l_strType == "give") l_bAny = true;
    });
    return l_bAny;
}

} // namespace toms::story
