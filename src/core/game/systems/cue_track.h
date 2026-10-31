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
    int cueMs = 1000;         // each cue's whole time (the owner: 1 s)
    int inputMs = 500;        // of which the input window (the owner: 0.5 s, adjustable)
    bool windowAtEnd = true;  // the window is the cue's last inputMs (see it, then answer); false: its first
    int warnMs = 0;           // an "!" this long before the window (and during it); 0: none
    int gapMs = 150;          // after an answer, the next cue comes this soon
};

class CueTrack {
public:
    enum State { Waiting = 0, Answered = 1, Missed = 2 };
    void start(int count, const CueTiming& t);
    // The time. Returns how many cues ran out unanswered (missed) in this step.
    int update(int dtMs);
    // Answer the current cue (only while its window is open; else ignored, returns false). ok: right or wrong.
    bool answer(bool ok);

    bool done() const { return index_ >= (int)states_.size(); }
    int index() const { return index_; }
    int count() const { return (int)states_.size(); }
    int state(int k) const { return states_[(size_t)k]; }
    int cueAgeMs() const { return gapLeft_ > 0 ? -1 : ageMs_; }   // since the current cue popped (-1: between cues)
    bool open() const;                                            // its input window is open
    bool warning() const;                                         // its "!" shows
    float timeLeft() const;                                       // 0..1 of the current cue
private:
    CueTiming t_;
    std::vector<int> states_;
    int index_ = 0, ageMs_ = 0, gapLeft_ = 0;
    int windowFrom() const { return t_.windowAtEnd ? t_.cueMs - t_.inputMs : 0; }
    int windowTo() const { return t_.windowAtEnd ? t_.cueMs : t_.inputMs; }
};

} // namespace toms
