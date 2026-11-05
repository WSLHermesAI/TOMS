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
    int m_iLv = 1;           // applies from this weapon level up (the highest lv at or below the level wins)
    int m_iBack = -1;        // points the combo goes back; -1: to the start
};

// One weapon level (docs/gameplay/07 phase 1, the owner 2026-10-22): how many points the combo track has, and the attribute
// bonuses the weapon gives at that level (data/stats.json ids: "str", "agi", ...).
struct WeaponLevel {
    int m_iLv = 1;
    int m_iPoints = 3;                       // the prompts or circles of an attack, at least 1 (from the file, no upper limit)
    std::map<std::string, int> m_mapStats;
};

// The battle's particles for a weapon level and up (the owner 2026-10-24: a higher level attacks with bigger effects).
// Effect names in media/fx/battle.particle; "" = none at that moment.
struct WeaponFx {
    int m_iLv = 1;
    std::string m_strRelease;   // at the player when the attack goes off
    std::string m_strHit;       // at the monster when the hit lands (with the damage number)
};

// What an attack does by how many points it matched (docs/gameplay/13 §4.2, the owner 2026-10-25): the highest row the
// result reaches applies. hits: the points matched (-1 = all of them).
struct WeaponResult {
    int m_iHits = 0;
    std::string m_strName;            // "perfect", "great", ... (the log and the editor)
    float m_fDamage = 1.0f;         // times the hit's damage
    std::string m_strFx;              // replaces the level's hit effect ("" = the level's)
    std::string m_strAnim;            // the player's clip for it ("" = attack)
    std::string m_strSound;           // "" = the usual
    std::string m_strMove;            // its named move (a text.json key, docs/gameplay/07 §5.4, 15 §3.3): "Tiger Wind" ...
    int m_iStunMs = 0;              // the monster stops this long
    int m_iPushMs = 0;              // its attack clock goes back this much
    float m_fBreakAdd = 0.0f;       // more on its break gauge (13 §6.4)
};

// How the combo grows (docs/gameplay/17 §19): from this many hits in a row on, this clip, hit effect and feel.
struct ComboTier { int m_iFrom = 1; std::string m_strClip, m_strFx, m_strFeel; };

struct WeaponPlay {
    std::string m_strId;                       // an items.json weapon id, "unarmed", or "default"
    std::vector<MissBackRule> m_vMissBack;   // sorted by lv
    int m_iMissWaitMs = 0;                   // after a miss, the wait before the next tap counts (the sword's "put back")
    // Battle v3's point play (docs/gameplay/07 §4, §5.1): the marker sweeps a track with the level's points.
    std::vector<WeaponLevel> m_vLevels;      // sorted by lv; empty: Lv 1 with 3 points, no bonus
    int m_iLevelExpFirst = 5;                // weapon EXP from Lv 1 to 2 ("levelExp"; monsters give it: enemies.json "weaponExp")
    float m_fLevelExpScale = 2.0f;           // each next level needs this many times the last; the level stops at levels.back()
    bool m_bTurnBack = true;                 // the marker turns back at the ends (a missed point can be saved); false: one sweep
    int m_iSweepMs = 1600;                   // one end-to-end sweep with no charge
    int m_iMaxSweeps = 3;                    // turning back: the combo ends after this many sweeps
    int m_iChargeMs = 2000;                  // waiting this long fills the charge
    float m_fChargeSpeedUp = 1.0f;           // a full charge makes the marker this much faster (1: twice as fast)
    float m_fChargeDamage = 1.0f;            // a full charge adds this much damage (1: twice the damage)
    float m_fHitWindow = 0.07f;              // how close (a fraction of the track) a tap must be to a point
    int m_iHitWindowMs = 140;                // ...but never shorter than this in time (the marker's speed; combo_play.h)
    int m_iLateMs = 80;                      // a tap this long after the marker passed a point still hits it
    std::vector<WeaponFx> m_vFx;             // sorted by lv; empty: the "default" entry's
    std::vector<WeaponResult> m_vResults;    // by points matched (13 §4.2); empty: the "default" entry's, else none
    std::string m_strPace;                     // "cooldown" / "stamina": this weapon's pace ("" = the battle's; 13 §6.3)
    std::string m_strPlay = "track";           // its attack: a play type's data name (play_type.h, docs/gameplay/17 §4.1)
    std::vector<ComboTier> m_vComboTiers;      // sorted by from; empty: the plain attack clip
    bool m_bComboKeep = false;                 // the count goes on from attack to attack until a miss (the bow: one arrow an attack)
    const ComboTier* f_ComboTierAt(int l_iCount) const;   // the highest reached; null: none
    std::string m_strUltimate;                 // its ultimate: a special_moves.json id (17 §16 Q49); "": the move naming this weapon, else a harder play
    // the swipe prompts by level (docs/gameplay/17 §4.2): from `lv` on, its last prompts ask for these gestures
    struct SwipeCues { int m_iLv = 1; std::vector<std::string> m_vCues; };
    std::vector<SwipeCues> m_vSwipeCues;     // sorted by lv
    std::vector<std::string> f_SwipeCuesAt(int l_iLv) const;
};

struct WeaponPlayTable {
    std::map<std::string, WeaponPlay> m_mapWeapons;
    // The weapon's settings: its own entry, else "default", else built-in (a miss goes back to the start).
    const WeaponPlay& f_Get(const std::string& l_strWeaponId) const;
};

WeaponPlay f_WeaponPlayFromJson(const std::string& l_strId, const nlohmann::json& l_jJ);
// data/weapon_play.json; a missing or broken file gives an empty table (every weapon on the built-in rule).
WeaponPlayTable f_LoadWeaponPlay(const std::string& l_strPath);

// The level entry for a weapon level: the highest at or below it (a built-in Lv 1, 3 points, when none).
const WeaponLevel& f_WeaponLevelEntry(const WeaponPlay& l_oW, int l_iWeaponLevel);
// The weapon level reached with this much weapon EXP (Lv 1 at 0; capped at the last level entry).
int f_WeaponLevelFromExp(const WeaponPlay& l_oW, int l_iExp);
// The weapon EXP from `weaponLevel` to the next level; 0 at the top.
int f_ExpToNextLevel(const WeaponPlay& l_oW, int l_iWeaponLevel);
// The whole weapon EXP that reaches this level (Lv 1: 0).
int f_WeaponExpForLevel(const WeaponPlay& l_oW, int l_iWeaponLevel);
// The highest level the weapon can reach.
int f_WeaponMaxLevel(const WeaponPlay& l_oW);

// The battle particles for a weapon level: the highest fx entry at or below it (null: none; below every entry: the first).
const WeaponFx* f_WeaponFxEntry(const WeaponPlay& l_oW, int l_iWeaponLevel);

// The result row for a combo that matched `hits` of `points` (failed: none); null when no row is reached.
const WeaponResult* f_WeaponResultFor(const WeaponPlay& l_oW, int l_iHits, int l_iPoints, bool l_bFailed);

// The rule for a weapon level: how many points a miss goes back (-1: to the start).
int f_MissBackPoints(const WeaponPlay& l_oW, int l_iWeaponLevel);
// The combo step after a miss at `step` (0 = the start): never below the start.
int f_StepAfterMiss(const WeaponPlay& l_oW, int l_iWeaponLevel, int l_iStep);

} // namespace toms
