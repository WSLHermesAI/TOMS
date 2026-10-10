// player_menu_rules.h — the player menu's small rules as plain functions (docs/20_PLAYER_MENU.md), so
// player_menu_test.cpp can check them without a running game: stacking the backpack, the 3-per-row
// grid cursor, what may be dropped, and level-ups by data/stats.json's "levelUp".
#pragma once
#include <string>
#include <vector>

namespace toms {

struct StackedItem {
    std::string id;
    int count = 1;
};

// The backpack holds one id per copy; the Items tab shows one cell per id with a count, in the order
// each id was first picked up.
inline std::vector<StackedItem> stackItems(const std::vector<std::string>& inv) {
    std::vector<StackedItem> out;
    for (const std::string& id : inv) {
        bool found = false;
        for (StackedItem& s : out)
            if (s.id == id) { s.count++; found = true; break; }
        if (!found) out.push_back({id, 1});
    }
    return out;
}

// Moves a grid cursor (`cols` per row, `n` cells) by one cell or one row. Returns false when a left /
// right move would leave the grid -- the caller then switches the filter chip instead.
inline bool gridMove(int& sel, int n, int dx, int dy, int cols = 3) {
    if (n <= 0) return dx == 0;
    if (sel < 0) sel = 0;
    if (sel > n - 1) sel = n - 1;
    int r = sel / cols, c = sel % cols;
    if (dx < 0 && c == 0) return false;
    if (dx > 0 && (c == cols - 1 || sel == n - 1)) return false;
    c += dx;
    r += dy;
    const int maxr = (n - 1) / cols;
    if (r < 0) r = 0;
    if (r > maxr) r = maxr;
    sel = r * cols + c;
    if (sel > n - 1) sel = n - 1;
    return true;
}

// Keys and items marked "important" are never dropped, nor is the gear being worn.
inline bool canDrop(bool important, bool worn) { return !important && !worn; }

// Spends EXP on levels: each level needs lv * expPerLevel. Returns how many levels were gained.
inline int levelUps(int& exp, int& lv, int expPerLevel) {
    if (expPerLevel < 1) expPerLevel = 1;
    int gained = 0;
    while (exp >= lv * expPerLevel) {
        exp -= lv * expPerLevel;
        lv++;
        gained++;
    }
    return gained;
}

}  // namespace toms
