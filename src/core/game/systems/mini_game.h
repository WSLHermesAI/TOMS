// mini_game.h -- reusable reward events (docs/gameplay/07 phase 6, the owner 2026-10-22): a short timing game that earns gold or
// crystals, defined in data/minigames.json and started from any dialogue choice ({"type": "miniGame", "id": ...}), so any
// NPC or event can offer one. The play is Battle v3's point play (combo_play.h): tap the points as the marker passes;
// each hit earns `perHit`, all of them `bonusAll` more. Each one can be played `limit` times a run.
//
// Pure content + pure functions, no Game dependency (systems/*).
#pragma once
#include "combo_play.h"

#include <json.hpp>
#include <map>
#include <string>

namespace toms {

struct MiniGameDef {
    std::string m_strId;
    nlohmann::json m_jTitle, m_jText;      // {"zh_TW": ..., "en": ...} (Locale::field); a language missing falls back
    std::string m_strCurrency = "gold";   // "gold" or "crystals"
    int m_iPerHit = 10, m_iBonusAll = 0;
    int m_iLimit = 3;                   // plays a run (0: no limit)
    ComboSettings m_oPlay;              // points, sweepMs, turnBack, maxSweeps, hitWindow (missBack: none)
};

struct MiniGameTable {
    std::map<std::string, MiniGameDef> m_mapGames;
    const MiniGameDef* f_Find(const std::string& l_strId) const {
        auto l_itIt = m_mapGames.find(l_strId);
        return l_itIt == m_mapGames.end() ? nullptr : &l_itIt->second;
    }
};

MiniGameDef f_MiniGameFromJson(const std::string& l_strId, const nlohmann::json& l_jJ);
// data/minigames.json; a missing or broken file gives an empty table.
MiniGameTable f_LoadMiniGames(const std::string& l_strPath);

// The reward for a finished play: perHit a hit, bonusAll more when every point was hit; a fail earns nothing.
int f_MiniGameReward(const MiniGameDef& l_oG, const ComboResult& l_oR);

} // namespace toms
