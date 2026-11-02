// timed_hits.cpp -- see timed_hits.h.
#include "timed_hits.h"

#include <algorithm>
#include <cstdlib>

namespace toms {

int TimedTrack::f_Add(int l_iAppearMs, int l_iDueMs) {
    Point l_oP;
    l_oP.m_iAppearMs = std::min(l_iAppearMs, l_iDueMs);
    l_oP.m_iDueMs = l_iDueMs;
    m_vPoints.push_back(l_oP);
    return (int)m_vPoints.size() - 1;
}

int TimedTrack::f_NextOpen() const {
    for (size_t l_nI = 0; l_nI < m_vPoints.size(); ++l_nI) if (f_Open(m_vPoints[l_nI])) return (int)l_nI;
    return -1;
}

void TimedTrack::f_Update(int l_iDtMs) {
    m_iNowMs += std::max(0, l_iDtMs);
    for (size_t l_nI = 0; l_nI < m_vPoints.size(); ++l_nI) {
        Point& l_oP = m_vPoints[l_nI];
        if (f_Open(l_oP) && m_iNowMs > l_oP.m_iDueMs + m_iGoodMs) {
            l_oP.m_eResult = Grade::Miss;
            l_oP.m_iOffsetMs = m_iNowMs - l_oP.m_iDueMs;
            m_vMissedQueue.push_back((int)l_nI);
        }
    }
}

TimedTrack::Tap TimedTrack::f_Tap() {
    Tap l_oT;
    int l_iBest = -1, l_iBestDist = 0;
    if (m_bOrdered) {
        l_iBest = f_NextOpen();
        if (l_iBest >= 0) l_iBestDist = std::abs(m_iNowMs - m_vPoints[l_iBest].m_iDueMs);
    } else {
        for (size_t l_nI = 0; l_nI < m_vPoints.size(); ++l_nI) {
            if (!f_Open(m_vPoints[l_nI]) || m_iNowMs < m_vPoints[l_nI].m_iAppearMs) continue;
            const int l_iD = std::abs(m_iNowMs - m_vPoints[l_nI].m_iDueMs);
            if (l_iBest < 0 || l_iD < l_iBestDist) { l_iBest = (int)l_nI; l_iBestDist = l_iD; }
        }
    }
    if (l_iBest < 0 || l_iBestDist > m_iGoodMs) {
        // nothing in reach: a wasted tap. An ordered track counts it against its next point (too early / mashing).
        if (m_bOrdered && l_iBest >= 0 && m_iNowMs >= m_vPoints[l_iBest].m_iAppearMs) {
            m_vPoints[l_iBest].m_eResult = Grade::Miss;
            m_vPoints[l_iBest].m_iOffsetMs = m_iNowMs - m_vPoints[l_iBest].m_iDueMs;
            l_oT.m_iIndex = l_iBest;
            l_oT.m_iOffsetMs = m_vPoints[l_iBest].m_iOffsetMs;
        }
        l_oT.m_eGrade = Grade::Miss;
        return l_oT;
    }
    Point& l_oP = m_vPoints[l_iBest];
    l_oP.m_iOffsetMs = m_iNowMs - l_oP.m_iDueMs;
    l_oP.m_eResult = l_iBestDist <= m_iPerfectMs ? Grade::Perfect : Grade::Good;
    l_oT.m_eGrade = l_oP.m_eResult;
    l_oT.m_iIndex = l_iBest;
    l_oT.m_iOffsetMs = l_oP.m_iOffsetMs;
    return l_oT;
}

std::vector<int> TimedTrack::f_TakeMissed() {
    std::vector<int> l_vOut;
    l_vOut.swap(m_vMissedQueue);
    return l_vOut;
}

float TimedTrack::f_Growth(int l_iI) const {
    if (l_iI < 0 || l_iI >= (int)m_vPoints.size()) return 0.0f;
    const Point& l_oP = m_vPoints[l_iI];
    if (m_iNowMs <= l_oP.m_iAppearMs) return 0.0f;
    const int l_iSpan = std::max(1, l_oP.m_iDueMs - l_oP.m_iAppearMs);
    return (float)(m_iNowMs - l_oP.m_iAppearMs) / (float)l_iSpan;
}

bool TimedTrack::f_Finished() const { return f_NextOpen() < 0; }

int TimedTrack::f_Hits() const {
    return (int)std::count_if(m_vPoints.begin(), m_vPoints.end(),
                              [](const Point& l_oP) { return l_oP.m_eResult == Grade::Perfect || l_oP.m_eResult == Grade::Good; });
}

int TimedTrack::f_Perfects() const {
    return (int)std::count_if(m_vPoints.begin(), m_vPoints.end(), [](const Point& l_oP) { return l_oP.m_eResult == Grade::Perfect; });
}

} // namespace toms
