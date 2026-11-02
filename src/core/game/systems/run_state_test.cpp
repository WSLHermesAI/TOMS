// run_state_test.cpp — S3's headless gate: the per-run story state, the four new condition leaves
// evaluated through a context backed by that state, and the schemaVersion 3 save round-trip
// (including the documented pre-v3 migration).
// Run: ./run_state_test
#include <cstdio>
#include <string>

#include "condition.h"
#include "run_state.h"
#include "save_system.h"

static int g_iChecks = 0, g_iFails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_iChecks;                                                               \
        if (!(cond)) {                                                            \
            ++g_iFails;                                                            \
            fprintf(stderr, "  FAIL: %s\n", std::string(__VA_ARGS__).c_str());    \
        }                                                                         \
    } while (0)

// A ConditionContext backed by the real RunStoryState -- the same relationship GameConditionContext
// has to the live game, minus the renderer. This is what makes the S3 leaves testable end to end.
class TestContext : public toms::ConditionContext {
public:
    TestContext(const toms::RunStoryState& l_oRs, int l_iCycle, int l_iBeat)
        : m_oRs(l_oRs), m_iCycle(l_iCycle), m_iBeat(l_iBeat) {}

    bool f_StoryFlagSet(const std::string& l_strF) const override { return m_oRs.f_Flag(l_strF); }
    int  f_StoryBeat() const override { return m_iBeat; }
    bool f_ItemHeld(const std::string&, int) const override { return false; }
    int  f_StatValue(const std::string&) const override { return 0; }
    bool f_MissionComplete(const std::string&) const override { return false; }
    bool f_MissionActive(const std::string&) const override { return false; }
    bool f_StageCleared(const std::string& l_strId) const override { return m_oRs.f_FloorCleared(l_strId); }
    // the S3 four
    bool f_ChoiceMade(const std::string& l_strC, const std::string& l_strO) const override { return m_oRs.f_ChoiceMade(l_strC, l_strO); }
    std::string f_SideStoryState(const std::string& l_strId) const override { return m_oRs.f_SideStoryState(l_strId); }
    int f_Counter(const std::string& l_strName) const override { return m_oRs.f_Counter(l_strName); }
    int f_CycleIndex() const override { return m_iCycle; }

private:
    const toms::RunStoryState& m_oRs;
    int m_iCycle, m_iBeat;
};

static void f_TestRunState() {
    toms::RunStoryState l_oRs;
    CHECK(l_oRs.f_Floor() == "F01", "a fresh run starts on F01");
    CHECK(l_oRs.f_ShardCount() == 0 && l_oRs.f_DeathsTotal() == 0, "a fresh run has no shards/deaths");

    // choices: the first main-line answer sticks (section 4.1's reversible:false)
    l_oRs.f_MakeChoice("c_motive", "opt_know_self");
    CHECK(l_oRs.f_ChoiceMade("c_motive", "opt_know_self"), "a made choice is recorded");
    l_oRs.f_MakeChoice("c_motive", "opt_avenge");
    CHECK(l_oRs.f_ChoiceMade("c_motive", "opt_know_self") && !l_oRs.f_ChoiceMade("c_motive", "opt_avenge"),
          "an irreversible choice is not rewritten by a later pass");
    l_oRs.f_MakeChoice("c_debug", "opt_a", /*reversible=*/true);
    l_oRs.f_MakeChoice("c_debug", "opt_b", /*reversible=*/true);
    CHECK(l_oRs.f_ChoiceMade("c_debug", "opt_b"), "a reversible choice can be changed");
    CHECK(!l_oRs.f_ChoiceMade("c_never", "opt_x"), "an unmade choice is false");

    // counters: clamped to the declared range (so section 8's rebirth maths can't go negative)
    std::map<std::string, toms::CounterDecl> l_mapDecls;
    l_mapDecls["insight"] = toms::CounterDecl{-20, 20, 6};
    l_oRs.f_SetCounterDecls(l_mapDecls);
    l_oRs.f_AddCounter("insight", 4);
    CHECK(l_oRs.f_Counter("insight") == 4, "counters accumulate");
    CHECK(!l_oRs.f_CounterVisible("insight"), "below displayAt the counter stays hidden");
    l_oRs.f_AddCounter("insight", 2);
    CHECK(l_oRs.f_CounterVisible("insight"), "at displayAt the counter becomes visible");
    l_oRs.f_AddCounter("insight", 100);
    CHECK(l_oRs.f_Counter("insight") == 20, "counters clamp at their declared maximum");
    l_oRs.f_AddCounter("insight", -100);
    CHECK(l_oRs.f_Counter("insight") == -20, "counters clamp at their declared minimum");
    CHECK(l_oRs.f_Counter("unset_counter") == 0, "an undeclared counter reads as 0");

    // side stories, flags, shards, floors, deaths
    l_oRs.f_SetSideStoryState("ss_01", "completed");
    CHECK(l_oRs.f_SideStoryState("ss_01") == "completed" && l_oRs.f_SideStoryState("ss_09").empty(),
          "side-story states are readable, unknown ones empty");
    l_oRs.f_SetFlag("flag_hate_carried");
    CHECK(l_oRs.f_Flag("flag_hate_carried") && !l_oRs.f_Flag("flag_never_set"), "flags set/read");
    l_oRs.f_SetFlag("flag_hate_carried", false);
    CHECK(!l_oRs.f_Flag("flag_hate_carried"), "flags can be cleared");
    l_oRs.f_AddShard("shard_05_the_night");
    l_oRs.f_AddShard("shard_05_the_night");
    CHECK(l_oRs.f_ShardCount() == 1, "shards do not duplicate");
    l_oRs.f_MarkFloorCleared("F05");
    CHECK(l_oRs.f_FloorCleared("F05") && !l_oRs.f_FloorCleared("F06"), "cleared floors are tracked");
    CHECK(l_oRs.f_Floor() == "F05", "clearing a floor makes it the current floor");
    l_oRs.f_NoteDeath(false);
    l_oRs.f_NoteDeath(true);
    CHECK(l_oRs.f_DeathsTotal() == 2 && l_oRs.f_DeathsNonBoss() == 1, "deaths split boss/non-boss");

    // S4: skill points and owned skills -- the actual gating rule (cost/prerequisites) lives in
    // skill_system.h's canUnlockSkill; RunStoryState only owns the two numbers it reads and
    // applies a decision that's already been made.
    CHECK(l_oRs.f_SkillPoints() == 0 && l_oRs.f_SkillsOwned().empty(), "a fresh run has no skills/points");
    l_oRs.f_AddSkillPoints(1);
    CHECK(l_oRs.f_SkillPoints() == 1, "addSkillPoints adds");
    l_oRs.f_AddSkillPoints(-5);
    CHECK(l_oRs.f_SkillPoints() == 0, "skill points never go negative");
    l_oRs.f_AddSkillPoints(2);
    l_oRs.f_UnlockSkill("s_yinqi", 0);   // a chapter-granted root: cost 0
    CHECK(l_oRs.f_HasSkill("s_yinqi") && l_oRs.f_SkillPoints() == 2, "a cost-0 unlock doesn't spend points");
    l_oRs.f_UnlockSkill("s_yinqi_1", 1);
    CHECK(l_oRs.f_HasSkill("s_yinqi_1") && l_oRs.f_SkillPoints() == 1, "unlocking a real node spends its cost");
    l_oRs.f_UnlockSkill("s_yinqi_1", 1);
    CHECK(l_oRs.f_SkillPoints() == 1, "unlocking an already-owned skill again is a no-op (doesn't double-spend)");

    // M8: superMax defaults to kDefaultSuperMax, is clamped to >=1, and rides along with the rest
    // of a fresh run's wipe -- see Game::rebirth() (game_story.cpp) for who re-applies it at half
    // power afterward; that carry-over logic doesn't live here.
    CHECK(l_oRs.f_SuperMax() == toms::RunStoryState::m_iDefaultSuperMax, "a fresh run's superMax is the default");
    l_oRs.f_SetSuperMax(2);
    CHECK(l_oRs.f_SuperMax() == 2, "setSuperMax sets it");
    l_oRs.f_SetSuperMax(0);
    CHECK(l_oRs.f_SuperMax() == 1, "setSuperMax clamps to a minimum of 1");
    l_oRs.f_SetSuperMax(3);

    // reset(keepShards=true) itself: shards survive, EVERYTHING else -- including skills/points/
    // superMax -- is wiped, same as a brand-new-game reset(). This is `reset()`'s own, narrow
    // contract; the "skills/points/superMax actually carry forward at half power" story is
    // Game::rebirth()'s job (game_story.cpp), which calls this and then immediately restores them
    // itself -- not tested here, since that needs Game, not just RunStoryState.
    l_oRs.f_Reset(/*keepShards=*/true);
    CHECK(l_oRs.f_ShardCount() == 1, "rebirth keeps the memory shards");
    CHECK(l_oRs.f_Choice("c_motive").empty() && l_oRs.f_Counter("insight") == 0
          && l_oRs.f_SideStoryState("ss_01").empty() && l_oRs.f_ClearedFloors().empty()
          && l_oRs.f_DeathsTotal() == 0 && l_oRs.f_Floor() == "F01"
          && l_oRs.f_SkillPoints() == 0 && l_oRs.f_SkillsOwned().empty()
          && l_oRs.f_SuperMax() == toms::RunStoryState::m_iDefaultSuperMax,
          "reset(keepShards=true) alone wipes choices/counters/side stories/floors/deaths/skills/superMax too");
    l_oRs.f_Reset();
    CHECK(l_oRs.f_ShardCount() == 0, "a fresh new game clears the shards too");
}

static void f_TestConditionLeaves() {
    toms::RunStoryState l_oRs;
    l_oRs.f_MakeChoice("c_motive", "opt_know_self");
    l_oRs.f_SetCounterDecls({{"insight", toms::CounterDecl{-20, 20, 6}}});
    l_oRs.f_AddCounter("insight", 6);
    l_oRs.f_SetSideStoryState("ss_08", "completed");
    l_oRs.f_MarkFloorCleared("F05");

    TestContext l_oCtx1(l_oRs, /*cycle=*/1, /*beat=*/3);
    TestContext l_oCtx2(l_oRs, /*cycle=*/2, /*beat=*/3);

    // choiceMade
    CHECK(toms::f_Evaluate({{"type", "choiceMade"}, {"choiceId", "c_motive"}, {"optionId", "opt_know_self"}}, l_oCtx1),
          "choiceMade matches the recorded option");
    CHECK(!toms::f_Evaluate({{"type", "choiceMade"}, {"choiceId", "c_motive"}, {"optionId", "opt_avenge"}}, l_oCtx1),
          "choiceMade rejects the option that was not taken");
    // sideStoryState
    CHECK(toms::f_Evaluate({{"type", "sideStoryState"}, {"sideStoryId", "ss_08"}, {"state", "completed"}}, l_oCtx1),
          "sideStoryState matches the recorded state");
    CHECK(!toms::f_Evaluate({{"type", "sideStoryState"}, {"sideStoryId", "ss_08"}, {"state", "failed"}}, l_oCtx1),
          "sideStoryState rejects a different state");
    // counterAtLeast
    CHECK(toms::f_Evaluate({{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 6}}, l_oCtx1),
          "counterAtLeast passes at the threshold");
    CHECK(!toms::f_Evaluate({{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 7}}, l_oCtx1),
          "counterAtLeast fails below the threshold");
    // cycleIndexAtLeast -- the whole point of the second cycle
    CHECK(!toms::f_Evaluate({{"type", "cycleIndexAtLeast"}, {"min", 2}}, l_oCtx1),
          "cycleIndexAtLeast(2) is false on the first life");
    CHECK(toms::f_Evaluate({{"type", "cycleIndexAtLeast"}, {"min", 2}}, l_oCtx2),
          "cycleIndexAtLeast(2) is true on the second life");
    // combinators still work with the new leaves
    nlohmann::json l_jCombo = {{"all", {{{"type", "choiceMade"}, {"choiceId", "c_motive"}, {"optionId", "opt_know_self"}},
                                    {{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 6}}}}};
    CHECK(toms::f_Evaluate(l_jCombo, l_oCtx1), "new leaves compose with all/any/not");
    // unknown leaf still fails closed
    CHECK(!toms::f_Evaluate({{"type", "noSuchType"}}, l_oCtx1), "an unknown leaf type still fails closed");
    // stageCleared now reads the run's cleared floors
    CHECK(toms::f_Evaluate({{"type", "stageCleared"}, {"stageId", "F05"}}, l_oCtx1),
          "stageCleared reads clearedFloors on the run state");
}

static void f_TestSaveV3() {
    // ---- round trip ----
    toms::RunSaveData l_oR;
    l_oR.m_iSchemaVersion = toms::g_iRunSaveSchemaVersion;
    CHECK(l_oR.m_iSchemaVersion == 3, "the run schema is version 3");
    l_oR.m_strCurrentStageId = "F33";
    l_oR.m_oPlayer.m_iGold = 77;
    toms::RunStoryState l_oRs;
    l_oRs.f_MakeChoice("c_gate_seal", "opt_unseal");
    l_oRs.f_AddCounter("humanity", 3);
    l_oRs.f_SetSideStoryState("ss_03", "accepted");
    l_oRs.f_SetFlag("flag_gate_unsealed");
    l_oRs.f_AddShard("shard_03_the_seal");
    l_oRs.f_MarkFloorCleared("F19");
    l_oRs.f_NoteDeath(false);
    l_oRs.f_AddSkillPoints(2);
    l_oRs.f_UnlockSkill("s_yinqi", 0);
    l_oRs.f_SetSuperMax(3);   // M8
    l_oRs.f_WriteInto(l_oR);

    nlohmann::json l_jJ = toms::f_ToJson(l_oR);
    CHECK(l_jJ["schemaVersion"] == 3 && l_jJ.contains("choices") && l_jJ.contains("counters")
          && l_jJ.contains("sideStories") && l_jJ.contains("flags") && l_jJ.contains("shards")
          && l_jJ.contains("clearedFloors") && l_jJ.contains("deathsTotal")
          && l_jJ.contains("skillPoints") && l_jJ.contains("skillsOwned") && l_jJ.contains("superMax"),
          "the v3 run file carries the story state");

    bool l_bMismatch = false;
    toms::RunSaveData l_oBack = toms::f_RunFromJson(l_jJ, &l_bMismatch);
    CHECK(!l_bMismatch, "a current-version file is not a version mismatch");
    toms::RunStoryState l_oRs2;
    l_oRs2.f_ReadFrom(l_oBack);
    CHECK(l_oRs2.f_ChoiceMade("c_gate_seal", "opt_unseal"), "choices survive the round trip");
    CHECK(l_oRs2.f_Counter("humanity") == 3 && l_oRs2.f_SideStoryState("ss_03") == "accepted",
          "counters and side stories survive the round trip");
    CHECK(l_oRs2.f_Flag("flag_gate_unsealed") && l_oRs2.f_HasShard("shard_03_the_seal")
          && l_oRs2.f_FloorCleared("F19") && l_oRs2.f_DeathsTotal() == 1,
          "flags, shards, floors and deaths survive the round trip");
    CHECK(l_oRs2.f_Floor() == "F19" && l_oBack.m_strCurrentStageId == "F33",
          "floor progress and the current stage both persist");
    CHECK(l_oRs2.f_SkillPoints() == 2 && l_oRs2.f_HasSkill("s_yinqi"),
          "S4: skill points and owned skills survive the round trip");
    CHECK(l_oRs2.f_SuperMax() == 3, "M8: superMax survives the round trip");

    // ---- pre-v3 migration: an OLD file must load, with the new fields defaulted ----
    nlohmann::json l_jOld = {
        {"schemaVersion", 2},
        {"currentStageId", "stage03"},
        {"player", {{"hp", 90}, {"maxhp", 120}, {"atk", 14}, {"def", 5}, {"gold", 12},
                    {"exp", 3}, {"lv", 2}, {"key_yellow", 1}, {"key_blue", 0}, {"key_red", 0},
                    {"inv", {"potion_red"}}}},
        {"entityStatus", {{"stage_03|4,5", "Defeated"}}}
    };
    l_bMismatch = false;
    toms::RunSaveData l_oMigrated = toms::f_RunFromJson(l_jOld, &l_bMismatch);
    CHECK(l_bMismatch, "the older version is reported as a mismatch (for logging)");
    CHECK(l_oMigrated.m_strCurrentStageId == "stage03" && l_oMigrated.m_oPlayer.m_iGold == 12
          && l_oMigrated.m_mapEntityStatus.size() == 1,
          "the pre-v3 fields still load");
    CHECK(l_oMigrated.m_mapChoices.empty() && l_oMigrated.m_mapCounters.empty() && l_oMigrated.m_mapSideStories.empty()
          && l_oMigrated.m_vShards.empty() && l_oMigrated.m_vClearedFloors.empty()
          && l_oMigrated.m_strFloor == "F01" && l_oMigrated.m_iDeathsTotal == 0
          && l_oMigrated.m_iSkillPoints == 0 && l_oMigrated.m_vSkillsOwned.empty()
          && l_oMigrated.m_iSuperMax == 5,
          "pre-v3 story state loads as the documented defaults, never a refusal");

    // meta: cycleIndex survives, defaults to 1 for old files
    toms::MetaSaveData l_oM;
    l_oM.m_iCycleIndex = 3;
    l_oM.m_vEndingsSeen = {"e_04"};
    l_oM.m_vHintsUnlocked = {"hint_shards"};
    l_oM.m_vForgeRecipesKnown = {"fr_gatewarden"};
    nlohmann::json l_jMj = toms::f_ToJson(l_oM);
    toms::MetaSaveData l_oM2 = toms::f_MetaFromJson(l_jMj);
    CHECK(l_oM2.m_iCycleIndex == 3 && l_oM2.m_vEndingsSeen.size() == 1 && l_oM2.m_vHintsUnlocked.size() == 1,
          "meta keeps the rebirth record");
    CHECK(l_oM2.m_vForgeRecipesKnown.size() == 1 && l_oM2.m_vForgeRecipesKnown[0] == "fr_gatewarden",
          "S5: forge blueprints known survive the round trip (meta scope, not run scope)");
    nlohmann::json l_jOldMeta = {{"schemaVersion", 1}, {"currentBeat", 2}};
    toms::MetaSaveData l_oM3 = toms::f_MetaFromJson(l_jOldMeta);
    CHECK(l_oM3.m_iCycleIndex == 1 && l_oM3.m_vEndingsSeen.empty(),
          "an old meta file loads with cycleIndex 1 (section 9's compatibility rule)");
    CHECK(l_oM3.m_vForgeRecipesKnown.empty(), "a pre-S5 meta file loads with no known recipes, never a refusal");
}

int main() {
    f_TestRunState();
    f_TestConditionLeaves();
    f_TestSaveV3();
    printf("run_state_test: %s (%d checks)\n", g_iFails == 0 ? "ALL PASS" : "FAILED", g_iChecks);
    return g_iFails == 0 ? 0 : 1;
}
