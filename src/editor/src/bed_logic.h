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
    int m_iWeaponLevel = 1;                 // its level
    ojson m_jAttrs = ojson::object();       // { "str": 3, ... } placed points
    int m_iAttrPoints = 0;                  // points still to place
    bool m_bUltimateReady = false;          // the battle starts with the ultimate's charge full (scenario "super")
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
// The words a setup is found by: its name, its weapon (id and words, "memory_blade" -> "memory blade"), its monsters, its pace.
std::string f_SetupSearchText(const std::string& l_strName, const Setup& l_oS);
// The setups filter: every word of `filter` (spaces between) is in `text`, any case. An empty filter matches all.
bool f_FilterMatches(const std::string& l_strText, const std::string& l_strFilter);

// ---- writing the data files back ----

// JSON in the hand-made layout of enemies.json: objects and arrays at depth >= `inlineFrom` that fit in `width`
// characters go on one line ({ "a": 1 }, ["x", "y"]); the rest one entry per line, two spaces per level.
std::string f_DumpCompact(const ojson& l_jJ, int l_iInlineFrom = 2, int l_iWidth = 160);

// stats.json with only its "levelUp" "points" array replaced (the rest of the file's text untouched).
// False (text unchanged) when the file has no such array.
bool f_ReplaceLevelPoints(std::string& l_strStatsText, const ojson& l_jRows);

// "strike:70, boss_near:30" -> [{ "id": "strike", "chance": 70 }, ...] (enemies.json "moves"; "weight" is read too) and back.
ojson f_MovesFromText(const std::string& l_strText);
std::string f_MovesToText(const ojson& l_jMoves);
// "bat, bat" -> ["bat", "bat"] and back.
ojson f_IdsFromText(const std::string& l_strText);
std::string f_IdsToText(const ojson& l_jIds);

// ---- the staff's symbols (data/circle_symbols.json; the Symbols tab; docs/gameplay/22 §5.1) ----
// A curve as the owner's engine has it (FM79979 cCurveWithTime): control points with a time each, smoothed by LOD (each
// pass cuts every corner at a quarter and three quarters of its segments, keeping the two ends), a position found at a
// time between the two points around it. Points are 0..1 across and down the play's area.

struct SymPoint { double m_dX = 0.0, m_dY = 0.0; };

// LOD 1: the points as they are; each more: one corner-cutting pass ((n-1)*2^(L-1)+1 points). The times the same way.
std::vector<SymPoint> f_CurveLod(const std::vector<SymPoint>& l_vPoints, int l_iLod);
std::vector<double> f_CurveLodTimes(const std::vector<double>& l_vTimes, int l_iLod);
double f_CurveLength(const std::vector<SymPoint>& l_vPoints);
// The place at time t along points with their times (before the first: the first; after the last: the last).
SymPoint f_CurveAt(const std::vector<SymPoint>& l_vPoints, const std::vector<double>& l_vTimes, double l_dT);
// n places evenly by distance along the points (the first and last included), with the time there (from l_vTimes).
void f_CurveSample(const std::vector<SymPoint>& l_vPoints, const std::vector<double>& l_vTimes, int l_iN,
                   std::vector<SymPoint>& l_vOutPoints, std::vector<double>& l_vOutTimes);
// Times from 0 to total: by distance along the points (an even speed), or by index (each step the same time).
std::vector<double> f_TimesByDistance(const std::vector<SymPoint>& l_vPoints, double l_dTotal);
std::vector<double> f_TimesByIndex(size_t l_nCount, double l_dTotal);

// circle_symbols.json written in its own layout: a symbol's keys one a line, its points as [x, y] pairs; a weapon's
// rows one a line; the rest as it was.
std::string f_WriteSymbolsFile(const ojson& l_jFile);

} // namespace bed
