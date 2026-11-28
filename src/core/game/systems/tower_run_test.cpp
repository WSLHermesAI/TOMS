// tower_run_test.cpp -- the tower in climbs (tower_run.h; docs/gameplay/27 §3): the stamina clock, the rules file, the
// score and rank. Exits 0 on success. Runs in assets/ (tests/CMakeLists.txt), so the shipped data is "data/...".
#include "tower_run.h"

#include <cstdio>
#include <fstream>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    StaminaRules l_oR;   // 60, one every 300 s, a fight 1 (the owner, 2026-11-28)
    {   // full: no timer; a fight from full starts it
        StaminaClock l_oC;
        CHECK(!l_oC.f_Catch(l_oR, 1000) && l_oC.m_llNextAt == 0, "full: no timer");
        CHECK(l_oC.f_Spend(l_oR, 1000, 1) && l_oC.m_iNow == 59 && l_oC.m_llNextAt == 1300, "a fight from full: 59, the next point at +300 s");
        CHECK(l_oC.f_SecondsToNext(1100) == 200, "200 s to the next point");
        CHECK(l_oC.f_Spend(l_oR, 1010, 1) && l_oC.m_llNextAt == 1300, "another fight keeps the timer running");
        CHECK(!l_oC.f_Catch(l_oR, 1299) && l_oC.m_iNow == 58, "not yet");
        CHECK(l_oC.f_Catch(l_oR, 1300) && l_oC.m_iNow == 59 && l_oC.m_llNextAt == 1600, "the time comes: +1, the next timer");
        CHECK(l_oC.f_Catch(l_oR, 1600) && l_oC.m_iNow == 60 && l_oC.m_llNextAt == 0, "full again: the timer stops");
    }
    {   // the game closed a long time: the points that came meanwhile, never above the maximum
        StaminaClock l_oC;
        l_oC.m_iNow = 10;
        l_oC.m_llNextAt = 5000;
        l_oC.f_Catch(l_oR, 5000 + 300 * 4 + 10);   // 5 points come (at 5000, 5300, ..., 6200)
        CHECK(l_oC.m_iNow == 15 && l_oC.m_llNextAt == 6500, "closed 20 min: +5, the next at 6500");
        l_oC.f_Catch(l_oR, 999999);
        CHECK(l_oC.m_iNow == 60 && l_oC.m_llNextAt == 0, "closed for days: full, no timer");
    }
    {   // none left: no fight
        StaminaClock l_oC;
        l_oC.m_iNow = 0;
        l_oC.m_llNextAt = 2000;
        CHECK(!l_oC.f_Spend(l_oR, 1500, 1) && l_oC.m_iNow == 0, "0: the fight does not start");
    }
    {   // the file: written and read back; a missing one is full
        StaminaClock l_oC;
        l_oC.m_iNow = 33;
        l_oC.m_llNextAt = 123456;
        const StaminaClock l_oBack = f_StaminaFromJson(f_StaminaToJson(l_oC), l_oR);
        CHECK(l_oBack.m_iNow == 33 && l_oBack.m_llNextAt == 123456, "the file round trip");
        CHECK(f_StaminaFromJson(nlohmann::json(), l_oR).m_iNow == 60, "no file: full");
    }
    {   // the shipped rules: the story's 7 chapters in order, a challenge after each, ranks best first
        std::ifstream l_oIn("data/tower_run.json");
        CHECK(l_oIn.good(), "data/tower_run.json opens");
        const TowerRunRules l_oT = f_TowerRunFromJson(nlohmann::json::parse(l_oIn, nullptr, false));
        CHECK(l_oT.m_vStories.size() == 2, "two stories: the orphan's and the challenges");
        if (l_oT.m_vStories.size() == 2) {
            const TowerStory& l_oSt = l_oT.m_vStories[0];
            CHECK(l_oSt.m_vChapters.size() == 7 && l_oSt.m_vChapters[0].m_strId == "ch_01" && l_oSt.m_vChapters[0].m_strAfter.empty() &&
                      l_oSt.m_vChapters[1].m_strAfter == "ch_01", "ch_01 first, ch_02 after it");
            const TowerStory& l_oCh = l_oT.m_vStories[1];
            CHECK(l_oCh.m_bChallenge && !l_oCh.m_vChapters.empty() && l_oCh.m_vChapters[0].m_strAfter == "ch_01" &&
                      l_oCh.m_vChapters[0].m_strFloorsOf == "ch_01" && l_oCh.m_vChapters[0].m_fHpX > 1.0f, "the first challenge: ch_01's floors, after it, stronger");
        }
        CHECK(l_oT.m_oStamina.m_iMax == 60 && l_oT.m_oStamina.m_iEverySec == 300 && l_oT.m_oStamina.m_iPerFight == 1, "the owner's stamina numbers");
        CHECK(l_oT.m_fKeepGoldOnDeath == 0.5f, "a death keeps half the gold");
        CHECK(!l_oT.m_vRanks.empty() && l_oT.m_vRanks.front().m_strRank == "S", "the ranks, S first");
        ClimbStats l_oS;
        l_oS.m_iFloors = 5; l_oS.m_iMonsters = 20; l_oS.m_iBestCombo = 12; l_oS.m_iPerfects = 6;
        const int l_iGood = f_ClimbScore(l_oS, l_oT);
        l_oS.m_iDamageTaken = 3000; l_oS.m_iSeconds = 1800;
        CHECK(f_ClimbScore(l_oS, l_oT) < l_iGood, "damage taken and time lower the score");
        CHECK(f_RankFor(0, l_oT) == &l_oT.m_vRanks.back(), "0 points: the lowest rank");
        CHECK(f_FindChapter(l_oT, "ch_03") != nullptr && f_FindChapter(l_oT, "nope") == nullptr, "chapters by id");
    }
    if (g_iFail) { std::printf("%d check(s) failed\n", g_iFail); return 1; }
    std::printf("tower_run_test: all checks passed\n");
    return 0;
}
