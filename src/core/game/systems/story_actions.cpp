// story_actions.cpp -- see story_actions.h.
#include "story_actions.h"

#include "condition.h"   // toms::varNumber

#include <algorithm>

namespace toms::story {

namespace {

std::string str(const nlohmann::json& a, const char* key) {
    auto it = a.find(key);
    return it != a.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int num(const nlohmann::json& a, const char* key, int def) {
    auto it = a.find(key);
    if (it == a.end()) return def;
    if (it->is_boolean() || it->is_number()) return toms::varNumber(*it);
    return def;
}

template <class F>
void forEach(const nlohmann::json& actions, F f) {
    if (actions.is_object()) { f(actions); return; }
    if (!actions.is_array()) return;
    for (const auto& a : actions) f(a);
}

} // namespace

const std::vector<std::string>& knownStats() {
    static const std::vector<std::string> s = {"hp", "maxhp", "atk", "def", "gold", "exp", "lv"};
    return s;
}

RunReport runActions(const nlohmann::json& actions, StoryEffects& fx) {
    RunReport rep;
    forEach(actions, [&](const nlohmann::json& a) {
        if (!a.is_object()) { rep.problems.push_back("an action that is not an object"); return; }
        const std::string type = str(a, "type");
        bool ok = true;
        if (type == "addStat")           ok = fx.addStat(str(a, "stat"), num(a, "delta", 0));
        else if (type == "setStat")      ok = fx.setStat(str(a, "stat"), num(a, "value", 0));
        else if (type == "give")         ok = fx.give(str(a, "itemId"), std::max(1, num(a, "count", 1)));
        else if (type == "take")         ok = fx.take(str(a, "itemId"), std::max(1, num(a, "count", 1)));
        else if (type == "setVar")       fx.setVar(str(a, "var"), num(a, "value", 0));
        else if (type == "addVar")       fx.addVar(str(a, "var"), num(a, "delta", 1));
        else if (type == "setFlag")      fx.setFlag(str(a, "flag"));
        else if (type == "setStoryFlag") fx.setStoryFlag(str(a, "flag"));
        else if (type == "addCounter")   fx.addCounter(str(a, "counter"), num(a, "delta", 0));
        else if (type == "fireEvent")    fx.fireEvent(str(a, "eventId"));
        else if (type == "message")      fx.message(str(a, "text"));
        else                             ok = fx.other(a);
        if (ok) ++rep.ran;
        else rep.problems.push_back(type + ": " + a.dump());
    });
    return rep;
}

std::vector<std::string> checkActions(const nlohmann::json& actions, const Catalog& cat, const std::string& where) {
    std::vector<std::string> out;
    auto bad = [&](const std::string& msg) { out.push_back(where + ": " + msg); };
    auto known = [&](const std::set<std::string>& set, const std::string& name, const char* what) {
        if (name.empty()) bad(std::string("no ") + what);
        else if (!set.empty() && !set.count(name)) bad(std::string("unknown ") + what + " '" + name + "'");
    };
    if (!actions.is_null() && !actions.is_array() && !actions.is_object()) { bad("actions must be a list"); return out; }
    forEach(actions, [&](const nlohmann::json& a) {
        if (!a.is_object()) { bad("an action that is not an object"); return; }
        const std::string type = str(a, "type");
        if (type == "addStat" || type == "setStat") {
            const std::string s = str(a, "stat");
            if (s.empty()) bad(type + ": no stat");
            else if (!cat.stats.empty() ? !cat.stats.count(s)
                     : std::find(knownStats().begin(), knownStats().end(), s) == knownStats().end())
                bad(type + ": unknown stat '" + s + "'");
            const char* field = type == "addStat" ? "delta" : "value";
            if (!a.contains(field) || !a[field].is_number_integer()) bad(type + ": '" + field + "' must be a whole number");
        } else if (type == "give" || type == "take") {
            known(cat.items, str(a, "itemId"), "item");
            if (a.contains("count") && !(a["count"].is_number_integer() && a["count"].get<int>() >= 1)) bad(type + ": count must be 1 or more");
        } else if (type == "setVar" || type == "addVar") {
            known(cat.vars, str(a, "var"), "variable");
            const char* field = type == "setVar" ? "value" : "delta";
            if (type == "setVar" && !a.contains(field)) bad("setVar: no value");
            if (a.contains(field) && !(a[field].is_number_integer() || a[field].is_boolean())) bad(type + ": '" + field + "' must be a number or true / false");
        } else if (type == "setFlag") {
            known(cat.flags, str(a, "flag"), "flag");
        } else if (type == "setStoryFlag") {
            if (str(a, "flag").empty()) bad("setStoryFlag: no flag");
        } else if (type == "addCounter") {
            known(cat.counters, str(a, "counter"), "counter");
        } else if (type == "fireEvent") {
            known(cat.events, str(a, "eventId"), "event");
        } else if (type == "message") {
            if (str(a, "text").empty()) bad("message: no text key");
        } else if (!cat.otherVerbs.count(type)) {
            bad("unknown action '" + type + "'");
        }
    });
    return out;
}

bool grantsReward(const nlohmann::json& actions) {
    bool any = false;
    forEach(actions, [&](const nlohmann::json& a) {
        const std::string type = a.is_object() ? str(a, "type") : std::string();
        if (type == "addStat" || type == "setStat" || type == "give") any = true;
    });
    return any;
}

} // namespace toms::story
