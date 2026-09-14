// condition_eval_test.cpp — headless verification of the Condition/Flag Evaluator (Milestone 3).
// Exits 0 on success, 1 on any CHECK failure. Run: ./condition_eval_test
#include "condition.h"
#include <cstdio>
#include <map>
#include <set>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

struct MockContext : ConditionContext {
    std::set<std::string> flags;
    int beat = 0;
    std::map<std::string, int> items;
    std::map<std::string, int> stats;
    std::set<std::string> missionsComplete, missionsActive, stagesCleared;

    bool storyFlagSet(const std::string& f) const override { return flags.count(f) != 0; }
    int  storyBeat() const override { return beat; }
    bool itemHeld(const std::string& id, int count) const override {
        auto it = items.find(id); return it != items.end() && it->second >= count;
    }
    int statValue(const std::string& s) const override {
        auto it = stats.find(s); return it == stats.end() ? 0 : it->second;
    }
    bool missionComplete(const std::string& id) const override { return missionsComplete.count(id) != 0; }
    bool missionActive(const std::string& id) const override { return missionsActive.count(id) != 0; }
    bool stageCleared(const std::string& id) const override { return stagesCleared.count(id) != 0; }

    // ---- S3 leaves (docs/story/STORY_DATA_SCHEMA.md section 2.2) ----
    std::map<std::string, std::string> choices;        // choiceId -> optionId
    std::map<std::string, std::string> sideStories;    // sideStoryId -> state
    std::map<std::string, int> counters;
    int cycle = 1;
    bool choiceMade(const std::string& c, const std::string& o) const override {
        auto it = choices.find(c); return it != choices.end() && it->second == o;
    }
    std::string sideStoryState(const std::string& id) const override {
        auto it = sideStories.find(id); return it == sideStories.end() ? std::string() : it->second;
    }
    int counter(const std::string& name) const override {
        auto it = counters.find(name); return it == counters.end() ? 0 : it->second;
    }
    int cycleIndex() const override { return cycle; }
};

int main() {
    MockContext ctx;
    ctx.beat = 4;
    ctx.flags = {"beat_04_scholar_hint_given"};
    ctx.items = {{"key_blue", 1}, {"gem_atk", 3}};
    ctx.stats = {{"atk", 20}, {"def", 5}};
    ctx.stagesCleared = {"stage_03"};
    ctx.missionsComplete = {"m_intro_once"};

    CHECK(evaluate(nlohmann::json(), ctx), "null condition is always true");

    // ---- S3: the four new leaf types (docs/story/STORY_DATA_SCHEMA.md section 2.2) ----
    ctx.choices = {{"c_motive", "opt_know_self"}};
    ctx.sideStories = {{"ss_08", "completed"}};
    ctx.counters = {{"insight", 6}, {"resolve", -3}};
    ctx.cycle = 2;
    CHECK(evaluate({{"type","choiceMade"},{"choiceId","c_motive"},{"optionId","opt_know_self"}}, ctx),
          "choiceMade: the taken option is true");
    CHECK(!evaluate({{"type","choiceMade"},{"choiceId","c_motive"},{"optionId","opt_avenge"}}, ctx),
          "choiceMade: an option not taken is false");
    CHECK(!evaluate({{"type","choiceMade"},{"choiceId","c_never"},{"optionId","opt_x"}}, ctx),
          "choiceMade: an unmade choice is false");
    CHECK(evaluate({{"type","sideStoryState"},{"sideStoryId","ss_08"},{"state","completed"}}, ctx),
          "sideStoryState: matching state is true");
    CHECK(!evaluate({{"type","sideStoryState"},{"sideStoryId","ss_08"},{"state","failed"}}, ctx),
          "sideStoryState: a different state is false");
    CHECK(!evaluate({{"type","sideStoryState"},{"sideStoryId","ss_09"},{"state","completed"}}, ctx),
          "sideStoryState: an unknown side story is false");
    CHECK(evaluate({{"type","counterAtLeast"},{"counter","insight"},{"min",6}}, ctx),
          "counterAtLeast: at the threshold");
    CHECK(!evaluate({{"type","counterAtLeast"},{"counter","insight"},{"min",7}}, ctx),
          "counterAtLeast: below the threshold");
    CHECK(!evaluate({{"type","counterAtLeast"},{"counter","resolve"},{"min",0}}, ctx),
          "counterAtLeast: a negative counter fails a positive floor");
    CHECK(evaluate({{"type","cycleIndexAtLeast"},{"min",2}}, ctx),
          "cycleIndexAtLeast: second cycle passes min 2");
    CHECK(!evaluate({{"type","cycleIndexAtLeast"},{"min",3}}, ctx),
          "cycleIndexAtLeast: second cycle fails min 3");
    // combinators and fail-closed behaviour are unchanged by the additions
    CHECK(evaluate({{"all", nlohmann::json::array({{{"type","choiceMade"},{"choiceId","c_motive"},{"optionId","opt_know_self"}},
                                                   {{"type","counterAtLeast"},{"counter","insight"},{"min",6}}})}}, ctx),
          "the new leaves compose with all()");
    CHECK(evaluate({{"not", {{"type","cycleIndexAtLeast"},{"min",3}}}}, ctx),
          "the new leaves compose with not()");

    CHECK(evaluate({{"type","storyBeatAtLeast"},{"value",4}}, ctx), "storyBeatAtLeast: 4>=4 true");
    CHECK(!evaluate({{"type","storyBeatAtLeast"},{"value",5}}, ctx), "storyBeatAtLeast: 4>=5 false");
    CHECK(evaluate({{"type","storyFlagSet"},{"flag","beat_04_scholar_hint_given"}}, ctx), "storyFlagSet: set flag true");
    CHECK(!evaluate({{"type","storyFlagSet"},{"flag","never_set"}}, ctx), "storyFlagSet: unset flag false");
    CHECK(evaluate({{"type","itemHeld"},{"itemId","key_blue"},{"count",1}}, ctx), "itemHeld: held true");
    CHECK(!evaluate({{"type","itemHeld"},{"itemId","key_red"},{"count",1}}, ctx), "itemHeld: not held false");
    CHECK(evaluate({{"type","itemHeld"},{"itemId","gem_atk"},{"count",3}}, ctx), "itemHeld: exact count true");
    CHECK(!evaluate({{"type","itemHeld"},{"itemId","gem_atk"},{"count",4}}, ctx), "itemHeld: insufficient count false");
    CHECK(evaluate({{"type","statAtLeast"},{"stat","atk"},{"value",20}}, ctx), "statAtLeast: 20>=20 true");
    CHECK(!evaluate({{"type","statAtLeast"},{"stat","atk"},{"value",21}}, ctx), "statAtLeast: 20>=21 false");
    CHECK(evaluate({{"type","missionComplete"},{"missionId","m_intro_once"}}, ctx), "missionComplete: complete true");
    CHECK(!evaluate({{"type","missionComplete"},{"missionId","m_other"}}, ctx), "missionComplete: not complete false");
    CHECK(evaluate({{"type","stageCleared"},{"stageId","stage_03"}}, ctx), "stageCleared: cleared true");
    CHECK(!evaluate({{"type","stageCleared"},{"stageId","stage_09"}}, ctx), "stageCleared: not cleared false");

    nlohmann::json allExpr = { {"all", nlohmann::json::array({
        nlohmann::json{{"type","storyBeatAtLeast"},{"value",4}},
        nlohmann::json{{"type","itemHeld"},{"itemId","key_blue"},{"count",1}}
    })} };
    CHECK(evaluate(allExpr, ctx), "all: both sub-conditions true -> true");

    nlohmann::json allExprFail = { {"all", nlohmann::json::array({
        nlohmann::json{{"type","storyBeatAtLeast"},{"value",4}},
        nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}}
    })} };
    CHECK(!evaluate(allExprFail, ctx), "all: one sub-condition false -> false");

    nlohmann::json anyExpr = { {"any", nlohmann::json::array({
        nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}},
        nlohmann::json{{"type","statAtLeast"},{"stat","atk"},{"value",20}}
    })} };
    CHECK(evaluate(anyExpr, ctx), "any: one sub-condition true -> true");

    nlohmann::json anyExprFail = { {"any", nlohmann::json::array({
        nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}},
        nlohmann::json{{"type","statAtLeast"},{"stat","atk"},{"value",99}}
    })} };
    CHECK(!evaluate(anyExprFail, ctx), "any: all sub-conditions false -> false");

    nlohmann::json notExpr = { {"not", nlohmann::json{{"type","itemHeld"},{"itemId","key_red"},{"count",1}}} };
    CHECK(evaluate(notExpr, ctx), "not: negates a false leaf -> true");

    // Nested combinators, matching the architecture-doc §9.1 worked-example shape.
    nlohmann::json nested = { {"all", nlohmann::json::array({
        nlohmann::json{{"type","storyBeatAtLeast"},{"value",4}},
        nlohmann::json{{"type","itemHeld"},{"itemId","key_blue"},{"count",1}},
        nlohmann::json{{"any", nlohmann::json::array({
            nlohmann::json{{"type","missionComplete"},{"missionId","m_intro_once"}},
            nlohmann::json{{"type","statAtLeast"},{"stat","atk"},{"value",99}}
        })}}
    })} };
    CHECK(evaluate(nested, ctx), "nested all/any evaluates correctly");

    CHECK(!evaluate(nlohmann::json{{"type","somethingMadeUp"}}, ctx), "unknown leaf type fails closed");
    CHECK(!evaluate(nlohmann::json{{"all", "not-an-array"}}, ctx), "malformed 'all' (not an array) fails closed");
    CHECK(!evaluate(nlohmann::json::array({1, 2, 3}), ctx), "a bare JSON array (not an object) fails closed");

    if (g_fail == 0) { printf("condition_eval_test: ALL PASS (24 checks)\n"); return 0; }
    printf("condition_eval_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
