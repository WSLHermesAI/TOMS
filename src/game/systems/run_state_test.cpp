// run_state_test.cpp — S3's headless gate: the per-run story state, the four new condition leaves
// evaluated through a context backed by that state, and the schemaVersion 3 save round-trip
// (including the documented pre-v3 migration).
// Run: ./run_state_test
#include <cstdio>
#include <string>

#include "condition.h"
#include "run_state.h"
#include "save_system.h"

static int g_checks = 0, g_fails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_fails;                                                            \
            fprintf(stderr, "  FAIL: %s\n", std::string(__VA_ARGS__).c_str());    \
        }                                                                         \
    } while (0)

// A ConditionContext backed by the real RunStoryState -- the same relationship GameConditionContext
// has to the live game, minus the renderer. This is what makes the S3 leaves testable end to end.
class TestContext : public toms::ConditionContext {
public:
    TestContext(const toms::RunStoryState& rs, int cycle, int beat)
        : rs_(rs), cycle_(cycle), beat_(beat) {}

    bool storyFlagSet(const std::string& f) const override { return rs_.flag(f); }
    int  storyBeat() const override { return beat_; }
    bool itemHeld(const std::string&, int) const override { return false; }
    int  statValue(const std::string&) const override { return 0; }
    bool missionComplete(const std::string&) const override { return false; }
    bool missionActive(const std::string&) const override { return false; }
    bool stageCleared(const std::string& id) const override { return rs_.floorCleared(id); }
    // the S3 four
    bool choiceMade(const std::string& c, const std::string& o) const override { return rs_.choiceMade(c, o); }
    std::string sideStoryState(const std::string& id) const override { return rs_.sideStoryState(id); }
    int counter(const std::string& name) const override { return rs_.counter(name); }
    int cycleIndex() const override { return cycle_; }

private:
    const toms::RunStoryState& rs_;
    int cycle_, beat_;
};

static void testRunState() {
    toms::RunStoryState rs;
    CHECK(rs.floor() == "F01", "a fresh run starts on F01");
    CHECK(rs.shardCount() == 0 && rs.deathsTotal() == 0, "a fresh run has no shards/deaths");

    // choices: the first main-line answer sticks (section 4.1's reversible:false)
    rs.makeChoice("c_motive", "opt_know_self");
    CHECK(rs.choiceMade("c_motive", "opt_know_self"), "a made choice is recorded");
    rs.makeChoice("c_motive", "opt_avenge");
    CHECK(rs.choiceMade("c_motive", "opt_know_self") && !rs.choiceMade("c_motive", "opt_avenge"),
          "an irreversible choice is not rewritten by a later pass");
    rs.makeChoice("c_debug", "opt_a", /*reversible=*/true);
    rs.makeChoice("c_debug", "opt_b", /*reversible=*/true);
    CHECK(rs.choiceMade("c_debug", "opt_b"), "a reversible choice can be changed");
    CHECK(!rs.choiceMade("c_never", "opt_x"), "an unmade choice is false");

    // counters: clamped to the declared range (so section 8's rebirth maths can't go negative)
    std::map<std::string, toms::CounterDecl> decls;
    decls["insight"] = toms::CounterDecl{-20, 20, 6};
    rs.setCounterDecls(decls);
    rs.addCounter("insight", 4);
    CHECK(rs.counter("insight") == 4, "counters accumulate");
    CHECK(!rs.counterVisible("insight"), "below displayAt the counter stays hidden");
    rs.addCounter("insight", 2);
    CHECK(rs.counterVisible("insight"), "at displayAt the counter becomes visible");
    rs.addCounter("insight", 100);
    CHECK(rs.counter("insight") == 20, "counters clamp at their declared maximum");
    rs.addCounter("insight", -100);
    CHECK(rs.counter("insight") == -20, "counters clamp at their declared minimum");
    CHECK(rs.counter("unset_counter") == 0, "an undeclared counter reads as 0");

    // side stories, flags, shards, floors, deaths
    rs.setSideStoryState("ss_01", "completed");
    CHECK(rs.sideStoryState("ss_01") == "completed" && rs.sideStoryState("ss_09").empty(),
          "side-story states are readable, unknown ones empty");
    rs.setFlag("flag_hate_carried");
    CHECK(rs.flag("flag_hate_carried") && !rs.flag("flag_never_set"), "flags set/read");
    rs.setFlag("flag_hate_carried", false);
    CHECK(!rs.flag("flag_hate_carried"), "flags can be cleared");
    rs.addShard("shard_05_the_night");
    rs.addShard("shard_05_the_night");
    CHECK(rs.shardCount() == 1, "shards do not duplicate");
    rs.markFloorCleared("F05");
    CHECK(rs.floorCleared("F05") && !rs.floorCleared("F06"), "cleared floors are tracked");
    CHECK(rs.floor() == "F05", "clearing a floor makes it the current floor");
    rs.noteDeath(false);
    rs.noteDeath(true);
    CHECK(rs.deathsTotal() == 2 && rs.deathsNonBoss() == 1, "deaths split boss/non-boss");

    // S4: skill points and owned skills -- the actual gating rule (cost/prerequisites) lives in
    // skill_system.h's canUnlockSkill; RunStoryState only owns the two numbers it reads and
    // applies a decision that's already been made.
    CHECK(rs.skillPoints() == 0 && rs.skillsOwned().empty(), "a fresh run has no skills/points");
    rs.addSkillPoints(1);
    CHECK(rs.skillPoints() == 1, "addSkillPoints adds");
    rs.addSkillPoints(-5);
    CHECK(rs.skillPoints() == 0, "skill points never go negative");
    rs.addSkillPoints(2);
    rs.unlockSkill("s_yinqi", 0);   // a chapter-granted root: cost 0
    CHECK(rs.hasSkill("s_yinqi") && rs.skillPoints() == 2, "a cost-0 unlock doesn't spend points");
    rs.unlockSkill("s_yinqi_1", 1);
    CHECK(rs.hasSkill("s_yinqi_1") && rs.skillPoints() == 1, "unlocking a real node spends its cost");
    rs.unlockSkill("s_yinqi_1", 1);
    CHECK(rs.skillPoints() == 1, "unlocking an already-owned skill again is a no-op (doesn't double-spend)");

    // M8: superMax defaults to kDefaultSuperMax, is clamped to >=1, and rides along with the rest
    // of a fresh run's wipe -- see Game::rebirth() (game_story.cpp) for who re-applies it at half
    // power afterward; that carry-over logic doesn't live here.
    CHECK(rs.superMax() == toms::RunStoryState::kDefaultSuperMax, "a fresh run's superMax is the default");
    rs.setSuperMax(2);
    CHECK(rs.superMax() == 2, "setSuperMax sets it");
    rs.setSuperMax(0);
    CHECK(rs.superMax() == 1, "setSuperMax clamps to a minimum of 1");
    rs.setSuperMax(3);

    // reset(keepShards=true) itself: shards survive, EVERYTHING else -- including skills/points/
    // superMax -- is wiped, same as a brand-new-game reset(). This is `reset()`'s own, narrow
    // contract; the "skills/points/superMax actually carry forward at half power" story is
    // Game::rebirth()'s job (game_story.cpp), which calls this and then immediately restores them
    // itself -- not tested here, since that needs Game, not just RunStoryState.
    rs.reset(/*keepShards=*/true);
    CHECK(rs.shardCount() == 1, "rebirth keeps the memory shards");
    CHECK(rs.choice("c_motive").empty() && rs.counter("insight") == 0
          && rs.sideStoryState("ss_01").empty() && rs.clearedFloors().empty()
          && rs.deathsTotal() == 0 && rs.floor() == "F01"
          && rs.skillPoints() == 0 && rs.skillsOwned().empty()
          && rs.superMax() == toms::RunStoryState::kDefaultSuperMax,
          "reset(keepShards=true) alone wipes choices/counters/side stories/floors/deaths/skills/superMax too");
    rs.reset();
    CHECK(rs.shardCount() == 0, "a fresh new game clears the shards too");
}

static void testConditionLeaves() {
    toms::RunStoryState rs;
    rs.makeChoice("c_motive", "opt_know_self");
    rs.setCounterDecls({{"insight", toms::CounterDecl{-20, 20, 6}}});
    rs.addCounter("insight", 6);
    rs.setSideStoryState("ss_08", "completed");
    rs.markFloorCleared("F05");

    TestContext ctx1(rs, /*cycle=*/1, /*beat=*/3);
    TestContext ctx2(rs, /*cycle=*/2, /*beat=*/3);

    // choiceMade
    CHECK(toms::evaluate({{"type", "choiceMade"}, {"choiceId", "c_motive"}, {"optionId", "opt_know_self"}}, ctx1),
          "choiceMade matches the recorded option");
    CHECK(!toms::evaluate({{"type", "choiceMade"}, {"choiceId", "c_motive"}, {"optionId", "opt_avenge"}}, ctx1),
          "choiceMade rejects the option that was not taken");
    // sideStoryState
    CHECK(toms::evaluate({{"type", "sideStoryState"}, {"sideStoryId", "ss_08"}, {"state", "completed"}}, ctx1),
          "sideStoryState matches the recorded state");
    CHECK(!toms::evaluate({{"type", "sideStoryState"}, {"sideStoryId", "ss_08"}, {"state", "failed"}}, ctx1),
          "sideStoryState rejects a different state");
    // counterAtLeast
    CHECK(toms::evaluate({{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 6}}, ctx1),
          "counterAtLeast passes at the threshold");
    CHECK(!toms::evaluate({{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 7}}, ctx1),
          "counterAtLeast fails below the threshold");
    // cycleIndexAtLeast -- the whole point of the second cycle
    CHECK(!toms::evaluate({{"type", "cycleIndexAtLeast"}, {"min", 2}}, ctx1),
          "cycleIndexAtLeast(2) is false on the first life");
    CHECK(toms::evaluate({{"type", "cycleIndexAtLeast"}, {"min", 2}}, ctx2),
          "cycleIndexAtLeast(2) is true on the second life");
    // combinators still work with the new leaves
    nlohmann::json combo = {{"all", {{{"type", "choiceMade"}, {"choiceId", "c_motive"}, {"optionId", "opt_know_self"}},
                                    {{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 6}}}}};
    CHECK(toms::evaluate(combo, ctx1), "new leaves compose with all/any/not");
    // unknown leaf still fails closed
    CHECK(!toms::evaluate({{"type", "noSuchType"}}, ctx1), "an unknown leaf type still fails closed");
    // stageCleared now reads the run's cleared floors
    CHECK(toms::evaluate({{"type", "stageCleared"}, {"stageId", "F05"}}, ctx1),
          "stageCleared reads clearedFloors on the run state");
}

static void testSaveV3() {
    // ---- round trip ----
    toms::RunSaveData r;
    r.schemaVersion = toms::kRunSaveSchemaVersion;
    CHECK(r.schemaVersion == 3, "the run schema is version 3");
    r.currentStageId = "F33";
    r.player.gold = 77;
    toms::RunStoryState rs;
    rs.makeChoice("c_gate_seal", "opt_unseal");
    rs.addCounter("humanity", 3);
    rs.setSideStoryState("ss_03", "accepted");
    rs.setFlag("flag_gate_unsealed");
    rs.addShard("shard_03_the_seal");
    rs.markFloorCleared("F19");
    rs.noteDeath(false);
    rs.addSkillPoints(2);
    rs.unlockSkill("s_yinqi", 0);
    rs.setSuperMax(3);   // M8
    rs.writeInto(r);

    nlohmann::json j = toms::toJson(r);
    CHECK(j["schemaVersion"] == 3 && j.contains("choices") && j.contains("counters")
          && j.contains("sideStories") && j.contains("flags") && j.contains("shards")
          && j.contains("clearedFloors") && j.contains("deathsTotal")
          && j.contains("skillPoints") && j.contains("skillsOwned") && j.contains("superMax"),
          "the v3 run file carries the story state");

    bool mismatch = false;
    toms::RunSaveData back = toms::runFromJson(j, &mismatch);
    CHECK(!mismatch, "a current-version file is not a version mismatch");
    toms::RunStoryState rs2;
    rs2.readFrom(back);
    CHECK(rs2.choiceMade("c_gate_seal", "opt_unseal"), "choices survive the round trip");
    CHECK(rs2.counter("humanity") == 3 && rs2.sideStoryState("ss_03") == "accepted",
          "counters and side stories survive the round trip");
    CHECK(rs2.flag("flag_gate_unsealed") && rs2.hasShard("shard_03_the_seal")
          && rs2.floorCleared("F19") && rs2.deathsTotal() == 1,
          "flags, shards, floors and deaths survive the round trip");
    CHECK(rs2.floor() == "F19" && back.currentStageId == "F33",
          "floor progress and the current stage both persist");
    CHECK(rs2.skillPoints() == 2 && rs2.hasSkill("s_yinqi"),
          "S4: skill points and owned skills survive the round trip");
    CHECK(rs2.superMax() == 3, "M8: superMax survives the round trip");

    // ---- pre-v3 migration: an OLD file must load, with the new fields defaulted ----
    nlohmann::json old = {
        {"schemaVersion", 2},
        {"currentStageId", "stage03"},
        {"player", {{"hp", 90}, {"maxhp", 120}, {"atk", 14}, {"def", 5}, {"gold", 12},
                    {"exp", 3}, {"lv", 2}, {"key_yellow", 1}, {"key_blue", 0}, {"key_red", 0},
                    {"inv", {"potion_red"}}}},
        {"entityStatus", {{"stage_03|4,5", "Defeated"}}}
    };
    mismatch = false;
    toms::RunSaveData migrated = toms::runFromJson(old, &mismatch);
    CHECK(mismatch, "the older version is reported as a mismatch (for logging)");
    CHECK(migrated.currentStageId == "stage03" && migrated.player.gold == 12
          && migrated.entityStatus.size() == 1,
          "the pre-v3 fields still load");
    CHECK(migrated.choices.empty() && migrated.counters.empty() && migrated.sideStories.empty()
          && migrated.shards.empty() && migrated.clearedFloors.empty()
          && migrated.floor == "F01" && migrated.deathsTotal == 0
          && migrated.skillPoints == 0 && migrated.skillsOwned.empty()
          && migrated.superMax == 5,
          "pre-v3 story state loads as the documented defaults, never a refusal");

    // meta: cycleIndex survives, defaults to 1 for old files
    toms::MetaSaveData m;
    m.cycleIndex = 3;
    m.endingsSeen = {"e_04"};
    m.hintsUnlocked = {"hint_shards"};
    m.forgeRecipesKnown = {"fr_gatewarden"};
    nlohmann::json mj = toms::toJson(m);
    toms::MetaSaveData m2 = toms::metaFromJson(mj);
    CHECK(m2.cycleIndex == 3 && m2.endingsSeen.size() == 1 && m2.hintsUnlocked.size() == 1,
          "meta keeps the rebirth record");
    CHECK(m2.forgeRecipesKnown.size() == 1 && m2.forgeRecipesKnown[0] == "fr_gatewarden",
          "S5: forge blueprints known survive the round trip (meta scope, not run scope)");
    nlohmann::json oldMeta = {{"schemaVersion", 1}, {"currentBeat", 2}};
    toms::MetaSaveData m3 = toms::metaFromJson(oldMeta);
    CHECK(m3.cycleIndex == 1 && m3.endingsSeen.empty(),
          "an old meta file loads with cycleIndex 1 (section 9's compatibility rule)");
    CHECK(m3.forgeRecipesKnown.empty(), "a pre-S5 meta file loads with no known recipes, never a refusal");
}

int main() {
    testRunState();
    testConditionLeaves();
    testSaveV3();
    printf("run_state_test: %s (%d checks)\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_checks);
    return g_fails == 0 ? 0 : 1;
}
