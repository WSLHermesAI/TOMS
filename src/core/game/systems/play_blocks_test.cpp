// play_blocks_test.cpp -- the reusable blocks of docs/gameplay/16 §2: drawn shapes (gesture.h), the cue track
// (cue_track.h), skills and their shows (special_moves.h). Exits 0 on success.
#include "cue_track.h"
#include "gesture.h"
#include "special_moves.h"

#include <cmath>
#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

static std::vector<glm::vec2> f_Polyline(const std::vector<glm::vec2>& l_vCorners, glm::vec2 l_v2At, float l_fSize) {
    std::vector<glm::vec2> l_vV;
    for (size_t l_nK = 1; l_nK < l_vCorners.size(); l_nK++)
        for (int l_iS = 0; l_iS < 10; l_iS++) l_vV.push_back(l_v2At + (l_vCorners[l_nK - 1] + (l_vCorners[l_nK] - l_vCorners[l_nK - 1]) * (l_iS / 10.0f)) * l_fSize);
    l_vV.push_back(l_v2At + l_vCorners.back() * l_fSize);
    return l_vV;
}

int main() {
    // 1. Shapes
    CHECK(f_ShapeCorners("pentagram").size() == 6 && f_ShapeClosed("pentagram"), "a pentagram: 5 corners, closed");
    CHECK(f_ShapeCorners("nothing").empty(), "an unknown shape: none");
    {
        const glm::vec2 l_v2Top = f_ShapePoint("pentagram", {100, 100}, 200, 0.0f);
        CHECK(std::fabs(l_v2Top.x - 100) < 0.01f && std::fabs(l_v2Top.y - 0) < 0.01f, "the pentagram starts at its top");
        const glm::vec2 l_v2End = f_ShapePoint("pentagram", {100, 100}, 200, 1.0f);
        CHECK(std::fabs(l_v2End.x - l_v2Top.x) < 0.01f && std::fabs(l_v2End.y - l_v2Top.y) < 0.01f, "and ends there");
    }
    {   // free strokes: any place and size, either way round, from any corner
        const auto l_vTri = f_Polyline(f_ShapeCorners("triangle"), {40, 70}, 130);
        CHECK(f_MatchStroke(l_vTri, "triangle"), "a triangle is a triangle");
        std::vector<glm::vec2> l_vBack(l_vTri.rbegin(), l_vTri.rend());
        CHECK(f_MatchStroke(l_vBack, "triangle"), "the other way round too");
        CHECK(!f_MatchStroke(l_vTri, "circle"), "a triangle is not a circle");
        const auto l_vCirc = f_Polyline(f_ShapeCorners("circle"), {0, 0}, 90);
        CHECK(f_MatchStroke(l_vCirc, "circle") && !f_MatchStroke(l_vCirc, "triangle"), "a circle is a circle, not a triangle");
        CHECK(!f_MatchStroke({{0, 0}, {100, 0}}, "triangle"), "a line is no triangle");
    }
    {   // traced on the screen: the first corner starts it, each next corner in order is a step
        ShapeTrace l_oT;
        l_oT.f_Start("pentagram", {300, 300}, 200, 30);
        CHECK(l_oT.f_StepCount() == 5, "five steps");
        const auto& l_vP = l_oT.f_Points();
        CHECK(l_oT.f_Pointer(0, l_vP[0].x, l_vP[0].y) == 0, "the start is no step");
        CHECK(l_oT.f_Pointer(1, l_vP[2].x, l_vP[2].y) == 0, "a corner out of order: nothing");
        CHECK(l_oT.f_Pointer(1, l_vP[1].x, l_vP[1].y) == 1 && l_oT.f_Steps() == 1, "the next corner: a step");
        l_oT.f_Pointer(2, 0, 0);
        CHECK(l_oT.f_Pointer(1, l_vP[2].x, l_vP[2].y) == 0, "lifted: a move without a press does nothing");
        CHECK(l_oT.f_Pointer(0, l_vP[2].x, l_vP[2].y) == 1, "pressed again on the next corner: a step");
        for (int l_iK = 3; l_iK <= 5; l_iK++) l_oT.f_Pointer(1, l_vP[(size_t)l_iK].x, l_vP[(size_t)l_iK].y);
        CHECK(l_oT.f_Done() && l_oT.f_Steps() == 5, "back at the top: done");
    }
    // 2. The cue track: 1000 ms cues, the last 500 take the answer, a "!" 300 ms before.
    {
        CueTiming l_oTm;
        l_oTm.m_iWarnMs = 300;
        CueTrack l_oC;
        l_oC.f_Start(3, l_oTm);
        l_oC.f_Update(100);
        CHECK(!l_oC.f_Open() && !l_oC.f_Warning(), "at first: only shown");
        l_oC.f_Update(150);   // 250
        CHECK(l_oC.f_Warning() && !l_oC.f_Open(), "the ! before the window");
        CHECK(!l_oC.f_Answer(true), "an answer before the window is ignored");
        l_oC.f_Update(300);   // 550
        CHECK(l_oC.f_Open() && l_oC.f_Answer(true) && l_oC.f_Index() == 1 && l_oC.f_State(0) == CueTrack::Answered, "answered in the window");
        CHECK(l_oC.f_CueAgeMs() == -1, "a short gap before the next");
        CHECK(l_oC.f_Update(150 + 1000) == 1 && l_oC.f_State(1) == CueTrack::Missed && l_oC.f_Index() == 2, "the next let pass: missed");
        l_oC.f_Update(600);
        CHECK(l_oC.f_Answer(false) && l_oC.f_Done() && l_oC.f_State(2) == CueTrack::Missed, "a wrong answer: missed; done");
    }
    // 3. Skills and shows
    {
        const nlohmann::json l_jJ = nlohmann::json::parse(R"({
          "_comment": "x",
          "star_seal": { "name": "skill.star_seal", "weapon": "staff", "use": "ultimate", "damage": 10,
            "play": { "kind": "shape", "shape": "pentagram", "timeMs": 6000 },
            "demo": [ { "at": 2100, "do": "dim" }, { "at": 0, "do": "pathFx", "shape": "pentagram", "fx": "t", "ms": 2000 } ],
            "finish": [ { "at": 400, "do": "hits", "count": 10, "gapMs": 110 } ] },
          "tiger": { "weapon": "sword", "use": "ultimate", "play": { "kind": "slice", "cues": ["way", "way", "triangle"] } },
          "guard_mash": { "use": "guard", "play": { "kind": "mash", "points": 12, "timeMs": 2000 } }
        })");
        const SpecialMoveTable l_oT = f_SpecialMovesFromJson(l_jJ);
        CHECK(l_oT.m_mapSkills.size() == 3, "three skills, the comment skipped");
        const SpecialMove* l_pS = l_oT.f_Find("star_seal");
        CHECK(l_pS && l_pS->m_oPlay.m_iPoints == 5 && l_pS->m_fDamage == 10.0f, "the pentagram: 5 steps, x10");
        CHECK(l_pS && l_pS->m_vDemo.size() == 2 && l_pS->m_vDemo[0].m_strWhat == "pathFx", "beats sorted by time");
        CHECK(l_pS && f_ShowLength(l_pS->m_vDemo) == 2100 && f_ShowLength(l_pS->m_vFinish) == 400 + 1100, "show lengths");
        CHECK(l_pS && f_BeatsDue(l_pS->m_vDemo, -1, 0).size() == 1 && f_BeatsDue(l_pS->m_vDemo, 0, 2100).size() == 1, "beats due");
        CHECK(l_oT.f_ForWeapon("sword", "ultimate") && l_oT.f_ForWeapon("sword", "ultimate")->m_oPlay.m_iPoints == 3, "a slice's points: its cues");
        CHECK(l_oT.f_Find("guard_mash") && l_oT.f_Find("guard_mash")->m_oPlay.m_iPoints == 12, "a guard skill");
        CHECK(!l_oT.f_ForWeapon("bow", "ultimate"), "none for the bow");
    }
    CHECK(f_NamePart("SUPER", 2, 5) == "SU" && f_NamePart("SUPER", 5, 5) == "SUPER" && f_NamePart("SUPER", 0, 5).empty(), "name parts");
    CHECK(f_NamePart("星之印", 1, 3) == "星", "name parts keep letters whole");
    if (g_iFail) { std::printf("play_blocks_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("play_blocks_test: all passed\n");
    return 0;
}
