// battle_rules.h -- the battle's data-driven rules (docs/gameplay/13_BATTLE_EDITOR.md, docs/gameplay/11_BATTLE_DATA.md):
//   - looks (data/looks.json): each status's clip, sound and particle (11 §4.2);
//   - monster events (enemies.json "events"): a trigger, a condition, actions (13 §4.4);
//   - moves (data/moves.json): what a monster does next, shown before it does it (its intent), and the answer it needs
//     (13 §6.1, 6.2);
//   - the pace (data/battle.json "pace"): cooldowns after a tap, or stamina (13 §6.3);
//   - the break gauge (13 §6.4).
//
// Pure content + pure functions, no Game dependency (the same shape as weapon_play.h); Game runs what they decide.
#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

namespace toms {

// ---- looks: a status's clip, sound and particle ----

struct LookState {
    std::string m_strClip;    // the clip's name in the fighter's .anim ("" = the status's own name)
    std::string m_strSound;   // a sound id ("" = none)
    std::string m_strFx;      // an effect in media/fx/battle.particle ("" = none)
};

struct Look {
    std::string m_strBase;                         // another look whose states this one starts from
    // How the fighter is drawn (docs/gameplay/18_FIGHTER_VIEWS.md, FighterView): "render" "2d" | "3d" ("" = the base's,
    // else 2d), "character" its .anim ("" = the base's, else media/anim/monster_battle.anim), "model" its .glb (3d).
    std::string m_strRender, m_strCharacter, m_strModel;
    std::map<std::string, LookState> m_mapStates;
};

struct LooksTable {
    std::map<std::string, Look> m_mapLooks;
    // The state of a look, following "base" (up to a few steps); a look that is not there falls back to `fallback`.
    // Null when no look in the chain has that state.
    const LookState* f_Find(const std::string& l_strLook, const std::string& l_strState, const std::string& l_strFallback = "monster") const;
};

LooksTable f_LooksFromJson(const nlohmann::json& l_jJ);

// ---- monster events ----

// One event of a monster (enemies.json "events": [{ "on": "hpBelow", "value": 50, "once": true, "do": [...] }]).
// Triggers (on):
//   beforeBattle   as the battle starts, before anything else (its "say" is the battle's first line)
//   battleStart    the battle has started
//   hpBelow        its HP fell below value % (checked after each hit on it)
//   playerHpBelow  the player's HP fell below value %
//   attacks        after its value-th attack (once), or every value-th (once: false)
//   everyMs        every value ms of battle time
//   perfectHit     a perfect combo hit it
//   falls          it fell (the last action can heal it: it stands again)
// Actions (do), each an object with one of these keys:
//   say: "text"                    a line in the battle log (a text.json key, or the words themselves)
//   stat: "atk"|"def"|"interval", mul: x, add: n     changes it for the rest of the battle
//   transformInto: "id", hp: "keepShare"|"full"     becomes another monster
//   heal: pct                      heals pct % of its max HP
//   summon: "id"                   another monster joins (while fewer than three)
//   flee: true                     it leaves the battle (no reward for it)
//   anim: "clip", fx: "effect", sound: "id"         shows something
//   flag: "story_flag"             sets a story flag
struct MonsterEvent {
    std::string m_strOn;
    float m_fValue = 0.0f;
    bool m_bOnce = true;
    nlohmann::json m_jActions = nlohmann::json::array();
    int m_iFired = 0;           // times it fired in this battle
};

std::vector<MonsterEvent> f_MonsterEventsFromJson(const nlohmann::json& l_jJ);

// What a check of one trigger needs to know.
struct EventCheck {
    std::string m_strOn;
    float m_fHpPctBefore = 100.0f, m_fHpPctAfter = 100.0f;   // hpBelow / playerHpBelow: the share before and after the hit
    int m_iAttacks = 0;                                   // attacks: its attacks so far
    int m_iMsBefore = 0, m_iMsAfter = 0;                     // everyMs: battle time before and after this step
};

// The events that fire for this check (indices into `events`); each one returned is counted as fired.
std::vector<int> f_FireMonsterEvents(std::vector<MonsterEvent>& l_vEvents, const EventCheck& l_oCheck);

// ---- moves: what a monster does next (its intent) ----

enum class MoveKind {
    Strike,   // a plain attack (today's): guard to soften it
    Heavy,    // a heavy blow: a perfect guard parries it (no damage, the monster staggers and takes more)
    Jabs,     // quick jabs: several small hits; they do not wait for a running combo, and interrupt it
    Spell,    // a charging spell: a combo matching enough points breaks it; a guard only halves it
    Guard,    // guarding: hits on it do little, a fully charged attack breaks the guard; it deals no damage
};

struct MoveDef {
    std::string m_strId;
    MoveKind m_eKind = MoveKind::Strike;
    float m_fDamage = 1.0f;      // times its attack
    int m_iHits = 1;             // jabs: how many
    float m_fTime = 1.0f;        // its wind-up: times the monster's attack interval
    int m_iBreakHits = 2;        // spell: the points a combo must match to break it
    float m_fReduce = 0.3f;      // guard: the share of damage that gets through
    std::string m_strText;         // the intent's words (a text.json key)
    std::string m_strIcon;         // the intent's icon (a sprite name; "" = the words only)
    // Its rhythm (docs/gameplay/16 §4.1): a wind-up, then `hits` hits with gaps, each with an "!" before it.
    int m_iChargeMs = 0;           // the wind-up in ms (0: `time` x the monster's attack interval)
    std::vector<int> m_vGapsMs;    // the time before each next hit (one value: every gap); empty: the hits land at once
    int m_iWarnMs = -1;            // the "!" before each hit (-1: battle.json guard.warnMs)
    std::string m_strGuardPlay;      // a skills.json id: a special guard play instead of the button (16 §4.3)
    int f_GapBefore(int l_iHit) const;   // hit 1, 2, ...: the gap before it (0 for the first)
};

struct MovesTable {
    std::map<std::string, MoveDef> m_mapMoves;
    const MoveDef& f_Get(const std::string& l_strId) const;   // unknown: a plain strike
};

MoveKind f_MoveKindFromString(const std::string& l_strS);
const char* f_MoveKindName(MoveKind l_eK);
MovesTable f_MovesFromJson(const nlohmann::json& l_jJ);

// A monster's moves (enemies.json "moves": ["strike", {"id": "heavy", "weight": 2}]); none: a plain strike.
struct MoveChoice { std::string m_strId; int m_iWeight = 1; };
std::vector<MoveChoice> f_MoveChoicesFromJson(const nlohmann::json& l_jJ);
// The move for a roll in [0, 1).
std::string f_PickMove(const std::vector<MoveChoice>& l_vChoices, float l_fRoll);

// ---- the pace (data/battle.json "pace", a weapon's or a battle setup's "pace") ----

enum class Pace { Cooldown, Stamina };

struct PaceSettings {
    Pace m_ePace = Pace::Cooldown;
    int m_iStaminaMax = 100;
    float m_fStaminaRegenPerS = 30.0f;   // refills while no combo runs
    int m_iAttackCost = 40;              // a combo's start
    int m_iGuardCost = 20;               // a guard tap
};

Pace f_PaceFromString(const std::string& l_strS, Pace l_eFallback);
const char* f_PaceName(Pace l_eP);
// battle.json's "pace" and "stamina" ({"max", "regenPerS", "attack", "guard"}).
PaceSettings f_PaceFromJson(const nlohmann::json& l_jBattleJson);

// ---- break (stagger) ----

struct BreakSettings {
    int m_iPoints = 8;          // matched points to fill a monster's gauge (enemies.json "break" overrides it)
    int m_iBrokenMs = 3000;     // how long a broken monster is stunned
    float m_fBrokenDamage = 1.5f;
    int m_iParryMs = 1500;      // a parried heavy blow: the monster staggers this long...
    float m_fParryDamage = 2.0f;   // ...and takes this much more
    int m_iParryPower = 85;     // the guard's power (0..100) that parries
    int m_iParryBreak = 3;      // a parry adds this much to the gauge
    int m_iGuardBreak = 2;      // breaking a guard adds this much
};

BreakSettings f_BreakFromJson(const nlohmann::json& l_jBattleJson);

// The gauge after adding `add` points: true when it filled (the gauge is then emptied).
bool f_AddBreak(float& l_fGauge, float l_fAdd, int l_iPoints);

// ---- the revamp (docs/gameplay/15_BATTLE_REVAMP.md) ----

// The feel (15 §3.8; data/battle.json "feel"): what a moment does to the screen. Moments: "hit", "great", "perfect",
// "miss", "parry", "block", "break", "hurt", "heavyHurt", "ultimate", "phase". Unknown moments do nothing.
struct FeelSettings {
    float m_fStrength = 1.0f;                     // scales every stop and shake (Q34: the owner's "how strong")
    std::map<std::string, int> m_mapStopMs;         // hit stop: both sides freeze this long
    std::map<std::string, float> m_mapShakePx;      // screen shake: this far
    int m_iShakeMs = 250;                         // and this long
    std::map<std::string, std::string> m_mapSounds; // a sound id per moment
    int m_iSlowMoMs = 450;                        // the last hit of a battle: this long...
    float m_fSlowMoScale = 0.3f;                  // ...at this speed
    float m_fReducedScale = 0.25f;                // "Reduce shaking and flashes": stops and shakes times this, no slow motion
    float m_fChainStep = 0.1f;                    // each perfect in a row: +10 % damage...
    int m_iChainMax = 10;                         // ...up to this many
    int f_Stop(const std::string& l_strMoment, bool l_bReduced) const;
    float f_Shake(const std::string& l_strMoment, bool l_bReduced) const;
    std::string f_Sound(const std::string& l_strMoment) const;
};
FeelSettings f_FeelFromJson(const nlohmann::json& l_jBattleJson);

// The grade of a won battle (15 §3.6, Q35; data/battle.json "grade"): S / A / B / C from its time, the HP lost and the
// perfects. score = 100 - over par s x timePenalty - lost share x damagePenalty + perfect rate x perfectBonus
//                   + best chain x chainBonus.
struct GradeSettings {
    float m_fParSeconds = 25.0f, m_fTimePenalty = 1.5f, m_fDamagePenalty = 60.0f, m_fPerfectBonus = 30.0f, m_fChainBonus = 3.0f;
    float m_fS = 95.0f, m_fA = 75.0f, m_fB = 55.0f;     // the lowest score of each grade
    float m_fSGold = 0.2f;                        // an S: +20 % gold...
    float m_fSCard = 0.25f;                       // ...and a card a quarter likelier
};
struct GradeInput { float m_fSeconds = 0, m_fLostShare = 0, m_fPerfectRate = 0; int m_iBestChain = 0; };
GradeSettings f_GradeFromJson(const nlohmann::json& l_jBattleJson);
std::string f_BattleGrade(const GradeInput& l_oIn, const GradeSettings& l_oG, int* l_pScore = nullptr);

// The guard (15 §3.2; data/battle.json "guard"): "timing" = one button, pressed just before a hit; "bar" = the
// five-zone bar (Battle v2) kept as an option.
struct GuardSettings {
    // "warn" (docs/gameplay/16 §4.2): only while the "!" shows over the monster (warnMs before each hit); the first
    // parryShare of it is a parry, the rest a block; a press with no "!" locks the guard missLockMs. A press stops the
    // player's own attack. "timing": pressed just before the hit (15 §3.2). "bar": the five-zone bar.
    std::string m_strMode = "timing";
    int m_iWarnMs = 500;
    float m_fParryShare = 0.4f;
    int m_iMissLockMs = 400;
    int m_iParryMs = 160;      // pressed at most this long before the hit: a parry (no damage; a heavy blow staggers)
    int m_iBlockMs = 550;      // ...this long: blocked (blockPower)
    int m_iEarlyMs = 1200;     // ...this long: too early (full damage, and the player is told)
    float m_fBlockPower = 70.0f;
    int m_iCooldownMs = 700;   // between two presses (no parrying by tapping all the time)
};
enum class GuardTiming { None, Parry, Block, Early };
GuardSettings f_GuardFromJson(const nlohmann::json& l_jBattleJson);
// What a guard pressed `sinceMs` before a hit does (sinceMs < 0: not pressed).
GuardTiming f_GuardTiming(int l_iSinceMs, const GuardSettings& l_oG);
// The "warn" mode: a press `sinceMs` before a hit whose "!" showed `warnMs` (pressed while it showed: a parry in its
// first parryShare, else a block; before it, or not pressed: none).
GuardTiming f_GuardWarn(int l_iSinceMs, int l_iWarnMs, const GuardSettings& l_oG);

} // namespace toms
