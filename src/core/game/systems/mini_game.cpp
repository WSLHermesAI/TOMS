// mini_game.cpp -- see mini_game.h.
#include "mini_game.h"
#include "save_system.h"   // readJsonFileSafe

#include <algorithm>

namespace toms {

MiniGameDef miniGameFromJson(const std::string& id, const nlohmann::json& j) {
    MiniGameDef g;
    g.id = id;
    if (!j.is_object()) return g;
    g.title = j.value("title", nlohmann::json());
    g.text = j.value("text", nlohmann::json());
    const std::string cur = j.value("currency", std::string("gold"));
    g.currency = cur == "crystals" ? cur : "gold";
    g.perHit = std::max(0, j.value("perHit", g.perHit));
    g.bonusAll = std::max(0, j.value("bonusAll", g.bonusAll));
    g.limit = std::max(0, j.value("limit", g.limit));
    g.play.points = std::clamp(j.value("points", 4), 1, kMaxComboPoints);
    g.play.sweepMs = std::max(200, j.value("sweepMs", 1600));
    g.play.turnBack = j.value("turnBack", true);
    g.play.maxSweeps = std::max(1, j.value("maxSweeps", 2));
    g.play.hitWindow = std::clamp(j.value("hitWindow", 0.07f), 0.01f, 0.25f);
    g.play.speedUp = 0;     // no charge in a mini-game
    g.play.missBack = 0;    // a miss loses nothing already hit (two in a row still end it)
    g.play.missWaitMs = 0;
    return g;
}

MiniGameTable loadMiniGames(const std::string& path) {
    MiniGameTable t;
    const nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return t;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().empty() || it.key()[0] == '_' || !it.value().is_object()) continue;   // "_comment"
        t.games[it.key()] = miniGameFromJson(it.key(), it.value());
    }
    return t;
}

int miniGameReward(const MiniGameDef& g, const ComboResult& r) {
    if (r.failed) return 0;
    return r.hits * g.perHit + (r.points > 0 && r.hits == r.points ? g.bonusAll : 0);
}

} // namespace toms
