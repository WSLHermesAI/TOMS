// ending_system_test.cpp — headless verification of the 15-ending resolver (M7, first slice).
// Exits 0 on success, 1 on any CHECK failure. Run: ./ending_system_test
#include "ending_system.h"
#include <cstdio>
#include <map>
#include <set>

using namespace toms;

static int g_iChecks = 0, g_iFails = 0;
#define CHECK(cond, msg) do { ++g_iChecks; if (!(cond)) { ++g_iFails; printf("  FAIL: %s\n", msg); } } while (0)

// Minimal mock, matching condition_eval_test.cpp's own shape -- this file only needs to prove
// the RESOLVER (judgeEnding/checkNamedEndings), not re-litigate the leaves condition_eval_test
// already covers exhaustively.
namespace {   // test-local: toms_tests links every test into one program
struct MockContext : ConditionContext {
    std::set<std::string> m_setFlags;
    bool f_StoryFlagSet(const std::string& l_strF) const override { return m_setFlags.count(l_strF) != 0; }
    int f_StoryBeat() const override { return 0; }
    std::map<std::string, int> m_mapItems;
    bool f_ItemHeld(const std::string& l_strId, int l_iCount) const override {
        auto l_itIt = m_mapItems.find(l_strId); return l_itIt != m_mapItems.end() && l_itIt->second >= l_iCount;
    }
    int f_StatValue(const std::string&) const override { return 0; }
    bool f_MissionComplete(const std::string&) const override { return false; }
    bool f_MissionActive(const std::string&) const override { return false; }
    bool f_StageCleared(const std::string&) const override { return false; }
    std::map<std::string, int> m_mapCounters;
    int f_Counter(const std::string& l_strName) const override {
        auto l_itIt = m_mapCounters.find(l_strName); return l_itIt == m_mapCounters.end() ? 0 : l_itIt->second;
    }
    int m_iCycle = 1;
    int f_CycleIndex() const override { return m_iCycle; }
    int m_iShards = 99, m_iDeaths = 0;
    int f_MemoryShardCount() const override { return m_iShards; }
    int f_DeathsNonBoss() const override { return m_iDeaths; }
};
}  // namespace

int main() {
    // 1. EndingDefinition JSON round-trip.
    {
        EndingDefinition l_oD;
        l_oD.m_strId = "e_test"; l_oD.m_strNameKey = "story.ending.test.name"; l_oD.m_strTextKey = "story.ending.test.text";
        l_oD.m_strTone = "bad"; l_oD.m_bAllowsRebirth = true;
        l_oD.m_jRequires = nlohmann::json{{"type", "always"}};
        auto l_jJ = f_ToJson(l_oD);
        EndingDefinition l_oD2 = f_EndingFromJson(l_jJ);
        CHECK(l_oD2.m_strId == "e_test" && l_oD2.m_strNameKey == "story.ending.test.name", "round-trip: id/name key");
        CHECK(l_oD2.m_bAllowsRebirth, "round-trip: allowsRebirth");
    }
    CHECK(f_EndingFromJson(nlohmann::json(nullptr)).m_strId.empty(), "endingFromJson(null) returns a default, not a throw");

    // 2. loadEndings against the REAL data/story/endings.json (run from the project root).
    EndingsTable l_oTable = f_LoadEndings("data/story/endings.json");
    CHECK(l_oTable.m_vEndings.size() == 15, "data/story/endings.json has all 15 authored endings");
    CHECK(l_oTable.m_strFallback == "e_05", "the fallback is e_05, matching STORY_BIBLE.md's neutral ending");
    CHECK(f_LoadEndings("data/does_not_exist.json").m_vEndings.empty(), "a missing file loads to an empty table, not a crash");

    // 3. judgeEnding: first-match-wins down the real content, falling back to e_05 when nothing matches.
    {
        // A context engineered so that NONE of the 15 real conditions match (including e_08's and
        // e_14's, which are otherwise the easiest to satisfy by omission) -- the only way to
        // actually exercise the guaranteed-fallback path rather than a real ending's own condition.
        MockContext l_oCtx;
        l_oCtx.m_mapItems = {{"key_red", 1}};                 // defeats e_08's "no key_red" gate
        l_oCtx.m_iShards = 99;                               // defeats e_07's "memoryShards < 5" gate
        l_oCtx.m_setFlags = {"flag_motive_know_self"};          // defeats e_14's "no main-line choice made" gate
        const EndingDefinition* l_pR = f_JudgeEnding(l_oTable, l_oCtx);
        CHECK(l_pR && l_pR->m_strId == "e_05", "a context where all 15 real conditions fail resolves to the guaranteed fallback (e_05)");
    }
    {
        MockContext l_oCtx;
        l_oCtx.m_setFlags = {"flag_blow_stay"};
        l_oCtx.m_mapCounters = {{"humanity", 5}};
        const EndingDefinition* l_pR = f_JudgeEnding(l_oTable, l_oCtx);
        CHECK(l_pR && l_pR->m_strId == "e_05", "e_05's own real condition also resolves to e_05 (not just the fallback path)");
    }
    {
        // Default mock (shards=99, no key_red, ss_10 incomplete, e_05's own gate unmet): e_07's
        // "memoryShards < 5" fails (99 >= 5), so e_08 ("no key_red, ss_10 incomplete") wins next.
        MockContext l_oCtx;
        const EndingDefinition* l_pR = f_JudgeEnding(l_oTable, l_oCtx);
        CHECK(l_pR && l_pR->m_strId == "e_08", "e_07 fails on plentiful shards; no key_red + ss_10 incomplete -> e_08 wins before the fallback");
    }
    {
        // Holding key_red clears e_08's own gate; with shards now low, e_07 (checked earlier in
        // array order than e_08) wins instead.
        MockContext l_oCtx;
        l_oCtx.m_mapItems = {{"key_red", 1}};
        l_oCtx.m_iShards = 3;
        const EndingDefinition* l_pR = f_JudgeEnding(l_oTable, l_oCtx);
        CHECK(l_pR && l_pR->m_strId == "e_07", "holding key_red clears e_08; low memory shards resolves to e_07 next");
    }

    // 4. checkNamedEndings: the non-F70 trigger rows (a wipe) -- NEVER falls back, and never
    //    matches an id outside the given order even if that id's own condition would be true.
    {
        MockContext l_oCtx;
        l_oCtx.m_iDeaths = 12;
        const EndingDefinition* l_pR = f_CheckNamedEndings(l_oTable, {"e_10", "e_13"}, l_oCtx);
        CHECK(l_pR && l_pR->m_strId == "e_13", "12 non-boss deaths matches e_13 via the wipe-only check");
    }
    {
        MockContext l_oCtx;   // nothing matches either e_10 or e_13
        const EndingDefinition* l_pR = f_CheckNamedEndings(l_oTable, {"e_10", "e_13"}, l_oCtx);
        CHECK(l_pR == nullptr, "an ordinary death (no special condition met) resolves to nullptr, never a forced fallback");
    }
    {
        MockContext l_oCtx;
        l_oCtx.m_setFlags = {"flag_blow_stay"};
        l_oCtx.m_mapCounters = {{"humanity", 5}};   // this WOULD satisfy e_05, but e_05 isn't in the order
        const EndingDefinition* l_pR = f_CheckNamedEndings(l_oTable, {"e_10", "e_13"}, l_oCtx);
        CHECK(l_pR == nullptr, "checkNamedEndings never matches an id outside its own order, even if that id's condition holds");
    }
    {
        MockContext l_oCtx;
        l_oCtx.m_iCycle = 9;
        const EndingDefinition* l_pR = f_CheckNamedEndings(l_oTable, {"e_10", "e_13"}, l_oCtx);
        CHECK(l_pR && l_pR->m_strId == "e_10", "cycleIndex 9 matches e_10 (checked before e_13, matching the order given)");
    }

    if (g_iFails == 0) { printf("ending_system_test: ALL PASS (%d checks)\n", g_iChecks); return 0; }
    printf("ending_system_test: %d/%d CHECK(s) FAILED\n", g_iFails, g_iChecks);
    return 1;
}
