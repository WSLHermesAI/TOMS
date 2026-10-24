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
    struct Point { int appearMs = 0, dueMs = 0; Grade result = Grade::None; int offsetMs = 0; };
    struct Tap { Grade grade = Grade::Miss; int index = -1; int offsetMs = 0; };   // offset: tap time - due (+ late)

    // perfectMs / goodMs: how far from the due time a tap still counts (each side). DEX widens them (the caller).
    TimedTrack(int perfectMs = 40, int goodMs = 110, bool ordered = true)
        : perfectMs_(perfectMs), goodMs_(goodMs < perfectMs ? perfectMs : goodMs), ordered_(ordered) {}

    int add(int appearMs, int dueMs);           // returns the point's index
    void clear() { points_.clear(); nowMs_ = 0; missedQueue_.clear(); }
    void update(int dtMs);                      // advances the clock; points past their window become missed
    Tap tap();                                  // a tap now: the nearest open point within the good window
    std::vector<int> takeMissed();              // points that became missed since the last call

    int now() const { return nowMs_; }
    const std::vector<Point>& points() const { return points_; }
    // How far point i has grown: 0 before it appears, 1 at its due time (more after, until it is judged).
    float growth(int i) const;
    bool finished() const;                      // every point judged
    int hits() const;                           // Perfect + Good
    int perfects() const;

private:
    bool open(const Point& p) const { return p.result == Grade::None; }
    int nextOpen() const;
    std::vector<Point> points_;
    std::vector<int> missedQueue_;
    int nowMs_ = 0;
    int perfectMs_, goodMs_;
    bool ordered_;
};

} // namespace toms
