// weapon_play.cpp -- see weapon_play.h.
#include "weapon_play.h"
#include "save_system.h"   // readJsonFileSafe

#include <algorithm>

namespace toms {

WeaponPlay weaponPlayFromJson(const std::string& id, const nlohmann::json& j) {
    WeaponPlay w;
    w.id = id;
    if (!j.is_object()) return w;
    w.missWaitMs = std::max(0, j.value("missWaitMs", 0));
    if (j.contains("missBack") && j["missBack"].is_array()) {
        for (const auto& r : j["missBack"]) {
            if (!r.is_object()) continue;
            MissBackRule rule;
            rule.lv = std::max(1, r.value("lv", 1));
            const auto& b = r.contains("back") ? r["back"] : nlohmann::json("start");
            rule.back = b.is_number_integer() ? std::max(0, b.get<int>()) : -1;   // "start" (or anything else): -1
            w.missBack.push_back(rule);
        }
        std::stable_sort(w.missBack.begin(), w.missBack.end(),
                         [](const MissBackRule& a, const MissBackRule& b) { return a.lv < b.lv; });
    }
    return w;
}

WeaponPlayTable loadWeaponPlay(const std::string& path) {
    WeaponPlayTable t;
    const nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return t;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().empty() || it.key()[0] == '_' || !it.value().is_object()) continue;   // "_comment"
        t.weapons[it.key()] = weaponPlayFromJson(it.key(), it.value());
    }
    return t;
}

const WeaponPlay& WeaponPlayTable::get(const std::string& weaponId) const {
    static const WeaponPlay builtIn{"builtin", {}, 0};
    auto it = weapons.find(weaponId);
    if (it != weapons.end()) return it->second;
    it = weapons.find("default");
    return it != weapons.end() ? it->second : builtIn;
}

int missBackPoints(const WeaponPlay& w, int weaponLevel) {
    int back = -1;   // no rule reaches this level: back to the start
    for (const MissBackRule& r : w.missBack)
        if (r.lv <= weaponLevel) back = r.back;
    return back;
}

int stepAfterMiss(const WeaponPlay& w, int weaponLevel, int step) {
    const int back = missBackPoints(w, weaponLevel);
    if (back < 0) return 0;
    return std::max(0, step - back);
}

} // namespace toms
