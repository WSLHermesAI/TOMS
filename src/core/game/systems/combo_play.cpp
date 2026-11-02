// combo_play.cpp -- see combo_play.h.
#include "combo_play.h"

#include <algorithm>
#include <cmath>

namespace toms {

ComboSettings f_ComboSettings(const WeaponPlay& l_oW, int l_iWeaponLevel) {
    ComboSettings l_oS;
    l_oS.m_iPoints = std::clamp(f_WeaponLevelEntry(l_oW, l_iWeaponLevel).m_iPoints, 1, g_iMaxComboPoints);
    l_oS.m_bTurnBack = l_oW.m_bTurnBack;
    l_oS.m_iSweepMs = l_oW.m_iSweepMs;
    l_oS.m_iMaxSweeps = l_oW.m_iMaxSweeps;
    l_oS.m_fSpeedUp = l_oW.m_fChargeSpeedUp;
    l_oS.m_fHitWindow = l_oW.m_fHitWindow;
    l_oS.m_iHitWindowMs = l_oW.m_iHitWindowMs;
    l_oS.m_iLateMs = l_oW.m_iLateMs;
    l_oS.m_iMissBack = f_MissBackPoints(l_oW, l_iWeaponLevel);
    l_oS.m_iMissWaitMs = l_oW.m_iMissWaitMs;
    return l_oS;
}

void ComboPlay::f_Start(const ComboSettings& l_oS, float l_fCharge) {
    m_oS = l_oS;
    m_oS.m_iPoints = std::clamp(m_oS.m_iPoints, 1, g_iMaxComboPoints);
    m_fCharge = std::clamp(l_fCharge, 0.0f, 1.0f);
    m_fPos = 0;
    m_iDir = 1;
    m_iSweeps = 0;
    m_iWaitMs = 0;
    m_iMissesInRow = 0;
    m_fSpeed = (1.0f + m_oS.m_fSpeedUp * m_fCharge) / float(std::max(1, m_oS.m_iSweepMs));
    m_vAt.clear();
    m_vHit.assign((size_t)m_oS.m_iPoints, false);
    for (int l_iI = 0; l_iI < m_oS.m_iPoints; l_iI++) m_vAt.push_back(float(l_iI + 1) / float(m_oS.m_iPoints + 1));   // evenly, off the ends
    m_vOrder.clear();
    m_iLastNear = -1;
    m_vEntered.clear();
    m_oResult = ComboResult();
    m_bRunning = true;
    m_bDone = false;
}

int ComboPlay::f_Hits() const { return (int)std::count(m_vHit.begin(), m_vHit.end(), true); }

void ComboPlay::f_Finish(bool l_bFailed) {
    m_bRunning = false;
    m_bDone = true;
    m_oResult.m_iHits = l_bFailed ? 0 : f_Hits();
    m_oResult.m_iPoints = m_oS.m_iPoints;
    m_oResult.m_fCharge = m_fCharge;
    m_oResult.m_bFailed = l_bFailed;
}

void ComboPlay::f_Update(int l_iDtMs) {
    if (!m_bRunning || l_iDtMs <= 0) return;
    m_iWaitMs = std::max(0, m_iWaitMs - l_iDtMs);
    m_fPos += float(m_iDir) * m_fSpeed * float(l_iDtMs);
    if (m_fPos >= 1.0f || m_fPos <= 0.0f) {   // an end: one sweep done
        m_fPos = std::clamp(m_fPos, 0.0f, 1.0f);
        m_iSweeps++;
        if (!m_oS.m_bTurnBack || m_iSweeps >= m_oS.m_iMaxSweeps) {
            f_Finish(false);
            return;
        }
        m_iDir = -m_iDir;
    }
    const int l_iNear = f_NearPoint();   // a window entered: the hint
    if (l_iNear >= 0 && l_iNear != m_iLastNear) m_vEntered.push_back(l_iNear);
    m_iLastNear = l_iNear;
}

float ComboPlay::f_Window() const {
    const float l_fGap = 1.0f / float(m_oS.m_iPoints + 1);   // between two points (and from an end to the first)
    return std::min(std::max(m_oS.m_fHitWindow, m_fSpeed * float(m_oS.m_iHitWindowMs)), 0.45f * l_fGap);
}

int ComboPlay::f_NearPoint() const {
    if (!m_bRunning) return -1;
    const float l_fW = f_Window();
    int l_iBest = -1;
    float l_fBestD = l_fW;
    for (size_t l_nI = 0; l_nI < m_vAt.size(); l_nI++) {
        const float l_fD = std::fabs(m_vAt[l_nI] - m_fPos);
        if (!m_vHit[l_nI] && l_fD <= l_fBestD) { l_fBestD = l_fD; l_iBest = (int)l_nI; }
    }
    return l_iBest;
}

std::vector<int> ComboPlay::f_TakeEntered() {
    std::vector<int> l_vOut;
    l_vOut.swap(m_vEntered);
    return l_vOut;
}

ComboPlay::Tap ComboPlay::f_Tap() {
    if (!m_bRunning || m_iWaitMs > 0) return Tap::Ignored;
    // The nearest point not hit yet, within its window -- or passed a moment ago (lateMs).
    const float l_fW = f_Window(), l_fLate = l_fW + m_fSpeed * float(m_oS.m_iLateMs);
    int l_iBest = -1;
    float l_fBestD = 2.0f;
    for (size_t l_nI = 0; l_nI < m_vAt.size(); l_nI++) {
        if (m_vHit[l_nI]) continue;
        const float l_fD = std::fabs(m_vAt[l_nI] - m_fPos);
        const bool l_bPassed = (m_fPos - m_vAt[l_nI]) * float(m_iDir) > 0;   // the marker went past it
        if ((l_fD <= l_fW || (l_bPassed && l_fD <= l_fLate)) && l_fD < l_fBestD) {
            l_fBestD = l_fD;
            l_iBest = (int)l_nI;
        }
    }
    if (l_iBest >= 0) {
        m_vHit[(size_t)l_iBest] = true;
        m_vOrder.push_back(l_iBest);
        m_iMissesInRow = 0;
        if (f_Hits() == m_oS.m_iPoints) f_Finish(false);
        return Tap::Hit;
    }
    m_iMissesInRow++;
    if (m_iMissesInRow >= 2) {   // two misses in a row: a fail
        f_Finish(true);
        return Tap::Miss;
    }
    // The combo goes back: the newest hits are lost (all of them for "to the start").
    int l_iBack = m_oS.m_iMissBack < 0 ? (int)m_vOrder.size() : std::min(m_oS.m_iMissBack, (int)m_vOrder.size());
    while (l_iBack-- > 0) {
        m_vHit[(size_t)m_vOrder.back()] = false;
        m_vOrder.pop_back();
    }
    m_iWaitMs = m_oS.m_iMissWaitMs;
    return Tap::Miss;
}

} // namespace toms
