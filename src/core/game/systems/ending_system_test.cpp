// ending_system_test.cpp — headless verification of the 15-ending resolver (M7, first slice).
// Exits 0 on success, 1 on any CHECK failure. Run: ./ending_system_test
#include "ending_system.h"
#include <cstdio>
#include <map>
#include <set>

using namespace toms;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fails; printf("  FAIL: %s\n", msg); } } while (0)

// Minimal mock, matching condition_eval_test.cpp's own shape -- this file only needs to prove
// the RESOLVER (judgeEnding/checkNamedEndings), not re-litigate the leaves condition_eval_test
// already covers exhaustively.
struct MockContext : ConditionContext {
    std::set<std::string> flags;
    bool storyFlagSet(const std::string& f) const override { return flags.count(f) != 0; }
    int storyBeat() const override { return 0; }
    std::map<std::string, int> items;
    bool itemHeld(const std::string& id, int count) const override {
        auto it = items.find(id); return it != items.end() && it->second >= count;
    }
    int statValue(const std::string&) const override { return 0; }
    bool missionComplete(const std::string&) const override { return false; }
    bool missionActive(const std::string&) const override { return false; }
    bool stageCleared(const std::string&) const override { return false; }
    std::map<std::string, int> counters;
    int counter(const std::string& name) const override {
        auto it = counters.find(name); return it == counters.end() ? 0 : it->second;
    }
    int cycle = 1;
    int cycleIndex() const override { return cycle; }
    int shards = 99, deaths = 0;
    int memoryShardCount() const override { return shards; }
    int deathsNonBoss() const override { return deaths; }
};

int main() {
    // 1. EndingDefinition JSON round-trip.
    {
        EndingDefinition d;
        d.id = "e_test"; d.nameKey = "story.ending.test.name"; d.textKey = "story.ending.test.text";
        d.tone = "bad"; d.allowsRebirth = true;
        d.requires_ = nlohmann::json{{"type", "always"}};
        auto j = toJson(d);
        EndingDefinition d2 = endingFromJson(j);
        CHECK(d2.id == "e_test" && d2.nameKey == "story.ending.test.name", "round-trip: id/name key");
        CHECK(d2.allowsRebirth, "round-trip: allowsRebirth");
    }
    CHECK(endingFromJson(nlohmann::json(nullptr)).id.empty(), "endingFromJson(null) returns a default, not a throw");

    // 2. loadEndings against the REAL data/story/endings.json (run from the project root).
    EndingsTable table = loadEndings("data/story/endings.json");
    CHECK(table.endings.size() == 15, "data/story/endings.json has all 15 authored endings");
    CHECK(table.fallback == "e_05", "the fallback is e_05, matching STORY_BIBLE.md's neutral ending");
    CHECK(loadEndings("data/does_not_exist.json").endings.empty(), "a missing file loads to an empty table, not a crash");

    // 3. judgeEnding: first-match-wins down the real content, falling back to e_05 when nothing matches.
    {
        // A context engineered so that NONE of the 15 real conditions match (including e_08's and
        // e_14's, which are otherwise the easiest to satisfy by omission) -- the only way to
        // actually exercise the guaranteed-fallback path rather than a real ending's own condition.
        MockContext ctx;
        ctx.items = {{"key_red", 1}};                 // defeats e_08's "no key_red" gate
        ctx.shards = 99;                               // defeats e_07's "memoryShards < 5" gate
        ctx.flags = {"flag_motive_know_self"};          // defeats e_14's "no main-line choice made" gate
        const EndingDefinition* r = judgeEnding(table, ctx);
        CHECK(r && r->id == "e_05", "a context where all 15 real conditions fail resolves to the guaranteed fallback (e_05)");
    }
    {
        MockContext ctx;
        ctx.flags = {"flag_blow_stay"};
        ctx.counters = {{"humanity", 5}};
        const EndingDefinition* r = judgeEnding(table, ctx);
        CHECK(r && r->id == "e_05", "e_05's own real condition also resolves to e_05 (not just the fallback path)");
    }
    {
        // Default mock (shards=99, no key_red, ss_10 incomplete, e_05's own gate unmet): e_07's
        // "memoryShards < 5" fails (99 >= 5), so e_08 ("no key_red, ss_10 incomplete") wins next.
        MockContext ctx;
        const EndingDefinition* r = judgeEnding(table, ctx);
        CHECK(r && r->id == "e_08", "e_07 fails on plentiful shards; no key_red + ss_10 incomplete -> e_08 wins before the fallback");
    }
    {
        // Holding key_red clears e_08's own gate; with shards now low, e_07 (checked earlier in
        // array order than e_08) wins instead.
        MockContext ctx;
        ctx.items = {{"key_red", 1}};
        ctx.shards = 3;
        const EndingDefinition* r = judgeEnding(table, ctx);
        CHECK(r && r->id == "e_07", "holding key_red clears e_08; low memory shards resolves to e_07 next");
    }

    // 4. checkNamedEndings: the non-F70 trigger rows (a wipe) -- NEVER falls back, and never
    //    matches an id outside the given order even if that id's own condition would be true.
    {
        MockContext ctx;
        ctx.deaths = 12;
        const EndingDefinition* r = checkNamedEndings(table, {"e_10", "e_13"}, ctx);
        CHECK(r && r->id == "e_13", "12 non-boss deaths matches e_13 via the wipe-only check");
    }
    {
        MockContext ctx;   // nothing matches either e_10 or e_13
        const EndingDefinition* r = checkNamedEndings(table, {"e_10", "e_13"}, ctx);
        CHECK(r == nullptr, "an ordinary death (no special condition met) resolves to nullptr, never a forced fallback");
    }
    {
        MockContext ctx;
        ctx.flags = {"flag_blow_stay"};
        ctx.counters = {{"humanity", 5}};   // this WOULD satisfy e_05, but e_05 isn't in the order
        const EndingDefinition* r = checkNamedEndings(table, {"e_10", "e_13"}, ctx);
        CHECK(r == nullptr, "checkNamedEndings never matches an id outside its own order, even if that id's condition holds");
    }
    {
        MockContext ctx;
        ctx.cycle = 9;
        const EndingDefinition* r = checkNamedEndings(table, {"e_10", "e_13"}, ctx);
        CHECK(r && r->id == "e_10", "cycleIndex 9 matches e_10 (checked before e_13, matching the order given)");
    }

    if (g_fails == 0) { printf("ending_system_test: ALL PASS (%d checks)\n", g_checks); return 0; }
    printf("ending_system_test: %d/%d CHECK(s) FAILED\n", g_fails, g_checks);
    return 1;
}
