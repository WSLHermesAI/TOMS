// weapon_play.h -- each weapon's play settings, from data/weapon_play.json (docs/gameplay/07_GAMEPLAY_PLAN.md §5.6).
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

// One weapon level (docs/gameplay/07 phase 1, the owner 2026-10-22): how many points the combo track has, and the attribute
// bonuses the weapon gives at that level (data/stats.json ids: "str", "agi", ...).
struct WeaponLevel {
    int lv = 1;
    int points = 3;                       // 1 .. kMaxComboPoints
    std::map<std::string, int> stats;
};
constexpr int kMaxComboPoints = 5;

// The battle's particles for a weapon level and up (the owner 2026-10-24: a higher level attacks with bigger effects).
// Effect names in media/fx/battle.particle; "" = none at that moment.
struct WeaponFx {
    int lv = 1;
    std::string release;   // at the player when the attack goes off
    std::string hit;       // at the monster when the hit lands (with the damage number)
};

struct WeaponPlay {
    std::string id;                       // an items.json weapon id, "unarmed", or "default"
    std::vector<MissBackRule> missBack;   // sorted by lv
    int missWaitMs = 0;                   // after a miss, the wait before the next tap counts (the sword's "put back")
    // Battle v3's point play (docs/gameplay/07 §4, §5.1): the marker sweeps a track with the level's points.
    std::vector<WeaponLevel> levels;      // sorted by lv; empty: Lv 1 with 3 points, no bonus
    std::vector<int> usesToLevel;         // wins needed from Lv 1 to 2, 2 to 3, ...; the level stops at levels.back()
    bool turnBack = true;                 // the marker turns back at the ends (a missed point can be saved); false: one sweep
    int sweepMs = 1600;                   // one end-to-end sweep with no charge
    int maxSweeps = 3;                    // turning back: the combo ends after this many sweeps
    int chargeMs = 2000;                  // waiting this long fills the charge
    float chargeSpeedUp = 1.0f;           // a full charge makes the marker this much faster (1: twice as fast)
    float chargeDamage = 1.0f;            // a full charge adds this much damage (1: twice the damage)
    float hitWindow = 0.07f;              // how close (a fraction of the track) a tap must be to a point
    int hitWindowMs = 140;                // ...but never shorter than this in time (the marker's speed; combo_play.h)
    int lateMs = 80;                      // a tap this long after the marker passed a point still hits it
    std::vector<WeaponFx> fx;             // sorted by lv; empty: the "default" entry's
};

struct WeaponPlayTable {
    std::map<std::string, WeaponPlay> weapons;
    // The weapon's settings: its own entry, else "default", else built-in (a miss goes back to the start).
    const WeaponPlay& get(const std::string& weaponId) const;
};

WeaponPlay weaponPlayFromJson(const std::string& id, const nlohmann::json& j);
// data/weapon_play.json; a missing or broken file gives an empty table (every weapon on the built-in rule).
WeaponPlayTable loadWeaponPlay(const std::string& path);

// The level entry for a weapon level: the highest at or below it (a built-in Lv 1, 3 points, when none).
const WeaponLevel& weaponLevelEntry(const WeaponPlay& w, int weaponLevel);
// The weapon level reached after `uses` wins with it (Lv 1 at 0; capped at the last level entry).
int weaponLevelFromUses(const WeaponPlay& w, int uses);
// The wins needed from `weaponLevel` to the next level; 0 at the top.
int usesToNextLevel(const WeaponPlay& w, int weaponLevel);
// The highest level the weapon can reach.
int weaponMaxLevel(const WeaponPlay& w);

// The battle particles for a weapon level: the highest fx entry at or below it (null: none; below every entry: the first).
const WeaponFx* weaponFxEntry(const WeaponPlay& w, int weaponLevel);

// The rule for a weapon level: how many points a miss goes back (-1: to the start).
int missBackPoints(const WeaponPlay& w, int weaponLevel);
// The combo step after a miss at `step` (0 = the start): never below the start.
int stepAfterMiss(const WeaponPlay& w, int weaponLevel, int step);

} // namespace toms
