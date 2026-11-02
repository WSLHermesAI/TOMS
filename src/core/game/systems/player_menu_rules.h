// player_menu_rules.h — the player menu's small rules as plain functions (docs/gameplay/03_PLAYER_MENU.md), so
// player_menu_test.cpp can check them without a running game: stacking the backpack, the 3-per-row
// grid cursor, what may be dropped, and level-ups by data/stats.json's "levelUp".
#pragma once
#include <string>
#include <vector>

namespace toms {

struct StackedItem {
    std::string m_strId;
    int m_iCount = 1;
};

// The backpack holds one id per copy; the Items tab shows one cell per id with a count, in the order
// each id was first picked up.
inline std::vector<StackedItem> f_StackItems(const std::vector<std::string>& l_vInv) {
    std::vector<StackedItem> l_vOut;
    for (const std::string& l_strId : l_vInv) {
        bool l_bFound = false;
        for (StackedItem& l_oS : l_vOut)
            if (l_oS.m_strId == l_strId) { l_oS.m_iCount++; l_bFound = true; break; }
        if (!l_bFound) l_vOut.push_back({l_strId, 1});
    }
    return l_vOut;
}

// Moves a grid cursor (`cols` per row, `n` cells) by one cell or one row. Returns false when a left /
// right move would leave the grid -- the caller then switches the filter chip instead.
inline bool f_GridMove(int& l_iSel, int l_iN, int l_iDx, int l_iDy, int l_iCols = 3) {
    if (l_iN <= 0) return l_iDx == 0;
    if (l_iSel < 0) l_iSel = 0;
    if (l_iSel > l_iN - 1) l_iSel = l_iN - 1;
    int l_iR = l_iSel / l_iCols, l_iC = l_iSel % l_iCols;
    if (l_iDx < 0 && l_iC == 0) return false;
    if (l_iDx > 0 && (l_iC == l_iCols - 1 || l_iSel == l_iN - 1)) return false;
    l_iC += l_iDx;
    l_iR += l_iDy;
    const int l_iMaxr = (l_iN - 1) / l_iCols;
    if (l_iR < 0) l_iR = 0;
    if (l_iR > l_iMaxr) l_iR = l_iMaxr;
    l_iSel = l_iR * l_iCols + l_iC;
    if (l_iSel > l_iN - 1) l_iSel = l_iN - 1;
    return true;
}

// Keys and items marked "important" are never dropped, nor is the gear being worn.
inline bool f_CanDrop(bool l_bImportant, bool l_bWorn) { return !l_bImportant && !l_bWorn; }

// Spends EXP on levels: each level needs lv * expPerLevel. Returns how many levels were gained. maxLevel > 0 caps the
// level (the EXP past it is kept, for when the cap is raised).
inline int f_LevelUps(int& l_iExp, int& l_iLv, int l_iExpPerLevel, int l_iMaxLevel = 0) {
    if (l_iExpPerLevel < 1) l_iExpPerLevel = 1;
    int l_iGained = 0;
    while ((l_iMaxLevel <= 0 || l_iLv < l_iMaxLevel) && l_iExp >= l_iLv * l_iExpPerLevel) {
        l_iExp -= l_iLv * l_iExpPerLevel;
        l_iLv++;
        l_iGained++;
    }
    return l_iGained;
}

// The attribute points a level gives (data/stats.json levelUp.points, the owner 2026-10-24): the row with the highest
// `from` at or below the level reached; below every row (or no table) the fallback.
struct LevelPoints { int m_iFrom = 2; int m_iPoints = 0; };
inline int f_PointsForLevel(const std::vector<LevelPoints>& l_vTable, int l_iLevelReached, int l_iFallback) {
    int l_iN = l_iFallback, l_iBest = 0;
    for (const LevelPoints& l_oR : l_vTable)
        if (l_oR.m_iFrom <= l_iLevelReached && l_oR.m_iFrom >= l_iBest) { l_iBest = l_oR.m_iFrom; l_iN = l_oR.m_iPoints; }
    return l_iN < 0 ? 0 : l_iN;
}

}  // namespace toms
