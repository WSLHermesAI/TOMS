// timed_hits.h -- judging taps against hits due at given times (docs/gameplay/07_GAMEPLAY_PLAN.md §5, §9: "timing collisions
// with a time offset first").
//
// Every weapon's play is some form of this: points (circles, beats, polygon corners, a marker crossing a spot) are
// due at known times; a tap near one is a hit, graded by how close it was; a point nobody tapped in time is missed.
// Each point also has an appear time, so its picture can grow from "appears" to "due" (the staff's growing lines,
// a shrinking circle): growth() is that 0..1.
//
//   toms::TimedTrack track(perfectMs, goodMs);
//   track.add(0, 600);  track.add(300, 900);      // appear at, due at (ms on the track's clock)
//   track.update(dt);  auto r = track.tap();      // r.grade: Perfect / Good / Miss, r.index: which point
//   for (int i : track.takeMissed()) ...          // points that went past their window untapped
//
// Ordered tracks (the sword, the staff) only accept the next point; free tracks (unarmed) accept any open one.
// Plain C++, no engine dependencies: other games can reuse it.
#pragma once

#include <vector>

namespace toms {

class TimedTrack {
public:
    enum class Grade { None, Perfect, Good, Miss };
    struct Point { int m_iAppearMs = 0, m_iDueMs = 0; Grade m_eResult = Grade::None; int m_iOffsetMs = 0; };
    struct Tap { Grade m_eGrade = Grade::Miss; int m_iIndex = -1; int m_iOffsetMs = 0; };   // offset: tap time - due (+ late)

    // perfectMs / goodMs: how far from the due time a tap still counts (each side). DEX widens them (the caller).
    TimedTrack(int l_iPerfectMs = 40, int l_iGoodMs = 110, bool l_bOrdered = true)
        : m_iPerfectMs(l_iPerfectMs), m_iGoodMs(l_iGoodMs < l_iPerfectMs ? l_iPerfectMs : l_iGoodMs), m_bOrdered(l_bOrdered) {}

    int f_Add(int l_iAppearMs, int l_iDueMs);           // returns the point's index
    void f_Clear() { m_vPoints.clear(); m_iNowMs = 0; m_vMissedQueue.clear(); }
    void f_Update(int l_iDtMs);                      // advances the clock; points past their window become missed
    Tap f_Tap();                                  // a tap now: the nearest open point within the good window
    std::vector<int> f_TakeMissed();              // points that became missed since the last call

    int f_Now() const { return m_iNowMs; }
    const std::vector<Point>& f_Points() const { return m_vPoints; }
    // How far point i has grown: 0 before it appears, 1 at its due time (more after, until it is judged).
    float f_Growth(int l_iI) const;
    bool f_Finished() const;                      // every point judged
    int f_Hits() const;                           // Perfect + Good
    int f_Perfects() const;

private:
    bool f_Open(const Point& l_oP) const { return l_oP.m_eResult == Grade::None; }
    int f_NextOpen() const;
    std::vector<Point> m_vPoints;
    std::vector<int> m_vMissedQueue;
    int m_iNowMs = 0;
    int m_iPerfectMs, m_iGoodMs;
    bool m_bOrdered;
};

} // namespace toms
