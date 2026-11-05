// touch_play.cpp -- see touch_play.h.
#include "touch_play.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {
constexpr float g_fPi = 3.14159265f;
}

void TouchPlay::f_Start(PlayType l_eType, int l_iPoints, bool l_bUltimate, const glm::vec4& l_v4Area, uint32_t l_uSeed,
                      const TouchPlaySettings& l_oS) {
    *this = TouchPlay{};
    m_eType = l_eType; m_oS = l_oS; m_v4Area = l_v4Area; m_oRng.seed(l_uSeed);
    m_iPoints = std::max(1, l_iPoints); m_bUltimate = l_bUltimate;
    m_bRunning = true;
    m_bStarted = l_bUltimate || m_eType != PlayType::ePULL_RELEASE;   // the bow waits for its first pull; the others run at once
    std::uniform_real_distribution<float> l_oU(0.0f, 1.0f);
    if (m_eType == PlayType::ePULL_RELEASE) f_NextTarget();
    if (f_IsSwipePlay(m_eType)) {
        const char* l_pszDefault = m_eType == PlayType::eSWIPE_GESTURE ? "v" : "way";
        std::vector<int> l_vExtra;
        for (int l_iK = 0; l_iK < m_iPoints; l_iK++) {
            m_vCueKinds.push_back(l_iK < (int)m_oS.m_vSliceCues.size() && !m_oS.m_vSliceCues[(size_t)l_iK].empty() ? m_oS.m_vSliceCues[(size_t)l_iK] : l_pszDefault);
            m_vCueDirs.push_back(std::uniform_int_distribution<int>(0, 7)(m_oRng));
            const auto l_itExtra = m_oS.m_mapCueExtraMs.find(m_vCueKinds.back());
            l_vExtra.push_back(l_itExtra == m_oS.m_mapCueExtraMs.end() ? 0 : l_itExtra->second);
        }
        m_oCueTrack.f_Start(m_iPoints, m_oS.m_oSliceCue, l_vExtra);
    }
    if (m_eType == PlayType::eSEQUENCE_CLICK) {
        const float l_fM = m_oS.m_fCircleRadius;
        for (int l_iK = 0; l_iK < m_iPoints; l_iK++) {   // apart from the others (a few tries; a small area takes what it gets)
            Circle l_oC;
            for (int l_iTryNo = 0; l_iTryNo < 12; l_iTryNo++) {
                l_oC.m_fX = m_v4Area.x + l_fM + l_oU(m_oRng) * std::max(0.0f, m_v4Area.z - 2 * l_fM);
                l_oC.m_fY = m_v4Area.y + l_fM + l_oU(m_oRng) * std::max(0.0f, m_v4Area.w - 2 * l_fM);
                bool l_bClear = true;
                for (const Circle& l_oO : m_vCircles) l_bClear = l_bClear && std::hypot(l_oC.m_fX - l_oO.m_fX, l_oC.m_fY - l_oO.m_fY) >= 2.6f * l_fM;
                if (l_bClear) break;
            }
            m_vCircles.push_back(l_oC);
        }
    }
    if (m_eType == PlayType::eDRAW_SHAPE) {
        m_oTrace.f_Start(m_oS.m_strShape, {m_v4Area.x + m_v4Area.z * 0.5f, m_v4Area.y + m_v4Area.w * 0.5f}, m_oS.m_fShapeSize, m_oS.m_fShapeCornerRadius * (1.0f + m_oS.m_fCornerBonus));
        m_iPoints = std::max(1, m_oTrace.f_StepCount());
    }
}

// ---- bow ----

void TouchPlay::f_NextTarget() {
    std::uniform_real_distribution<float> l_oU(0.0f, 1.0f);
    m_oTarget.m_fR = m_oS.m_fBowRadius * (m_bUltimate ? 0.8f : 1.0f);
    // the targets keep to the monster's middle; a move's spread over the whole area
    const float l_fSpread = m_bUltimate ? 1.0f : 0.45f;
    const float l_fCx = m_v4Area.x + m_v4Area.z * 0.5f, l_fCy = m_v4Area.y + m_v4Area.w * 0.5f;
    const float l_fHw = std::max(0.0f, m_v4Area.z * 0.5f - m_oTarget.m_fR), l_fHh = std::max(0.0f, m_v4Area.w * 0.5f - m_oTarget.m_fR);
    m_v2TargetTo = {l_fCx + (l_oU(m_oRng) - 0.5f) * 2.0f * l_fHw * l_fSpread, l_fCy + (l_oU(m_oRng) - 0.5f) * 2.0f * l_fHh * l_fSpread};
    const float l_fA = l_oU(m_oRng) * 2.0f * g_fPi;   // it comes in from a little way off, then stops (the owner: 0.5 s)
    m_v2TargetFrom = m_v2TargetTo + glm::vec2(std::cos(l_fA), std::sin(l_fA)) * m_oS.m_fBowMoveDist;
    m_v2TargetFrom.x = std::clamp(m_v2TargetFrom.x, m_v4Area.x + m_oTarget.m_fR, m_v4Area.x + std::max(m_oTarget.m_fR, m_v4Area.z - m_oTarget.m_fR));
    m_v2TargetFrom.y = std::clamp(m_v2TargetFrom.y, m_v4Area.y + m_oTarget.m_fR, m_v4Area.y + std::max(m_oTarget.m_fR, m_v4Area.w - m_oTarget.m_fR));
    m_iTargetAgeMs = 0;
    m_iOneMs = 0;
    m_bAiming = false;
    m_iHoldMs = 0;
    const glm::vec2 l_v2P = f_TargetPos();
    m_oTarget.m_fX = l_v2P.x; m_oTarget.m_fY = l_v2P.y;
}

glm::vec2 TouchPlay::f_TargetPos() const {
    const float l_fK = std::clamp((float)m_iTargetAgeMs / (float)std::max(1, m_oS.m_iBowMoveMs), 0.0f, 1.0f);
    const float l_fE = 1.0f - (1.0f - l_fK) * (1.0f - l_fK);   // eased out: it slows down to a stop
    return m_v2TargetFrom + (m_v2TargetTo - m_v2TargetFrom) * l_fE;
}

float TouchPlay::f_Charge() const {
    return m_bAiming ? std::clamp((float)m_iHoldMs / (float)std::max(1, m_oS.m_iBowHoldMs), 0.0f, 1.0f) : 0.0f;
}

glm::vec2 TouchPlay::f_Crosshair() const {   // drag back, the crosshair goes the other way (a slingshot), and it shakes
    glm::vec2 l_v2C = m_v2AimFrom - (m_v2AimTo - m_v2AimFrom) * m_oS.m_fBowPull;
    const float l_fAmp = m_oS.m_fBowShakePx * (float)m_iHoldMs / 1000.0f;
    l_v2C += glm::vec2(std::sin((float)m_iAgeMs * 0.021f), std::cos((float)m_iAgeMs * 0.017f)) * l_fAmp;
    return l_v2C;
}

void TouchPlay::f_Release() {
    const glm::vec2 l_v2D = m_v2AimTo - m_v2AimFrom;
    const float l_fLen = std::sqrt(l_v2D.x * l_v2D.x + l_v2D.y * l_v2D.y);
    const float l_fCh = f_Charge();
    m_bAiming = false;
    if (l_fLen < m_oS.m_fBowMinPull) { m_iHoldMs = 0; return; }   // not a shot
    const glm::vec2 l_v2C = f_Crosshair();
    m_iHoldMs = 0;
    m_fChargeSum += l_fCh;
    m_iShots++;
    const float l_fDist = std::hypot(l_v2C.x - m_oTarget.m_fX, l_v2C.y - m_oTarget.m_fY);
    f_FinishOne(l_fDist <= m_oTarget.m_fR, l_fDist <= m_oTarget.m_fR * 0.4f, l_v2C.x, l_v2C.y);
}

// ---- slice ----

glm::vec2 TouchPlay::f_PromptAt() const { return {m_v4Area.x + m_v4Area.z * 0.5f, m_v4Area.y + m_v4Area.w - 50.0f}; }

void TouchPlay::f_JudgeStroke() {
    if (m_vStroke.size() < 2 || !m_oCueTrack.f_Open()) return;   // only the window takes the answer (a swipe too soon: nothing)
    const int l_iK = m_oCueTrack.f_Index();
    const glm::vec2 l_v2D = m_vStroke.back() - m_vStroke.front();
    const float l_fLen = std::sqrt(l_v2D.x * l_v2D.x + l_v2D.y * l_v2D.y);
    bool l_bOk = false, l_bPerfect = false;
    if (m_vCueKinds[(size_t)l_iK] == "way") {
        if (l_fLen < m_oS.m_fSliceMinLength) return;   // not a cut
        const float l_fWant = (float)m_vCueDirs[(size_t)l_iK] * g_fPi / 4.0f, l_fGot = std::atan2(l_v2D.y, l_v2D.x);
        const float l_fDiff = std::fabs(std::fmod(l_fGot - l_fWant + 3.0f * g_fPi, 2.0f * g_fPi) - g_fPi);
        l_bOk = l_fDiff < m_oS.m_fSliceTolerance;
        l_bPerfect = l_fDiff < m_oS.m_fSliceTolerance * 0.45f && l_fLen > 2.0f * m_oS.m_fSliceMinLength;
    } else {
        glm::vec2 l_v2Lo = m_vStroke[0], l_v2Hi = m_vStroke[0];
        for (glm::vec2 l_v2P : m_vStroke) { l_v2Lo = glm::min(l_v2Lo, l_v2P); l_v2Hi = glm::max(l_v2Hi, l_v2P); }
        if (std::max(l_v2Hi.x - l_v2Lo.x, l_v2Hi.y - l_v2Lo.y) < m_oS.m_fSliceMinLength) return;   // too small to be a shape
        const std::string& l_strCue = m_vCueKinds[(size_t)l_iK];
        if (f_GestureTier(l_strCue) > 1) {
            l_bOk = f_MatchGesture(m_vStroke, l_strCue, m_oS.m_fGestureExtraDeg);
            l_bPerfect = l_bOk && f_MatchGesture(m_vStroke, l_strCue, m_oS.m_fGestureExtraDeg - 12.0f);
        } else {
            l_bOk = f_MatchStroke(m_vStroke, l_strCue, m_oS.m_fShapeTolerance);
            l_bPerfect = l_bOk && f_MatchStroke(m_vStroke, l_strCue, m_oS.m_fShapeTolerance * 0.65f);
        }
    }
    m_oCueTrack.f_Answer(l_bOk);
    const glm::vec2 l_v2At = f_PromptAt();
    f_FinishOne(l_bOk, l_bPerfect, l_v2At.x, l_v2At.y);
}

// ---- the plays ----

int TouchPlay::f_CirclesLit() const {
    if (m_eCirclePhase != CirclePhase::Count) return (int)m_vCircles.size();
    return std::min((int)m_vCircles.size(), m_iPhaseMs / std::max(1, m_oS.m_iCircleCountMs) + 1);
}

float TouchPlay::f_TimeLeft() const {
    if (!m_bStarted) return 1.0f;
    auto l_fnLeft = [](int l_iUsed, int l_iAll) { return std::clamp(1.0f - (float)l_iUsed / (float)std::max(1, l_iAll), 0.0f, 1.0f); };
    switch (m_eType) {
    case PlayType::ePULL_RELEASE: return l_fnLeft(m_iOneMs, m_oS.m_iBowTimeMs);
    case PlayType::eSWIPE_WAY:
    case PlayType::eSWIPE_GESTURE: return m_oCueTrack.f_TimeLeft();
    case PlayType::eFREE_SWIPE:
    case PlayType::eFREE_TAP:
    case PlayType::eFREE_SHOT: return l_fnLeft(m_iClockMs, m_oS.m_iFreeTimeMs);
    case PlayType::eSEQUENCE_CLICK: return m_eCirclePhase == CirclePhase::Tap ? l_fnLeft(m_iPhaseMs, m_oS.m_iCircleTapMs * m_iPoints) : 1.0f;
    case PlayType::eDRAW_SHAPE: return l_fnLeft(m_iClockMs, m_oS.m_iShapeTimeMs);
    case PlayType::eMASH: return l_fnLeft(m_iClockMs, m_oS.m_iMashTimeMs);
    default: return 1.0f;
    }
}

void TouchPlay::f_FinishOne(bool l_bOk, bool l_bPerfect, float l_fX, float l_fY) {
    m_vMarks.push_back({l_fX, l_fY, l_bOk, 0});
    m_vHits.push_back({l_bOk, l_bOk && l_bPerfect, l_fX, l_fY});
    if (l_bOk) { m_iHitCount++; if (l_bPerfect) m_iPerfects++; }
    m_iLastAnswerMs = m_iClockMs;
    m_iIndex++;
    m_iOneMs = 0;
    if (f_IsFreePlay(m_eType)) { m_iPoints = std::max(1, m_iHitCount); return; }   // as many as the player makes, until the time is up
    if (m_iIndex >= m_iPoints) { m_bDone = true; return; }
    if (m_eType == PlayType::ePULL_RELEASE) f_NextTarget();
}

void TouchPlay::f_Pointer(int l_iPhase, float l_fX, float l_fY) {
    if (!f_Running()) return;
    const glm::vec2 l_v2P(l_fX, l_fY);
    switch (m_eType) {
    case PlayType::ePULL_RELEASE:
        if (l_iPhase == 0) { m_bAiming = true; m_v2AimFrom = m_v2AimTo = l_v2P; m_iHoldMs = 0; m_bStarted = true; return; }
        if (!m_bAiming) return;
        m_v2AimTo = l_v2P;
        if (l_iPhase == 2) f_Release();
        return;
    case PlayType::eFREE_TAP:
        if (l_iPhase == 0) f_FinishOne(true, false, l_fX, l_fY);
        return;
    case PlayType::eFREE_SWIPE:
    case PlayType::eFREE_SHOT:
        if (l_iPhase == 0) { m_bAiming = true; m_vStroke.assign(1, l_v2P); return; }
        if (!m_bAiming) return;
        if (glm::length(l_v2P - m_vStroke.back()) >= 3.0f) m_vStroke.push_back(l_v2P);
        if (l_iPhase == 2) {
            m_bAiming = false;
            if (glm::length(l_v2P - m_vStroke.front()) >= m_oS.m_fFreeMinSwipe) {   // a shot always hits: it lands on the monster
                if (m_eType == PlayType::eFREE_SHOT) f_FinishOne(true, false, m_v4Area.x + m_v4Area.z * 0.5f, m_v4Area.y + m_v4Area.w * 0.5f);
                else f_FinishOne(true, false, l_fX, l_fY);
            }
            m_vStroke.clear();
        }
        return;
    case PlayType::eSWIPE_WAY:
    case PlayType::eSWIPE_GESTURE:
        if (l_iPhase == 0) { m_bAiming = true; m_vStroke.assign(1, l_v2P); return; }
        if (!m_bAiming) return;
        if (m_vStroke.empty() || glm::length(l_v2P - m_vStroke.back()) >= 3.0f) m_vStroke.push_back(l_v2P);
        if (l_iPhase == 2) { m_bAiming = false; f_JudgeStroke(); m_vStroke.clear(); }
        return;
    case PlayType::eSEQUENCE_CLICK: {
        if (l_iPhase != 0 || m_eCirclePhase != CirclePhase::Tap) return;   // only once they are back and the "!" has gone
        for (size_t l_nK = 0; l_nK < m_vCircles.size(); l_nK++) {
            Circle& l_oC = m_vCircles[l_nK];
            if (l_oC.m_iState != 0 || std::hypot(l_fX - l_oC.m_fX, l_fY - l_oC.m_fY) > m_oS.m_fCircleRadius * 1.2f) continue;
            if ((int)l_nK == m_iIndex) {   // the next in the remembered order: a spell
                const bool l_bPerfect = m_iClockMs - m_iLastAnswerMs <= m_oS.m_iCirclePerfectMs;
                l_oC.m_iState = l_bPerfect ? 2 : 1;
                f_FinishOne(true, l_bPerfect, l_oC.m_fX, l_oC.m_fY);
            } else {                  // a wrong circle: the one due is missed, the play goes on (Q40)
                Circle& l_oDue = m_vCircles[(size_t)m_iIndex];
                l_oDue.m_iState = 3;
                f_FinishOne(false, false, l_oDue.m_fX, l_oDue.m_fY);
            }
            return;
        }
        return;
    }
    case PlayType::eDRAW_SHAPE: {
        const int l_iBefore = m_oTrace.f_Steps();
        if (m_oTrace.f_Pointer(l_iPhase, l_fX, l_fY) <= 0) return;
        for (int l_iK = l_iBefore; l_iK < m_oTrace.f_Steps(); l_iK++) {
            const glm::vec2 l_v2C = m_oTrace.f_Points()[(size_t)l_iK + 1];
            f_FinishOne(true, m_iClockMs - m_iLastAnswerMs <= m_oS.m_iShapeStepPerfectMs, l_v2C.x, l_v2C.y);
        }
        return;
    }
    case PlayType::eMASH:
        if (l_iPhase == 0) f_FinishOne(true, true, l_fX, l_fY);
        return;
    default:
        return;
    }
}

void TouchPlay::f_Update(int l_iDtMs) {
    for (Mark& l_oM : m_vMarks) l_oM.m_iAgeMs += l_iDtMs;
    if (!f_Running()) return;
    m_iAgeMs += l_iDtMs;
    if (m_eType == PlayType::ePULL_RELEASE) {   // the target moves a moment, then stops; it does so before the first pull too
        m_iTargetAgeMs += l_iDtMs;
        const glm::vec2 l_v2P = f_TargetPos();
        m_oTarget.m_fX = l_v2P.x; m_oTarget.m_fY = l_v2P.y;
        if (m_bAiming) {
            m_iHoldMs += l_iDtMs;
            if (m_iHoldMs >= m_oS.m_iBowHoldMs) { m_iHoldMs = m_oS.m_iBowHoldMs; f_Release(); }   // the charge limit: the arrow goes (Q41)
        }
    }
    if (!m_bStarted || !f_Running()) return;
    m_iClockMs += l_iDtMs;
    m_iOneMs += l_iDtMs;
    switch (m_eType) {
    case PlayType::ePULL_RELEASE:
        if (m_iOneMs > m_oS.m_iBowTimeMs) { m_bAiming = false; m_iHoldMs = 0; f_FinishOne(false, false, m_oTarget.m_fX, m_oTarget.m_fY); }
        break;
    case PlayType::eFREE_SWIPE:
    case PlayType::eFREE_TAP:
    case PlayType::eFREE_SHOT:
        if (m_iClockMs >= m_oS.m_iFreeTimeMs) {   // time up: what was done counts
            m_bAiming = false;
            m_bFailed = m_iHitCount == 0;
            m_iPoints = std::max(1, m_iHitCount);
            m_iIndex = m_iPoints;
            m_bDone = true;
        }
        break;
    case PlayType::eSWIPE_WAY:
    case PlayType::eSWIPE_GESTURE: {
        const int l_iMissed = m_oCueTrack.f_Update(l_iDtMs);
        for (int l_iK = 0; l_iK < l_iMissed; l_iK++) {   // a prompt let pass: a miss (finishOne counts it; the track has moved on)
            const glm::vec2 l_v2At = f_PromptAt();
            f_FinishOne(false, false, l_v2At.x, l_v2At.y);
        }
        break;
    }
    case PlayType::eSEQUENCE_CLICK: {
        m_iPhaseMs += l_iDtMs;
        const int l_iN = (int)m_vCircles.size();
        if (m_eCirclePhase == CirclePhase::Count && m_iPhaseMs >= l_iN * m_oS.m_iCircleCountMs) { m_eCirclePhase = CirclePhase::Hide; m_iPhaseMs = 0; }
        if (m_eCirclePhase == CirclePhase::Hide && m_iPhaseMs >= m_oS.m_iCircleHideMs) { m_eCirclePhase = CirclePhase::Warn; m_iPhaseMs = 0; }
        if (m_eCirclePhase == CirclePhase::Warn && m_iPhaseMs >= m_oS.m_iCircleWarnMs) {
            m_eCirclePhase = CirclePhase::Tap; m_iPhaseMs = 0; m_iLastAnswerMs = m_iClockMs;
        }
        if (m_eCirclePhase == CirclePhase::Tap && m_iPhaseMs >= m_oS.m_iCircleTapMs * l_iN)   // time up: the rest are missed
            while (!m_bDone) { m_vCircles[(size_t)m_iIndex].m_iState = 3; f_FinishOne(false, false, m_vCircles[(size_t)m_iIndex].m_fX, m_vCircles[(size_t)m_iIndex].m_fY); }
        break;
    }
    case PlayType::eDRAW_SHAPE:
    case PlayType::eMASH:
        if (m_iClockMs >= (m_eType == PlayType::eDRAW_SHAPE ? m_oS.m_iShapeTimeMs : m_oS.m_iMashTimeMs)) {   // time up: what is done counts
            m_bFailed = m_iHitCount == 0;
            m_iIndex = m_iPoints;
            m_bDone = true;
        }
        break;
    default:
        break;
    }
}

TouchPlaySettings f_TouchPlayFromJson(const nlohmann::json& l_jB) {
    TouchPlaySettings l_oS;
    if (!l_jB.is_object() || !l_jB.contains("plays") || !l_jB["plays"].is_object()) return l_oS;
    const nlohmann::json& l_jJ = l_jB["plays"];
    l_oS.m_iBowTimeMs = std::max(500, l_jJ.value("bowTimeMs", l_oS.m_iBowTimeMs));
    l_oS.m_fBowRadius = std::max(8.0f, l_jJ.value("bowRadius", l_oS.m_fBowRadius));
    l_oS.m_fBowPull = std::max(0.1f, l_jJ.value("bowPull", l_oS.m_fBowPull));
    l_oS.m_iBowMoveMs = std::max(0, l_jJ.value("bowMoveMs", l_oS.m_iBowMoveMs));
    l_oS.m_fBowMoveDist = std::max(0.0f, l_jJ.value("bowMoveDist", l_oS.m_fBowMoveDist));
    l_oS.m_iBowHoldMs = std::max(200, l_jJ.value("bowHoldMs", l_oS.m_iBowHoldMs));
    l_oS.m_fBowShakePx = std::max(0.0f, l_jJ.value("bowShakePx", l_oS.m_fBowShakePx));
    l_oS.m_iBowHoldPerStr = std::max(0, l_jJ.value("bowHoldPerStr", l_oS.m_iBowHoldPerStr));
    l_oS.m_fBowShakePerDex = std::max(0.0f, l_jJ.value("bowShakePerDex", l_oS.m_fBowShakePerDex));
    l_oS.m_oSliceCue.m_iCueMs = std::max(200, l_jJ.value("cueMs", l_oS.m_oSliceCue.m_iCueMs));
    l_oS.m_oSliceCue.m_iInputMs = std::clamp(l_jJ.value("inputMs", l_oS.m_oSliceCue.m_iInputMs), 50, l_oS.m_oSliceCue.m_iCueMs);
    l_oS.m_oSliceCue.m_bWindowAtEnd = l_jJ.value("inputAtEnd", l_oS.m_oSliceCue.m_bWindowAtEnd);
    l_oS.m_oSliceCue.m_iGapMs = std::max(0, l_jJ.value("cueGapMs", l_oS.m_oSliceCue.m_iGapMs));
    l_oS.m_iCircleCountMs = std::max(100, l_jJ.value("circleCountMs", l_oS.m_iCircleCountMs));
    l_oS.m_iCircleHideMs = std::max(0, l_jJ.value("circleHideMs", l_oS.m_iCircleHideMs));
    l_oS.m_iCircleWarnMs = std::max(0, l_jJ.value("circleWarnMs", l_oS.m_iCircleWarnMs));
    l_oS.m_iCircleTapMs = std::max(200, l_jJ.value("circleTapMs", l_oS.m_iCircleTapMs));
    l_oS.m_iCirclePerfectMs = std::max(0, l_jJ.value("circlePerfectMs", l_oS.m_iCirclePerfectMs));
    if (l_jJ.contains("cueExtraMs") && l_jJ["cueExtraMs"].is_object())
        for (auto l_oIt = l_jJ["cueExtraMs"].begin(); l_oIt != l_jJ["cueExtraMs"].end(); ++l_oIt)
            if (l_oIt.value().is_number()) l_oS.m_mapCueExtraMs[l_oIt.key()] = std::max(0, l_oIt.value().get<int>());
    l_oS.m_fFreeMinSwipe = std::max(1.0f, l_jJ.value("freeMinSwipe", l_oS.m_fFreeMinSwipe));
    l_oS.m_fDexWayDeg = std::max(0.0f, l_jJ.value("dexWayDeg", l_oS.m_fDexWayDeg));
    l_oS.m_fDexWayMaxDeg = std::max(0.0f, l_jJ.value("dexWayMaxDeg", l_oS.m_fDexWayMaxDeg));
    l_oS.m_fDexGestureMaxDeg = std::max(0.0f, l_jJ.value("dexGestureMaxDeg", l_oS.m_fDexGestureMaxDeg));
    l_oS.m_fDexCornerPct = std::max(0.0f, l_jJ.value("dexCornerPct", l_oS.m_fDexCornerPct));
    l_oS.m_iDexWindowMs = std::max(0, l_jJ.value("dexWindowMs", l_oS.m_iDexWindowMs));
    return l_oS;
}

ComboResult TouchPlay::f_Result() const {
    ComboResult l_oR;
    l_oR.m_iPoints = m_iPoints;
    l_oR.m_iHits = m_iHitCount;
    l_oR.m_iPerfects = m_iPerfects;
    l_oR.m_bFailed = m_bFailed || m_iHitCount == 0;
    return l_oR;
}

} // namespace toms
