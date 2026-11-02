// talk_system_test -- NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): talk_system.h + story_actions.h.
//
//   * chat lines: one line, loop, stay, random (never twice in a row), a line joining when its condition comes true,
//     talkCountAtLeast seeing the talks before this one;
//   * topics: open / inactive / finished with and without an after-dialogue, woken by `next`, topicOrder, and when the
//     menu appears (decision 2: one open topic alone plays at once; next to a finished one, the menu);
//   * a dialogue's conditional start; the conditions varAtLeast / varEquals / talkCountAtLeast / onStage;
//   * story actions: every verb reaches the effects, failures are reported, checkActions finds bad data;
//   * the run save keeps event variables and talk state;
//   * the shipped data: every NPC file, talk event, dialogue and action is valid, and the village elder's five
//     events open and close in the order the story needs;
//   * the stage editor's per-object settings and the types' looks: on an object's cell, sprites in the atlas, clips that
//     exist, tints, linked events that exist.
// Run in assets/ (tests/CMakeLists.txt).
#include "condition.h"
#include "run_state.h"
#include "save_system.h"
#include "story_actions.h"
#include "talk_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_iChecks = 0, g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using nlohmann::json;
using namespace toms::talk;

// A run as the conditions see it: finished / started events, variables, counters, talk counts, the stage, choices.
struct FakeRun : toms::ConditionContext, TalkQuery {
    std::set<std::string> m_setStarted, m_setFinished, m_setCleared;
    std::map<std::string, int> m_mapVars, m_mapCounters, m_mapTalks, m_mapStats, m_mapCounts;
    std::map<std::string, std::string> m_mapChoices;
    std::string m_strStage;
    std::vector<int> m_vRolls;   // random(): these in turn, then 0
    // ConditionContext
    bool f_StoryFlagSet(const std::string&) const override { return false; }
    int f_StoryBeat() const override { return 0; }
    bool f_ItemHeld(const std::string&, int) const override { return false; }
    int f_StatValue(const std::string& l_strS) const override { auto l_itIt = m_mapStats.find(l_strS); return l_itIt == m_mapStats.end() ? 0 : l_itIt->second; }
    bool f_MissionComplete(const std::string&) const override { return false; }
    bool f_MissionActive(const std::string&) const override { return false; }
    bool f_StageCleared(const std::string& l_strS) const override { return m_setCleared.count(l_strS) > 0; }
    bool f_RunFlagSet(const std::string& l_strF) const override { return l_strF.rfind("event_", 0) == 0 && m_setFinished.count(l_strF.substr(6)); }
    int f_Counter(const std::string& l_strC) const override { auto l_itIt = m_mapCounters.find(l_strC); return l_itIt == m_mapCounters.end() ? 0 : l_itIt->second; }
    int f_VarValue(const std::string& l_strV) const override { auto l_itIt = m_mapVars.find(l_strV); return l_itIt == m_mapVars.end() ? 0 : l_itIt->second; }
    int f_TalkCount(const std::string& l_strN) const override { auto l_itIt = m_mapTalks.find(l_strN); return l_itIt == m_mapTalks.end() ? 0 : l_itIt->second; }
    std::string f_CurrentStage() const override { return m_strStage; }
    bool f_ChoiceMade(const std::string& l_strC, const std::string& l_strO) const override { auto l_itIt = m_mapChoices.find(l_strC); return l_itIt != m_mapChoices.end() && l_itIt->second == l_strO; }
    // TalkQuery
    bool f_Holds(const json& l_jC) const override { return toms::f_Evaluate(l_jC, *this); }
    bool f_EventStarted(const std::string& l_strId) const override { return m_setStarted.count(l_strId) || m_setFinished.count(l_strId); }
    bool f_EventFinished(const std::string& l_strId) const override { return m_setFinished.count(l_strId) > 0; }
    int f_EventCount(const std::string& l_strId) const override {
        auto l_itIt = m_mapCounts.find(l_strId);
        const int l_iN = l_itIt == m_mapCounts.end() ? 0 : l_itIt->second;
        return m_setFinished.count(l_strId) ? std::max(l_iN, 1) : l_iN;
    }
    int f_Random(int l_iN) override {
        int l_iR = 0;
        if (!m_vRolls.empty()) { l_iR = m_vRolls.front(); m_vRolls.erase(m_vRolls.begin()); }
        return l_iN <= 0 ? 0 : l_iR % l_iN;
    }
};

json f_ReadJson(const std::string& l_strPath) {
    std::string l_strText;
    if (!toms::f_VfsReadAll(l_strPath, l_strText)) return json();
    return json::parse(l_strText, nullptr, false);
}

std::vector<std::string> f_JsonFiles(const std::string& l_strDir) {
    std::vector<std::string> l_vOut;
    for (const std::string& l_strN : toms::f_VfsListDir(l_strDir))
        if (l_strN.size() > 5 && l_strN.compare(l_strN.size() - 5, 5, ".json") == 0) l_vOut.push_back(l_strN);
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

NpcDef f_NpcWith(std::vector<std::string> l_vLines, ChatOrder l_eOrder) {
    NpcDef l_oN;
    l_oN.m_strId = "npc";
    for (auto& l_strL : l_vLines) l_oN.m_vLines.push_back({l_strL, json(), json()});
    l_oN.m_eOrder = l_eOrder;
    return l_oN;
}

// Plays `talks` chat lines and returns their dialogue ids.
std::vector<std::string> f_Chat(const NpcDef& l_oNpc, toms::NpcTalkState& l_oSt, FakeRun& l_oRun, int l_iTalks) {
    std::vector<std::string> l_vOut;
    for (int l_iI = 0; l_iI < l_iTalks; ++l_iI) {
        const int l_iL = f_NextChatLine(l_oNpc, l_oSt, l_oRun);
        l_vOut.push_back(l_iL < 0 ? "-" : l_oNpc.m_vLines[l_iL].m_strDialogue);
    }
    return l_vOut;
}

std::string f_Joined(const std::vector<std::string>& l_vV) {
    std::string l_strS;
    for (auto& l_strX : l_vV) l_strS += (l_strS.empty() ? "" : " ") + l_strX;
    return l_strS;
}

std::string f_MenuIds(const std::vector<MenuEntry>& l_vM) {
    std::string l_strS;
    for (auto& l_oE : l_vM) l_strS += (l_strS.empty() ? "" : " ") + l_oE.m_strEventId + (l_oE.m_bFinished ? "*" : "");
    return l_strS;
}

void f_TestChatLines() {
    FakeRun l_oRun;
    toms::NpcTalkState l_oSt;
    CHECK(f_Joined(f_Chat(f_NpcWith({"A"}, ChatOrder::Loop), l_oSt, l_oRun, 4)) == "A A A A", "one line is always the first");
    l_oSt = {};
    CHECK(f_Joined(f_Chat(f_NpcWith({"A", "B", "C"}, ChatOrder::Loop), l_oSt, l_oRun, 5)) == "A B C A B", "loop");
    l_oSt = {};
    CHECK(f_Joined(f_Chat(f_NpcWith({"A", "B", "C"}, ChatOrder::Stay), l_oSt, l_oRun, 5)) == "A B C C C", "stay");
    l_oSt = {};
    l_oRun.m_vRolls = {1, 1, 0, 2, 2};   // B, then B again would repeat -> moved on
    const std::vector<std::string> l_vR = f_Chat(f_NpcWith({"A", "B", "C"}, ChatOrder::Random), l_oSt, l_oRun, 5);
    bool l_bRepeat = false;
    for (size_t l_nI = 1; l_nI < l_vR.size(); ++l_nI) l_bRepeat |= l_vR[l_nI] == l_vR[l_nI - 1];
    CHECK(!l_bRepeat, "random never plays a line twice in a row: %s", f_Joined(l_vR).c_str());

    // A line that needs 3 talks joins the rotation; conditions see the talks before this one.
    NpcDef l_oN = f_NpcWith({"A", "S"}, ChatOrder::Loop);
    l_oN.m_vLines[1].m_jRequires = {{"type", "talkCountAtLeast"}, {"npc", "npc"}, {"value", 3}};
    l_oSt = {};
    l_oRun = FakeRun{};
    std::vector<std::string> l_vSeen;
    for (int l_iI = 0; l_iI < 6; ++l_iI) {
        l_oRun.m_mapTalks["npc"] = l_oSt.m_iCount;
        const TalkPlan l_oP = f_PlanTalk(l_oN, {}, l_oSt, l_oRun);
        l_vSeen.push_back(l_oP.m_eKind == TalkKind::Chat ? l_oN.m_vLines[l_oP.m_iChatLine].m_strDialogue : "?");
    }
    CHECK(f_Joined(l_vSeen) == "A A A S A S", "a line needing 3 talks plays from the 4th talk: %s", f_Joined(l_vSeen).c_str());
    CHECK(l_oSt.m_iCount == 6, "every talk is counted (%d)", l_oSt.m_iCount);
}

json f_Event(const std::string& l_strId, const std::string& l_strNpc, json l_jExtra = json::object()) {
    json l_jE = {{"eventId", l_strId}, {"trigger", {{"on", "talk"}, {"npc", l_strNpc}}}};
    for (auto& [l_strK, l_jV] : l_jExtra.items()) l_jE[l_strK] = l_jV;
    return l_jE;
}

void f_TestTopics() {
    // The design's example (section 5): five events, three listed (two open, one finished), two not.
    const json l_jPool = json::array({
        f_Event("well", "v", {{"next", {"ledger"}}}),
        f_Event("ledger", "v"),
        f_Event("bell", "v", {{"requires", {{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 1}}}}),
        f_Event("road", "v", {{"afterDialogue", "road_after"}}),
        f_Event("wish", "v", {{"requires", {{"type", "stageCleared"}, {"stageId", "F10"}}}}),
        f_Event("other", "w"),
        {{"eventId", "tile"}},   // not a talk event
    });
    const auto l_mapTopics = f_CollectTopics({l_jPool});
    CHECK(l_mapTopics.size() == 2 && l_mapTopics.at("v").size() == 5 && l_mapTopics.at("w").size() == 1, "topics grouped by NPC");
    CHECK(l_mapTopics.at("v")[1].m_bNeedsWake && !l_mapTopics.at("v")[0].m_bNeedsWake, "an event named in `next` waits to be woken");

    FakeRun l_oRun;
    l_oRun.m_mapCounters["insight"] = 1;
    l_oRun.m_setFinished = {"road"};
    CHECK(f_MenuIds(f_MenuTopics(l_mapTopics.at("v"), l_oRun)) == "well bell road*", "the example: %s", f_MenuIds(f_MenuTopics(l_mapTopics.at("v"), l_oRun)).c_str());
    l_oRun.m_setFinished.insert("well");
    l_oRun.m_setStarted.insert("ledger");
    CHECK(f_MenuIds(f_MenuTopics(l_mapTopics.at("v"), l_oRun)) == "ledger bell road*",
          "finishing the well wakes the ledger; a finished event without an after-dialogue is hidden: %s",
          f_MenuIds(f_MenuTopics(l_mapTopics.at("v"), l_oRun)).c_str());

    // topicOrder moves an event ahead of pool order.
    const auto l_mapOrdered = f_CollectTopics({json::array({f_Event("a", "v"), f_Event("b", "v", {{"topicOrder", 1}})})});
    CHECK(l_mapOrdered.at("v")[0].m_strEventId == "b", "topicOrder first");

    // When the menu appears (decision 2).
    NpcDef l_oNpc = f_NpcWith({"chat"}, ChatOrder::Loop);
    l_oNpc.m_strId = "v";
    toms::NpcTalkState l_oSt;
    FakeRun l_oR2;
    const auto l_mapOne = f_CollectTopics({json::array({f_Event("x", "v", {{"afterDialogue", "x_after"}}), f_Event("y", "v")})});
    TalkPlan l_oP = f_PlanTalk(l_oNpc, l_mapOne.at("v"), l_oSt, l_oR2);
    CHECK(l_oP.m_eKind == TalkKind::Menu && l_oP.m_vMenu.size() == 2, "two open topics: the menu");
    l_oR2.m_setFinished = {"y"};
    l_oP = f_PlanTalk(l_oNpc, l_mapOne.at("v"), l_oSt, l_oR2);
    CHECK(l_oP.m_eKind == TalkKind::Topic && l_oP.m_oTopic.m_strEventId == "x", "one open topic, the other finished and hidden: plays at once");
    l_oR2.m_setFinished = {"x"};
    l_oP = f_PlanTalk(l_oNpc, l_mapOne.at("v"), l_oSt, l_oR2);
    CHECK(l_oP.m_eKind == TalkKind::Menu && f_MenuIds(l_oP.m_vMenu) == "y x*", "one open next to a finished one: the menu (%s)", f_MenuIds(l_oP.m_vMenu).c_str());
    // A repeatable topic ("once": false, maxTimes 2) is open again after finishing, until its runs are used up.
    const auto l_mapRep = f_CollectTopics({json::array({f_Event("r", "v", {{"once", false}, {"maxTimes", 2}, {"afterDialogue", "r_after"}}),
                                                 f_Event("u", "v", {{"once", false}})})});
    FakeRun l_oR3;
    l_oR3.m_setFinished = {"r", "u"};
    l_oR3.m_mapCounts = {{"r", 1}, {"u", 40}};
    CHECK(f_MenuIds(f_MenuTopics(l_mapRep.at("v"), l_oR3)) == "r u", "repeatable: open again after one run; no limit: always (%s)", f_MenuIds(f_MenuTopics(l_mapRep.at("v"), l_oR3)).c_str());
    l_oR3.m_mapCounts["r"] = 2;
    CHECK(f_MenuIds(f_MenuTopics(l_mapRep.at("v"), l_oR3)) == "u r*", "used up: listed as finished for its after-dialogue (%s)", f_MenuIds(f_MenuTopics(l_mapRep.at("v"), l_oR3)).c_str());
    l_oR2.m_setFinished = {"x", "y"};
    l_oP = f_PlanTalk(l_oNpc, l_mapOne.at("v"), l_oSt, l_oR2);
    CHECK(l_oP.m_eKind == TalkKind::Menu && f_MenuIds(l_oP.m_vMenu) == "x*", "only a finished one with an after-dialogue: the menu");
    l_oP = f_PlanTalk(l_oNpc, {}, l_oSt, l_oR2);
    CHECK(l_oP.m_eKind == TalkKind::Chat, "no topics: a chat line");
    l_oNpc.m_bMenuAlways = true;
    l_oR2.m_setFinished = {"y"};
    l_oP = f_PlanTalk(l_oNpc, l_mapOne.at("v"), l_oSt, l_oR2);
    CHECK(l_oP.m_eKind == TalkKind::Menu, "menu \"always\"");
}

void f_TestConditionsAndStart() {
    FakeRun l_oRun;
    l_oRun.m_mapVars["v"] = 1;
    l_oRun.m_mapTalks["elder"] = 8;
    l_oRun.m_strStage = "stage_11";
    CHECK(toms::f_Evaluate({{"type", "varEquals"}, {"var", "v"}, {"value", true}}, l_oRun), "varEquals true = 1");
    CHECK(!toms::f_Evaluate({{"type", "varEquals"}, {"var", "v"}, {"value", false}}, l_oRun), "varEquals false");
    CHECK(toms::f_Evaluate({{"type", "varAtLeast"}, {"var", "v"}, {"value", 1}}, l_oRun), "varAtLeast");
    CHECK(toms::f_Evaluate({{"type", "talkCountAtLeast"}, {"npc", "elder"}, {"value", 8}}, l_oRun), "talkCountAtLeast");
    CHECK(toms::f_Evaluate({{"type", "onStage"}, {"stageId", "stage_11"}}, l_oRun), "onStage");
    const json l_jD = {{"start", json::array({{{"node", "pay"}, {"requires", {{"type", "choiceMade"}, {"choiceId", "e"}, {"optionId", "pay"}}}},
                                           {{"node", "thanks"}}})}};
    CHECK(f_StartNode(l_jD, l_oRun) == "thanks", "conditional start: the fallback");
    l_oRun.m_mapChoices["e"] = "pay";
    CHECK(f_StartNode(l_jD, l_oRun) == "pay", "conditional start: the choice made");
    CHECK(f_StartNode({{"start", "root"}}, l_oRun) == "root", "plain start");
}

struct Recorder : toms::story::StoryEffects {
    std::vector<std::string> m_vLog;
    bool f_AddStat(const std::string& l_strS, int l_iD) override { m_vLog.push_back("addStat " + l_strS + " " + std::to_string(l_iD)); return l_strS != "nope"; }
    bool f_SetStat(const std::string& l_strS, int l_iV) override { m_vLog.push_back("setStat " + l_strS + " " + std::to_string(l_iV)); return true; }
    bool f_Give(const std::string& l_strI, int l_iC) override { m_vLog.push_back("give " + l_strI + " " + std::to_string(l_iC)); return true; }
    bool f_Take(const std::string& l_strI, int l_iC) override { m_vLog.push_back("take " + l_strI + " " + std::to_string(l_iC)); return false; }
    void f_SetVar(const std::string& l_strV, int l_iX) override { m_vLog.push_back("setVar " + l_strV + " " + std::to_string(l_iX)); }
    void f_AddVar(const std::string& l_strV, int l_iD) override { m_vLog.push_back("addVar " + l_strV + " " + std::to_string(l_iD)); }
    void f_SetFlag(const std::string& l_strF) override { m_vLog.push_back("setFlag " + l_strF); }
    void f_SetStoryFlag(const std::string& l_strF) override { m_vLog.push_back("setStoryFlag " + l_strF); }
    void f_AddCounter(const std::string& l_strC, int l_iD) override { m_vLog.push_back("addCounter " + l_strC + " " + std::to_string(l_iD)); }
    void f_FireEvent(const std::string& l_strE) override { m_vLog.push_back("fireEvent " + l_strE); }
    void f_Message(const std::string& l_strT) override { m_vLog.push_back("message " + l_strT); }
};

void f_TestActions() {
    Recorder l_oR;
    const json l_jActs = json::parse(R"([
        {"type": "addStat", "stat": "str", "delta": 2}, {"type": "setStat", "stat": "hp", "value": 50},
        {"type": "give", "itemId": "gem_atk"}, {"type": "take", "itemId": "potion_red", "count": 2},
        {"type": "setVar", "var": "seen", "value": true}, {"type": "addVar", "var": "n", "delta": 3},
        {"type": "setFlag", "flag": "f"}, {"type": "setStoryFlag", "flag": "s"},
        {"type": "addCounter", "counter": "insight", "delta": -1}, {"type": "fireEvent", "eventId": "ev_x"},
        {"type": "message", "text": "k"}, {"type": "addStat", "stat": "nope", "delta": 1}, {"type": "dance"}])");
    const toms::story::RunReport l_oRep = toms::story::f_RunActions(l_jActs, l_oR);
    CHECK(f_Joined(l_oR.m_vLog) == "addStat str 2 setStat hp 50 give gem_atk 1 take potion_red 2 setVar seen 1 addVar n 3 setFlag f "
                           "setStoryFlag s addCounter insight -1 fireEvent ev_x message k addStat nope 1",
          "every verb in order: %s", f_Joined(l_oR.m_vLog).c_str());
    CHECK(l_oRep.m_iRan == 10 && l_oRep.m_vProblems.size() == 3, "ran %d, problems %zu (take, unknown stat, unknown verb)", l_oRep.m_iRan, l_oRep.m_vProblems.size());
    Recorder l_oOne;
    toms::story::f_RunActions({{"type", "give"}, {"itemId", "a"}}, l_oOne);
    CHECK(l_oOne.m_vLog.size() == 1, "a single action object runs too");

    toms::story::Catalog l_oCat;
    l_oCat.m_setItems = {"gem_atk"};
    l_oCat.m_setVars = {"seen"};
    const auto l_vErrs = toms::story::f_CheckActions(json::parse(R"([
        {"type": "addStat", "stat": "luck", "delta": 1}, {"type": "addStat", "stat": "atk", "delta": "2"},
        {"type": "give", "itemId": "gem_x"}, {"type": "setVar", "var": "seen", "value": "yes"},
        {"type": "fly"}, {"type": "give", "itemId": "gem_atk", "count": 0}])"), l_oCat, "t");
    CHECK(l_vErrs.size() == 6, "checkActions: %zu problems", l_vErrs.size());
    CHECK(toms::story::f_CheckActions(json::parse(R"([{"type": "addStat", "stat": "atk", "delta": 2}, {"type": "startMission", "missionId": "m"}])"),
                                    l_oCat, "t").empty(), "valid actions pass");
    CHECK(toms::story::f_GrantsReward(json::parse(R"([{"type": "setVar", "var": "a", "value": 1}, {"type": "give", "itemId": "x"}])")), "a gift is a reward");
    CHECK(!toms::story::f_GrantsReward(json::parse(R"([{"type": "setVar", "var": "a", "value": 1}])")), "a variable is not");
}

void f_TestSave() {
    toms::RunStoryState l_oRun;
    l_oRun.f_SetVarDecls({{"flag", {0, 0, 1}}, {"trust", {2, 0, 5}}});
    CHECK(l_oRun.f_Var("trust") == 2 && l_oRun.f_Var("unknown") == 0, "defaults");
    l_oRun.f_SetVar("flag", 7);
    l_oRun.f_AddVar("trust", 10);
    CHECK(l_oRun.f_Var("flag") == 1 && l_oRun.f_Var("trust") == 5, "clamped to the declared range");
    l_oRun.f_NpcTalk("villager") = {3, 9, 1};
    toms::RunSaveData l_oD;
    l_oRun.f_WriteInto(l_oD);
    const toms::RunSaveData l_oBack = toms::f_RunFromJson(toms::f_ToJson(l_oD));
    toms::RunStoryState l_oR2;
    l_oR2.f_SetVarDecls(l_oRun.f_VarDecls());
    l_oR2.f_ReadFrom(l_oBack);
    CHECK(l_oR2.f_Var("flag") == 1 && l_oR2.f_Var("trust") == 5, "variables survive a save");
    const toms::NpcTalkState l_oT = l_oR2.f_NpcTalk("villager");
    CHECK(l_oT.m_iIndex == 3 && l_oT.m_iCount == 9 && l_oT.m_iLastRandom == 1, "talk state survives a save");
    l_oR2.f_Reset();
    CHECK(l_oR2.f_Var("trust") == 2 && l_oR2.f_NpcTalk("villager").m_iCount == 0, "a new run starts over");
}

// ---------- the shipped data ----------

std::set<std::string> f_KeysOf(const json& l_jJ) {
    std::set<std::string> l_setS;
    if (l_jJ.is_object()) for (auto& [l_strK, l_jV] : l_jJ.items()) if (!l_strK.empty() && l_strK[0] != '_') l_setS.insert(l_strK);
    return l_setS;
}

void f_TestData() {
    // the catalogue every action is checked against
    toms::story::Catalog l_oCat;
    l_oCat.m_setItems = f_KeysOf(f_ReadJson("data/items.json"));
    for (const char* l_pszK : {"key_yellow", "key_blue", "key_red"}) l_oCat.m_setItems.insert(l_pszK);
    l_oCat.m_setVars = f_KeysOf(f_ReadJson("data/story/vars.json"));
    l_oCat.m_setCounters = f_KeysOf(f_ReadJson("data/story/counters.json"));
    for (const auto& l_strS : toms::story::f_KnownStats()) l_oCat.m_setStats.insert(l_strS);
    const json l_jStats = f_ReadJson("data/stats.json");
    if (l_jStats.contains("attributes")) for (const auto& l_jA : l_jStats["attributes"]) l_oCat.m_setStats.insert(l_jA.value("id", std::string()));
    CHECK(l_oCat.m_setStats.count("str"), "STR is an attribute addStat can raise");

    std::vector<json> l_vPools;
    std::map<std::string, json> l_mapEvents;
    for (const std::string& l_strN : f_JsonFiles("data/events/"))
        if (l_strN.rfind("pool_", 0) == 0) {
            const json l_jP = f_ReadJson("data/events/" + l_strN);
            l_vPools.push_back(l_jP.value("events", json::array()));
            for (const auto& l_jE : l_vPools.back()) { l_mapEvents[l_jE.value("eventId", std::string())] = l_jE; l_oCat.m_setEvents.insert(l_jE.value("eventId", std::string())); }
        }
    for (const auto& [l_strId, l_jE] : l_mapEvents)
        for (const std::string& l_strP : toms::story::f_CheckActions(l_jE.value("actions", json()), l_oCat, l_strId)) CHECK(false, "%s", l_strP.c_str());

    // NPCs placed anywhere (tower floors and the old stages)
    std::set<std::string> l_setPlaced;
    auto l_fnScan = [&](const std::string& l_strPath) {
        const json l_jSt = f_ReadJson(l_strPath);
        if (!l_jSt.contains("legend") || !l_jSt.contains("tiles")) return;
        std::string l_strCells;
        for (const auto& l_jRow : l_jSt["tiles"]) if (l_jRow.is_string()) l_strCells += l_jRow.get<std::string>();
        for (auto& [l_strCh, l_jSem] : l_jSt["legend"].items())
            if (l_jSem.is_string() && l_jSem.get<std::string>().rfind("npc:", 0) == 0 && l_strCells.find(l_strCh) != std::string::npos) l_setPlaced.insert(l_jSem.get<std::string>().substr(4));
    };
    for (const std::string& l_strN : f_JsonFiles("data/story/floors/")) if (l_strN.find(".stage.") != std::string::npos) l_fnScan("data/story/floors/" + l_strN);
    for (const std::string& l_strN : f_JsonFiles("data/stages/")) l_fnScan("data/stages/" + l_strN);
    CHECK(l_setPlaced.count("villager"), "the villager stands somewhere");

    // a dialogue file: it exists, every next / start node exists, every action is valid
    std::set<std::string> l_setChecked;
    auto l_fnCheckDialogue = [&](const std::string& l_strId, const char* l_pszRole, bool l_bRepeatable) -> json {
        const json l_jD = f_ReadJson("data/dialogue/" + l_strId + ".json");
        CHECK(l_jD.is_object() && l_jD.contains("nodes"), "%s dialogue %s exists", l_pszRole, l_strId.c_str());
        if (!l_jD.is_object() || !l_jD.contains("nodes") || !l_setChecked.insert(l_strId).second) return l_jD;
        const json& l_jNodes = l_jD["nodes"];
        std::vector<std::string> l_vStarts;
        if (l_jD["start"].is_string()) l_vStarts.push_back(l_jD["start"]);
        else for (const auto& l_jS : l_jD["start"]) l_vStarts.push_back(l_jS.value("node", std::string()));
        for (auto& l_strS : l_vStarts) CHECK(l_jNodes.contains(l_strS), "%s: start node %s exists", l_strId.c_str(), l_strS.c_str());
        for (auto& [l_strNid, l_jNode] : l_jNodes.items())
            for (const auto& l_jC : l_jNode.value("choices", json::array())) {
                // "next": "" closes the dialogue, like null
                if (l_jC.contains("next") && l_jC["next"].is_string() && !l_jC["next"].get<std::string>().empty()) CHECK(l_jNodes.contains(l_jC["next"].get<std::string>()), "%s/%s: next %s exists", l_strId.c_str(), l_strNid.c_str(), l_jC["next"].get<std::string>().c_str());
                for (const std::string& l_strP : toms::story::f_CheckActions(l_jC.value("actions", json()), l_oCat, l_strId + "/" + l_strNid)) CHECK(false, "%s", l_strP.c_str());
                if (l_bRepeatable) CHECK(!toms::story::f_GrantsReward(l_jC.value("actions", json())) && !l_jC.contains("finish"),
                                      "%s/%s: a repeatable dialogue gives no reward and finishes nothing", l_strId.c_str(), l_strNid.c_str());
                for (const auto& l_jF : l_jC.value("fire", json::array())) CHECK(l_mapEvents.count(l_jF.get<std::string>()), "%s: fires a real event", l_strId.c_str());
            }
        return l_jD;
    };

    // NPC files
    std::map<std::string, NpcDef> l_mapNpcs;
    for (const std::string& l_strN : f_JsonFiles("data/npcs/")) {
        NpcDef l_oD;
        std::string l_strErr;
        CHECK(f_ParseNpc(f_ReadJson("data/npcs/" + l_strN), l_oD, &l_strErr), "npcs/%s: %s", l_strN.c_str(), l_strErr.c_str());
        CHECK(l_oD.m_strId + ".json" == l_strN, "npcs/%s: the file is named after its npcId", l_strN.c_str());
        l_mapNpcs[l_oD.m_strId] = l_oD;
        for (const ChatLine& l_oL : l_oD.m_vLines) {
            l_fnCheckDialogue(l_oL.m_strDialogue, "chat", false);
            for (const std::string& l_strP : toms::story::f_CheckActions(l_oL.m_jActions, l_oCat, l_strN)) CHECK(false, "%s", l_strP.c_str());
            CHECK(!toms::story::f_GrantsReward(l_oL.m_jActions), "npcs/%s: a chat line (repeatable) gives no reward", l_strN.c_str());
        }
    }
    for (const char* l_pszId : {"villager", "sorcerer", "king", "princess", "handmaiden", "skeleton_scholar"})
        CHECK(l_mapNpcs.count(l_pszId), "every placed NPC has a file: %s", l_pszId);

    // repeatable and varChanged events: valid fields, a declared variable
    for (const auto& [l_strId, l_jE] : l_mapEvents) {
        if (l_jE.contains("once")) CHECK(l_jE["once"].is_boolean(), "%s: once is true / false", l_strId.c_str());
        if (l_jE.contains("maxTimes")) CHECK(l_jE["maxTimes"].is_number_integer() && l_jE["maxTimes"].get<int>() >= 0, "%s: maxTimes is 0 or more", l_strId.c_str());
        if (l_jE.contains("maxTimes")) CHECK(!l_jE.value("once", true), "%s: maxTimes without \"once\": false does nothing", l_strId.c_str());
        const json l_jTr = l_jE.value("trigger", json::object());
        if (l_jTr.value("on", std::string()) == "varChanged")
            CHECK(l_oCat.m_setVars.count(l_jTr.value("var", std::string())), "%s: fires when %s changes, which story/vars.json lacks", l_strId.c_str(), l_jTr.value("var", std::string()).c_str());
    }

    // talk events
    const auto l_mapTopics = f_CollectTopics(l_vPools);
    for (const auto& [l_strNpc, l_vList] : l_mapTopics) {
        CHECK(l_setPlaced.count(l_strNpc), "talk events' NPC %s is placed on a floor", l_strNpc.c_str());
        for (const Topic& l_oT : l_vList) {
            if (!l_oT.m_strDialogue.empty()) {
                const json l_jD = l_fnCheckDialogue(l_oT.m_strDialogue, "topic", false);
                bool l_bFinishes = l_jD.is_object() && l_jD.value("finishOnClose", false);
                const json l_jNodes = l_jD.is_object() ? l_jD.value("nodes", json::object()) : json::object();
                for (auto& [l_strNid, l_jNode] : l_jNodes.items())
                    for (const auto& l_jC : l_jNode.value("choices", json::array())) l_bFinishes |= l_jC.value("finish", false);
                CHECK(l_bFinishes, "%s: its dialogue can finish it", l_oT.m_strEventId.c_str());
            }
            if (!l_oT.m_strAfterDialogue.empty()) l_fnCheckDialogue(l_oT.m_strAfterDialogue, "after", true);
        }
    }

    // The village elder's story, played through the talk logic with a fake run.
    CHECK(l_mapTopics.count("villager") && l_mapTopics.at("villager").size() == 6, "the elder has six topics: his five worries and the potion trade");
    if (!l_mapTopics.count("villager") || !l_mapNpcs.count("villager")) return;
    const auto& l_vElder = l_mapTopics.at("villager");
    const NpcDef& l_oVillager = l_mapNpcs.at("villager");
    FakeRun l_oRun;
    toms::NpcTalkState l_oSt;
    auto l_fnTalk = [&]() { l_oRun.m_mapTalks["villager"] = l_oSt.m_iCount; return f_PlanTalk(l_oVillager, l_vElder, l_oSt, l_oRun); };
    TalkPlan l_oP = l_fnTalk();
    CHECK(l_oP.m_eKind == TalkKind::Menu && f_MenuIds(l_oP.m_vMenu) == "ev_elder_north_road ev_elder_well_night", "start: two topics (%s)", f_MenuIds(l_oP.m_vMenu).c_str());
    l_oRun.m_setFinished.insert("ev_elder_north_road");
    l_oRun.m_mapVars["elder_trust"] = 1;
    l_oRun.m_mapCounters["insight"] = 1;   // the bell chain's reward
    l_oP = l_fnTalk();
    CHECK(f_MenuIds(l_oP.m_vMenu) == "ev_elder_well_night ev_elder_bell_rumour ev_elder_north_road*",
          "five events, three listed and two not (%s)", f_MenuIds(l_oP.m_vMenu).c_str());
    l_oRun.m_setFinished.insert("ev_elder_well_night");
    l_oRun.m_setStarted.insert("ev_elder_ledger");   // woken by the well's `next`
    l_oP = l_fnTalk();
    CHECK(f_MenuIds(l_oP.m_vMenu) == "ev_elder_ledger ev_elder_bell_rumour ev_elder_north_road* ev_elder_well_night*",
          "the well wakes the ledger (%s)", f_MenuIds(l_oP.m_vMenu).c_str());
    l_oRun.m_setFinished.insert("ev_elder_ledger");
    l_oRun.m_setFinished.insert("ev_elder_bell_rumour");
    l_oRun.m_mapVars["elder_trust"] = 3;
    l_oP = l_fnTalk();
    CHECK(f_MenuIds(l_oP.m_vMenu).find("ev_elder_last_wish") == std::string::npos, "the last wish waits for F05");
    l_oRun.m_setCleared.insert("F05");
    l_oP = l_fnTalk();
    CHECK(!l_oP.m_vMenu.empty() && l_oP.m_vMenu[0].m_strEventId == "ev_elder_last_wish", "trust 3 and F05 cleared: the last wish (%s)", f_MenuIds(l_oP.m_vMenu).c_str());
    l_oRun.m_setFinished.insert("ev_elder_last_wish");
    l_oP = l_fnTalk();
    CHECK(f_MenuIds(l_oP.m_vMenu) == "ev_elder_potion ev_elder_north_road* ev_elder_well_night* ev_elder_bell_rumour*",
          "at the end: the potion trade and the three after-dialogues (%s)", f_MenuIds(l_oP.m_vMenu).c_str());
    l_oRun.m_mapCounts["ev_elder_potion"] = 3;
    l_oRun.m_setFinished.insert("ev_elder_potion");
    l_oP = l_fnTalk();
    CHECK(f_MenuIds(l_oP.m_vMenu) == "ev_elder_north_road* ev_elder_well_night* ev_elder_bell_rumour*",
          "three potions bought: the trade is gone (%s)", f_MenuIds(l_oP.m_vMenu).c_str());

    // Chat lines: no topics -> the rotation; the secret joins after eight talks.
    FakeRun l_oQuiet;
    toms::NpcTalkState l_oCs;
    std::vector<std::string> l_vLines;
    for (int l_iI = 0; l_iI < 10; ++l_iI) {
        l_oQuiet.m_mapTalks["villager"] = l_oCs.m_iCount;
        const TalkPlan l_oC = f_PlanTalk(l_oVillager, {}, l_oCs, l_oQuiet);
        l_vLines.push_back(l_oC.m_eKind == TalkKind::Chat ? l_oVillager.m_vLines[l_oC.m_iChatLine].m_strDialogue : "?");
    }
    CHECK(l_vLines[0] == "villager_elder" && l_vLines[1] == "elder_chat_weather" && l_vLines[2] == "villager_elder", "chat lines loop");
    CHECK(std::find(l_vLines.begin(), l_vLines.begin() + 8, "elder_secret_well") == l_vLines.begin() + 8 &&
          std::find(l_vLines.begin() + 8, l_vLines.end(), "elder_secret_well") != l_vLines.end(), "the well's secret only after eight talks: %s", f_Joined(l_vLines).c_str());
}

// ---------- the stage editor's per-object settings and the types' looks (docs/gameplay/02_STAGE_EDITOR.md) ----------
void f_TestObjects() {
    // the game atlas's sprite names (libGDX text: unindented lines without ':' after the header)
    std::set<std::string> l_setSprites;
    {
        std::string l_strText;
        if (toms::f_VfsReadAll("media/atlas/game.atlas", l_strText)) {
            size_t l_nA = 0; bool l_bFirst = true;
            while (l_nA < l_strText.size()) {
                size_t l_nB = l_strText.find('\n', l_nA); if (l_nB == std::string::npos) l_nB = l_strText.size();
                std::string l_strLine = l_strText.substr(l_nA, l_nB - l_nA); if (!l_strLine.empty() && l_strLine.back() == '\r') l_strLine.pop_back();
                if (!l_bFirst && !l_strLine.empty() && l_strLine[0] != ' ' && l_strLine[0] != '\t' && l_strLine.find(':') == std::string::npos &&
                    l_strLine.find(".png") == std::string::npos) l_setSprites.insert(l_strLine);
                l_bFirst = false; l_nA = l_nB + 1;
            }
        }
    }
    CHECK(l_setSprites.count("slime") && l_setSprites.count("npc_king"), "the atlas's sprite names are read (%zu)", l_setSprites.size());
    std::set<std::string> l_setEvents;
    for (const std::string& l_strN : f_JsonFiles("data/events/"))
        if (l_strN.rfind("pool_", 0) == 0) for (const auto& l_jE : f_ReadJson("data/events/" + l_strN).value("events", json::array())) l_setEvents.insert(l_jE.value("eventId", std::string()));
    std::map<std::string, std::set<std::string>> l_mapClips;   // anim file -> its clip names
    auto l_fnClipOk = [&](const std::string& l_strRef) {
        const size_t l_nH = l_strRef.find('#'); if (l_nH == std::string::npos) return false;
        const std::string l_strFile = l_strRef.substr(0, l_nH);
        if (!l_mapClips.count(l_strFile)) { std::set<std::string>& l_setS = l_mapClips[l_strFile];
            for (const auto& l_jC : f_ReadJson("media/" + l_strFile).value("clips", json::array())) l_setS.insert(l_jC.value("name", std::string())); }
        return l_mapClips[l_strFile].count(l_strRef.substr(l_nH + 1)) > 0;
    };
    auto l_fnCheckLook = [&](const json& l_jLook, const std::string& l_strWhere) {
        if (!l_jLook.is_object()) { CHECK(l_jLook.is_null(), "%s: look is an object", l_strWhere.c_str()); return; }
        if (l_jLook.contains("sprite")) { std::string l_strS = l_jLook["sprite"].get<std::string>(); if (l_strS.size() > 4 && l_strS.compare(l_strS.size() - 4, 4, ".png") == 0) l_strS.resize(l_strS.size() - 4);
            CHECK(l_setSprites.count(l_strS), "%s: sprite %s is in the game atlas", l_strWhere.c_str(), l_strS.c_str()); }
        if (l_jLook.contains("anim")) CHECK(l_jLook["anim"].is_string() && l_fnClipOk(l_jLook["anim"].get<std::string>()), "%s: clip %s exists", l_strWhere.c_str(), l_jLook["anim"].dump().c_str());
        if (l_jLook.contains("tint")) { const json& l_jT = l_jLook["tint"]; bool l_bOk = l_jT.is_array() && (l_jT.size() == 3 || l_jT.size() == 4);
            if (l_bOk) for (const auto& l_jV : l_jT) l_bOk = l_bOk && l_jV.is_number() && l_jV.get<double>() >= 0 && l_jV.get<double>() <= 4;
            CHECK(l_bOk, "%s: tint is [r, g, b] or [r, g, b, a]", l_strWhere.c_str()); }
    };
    // the types' looks
    const json l_jEnemiesJ = f_ReadJson("data/enemies.json"), l_jItemsJ = f_ReadJson("data/items.json");   // named: .items() of a temporary dangles
    for (auto& [l_strId, l_jE] : l_jEnemiesJ.items()) if (l_jE.is_object() && l_jE.contains("look")) l_fnCheckLook(l_jE["look"], "enemies.json " + l_strId);
    for (auto& [l_strId, l_jE] : l_jItemsJ.items()) if (l_jE.is_object() && l_jE.contains("look")) l_fnCheckLook(l_jE["look"], "items.json " + l_strId);
    for (const std::string& l_strN : f_JsonFiles("data/npcs/")) { const json l_jF = f_ReadJson("data/npcs/" + l_strN); if (l_jF.contains("look")) l_fnCheckLook(l_jF["look"], "npcs/" + l_strN); }
    // every stage's per-object settings: on an object's cell, a valid look, real events
    std::vector<std::string> l_vStages;
    for (const std::string& l_strN : f_JsonFiles("data/story/floors/")) if (l_strN.find(".stage.") != std::string::npos) l_vStages.push_back("data/story/floors/" + l_strN);
    for (const std::string& l_strN : f_JsonFiles("data/stages/")) l_vStages.push_back("data/stages/" + l_strN);
    l_vStages.push_back("../tests/stage/objects.stage.json");
    int l_iObjects = 0;
    for (const std::string& l_strPath : l_vStages) {
        const json l_jSt = f_ReadJson(l_strPath);
        if (!l_jSt.is_object() || !l_jSt.contains("objects")) continue;
        const json& l_jLegend = l_jSt["legend"];
        for (auto& [l_strKey, l_jO] : l_jSt["objects"].items()) {
            ++l_iObjects;
            const std::string l_strWhere = l_strPath + " " + l_strKey;
            int l_iX = -1, l_iY = -1; std::sscanf(l_strKey.c_str(), "%d,%d", &l_iX, &l_iY);
            const bool l_bIn = l_iY >= 0 && l_iY < (int)l_jSt["tiles"].size() && l_iX >= 0 && l_iX < (int)l_jSt["tiles"][l_iY].get<std::string>().size();
            CHECK(l_bIn, "%s: the cell is on the map", l_strWhere.c_str());
            if (l_bIn) { const std::string l_strC(1, l_jSt["tiles"][l_iY].get<std::string>()[l_iX]);
                CHECK(l_strC != "." && l_strC != "#" && l_jLegend.contains(l_strC), "%s: an object stands on the cell ('%s')", l_strWhere.c_str(), l_strC.c_str()); }
            l_fnCheckLook(l_jO.value("look", json()), l_strWhere);
            for (const auto& l_jEv : l_jO.value("events", json::array())) CHECK(l_setEvents.count(l_jEv.get<std::string>()), "%s: event %s exists", l_strWhere.c_str(), l_jEv.get<std::string>().c_str());
        }
    }
    CHECK(l_iObjects >= 4, "the test stage's objects are checked (%d)", l_iObjects);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        f_TestChatLines();
        f_TestTopics();
        f_TestConditionsAndStart();
        f_TestActions();
        f_TestSave();
        f_TestData();
        f_TestObjects();
    } catch (const std::exception& l_oE) {   // bad data must fail the test with a reason, not abort it
        std::printf("  FAIL: exception: %s\n", l_oE.what());
        ++g_iFailed;
    }
    std::printf("talk_system_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed ? 1 : 0;
}
