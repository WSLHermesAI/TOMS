// skill_system_test.cpp — headless verification of the skill tree's pure content/math (S4).
// Exits 0 on success, 1 on any CHECK failure. Run: ./skill_system_test
#include "skill_system.h"
#include <cstdio>

using namespace toms;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // 1. SkillDefinition JSON round-trip.
    {
        SkillDefinition d;
        d.id = "s_yinqi_1"; d.lineage = "yinqi"; d.tier = 1; d.cost = 1;
        d.requires_ = {"s_yinqi"};
        d.name = "Qi Drawing: Piercing"; d.desc = "deepens it";
        d.statAtk = 2; d.statDef = 0;
        auto j = toJson(d);
        SkillDefinition d2 = skillFromJson(j);
        CHECK(d2.id == "s_yinqi_1" && d2.lineage == "yinqi", "round-trip: id/lineage");
        CHECK(d2.tier == 1 && d2.cost == 1, "round-trip: tier/cost");
        CHECK(d2.requires_.size() == 1 && d2.requires_[0] == "s_yinqi", "round-trip: requires");
        CHECK(d2.statAtk == 2 && d2.statDef == 0, "round-trip: effect");
    }
    // A file that keys by id but doesn't repeat "id" inside the object still gets one (loadSkillDefs).
    {
        nlohmann::json noId = { {"lineage","yuqi"}, {"tier",0}, {"cost",0} };
        SkillDefinition d = skillFromJson(noId);
        CHECK(d.id.empty(), "skillFromJson alone leaves a missing id empty -- loadSkillDefs is what backfills it");
    }
    // Malformed input never throws / crashes -- fail-soft like every other *FromJson in this project.
    CHECK(skillFromJson(nlohmann::json(nullptr)).id.empty(), "skillFromJson(null) returns a default, not a throw");

    // 2. loadSkillDefs against the REAL data/skills.json (run from the project root, matching every
    //    other data-file test in this suite).
    auto defs = loadSkillDefs("data/skills.json");
    // M9: ch_04's side story ss_04 added a 7th node (s_yinqi_combo, a tier-2 dialogue grant) on
    // top of the 3 lineages' root + tier-1 nodes.
    CHECK(defs.size() == 7, "data/skills.json has the 3 lineages' root + tier-1 nodes, plus ss_04's s_yinqi_combo (7 total)");
    CHECK(defs.count("s_yinqi") && defs.count("s_yuqi") && defs.count("s_faqi"),
          "the three chapter-granted roots exist");
    CHECK(defs["s_yinqi"].cost == 0 && defs["s_yuqi"].cost == 0 && defs["s_faqi"].cost == 0,
          "root nodes cost 0 (they arrive as a chapter grant, not a purchase)");
    CHECK(defs["s_yinqi_1"].requires_.size() == 1 && defs["s_yinqi_1"].requires_[0] == "s_yinqi",
          "a tier-1 node requires its lineage's root");
    CHECK(loadSkillDefs("data/does_not_exist.json").empty(), "a missing file loads to an empty map, not a crash");

    // 3. canUnlockSkill: every gating rule, against the real loaded content.
    // A root (tier 0) is grant-only -- Game::applyChapterGrants awards it directly via
    // RunStoryState::unlockSkill, bypassing this predicate entirely, exactly so that path is never
    // subject to it. canUnlockSkill itself must refuse it even with points and even unowned --
    // this is the fix for a real bug caught live (S4's own verification): without this rule,
    // opening the skill screen with any point at all let the player instantly buy ch_02/ch_03's
    // not-yet-granted roots, since their cost is 0.
    CHECK(!canUnlockSkill(defs, {}, 999, "s_yuqi"), "a root (tier 0) is never purchasable, however many points -- grant-only");
    CHECK(!canUnlockSkill(defs, {"s_yinqi"}, 5, "s_yinqi"), "already-owned is never unlockable again");
    CHECK(!canUnlockSkill(defs, {}, 0, "s_yinqi_1"), "missing the required prerequisite blocks it");
    CHECK(!canUnlockSkill(defs, {"s_yinqi"}, 0, "s_yinqi_1"), "prerequisite owned but not enough points still blocks it");
    CHECK(canUnlockSkill(defs, {"s_yinqi"}, 1, "s_yinqi_1"), "prerequisite owned + exact cost -> unlockable");
    CHECK(canUnlockSkill(defs, {"s_yinqi"}, 5, "s_yinqi_1"), "more than enough points is still fine");
    CHECK(!canUnlockSkill(defs, {}, 999, "not_a_real_skill"), "an unknown skill id is never unlockable, however many points");

    // 4. applySkillEffects: stacks additively on top of an already-computed atk/def, exactly the
    //    same shape as applyEquipmentStats so a caller adds both without a parallel formula.
    {
        int atk = 12, def = 4;
        applySkillEffects(defs, {"s_yinqi", "s_yuqi"}, atk, def);
        CHECK(atk == 13 && def == 5, "owning both roots adds +1 atk (yinqi) and +1 def (yuqi)");
    }
    {
        int atk = 12, def = 4;
        applySkillEffects(defs, {"s_yinqi", "s_yinqi_1"}, atk, def);
        CHECK(atk == 15 && def == 4, "a lineage's root + its tier-1 node stack (1 + 2 = 3 atk)");
    }
    {
        int atk = 12, def = 4;
        applySkillEffects(defs, {"s_faqi"}, atk, def);
        CHECK(atk == 13 && def == 5, "a hybrid node (faqi) contributes both stats at once");
    }
    {
        int atk = 12, def = 4;
        applySkillEffects(defs, {"not_a_real_skill"}, atk, def);
        CHECK(atk == 12 && def == 4, "an unknown owned id contributes nothing rather than throwing");
    }
    {
        int atk = 12, def = 4;
        applySkillEffects(defs, {}, atk, def);
        CHECK(atk == 12 && def == 4, "owning nothing changes nothing");
    }

    // 5. M8: rebirthScale halves a tier>=1 node's bonus (floored), but a root (tier<=0, a pure
    //    grant, never purchased) always keeps its full effect regardless of the scale -- matching
    //    STORY_BIBLE.md §8's explicit carve-out ("純解鎖型技能如「引氣」保留全效").
    {
        int atk = 12, def = 4;
        applySkillEffects(defs, {"s_yinqi", "s_yinqi_1"}, atk, def, 0.5f);
        CHECK(atk == 14, "root's +1 stays full-effect; tier-1's +2 floors to +1 at half power (12+1+1=14)");
    }
    {
        int atk = 12, def = 4;
        applySkillEffects(defs, {"s_yinqi"}, atk, def, 0.5f);
        CHECK(atk == 13, "a lone root is completely unaffected by rebirthScale");
    }
    {
        // default rebirthScale (1.0, unspecified) behaves exactly as every pre-M8 call site expects.
        int atk = 12, def = 4;
        applySkillEffects(defs, {"s_yinqi", "s_yinqi_1"}, atk, def);
        CHECK(atk == 15, "omitting rebirthScale defaults to 1.0 (no change from before M8)");
    }

    if (g_fails == 0) { printf("skill_system_test: ALL PASS (%d checks)\n", g_checks); return 0; }
    printf("skill_system_test: %d/%d CHECK(s) FAILED\n", g_fails, g_checks);
    return 1;
}
