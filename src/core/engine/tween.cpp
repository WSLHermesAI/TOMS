// tween.cpp -- see tween.h.
#include "tween.h"

#include <algorithm>
#include <cmath>

namespace toms {

float f_Ease(Ease l_eE, float l_fT) {
    l_fT = std::clamp(l_fT, 0.0f, 1.0f);
    switch (l_eE) {
        case Ease::Linear:     return l_fT;
        case Ease::InQuad:     return l_fT * l_fT;
        case Ease::OutQuad:    return l_fT * (2.0f - l_fT);
        case Ease::InOutQuad:  return l_fT < 0.5f ? 2.0f * l_fT * l_fT : 1.0f - std::pow(-2.0f * l_fT + 2.0f, 2.0f) / 2.0f;
        case Ease::InCubic:    return l_fT * l_fT * l_fT;
        case Ease::OutCubic:   return 1.0f - std::pow(1.0f - l_fT, 3.0f);
        case Ease::InOutCubic: return l_fT < 0.5f ? 4.0f * l_fT * l_fT * l_fT : 1.0f - std::pow(-2.0f * l_fT + 2.0f, 3.0f) / 2.0f;
        case Ease::OutBack: {
            const float l_fC1 = 1.70158f, l_fC3 = l_fC1 + 1.0f;
            return 1.0f + l_fC3 * std::pow(l_fT - 1.0f, 3.0f) + l_fC1 * std::pow(l_fT - 1.0f, 2.0f);
        }
        case Ease::OutBounce: {
            const float l_fN = 7.5625f, l_fD = 2.75f;
            if (l_fT < 1.0f / l_fD) return l_fN * l_fT * l_fT;
            if (l_fT < 2.0f / l_fD) { l_fT -= 1.5f / l_fD; return l_fN * l_fT * l_fT + 0.75f; }
            if (l_fT < 2.5f / l_fD) { l_fT -= 2.25f / l_fD; return l_fN * l_fT * l_fT + 0.9375f; }
            l_fT -= 2.625f / l_fD;
            return l_fN * l_fT * l_fT + 0.984375f;
        }
    }
    return l_fT;
}

Ease f_EaseFromName(const std::string& l_strN) {
    static const struct { const char* m_pszName; Ease m_eE; } l_arrTable[] = {
        {"linear", Ease::Linear}, {"inQuad", Ease::InQuad}, {"outQuad", Ease::OutQuad}, {"inOutQuad", Ease::InOutQuad},
        {"inCubic", Ease::InCubic}, {"outCubic", Ease::OutCubic}, {"inOutCubic", Ease::InOutCubic},
        {"outBack", Ease::OutBack}, {"outBounce", Ease::OutBounce},
    };
    for (const auto& l_oR : l_arrTable) if (l_strN == l_oR.m_pszName) return l_oR.m_eE;
    return Ease::Linear;
}

void Tween::f_Update(int l_iDtMs) {
    if (l_iDtMs <= 0) return;
    m_iElapsedMs += l_iDtMs;
    if (m_iElapsedMs < m_iDurationMs) return;
    switch (m_eLoop) {
        case Loop::Once:     m_iElapsedMs = m_iDurationMs; break;
        case Loop::Repeat:   m_iElapsedMs %= m_iDurationMs; break;
        case Loop::PingPong: {
            const int l_iPasses = m_iElapsedMs / m_iDurationMs;   // a long frame can cross more than one end
            m_iElapsedMs %= m_iDurationMs;
            if (l_iPasses % 2 == 1) m_bForward = !m_bForward;
            break;
        }
    }
}

void Tween::f_SetDuration(int l_iDurationMs) {
    l_iDurationMs = std::max(1, l_iDurationMs);
    const float l_fP = f_Progress();
    m_iDurationMs = l_iDurationMs;
    m_iElapsedMs = (int)std::lround(l_fP * m_iDurationMs);
}

float Tween::f_Progress() const { return std::clamp((float)m_iElapsedMs / (float)m_iDurationMs, 0.0f, 1.0f); }

float Tween::f_Value() const {
    const float l_fP = f_Progress();
    const float l_fK = f_Ease(m_eEase, m_bForward ? l_fP : 1.0f - l_fP);
    return m_fFrom + (m_fTo - m_fFrom) * l_fK;
}

} // namespace toms
