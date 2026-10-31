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
    std::string clip;    // the clip's name in the fighter's .anim ("" = the status's own name)
    std::string sound;   // a sound id ("" = none)
    std::string fx;      // an effect in media/fx/battle.particle ("" = none)
};

struct Look {
    std::string base;                         // another look whose states this one starts from
    std::map<std::string, LookState> states;
};

struct LooksTable {
    std::map<std::string, Look> looks;
    // The state of a look, following "base" (up to a few steps); a look that is not there falls back to `fallback`.
    // Null when no look in the chain has that state.
    const LookState* find(const std::string& look, const std::string& state, const std::string& fallback = "monster") const;
};

LooksTable looksFromJson(const nlohmann::json& j);

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
    std::string on;
    float value = 0.0f;
    bool once = true;
    nlohmann::json actions = nlohmann::json::array();
    int fired = 0;           // times it fired in this battle
};

std::vector<MonsterEvent> monsterEventsFromJson(const nlohmann::json& j);

// What a check of one trigger needs to know.
struct EventCheck {
    std::string on;
    float hpPctBefore = 100.0f, hpPctAfter = 100.0f;   // hpBelow / playerHpBelow: the share before and after the hit
    int attacks = 0;                                   // attacks: its attacks so far
    int msBefore = 0, msAfter = 0;                     // everyMs: battle time before and after this step
};

// The events that fire for this check (indices into `events`); each one returned is counted as fired.
std::vector<int> fireMonsterEvents(std::vector<MonsterEvent>& events, const EventCheck& check);

// ---- moves: what a monster does next (its intent) ----

enum class MoveKind {
    Strike,   // a plain attack (today's): guard to soften it
    Heavy,    // a heavy blow: a perfect guard parries it (no damage, the monster staggers and takes more)
    Jabs,     // quick jabs: several small hits; they do not wait for a running combo, and interrupt it
    Spell,    // a charging spell: a combo matching enough points breaks it; a guard only halves it
    Guard,    // guarding: hits on it do little, a fully charged attack breaks the guard; it deals no damage
};

struct MoveDef {
    std::string id;
    MoveKind kind = MoveKind::Strike;
    float damage = 1.0f;      // times its attack
    int hits = 1;             // jabs: how many
    float time = 1.0f;        // its wind-up: times the monster's attack interval
    int breakHits = 2;        // spell: the points a combo must match to break it
    float reduce = 0.3f;      // guard: the share of damage that gets through
    std::string text;         // the intent's words (a text.json key)
    std::string icon;         // the intent's icon (a sprite name; "" = the words only)
    // Its rhythm (docs/gameplay/16 §4.1): a wind-up, then `hits` hits with gaps, each with an "!" before it.
    int chargeMs = 0;           // the wind-up in ms (0: `time` x the monster's attack interval)
    std::vector<int> gapsMs;    // the time before each next hit (one value: every gap); empty: the hits land at once
    int warnMs = -1;            // the "!" before each hit (-1: battle.json guard.warnMs)
    std::string guardPlay;      // a skills.json id: a special guard play instead of the button (16 §4.3)
    int gapBefore(int hit) const;   // hit 1, 2, ...: the gap before it (0 for the first)
};

struct MovesTable {
    std::map<std::string, MoveDef> moves;
    const MoveDef& get(const std::string& id) const;   // unknown: a plain strike
};

MoveKind moveKindFromString(const std::string& s);
const char* moveKindName(MoveKind k);
MovesTable movesFromJson(const nlohmann::json& j);

// A monster's moves (enemies.json "moves": ["strike", {"id": "heavy", "weight": 2}]); none: a plain strike.
struct MoveChoice { std::string id; int weight = 1; };
std::vector<MoveChoice> moveChoicesFromJson(const nlohmann::json& j);
// The move for a roll in [0, 1).
std::string pickMove(const std::vector<MoveChoice>& choices, float roll);

// ---- the pace (data/battle.json "pace", a weapon's or a battle setup's "pace") ----

enum class Pace { Cooldown, Stamina };

struct PaceSettings {
    Pace pace = Pace::Cooldown;
    int staminaMax = 100;
    float staminaRegenPerS = 30.0f;   // refills while no combo runs
    int attackCost = 40;              // a combo's start
    int guardCost = 20;               // a guard tap
};

Pace paceFromString(const std::string& s, Pace fallback);
const char* paceName(Pace p);
// battle.json's "pace" and "stamina" ({"max", "regenPerS", "attack", "guard"}).
PaceSettings paceFromJson(const nlohmann::json& battleJson);

// ---- break (stagger) ----

struct BreakSettings {
    int points = 8;          // matched points to fill a monster's gauge (enemies.json "break" overrides it)
    int brokenMs = 3000;     // how long a broken monster is stunned
    float brokenDamage = 1.5f;
    int parryMs = 1500;      // a parried heavy blow: the monster staggers this long...
    float parryDamage = 2.0f;   // ...and takes this much more
    int parryPower = 85;     // the guard's power (0..100) that parries
    int parryBreak = 3;      // a parry adds this much to the gauge
    int guardBreak = 2;      // breaking a guard adds this much
};

BreakSettings breakFromJson(const nlohmann::json& battleJson);

// The gauge after adding `add` points: true when it filled (the gauge is then emptied).
bool addBreak(float& gauge, float add, int points);

// ---- the revamp (docs/gameplay/15_BATTLE_REVAMP.md) ----

// The feel (15 §3.8; data/battle.json "feel"): what a moment does to the screen. Moments: "hit", "great", "perfect",
// "miss", "parry", "block", "break", "hurt", "heavyHurt", "ultimate", "phase". Unknown moments do nothing.
struct FeelSettings {
    float strength = 1.0f;                     // scales every stop and shake (Q34: the owner's "how strong")
    std::map<std::string, int> stopMs;         // hit stop: both sides freeze this long
    std::map<std::string, float> shakePx;      // screen shake: this far
    int shakeMs = 250;                         // and this long
    std::map<std::string, std::string> sounds; // a sound id per moment
    int slowMoMs = 450;                        // the last hit of a battle: this long...
    float slowMoScale = 0.3f;                  // ...at this speed
    float reducedScale = 0.25f;                // "Reduce shaking and flashes": stops and shakes times this, no slow motion
    float chainStep = 0.1f;                    // each perfect in a row: +10 % damage...
    int chainMax = 10;                         // ...up to this many
    int stop(const std::string& moment, bool reduced) const;
    float shake(const std::string& moment, bool reduced) const;
    std::string sound(const std::string& moment) const;
};
FeelSettings feelFromJson(const nlohmann::json& battleJson);

// The grade of a won battle (15 §3.6, Q35; data/battle.json "grade"): S / A / B / C from its time, the HP lost and the
// perfects. score = 100 - over par s x timePenalty - lost share x damagePenalty + perfect rate x perfectBonus
//                   + best chain x chainBonus.
struct GradeSettings {
    float parSeconds = 25.0f, timePenalty = 1.5f, damagePenalty = 60.0f, perfectBonus = 30.0f, chainBonus = 3.0f;
    float s = 95.0f, a = 75.0f, b = 55.0f;     // the lowest score of each grade
    float sGold = 0.2f;                        // an S: +20 % gold...
    float sCard = 0.25f;                       // ...and a card a quarter likelier
};
struct GradeInput { float seconds = 0, lostShare = 0, perfectRate = 0; int bestChain = 0; };
GradeSettings gradeFromJson(const nlohmann::json& battleJson);
std::string battleGrade(const GradeInput& in, const GradeSettings& g, int* score = nullptr);

// The guard (15 §3.2; data/battle.json "guard"): "timing" = one button, pressed just before a hit; "bar" = the
// five-zone bar (Battle v2) kept as an option.
struct GuardSettings {
    // "warn" (docs/gameplay/16 §4.2): only while the "!" shows over the monster (warnMs before each hit); the first
    // parryShare of it is a parry, the rest a block; a press with no "!" locks the guard missLockMs. A press stops the
    // player's own attack. "timing": pressed just before the hit (15 §3.2). "bar": the five-zone bar.
    std::string mode = "timing";
    int warnMs = 500;
    float parryShare = 0.4f;
    int missLockMs = 400;
    int parryMs = 160;      // pressed at most this long before the hit: a parry (no damage; a heavy blow staggers)
    int blockMs = 550;      // ...this long: blocked (blockPower)
    int earlyMs = 1200;     // ...this long: too early (full damage, and the player is told)
    float blockPower = 70.0f;
    int cooldownMs = 700;   // between two presses (no parrying by tapping all the time)
};
enum class GuardTiming { None, Parry, Block, Early };
GuardSettings guardFromJson(const nlohmann::json& battleJson);
// What a guard pressed `sinceMs` before a hit does (sinceMs < 0: not pressed).
GuardTiming guardTiming(int sinceMs, const GuardSettings& g);
// The "warn" mode: a press `sinceMs` before a hit whose "!" showed `warnMs` (pressed while it showed: a parry in its
// first parryShare, else a block; before it, or not pressed: none).
GuardTiming guardWarn(int sinceMs, int warnMs, const GuardSettings& g);

} // namespace toms
