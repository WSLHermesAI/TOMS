// cue_track.cpp -- see cue_track.h.
#include "cue_track.h"

#include <algorithm>

namespace toms {

void CueTrack::f_Start(int l_iCount, const CueTiming& l_oT, const std::vector<int>& l_vExtraMs) {
    *this = CueTrack{};
    m_oT = l_oT;
    m_oT.m_iCueMs = std::max(1, m_oT.m_iCueMs);
    m_oT.m_iInputMs = std::clamp(m_oT.m_iInputMs, 1, m_oT.m_iCueMs);
    m_vStates.assign((size_t)std::max(0, l_iCount), Waiting);
    m_vExtraMs = l_vExtraMs;
}

bool CueTrack::f_Open() const {
    return !f_Done() && m_iGapLeft <= 0 && m_iAgeMs >= f_WindowFrom() && m_iAgeMs < f_WindowTo();
}

bool CueTrack::f_Warning() const {
    return m_oT.m_iWarnMs > 0 && !f_Done() && m_iGapLeft <= 0 && m_iAgeMs >= f_WindowFrom() - m_oT.m_iWarnMs && m_iAgeMs < f_WindowTo();
}

float CueTrack::f_TimeLeft() const {
    if (f_Done() || m_iGapLeft > 0) return 0.0f;
    return std::clamp(1.0f - (float)m_iAgeMs / (float)std::max(1, f_WindowTo()), 0.0f, 1.0f);
}

int CueTrack::f_Update(int l_iDtMs) {
    int l_iMissed = 0;
    while (l_iDtMs > 0 && !f_Done()) {
        if (m_iGapLeft > 0) {   // between an answer and the next cue
            const int l_iD = std::min(l_iDtMs, m_iGapLeft);
            m_iGapLeft -= l_iD; l_iDtMs -= l_iD;
            continue;
        }
        const int l_iEnd = f_WindowTo();   // the cue ends when its window closes
        const int l_iD = std::min(l_iDtMs, l_iEnd - m_iAgeMs);
        m_iAgeMs += l_iD; l_iDtMs -= l_iD;
        if (m_iAgeMs >= l_iEnd) {   // its window closed unanswered: missed, the next one at once
            m_vStates[(size_t)m_iIndex] = Missed;
            l_iMissed++;
            m_iIndex++;
            m_iAgeMs = 0;
        }
    }
    return l_iMissed;
}

bool CueTrack::f_Answer(bool l_bOk) {
    if (!f_Open()) return false;
    m_vStates[(size_t)m_iIndex] = l_bOk ? Answered : Missed;
    m_iIndex++;
    m_iAgeMs = 0;
    m_iGapLeft = f_Done() ? 0 : m_oT.m_iGapMs;
    return true;
}

} // namespace toms
