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
    if (j.contains("levels") && j["levels"].is_array()) {
        for (const auto& l : j["levels"]) {
            if (!l.is_object()) continue;
            WeaponLevel lv;
            lv.lv = std::max(1, l.value("lv", 1));
            lv.points = std::clamp(l.value("points", 3), 1, kMaxComboPoints);
            if (l.contains("stats") && l["stats"].is_object())
                for (auto s = l["stats"].begin(); s != l["stats"].end(); ++s)
                    if (s.value().is_number_integer()) lv.stats[s.key()] = s.value().get<int>();
            w.levels.push_back(lv);
        }
        std::stable_sort(w.levels.begin(), w.levels.end(), [](const WeaponLevel& a, const WeaponLevel& b) { return a.lv < b.lv; });
    }
    if (j.contains("usesToLevel") && j["usesToLevel"].is_array())
        for (const auto& u : j["usesToLevel"])
            if (u.is_number_integer()) w.usesToLevel.push_back(std::max(1, u.get<int>()));
    w.turnBack = j.value("turnBack", w.turnBack);
    w.sweepMs = std::max(200, j.value("sweepMs", w.sweepMs));
    w.maxSweeps = std::max(1, j.value("maxSweeps", w.maxSweeps));
    w.chargeMs = std::max(1, j.value("chargeMs", w.chargeMs));
    w.chargeSpeedUp = std::max(0.0f, j.value("chargeSpeedUp", w.chargeSpeedUp));
    w.chargeDamage = std::max(0.0f, j.value("chargeDamage", w.chargeDamage));
    w.hitWindow = std::clamp(j.value("hitWindow", w.hitWindow), 0.01f, 0.25f);
    w.hitWindowMs = std::clamp(j.value("hitWindowMs", w.hitWindowMs), 0, 1000);
    w.lateMs = std::clamp(j.value("lateMs", w.lateMs), 0, 500);
    if (j.contains("fx") && j["fx"].is_array()) {
        for (const auto& f : j["fx"]) {
            if (!f.is_object()) continue;
            WeaponFx fx;
            fx.lv = std::max(1, f.value("lv", 1));
            fx.release = f.value("release", std::string());
            fx.hit = f.value("hit", std::string());
            w.fx.push_back(fx);
        }
        std::stable_sort(w.fx.begin(), w.fx.end(), [](const WeaponFx& a, const WeaponFx& b) { return a.lv < b.lv; });
    }
    if (j.contains("results") && j["results"].is_array()) {
        for (const auto& r : j["results"]) {
            if (!r.is_object()) continue;
            WeaponResult x;
            const nlohmann::json h = r.value("hits", nlohmann::json(0));
            x.hits = h.is_string() ? (h.get<std::string>() == "all" ? -1 : 0) : std::max(0, h.get<int>());
            x.name = r.value("name", std::string());
            x.damage = std::max(0.0f, r.value("damage", 1.0f));
            x.fx = r.value("fx", std::string());
            x.anim = r.value("anim", std::string());
            x.sound = r.value("sound", std::string());
            x.stunMs = std::max(0, r.value("stunMs", 0));
            x.pushMs = std::max(0, r.value("pushMs", 0));
            x.breakAdd = std::max(0.0f, r.value("breakAdd", 0.0f));
            w.results.push_back(x);
        }
    }
    w.pace = j.value("pace", std::string());
    return w;
}

const WeaponResult* weaponResultFor(const WeaponPlay& w, int hits, int points, bool failed) {
    if (failed) hits = 0;
    const WeaponResult* best = nullptr;
    int bestNeed = -1;
    for (const WeaponResult& r : w.results) {
        const int need = r.hits < 0 ? std::max(1, points) : r.hits;   // "all": every point
        if (hits >= need && need > bestNeed) { best = &r; bestNeed = need; }
    }
    return best;
}

const WeaponFx* weaponFxEntry(const WeaponPlay& w, int weaponLevel) {
    const WeaponFx* best = nullptr;
    for (const WeaponFx& f : w.fx)
        if (f.lv <= weaponLevel) best = &f;
    return best ? best : (w.fx.empty() ? nullptr : &w.fx.front());
}

const WeaponLevel& weaponLevelEntry(const WeaponPlay& w, int weaponLevel) {
    static const WeaponLevel builtIn;
    const WeaponLevel* best = nullptr;
    for (const WeaponLevel& l : w.levels)
        if (l.lv <= weaponLevel) best = &l;
    return best ? *best : (w.levels.empty() ? builtIn : w.levels.front());
}

int weaponMaxLevel(const WeaponPlay& w) { return w.levels.empty() ? 1 : std::max(1, w.levels.back().lv); }

int weaponLevelFromUses(const WeaponPlay& w, int uses) {
    int lv = 1, need = 0;
    for (int step : w.usesToLevel) {
        need += step;
        if (uses < need) break;
        lv++;
    }
    return std::min(lv, weaponMaxLevel(w));
}

int usesToNextLevel(const WeaponPlay& w, int weaponLevel) {
    if (weaponLevel >= weaponMaxLevel(w) || weaponLevel - 1 >= (int)w.usesToLevel.size()) return 0;
    return w.usesToLevel[(size_t)(weaponLevel - 1)];
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
    static const WeaponPlay builtIn = [] { WeaponPlay w; w.id = "builtin"; return w; }();
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
