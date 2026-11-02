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
std::vector<int> f_LevelPoints(int l_iTotal, int l_iMaxLevel, double l_dK);

// The rows of data/stats.json "levelUp.points" ({ "from": level, "points": n }) for those levels, neighbours with the same
// points merged; `oldRows` past maxLevel are kept (and the old value goes on from maxLevel + 1).
ojson f_PointsRows(const std::vector<int>& l_vPerLevel, int l_iMaxLevel, const ojson& l_jOldRows);
// The points the old table gives at a level (the row with the highest "from" at or below it; 0 below every row).
int f_PointsAt(const ojson& l_jRows, int l_iLevel);

// ---- the test setup (13 §2): a battle scenario (docs/gameplay/08_TEST_SCENARIOS.md) ----

struct SetupFoe {
    std::string m_strId;           // enemies.json id
    ojson m_jOverrides = ojson::object();   // hp, atk, def, atk_interval_ms, moves, look, break, events (the editor's edits)
};

struct TestPlayer {
    int m_iLv = 1, m_iHp = 120, m_iMaxhp = 120;
    std::string m_strWeapon = "bow";          // items.json id; "" = unarmed
    int m_iWeaponWins = 0;                  // its wins (its level)
    ojson m_jAttrs = ojson::object();       // { "str": 3, ... } placed points
    int m_iAttrPoints = 0;                  // points still to place
};

struct Setup {
    std::string m_strName;
    std::vector<SetupFoe> m_vFoes;          // 1..3 (the "+ Battle" ticks, in order)
    TestPlayer m_oPlayer;
    std::string m_strPace;                    // "cooldown" / "stamina" / "" (the data's)
};

// The scenario a setup runs as (scene "battle"); the setup itself is kept under "_editor" so it loads back exactly.
ojson f_SetupToScenario(const Setup& l_oS);
// A saved setup (a scenario written by setupToScenario). False when it is not one.
bool f_SetupFromScenario(const ojson& l_jJ, Setup& l_oOut);

// ---- writing the data files back ----

// JSON in the hand-made layout of enemies.json: objects and arrays at depth >= `inlineFrom` that fit in `width`
// characters go on one line ({ "a": 1 }, ["x", "y"]); the rest one entry per line, two spaces per level.
std::string f_DumpCompact(const ojson& l_jJ, int l_iInlineFrom = 2, int l_iWidth = 160);

// stats.json with only its "levelUp" "points" array replaced (the rest of the file's text untouched).
// False (text unchanged) when the file has no such array.
bool f_ReplaceLevelPoints(std::string& l_strStatsText, const ojson& l_jRows);

// "strike, heavy:2" -> ["strike", { "id": "heavy", "weight": 2 }] (enemies.json "moves") and back.
ojson f_MovesFromText(const std::string& l_strText);
std::string f_MovesToText(const ojson& l_jMoves);
// "bat, bat" -> ["bat", "bat"] and back.
ojson f_IdsFromText(const std::string& l_strText);
std::string f_IdsToText(const ojson& l_jIds);

} // namespace bed
