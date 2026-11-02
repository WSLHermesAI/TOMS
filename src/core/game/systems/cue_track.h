// cue_track.h -- a row of cues, one after another (docs/gameplay/16_SKILLS_AND_PLAYS.md §2, block B1): each cue pops in,
// waits, and opens an input window; no answer in time and it passes, the next one follows. Optionally an "!" warning
// (block B3) shows a while before the window and during it.
//
//   cue  |pop ........ [warn ..... [window ........]]| next cue
//        0                         cueMs - inputMs    cueMs        (windowAtEnd; else the window is the cue's start)
//
// It only keeps the time and each cue's state; what a cue asks for (a way, a shape, a spot) and what an answer does
// belong to its user (touch_play.h: the sword's prompts).
//
// Pure: time in ms; no Game dependency.
#pragma once
#include <vector>

namespace toms {

struct CueTiming {
    int m_iCueMs = 1000;         // each cue's whole time (the owner: 1 s)
    int m_iInputMs = 500;        // of which the input window (the owner: 0.5 s, adjustable)
    bool m_bWindowAtEnd = true;  // the window is the cue's last inputMs (see it, then answer); false: its first
    int m_iWarnMs = 0;           // an "!" this long before the window (and during it); 0: none
    int m_iGapMs = 150;          // after an answer, the next cue comes this soon
};

class CueTrack {
public:
    enum State { Waiting = 0, Answered = 1, Missed = 2 };
    void f_Start(int l_iCount, const CueTiming& l_oT);
    // The time. Returns how many cues ran out unanswered (missed) in this step.
    int f_Update(int l_iDtMs);
    // Answer the current cue (only while its window is open; else ignored, returns false). ok: right or wrong.
    bool f_Answer(bool l_bOk);

    bool f_Done() const { return m_iIndex >= (int)m_vStates.size(); }
    int f_Index() const { return m_iIndex; }
    int f_Count() const { return (int)m_vStates.size(); }
    int f_State(int l_iK) const { return m_vStates[(size_t)l_iK]; }
    int f_CueAgeMs() const { return m_iGapLeft > 0 ? -1 : m_iAgeMs; }   // since the current cue popped (-1: between cues)
    bool f_Open() const;                                            // its input window is open
    bool f_Warning() const;                                         // its "!" shows
    float f_TimeLeft() const;                                       // 0..1 of the current cue
private:
    CueTiming m_oT;
    std::vector<int> m_vStates;
    int m_iIndex = 0, m_iAgeMs = 0, m_iGapLeft = 0;
    int f_WindowFrom() const { return m_oT.m_bWindowAtEnd ? m_oT.m_iCueMs - m_oT.m_iInputMs : 0; }
    int f_WindowTo() const { return m_oT.m_bWindowAtEnd ? m_oT.m_iCueMs : m_oT.m_iInputMs; }
};

} // namespace toms
