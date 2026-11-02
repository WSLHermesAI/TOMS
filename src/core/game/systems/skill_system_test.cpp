// skill_system_test.cpp — headless verification of the skill tree's pure content/math (S4).
// Exits 0 on success, 1 on any CHECK failure. Run: ./skill_system_test
#include "skill_system.h"
#include <cstdio>

using namespace toms;

static int g_iChecks = 0, g_iFails = 0;
#define CHECK(cond, msg) do { ++g_iChecks; if (!(cond)) { ++g_iFails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // 1. SkillDefinition JSON round-trip.
    {
        SkillDefinition l_oD;
        l_oD.m_strId = "s_yinqi_1"; l_oD.m_strLineage = "yinqi"; l_oD.m_iTier = 1; l_oD.m_iCost = 1;
        l_oD.m_vRequires = {"s_yinqi"};
        l_oD.m_jName = "Qi Drawing: Piercing"; l_oD.m_jDesc = "deepens it";
        l_oD.m_iStatAtk = 2; l_oD.m_iStatDef = 0;
        auto l_jJ = f_ToJson(l_oD);
        SkillDefinition l_oD2 = f_SkillFromJson(l_jJ);
        CHECK(l_oD2.m_strId == "s_yinqi_1" && l_oD2.m_strLineage == "yinqi", "round-trip: id/lineage");
        CHECK(l_oD2.m_iTier == 1 && l_oD2.m_iCost == 1, "round-trip: tier/cost");
        CHECK(l_oD2.m_vRequires.size() == 1 && l_oD2.m_vRequires[0] == "s_yinqi", "round-trip: requires");
        CHECK(l_oD2.m_iStatAtk == 2 && l_oD2.m_iStatDef == 0, "round-trip: effect");
    }
    // A file that keys by id but doesn't repeat "id" inside the object still gets one (loadSkillDefs).
    {
        nlohmann::json l_jNoId = { {"lineage","yuqi"}, {"tier",0}, {"cost",0} };
        SkillDefinition l_oD = f_SkillFromJson(l_jNoId);
        CHECK(l_oD.m_strId.empty(), "skillFromJson alone leaves a missing id empty -- loadSkillDefs is what backfills it");
    }
    // Malformed input never throws / crashes -- fail-soft like every other *FromJson in this project.
    CHECK(f_SkillFromJson(nlohmann::json(nullptr)).m_strId.empty(), "skillFromJson(null) returns a default, not a throw");

    // 2. loadSkillDefs against the REAL data/skills.json (run from the project root, matching every
    //    other data-file test in this suite).
    auto l_mapDefs = f_LoadSkillDefs("data/skills.json");
    // M9: ch_04's side story ss_04 added a 7th node (s_yinqi_combo, a tier-2 dialogue grant) on
    // top of the 3 lineages' root + tier-1 nodes.
    CHECK(l_mapDefs.size() == 7, "data/skills.json has the 3 lineages' root + tier-1 nodes, plus ss_04's s_yinqi_combo (7 total)");
    CHECK(l_mapDefs.count("s_yinqi") && l_mapDefs.count("s_yuqi") && l_mapDefs.count("s_faqi"),
          "the three chapter-granted roots exist");
    CHECK(l_mapDefs["s_yinqi"].m_iCost == 0 && l_mapDefs["s_yuqi"].m_iCost == 0 && l_mapDefs["s_faqi"].m_iCost == 0,
          "root nodes cost 0 (they arrive as a chapter grant, not a purchase)");
    CHECK(l_mapDefs["s_yinqi_1"].m_vRequires.size() == 1 && l_mapDefs["s_yinqi_1"].m_vRequires[0] == "s_yinqi",
          "a tier-1 node requires its lineage's root");
    CHECK(f_LoadSkillDefs("data/does_not_exist.json").empty(), "a missing file loads to an empty map, not a crash");

    // 3. canUnlockSkill: every gating rule, against the real loaded content.
    // A root (tier 0) is grant-only -- Game::applyChapterGrants awards it directly via
    // RunStoryState::unlockSkill, bypassing this predicate entirely, exactly so that path is never
    // subject to it. canUnlockSkill itself must refuse it even with points and even unowned --
    // this is the fix for a real bug caught live (S4's own verification): without this rule,
    // opening the skill screen with any point at all let the player instantly buy ch_02/ch_03's
    // not-yet-granted roots, since their cost is 0.
    CHECK(!f_CanUnlockSkill(l_mapDefs, {}, 999, "s_yuqi"), "a root (tier 0) is never purchasable, however many points -- grant-only");
    CHECK(!f_CanUnlockSkill(l_mapDefs, {"s_yinqi"}, 5, "s_yinqi"), "already-owned is never unlockable again");
    CHECK(!f_CanUnlockSkill(l_mapDefs, {}, 0, "s_yinqi_1"), "missing the required prerequisite blocks it");
    CHECK(!f_CanUnlockSkill(l_mapDefs, {"s_yinqi"}, 0, "s_yinqi_1"), "prerequisite owned but not enough points still blocks it");
    CHECK(f_CanUnlockSkill(l_mapDefs, {"s_yinqi"}, 1, "s_yinqi_1"), "prerequisite owned + exact cost -> unlockable");
    CHECK(f_CanUnlockSkill(l_mapDefs, {"s_yinqi"}, 5, "s_yinqi_1"), "more than enough points is still fine");
    CHECK(!f_CanUnlockSkill(l_mapDefs, {}, 999, "not_a_real_skill"), "an unknown skill id is never unlockable, however many points");

    // 4. applySkillEffects: stacks additively on top of an already-computed atk/def, exactly the
    //    same shape as applyEquipmentStats so a caller adds both without a parallel formula.
    {
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {"s_yinqi", "s_yuqi"}, l_iAtk, l_iDef);
        CHECK(l_iAtk == 13 && l_iDef == 5, "owning both roots adds +1 atk (yinqi) and +1 def (yuqi)");
    }
    {
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {"s_yinqi", "s_yinqi_1"}, l_iAtk, l_iDef);
        CHECK(l_iAtk == 15 && l_iDef == 4, "a lineage's root + its tier-1 node stack (1 + 2 = 3 atk)");
    }
    {
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {"s_faqi"}, l_iAtk, l_iDef);
        CHECK(l_iAtk == 13 && l_iDef == 5, "a hybrid node (faqi) contributes both stats at once");
    }
    {
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {"not_a_real_skill"}, l_iAtk, l_iDef);
        CHECK(l_iAtk == 12 && l_iDef == 4, "an unknown owned id contributes nothing rather than throwing");
    }
    {
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {}, l_iAtk, l_iDef);
        CHECK(l_iAtk == 12 && l_iDef == 4, "owning nothing changes nothing");
    }

    // 5. M8: rebirthScale halves a tier>=1 node's bonus (floored), but a root (tier<=0, a pure
    //    grant, never purchased) always keeps its full effect regardless of the scale -- matching
    //    STORY_BIBLE.md §8's explicit carve-out ("純解鎖型技能如「引氣」保留全效").
    {
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {"s_yinqi", "s_yinqi_1"}, l_iAtk, l_iDef, 0.5f);
        CHECK(l_iAtk == 14, "root's +1 stays full-effect; tier-1's +2 floors to +1 at half power (12+1+1=14)");
    }
    {
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {"s_yinqi"}, l_iAtk, l_iDef, 0.5f);
        CHECK(l_iAtk == 13, "a lone root is completely unaffected by rebirthScale");
    }
    {
        // default rebirthScale (1.0, unspecified) behaves exactly as every pre-M8 call site expects.
        int l_iAtk = 12, l_iDef = 4;
        f_ApplySkillEffects(l_mapDefs, {"s_yinqi", "s_yinqi_1"}, l_iAtk, l_iDef);
        CHECK(l_iAtk == 15, "omitting rebirthScale defaults to 1.0 (no change from before M8)");
    }

    if (g_iFails == 0) { printf("skill_system_test: ALL PASS (%d checks)\n", g_iChecks); return 0; }
    printf("skill_system_test: %d/%d CHECK(s) FAILED\n", g_iFails, g_iChecks);
    return 1;
}
