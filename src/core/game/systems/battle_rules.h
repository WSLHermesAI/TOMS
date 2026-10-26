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

} // namespace toms
