// bed_logic.h -- battle_editor's logic with no window (docs/gameplay/13_BATTLE_EDITOR.md): the level growth calculator, the
// test setup written as a scenario, and the data files written back in their own layout. Tested by --selftest.
#pragma once
#include <json.hpp>

#include <string>
#include <vector>

namespace bed {

using ojson = nlohmann::ordered_json;   // the data files keep their key order when written back

// ---- the growth calculator (13 §4.3) ----

// The attribute points levels 2 .. L give, on a curve, adding up to exactly `total`:
//   level n gets total * ((n-1)^k - (n-2)^k) / (L-1)^k, rounded so the sum stays exact (the largest remainders get the
//   extra point; a tie goes to the later level). k = 1: the same every level; above 1: more later; below 1: more early.
// Element 0 is level 2. Empty when L < 2.
std::vector<int> levelPoints(int total, int maxLevel, double k);

// The rows of data/stats.json "levelUp.points" ({ "from": level, "points": n }) for those levels, neighbours with the same
// points merged; `oldRows` past maxLevel are kept (and the old value goes on from maxLevel + 1).
ojson pointsRows(const std::vector<int>& perLevel, int maxLevel, const ojson& oldRows);
// The points the old table gives at a level (the row with the highest "from" at or below it; 0 below every row).
int pointsAt(const ojson& rows, int level);

// ---- the test setup (13 §2): a battle scenario (docs/gameplay/08_TEST_SCENARIOS.md) ----

struct SetupFoe {
    std::string id;           // enemies.json id
    ojson overrides = ojson::object();   // hp, atk, def, atk_interval_ms, moves, look, break, events (the editor's edits)
};

struct TestPlayer {
    int lv = 1, hp = 120, maxhp = 120;
    std::string weapon = "bow";          // items.json id; "" = unarmed
    int weaponWins = 0;                  // its wins (its level)
    ojson attrs = ojson::object();       // { "str": 3, ... } placed points
    int attrPoints = 0;                  // points still to place
};

struct Setup {
    std::string name;
    std::vector<SetupFoe> foes;          // 1..3 (the "+ Battle" ticks, in order)
    TestPlayer player;
    std::string pace;                    // "cooldown" / "stamina" / "" (the data's)
};

// The scenario a setup runs as (scene "battle"); the setup itself is kept under "_editor" so it loads back exactly.
ojson setupToScenario(const Setup& s);
// A saved setup (a scenario written by setupToScenario). False when it is not one.
bool setupFromScenario(const ojson& j, Setup& out);

// ---- writing the data files back ----

// JSON in the hand-made layout of enemies.json: objects and arrays at depth >= `inlineFrom` that fit in `width`
// characters go on one line ({ "a": 1 }, ["x", "y"]); the rest one entry per line, two spaces per level.
std::string dumpCompact(const ojson& j, int inlineFrom = 2, int width = 160);

// stats.json with only its "levelUp" "points" array replaced (the rest of the file's text untouched).
// False (text unchanged) when the file has no such array.
bool replaceLevelPoints(std::string& statsText, const ojson& rows);

// "strike, heavy:2" -> ["strike", { "id": "heavy", "weight": 2 }] (enemies.json "moves") and back.
ojson movesFromText(const std::string& text);
std::string movesToText(const ojson& moves);
// "bat, bat" -> ["bat", "bat"] and back.
ojson idsFromText(const std::string& text);
std::string idsToText(const ojson& ids);

} // namespace bed
