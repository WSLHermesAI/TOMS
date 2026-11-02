// gesture.cpp -- see gesture.h.
#include "gesture.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {
constexpr float g_fPi = 3.14159265f;

std::vector<glm::vec2> f_Ring(int l_iN, float l_fStartDeg, int l_iStep) {   // n points on a circle, every `step`th, closed
    std::vector<glm::vec2> l_vV;
    for (int l_iK = 0; l_iK <= l_iN; l_iK++) {
        const float l_fA = (l_fStartDeg + 360.0f * (float)((l_iK * l_iStep) % l_iN) / (float)l_iN) * g_fPi / 180.0f;
        l_vV.push_back({0.5f + 0.5f * std::cos(l_fA), 0.5f + 0.5f * std::sin(l_fA)});
    }
    return l_vV;
}
}  // namespace

std::vector<glm::vec2> f_ShapeCorners(const std::string& l_strName) {
    if (l_strName == "pentagram") return f_Ring(5, -90.0f, 2);   // the top, then every second point: a star in one stroke
    if (l_strName == "triangle") return {{0.5f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {0.5f, 0.0f}};
    if (l_strName == "square") return {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}};
    if (l_strName == "circle") return f_Ring(8, -90.0f, 1);
    if (l_strName == "line") return {{0.0f, 0.5f}, {1.0f, 0.5f}};
    return {};
}

bool f_ShapeClosed(const std::string& l_strName) { return l_strName == "pentagram" || l_strName == "triangle" || l_strName == "square" || l_strName == "circle"; }

glm::vec2 f_ShapePoint(const std::string& l_strName, glm::vec2 l_v2Centre, float l_fSize, float l_fT) {
    const std::vector<glm::vec2> l_vC = f_ShapeCorners(l_strName);
    if (l_vC.empty()) return l_v2Centre;
    auto l_fnAt = [&](glm::vec2 l_v2P) { return l_v2Centre + (l_v2P - glm::vec2(0.5f)) * l_fSize; };
    if (l_vC.size() == 1) return l_fnAt(l_vC[0]);
    float l_fTotal = 0.0f;
    for (size_t l_nK = 1; l_nK < l_vC.size(); l_nK++) l_fTotal += glm::length(l_vC[l_nK] - l_vC[l_nK - 1]);
    float l_fWant = std::clamp(l_fT, 0.0f, 1.0f) * l_fTotal;
    for (size_t l_nK = 1; l_nK < l_vC.size(); l_nK++) {
        const float l_fLen = glm::length(l_vC[l_nK] - l_vC[l_nK - 1]);
        if (l_fWant <= l_fLen || l_nK + 1 == l_vC.size()) return l_fnAt(l_vC[l_nK - 1] + (l_vC[l_nK] - l_vC[l_nK - 1]) * (l_fLen > 0.0f ? std::min(1.0f, l_fWant / l_fLen) : 1.0f));
        l_fWant -= l_fLen;
    }
    return l_fnAt(l_vC.back());
}

void ShapeTrace::f_Start(const std::string& l_strShape, glm::vec2 l_v2Centre, float l_fSize, float l_fCornerRadius) {
    *this = ShapeTrace{};
    for (glm::vec2 l_v2P : f_ShapeCorners(l_strShape)) m_vPts.push_back(l_v2Centre + (l_v2P - glm::vec2(0.5f)) * l_fSize);
    m_fRadius = l_fCornerRadius;
}

int ShapeTrace::f_Pointer(int l_iPhase, float l_fX, float l_fY) {
    if (l_iPhase == 0) m_bDown = true;
    if (!m_bDown) return 0;
    if (l_iPhase == 2) m_bDown = false;
    m_v2Last = {l_fX, l_fY};
    const int l_iBefore = f_Steps();
    while (!f_Done() && glm::length(m_v2Last - m_vPts[(size_t)m_iReached]) <= m_fRadius) m_iReached++;   // (a closed shape's end is its start again)
    return f_Steps() - l_iBefore;
}

bool f_MatchStroke(const std::vector<glm::vec2>& l_vStroke, const std::string& l_strShape, float l_fTolerance) {
    std::vector<glm::vec2> l_vC = f_ShapeCorners(l_strShape);
    if (l_vC.empty() || l_vStroke.size() < 2) return false;
    glm::vec2 l_v2Lo = l_vStroke[0], l_v2Hi = l_vStroke[0];
    for (glm::vec2 l_v2P : l_vStroke) { l_v2Lo = glm::min(l_v2Lo, l_v2P); l_v2Hi = glm::max(l_v2Hi, l_v2P); }
    const float l_fSide = std::max(l_v2Hi.x - l_v2Lo.x, l_v2Hi.y - l_v2Lo.y);
    if (l_fSide < 1.0f) return false;
    // the stroke in its own box: the longer side 1, the shorter kept in proportion and centred (a thin line stays thin)
    const glm::vec2 l_v2Off = (glm::vec2(l_fSide) - (l_v2Hi - l_v2Lo)) * 0.5f;
    std::vector<glm::vec2> l_vS;
    for (glm::vec2 l_v2P : l_vStroke) l_vS.push_back((l_v2P - l_v2Lo + l_v2Off) / l_fSide);
    auto l_fnPasses = [&](const std::vector<glm::vec2>& l_vCorners) {   // each corner, in order, near a later point
        size_t l_nFrom = 0;
        for (glm::vec2 l_v2K : l_vCorners) {
            size_t l_nHit = l_vS.size();
            for (size_t l_nI = l_nFrom; l_nI < l_vS.size(); l_nI++)
                if (glm::length(l_vS[l_nI] - l_v2K) <= l_fTolerance) { l_nHit = l_nI; break; }
            if (l_nHit == l_vS.size()) return false;
            l_nFrom = l_nHit;
        }
        return true;
    };
    if (l_strShape == "circle")   // round: every point about as far from the middle (a triangle's corners stick out)
        for (glm::vec2 l_v2P : l_vS)
            if (glm::length(l_v2P - glm::vec2(0.5f)) > 0.5f + l_fTolerance || glm::length(l_v2P - glm::vec2(0.5f)) < 0.5f - 1.5f * l_fTolerance) return false;
    if (!f_ShapeClosed(l_strShape)) {
        std::vector<glm::vec2> l_vBack(l_vC.rbegin(), l_vC.rend());
        return l_fnPasses(l_vC) || l_fnPasses(l_vBack);
    }
    l_vC.pop_back();   // a closed shape: try each corner as the start, both ways round
    const size_t l_nN = l_vC.size();
    for (size_t l_nFirst = 0; l_nFirst < l_nN; l_nFirst++)
        for (int l_iDir : {1, -1}) {
            std::vector<glm::vec2> l_vOrder;
            for (size_t l_nK = 0; l_nK <= l_nN; l_nK++) {
                const int l_iIdx = (((int)l_nFirst + l_iDir * (int)l_nK) % (int)l_nN + (int)l_nN) % (int)l_nN;
                l_vOrder.push_back(l_vC[(size_t)l_iIdx]);
            }
            if (l_fnPasses(l_vOrder)) return true;
        }
    return false;
}

} // namespace toms
