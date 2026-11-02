// battle_rules_test.cpp -- looks, monster events, moves, the pace and the break gauge (battle_rules.h). Exits 0 on success.
#include "battle_rules.h"

#include <cstdio>

using namespace toms;
using nlohmann::json;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    // 1. Looks: a state from the look, from its base, and the fallback look for one that is not there.
    const LooksTable l_oLooks = f_LooksFromJson(json::parse(R"({
        "_comment": "x",
        "monster": { "states": { "attack": { "sound": "enemy_attack" }, "die": { "sound": "enemy_down", "fx": "puff" } } },
        "golem":   { "base": "monster", "states": { "attack": { "clip": "slam", "sound": "golem_slam", "fx": "dust" } } }
    })"));
    const LookState* l_pA = l_oLooks.f_Find("golem", "attack");
    CHECK(l_pA && l_pA->m_strClip == "slam" && l_pA->m_strSound == "golem_slam" && l_pA->m_strFx == "dust", "the look's own state");
    const LookState* l_pD = l_oLooks.f_Find("golem", "die");
    CHECK(l_pD && l_pD->m_strSound == "enemy_down" && l_pD->m_strFx == "puff", "a state from the base look");
    const LookState* l_pF = l_oLooks.f_Find("nobody", "attack");
    CHECK(l_pF && l_pF->m_strSound == "enemy_attack", "an unknown look falls back to \"monster\"");
    CHECK(l_oLooks.f_Find("golem", "idle") == nullptr, "no state in the chain: null");

    // 2. Monster events.
    std::vector<MonsterEvent> l_vEv = f_MonsterEventsFromJson(json::parse(R"([
        { "on": "hpBelow", "value": 50, "do": [ { "say": "Runes blaze" }, { "stat": "atk", "mul": 1.3 } ] },
        { "on": "hpBelow", "value": 20, "do": [ { "transformInto": "golem_core", "hp": "keepShare" } ] },
        { "on": "attacks", "value": 2, "once": false, "do": [ { "say": "again" } ] },
        { "on": "everyMs", "value": 1000, "do": [ { "heal": 5 } ] },
        { "on": "battleStart", "do": [] }
    ])"));
    CHECK(l_vEv.size() == 5 && l_vEv[0].m_bOnce && !l_vEv[2].m_bOnce && !l_vEv[3].m_bOnce, "events parsed, everyMs repeats by default");
    EventCheck l_oHit; l_oHit.m_strOn = "hpBelow"; l_oHit.m_fHpPctBefore = 80; l_oHit.m_fHpPctAfter = 60;
    CHECK(f_FireMonsterEvents(l_vEv, l_oHit).empty(), "above 50 %: nothing");
    l_oHit.m_fHpPctBefore = 60; l_oHit.m_fHpPctAfter = 10;
    std::vector<int> l_vFired = f_FireMonsterEvents(l_vEv, l_oHit);
    CHECK(l_vFired.size() == 2 && l_vFired[0] == 0 && l_vFired[1] == 1, "one hit crossing 50 and 20: both fire");
    CHECK(f_FireMonsterEvents(l_vEv, l_oHit).empty(), "once: they do not fire again");
    EventCheck l_oAt; l_oAt.m_strOn = "attacks";
    l_oAt.m_iAttacks = 1; CHECK(f_FireMonsterEvents(l_vEv, l_oAt).empty(), "attack 1 of every 2: no");
    l_oAt.m_iAttacks = 2; CHECK(f_FireMonsterEvents(l_vEv, l_oAt).size() == 1, "attack 2: yes");
    l_oAt.m_iAttacks = 4; CHECK(f_FireMonsterEvents(l_vEv, l_oAt).size() == 1, "attack 4: again (once: false)");
    EventCheck l_oT; l_oT.m_strOn = "everyMs"; l_oT.m_iMsBefore = 900; l_oT.m_iMsAfter = 1016;
    CHECK(f_FireMonsterEvents(l_vEv, l_oT).size() == 1, "every 1000 ms: crossing 1000");
    l_oT.m_iMsBefore = 1016; l_oT.m_iMsAfter = 1032;
    CHECK(f_FireMonsterEvents(l_vEv, l_oT).empty(), "not again until 2000");
    EventCheck l_oS; l_oS.m_strOn = "battleStart";
    CHECK(f_FireMonsterEvents(l_vEv, l_oS).size() == 1 && f_FireMonsterEvents(l_vEv, l_oS).empty(), "battleStart once");

    // 3. Moves and the choice of the next one.
    const MovesTable l_oMoves = f_MovesFromJson(json::parse(R"({
        "heavy": { "kind": "heavy", "damage": 1.8, "time": 1.3 },
        "jabs":  { "kind": "jabs", "damage": 0.45, "hits": 3 },
        "ward":  { "kind": "guard", "reduce": 0.25 }
    })"));
    CHECK(l_oMoves.f_Get("heavy").m_eKind == MoveKind::Heavy && l_oMoves.f_Get("heavy").m_fDamage > 1.7f && l_oMoves.f_Get("heavy").m_fTime > 1.2f,
          "heavy parsed");
    CHECK(l_oMoves.f_Get("jabs").m_iHits == 3 && l_oMoves.f_Get("ward").m_eKind == MoveKind::Guard && l_oMoves.f_Get("ward").m_fReduce < 0.3f,
          "jabs and a guard by another name");
    CHECK(l_oMoves.f_Get("heavy").m_strText == "battle.intent.heavy", "the intent's words default to battle.intent.<kind>");
    CHECK(l_oMoves.f_Get("nothing").m_eKind == MoveKind::Strike, "an unknown move: a strike");
    const std::vector<MoveChoice> l_vCh = f_MoveChoicesFromJson(json::parse(R"(["strike", { "id": "heavy", "weight": 3 }])"));
    CHECK(l_vCh.size() == 2 && l_vCh[1].m_iWeight == 3, "choices parsed");
    CHECK(f_PickMove(l_vCh, 0.0f) == "strike" && f_PickMove(l_vCh, 0.24f) == "strike" && f_PickMove(l_vCh, 0.25f) == "heavy" &&
              f_PickMove(l_vCh, 0.999f) == "heavy", "the weights split the roll");
    CHECK(f_PickMove({}, 0.5f) == "strike", "no moves: a strike");

    // 4. The pace.
    const PaceSettings l_oP = f_PaceFromJson(json::parse(R"({ "pace": "stamina", "stamina": { "max": 80, "attack": 30 } })"));
    CHECK(l_oP.m_ePace == Pace::Stamina && l_oP.m_iStaminaMax == 80 && l_oP.m_iAttackCost == 30 && l_oP.m_iGuardCost == 20, "stamina pace");
    CHECK(f_PaceFromJson(json::object()).m_ePace == Pace::Cooldown, "no pace: cooldowns (as before)");
    CHECK(f_PaceFromString("bogus", Pace::Stamina) == Pace::Stamina && f_PaceFromString("cooldown", Pace::Stamina) == Pace::Cooldown,
          "an override, or the fallback");

    // 5. Break.
    const BreakSettings l_oB = f_BreakFromJson(json::parse(R"({ "break": { "points": 5, "parryPower": 90 } })"));
    CHECK(l_oB.m_iPoints == 5 && l_oB.m_iParryPower == 90 && l_oB.m_iBrokenMs == 3000, "break settings");
    float l_fG = 0.0f;
    CHECK(!f_AddBreak(l_fG, 3, 5) && l_fG == 3.0f, "3 of 5");
    CHECK(f_AddBreak(l_fG, 2, 5) && l_fG == 0.0f, "5 of 5: broken, the gauge empties");

    // 6. The revamp (docs/gameplay/15): the feel, the grade, the timed guard.
    const FeelSettings l_oFl = f_FeelFromJson(json::parse(R"({ "feel": { "strength": 2, "stopMs": { "perfect": 50, "new": 10 },
        "sounds": { "perfect": "battle_perfect" }, "slowMoMs": 300 } })"));
    CHECK(l_oFl.f_Stop("perfect", false) == 100 && l_oFl.f_Stop("new", false) == 20 && l_oFl.f_Stop("hit", false) == 50,
          "stops: the file's, the built-in ones, times the strength");
    CHECK(l_oFl.f_Stop("perfect", true) == 25 && l_oFl.f_Stop("nothing", false) == 0, "reduced: a quarter; an unknown moment: none");
    CHECK(l_oFl.f_Shake("break", false) == 18.0f && l_oFl.f_Sound("perfect") == "battle_perfect" && l_oFl.f_Sound("hit").empty() &&
              l_oFl.m_iSlowMoMs == 300, "shake, sounds, slow motion");
    const GradeSettings l_oGs = f_GradeFromJson(json::object());
    GradeInput l_oGi;
    l_oGi.m_fSeconds = 20; l_oGi.m_fLostShare = 0.0f; l_oGi.m_fPerfectRate = 0.5f; l_oGi.m_iBestChain = 2;
    int l_iSc = 0;
    CHECK(f_BattleGrade(l_oGi, l_oGs, &l_iSc) == "S" && l_iSc == 121, "quick, untouched, half perfect: S");
    l_oGi.m_fSeconds = 45; l_oGi.m_fLostShare = 0.5f; l_oGi.m_fPerfectRate = 0.0f; l_oGi.m_iBestChain = 0;
    CHECK(f_BattleGrade(l_oGi, l_oGs, &l_iSc) == "C" && l_iSc == 40, "slow and hurt: C (100 - 30 - 30)");
    l_oGi.m_fSeconds = 30; l_oGi.m_fLostShare = 0.2f; l_oGi.m_fPerfectRate = 0.2f;
    CHECK(f_BattleGrade(l_oGi, l_oGs) == "A", "in between: A (100 - 7.5 - 12 + 6 = 86.5)");
    const GuardSettings l_oGd = f_GuardFromJson(json::object());
    CHECK(l_oGd.m_strMode == "timing" && f_GuardTiming(-1, l_oGd) == GuardTiming::None && f_GuardTiming(100, l_oGd) == GuardTiming::Parry &&
              f_GuardTiming(400, l_oGd) == GuardTiming::Block && f_GuardTiming(900, l_oGd) == GuardTiming::Early &&
              f_GuardTiming(3000, l_oGd) == GuardTiming::None, "the guard by how long before the hit");
    CHECK(f_GuardFromJson(json::parse(R"({ "guard": { "mode": "bar" } })")).m_strMode == "bar", "the bar kept as an option");
    {   // the "warn" guard (docs/gameplay/16 §4.2): only while the "!" shows; its first 40 % a parry
        const GuardSettings l_oGw = f_GuardFromJson(json::parse(R"({ "guard": { "mode": "warn", "warnMs": 500 } })"));
        CHECK(l_oGw.m_strMode == "warn" && f_GuardWarn(450, 500, l_oGw) == GuardTiming::Parry && f_GuardWarn(300, 500, l_oGw) == GuardTiming::Parry &&
                  f_GuardWarn(250, 500, l_oGw) == GuardTiming::Block && f_GuardWarn(0, 500, l_oGw) == GuardTiming::Block &&
                  f_GuardWarn(600, 500, l_oGw) == GuardTiming::None && f_GuardWarn(-1, 500, l_oGw) == GuardTiming::None,
              "the warn guard: parry early in the !, block late, nothing before it");
        // a move's rhythm: hits with gaps
        const MovesTable l_oMt = f_MovesFromJson(json::parse(
            R"({ "bite": { "kind": "jabs", "chargeMs": 1600, "hits": 3, "gapsMs": [500] },
                 "frenzy": { "kind": "jabs", "hits": 4, "gapsMs": [150, 600] }, "seal": { "kind": "spell", "guardPlay": "g" } })"));
        CHECK(l_oMt.f_Get("bite").m_iChargeMs == 1600 && l_oMt.f_Get("bite").f_GapBefore(0) == 0 && l_oMt.f_Get("bite").f_GapBefore(2) == 500,
              "one gap for every hit");
        CHECK(l_oMt.f_Get("frenzy").f_GapBefore(1) == 150 && l_oMt.f_Get("frenzy").f_GapBefore(2) == 600 && l_oMt.f_Get("frenzy").f_GapBefore(3) == 600,
              "a gap per hit, the last one repeated");
        CHECK(l_oMt.f_Get("seal").m_strGuardPlay == "g" && l_oMt.f_Get("seal").m_iWarnMs == -1, "a special guard play");
    }

    if (g_iFail) { std::printf("battle_rules_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("battle_rules_test: all passed\n");
    return 0;
}
