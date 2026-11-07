// play_types_test.cpp -- the play types of docs/gameplay/17 §4-5: the swipe tiers (gesture.h), a cue with more time
// (cue_track.h), gesture prompts, DEX and the free modes (touch_play.h), a free mode as data (special_moves.h).
// Exits 0 on success.
#include "battle_cells.h"
#include "cue_track.h"
#include "gesture.h"
#include "special_moves.h"
#include "touch_play.h"
#include "weapon_play.h"

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

static void f_Swipe(TouchPlay& l_oP, float l_fX, float l_fY, float l_fDeg, float l_fLen) {
    const float l_fA = l_fDeg * 3.14159265f / 180.0f;
    l_oP.f_Pointer(0, l_fX, l_fY);
    l_oP.f_Pointer(1, l_fX + std::cos(l_fA) * l_fLen * 0.5f, l_fY + std::sin(l_fA) * l_fLen * 0.5f);
    l_oP.f_Pointer(2, l_fX + std::cos(l_fA) * l_fLen, l_fY + std::sin(l_fA) * l_fLen);
}

int main() {
    const glm::vec4 l_v4Area(500, 100, 300, 200);
    {   // the swipe tiers (17 §4.2): any place, size and direction
        const std::vector<glm::vec2> l_vV = f_Polyline({{0, 0}, {0.4f, 1}, {1, 0}}, {50, 50}, 150);
        CHECK(f_MatchGesture(l_vV, "v"), "a tick is a V");
        std::vector<glm::vec2> l_vTurned;
        for (glm::vec2 l_v2P : l_vV) l_vTurned.push_back({300.0f - l_v2P.y, 40.0f + l_v2P.x});
        CHECK(f_MatchGesture(l_vTurned, "v"), "a V on its side too");
        CHECK(!f_MatchGesture(f_Polyline({{0, 0}, {1, 0}}, {0, 0}, 200), "v"), "a straight line is no V");
        for (int l_iTurn = 0; l_iTurn < 8; l_iTurn++) {   // the Nike-like tick the prompt shows, in each of its eight turns, drawn either way
            const float l_fA = (float)l_iTurn * 45.0f * 3.14159265f / 180.0f;
            std::vector<glm::vec2> l_vTurnedTick;
            for (glm::vec2 l_v2P : f_ShapeCorners("v")) {
                const glm::vec2 l_v2D = l_v2P - glm::vec2(0.5f);
                l_vTurnedTick.push_back(glm::vec2(0.5f) + glm::vec2(l_v2D.x * std::cos(l_fA) - l_v2D.y * std::sin(l_fA), l_v2D.x * std::sin(l_fA) + l_v2D.y * std::cos(l_fA)));
            }
            const std::vector<glm::vec2> l_vStroke = f_Polyline(l_vTurnedTick, {200, 200}, 150);
            CHECK(f_MatchGesture(l_vStroke, "v"), "a tick in any of the eight turns");
            CHECK(f_MatchGesture(std::vector<glm::vec2>(l_vStroke.rbegin(), l_vStroke.rend()), "v"), "and drawn from its other end");
        }
        CHECK(!f_MatchGesture(f_Polyline({{0, 0}, {1, 0}, {0, 0.01f}}, {0, 0}, 200), "v"), "straight back along itself is no V");
        const std::vector<glm::vec2> l_vM = f_Polyline(f_ShapeCorners("m"), {10, 10}, 200);
        CHECK(f_MatchGesture(l_vM, "m") && !f_MatchGesture(l_vM, "v") && !f_MatchGesture(l_vM, "square"), "an M is an M only");
        CHECK(!f_MatchGesture(l_vV, "m"), "a V is no M");
        for (int l_iStart = 0; l_iStart < 4; l_iStart++) {   // any of the four corners, either way round
            const std::vector<glm::vec2> l_vC = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            std::vector<glm::vec2> l_vOne, l_vTwo;
            for (int l_iK = 0; l_iK <= 4; l_iK++) { l_vOne.push_back(l_vC[(size_t)((l_iStart + l_iK) % 4)]); l_vTwo.push_back(l_vC[(size_t)((l_iStart + 4 - l_iK) % 4)]); }
            CHECK(f_MatchGesture(f_Polyline(l_vOne, {30, 30}, 180), "square"), "a square from any corner");
            CHECK(f_MatchGesture(f_Polyline(l_vTwo, {30, 30}, 180), "square"), "the other way round");
        }
        CHECK(!f_MatchGesture(f_Polyline({{0, 0}, {1, 0}, {1, 1}, {0, 1}}, {0, 0}, 180), "square"), "three sides: not closed");
        CHECK(!f_MatchGesture(l_vM, "square"), "an M is no square");
        // a hand's corner: a little shake does not add a part
        std::vector<glm::vec2> l_vShaky = l_vV;
        for (size_t l_nI = 0; l_nI < l_vShaky.size(); l_nI++) l_vShaky[l_nI].y += (l_nI % 2 ? 2.0f : -2.0f);
        CHECK(f_MatchGesture(l_vShaky, "v"), "a shaky V");
        // DEX: a wide V fails, then passes with the extra angle
        const std::vector<glm::vec2> l_vWide = f_Polyline({{0, 0}, {0.5f, 0.3f}, {1, 0.1f}}, {0, 0}, 300);
        CHECK(!f_MatchGesture(l_vWide, "v", -20.0f) && f_MatchGesture(l_vWide, "v", 15.0f), "DEX widens a gesture's angles");
        CHECK(f_GestureTier("way") == 1 && f_GestureTier("v") == 2 && f_GestureTier("m") == 3 && f_GestureTier("square") == 4 && f_GestureTier("triangle") == 0, "the tiers");
    }
    {   // a cue with more time: its window lasts that much longer
        CueTiming l_oTm;
        CueTrack l_oC;
        l_oC.f_Start(2, l_oTm, {400});
        l_oC.f_Update(1200);
        CHECK(l_oC.f_Open() && l_oC.f_Index() == 0, "1000 + 400: still open at 1200");
        CHECK(l_oC.f_Update(300) == 1 && l_oC.f_Index() == 1, "closed at 1400");
    }
    // 5. Gesture prompts (17 §4.2): the tick in a swipe play, with its own extra time; DEX widens the corners.
    {
        TouchPlaySettings l_oS;
        l_oS.m_vSliceCues = {"v"};
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eSWIPE_WAY, 1, false, l_v4Area, 2, l_oS);
        l_oP.f_Update(1200);   // past the plain 1000 ms: a V has 400 more
        CHECK(l_oP.f_Cues().f_Open(), "a V has more time");
        l_oP.f_Pointer(0, 600, 200);
        for (int l_iK = 1; l_iK <= 10; l_iK++) l_oP.f_Pointer(1, 600.0f + 6.0f * l_iK, 200.0f + 12.0f * l_iK);
        for (int l_iK = 1; l_iK <= 10; l_iK++) l_oP.f_Pointer(1, 660.0f + 12.0f * l_iK, 320.0f - 16.0f * l_iK);
        l_oP.f_Pointer(2, 780, 160);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 1, "a V drawn: the prompt cut");
    }
    {
        TouchPlaySettings l_oS;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eSWIPE_GESTURE, 2, false, l_v4Area, 2, l_oS);
        CHECK(l_oP.f_CueKind(0) == "v" && l_oP.f_CueKind(1) == "v", "a gesture play asks for V by itself");
        l_oP.f_Update(600);
        f_Swipe(l_oP, 600, 200, 0.0f, 200);   // a straight swipe
        CHECK(l_oP.f_Index() == 1 && l_oP.f_Result().m_iHits == 0, "a straight swipe is a wrong answer");
    }
    {
        TouchPlaySettings l_oS;
        l_oS.m_fShapeCornerRadius = 40.0f;
        l_oS.m_fCornerBonus = 0.5f;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eDRAW_SHAPE, 0, true, l_v4Area, 1, l_oS);
        const auto& l_vPts = l_oP.f_Trace().f_Points();
        l_oP.f_Pointer(0, l_vPts[0].x + 55.0f, l_vPts[0].y);   // 55 > 40, but within 60
        l_oP.f_Pointer(1, l_vPts[1].x, l_vPts[1].y);
        CHECK(l_oP.f_Index() == 1, "DEX: the corners are bigger");
    }
    // 6. Free modes (17 §5): any swipe or tap counts until the time is up.
    {
        TouchPlaySettings l_oS;
        l_oS.m_iFreeTimeMs = 3000;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eFREE_SWIPE, 1, true, l_v4Area, 1, l_oS);
        CHECK(l_oP.f_Started() && l_oP.f_Running(), "it runs at once");
        for (int l_iK = 0; l_iK < 8; l_iK++) f_Swipe(l_oP, 100.0f + 10.0f * l_iK, 500, 45.0f * l_iK, 60);
        l_oP.f_Pointer(0, 300, 300); l_oP.f_Pointer(2, 303, 300);   // too short: not a swipe
        CHECK(l_oP.f_Hits() == 8 && l_oP.f_Running() && l_oP.f_TakeHits().size() == 8, "any way, anywhere, no limit; each a hit at once");
        l_oP.f_Update(2900);
        f_Swipe(l_oP, 100, 100, 0.0f, 60);
        CHECK(l_oP.f_Running() && l_oP.f_Hits() == 9, "still in time");
        l_oP.f_Update(100);
        f_Swipe(l_oP, 100, 100, 0.0f, 60);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 9 && l_oP.f_Result().m_iPoints == 9 && !l_oP.f_Result().m_bFailed, "time up: what was done is the whole");
    }
    {
        TouchPlaySettings l_oS;
        l_oS.m_iFreeTimeMs = 2000;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eFREE_SHOT, 1, true, l_v4Area, 1, l_oS);
        f_Swipe(l_oP, 10, 700, 90.0f, 40);   // far from the monster: it still hits
        const auto l_vHits = l_oP.f_TakeHits();
        CHECK(l_vHits.size() == 1 && l_vHits[0].m_bOk && l_vHits[0].m_fX == 650.0f && l_vHits[0].m_fY == 200.0f, "a free shot always lands on the monster");
    }
    {
        TouchPlaySettings l_oS;
        l_oS.m_iFreeTimeMs = 2000;
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eFREE_TAP, 1, true, l_v4Area, 1, l_oS);
        for (int l_iK = 0; l_iK < 5; l_iK++) { l_oP.f_Pointer(0, 20.0f * l_iK, 700); l_oP.f_Pointer(2, 20.0f * l_iK, 700); }
        CHECK(l_oP.f_Hits() == 5, "any tap anywhere");
        l_oP.f_Update(2000);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_iHits == 5, "time up");
    }
    {
        TouchPlay l_oP;
        l_oP.f_Start(PlayType::eFREE_TAP, 1, true, l_v4Area, 1);
        l_oP.f_Update(5000);
        CHECK(l_oP.f_Done() && l_oP.f_Result().m_bFailed && l_oP.f_Result().m_iHits == 0, "nothing done: a miss");
    }
    {   // how the combo grows (17 §19): out of order in the file, the highest reached counts
        const WeaponPlay l_oW = f_WeaponPlayFromJson("x", nlohmann::json::parse(R"({ "comboKeep": true, "comboTiers": [
          { "from": 3, "clip": "attack_3", "fx": "f3", "feel": "great" }, { "from": 1, "clip": "attack" }, { "from": 2, "clip": "attack_2" } ] })"));
        CHECK(l_oW.m_bComboKeep && !l_oW.f_ComboTierAt(0), "no hits: no row");
        CHECK(l_oW.f_ComboTierAt(1)->m_strClip == "attack" && l_oW.f_ComboTierAt(2)->m_strClip == "attack_2", "the rows in turn");
        CHECK(l_oW.f_ComboTierAt(9)->m_strClip == "attack_3" && l_oW.f_ComboTierAt(9)->m_strFx == "f3" && l_oW.f_ComboTierAt(9)->m_strFeel == "great", "past the last: the last");
        const WeaponPlay l_oNone = f_WeaponPlayFromJson("y", nlohmann::json::object());
        CHECK(!l_oNone.m_bComboKeep && !l_oNone.f_ComboTierAt(5), "a weapon without rows: the plain clip");
    }
    {   // weapon EXP (17 §19): 5, then each level double
        const WeaponPlay l_oW = f_WeaponPlayFromJson("x", nlohmann::json::parse(R"({ "levelExp": { "first": 5, "scale": 2 },
          "levels": [ { "lv": 1, "points": 3 }, { "lv": 10, "points": 5 } ] })"));
        CHECK(f_ExpToNextLevel(l_oW, 1) == 5 && f_ExpToNextLevel(l_oW, 2) == 10 && f_ExpToNextLevel(l_oW, 3) == 20 && f_ExpToNextLevel(l_oW, 9) == 1280, "each level double");
        CHECK(f_ExpToNextLevel(l_oW, 10) == 0 && f_WeaponMaxLevel(l_oW) == 10, "the top");
        CHECK(f_WeaponLevelFromExp(l_oW, 0) == 1 && f_WeaponLevelFromExp(l_oW, 4) == 1 && f_WeaponLevelFromExp(l_oW, 5) == 2 && f_WeaponLevelFromExp(l_oW, 14) == 2 && f_WeaponLevelFromExp(l_oW, 15) == 3, "the level from the EXP");
        CHECK(f_WeaponLevelFromExp(l_oW, 999999) == 10, "stops at the top");
        CHECK(f_WeaponExpForLevel(l_oW, 1) == 0 && f_WeaponExpForLevel(l_oW, 3) == 15 && f_WeaponExpForLevel(l_oW, 10) == 2555, "the whole EXP to a level");
        for (int l_iLv = 1; l_iLv <= 10; l_iLv++) CHECK(f_WeaponLevelFromExp(l_oW, f_WeaponExpForLevel(l_oW, l_iLv)) == l_iLv, "a level's EXP reaches that level");
        const WeaponPlay l_oMany = f_WeaponPlayFromJson("y", nlohmann::json::parse(R"({ "levels": [ { "lv": 1, "points": 7 } ] })"));
        CHECK(f_WeaponLevelEntry(l_oMany, 1).m_iPoints == 7, "the file decides how many points: no limit in code");
        TouchPlay l_oSeven;
        l_oSeven.f_Start(PlayType::eSEQUENCE_CLICK, 7, false, glm::vec4(100, 100, 700, 500), 3);
        CHECK(l_oSeven.f_Circles().size() == 7 && l_oSeven.f_Points() == 7, "seven circles");
        const WeaponPlayTable l_oT = f_LoadWeaponPlay("data/weapon_play.json");
        CHECK(!l_oT.m_mapWeapons.empty(), "the shipped weapons");
        for (const auto& [l_strId, l_oOne] : l_oT.m_mapWeapons) CHECK(f_WeaponMaxLevel(l_oOne) >= 10 && f_ExpToNextLevel(l_oOne, 1) == 5, "every weapon has ten levels, from 5 EXP");
    }
    {   // the cells (19 §2.1): a shape's variant reaches the hero when one can; steps stop at the edges
        const CellRules l_oR = f_CellRulesFromJson(nlohmann::json::parse(R"({ "cells": { "warnMs": 900, "start": 1,
          "shapes": { "near": [[0, 1, 2]], "line": [[0, 3], [1, 4], [2, 5]], "fan": [[0, 1, 2, 3, 5]] } } })"));
        CHECK(l_oR.m_iWarnMs == 900 && l_oR.m_iStart == 1 && l_oR.m_mapShapes.size() == 3, "the rules from the file");
        CHECK(f_PickDanger(l_oR, "near", 4, 0.5f) == 0b000111u, "near: the near row, the hero clear of it");
        for (int l_iHero = 0; l_iHero < 6; l_iHero++) CHECK(f_PickDanger(l_oR, "line", l_iHero, 0.9f) & (1u << l_iHero), "a line is aimed at the hero's column");
        CHECK(f_PickDanger(l_oR, "fan", 4, 0.0f) == 0b101111u, "a half circle: only the middle of the far row is clear");
        CHECK(f_PickDanger(l_oR, "nothing", 0, 0.0f) == 0u, "an unknown shape: none");
        {   // any grid: the shapes as forms, not cell numbers
            const CellRules l_oG = f_CellRulesFromJson(nlohmann::json::parse(R"({ "cells": { "cols": 3, "rows": 5,
              "shapes": { "top": [{ "row": 0 }], "bottom": [{ "row": -1 }], "line": [{ "cols": "each" }], "band": [{ "rows": "each" }],
                          "edge": [{ "col": -1 }], "fan": [{ "allBut": "start" }], "off": [[0, 99]] } } })"));
            CHECK(l_oG.m_iCols == 3 && l_oG.m_iRows == 5 && f_CellStart(l_oG, 3, 5) == 13 && f_CellStart(l_oG, 2, 2) == 3, "the start: the far row's middle");
            CHECK(f_PickDanger(l_oG, "top", 13, 0.0f) == 0b111u && f_PickDanger(l_oG, "bottom", 0, 0.0f) == (0b111u << 12), "rows, counted from either end");
            CHECK(f_PickDanger(l_oG, "line", 13, 0.0f) == ((1u << 1) | (1u << 4) | (1u << 7) | (1u << 10) | (1u << 13)), "a column aimed at the hero");
            CHECK(f_PickDanger(l_oG, "band", 7, 0.0f) == (0b111u << 6), "a row aimed at the hero");
            CHECK(f_PickDanger(l_oG, "edge", 0, 0.0f) == ((1u << 2) | (1u << 5) | (1u << 8) | (1u << 11) | (1u << 14)), "the last column");
            CHECK(f_PickDanger(l_oG, "fan", 0, 0.0f) == (0x7fffu & ~(1u << 13)), "everything but the start");
            CHECK(f_PickDanger(l_oG, "line", 3, 0.0f, 2, 2) == ((1u << 1) | (1u << 3)), "another grid: 2 x 2");
            CHECK(f_PickDanger(l_oG, "off", 0, 0.0f) == 1u, "a cell off the grid is dropped");
            CHECK(f_CellStep(13, 0, -1, 3, 5) == 10 && f_CellStep(14, 1, 0, 3, 5) == 14 && f_CellStep(1, 0, -1, 3, 5) == 1 && f_CellStep(3, 0, 1, 2, 2) == 3, "steps on any grid");
            const CellRules l_oBig = f_CellRulesFromJson(nlohmann::json::parse(R"({ "cells": { "cols": 8, "rows": 8 } })"));
            CHECK(l_oBig.m_iCols * l_oBig.m_iRows <= g_iCellMaxCount, "the grid fits the 32-bit mask");
        }
        CHECK(f_CellStep(4, 0, -1) == 1 && f_CellStep(0, -1, 0) == 0 && f_CellStep(2, 1, 0) == 2 && f_CellStep(3, 0, 1) == 3 && f_CellStep(4, 1, 0) == 5, "steps, stopped at the edges");
    }
    CHECK(f_PlayTypeFromString("swipeGesture") == PlayType::eSWIPE_GESTURE && f_PlayTypeFromString("freeShot") == PlayType::eFREE_SHOT &&
              std::string(f_PlayTypeName(PlayType::eSEQUENCE_CLICK)) == "sequenceClick" && f_IsSwipePlay(PlayType::eSWIPE_GESTURE) && f_IsFreePlay(PlayType::eFREE_TAP) && !f_IsFreePlay(PlayType::eMASH), "the types");
    {   // a free mode as data, and the older kind names
        const SpecialMoveTable l_oT = f_SpecialMovesFromJson(nlohmann::json::parse(R"({
          "free": { "use": "ultimate", "damage": 0, "play": { "kind": "freeSwipe", "timeMs": 3000, "hitDamage": 0.5 } },
          "old": { "play": { "kind": "slice", "cues": ["way", "v"] } },
          "star": { "play": { "kind": "shape", "shape": "pentagram" } }
        })"));
        const SpecialMove* l_pF = l_oT.f_Find("free");
        CHECK(l_pF && l_pF->m_oPlay.m_eType == PlayType::eFREE_SWIPE && l_pF->m_oPlay.m_fHitDamage == 0.5f && l_pF->m_oPlay.m_iTimeMs == 3000, "a free mode");
        CHECK(l_oT.f_Find("old")->m_oPlay.m_eType == PlayType::eSWIPE_WAY && l_oT.f_Find("old")->m_oPlay.m_iPoints == 2, "an older kind name still reads");
        CHECK(l_oT.f_Find("star")->m_oPlay.m_eType == PlayType::eDRAW_SHAPE && l_oT.f_Find("star")->m_oPlay.m_iPoints == 5, "and a shape's steps");
    }
    CHECK(f_PlayTypeFromString("swipeGesture") == PlayType::eSWIPE_GESTURE && f_PlayTypeFromString("freeShot") == PlayType::eFREE_SHOT, "names");
    CHECK(f_PlayTypeFromString("pullRelease") == PlayType::ePULL_RELEASE && f_PlayTypeFromString("bow") == PlayType::ePULL_RELEASE, "the new name and the older one");
    CHECK(std::string(f_PlayTypeName(PlayType::eSEQUENCE_CLICK)) == "sequenceClick" && f_PlayTypeFromString("anything") == PlayType::eTRACK, "names back");
    CHECK(f_IsSwipePlay(PlayType::eSWIPE_GESTURE) && f_IsFreePlay(PlayType::eFREE_TAP) && !f_IsFreePlay(PlayType::eMASH), "the groups");
    if (g_iFail) { std::printf("play_types_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("play_types_test: all passed\n");
    return 0;
}
