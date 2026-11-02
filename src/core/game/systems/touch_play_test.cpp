// touch_play_test.cpp -- the plays (touch_play.h): bow, slice, circles, shape, mash. Exits 0 on success.
#include "touch_play.h"

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
        l_oP.f_Start(PlayKind::Bow, 1, false, l_v4Area, 7);
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
        l_oP.f_Start(PlayKind::Bow, 1, false, l_v4Area, 3, l_oS);
        l_oP.f_Update(600);
        const auto l_oT = l_oP.f_Target();
        l_oP.f_Pointer(0, l_oT.m_fX, l_oT.m_fY + 90);
        l_oP.f_Pointer(1, l_oT.m_fX, l_oT.m_fY + 140);
        l_oP.f_Update(1600);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 1 && l_oP.f_ShotCharge() == 1.0f, "the charge limit lets it go at full charge");
    }
    {   // a short pull is not a shot; an arrow not shot in time is a miss
        TouchPlay l_oP;
        l_oP.f_Start(PlayKind::Bow, 1, false, l_v4Area, 4);
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
        l_oP.f_Start(PlayKind::Slice, 3, false, l_v4Area, 11, l_oS);
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
    // 3. Circles: count, hide, "!", then tapped in the remembered order; each tap a spell.
    {
        TouchPlay l_oP;
        l_oP.f_Start(PlayKind::Circles, 3, false, l_v4Area, 9);
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
        l_oP.f_Update(500);    // 1900: back, with the "!"
        CHECK(l_oP.f_CirclePhase() == TouchPlay::CirclePhase::Warn, "back, with the !");
        l_oP.f_Update(500);    // 2400: tap
        CHECK(l_oP.f_CirclePhase() == TouchPlay::CirclePhase::Tap, "now to be tapped");
        l_oP.f_Pointer(0, l_vC[0].m_fX, l_vC[0].m_fY);
        l_oP.f_Update(700);
        l_oP.f_Pointer(0, l_vC[2].m_fX, l_vC[2].m_fY);   // the wrong one: the second is missed
        l_oP.f_Pointer(0, l_vC[2].m_fX, l_vC[2].m_fY);   // now the third is due
        const auto l_vHits = l_oP.f_TakeHits();
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 2 && l_oP.f_Circles()[1].m_iState == 3 && l_vHits.size() == 3, "in order; a wrong one misses the due one");
        CHECK(l_oP.f_Circles()[0].m_iState == 2 && l_oP.f_Circles()[2].m_iState == 2, "quick taps are perfect");
    }
    // 4. Shape: the pentagram traced corner by corner (5 steps); time up keeps what is done.
    {
        TouchPlaySettings l_oS;
        l_oS.m_iShapeTimeMs = 3000;
        TouchPlay l_oP;
        l_oP.f_Start(PlayKind::Shape, 0, true, l_v4Area, 1, l_oS);
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
        l_oP.f_Start(PlayKind::Mash, 4, false, l_v4Area, 1);
        for (int l_iK = 0; l_iK < 4; l_iK++) l_oP.f_Pointer(0, 600, 200);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 4, "four taps");
    }
    CHECK(f_PlayKindFromString("bow") == PlayKind::Bow && f_PlayKindFromString("shape") == PlayKind::Shape &&
              f_PlayKindFromString("anything") == PlayKind::Track && std::string(f_PlayKindName(PlayKind::Mash)) == "mash", "names");
    if (g_iFail) { std::printf("touch_play_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("touch_play_test: all passed\n");
    return 0;
}
