// atlas_packer.cpp -- see atlas_packer.h.
#include "atlas_packer.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace atlas {

const char* heuristicName(Heuristic h) {
    switch (h) {
        case Heuristic::Auto: return "auto";
        case Heuristic::BestShortSideFit: return "bssf";
        case Heuristic::BestLongSideFit: return "blsf";
        case Heuristic::BestAreaFit: return "baf";
        case Heuristic::BottomLeft: return "bl";
        case Heuristic::ContactPoint: return "cp";
        case Heuristic::Shelf: return "shelf";
    }
    return "auto";
}

bool parseHeuristic(const std::string& s, Heuristic& out) {
    for (Heuristic h : {Heuristic::Auto, Heuristic::BestShortSideFit, Heuristic::BestLongSideFit,
                        Heuristic::BestAreaFit, Heuristic::BottomLeft, Heuristic::ContactPoint, Heuristic::Shelf})
        if (s == heuristicName(h)) { out = h; return true; }
    return false;
}

// ---- MaxRectsBin -------------------------------------------------------------------------------

MaxRectsBin::MaxRectsBin(int w, int h) : w_(w), h_(h) { free_.push_back({0, 0, w, h}); }

static int commonInterval(int a0, int a1, int b0, int b1) {
    return (a1 < b0 || b1 < a0) ? 0 : std::min(a1, b1) - std::max(a0, b0);
}

int MaxRectsBin::contactScore(const IRect& r) const {
    int s = 0;
    if (r.x == 0 || r.right() == w_) s += r.h;
    if (r.y == 0 || r.bottom() == h_) s += r.w;
    for (const IRect& u : used_) {
        if (u.x == r.right() || u.right() == r.x) s += commonInterval(u.y, u.bottom(), r.y, r.bottom());
        if (u.y == r.bottom() || u.bottom() == r.y) s += commonInterval(u.x, u.right(), r.x, r.right());
    }
    return s;
}

bool MaxRectsBin::score(int w, int h, Heuristic rule, IRect& place, int& best1, int& best2) const {
    best1 = INT_MAX; best2 = INT_MAX;
    bool found = false;
    for (const IRect& f : free_) {
        if (f.w < w || f.h < h) continue;
        const IRect r{f.x, f.y, w, h};
        int s1 = 0, s2 = 0;
        const int leftW = f.w - w, leftH = f.h - h;
        switch (rule) {
            case Heuristic::BestShortSideFit: s1 = std::min(leftW, leftH); s2 = std::max(leftW, leftH); break;
            case Heuristic::BestLongSideFit:  s1 = std::max(leftW, leftH); s2 = std::min(leftW, leftH); break;
            case Heuristic::BestAreaFit:      s1 = f.w * f.h - w * h;      s2 = std::min(leftW, leftH); break;
            case Heuristic::BottomLeft:       s1 = f.y + h;                s2 = f.x; break;
            case Heuristic::ContactPoint:     s1 = -contactScore(r);       s2 = 0; break;
            default:                          s1 = std::min(leftW, leftH); s2 = std::max(leftW, leftH); break;
        }
        if (s1 < best1 || (s1 == best1 && s2 < best2)) {
            best1 = s1; best2 = s2; place = r; found = true;
        }
    }
    return found;
}

void MaxRectsBin::commit(const IRect& r) {
    split(r);
    prune();
    used_.push_back(r);
}

IRect MaxRectsBin::insert(int w, int h, Heuristic rule) {
    IRect place; int s1, s2;
    if (!score(w, h, rule, place, s1, s2)) return {};
    commit(place);
    return place;
}

bool MaxRectsBin::reserve(const IRect& r) {
    if (r.x < 0 || r.y < 0 || r.right() > w_ || r.bottom() > h_) return false;
    for (const IRect& u : used_)
        if (u.intersects(r)) return false;
    commit(r);
    return true;
}

// The free rects untouched by a placement never contain each other (prune keeps it so), so only
// the pieces cut by this placement (new_) need checking -- O(free x new) instead of O(free^2).
void MaxRectsBin::split(const IRect& u) {
    std::vector<IRect> kept;
    kept.reserve(free_.size());
    new_.clear();
    for (const IRect& f : free_) {
        if (!f.intersects(u)) { kept.push_back(f); continue; }
        if (u.x > f.x) new_.push_back({f.x, f.y, u.x - f.x, f.h});
        if (u.right() < f.right()) new_.push_back({u.right(), f.y, f.right() - u.right(), f.h});
        if (u.y > f.y) new_.push_back({f.x, f.y, f.w, u.y - f.y});
        if (u.bottom() < f.bottom()) new_.push_back({f.x, u.bottom(), f.w, f.bottom() - u.bottom()});
    }
    free_.swap(kept);
}

void MaxRectsBin::prune() {
    // A new piece inside another new piece (or an identical earlier one), or inside an old rect, goes.
    std::vector<char> deadNew(new_.size(), 0);
    for (size_t i = 0; i < new_.size(); i++) {
        for (size_t j = 0; j < new_.size() && !deadNew[i]; j++)
            if (i != j && !deadNew[j] && new_[j].contains(new_[i]) && (new_[j] != new_[i] || j < i)) deadNew[i] = 1;
        for (size_t j = 0; j < free_.size() && !deadNew[i]; j++)
            if (free_[j].contains(new_[i])) deadNew[i] = 1;
    }
    // An old rect inside a surviving new piece goes.
    size_t k = 0;
    for (size_t i = 0; i < free_.size(); i++) {
        bool dead = false;
        for (size_t j = 0; j < new_.size() && !dead; j++)
            if (!deadNew[j] && new_[j].contains(free_[i])) dead = true;
        if (!dead) free_[k++] = free_[i];
    }
    free_.resize(k);
    for (size_t j = 0; j < new_.size(); j++)
        if (!deadNew[j]) free_.push_back(new_[j]);
    new_.clear();
}

double MaxRectsBin::occupancy() const {
    double a = 0;
    for (const IRect& u : used_) a += (double)u.w * u.h;
    return a / ((double)w_ * h_);
}

// ---- one page ----------------------------------------------------------------------------------

namespace {

// Up to this many items per page the packer searches all of them for the best next placement
// (tightest, O(n^2)); above it, it places them biggest first (fast enough for live rebuilds).
constexpr size_t kGlobalBestFitMax = 128;

struct PageAttempt {
    std::vector<int> placed;              // item indices placed on this page
    std::vector<IRect> rects;             // their rects in bin space (padding included)
    long long area = 0;
};

// Packs as many of `todo` as fit into a bin of (bw, bh) after the fixed rects. Items are
// already inflated by padding.
PageAttempt packOne(const std::vector<PackItem>& items, const std::vector<int>& fixed,
                    const std::vector<IRect>& fixedRects, const std::vector<int>& todo,
                    int bw, int bh, Heuristic rule, bool& fixedOk) {
    PageAttempt a;
    fixedOk = true;
    if (rule == Heuristic::Shelf) {
        // Rows from the top: tallest first, left to right, a new row when the current one is full.
        // Fixed rects are avoided by skipping past them.
        std::vector<int> order = todo;
        std::stable_sort(order.begin(), order.end(), [&](int l, int r) { return items[l].h > items[r].h; });
        std::vector<IRect> used = fixedRects;
        for (size_t i = 0; i < fixedRects.size(); i++) { a.placed.push_back(fixed[i]); a.rects.push_back(fixedRects[i]); }
        int x = 0, y = 0, rowH = 0;
        for (int idx : order) {
            const int w = items[idx].w, h = items[idx].h;
            bool done = false;
            while (!done) {
                if (x + w > bw) { y += rowH; x = 0; rowH = 0; }
                if (y + h > bh || w > bw) break;
                IRect r{x, y, w, h};
                const IRect* hit = nullptr;
                for (const IRect& u : used) if (u.intersects(r)) { hit = &u; break; }
                if (hit) { x = hit->right(); continue; }
                used.push_back(r); a.placed.push_back(idx); a.rects.push_back(r);
                a.area += (long long)w * h;
                x += w; rowH = std::max(rowH, h);
                done = true;
            }
        }
        return a;
    }

    MaxRectsBin bin(bw, bh);
    for (size_t i = 0; i < fixed.size(); i++) {
        if (!bin.reserve(fixedRects[i])) { fixedOk = false; continue; }
        a.placed.push_back(fixed[i]); a.rects.push_back(fixedRects[i]);
    }
    std::vector<int> left = todo;
    if (left.size() > kGlobalBestFitMax) {
        // Many items: biggest first, each at its best place -- one pass instead of the n^2 search
        // below, a few percent looser.
        std::stable_sort(left.begin(), left.end(), [&](int l, int r) {
            const int ml = std::max(items[l].w, items[l].h), mr = std::max(items[r].w, items[r].h);
            if (ml != mr) return ml > mr;
            return items[l].w * items[l].h > items[r].w * items[r].h;
        });
        for (int idx : left) {
            IRect r; int s1, s2;
            if (!bin.score(items[idx].w, items[idx].h, rule, r, s1, s2)) continue;
            bin.commit(r);
            a.placed.push_back(idx); a.rects.push_back(r);
            a.area += (long long)items[idx].w * items[idx].h;
        }
        return a;
    }
    // Global best fit: each step places the item whose best placement scores best.
    while (!left.empty()) {
        int bestK = -1, b1 = INT_MAX, b2 = INT_MAX;
        IRect bestR;
        for (size_t k = 0; k < left.size(); k++) {
            IRect r; int s1, s2;
            if (!bin.score(items[left[k]].w, items[left[k]].h, rule, r, s1, s2)) continue;
            if (s1 < b1 || (s1 == b1 && s2 < b2)) { b1 = s1; b2 = s2; bestK = (int)k; bestR = r; }
        }
        if (bestK < 0) break;
        bin.commit(bestR);
        const int idx = left[bestK];
        a.placed.push_back(idx); a.rects.push_back(bestR);
        a.area += (long long)items[idx].w * items[idx].h;
        left.erase(left.begin() + bestK);
    }
    return a;
}

int nextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

}  // namespace

PackResult packPages(const std::vector<PackItem>& items, const PackSettings& s) {
    PackResult res;
    res.placements.assign(items.size(), {});
    const int pad = std::max(0, s.padding), border = std::max(0, s.border);
    // Bin space: the page minus its border, plus one padding on the right/bottom so the last item
    // in a row needs no trailing gap.
    const int maxBW = s.maxWidth - 2 * border + pad, maxBH = s.maxHeight - 2 * border + pad;

    std::vector<PackItem> inflated = items;
    for (PackItem& it : inflated) { it.w += pad; it.h += pad; }

    std::vector<int> todo;
    int lastFixedPage = -1;
    for (int i = 0; i < (int)items.size(); i++) {
        if (items[i].fixed) { lastFixedPage = std::max(lastFixedPage, items[i].fixedPage); continue; }
        if (inflated[i].w > maxBW || inflated[i].h > maxBH) {
            res.errors.push_back("item " + std::to_string(i) + " (" + std::to_string(items[i].w) + "x" +
                                 std::to_string(items[i].h) + ") is larger than the maximum page size");
            continue;
        }
        todo.push_back(i);
    }

    const std::vector<Heuristic> rules = s.heuristic == Heuristic::Auto
        ? std::vector<Heuristic>{Heuristic::BestShortSideFit, Heuristic::BestLongSideFit, Heuristic::BestAreaFit,
                                 Heuristic::BottomLeft, Heuristic::ContactPoint}
        : std::vector<Heuristic>{s.heuristic};

    for (int page = 0; !todo.empty() || page <= lastFixedPage; page++) {
        std::vector<int> fixed;
        std::vector<IRect> fixedRects;
        int needW = 0, needH = 0;
        for (int i = 0; i < (int)items.size(); i++) {
            if (!items[i].fixed || items[i].fixedPage != page) continue;
            fixed.push_back(i);
            IRect r{items[i].fixedX - border, items[i].fixedY - border, inflated[i].w, inflated[i].h};
            fixedRects.push_back(r);
            needW = std::max(needW, r.right()); needH = std::max(needH, r.bottom());
        }

        // Candidate bin sizes, smallest area first. Powers of two are always tried; without the
        // power-of-two rule the final page is shrunk to what was used afterwards.
        std::vector<IRect> cands;
        if (s.fixedSize) {
            cands.push_back({0, 0, maxBW, maxBH});
        } else {
            long long area = 0;
            for (int i : todo) area += (long long)inflated[i].w * inflated[i].h;
            for (size_t i = 0; i < fixed.size(); i++) area += (long long)fixedRects[i].w * fixedRects[i].h;
            std::vector<int> ws, hs;
            for (int v = nextPow2(std::max(1, s.minWidth)); v < s.maxWidth; v <<= 1) ws.push_back(v);
            ws.push_back(s.maxWidth);
            for (int v = nextPow2(std::max(1, s.minHeight)); v < s.maxHeight; v <<= 1) hs.push_back(v);
            hs.push_back(s.maxHeight);
            for (int w : ws)
                for (int h : hs) {
                    if (s.square && w != h) continue;
                    const int bw = w - 2 * border + pad, bh = h - 2 * border + pad;
                    if (bw <= 0 || bh <= 0 || bw < needW || bh < needH) continue;
                    if ((long long)bw * bh < area) continue;
                    cands.push_back({0, 0, bw, bh});
                }
            std::stable_sort(cands.begin(), cands.end(), [](const IRect& a, const IRect& b) {
                const long long aa = (long long)a.w * a.h, bb = (long long)b.w * b.h;
                if (aa != bb) return aa < bb;
                return std::abs(a.w - a.h) < std::abs(b.w - b.h);
            });
            if (cands.empty() || cands.back().w != maxBW || cands.back().h != maxBH)
                if (!s.square || maxBW == maxBH) cands.push_back({0, 0, maxBW, maxBH});
        }

        PageAttempt chosen;
        IRect chosenBin;
        bool allFit = false;
        for (const IRect& c : cands) {
            for (Heuristic rule : rules) {
                bool fixedOk = true;
                PageAttempt a = packOne(inflated, fixed, fixedRects, todo, c.w, c.h, rule, fixedOk);
                if (!fixedOk) continue;
                if (a.placed.size() == todo.size() + fixed.size()) { chosen = a; chosenBin = c; allFit = true; break; }
                if (c.w == maxBW && c.h == maxBH && a.area > chosen.area) { chosen = a; chosenBin = c; }
            }
            if (allFit) break;
        }
        // Without the power-of-two rule the page may be any size: squeeze the bin that worked,
        // height first then width, by binary search (any rule that still fits everything wins).
        if (allFit && !s.powerOfTwo && !s.fixedSize) {
            auto fitsAll = [&](int bw, int bh, PageAttempt& out) {
                for (Heuristic rule : rules) {
                    bool fixedOk = true;
                    PageAttempt a = packOne(inflated, fixed, fixedRects, todo, bw, bh, rule, fixedOk);
                    if (fixedOk && a.placed.size() == todo.size() + fixed.size()) { out = std::move(a); return true; }
                }
                return false;
            };
            long long area = 0;
            int minW = needW, minH = needH;
            for (int i : todo) {
                area += (long long)inflated[i].w * inflated[i].h;
                minW = std::max(minW, inflated[i].w);
                minH = std::max(minH, inflated[i].h);
            }
            for (size_t i = 0; i < fixed.size(); i++) area += (long long)fixedRects[i].w * fixedRects[i].h;
            auto squeeze = [&](bool height) {
                int lo = height ? std::max(minH, (int)((area + chosenBin.w - 1) / chosenBin.w)) : std::max(minW, (int)((area + chosenBin.h - 1) / chosenBin.h));
                int hi = height ? chosenBin.h : chosenBin.w;
                if (s.square) lo = std::max(lo, std::max(minW, minH));
                while (lo < hi) {
                    const int mid = lo + (hi - lo) / 2;
                    PageAttempt a;
                    const int bw = s.square ? mid : (height ? chosenBin.w : mid), bh = s.square ? mid : (height ? mid : chosenBin.h);
                    if (fitsAll(bw, bh, a)) { hi = mid; chosen = std::move(a); chosenBin = {0, 0, bw, bh}; }
                    else lo = mid + 1;
                }
            };
            squeeze(true);
            if (!s.square) squeeze(false);
        }
        if (chosenBin.w == 0) {   // nothing usable (e.g. pinned rects overlap)
            bool fixedOk = true;
            chosen = packOne(inflated, fixed, fixedRects, todo, maxBW, maxBH, rules[0], fixedOk);
            chosenBin = {0, 0, maxBW, maxBH};
            if (!fixedOk) res.errors.push_back("pinned sprites overlap or lie outside page " + std::to_string(page));
        }
        if (chosen.placed.empty() && fixed.empty()) {
            res.errors.push_back("could not place the remaining sprites");
            break;
        }

        // Page size: the bin back to page space; without the power-of-two rule, shrink to the used area.
        int usedW = 0, usedH = 0;
        for (const IRect& r : chosen.rects) { usedW = std::max(usedW, r.right() - pad); usedH = std::max(usedH, r.bottom() - pad); }
        int pw = chosenBin.w - pad + 2 * border, ph = chosenBin.h - pad + 2 * border;
        if (!s.fixedSize) {
            const int tw = usedW + 2 * border, th = usedH + 2 * border;
            if (s.powerOfTwo) { pw = std::min(pw, nextPow2(tw)); ph = std::min(ph, nextPow2(th)); }
            else { pw = std::min(pw, tw); ph = std::min(ph, th); }
            pw = std::max(pw, std::min(s.minWidth, s.maxWidth)); ph = std::max(ph, std::min(s.minHeight, s.maxHeight));
            if (s.square) pw = ph = std::max(pw, ph);
        }
        res.pageSizes.push_back({0, 0, pw, ph});

        for (size_t k = 0; k < chosen.placed.size(); k++) {
            const int idx = chosen.placed[k];
            res.placements[idx] = {page, chosen.rects[k].x + border, chosen.rects[k].y + border};
        }
        std::vector<int> rest;
        for (int i : todo)
            if (res.placements[i].page < 0) rest.push_back(i);
        todo.swap(rest);
    }
    return res;
}

}  // namespace atlas
