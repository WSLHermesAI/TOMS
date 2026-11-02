// atlas_packer.h -- rectangle packing for the atlas tool.
//
//   MaxRects (Jukka Jylänki, "A Thousand Ways to Pack the Bin", 2010) with the five usual
//   placement rules, plus a simple shelf packer (the old PI editor's row layout, done right).
//   packPages() is the front door: fixed rects first (pinned sprites), then the rest, growing the
//   page from small to the maximum size and opening further pages when one is full.
//
// Everything is deterministic: ties go to the lower input index, so the same input gives the same
// atlas on every machine and in the web build.
#pragma once
#include "atlas_image.h"

#include <string>
#include <vector>

namespace atlas {

enum class Heuristic { Auto, BestShortSideFit, BestLongSideFit, BestAreaFit, BottomLeft, ContactPoint, Shelf };
const char* f_HeuristicName(Heuristic l_eH);
bool f_ParseHeuristic(const std::string& l_strS, Heuristic& l_eOut);

class MaxRectsBin {
public:
    MaxRectsBin(int l_iW, int l_iH);
    int f_Width() const { return m_iW; }
    int f_Height() const { return m_iH; }
    // Places a w x h rect; returns an empty rect when it does not fit.
    IRect f_Insert(int l_iW, int l_iH, Heuristic l_eRule);
    // Reserves an exact rect (a pinned sprite). Returns false if it overlaps something already used
    // or lies outside the bin.
    bool f_Reserve(const IRect& l_oR);
    // Score of placing w x h under `rule` (lower is better); false if it does not fit.
    bool f_Score(int l_iW, int l_iH, Heuristic l_eRule, IRect& l_oPlace, int& l_iS1, int& l_iS2) const;
    void f_Commit(const IRect& l_oR);
    double f_Occupancy() const;

private:
    int m_iW, m_iH;
    std::vector<IRect> m_vFree, m_vUsed;
    std::vector<IRect> m_vNew;    // free pieces cut by the last placement, until prune() merges them
    void f_Split(const IRect& l_oUsed);
    void f_Prune();
    int f_ContactScore(const IRect& l_oR) const;
};

struct PackItem {
    int m_iW = 0, m_iH = 0;        // size including padding/extrude
    bool m_bFixed = false;      // pinned: must go at (fixedPage, fixedX, fixedY)
    int m_iFixedPage = 0, m_iFixedX = 0, m_iFixedY = 0;
};

struct PackSettings {
    int m_iMaxWidth = 2048, m_iMaxHeight = 2048;
    int m_iMinWidth = 16, m_iMinHeight = 16;
    bool m_bPowerOfTwo = false;
    bool m_bSquare = false;
    bool m_bFixedSize = false;     // always use maxWidth x maxHeight (no shrinking)
    int m_iBorder = 0;             // empty margin along the page edges
    int m_iPadding = 0;            // empty pixels between two items
    Heuristic m_eHeuristic = Heuristic::Auto;
};

struct PackPlacement { int m_iPage = -1; int m_iX = 0, m_iY = 0; };

struct PackResult {
    std::vector<PackPlacement> m_vPlacements;   // one per item; page -1 = could not be placed
    std::vector<IRect> m_vPageSizes;            // w,h per page (x,y unused)
    std::vector<std::string> m_vErrors;
};

PackResult f_PackPages(const std::vector<PackItem>& l_vItems, const PackSettings& l_oS);

}  // namespace atlas
