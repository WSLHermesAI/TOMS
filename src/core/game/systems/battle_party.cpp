// battle_party.cpp -- see battle_party.h.
#include "battle_party.h"

#include <cstdio>

namespace toms {

int nextStanding(const std::vector<int>& hp, int from, int dir) {
    const int n = (int)hp.size();
    if (n == 0) return -1;
    if (from < 0 || from >= n) from = 0;
    if (dir == 0 && hp[(size_t)from] > 0) return from;
    const int step = dir < 0 ? -1 : 1;
    for (int k = 1; k <= n; k++) {
        const int i = ((from + step * k) % n + n) % n;
        if (hp[(size_t)i] > 0) return i;
    }
    return -1;
}

bool allDown(const std::vector<int>& hp) {
    for (int h : hp)
        if (h > 0) return false;
    return true;
}

std::string battleEventLine(const BattleEvent& e, const std::vector<std::string>& foeIds) {
    auto name = [&](int i) {
        if (i < 0) return std::string("player");
        return "foe " + std::to_string(i) + (i < (int)foeIds.size() ? " (" + foeIds[(size_t)i] + ")" : std::string());
    };
    char t[16];
    std::snprintf(t, sizeof t, "%6.2f", e.ms / 1000.0);
    std::string s = std::string(t) + "  " + name(e.who);
    if (e.whom != e.who && (e.type == "hit" || e.type == "combo" || e.type == "super" || e.type == "target" ||
                             e.type == "parry" || e.type == "guardBreak" || e.type == "spellBreak" || e.type == "summon"))
        s += " -> " + name(e.whom);
    s += ": " + e.type;
    if (e.type == "hit" || e.type == "guard" || e.type == "combo" || e.type == "super") s += " " + std::to_string(e.value);
    if (!e.note.empty()) s += " (" + e.note + ")";
    return s;
}

} // namespace toms
