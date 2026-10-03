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
const char* heuristicName(Heuristic h);
bool parseHeuristic(const std::string& s, Heuristic& out);

class MaxRectsBin {
public:
    MaxRectsBin(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }
    // Places a w x h rect; returns an empty rect when it does not fit.
    IRect insert(int w, int h, Heuristic rule);
    // Reserves an exact rect (a pinned sprite). Returns false if it overlaps something already used
    // or lies outside the bin.
    bool reserve(const IRect& r);
    // Score of placing w x h under `rule` (lower is better); false if it does not fit.
    bool score(int w, int h, Heuristic rule, IRect& place, int& s1, int& s2) const;
    void commit(const IRect& r);
    double occupancy() const;

private:
    int w_, h_;
    std::vector<IRect> free_, used_;
    std::vector<IRect> new_;    // free pieces cut by the last placement, until prune() merges them
    void split(const IRect& used);
    void prune();
    int contactScore(const IRect& r) const;
};

struct PackItem {
    int w = 0, h = 0;        // size including padding/extrude
    bool fixed = false;      // pinned: must go at (fixedPage, fixedX, fixedY)
    int fixedPage = 0, fixedX = 0, fixedY = 0;
};

struct PackSettings {
    int maxWidth = 2048, maxHeight = 2048;
    int minWidth = 16, minHeight = 16;
    bool powerOfTwo = false;
    bool square = false;
    bool fixedSize = false;     // always use maxWidth x maxHeight (no shrinking)
    int border = 0;             // empty margin along the page edges
    int padding = 0;            // empty pixels between two items
    Heuristic heuristic = Heuristic::Auto;
};

struct PackPlacement { int page = -1; int x = 0, y = 0; };

struct PackResult {
    std::vector<PackPlacement> placements;   // one per item; page -1 = could not be placed
    std::vector<IRect> pageSizes;            // w,h per page (x,y unused)
    std::vector<std::string> errors;
};

PackResult packPages(const std::vector<PackItem>& items, const PackSettings& s);

}  // namespace atlas
