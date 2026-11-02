// mini_game_test.cpp -- reusable reward events (mini_game.h) and the shipped data/minigames.json. Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the data file is "data/minigames.json".
#include "mini_game.h"

#include <cstdio>

using namespace toms;

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)
}  // namespace

int main() {
    // 1. Parsing: the play's settings, the currency (only gold or crystals), no charge and nothing lost on a miss.
    {
        const MiniGameDef l_oG = f_MiniGameFromJson("x", nlohmann::json::parse(R"({"currency": "crystals", "perHit": 2, "bonusAll": 5,
            "points": 9, "sweepMs": 1200, "limit": 1, "title": {"zh_TW": "測試"}})"));
        CHECK(l_oG.m_strCurrency == "crystals" && l_oG.m_iPerHit == 2 && l_oG.m_iBonusAll == 5 && l_oG.m_iLimit == 1, "fields read");
        CHECK(l_oG.m_oPlay.m_iPoints == g_iMaxComboPoints && l_oG.m_oPlay.m_iSweepMs == 1200, "points capped at five; the sweep");
        CHECK(l_oG.m_oPlay.m_fSpeedUp == 0 && l_oG.m_oPlay.m_iMissBack == 0, "no charge; a miss keeps the hits");
        CHECK(f_MiniGameFromJson("y", nlohmann::json::parse(R"({"currency": "diamonds"})")).m_strCurrency == "gold", "unknown currency: gold");
    }
    // 2. The reward: per hit, the bonus for all, nothing for a fail.
    {
        MiniGameDef l_oG;
        l_oG.m_iPerHit = 10;
        l_oG.m_iBonusAll = 25;
        ComboResult l_oR;
        l_oR.m_iPoints = 4;
        l_oR.m_iHits = 3;
        CHECK(f_MiniGameReward(l_oG, l_oR) == 30, "3 of 4: 3 x 10");
        l_oR.m_iHits = 4;
        CHECK(f_MiniGameReward(l_oG, l_oR) == 65, "all 4: 40 + 25");
        l_oR.m_bFailed = true;
        CHECK(f_MiniGameReward(l_oG, l_oR) == 0, "a fail: nothing");
    }
    // 3. A whole play through ComboPlay: hit every point.
    {
        MiniGameDef l_oG = f_MiniGameFromJson("z", nlohmann::json::parse(R"({"points": 2, "sweepMs": 1000, "perHit": 1, "bonusAll": 1})"));
        ComboPlay l_oC;
        l_oC.f_Start(l_oG.m_oPlay, 0.0f);
        for (float l_fP : l_oC.f_PointsAt()) {
            while (l_oC.f_Running() && l_oC.f_Marker() < l_fP - 0.005f) l_oC.f_Update(1);
            l_oC.f_Tap();
        }
        CHECK(l_oC.f_Done() && f_MiniGameReward(l_oG, l_oC.f_Result()) == 3, "both hit: 2 + 1");
    }
    // 4. The shipped events: the gold chest and the crystal shrine (its text Traditional Chinese only, the owner's test).
    {
        const MiniGameTable l_oT = f_LoadMiniGames("data/minigames.json");
        const MiniGameDef* l_pChest = l_oT.f_Find("chest_lockpick");
        const MiniGameDef* l_pShrine = l_oT.f_Find("crystal_shrine");
        CHECK(l_pChest && l_pChest->m_strCurrency == "gold" && l_pChest->m_iPerHit > 0, "chest_lockpick: gold");
        CHECK(l_pShrine && l_pShrine->m_strCurrency == "crystals" && l_pShrine->m_iPerHit > 0, "crystal_shrine: crystals");
        CHECK(l_pShrine && l_pShrine->m_jText.contains("zh_TW") && !l_pShrine->m_jText.contains("en"), "crystal_shrine: zh_TW text only");
    }
    if (g_iFail) { std::printf("mini_game_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("mini_game_test: OK\n");
    return 0;
}
