// weapon_play.h -- each weapon's play settings, from data/weapon_play.json (docs/30_GAMEPLAY_PLAN.md §5.6).
//
// The owner's rule (2026-10-20): a miss sends the combo back to an earlier step, and how far depends on the weapon and
// its level -- e.g. Lv 5 back to the start, Lv 10 back 5 points, Lv 15 back 4. Every weapon's values live in the file,
// so they change without code. The file also holds each weapon's other play settings (the wait after a miss ...);
// later phases add to it.
//
// Pure content + pure functions, no Game dependency (the same shape as skill_system.h / hub_system.h).
#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

namespace toms {

struct MissBackRule {
    int lv = 1;           // applies from this weapon level up (the highest lv at or below the level wins)
    int back = -1;        // points the combo goes back; -1: to the start
};

struct WeaponPlay {
    std::string id;                       // an items.json weapon id, "unarmed", or "default"
    std::vector<MissBackRule> missBack;   // sorted by lv
    int missWaitMs = 0;                   // after a miss, the wait before the next tap counts (the sword's "put back")
};

struct WeaponPlayTable {
    std::map<std::string, WeaponPlay> weapons;
    // The weapon's settings: its own entry, else "default", else built-in (a miss goes back to the start).
    const WeaponPlay& get(const std::string& weaponId) const;
};

WeaponPlay weaponPlayFromJson(const std::string& id, const nlohmann::json& j);
// data/weapon_play.json; a missing or broken file gives an empty table (every weapon on the built-in rule).
WeaponPlayTable loadWeaponPlay(const std::string& path);

// The rule for a weapon level: how many points a miss goes back (-1: to the start).
int missBackPoints(const WeaponPlay& w, int weaponLevel);
// The combo step after a miss at `step` (0 = the start): never below the start.
int stepAfterMiss(const WeaponPlay& w, int weaponLevel, int step);

} // namespace toms
