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
    std::string id;
    nlohmann::json title, text;      // {"zh_TW": ..., "en": ...} (Locale::field); a language missing falls back
    std::string currency = "gold";   // "gold" or "crystals"
    int perHit = 10, bonusAll = 0;
    int limit = 3;                   // plays a run (0: no limit)
    ComboSettings play;              // points, sweepMs, turnBack, maxSweeps, hitWindow (missBack: none)
};

struct MiniGameTable {
    std::map<std::string, MiniGameDef> games;
    const MiniGameDef* find(const std::string& id) const {
        auto it = games.find(id);
        return it == games.end() ? nullptr : &it->second;
    }
};

MiniGameDef miniGameFromJson(const std::string& id, const nlohmann::json& j);
// data/minigames.json; a missing or broken file gives an empty table.
MiniGameTable loadMiniGames(const std::string& path);

// The reward for a finished play: perHit a hit, bonusAll more when every point was hit; a fail earns nothing.
int miniGameReward(const MiniGameDef& g, const ComboResult& r);

} // namespace toms
