// touch_play_test.cpp -- the plays (touch_play.h): bow, slice, circles, shape, mash. Exits 0 on success.
#include "touch_play.h"
#include "special_moves.h"

#include <cmath>
#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

static void f_Swipe(TouchPlay& l_oP, float l_fX, float l_fY, float l_fDeg, float l_fLen) {
    const float l_fA = l_fDeg * 3.14159265f / 180.0f;
    l_oP.f_Pointer(0, l_fX, l_fY);
    l_oP.f_Pointer(1, l_fX + std::cos(l_fA) * l_fLen * 0.5f, l_fY + std::sin(l_fA) * l_fLen * 0.5f);
    l_oP.f_Pointer(2, l_fX + std::cos(l_fA) * l_fLen, l_fY + std::sin(l_fA) * l_fLen);
}

int main() {
    const glm::vec4 l_v4Area(500, 100, 300, 200);
    // 1. Bow: the target moves 0.5 s, then stops; a pull charges and shakes; a normal attack is one arrow.
    {
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::ePULL_RELEASE, 1, false, l_v4Area, 7);
        CHECK(l_oP.f_Running() && !l_oP.f_Started(), "the bow waits for its pull");
        const auto l_oA = l_oP.f_Target();
        l_oP.f_Update(250);
        const auto l_oB = l_oP.f_Target();
        l_oP.f_Update(400);
        const auto l_oC = l_oP.f_Target();
        l_oP.f_Update(400);
        const auto l_oD = l_oP.f_Target();
        CHECK(std::hypot(l_oB.m_fX - l_oA.m_fX, l_oB.m_fY - l_oA.m_fY) > 1.0f, "the target moves at first");
        CHECK(std::hypot(l_oD.m_fX - l_oC.m_fX, l_oD.m_fY - l_oC.m_fY) < 0.01f, "then stops (after 0.5 s)");
        CHECK(l_oD.m_fX >= l_v4Area.x && l_oD.m_fX <= l_v4Area.x + l_v4Area.z && l_oD.m_fY >= l_v4Area.y && l_oD.m_fY <= l_v4Area.y + l_v4Area.w, "in the area");
        // pull 50 px straight down from 90 px under it: the crosshair (no shake at once) on the target
        l_oP.f_Pointer(0, l_oD.m_fX, l_oD.m_fY + 90);
        l_oP.f_Pointer(1, l_oD.m_fX, l_oD.m_fY + 140);
        CHECK(l_oP.f_Aiming() && l_oP.f_Charge() == 0.0f && std::fabs(l_oP.f_Crosshair().y - l_oD.m_fY) < 0.01f, "aimed, not charged yet");
        l_oP.f_Update(750);
        CHECK(std::fabs(l_oP.f_Charge() - 0.5f) < 0.01f, "half the 1.5 s limit: half charged");
        l_oP.f_Pointer(2, l_oD.m_fX, l_oD.m_fY + 140);
        const ComboResult l_oR = l_oP.f_Result();
        CHECK(l_oP.f_Done() && l_oR.m_iHits == 1 && l_oR.m_iPoints == 1 && std::fabs(l_oP.f_ShotCharge() - 0.5f) < 0.01f, "one arrow, charged half");
    }
    {   // held to the charge limit: the arrow goes by itself, fully charged (Q41)
        TouchPlaySettings l_oS;
        l_oS.m_fBowShakePx = 0.0f;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::ePULL_RELEASE, 1, false, l_v4Area, 3, l_oS);
        l_oP.f_Update(600);
        const auto l_oT = l_oP.f_Target();
        l_oP.f_Pointer(0, l_oT.m_fX, l_oT.m_fY + 90);
        l_oP.f_Pointer(1, l_oT.m_fX, l_oT.m_fY + 140);
        l_oP.f_Update(1600);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 1 && l_oP.f_ShotCharge() == 1.0f, "the charge limit lets it go at full charge");
    }
    {   // a short pull is not a shot; an arrow not shot in time is a miss
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::ePULL_RELEASE, 1, false, l_v4Area, 4);
        l_oP.f_Pointer(0, 600, 300); l_oP.f_Pointer(2, 605, 300);
        CHECK(l_oP.f_Started() && l_oP.f_Index() == 0, "too short a pull: no shot");
        l_oP.f_Update(4100);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_bFailed, "the arrow's time ran out: a miss");
    }
    // 2. Slice: one prompt at a time; only its last 0.5 s take the answer; each answer is a hit at once.
    {
        TouchPlaySettings l_oS;
        l_oS.m_vSliceCues = {"way", "way", "triangle"};
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eSWIPE_WAY, 3, false, l_v4Area, 11, l_oS);
        CHECK(l_oP.f_Started() && l_oP.f_CueKind(2) == "triangle", "the sword starts at once; its third prompt a triangle");
        f_Swipe(l_oP, 650, 200, l_oP.f_CueDir(0) * 45.0f, 160);
        CHECK(l_oP.f_Index() == 0, "a swipe before the window: nothing");
        l_oP.f_Update(600);   // the window is open (500..1000)
        f_Swipe(l_oP, 650, 200, l_oP.f_CueDir(0) * 45.0f, 160);
        auto l_vHits = l_oP.f_TakeHits();
        CHECK(l_oP.f_Index() == 1 && l_vHits.size() == 1 && l_vHits[0].m_bOk && l_vHits[0].m_bPerfect, "the right way in the window: a hit, at once");
        l_oP.f_Update(150 + 1000);   // the next prompt passes unanswered
        l_vHits = l_oP.f_TakeHits();
        CHECK(l_oP.f_Index() == 2 && l_vHits.size() == 1 && !l_vHits[0].m_bOk, "a prompt let pass: a miss");
        l_oP.f_Update(600);
        std::vector<glm::vec2> l_vTri = {{650, 150}, {700, 250}, {600, 250}, {650, 150}};
        l_oP.f_Pointer(0, l_vTri[0].x, l_vTri[0].y);
        for (float l_fT = 0.0f; l_fT <= 3.0f; l_fT += 0.1f) {
            const int l_iK = std::min(2, (int)l_fT);
            const glm::vec2 l_v2Q = l_vTri[(size_t)l_iK] + (l_vTri[(size_t)l_iK + 1] - l_vTri[(size_t)l_iK]) * (l_fT - (float)l_iK);
            l_oP.f_Pointer(1, l_v2Q.x, l_v2Q.y);
        }
        l_oP.f_Pointer(2, l_vTri[3].x, l_vTri[3].y);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 2 && l_oP.f_Result().m_iPoints == 3, "a triangle drawn: the third prompt cut");
    }
    // 3. Circles: count, hide, back one by one (numbered, already to be tapped), then tapped in the remembered order; each tap
    // a spell (docs/gameplay/20 §2).
    {
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eSEQUENCE_CLICK, 3, false, l_v4Area, 9);
        CHECK(l_oP.f_Started() && l_oP.f_CirclePhase() == TouchPlay::CirclePhase::Count && l_oP.f_CirclesLit() == 1, "starts by itself, counting");
        const auto l_vC = l_oP.f_Circles();
        for (size_t l_nI = 0; l_nI < l_vC.size(); l_nI++)
            for (size_t l_nJ = l_nI + 1; l_nJ < l_vC.size(); l_nJ++) CHECK(std::hypot(l_vC[l_nI].m_fX - l_vC[l_nJ].m_fX, l_vC[l_nI].m_fY - l_vC[l_nJ].m_fY) >= 2.6f * 34.0f, "apart");
        l_oP.f_Update(500);
        CHECK(l_oP.f_CirclesLit() == 2, "the second lit");
        l_oP.f_Pointer(0, l_vC[0].m_fX, l_vC[0].m_fY);
        CHECK(l_oP.f_Index() == 0, "no tap while counting");
        l_oP.f_Update(900);    // 1400: hiding
        CHECK(l_oP.f_CirclePhase() == TouchPlay::CirclePhase::Hide, "then hidden");
        l_oP.f_Update(500);    // 1900: the first back, with its number, already to be tapped (no "!" by default)
        CHECK(l_oP.f_CirclePhase() == TouchPlay::CirclePhase::Tap && l_oP.f_CirclesShown() == 1 && l_oP.f_CircleNumbersShown(), "back one by one, numbered");
        l_oP.f_Pointer(0, l_vC[1].m_fX, l_vC[1].m_fY);
        CHECK(l_oP.f_Index() == 0, "a circle not back yet is not tapped");
        l_oP.f_Pointer(0, l_vC[0].m_fX, l_vC[0].m_fY);
        CHECK(l_oP.f_Index() == 1, "the first is tapped while the others come back");
        l_oP.f_Update(200);    // 2100: all back
        CHECK(l_oP.f_CirclesShown() == 3 && !l_oP.f_CircleNumbersShown(), "all back: the numbers go");
        l_oP.f_Update(500);
        l_oP.f_Pointer(0, l_vC[2].m_fX, l_vC[2].m_fY);   // the wrong one: the second is missed
        l_oP.f_Pointer(0, l_vC[2].m_fX, l_vC[2].m_fY);   // now the third is due
        const auto l_vHits = l_oP.f_TakeHits();
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 2 && l_oP.f_Circles()[1].m_iState == 3 && l_vHits.size() == 3, "in order; a wrong one misses the due one");
        CHECK(l_oP.f_Circles()[0].m_iState == 2 && l_oP.f_Circles()[2].m_iState == 2, "quick taps are perfect");
        CHECK(l_vHits[0].m_iOrder == 0 && l_vHits[2].m_iOrder == 2, "each answer knows which it is (the staff's damage grows along the chain)");
    }
    // 4. Shape: the pentagram traced corner by corner (5 steps); time up keeps what is done.
    {
        TouchPlaySettings l_oS;
        l_oS.m_iShapeTimeMs = 3000;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eDRAW_SHAPE, 0, true, l_v4Area, 1, l_oS);
        CHECK(l_oP.f_Points() == 5 && l_oP.f_Trace().f_Points().size() == 6, "a pentagram: 5 steps");
        const auto& l_vPts = l_oP.f_Trace().f_Points();
        l_oP.f_Pointer(0, l_vPts[0].x, l_vPts[0].y);
        for (int l_iK = 1; l_iK <= 3; l_iK++) l_oP.f_Pointer(1, l_vPts[(size_t)l_iK].x, l_vPts[(size_t)l_iK].y);
        CHECK(l_oP.f_Index() == 3 && l_oP.f_TakeHits().size() == 3, "three corners: three steps");
        l_oP.f_Update(3000);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 3 && !l_oP.f_Result().m_bFailed, "time up: 3 of 5");
    }
    {   // mash
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eMASH, 4, false, l_v4Area, 1);
        for (int l_iK = 0; l_iK < 4; l_iK++) l_oP.f_Pointer(0, 600, 200);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 4, "four taps");
    }
    CHECK(f_PlayTypeFromString("bow") == PlayType::ePULL_RELEASE && f_PlayTypeFromString("shape") == PlayType::eDRAW_SHAPE &&
              f_PlayTypeFromString("anything") == PlayType::eTRACK && std::string(f_PlayTypeName(PlayType::eMASH)) == "mash", "names (the older data names too)");
    {   // the staff's slide (the owner, 2026-11-27): touch the first circle, move on through the rest in order without lifting
        TouchPlaySettings l_oS;
        l_oS.m_vCircleSymbol = {{0, 0.5f}, {0.5f, 0.5f}, {1, 0.5f}};   // a row: (134, 250), (350, 250), (566, 250) in the area below
        const glm::vec4 l_v4A(100, 100, 500, 300);
        auto l_fnReady = [&](TouchPlay& l_oP) {   // on to the taps, all three back
            for (int l_iK = 0; l_iK < 400 && !(l_oP.f_CirclePhase() == TouchPlay::CirclePhase::Tap && l_oP.f_CirclesShown() == 3); l_iK++) l_oP.f_Update(10);
        };
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eSEQUENCE_CLICK, 3, false, l_v4A, 1, l_oS);
        l_fnReady(l_oP);
        const auto& l_vC = l_oP.f_Circles();
        l_oP.f_Pointer(0, l_vC[0].m_fX, l_vC[0].m_fY);
        for (float l_fX = l_vC[0].m_fX; l_fX <= l_vC[2].m_fX; l_fX += 10.0f) l_oP.f_Pointer(1, l_fX, l_vC[0].m_fY);   // along the row
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 3, "one touch, slid through all three in order: three hits");

        l_oS.m_vCircleSymbol = {{0, 0.5f}, {1, 0.5f}, {0.5f, 0.5f}};   // the second is at the right: the way there passes the third's middle
        TouchPlay l_oQ;
        l_oQ.f_Start(PlayType::eSEQUENCE_CLICK, 3, false, l_v4A, 1, l_oS);
        l_fnReady(l_oQ);
        const auto& l_vD = l_oQ.f_Circles();
        l_oQ.f_Pointer(0, l_vD[0].m_fX, l_vD[0].m_fY);
        for (float l_fX = l_vD[0].m_fX; l_fX <= l_vD[1].m_fX; l_fX += 10.0f) l_oQ.f_Pointer(1, l_fX, l_vD[0].m_fY);
        CHECK(l_oQ.f_Result().m_iHits == 1 && l_oQ.f_Index() == 2, "through a wrong circle's middle: the one due is missed, and the slide ends");
        l_oQ.f_Pointer(1, l_vD[2].m_fX, l_vD[2].m_fY);
        CHECK(l_oQ.f_Result().m_iHits == 1, "after it ends, moving onto the next does not take it");
        l_oQ.f_Pointer(2, l_vD[2].m_fX, l_vD[2].m_fY);
        l_oQ.f_Pointer(0, l_vD[2].m_fX, l_vD[2].m_fY);
        CHECK(l_oQ.f_Done() && l_oQ.f_Result().m_iHits == 2, "lift and touch it: taken");

        l_oS.m_bCircleSlide = false;
        l_oS.m_vCircleSymbol = {{0, 0.5f}, {0.5f, 0.5f}, {1, 0.5f}};
        TouchPlay l_oR;
        l_oR.f_Start(PlayType::eSEQUENCE_CLICK, 3, false, l_v4A, 1, l_oS);
        l_fnReady(l_oR);
        l_oR.f_Pointer(0, l_oR.f_Circles()[0].m_fX, l_oR.f_Circles()[0].m_fY);
        for (float l_fX = l_oR.f_Circles()[0].m_fX; l_fX <= l_oR.f_Circles()[2].m_fX; l_fX += 10.0f) l_oR.f_Pointer(1, l_fX, l_oR.f_Circles()[0].m_fY);
        CHECK(l_oR.f_Result().m_iHits == 1 && !l_oR.f_Done(), "circleSlide off: only taps count");
    }
    {   // the long bow (docs/gameplay/26): the line from the bow hand grows by time, stops at the target's distance; the rings judge it
        TouchPlaySettings l_oS;
        l_oS.m_fBowShakePx = 0.0f;
        l_oS.m_fBowGrowPerSec = 0.30f;
        l_oS.m_v2BowOrigin = {100.0f, 500.0f};
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::ePULL_RELEASE, 1, false, glm::vec4(500, 100, 300, 200), 7, l_oS);
        l_oP.f_Update(600);   // the target has stopped
        const float l_fD = l_oP.f_TargetDistance();
        l_oP.f_Pointer(0, 900, 650);   // a press anywhere: the line starts at the bow hand
        l_oP.f_Update(1000);
        CHECK(std::fabs(l_oP.f_AimLength() - 0.30f * l_fD) < 1.0f && !l_oP.f_AimStopped(), "after 1 s: 30 % of the way");
        const glm::vec2 l_v2Tip = l_oP.f_Crosshair(), l_v2O = l_oP.f_BowOrigin();
        const glm::vec2 l_v2T(l_oP.f_Target().m_fX, l_oP.f_Target().m_fY);
        CHECK(std::fabs((l_v2Tip.x - l_v2O.x) * (l_v2T.y - l_v2O.y) - (l_v2Tip.y - l_v2O.y) * (l_v2T.x - l_v2O.x)) < 1.0f * l_fD, "with no drag it points at the target");
        l_oP.f_Update(3000);
        CHECK(l_oP.f_AimStopped() && std::fabs(l_oP.f_AimLength() - l_fD) < 1.0f, "it stops at the target's distance, and grows no more");
        CHECK(l_oP.f_Running() && !l_oP.f_Done(), "no charge limit: still aiming");
        CHECK(l_oP.f_TipTier() >= 1.2f, "on the middle: the perfect ring");
        l_oP.f_Pointer(1, 900, 650 + 10);   // a small drag turns it a little
        CHECK(l_oP.f_TipTier() < 1.2f, "a small drag moves the tip off the middle");
        l_oP.f_Pointer(1, 900, 650);
        l_oP.f_Pointer(2, 900, 650);
        const auto l_vHits = l_oP.f_TakeHits();
        CHECK(l_oP.f_Done() && l_vHits.size() == 1 && l_vHits[0].m_bOk && l_vHits[0].m_bPerfect && std::fabs(l_vHits[0].m_fMul - 1.2f) < 1e-4f &&
                  std::fabs(l_oP.f_ShotMul() - 1.2f) < 1e-4f, "let go on the middle: x1.2 (STR bonus 1)");

        // the rings, widened by DEX (Dx) and the force of STR (Sb)
        CHECK(TouchPlay::f_RingTier(4, l_oS) == 1.2f && TouchPlay::f_RingTier(15, l_oS) == 1.0f && TouchPlay::f_RingTier(40, l_oS) == 0.8f &&
                  TouchPlay::f_RingTier(70, l_oS) == 0.5f && TouchPlay::f_RingTier(100, l_oS) == 0.1f && TouchPlay::f_RingTier(130, l_oS) == 0.0f,
              "5 / 15 / 50 / 80 / 120 px: x1.2 / 1.0 / 0.8 / 0.5 / 0.1, beyond x0");
        l_oS.m_fBowRingScale = 1.3f;
        CHECK(TouchPlay::f_RingTier(104, l_oS) == 0.5f && TouchPlay::f_RingTier(6, l_oS) == 1.2f, "DEX widens them (Dx 1.3: 6 px is perfect)");

        // a touch let go at once is not a shot; released short, the arrow falls short
        l_oS.m_fBowRingScale = 1.0f;
        l_oS.m_fBowStrBonus = 1.15f;
        TouchPlay l_oQ;
        l_oQ.f_Start(PlayType::ePULL_RELEASE, 1, false, glm::vec4(500, 100, 300, 200), 7, l_oS);
        l_oQ.f_Update(600);
        l_oQ.f_Pointer(0, 900, 650);
        l_oQ.f_Update(50);
        l_oQ.f_Pointer(2, 900, 650);
        CHECK(l_oQ.f_Index() == 0, "a quick touch: no shot");
        l_oQ.f_Pointer(0, 900, 650);
        l_oQ.f_Update(500);
        l_oQ.f_Pointer(2, 900, 650);
        CHECK(l_oQ.f_Done() && l_oQ.f_Result().m_iHits == 0 && l_oQ.f_ShotMul() == 0.0f, "let go at 15 % of the way: short, x0");
    }
    {   // the long bow's Taichi (docs/gameplay/26 §6): turns either way count; the damage by turns done
        TouchPlaySettings l_oS;
        l_oS.m_bSpinFree = true;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eSPIN_CIRCLE, 10, true, glm::vec4(300, 100, 340, 340), 1, l_oS);
        const glm::vec2 l_v2C = l_oP.f_SpinCentre();
        auto l_fnTurn = [&](float l_fFrom, float l_fBy) {   // the finger round the middle from one angle by so many degrees
            const int l_iSteps = (int)std::ceil(std::fabs(l_fBy) / 10.0f);
            for (int l_iK = 0; l_iK <= l_iSteps; l_iK++) {
                const float l_fA = (l_fFrom + l_fBy * (float)l_iK / (float)l_iSteps) / 57.29578f;
                l_oP.f_Pointer(l_iK == 0 ? 0 : 1, l_v2C.x + std::cos(l_fA) * 120.0f, l_v2C.y + std::sin(l_fA) * 120.0f);
            }
            l_oP.f_Pointer(2, l_v2C.x, l_v2C.y);
        };
        l_fnTurn(0.0f, 2.0f * 360.0f);
        CHECK(l_oP.f_Result().m_iHits == 2 && l_oP.f_SpinAngle() > 700.0f, "two turns clockwise: 2, and the picture turned with them");
        l_fnTurn(0.0f, -3.0f * 360.0f);
        CHECK(l_oP.f_Result().m_iHits == 5, "three anticlockwise count too: 5");
        l_oP.f_Update(l_oS.m_iSpinTimeMs + 100);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 5, "time up: 5 turns done");
        SkillPlaySpec l_oSpec;
        l_oSpec.m_vTurnDamage = {{1, 1.1f}, {2, 1.2f}, {3, 1.5f}, {4, 1.6f}, {5, 1.8f}, {6, 2.0f}, {8, 3.0f}, {10, 5.0f}};
        CHECK(f_TurnDamage(l_oSpec, 0) == 0.0f && f_TurnDamage(l_oSpec, 1) == 1.1f && f_TurnDamage(l_oSpec, 5) == 1.8f && f_TurnDamage(l_oSpec, 7) == 2.0f &&
                  f_TurnDamage(l_oSpec, 9) == 3.0f && f_TurnDamage(l_oSpec, 10) == 5.0f && f_TurnDamage(l_oSpec, 14) == 5.0f,
              "0 turns: none; 1 x1.1, 5 x1.8, 7 counts as 6 (x2), 9 as 8 (x3), 10 and more x5");
    }
    {   // a monster about to attack takes the hold away (the owner, 2026-11-27): no shot, and the arrow gets its time again
        TouchPlaySettings l_oS;
        l_oS.m_fBowGrowPerSec = 0.30f;
        l_oS.m_v2BowOrigin = {100.0f, 500.0f};
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::ePULL_RELEASE, 1, false, glm::vec4(500, 100, 300, 200), 7, l_oS);
        l_oP.f_Update(600);
        l_oP.f_Pointer(0, 900, 650);
        l_oP.f_Update(3000);
        l_oP.f_CancelHold();
        CHECK(!l_oP.f_Aiming() && l_oP.f_Index() == 0 && l_oP.f_AimLength() == 0.0f, "the hold taken away: no shot, the line gone");
        l_oP.f_Pointer(2, 900, 650);
        CHECK(l_oP.f_Index() == 0, "letting go afterwards shoots nothing");
        l_oP.f_Update(l_oS.m_iBowTimeMs - 100);
        CHECK(l_oP.f_Running() && l_oP.f_Index() == 0, "the arrow's 4 s start again");
    }
    {   // a chain with a shape (the memory blade): a circle drawn in one stroke is taken
        TouchPlaySettings l_oS;
        l_oS.m_vSliceCues = {"circle"};
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eSWIPE_CHAIN, 1, false, glm::vec4(300, 100, 400, 300), 3, l_oS);  // one command
        CHECK(l_oP.f_CueKind(0) == "circle", "the command is a circle");
        for (int l_iK = 0; l_iK <= 36; l_iK++) {
            const float l_fA = (-90.0f + 10.0f * (float)l_iK) / 57.29578f;
            l_oP.f_Pointer(l_iK == 0 ? 0 : 1, 500.0f + std::cos(l_fA) * 80.0f, 250.0f + std::sin(l_fA) * 80.0f);
        }
        l_oP.f_Pointer(2, 500.0f, 170.0f);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 1, "drawn round: taken");
    }
    if (g_iFail) { std::printf("touch_play_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("touch_play_test: all passed\n");
    return 0;
}
