// story_actions.h -- the one action vocabulary every story place runs (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 7).
//
// An action is plain JSON, { "type": "<verb>", ...fields }, and a result is a list of them, in any mix:
//   addStat / setStat   a player status value by name (hp, atk, def, gold, exp, lv, maxhp, ...)
//   give / take         items (a gift, a trade)
//   setVar / addVar     event variables (story/vars.json; true / false are 1 / 0)
//   setFlag             a run flag; setStoryFlag a story flag kept across runs
//   addCounter          a story counter (insight / resolve / humanity)
//   fireEvent           wake another event (as an event's `next`; a "chain" event runs at once)
//   message             a notification line (a text key)
// Every place that changes the game from data uses this: a tile event's record, an NPC topic's dialogue choices,
// a chat line, an after-dialogue, and the old dialogue verbs pass through `other` (startMission, makeChoice, ...).
// So a story built from events, NPCs and dialogues has one set of rules, one set of checks and one test.
//
// Pure logic: the game plugs its state in through StoryEffects (Game's own implementation; a recording fake in
// talk_system_test.cpp), and checkActions() validates data against a catalogue of known names (tests, editors).
#pragma once

#include <json.hpp>

#include <set>
#include <string>
#include <vector>

namespace toms::story {

// What actions can change. Each returns false when it cannot (an unknown stat, an item not held), and runActions
// reports it; the game state is left as it was for that action.
class StoryEffects {
public:
    virtual ~StoryEffects() = default;
    virtual bool f_AddStat(const std::string& l_strStat, int l_iDelta) = 0;
    virtual bool f_SetStat(const std::string& l_strStat, int l_iValue) = 0;
    virtual bool f_Give(const std::string& l_strItemId, int l_iCount) = 0;
    virtual bool f_Take(const std::string& l_strItemId, int l_iCount) = 0;
    virtual void f_SetVar(const std::string& l_strVar, int l_iValue) = 0;
    virtual void f_AddVar(const std::string& l_strVar, int l_iDelta) = 0;
    virtual void f_SetFlag(const std::string& l_strFlag) = 0;
    virtual void f_SetStoryFlag(const std::string& l_strFlag) = 0;
    virtual void f_AddCounter(const std::string& l_strCounter, int l_iDelta) = 0;
    virtual void f_FireEvent(const std::string& l_strEventId) = 0;
    virtual void f_Message(const std::string& l_strTextKey) = 0;
    // Any other verb (the older dialogue verbs: startMission, makeChoice, enterBattle, ...). false = unknown.
    virtual bool f_Other(const nlohmann::json& /*action*/) { return false; }
};

struct RunReport {
    int m_iRan = 0;                         // actions that did something
    std::vector<std::string> m_vProblems;   // "addStat: unknown stat 'str'", one per failed action
};

// Runs `actions` in order: an array of actions, a single action object, or null (nothing).
RunReport f_RunActions(const nlohmann::json& l_jActions, StoryEffects& l_oFx);

// The names data may use, for checkActions. An empty set means "do not check that kind of name".
struct Catalog {
    std::set<std::string> m_setStats, m_setItems, m_setVars, m_setFlags, m_setCounters, m_setEvents;
    std::set<std::string> m_setOtherVerbs = {"startMission", "claimMission", "makeChoice", "enterBattle", "unlockSkill",
                                        "setSideStoryState"};
};

// Problems in `actions` (same shapes as runActions): unknown verbs, missing fields, names not in the catalogue,
// a bool or number of the wrong kind. `where` prefixes every message (a file and a node, for example).
std::vector<std::string> f_CheckActions(const nlohmann::json& l_jActions, const Catalog& l_oCat, const std::string& l_strWhere);

// True when `actions` gives the player something that a repeated run would give again (addStat, setStat, give):
// used to warn about rewards in chat lines and after-dialogues, which the player can repeat.
bool f_GrantsReward(const nlohmann::json& l_jActions);

// The player stats addStat / setStat understand, in the order the docs list them.
const std::vector<std::string>& f_KnownStats();

} // namespace toms::story
