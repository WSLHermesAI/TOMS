// condition_eval_test.cpp — headless verification of the Condition/Flag Evaluator (Milestone 3).
// Exits 0 on success, 1 on any CHECK failure. Run: ./condition_eval_test
#include "condition.h"
#include <cstdio>
#include <map>
#include <set>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

namespace {   // test-local: toms_tests links every test into one program
struct MockContext : ConditionContext {
    std::set<std::string> m_setFlags;
    int m_iBeat = 0;
    std::map<std::string, int> m_mapItems;
    std::map<std::string, int> m_mapStats;
    std::set<std::string> m_setMissionsComplete, m_setMissionsActive, m_setStagesCleared;

    bool f_StoryFlagSet(const std::string& l_strF) const override { return m_setFlags.count(l_strF) != 0; }
    int  f_StoryBeat() const override { return m_iBeat; }
    bool f_ItemHeld(const std::string& l_strId, int l_iCount) const override {
        auto l_itIt = m_mapItems.find(l_strId); return l_itIt != m_mapItems.end() && l_itIt->second >= l_iCount;
    }
    int f_StatValue(const std::string& l_strS) const override {
        auto l_itIt = m_mapStats.find(l_strS); return l_itIt == m_mapStats.end() ? 0 : l_itIt->second;
    }
    bool f_MissionComplete(const std::string& l_strId) const override { return m_setMissionsComplete.count(l_strId) != 0; }
    bool f_MissionActive(const std::string& l_strId) const override { return m_setMissionsActive.count(l_strId) != 0; }
    bool f_StageCleared(const std::string& l_strId) const override { return m_setStagesCleared.count(l_strId) != 0; }

    // ---- S3 leaves (docs/story/STORY_DATA_SCHEMA.md section 2.2) ----
    std::map<std::string, std::string> m_mapChoices;        // choiceId -> optionId
    std::map<std::string, std::string> m_mapSideStories;    // sideStoryId -> state
    std::map<std::string, int> m_mapCounters;
    int m_iCycle = 1;
    bool f_ChoiceMade(const std::string& l_strC, const std::string& l_strO) const override {
        auto l_itIt = m_mapChoices.find(l_strC); return l_itIt != m_mapChoices.end() && l_itIt->second == l_strO;
    }
    std::string f_SideStoryState(const std::string& l_strId) const override {
        auto l_itIt = m_mapSideStories.find(l_strId); return l_itIt == m_mapSideStories.end() ? std::string() : l_itIt->second;
    }
    int f_Counter(const std::string& l_strName) const override {
        auto l_itIt = m_mapCounters.find(l_strName); return l_itIt == m_mapCounters.end() ? 0 : l_itIt->second;
    }
    // M9: a separate set from `flags` above -- storyFlagSet reads META flags, runFlagSet reads
    // RUN flags, and they are genuinely different namespaces in the real game (game_condition.h).
    std::set<std::string> m_setRunFlags;
    bool f_RunFlagSet(const std::string& l_strF) const override { return m_setRunFlags.count(l_strF) != 0; }
    int f_CycleIndex() const override { return m_iCycle; }
    // M7
    int m_iShards = 0;
    int f_MemoryShardCount() const override { return m_iShards; }
    int m_iDeaths = 0;
    int f_DeathsNonBoss() const override { return m_iDeaths; }
};
}  // namespace

int main() {
    MockContext l_oCtx;
    l_oCtx.m_iBeat = 4;
    l_oCtx.m_setFlags = {"beat_04_scholar_hint_given"};
    l_oCtx.m_mapItems = {{"key_blue", 1}, {"gem_atk", 3}};
    l_oCtx.m_mapStats = {{"atk", 20}, {"def", 5}};
    l_oCtx.m_setStagesCleared = {"stage_03"};
    l_oCtx.m_setMissionsComplete = {"m_intro_once"};

    CHECK(f_Evaluate(nlohmann::json(), l_oCtx), "null condition is always true");

    // ---- S3: the four new leaf types (docs/story/STORY_DATA_SCHEMA.md section 2.2) ----
    l_oCtx.m_mapChoices = {{"c_motive", "opt_know_self"}};
    l_oCtx.m_mapSideStories = {{"ss_08", "completed"}};
    l_oCtx.m_mapCounters = {{"insight", 6}, {"resolve", -3}};
    l_oCtx.m_iCycle = 2;
    CHECK(f_Evaluate({{"type","choiceMade"},{"choiceId","c_motive"},{"optionId","opt_know_self"}}, l_oCtx),
          "choiceMade: the taken option is true");
    CHECK(!f_Evaluate({{"type","choiceMade"},{"choiceId","c_motive"},{"optionId","opt_avenge"}}, l_oCtx),
          "choiceMade: an option not taken is false");
    CHECK(!f_Evaluate({{"type","choiceMade"},{"choiceId","c_never"},{"optionId","opt_x"}}, l_oCtx),
          "choiceMade: an unmade choice is false");
    CHECK(f_Evaluate({{"type","sideStoryState"},{"sideStoryId","ss_08"},{"state","completed"}}, l_oCtx),
          "sideStoryState: matching state is true");
    CHECK(!f_Evaluate({{"type","sideStoryState"},{"sideStoryId","ss_08"},{"state","failed"}}, l_oCtx),
          "sideStoryState: a different state is false");
    CHECK(!f_Evaluate({{"type","sideStoryState"},{"sideStoryId","ss_09"},{"state","completed"}}, l_oCtx),
          "sideStoryState: an unknown side story is false");
    CHECK(f_Evaluate({{"type","counterAtLeast"},{"counter","insight"},{"min",6}}, l_oCtx),
          "counterAtLeast: at the threshold");
    CHECK(!f_Evaluate({{"type","counterAtLeast"},{"counter","insight"},{"min",7}}, l_oCtx),
          "counterAtLeast: below the threshold");
    CHECK(!f_Evaluate({{"type","counterAtLeast"},{"counter","resolve"},{"min",0}}, l_oCtx),
          "counterAtLeast: a negative counter fails a positive floor");
    CHECK(f_Evaluate({{"type","cycleIndexAtLeast"},{"min",2}}, l_oCtx),
          "cycleIndexAtLeast: second cycle passes min 2");
    CHECK(!f_Evaluate({{"type","cycleIndexAtLeast"},{"min",3}}, l_oCtx),
          "cycleIndexAtLeast: second cycle fails min 3");
    // combinators and fail-closed behaviour are unchanged by the additions
    CHECK(f_Evaluate({{"all", nlohmann::json::array({{{"type","choiceMade"},{"choiceId","c_motive"},{"optionId","opt_know_self"}},
                                                   {{"type","counterAtLeast"},{"counter","insight"},{"min",6}}})}}, l_oCtx),
          "the new leaves compose with all()");
    CHECK(f_Evaluate({{"not", {{"type","cycleIndexAtLeast"},{"min",3}}}}, l_oCtx),
          "the new leaves compose with not()");

    CHECK(f_Evaluate({{"type","storyBeatAtLeast"},{"value",4}}, l_oCtx), "storyBeatAtLeast: 4>=4 true");
    CHECK(!f_Evaluate({{"type","storyBeatAtLeast"},{"value",5}}, l_oCtx), "storyBeatAtLeast: 4>=5 false");
    CHECK(f_Evaluate({{"type","storyFlagSet"},{"flag","beat_04_scholar_hint_given"}}, l_oCtx), "storyFlagSet: set flag true");
    CHECK(!f_Evaluate({{"type","storyFlagSet"},{"flag","never_set"}}, l_oCtx), "storyFlagSet: unset flag false");
    l_oCtx.m_setRunFlags.insert("event_ev_library_torn_page");
    CHECK(f_Evaluate({{"type","runFlagSet"},{"flag","event_ev_library_torn_page"}}, l_oCtx), "runFlagSet: set run flag true");
    CHECK(!f_Evaluate({{"type","runFlagSet"},{"flag","beat_04_scholar_hint_given"}}, l_oCtx),
          "runFlagSet: a META flag of the same kind of name is NOT a run flag -- separate namespaces");
    CHECK(f_Evaluate({{"type","itemHeld"},{"itemId","key_blue"},{"count",1}}, l_oCtx), "itemHeld: held true");
    CHECK(!f_Evaluate({{"type","itemHeld"},{"itemId","key_red"},{"count",1}}, l_oCtx), "itemHeld: not held false");
    CHECK(f_Evaluate({{"type","itemHeld"},{"itemId","gem_atk"},{"count",3}}, l_oCtx), "itemHeld: exact count true");
    CHECK(!f_Evaluate({{"type","itemHeld"},{"itemId","gem_atk"},{"count",4}}, l_oCtx), "itemHeld: insufficient count false");
    CHECK(f_Evaluate({{"type","statAtLeast"},{"stat","atk"},{"value",20}}, l_oCtx), "statAtLeast: 20>=20 true");
    CHECK(!f_Evaluate({{"type","statAtLeast"},{"stat","atk"},{"value",21}}, l_oCtx), "statAtLeast: 20>=21 false");
    CHECK(f_Evaluate({{"type","missionComplete"},{"missionId","m_intro_once"}}, l_oCtx), "missionComplete: complete true");
    CHECK(!f_Evaluate({{"type","missionComplete"},{"missionId","m_other"}}, l_oCtx), "missionComplete: not complete false");
    CHECK(f_Evaluate({{"type","stageCleared"},{"stageId","stage_03"}}, l_oCtx), "stageCleared: cleared true");
    CHECK(!f_Evaluate({{"type","stageCleared"},{"stageId","stage_09"}}, l_oCtx), "stageCleared: not cleared false");

    nlohmann::json l_jAllExpr = { {"all", nlohmann::json::array({
        nlohmann::json{{"type","storyBeatAtLeast"},{"value",4}},
        nlohmann::json{{"type","itemHeld"},{"itemId","key_blue"},{"count",1}}
    })} };
    CHECK(f_Evaluate(l_jAllExpr, l_oCtx), "all: both sub-conditions true -> true");

    nlohmann::json l_jAllExprFail = { {"all", nlohmann::json::array({
        nlohmann::json{{"type","storyBeatAtLeast"},{"value",4}},
        nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}}
    })} };
    CHECK(!f_Evaluate(l_jAllExprFail, l_oCtx), "all: one sub-condition false -> false");

    nlohmann::json l_jAnyExpr = { {"any", nlohmann::json::array({
        nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}},
        nlohmann::json{{"type","statAtLeast"},{"stat","atk"},{"value",20}}
    })} };
    CHECK(f_Evaluate(l_jAnyExpr, l_oCtx), "any: one sub-condition true -> true");

    nlohmann::json l_jAnyExprFail = { {"any", nlohmann::json::array({
        nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}},
        nlohmann::json{{"type","statAtLeast"},{"stat","atk"},{"value",99}}
    })} };
    CHECK(!f_Evaluate(l_jAnyExprFail, l_oCtx), "any: all sub-conditions false -> false");

    nlohmann::json l_jNotExpr = { {"not", nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}}} };
    CHECK(f_Evaluate(l_jNotExpr, l_oCtx), "not: negates a false leaf -> true");

    // Nested combinators, matching the architecture-doc §9.1 worked-example shape.
    nlohmann::json l_jNested = { {"all", nlohmann::json::array({
        nlohmann::json{{"type","storyBeatAtLeast"},{"value",4}},
        nlohmann::json{{"type","itemHeld"},{"itemId","key_blue"},{"count",1}},
        nlohmann::json{{"any", nlohmann::json::array({
            nlohmann::json{{"type","missionComplete"},{"missionId","m_intro_once"}},
            nlohmann::json{{"type","statAtLeast"},{"stat","atk"},{"value",99}}
        })}}
    })} };
    CHECK(f_Evaluate(l_jNested, l_oCtx), "nested all/any evaluates correctly");

    CHECK(!f_Evaluate(nlohmann::json{{"type","somethingMadeUp"}}, l_oCtx), "unknown leaf type fails closed");
    CHECK(!f_Evaluate(nlohmann::json{{"all", "not-an-array"}}, l_oCtx), "malformed 'all' (not an array) fails closed");
    CHECK(!f_Evaluate(nlohmann::json::array({1, 2, 3}), l_oCtx), "a bare JSON array (not an object) fails closed");

    // M7: "always" -- the endings resolver's guaranteed-fallback leaf.
    CHECK(f_Evaluate(nlohmann::json{{"type","always"}}, l_oCtx), "'always' is unconditionally true");

    // M7: "memoryShards" -- e_07's own condition.
    l_oCtx.m_iShards = 4;
    CHECK(!f_Evaluate(nlohmann::json{{"type","memoryShards"},{"min",5}}, l_oCtx), "4 shards does not satisfy a min of 5");
    l_oCtx.m_iShards = 5;
    CHECK(f_Evaluate(nlohmann::json{{"type","memoryShards"},{"min",5}}, l_oCtx), "5 shards satisfies a min of 5");

    // M7: "deathsNonBoss" -- e_13's own condition, a dedicated field, not a generic named counter.
    l_oCtx.m_iDeaths = 11;
    CHECK(!f_Evaluate(nlohmann::json{{"type","deathsNonBoss"},{"min",12}}, l_oCtx), "11 non-boss deaths does not satisfy a min of 12");
    l_oCtx.m_iDeaths = 12;
    CHECK(f_Evaluate(nlohmann::json{{"type","deathsNonBoss"},{"min",12}}, l_oCtx), "12 non-boss deaths satisfies a min of 12");

    if (g_iFail == 0) { printf("condition_eval_test: ALL PASS (29 checks)\n"); return 0; }
    printf("condition_eval_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
