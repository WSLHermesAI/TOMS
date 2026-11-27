// circle_symbols_test.cpp -- the staff's symbols (circle_symbols.h) and the shipped data/circle_symbols.json. Exits 0 on
// success. Runs in assets/ (tests/CMakeLists.txt), so the data file is "data/circle_symbols.json".
#include "circle_symbols.h"
#include "touch_play.h"
#include "weapon_play.h"
#include "combo_play.h"

#include <cmath>
#include <cstdio>
#include <set>

using namespace toms;

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)
}  // namespace

int main() {
    // 1. Rows by level: each sets only what it has; a level takes the rows up to it.
    {
        const CircleSymbolTable l_oT = f_CircleSymbolsFromJson(nlohmann::json::parse(R"({
            "symbols": { "tri": { "points": [[0, 0], [1, 0], [0.5, 1]], "rotate": true, "reverse": true },
                         "row": { "points": [[0, 0.5], [0.5, 0.5], [1, 0.5]] } },
            "weapons": { "staff": [ { "_comment": "skipped" },
                                    { "lv": 4, "chance": 60, "symbols": { "row": 1 } },
                                    { "lv": 1, "chance": 100, "symbols": { "tri": 1 } } ] } })"));
        CHECK(l_oT.m_mapSymbols.size() == 2 && l_oT.m_mapWeapons.at("staff").size() == 2, "two symbols, two rows (the comment row skipped)");
        const CircleSymbolRow l_oLv1 = l_oT.f_At("staff", 1), l_oLv5 = l_oT.f_At("staff", 5);
        CHECK(l_oLv1.m_fChance == 100.0f && l_oLv1.m_mapWeights.count("tri"), "Lv 1: its row");
        CHECK(l_oLv5.m_fChance == 60.0f && l_oLv5.m_mapWeights.count("row") && !l_oLv5.m_mapWeights.count("tri"), "Lv 5: Lv 4's chance and symbols");
        CHECK(l_oT.f_At("sword", 9).m_fChance == 0.0f, "a weapon with no rows: never");
        std::mt19937 l_oRng(5);
        std::set<std::string> l_setStarts;
        for (int l_iK = 0; l_iK < 40; l_iK++) {
            std::string l_strId;
            const std::vector<glm::vec2> l_vP = l_oT.f_Pick("staff", 1, 3, l_oRng, &l_strId);
            CHECK(l_strId == "tri" && l_vP.size() == 3, "chance 100: the triangle every time");
            if (l_vP.size() == 3) l_setStarts.insert(std::to_string(l_vP[0].x) + "," + std::to_string(l_vP[0].y));
        }
        CHECK(l_setStarts.size() == 3, "rotate: it starts at each of its points");
        CHECK(l_oT.f_Pick("staff", 1, 4, l_oRng).empty(), "no symbol with 4 or more points: anywhere");
        CHECK(l_oT.f_Pick("staff", 1, 2, l_oRng).size() == 2, "fewer circles than its points: its first ones");
    }
    // 2. The play puts the circles on the symbol's places, inside the area by the circle's radius.
    {
        TouchPlaySettings l_oS;
        l_oS.m_vCircleSymbol = {{0, 0}, {1, 0}, {0.5f, 1}};
        TouchPlay l_oP;
        const glm::vec4 l_v4Area(100, 100, 500, 300);
        l_oP.f_Start(PlayType::eSEQUENCE_CLICK, 3, false, l_v4Area, 1, l_oS);
        const auto& l_vC = l_oP.f_Circles();
        const float l_fM = l_oS.m_fCircleRadius;
        CHECK(l_oP.f_CircleSymbol() && l_vC.size() == 3, "laid out as the symbol");
        CHECK(l_vC.size() == 3 && std::fabs(l_vC[0].m_fX - (100 + l_fM)) < 0.01f && std::fabs(l_vC[1].m_fX - (600 - l_fM)) < 0.01f &&
                  std::fabs(l_vC[2].m_fY - (400 - l_fM)) < 0.01f, "0..1 across the area, inside its edge");
        l_oS.m_vCircleSymbol.clear();
        TouchPlay l_oQ;
        l_oQ.f_Start(PlayType::eSEQUENCE_CLICK, 3, false, l_v4Area, 1, l_oS);
        CHECK(!l_oQ.f_CircleSymbol() && l_oQ.f_Circles().size() == 3, "no symbol: anywhere");
        // a symbol's own rhythm ("times"): lit at 0, 500 and 1500 ms, then counting ends circleCountMs after the last
        TouchPlaySettings l_oT;
        l_oT.m_vCircleSymbol = {{0, 0}, {1, 0}, {0.5f, 1}};
        l_oT.m_vCircleTimesMs = {0, 500, 1500};
        TouchPlay l_oR;
        l_oR.f_Start(PlayType::eSEQUENCE_CLICK, 3, false, l_v4Area, 1, l_oT);
        CHECK(l_oR.f_CirclesLit() == 1, "at 0 ms: the first lit");
        l_oR.f_Update(499);
        CHECK(l_oR.f_CirclesLit() == 1, "at 499 ms: still one");
        l_oR.f_Update(1);
        CHECK(l_oR.f_CirclesLit() == 2, "at 500 ms: the second");
        l_oR.f_Update(1000);
        CHECK(l_oR.f_CirclesLit() == 3 && l_oR.f_CirclePhase() == TouchPlay::CirclePhase::Count, "at 1500 ms: all three, still counting");
        l_oR.f_Update(l_oT.m_iCircleCountMs);
        CHECK(l_oR.f_CirclePhase() != TouchPlay::CirclePhase::Count, "then counting ends");
    }
    // 3. The shipped file: every symbol has points, every weight names a symbol, the staff's rows pick for its circles.
    {
        const CircleSymbolTable l_oT = f_LoadCircleSymbols("data/circle_symbols.json");
        CHECK(!l_oT.m_mapSymbols.empty() && l_oT.m_mapWeapons.count("staff"), "data/circle_symbols.json read, with the staff's rows");
        for (const auto& [l_strW, l_vRows] : l_oT.m_mapWeapons)
            for (const CircleSymbolRow& l_oR : l_vRows)
                for (const auto& l_pairW : l_oR.m_mapWeights)
                    if (!l_oT.m_mapSymbols.count(l_pairW.first)) { std::printf("FAIL: %s Lv %d names \"%s\", no such symbol\n", l_strW.c_str(), l_oR.m_iLv, l_pairW.first.c_str()); g_iFail++; }
        const WeaponPlayTable l_oWp = f_LoadWeaponPlay("data/weapon_play.json");
        std::mt19937 l_oRng(11);
        for (int l_iLv = 1; l_iLv <= 10; l_iLv++) {   // each level's circle count has a symbol it can show
            const int l_iPoints = f_ComboSettings(l_oWp.f_Get("staff"), l_iLv).m_iPoints;
            bool l_bAny = false;
            for (int l_iK = 0; l_iK < 200 && !l_bAny; l_iK++) l_bAny = !l_oT.f_Pick("staff", l_iLv, l_iPoints, l_oRng).empty();
            if (!l_bAny) { std::printf("FAIL: the staff at Lv %d (%d circles) never shows a symbol\n", l_iLv, l_iPoints); g_iFail++; }
        }
    }
    if (g_iFail) { std::printf("circle_symbols_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("circle_symbols_test: all passed\n");
    return 0;
}
