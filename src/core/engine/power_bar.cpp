#include "power_bar.h"
#include <algorithm>
#include <cmath>

namespace toms {

float f_SimulatePosition(const PowerBarParams& l_oP, float l_fHeldSeconds, float l_fStepSeconds) {
    if (l_fHeldSeconds <= 0.0f) return 0.0f;
    float l_fSpan = 2.0f * l_oP.m_fRedOuter;
    float l_fPosition = 0.0f;
    float l_fDirection = 1.0f;
    float l_fT = 0.0f;
    while (l_fT < l_fHeldSeconds) {
        float l_fDt = std::min(l_fStepSeconds, l_fHeldSeconds - l_fT);
        float l_fProgress = l_oP.m_fRampTime > 0.0f ? std::min(1.0f, l_fT / l_oP.m_fRampTime) : 1.0f;
        float l_fSpeed = l_oP.m_fV0 + (l_oP.m_fVmax - l_oP.m_fV0) * (l_fProgress * l_fProgress * l_fProgress);   // cubic ease-in
        l_fPosition += l_fDirection * l_fSpeed * l_fDt;
        if (l_fPosition > l_fSpan) { l_fPosition = 2.0f * l_fSpan - l_fPosition; l_fDirection = -1.0f; }
        if (l_fPosition < 0.0f) { l_fPosition = -l_fPosition; l_fDirection = 1.0f; }
        l_fT += l_fDt;
    }
    return l_fPosition;
}

float f_PowerFromPosition(const PowerBarParams& l_oP, float l_fPosition) {
    float l_fCenter = l_oP.m_fRedOuter;
    float l_fDist = std::fabs(l_fPosition - l_fCenter);
    if (l_fDist <= l_oP.m_fGreenHalf) {
        float l_fT = l_oP.m_fGreenHalf <= 0.0f ? 0.0f : l_fDist / l_oP.m_fGreenHalf;
        return 100.0f - 29.0f * l_fT;                       // 100 (center) .. 71 (green edge)
    } else if (l_fDist <= l_oP.m_fBlueOuter) {
        float l_fT = (l_fDist - l_oP.m_fGreenHalf) / (l_oP.m_fBlueOuter - l_oP.m_fGreenHalf);
        return 70.0f - 39.0f * l_fT;                         // 70 .. 31
    } else {
        float l_fT = std::min(1.0f, (l_fDist - l_oP.m_fBlueOuter) / (l_oP.m_fRedOuter - l_oP.m_fBlueOuter));
        return 30.0f - 30.0f * l_fT;                         // 30 .. 0 (edge)
    }
}

PowerZone f_ZoneFromPosition(const PowerBarParams& l_oP, float l_fPosition) {
    float l_fDist = std::fabs(l_fPosition - l_oP.m_fRedOuter);
    if (l_fDist <= l_oP.m_fGreenHalf) return PowerZone::Green;
    if (l_fDist <= l_oP.m_fBlueOuter) return PowerZone::Blue;
    return PowerZone::Red;
}

int f_ComputeAttackDamage(int l_iBaseHit, float l_fPowerPercent, float l_fMaxMult) {
    float l_fMult = 0.2f + (l_fMaxMult - 0.2f) * (l_fPowerPercent / 100.0f);
    int l_iDmg = (int)std::ceil(l_iBaseHit * l_fMult);
    if (l_fPowerPercent >= 97.0f) l_iDmg = (int)std::ceil(l_iDmg * 1.25f);
    return l_iDmg;
}

int f_ComputeDefenseDamage(int l_iIncoming, float l_fPowerPercent, float l_fMitigationFloor) {
    if (l_fPowerPercent >= 99.0f) return 0;   // Perfect Guard, regardless of any floor
    float l_fMitigation = l_fPowerPercent / 100.0f;
    if (l_fMitigation < l_fMitigationFloor) l_fMitigation = l_fMitigationFloor;
    return (int)std::ceil(l_iIncoming * (1.0f - l_fMitigation));
}

} // namespace toms
