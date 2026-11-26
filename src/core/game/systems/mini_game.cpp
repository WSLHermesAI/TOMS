// mini_game.cpp -- see mini_game.h.
#include "field_keys.h"    // toms::field: every data field by its constant
#include "mini_game.h"
#include "save_system.h"   // readJsonFileSafe

#include <algorithm>

namespace toms {

MiniGameDef f_MiniGameFromJson(const std::string& l_strId, const nlohmann::json& l_jJ) {
    MiniGameDef l_oG;
    l_oG.m_strId = l_strId;
    if (!l_jJ.is_object()) return l_oG;
    l_oG.m_jTitle = l_jJ.value(toms::field::g_pszTitle, nlohmann::json());
    l_oG.m_jText = l_jJ.value(toms::field::g_pszText, nlohmann::json());
    const std::string l_strCur = l_jJ.value(toms::field::g_pszCurrency, std::string("gold"));
    l_oG.m_strCurrency = l_strCur == "crystals" ? l_strCur : "gold";
    l_oG.m_iPerHit = std::max(0, l_jJ.value(toms::field::g_pszPerHit, l_oG.m_iPerHit));
    l_oG.m_iBonusAll = std::max(0, l_jJ.value(toms::field::g_pszBonusAll, l_oG.m_iBonusAll));
    l_oG.m_iLimit = std::max(0, l_jJ.value(toms::field::g_pszLimit, l_oG.m_iLimit));
    l_oG.m_oPlay.m_iPoints = std::max(1, l_jJ.value(toms::field::g_pszPoints, 4));
    l_oG.m_oPlay.m_iSweepMs = std::max(200, l_jJ.value(toms::field::g_pszSweepMs, 1600));
    l_oG.m_oPlay.m_bTurnBack = l_jJ.value(toms::field::g_pszTurnBack, true);
    l_oG.m_oPlay.m_iMaxSweeps = std::max(1, l_jJ.value(toms::field::g_pszMaxSweeps, 2));
    l_oG.m_oPlay.m_fHitWindow = std::clamp(l_jJ.value(toms::field::g_pszHitWindow, 0.07f), 0.01f, 0.25f);
    l_oG.m_oPlay.m_fSpeedUp = 0;     // no charge in a mini-game
    l_oG.m_oPlay.m_iMissBack = 0;    // a miss loses nothing already hit (two in a row still end it)
    l_oG.m_oPlay.m_iMissWaitMs = 0;
    return l_oG;
}

MiniGameTable f_LoadMiniGames(const std::string& l_strPath) {
    MiniGameTable l_oT;
    const nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_oT;
    for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_object()) continue;   // "_comment"
        l_oT.m_mapGames[l_oIt.key()] = f_MiniGameFromJson(l_oIt.key(), l_oIt.value());
    }
    return l_oT;
}

int f_MiniGameReward(const MiniGameDef& l_oG, const ComboResult& l_oR) {
    if (l_oR.m_bFailed) return 0;
    return l_oR.m_iHits * l_oG.m_iPerHit + (l_oR.m_iPoints > 0 && l_oR.m_iHits == l_oR.m_iPoints ? l_oG.m_iBonusAll : 0);
}

} // namespace toms
