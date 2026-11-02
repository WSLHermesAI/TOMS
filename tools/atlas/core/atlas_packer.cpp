// atlas_packer.cpp -- see atlas_packer.h.
#include "atlas_packer.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace atlas {

const char* f_HeuristicName(Heuristic l_eH) {
    switch (l_eH) {
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

bool f_ParseHeuristic(const std::string& l_strS, Heuristic& l_eOut) {
    for (Heuristic l_eH : {Heuristic::Auto, Heuristic::BestShortSideFit, Heuristic::BestLongSideFit,
                        Heuristic::BestAreaFit, Heuristic::BottomLeft, Heuristic::ContactPoint, Heuristic::Shelf})
        if (l_strS == f_HeuristicName(l_eH)) { l_eOut = l_eH; return true; }
    return false;
}

// ---- MaxRectsBin -------------------------------------------------------------------------------

MaxRectsBin::MaxRectsBin(int l_iW, int l_iH) : m_iW(l_iW), m_iH(l_iH) { m_vFree.push_back({0, 0, l_iW, l_iH}); }

static int f_CommonInterval(int l_iA0, int l_iA1, int l_iB0, int l_iB1) {
    return (l_iA1 < l_iB0 || l_iB1 < l_iA0) ? 0 : std::min(l_iA1, l_iB1) - std::max(l_iA0, l_iB0);
}

int MaxRectsBin::f_ContactScore(const IRect& l_oR) const {
    int l_iS = 0;
    if (l_oR.m_iX == 0 || l_oR.f_Right() == m_iW) l_iS += l_oR.m_iH;
    if (l_oR.m_iY == 0 || l_oR.f_Bottom() == m_iH) l_iS += l_oR.m_iW;
    for (const IRect& l_oU : m_vUsed) {
        if (l_oU.m_iX == l_oR.f_Right() || l_oU.f_Right() == l_oR.m_iX) l_iS += f_CommonInterval(l_oU.m_iY, l_oU.f_Bottom(), l_oR.m_iY, l_oR.f_Bottom());
        if (l_oU.m_iY == l_oR.f_Bottom() || l_oU.f_Bottom() == l_oR.m_iY) l_iS += f_CommonInterval(l_oU.m_iX, l_oU.f_Right(), l_oR.m_iX, l_oR.f_Right());
    }
    return l_iS;
}

bool MaxRectsBin::f_Score(int l_iW, int l_iH, Heuristic l_eRule, IRect& l_oPlace, int& l_iBest1, int& l_iBest2) const {
    l_iBest1 = INT_MAX; l_iBest2 = INT_MAX;
    bool l_bFound = false;
    for (const IRect& l_oF : m_vFree) {
        if (l_oF.m_iW < l_iW || l_oF.m_iH < l_iH) continue;
        const IRect l_oR{l_oF.m_iX, l_oF.m_iY, l_iW, l_iH};
        int l_iS1 = 0, l_iS2 = 0;
        const int l_iLeftW = l_oF.m_iW - l_iW, l_iLeftH = l_oF.m_iH - l_iH;
        switch (l_eRule) {
            case Heuristic::BestShortSideFit: l_iS1 = std::min(l_iLeftW, l_iLeftH); l_iS2 = std::max(l_iLeftW, l_iLeftH); break;
            case Heuristic::BestLongSideFit:  l_iS1 = std::max(l_iLeftW, l_iLeftH); l_iS2 = std::min(l_iLeftW, l_iLeftH); break;
            case Heuristic::BestAreaFit:      l_iS1 = l_oF.m_iW * l_oF.m_iH - l_iW * l_iH;      l_iS2 = std::min(l_iLeftW, l_iLeftH); break;
            case Heuristic::BottomLeft:       l_iS1 = l_oF.m_iY + l_iH;                l_iS2 = l_oF.m_iX; break;
            case Heuristic::ContactPoint:     l_iS1 = -f_ContactScore(l_oR);       l_iS2 = 0; break;
            default:                          l_iS1 = std::min(l_iLeftW, l_iLeftH); l_iS2 = std::max(l_iLeftW, l_iLeftH); break;
        }
        if (l_iS1 < l_iBest1 || (l_iS1 == l_iBest1 && l_iS2 < l_iBest2)) {
            l_iBest1 = l_iS1; l_iBest2 = l_iS2; l_oPlace = l_oR; l_bFound = true;
        }
    }
    return l_bFound;
}

void MaxRectsBin::f_Commit(const IRect& l_oR) {
    f_Split(l_oR);
    f_Prune();
    m_vUsed.push_back(l_oR);
}

IRect MaxRectsBin::f_Insert(int l_iW, int l_iH, Heuristic l_eRule) {
    IRect l_oPlace; int l_iS1, l_iS2;
    if (!f_Score(l_iW, l_iH, l_eRule, l_oPlace, l_iS1, l_iS2)) return {};
    f_Commit(l_oPlace);
    return l_oPlace;
}

bool MaxRectsBin::f_Reserve(const IRect& l_oR) {
    if (l_oR.m_iX < 0 || l_oR.m_iY < 0 || l_oR.f_Right() > m_iW || l_oR.f_Bottom() > m_iH) return false;
    for (const IRect& l_oU : m_vUsed)
        if (l_oU.f_Intersects(l_oR)) return false;
    f_Commit(l_oR);
    return true;
}

// The free rects untouched by a placement never contain each other (prune keeps it so), so only
// the pieces cut by this placement (new_) need checking -- O(free x new) instead of O(free^2).
void MaxRectsBin::f_Split(const IRect& l_oU) {
    std::vector<IRect> l_vKept;
    l_vKept.reserve(m_vFree.size());
    m_vNew.clear();
    for (const IRect& l_oF : m_vFree) {
        if (!l_oF.f_Intersects(l_oU)) { l_vKept.push_back(l_oF); continue; }
        if (l_oU.m_iX > l_oF.m_iX) m_vNew.push_back({l_oF.m_iX, l_oF.m_iY, l_oU.m_iX - l_oF.m_iX, l_oF.m_iH});
        if (l_oU.f_Right() < l_oF.f_Right()) m_vNew.push_back({l_oU.f_Right(), l_oF.m_iY, l_oF.f_Right() - l_oU.f_Right(), l_oF.m_iH});
        if (l_oU.m_iY > l_oF.m_iY) m_vNew.push_back({l_oF.m_iX, l_oF.m_iY, l_oF.m_iW, l_oU.m_iY - l_oF.m_iY});
        if (l_oU.f_Bottom() < l_oF.f_Bottom()) m_vNew.push_back({l_oF.m_iX, l_oU.f_Bottom(), l_oF.m_iW, l_oF.f_Bottom() - l_oU.f_Bottom()});
    }
    m_vFree.swap(l_vKept);
}

void MaxRectsBin::f_Prune() {
    // A new piece inside another new piece (or an identical earlier one), or inside an old rect, goes.
    std::vector<char> l_vDeadNew(m_vNew.size(), 0);
    for (size_t l_nI = 0; l_nI < m_vNew.size(); l_nI++) {
        for (size_t l_nJ = 0; l_nJ < m_vNew.size() && !l_vDeadNew[l_nI]; l_nJ++)
            if (l_nI != l_nJ && !l_vDeadNew[l_nJ] && m_vNew[l_nJ].f_Contains(m_vNew[l_nI]) && (m_vNew[l_nJ] != m_vNew[l_nI] || l_nJ < l_nI)) l_vDeadNew[l_nI] = 1;
        for (size_t l_nJ = 0; l_nJ < m_vFree.size() && !l_vDeadNew[l_nI]; l_nJ++)
            if (m_vFree[l_nJ].f_Contains(m_vNew[l_nI])) l_vDeadNew[l_nI] = 1;
    }
    // An old rect inside a surviving new piece goes.
    size_t l_nK = 0;
    for (size_t l_nI = 0; l_nI < m_vFree.size(); l_nI++) {
        bool l_bDead = false;
        for (size_t l_nJ = 0; l_nJ < m_vNew.size() && !l_bDead; l_nJ++)
            if (!l_vDeadNew[l_nJ] && m_vNew[l_nJ].f_Contains(m_vFree[l_nI])) l_bDead = true;
        if (!l_bDead) m_vFree[l_nK++] = m_vFree[l_nI];
    }
    m_vFree.resize(l_nK);
    for (size_t l_nJ = 0; l_nJ < m_vNew.size(); l_nJ++)
        if (!l_vDeadNew[l_nJ]) m_vFree.push_back(m_vNew[l_nJ]);
    m_vNew.clear();
}

double MaxRectsBin::f_Occupancy() const {
    double l_dA = 0;
    for (const IRect& l_oU : m_vUsed) l_dA += (double)l_oU.m_iW * l_oU.m_iH;
    return l_dA / ((double)m_iW * m_iH);
}

// ---- one page ----------------------------------------------------------------------------------

namespace {

// Up to this many items per page the packer searches all of them for the best next placement
// (tightest, O(n^2)); above it, it places them biggest first (fast enough for live rebuilds).
constexpr size_t g_nGlobalBestFitMax = 128;

struct PageAttempt {
    std::vector<int> m_vPlaced;              // item indices placed on this page
    std::vector<IRect> m_vRects;             // their rects in bin space (padding included)
    long long m_i64Area = 0;
};

// Packs as many of `todo` as fit into a bin of (bw, bh) after the fixed rects. Items are
// already inflated by padding.
PageAttempt f_PackOne(const std::vector<PackItem>& l_vItems, const std::vector<int>& l_vFixed,
                    const std::vector<IRect>& l_vFixedRects, const std::vector<int>& l_vTodo,
                    int l_iBw, int l_iBh, Heuristic l_eRule, bool& l_bFixedOk) {
    PageAttempt l_oA;
    l_bFixedOk = true;
    if (l_eRule == Heuristic::Shelf) {
        // Rows from the top: tallest first, left to right, a new row when the current one is full.
        // Fixed rects are avoided by skipping past them.
        std::vector<int> l_vOrder = l_vTodo;
        std::stable_sort(l_vOrder.begin(), l_vOrder.end(), [&](int l_iL, int l_iR) { return l_vItems[l_iL].m_iH > l_vItems[l_iR].m_iH; });
        std::vector<IRect> l_vUsed = l_vFixedRects;
        for (size_t l_nI = 0; l_nI < l_vFixedRects.size(); l_nI++) { l_oA.m_vPlaced.push_back(l_vFixed[l_nI]); l_oA.m_vRects.push_back(l_vFixedRects[l_nI]); }
        int l_iX = 0, l_iY = 0, l_iRowH = 0;
        for (int l_iIdx : l_vOrder) {
            const int l_iW = l_vItems[l_iIdx].m_iW, l_iH = l_vItems[l_iIdx].m_iH;
            bool l_bDone = false;
            while (!l_bDone) {
                if (l_iX + l_iW > l_iBw) { l_iY += l_iRowH; l_iX = 0; l_iRowH = 0; }
                if (l_iY + l_iH > l_iBh || l_iW > l_iBw) break;
                IRect l_oR{l_iX, l_iY, l_iW, l_iH};
                const IRect* l_pHit = nullptr;
                for (const IRect& l_oU : l_vUsed) if (l_oU.f_Intersects(l_oR)) { l_pHit = &l_oU; break; }
                if (l_pHit) { l_iX = l_pHit->f_Right(); continue; }
                l_vUsed.push_back(l_oR); l_oA.m_vPlaced.push_back(l_iIdx); l_oA.m_vRects.push_back(l_oR);
                l_oA.m_i64Area += (long long)l_iW * l_iH;
                l_iX += l_iW; l_iRowH = std::max(l_iRowH, l_iH);
                l_bDone = true;
            }
        }
        return l_oA;
    }

    MaxRectsBin l_oBin(l_iBw, l_iBh);
    for (size_t l_nI = 0; l_nI < l_vFixed.size(); l_nI++) {
        if (!l_oBin.f_Reserve(l_vFixedRects[l_nI])) { l_bFixedOk = false; continue; }
        l_oA.m_vPlaced.push_back(l_vFixed[l_nI]); l_oA.m_vRects.push_back(l_vFixedRects[l_nI]);
    }
    std::vector<int> l_vLeft = l_vTodo;
    if (l_vLeft.size() > g_nGlobalBestFitMax) {
        // Many items: biggest first, each at its best place -- one pass instead of the n^2 search
        // below, a few percent looser.
        std::stable_sort(l_vLeft.begin(), l_vLeft.end(), [&](int l_iL, int l_iR) {
            const int l_iMl = std::max(l_vItems[l_iL].m_iW, l_vItems[l_iL].m_iH), l_iMr = std::max(l_vItems[l_iR].m_iW, l_vItems[l_iR].m_iH);
            if (l_iMl != l_iMr) return l_iMl > l_iMr;
            return l_vItems[l_iL].m_iW * l_vItems[l_iL].m_iH > l_vItems[l_iR].m_iW * l_vItems[l_iR].m_iH;
        });
        for (int l_iIdx : l_vLeft) {
            IRect l_oR; int l_iS1, l_iS2;
            if (!l_oBin.f_Score(l_vItems[l_iIdx].m_iW, l_vItems[l_iIdx].m_iH, l_eRule, l_oR, l_iS1, l_iS2)) continue;
            l_oBin.f_Commit(l_oR);
            l_oA.m_vPlaced.push_back(l_iIdx); l_oA.m_vRects.push_back(l_oR);
            l_oA.m_i64Area += (long long)l_vItems[l_iIdx].m_iW * l_vItems[l_iIdx].m_iH;
        }
        return l_oA;
    }
    // Global best fit: each step places the item whose best placement scores best.
    while (!l_vLeft.empty()) {
        int l_iBestK = -1, l_iB1 = INT_MAX, l_iB2 = INT_MAX;
        IRect l_oBestR;
        for (size_t l_nK = 0; l_nK < l_vLeft.size(); l_nK++) {
            IRect l_oR; int l_iS1, l_iS2;
            if (!l_oBin.f_Score(l_vItems[l_vLeft[l_nK]].m_iW, l_vItems[l_vLeft[l_nK]].m_iH, l_eRule, l_oR, l_iS1, l_iS2)) continue;
            if (l_iS1 < l_iB1 || (l_iS1 == l_iB1 && l_iS2 < l_iB2)) { l_iB1 = l_iS1; l_iB2 = l_iS2; l_iBestK = (int)l_nK; l_oBestR = l_oR; }
        }
        if (l_iBestK < 0) break;
        l_oBin.f_Commit(l_oBestR);
        const int l_iIdx = l_vLeft[l_iBestK];
        l_oA.m_vPlaced.push_back(l_iIdx); l_oA.m_vRects.push_back(l_oBestR);
        l_oA.m_i64Area += (long long)l_vItems[l_iIdx].m_iW * l_vItems[l_iIdx].m_iH;
        l_vLeft.erase(l_vLeft.begin() + l_iBestK);
    }
    return l_oA;
}

int f_NextPow2(int l_iV) { int l_iP = 1; while (l_iP < l_iV) l_iP <<= 1; return l_iP; }

}  // namespace

PackResult f_PackPages(const std::vector<PackItem>& l_vItems, const PackSettings& l_oS) {
    PackResult l_oRes;
    l_oRes.m_vPlacements.assign(l_vItems.size(), {});
    const int l_iPad = std::max(0, l_oS.m_iPadding), l_iBorder = std::max(0, l_oS.m_iBorder);
    // Bin space: the page minus its border, plus one padding on the right/bottom so the last item
    // in a row needs no trailing gap.
    const int l_iMaxBW = l_oS.m_iMaxWidth - 2 * l_iBorder + l_iPad, l_iMaxBH = l_oS.m_iMaxHeight - 2 * l_iBorder + l_iPad;

    std::vector<PackItem> l_vInflated = l_vItems;
    for (PackItem& l_oIt : l_vInflated) { l_oIt.m_iW += l_iPad; l_oIt.m_iH += l_iPad; }

    std::vector<int> l_vTodo;
    int l_iLastFixedPage = -1;
    for (int l_iI = 0; l_iI < (int)l_vItems.size(); l_iI++) {
        if (l_vItems[l_iI].m_bFixed) { l_iLastFixedPage = std::max(l_iLastFixedPage, l_vItems[l_iI].m_iFixedPage); continue; }
        if (l_vInflated[l_iI].m_iW > l_iMaxBW || l_vInflated[l_iI].m_iH > l_iMaxBH) {
            l_oRes.m_vErrors.push_back("item " + std::to_string(l_iI) + " (" + std::to_string(l_vItems[l_iI].m_iW) + "x" +
                                 std::to_string(l_vItems[l_iI].m_iH) + ") is larger than the maximum page size");
            continue;
        }
        l_vTodo.push_back(l_iI);
    }

    const std::vector<Heuristic> l_vRules = l_oS.m_eHeuristic == Heuristic::Auto
        ? std::vector<Heuristic>{Heuristic::BestShortSideFit, Heuristic::BestLongSideFit, Heuristic::BestAreaFit,
                                 Heuristic::BottomLeft, Heuristic::ContactPoint}
        : std::vector<Heuristic>{l_oS.m_eHeuristic};

    for (int l_iPage = 0; !l_vTodo.empty() || l_iPage <= l_iLastFixedPage; l_iPage++) {
        std::vector<int> l_vFixed;
        std::vector<IRect> l_vFixedRects;
        int l_iNeedW = 0, l_iNeedH = 0;
        for (int l_iI = 0; l_iI < (int)l_vItems.size(); l_iI++) {
            if (!l_vItems[l_iI].m_bFixed || l_vItems[l_iI].m_iFixedPage != l_iPage) continue;
            l_vFixed.push_back(l_iI);
            IRect l_oR{l_vItems[l_iI].m_iFixedX - l_iBorder, l_vItems[l_iI].m_iFixedY - l_iBorder, l_vInflated[l_iI].m_iW, l_vInflated[l_iI].m_iH};
            l_vFixedRects.push_back(l_oR);
            l_iNeedW = std::max(l_iNeedW, l_oR.f_Right()); l_iNeedH = std::max(l_iNeedH, l_oR.f_Bottom());
        }

        // Candidate bin sizes, smallest area first. Powers of two are always tried; without the
        // power-of-two rule the final page is shrunk to what was used afterwards.
        std::vector<IRect> l_vCands;
        if (l_oS.m_bFixedSize) {
            l_vCands.push_back({0, 0, l_iMaxBW, l_iMaxBH});
        } else {
            long long l_i64Area = 0;
            for (int l_iI : l_vTodo) l_i64Area += (long long)l_vInflated[l_iI].m_iW * l_vInflated[l_iI].m_iH;
            for (size_t l_nI = 0; l_nI < l_vFixed.size(); l_nI++) l_i64Area += (long long)l_vFixedRects[l_nI].m_iW * l_vFixedRects[l_nI].m_iH;
            std::vector<int> l_vWs, l_vHs;
            for (int l_iV = f_NextPow2(std::max(1, l_oS.m_iMinWidth)); l_iV < l_oS.m_iMaxWidth; l_iV <<= 1) l_vWs.push_back(l_iV);
            l_vWs.push_back(l_oS.m_iMaxWidth);
            for (int l_iV = f_NextPow2(std::max(1, l_oS.m_iMinHeight)); l_iV < l_oS.m_iMaxHeight; l_iV <<= 1) l_vHs.push_back(l_iV);
            l_vHs.push_back(l_oS.m_iMaxHeight);
            for (int l_iW : l_vWs)
                for (int l_iH : l_vHs) {
                    if (l_oS.m_bSquare && l_iW != l_iH) continue;
                    const int l_iBw = l_iW - 2 * l_iBorder + l_iPad, l_iBh = l_iH - 2 * l_iBorder + l_iPad;
                    if (l_iBw <= 0 || l_iBh <= 0 || l_iBw < l_iNeedW || l_iBh < l_iNeedH) continue;
                    if ((long long)l_iBw * l_iBh < l_i64Area) continue;
                    l_vCands.push_back({0, 0, l_iBw, l_iBh});
                }
            std::stable_sort(l_vCands.begin(), l_vCands.end(), [](const IRect& l_oA, const IRect& l_oB) {
                const long long l_i64Aa = (long long)l_oA.m_iW * l_oA.m_iH, l_i64Bb = (long long)l_oB.m_iW * l_oB.m_iH;
                if (l_i64Aa != l_i64Bb) return l_i64Aa < l_i64Bb;
                return std::abs(l_oA.m_iW - l_oA.m_iH) < std::abs(l_oB.m_iW - l_oB.m_iH);
            });
            if (l_vCands.empty() || l_vCands.back().m_iW != l_iMaxBW || l_vCands.back().m_iH != l_iMaxBH)
                if (!l_oS.m_bSquare || l_iMaxBW == l_iMaxBH) l_vCands.push_back({0, 0, l_iMaxBW, l_iMaxBH});
        }

        PageAttempt l_oChosen;
        IRect l_oChosenBin;
        bool l_bAllFit = false;
        for (const IRect& l_oC : l_vCands) {
            for (Heuristic l_eRule : l_vRules) {
                bool l_bFixedOk = true;
                PageAttempt l_oA = f_PackOne(l_vInflated, l_vFixed, l_vFixedRects, l_vTodo, l_oC.m_iW, l_oC.m_iH, l_eRule, l_bFixedOk);
                if (!l_bFixedOk) continue;
                if (l_oA.m_vPlaced.size() == l_vTodo.size() + l_vFixed.size()) { l_oChosen = l_oA; l_oChosenBin = l_oC; l_bAllFit = true; break; }
                if (l_oC.m_iW == l_iMaxBW && l_oC.m_iH == l_iMaxBH && l_oA.m_i64Area > l_oChosen.m_i64Area) { l_oChosen = l_oA; l_oChosenBin = l_oC; }
            }
            if (l_bAllFit) break;
        }
        // Without the power-of-two rule the page may be any size: squeeze the bin that worked,
        // height first then width, by binary search (any rule that still fits everything wins).
        if (l_bAllFit && !l_oS.m_bPowerOfTwo && !l_oS.m_bFixedSize) {
            auto l_fnFitsAll = [&](int l_iBw, int l_iBh, PageAttempt& l_oOut) {
                for (Heuristic l_eRule : l_vRules) {
                    bool l_bFixedOk = true;
                    PageAttempt l_oA = f_PackOne(l_vInflated, l_vFixed, l_vFixedRects, l_vTodo, l_iBw, l_iBh, l_eRule, l_bFixedOk);
                    if (l_bFixedOk && l_oA.m_vPlaced.size() == l_vTodo.size() + l_vFixed.size()) { l_oOut = std::move(l_oA); return true; }
                }
                return false;
            };
            long long l_i64Area = 0;
            int l_iMinW = l_iNeedW, l_iMinH = l_iNeedH;
            for (int l_iI : l_vTodo) {
                l_i64Area += (long long)l_vInflated[l_iI].m_iW * l_vInflated[l_iI].m_iH;
                l_iMinW = std::max(l_iMinW, l_vInflated[l_iI].m_iW);
                l_iMinH = std::max(l_iMinH, l_vInflated[l_iI].m_iH);
            }
            for (size_t l_nI = 0; l_nI < l_vFixed.size(); l_nI++) l_i64Area += (long long)l_vFixedRects[l_nI].m_iW * l_vFixedRects[l_nI].m_iH;
            auto l_fnSqueeze = [&](bool l_bHeight) {
                int l_iLo = l_bHeight ? std::max(l_iMinH, (int)((l_i64Area + l_oChosenBin.m_iW - 1) / l_oChosenBin.m_iW)) : std::max(l_iMinW, (int)((l_i64Area + l_oChosenBin.m_iH - 1) / l_oChosenBin.m_iH));
                int l_iHi = l_bHeight ? l_oChosenBin.m_iH : l_oChosenBin.m_iW;
                if (l_oS.m_bSquare) l_iLo = std::max(l_iLo, std::max(l_iMinW, l_iMinH));
                while (l_iLo < l_iHi) {
                    const int l_iMid = l_iLo + (l_iHi - l_iLo) / 2;
                    PageAttempt l_oA;
                    const int l_iBw = l_oS.m_bSquare ? l_iMid : (l_bHeight ? l_oChosenBin.m_iW : l_iMid), l_iBh = l_oS.m_bSquare ? l_iMid : (l_bHeight ? l_iMid : l_oChosenBin.m_iH);
                    if (l_fnFitsAll(l_iBw, l_iBh, l_oA)) { l_iHi = l_iMid; l_oChosen = std::move(l_oA); l_oChosenBin = {0, 0, l_iBw, l_iBh}; }
                    else l_iLo = l_iMid + 1;
                }
            };
            l_fnSqueeze(true);
            if (!l_oS.m_bSquare) l_fnSqueeze(false);
        }
        if (l_oChosenBin.m_iW == 0) {   // nothing usable (e.g. pinned rects overlap)
            bool l_bFixedOk = true;
            l_oChosen = f_PackOne(l_vInflated, l_vFixed, l_vFixedRects, l_vTodo, l_iMaxBW, l_iMaxBH, l_vRules[0], l_bFixedOk);
            l_oChosenBin = {0, 0, l_iMaxBW, l_iMaxBH};
            if (!l_bFixedOk) l_oRes.m_vErrors.push_back("pinned sprites overlap or lie outside page " + std::to_string(l_iPage));
        }
        if (l_oChosen.m_vPlaced.empty() && l_vFixed.empty()) {
            l_oRes.m_vErrors.push_back("could not place the remaining sprites");
            break;
        }

        // Page size: the bin back to page space; without the power-of-two rule, shrink to the used area.
        int l_iUsedW = 0, l_iUsedH = 0;
        for (const IRect& l_oR : l_oChosen.m_vRects) { l_iUsedW = std::max(l_iUsedW, l_oR.f_Right() - l_iPad); l_iUsedH = std::max(l_iUsedH, l_oR.f_Bottom() - l_iPad); }
        int l_iPw = l_oChosenBin.m_iW - l_iPad + 2 * l_iBorder, l_iPh = l_oChosenBin.m_iH - l_iPad + 2 * l_iBorder;
        if (!l_oS.m_bFixedSize) {
            const int l_iTw = l_iUsedW + 2 * l_iBorder, l_iTh = l_iUsedH + 2 * l_iBorder;
            if (l_oS.m_bPowerOfTwo) { l_iPw = std::min(l_iPw, f_NextPow2(l_iTw)); l_iPh = std::min(l_iPh, f_NextPow2(l_iTh)); }
            else { l_iPw = std::min(l_iPw, l_iTw); l_iPh = std::min(l_iPh, l_iTh); }
            l_iPw = std::max(l_iPw, std::min(l_oS.m_iMinWidth, l_oS.m_iMaxWidth)); l_iPh = std::max(l_iPh, std::min(l_oS.m_iMinHeight, l_oS.m_iMaxHeight));
            if (l_oS.m_bSquare) l_iPw = l_iPh = std::max(l_iPw, l_iPh);
        }
        l_oRes.m_vPageSizes.push_back({0, 0, l_iPw, l_iPh});

        for (size_t l_nK = 0; l_nK < l_oChosen.m_vPlaced.size(); l_nK++) {
            const int l_iIdx = l_oChosen.m_vPlaced[l_nK];
            l_oRes.m_vPlacements[l_iIdx] = {l_iPage, l_oChosen.m_vRects[l_nK].m_iX + l_iBorder, l_oChosen.m_vRects[l_nK].m_iY + l_iBorder};
        }
        std::vector<int> l_vRest;
        for (int l_iI : l_vTodo)
            if (l_oRes.m_vPlacements[l_iI].m_iPage < 0) l_vRest.push_back(l_iI);
        l_vTodo.swap(l_vRest);
    }
    return l_oRes;
}

}  // namespace atlas
