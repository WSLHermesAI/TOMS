// combo_play_test.cpp -- Battle v3's point play (combo_play.h) and the weapon levels (weapon_play.h). Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the data file is "data/weapon_play.json".
#include "combo_play.h"

#include <cmath>
#include <cstdio>

using namespace toms;

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

// Runs the marker until it is within `w` of position p (or the combo ends).
void f_RunTo(ComboPlay& l_oC, float l_fP, float l_fW = 0.01f) {
    for (int l_iI = 0; l_iI < 20000 && l_oC.f_Running() && std::fabs(l_oC.f_Marker() - l_fP) > l_fW; l_iI++) l_oC.f_Update(1);
}
}  // namespace

int main() {
    // 1. Weapon levels: points and bonuses by level, the level from the wins.
    {
        const WeaponPlay l_oW = f_WeaponPlayFromJson("bow", nlohmann::json::parse(R"({
            "levels": [ {"lv": 3, "points": 4, "stats": {"str": 12, "agi": 10}}, {"lv": 1, "points": 3, "stats": {"str": 10}},
                        {"lv": 5, "points": 9} ],
            "usesToLevel": [2, 3, 4, 5]})"));
        CHECK(l_oW.m_vLevels.size() == 3 && l_oW.m_vLevels[0].m_iLv == 1, "levels sorted");
        CHECK(f_WeaponLevelEntry(l_oW, 1).m_iPoints == 3 && f_WeaponLevelEntry(l_oW, 2).m_iPoints == 3 && f_WeaponLevelEntry(l_oW, 3).m_iPoints == 4,
              "the highest entry at or below the level");
        CHECK(f_WeaponLevelEntry(l_oW, 5).m_iPoints == g_iMaxComboPoints, "points capped at five");
        CHECK(f_WeaponLevelEntry(l_oW, 3).m_mapStats.at("agi") == 10, "the level's stats");
        CHECK(f_WeaponLevelFromUses(l_oW, 0) == 1 && f_WeaponLevelFromUses(l_oW, 1) == 1 && f_WeaponLevelFromUses(l_oW, 2) == 2 &&
                  f_WeaponLevelFromUses(l_oW, 5) == 3 && f_WeaponLevelFromUses(l_oW, 999) == 5,
              "levels from wins (2, then 3 more, ...), capped at the last entry");
        CHECK(f_UsesToNextLevel(l_oW, 1) == 2 && f_UsesToNextLevel(l_oW, 2) == 3 && f_UsesToNextLevel(l_oW, 5) == 0, "wins to the next level");
        const WeaponPlay l_oNone = f_WeaponPlayFromJson("x", nlohmann::json::object());
        CHECK(f_WeaponLevelEntry(l_oNone, 7).m_iPoints == 3 && f_WeaponMaxLevel(l_oNone) == 1 && l_oNone.m_bTurnBack, "no data: Lv 1, 3 points");
    }

    ComboSettings l_oS;
    l_oS.m_iPoints = 3;
    l_oS.m_iSweepMs = 1000;
    l_oS.m_iMaxSweeps = 3;
    l_oS.m_fHitWindow = 0.05f;
    l_oS.m_iMissBack = -1;
    // 2. Three points at 1/4, 2/4, 3/4; hit them all: the combo ends at once, power 100.
    {
        ComboPlay l_oC;
        l_oC.f_Start(l_oS, 0.0f);
        CHECK(l_oC.f_Running() && l_oC.f_PointsAt().size() == 3 && std::fabs(l_oC.f_PointsAt()[1] - 0.5f) < 1e-5f, "points evenly on the track");
        for (float l_fP : {0.25f, 0.5f, 0.75f}) {
            f_RunTo(l_oC, l_fP);
            CHECK(l_oC.f_Tap() == ComboPlay::Tap::Hit, "a tap on a point hits it");
        }
        CHECK(l_oC.f_Done() && l_oC.f_Result().m_iHits == 3 && l_oC.f_Result().f_Power() == 100.0f && !l_oC.f_Result().m_bFailed, "all hit: done, full power");
    }
    // 3. Charge makes the marker faster: with a full charge (speedUp 1) the first point comes in half the time.
    {
        ComboPlay l_oSlow, l_oFast;
        l_oSlow.f_Start(l_oS, 0.0f);
        l_oFast.f_Start(l_oS, 1.0f);
        l_oSlow.f_Update(100);
        l_oFast.f_Update(100);
        CHECK(std::fabs(l_oFast.f_Marker() - 2.0f * l_oSlow.f_Marker()) < 1e-4f, "a full charge: twice as fast");
        CHECK(std::fabs(l_oFast.f_Result().m_fCharge) < 1e-6f && l_oFast.f_Running(), "the charge is reported at the end");
    }
    // 4. A miss takes the combo back (to the start here); two in a row are a fail.
    {
        ComboPlay l_oC;
        l_oC.f_Start(l_oS, 0.5f);
        f_RunTo(l_oC, 0.25f);
        l_oC.f_Tap();
        f_RunTo(l_oC, 0.4f);
        CHECK(l_oC.f_Tap() == ComboPlay::Tap::Miss && l_oC.f_Hits() == 0 && l_oC.f_MissesInRow() == 1, "a miss: back to the start");
        f_RunTo(l_oC, 0.5f);
        CHECK(l_oC.f_Tap() == ComboPlay::Tap::Hit && l_oC.f_MissesInRow() == 0, "a hit resets the misses in a row");
        f_RunTo(l_oC, 0.6f);
        l_oC.f_Tap();
        f_RunTo(l_oC, 0.65f);
        CHECK(l_oC.f_Tap() == ComboPlay::Tap::Miss && l_oC.f_Done() && l_oC.f_Result().m_bFailed && l_oC.f_Result().f_Power() == 0.0f,
              "two misses in a row: a fail (power 0, a plain attack)");
    }
    // 5. missBack 1 takes back only the newest hit; missWaitMs ignores taps for a while.
    {
        ComboSettings l_oB = l_oS;
        l_oB.m_iMissBack = 1;
        l_oB.m_iMissWaitMs = 200;
        ComboPlay l_oC;
        l_oC.f_Start(l_oB, 0.0f);
        f_RunTo(l_oC, 0.25f);
        l_oC.f_Tap();
        f_RunTo(l_oC, 0.5f);
        l_oC.f_Tap();
        f_RunTo(l_oC, 0.6f);
        CHECK(l_oC.f_Tap() == ComboPlay::Tap::Miss && l_oC.f_Hits() == 1 && l_oC.f_PointsHit()[0] && !l_oC.f_PointsHit()[1], "back one: the newest hit lost");
        CHECK(l_oC.f_Waiting() && l_oC.f_Tap() == ComboPlay::Tap::Ignored, "after a miss, taps wait");
    }
    // 6. Turning back: a missed point can be taken on the way back; the sweeps run out.
    {
        ComboPlay l_oC;
        l_oC.f_Start(l_oS, 0.0f);
        f_RunTo(l_oC, 0.75f);
        l_oC.f_Tap();               // the last point first: the order is free
        f_RunTo(l_oC, 1.0f, 0.0f);  // the end: the marker turns back
        l_oC.f_Update(1);
        f_RunTo(l_oC, 0.25f);
        CHECK(l_oC.f_Running() && l_oC.f_Tap() == ComboPlay::Tap::Hit && l_oC.f_Hits() == 2, "turned back: point 1 taken on the way back");
        while (l_oC.f_Running()) l_oC.f_Update(10);
        CHECK(l_oC.f_Done() && !l_oC.f_Result().m_bFailed && l_oC.f_Result().m_iHits == 2 && std::fabs(l_oC.f_Result().f_Power() - 200.0f / 3.0f) < 0.01f,
              "the sweeps ran out: the hits so far");
    }
    // 7. No turning back (the sword): one sweep ends the combo.
    {
        ComboSettings l_oOne = l_oS;
        l_oOne.m_bTurnBack = false;
        ComboPlay l_oC;
        l_oC.f_Start(l_oOne, 0.0f);
        int l_iMs = 0;
        while (l_oC.f_Running() && l_iMs < 5000) { l_oC.f_Update(10); l_iMs += 10; }
        CHECK(l_oC.f_Done() && l_iMs <= 1010 && l_oC.f_Result().m_iHits == 0, "one sweep, then done");
    }
    // 8. The shipped data: every weapon has levels with points, and the starting bow starts at 3 points.
    {
        const WeaponPlayTable l_oT = f_LoadWeaponPlay("data/weapon_play.json");
        const WeaponPlay& l_oBow = l_oT.f_Get("bow");
        CHECK(!l_oBow.m_vLevels.empty() && f_WeaponLevelEntry(l_oBow, 1).m_iPoints == 3, "bow: Lv 1 has 3 points");
        CHECK(f_WeaponLevelEntry(l_oBow, f_WeaponMaxLevel(l_oBow)).m_iPoints == g_iMaxComboPoints, "bow: the top level has five");
        CHECK(!l_oBow.m_vUsesToLevel.empty(), "bow: wins per level");
        const ComboSettings l_oCs = f_ComboSettings(l_oBow, 1);
        CHECK(l_oCs.m_iPoints == 3 && l_oCs.m_iSweepMs == l_oBow.m_iSweepMs, "comboSettings from the weapon");
    }

    // 9. The timing help (the owner 2026-10-24): a window at least hitWindowMs long, a late tap still counting, never
    //    more than 45% of the gap, and the hint when the marker enters a window.
    {
        ComboSettings l_oT = l_oS;   // 3 points at 1/4, 2/4, 3/4; 1000 ms a sweep: the marker moves 0.001 a ms
        l_oT.m_fHitWindow = 0.02f;
        l_oT.m_iHitWindowMs = 100;
        ComboPlay l_oC;
        l_oC.f_Start(l_oT, 0.0f);
        CHECK(std::fabs(l_oC.f_Window() - 0.1f) < 1e-4f, "hitWindowMs: 100 ms of travel is 0.1 of the track");
        f_RunTo(l_oC, 0.16f, 0.0005f);
        CHECK(l_oC.f_Tap() == ComboPlay::Tap::Hit, "a tap 0.09 before a point hits it with the time window");
        ComboPlay l_oFull;
        l_oFull.f_Start(l_oT, 1.0f);   // twice as fast: 100 ms is 0.2, but the cap is 45% of the 0.25 gap
        CHECK(std::fabs(l_oFull.f_Window() - 0.1125f) < 1e-4f, "the window is capped at 45% of the gap between points");
        l_oT.m_iHitWindowMs = 0;
        ComboPlay l_oPlain;
        l_oPlain.f_Start(l_oT, 0.0f);
        f_RunTo(l_oPlain, 0.16f, 0.0005f);
        CHECK(l_oPlain.f_Tap() == ComboPlay::Tap::Miss, "without it the same tap misses");

        ComboSettings l_oL = l_oS;
        l_oL.m_fHitWindow = 0.05f;
        l_oL.m_iLateMs = 100;   // 0.1 of the track at this speed
        ComboPlay l_oLate;
        l_oLate.f_Start(l_oL, 0.0f);
        f_RunTo(l_oLate, 0.37f, 0.0005f);   // 0.12 past the first point
        CHECK(l_oLate.f_Tap() == ComboPlay::Tap::Hit, "lateMs: a tap just after the marker passed a point hits it");
        ComboPlay l_oEarly;
        l_oEarly.f_Start(l_oL, 0.0f);
        f_RunTo(l_oEarly, 0.13f, 0.0005f);  // 0.12 before it: the grace is only for late taps
        CHECK(l_oEarly.f_Tap() == ComboPlay::Tap::Miss, "lateMs does not make early taps count");

        ComboSettings l_oFive = l_oS;
        l_oFive.m_iPoints = 5;
        l_oFive.m_fHitWindow = 0.2f;
        ComboPlay l_oF;
        l_oF.f_Start(l_oFive, 0.0f);
        CHECK(std::fabs(l_oF.f_Window() - 0.45f / 6.0f) < 1e-4f, "five points: the window stays inside the gap");

        ComboPlay l_oH;
        l_oH.f_Start(l_oS, 0.0f);   // hitWindow 0.05
        f_RunTo(l_oH, 0.25f, 0.001f);
        CHECK(l_oH.f_NearPoint() == 0, "nearPoint: the marker is in the first point's window");
        std::vector<int> l_vIn = l_oH.f_TakeEntered();
        CHECK(l_vIn.size() == 1 && l_vIn[0] == 0, "takeEntered: the first point's window, once");
        f_RunTo(l_oH, 0.35f, 0.001f);
        CHECK(l_oH.f_NearPoint() == -1 && l_oH.f_TakeEntered().empty(), "out of it: nothing near, nothing entered");
        l_oH.f_Tap();   // a miss here (nothing near) -- then the second point
        f_RunTo(l_oH, 0.5f, 0.001f);
        l_vIn = l_oH.f_TakeEntered();
        CHECK(l_vIn.size() == 1 && l_vIn[0] == 1, "the next window entered: the second point");
    }

    if (g_iFail) { std::printf("combo_play_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("combo_play_test: OK\n");
    return 0;
}
