// timed_hits.cpp -- see timed_hits.h.
#include "timed_hits.h"

#include <algorithm>
#include <cstdlib>

namespace toms {

int TimedTrack::add(int appearMs, int dueMs) {
    Point p;
    p.appearMs = std::min(appearMs, dueMs);
    p.dueMs = dueMs;
    points_.push_back(p);
    return (int)points_.size() - 1;
}

int TimedTrack::nextOpen() const {
    for (size_t i = 0; i < points_.size(); ++i) if (open(points_[i])) return (int)i;
    return -1;
}

void TimedTrack::update(int dtMs) {
    nowMs_ += std::max(0, dtMs);
    for (size_t i = 0; i < points_.size(); ++i) {
        Point& p = points_[i];
        if (open(p) && nowMs_ > p.dueMs + goodMs_) {
            p.result = Grade::Miss;
            p.offsetMs = nowMs_ - p.dueMs;
            missedQueue_.push_back((int)i);
        }
    }
}

TimedTrack::Tap TimedTrack::tap() {
    Tap t;
    int best = -1, bestDist = 0;
    if (ordered_) {
        best = nextOpen();
        if (best >= 0) bestDist = std::abs(nowMs_ - points_[best].dueMs);
    } else {
        for (size_t i = 0; i < points_.size(); ++i) {
            if (!open(points_[i]) || nowMs_ < points_[i].appearMs) continue;
            const int d = std::abs(nowMs_ - points_[i].dueMs);
            if (best < 0 || d < bestDist) { best = (int)i; bestDist = d; }
        }
    }
    if (best < 0 || bestDist > goodMs_) {
        // nothing in reach: a wasted tap. An ordered track counts it against its next point (too early / mashing).
        if (ordered_ && best >= 0 && nowMs_ >= points_[best].appearMs) {
            points_[best].result = Grade::Miss;
            points_[best].offsetMs = nowMs_ - points_[best].dueMs;
            t.index = best;
            t.offsetMs = points_[best].offsetMs;
        }
        t.grade = Grade::Miss;
        return t;
    }
    Point& p = points_[best];
    p.offsetMs = nowMs_ - p.dueMs;
    p.result = bestDist <= perfectMs_ ? Grade::Perfect : Grade::Good;
    t.grade = p.result;
    t.index = best;
    t.offsetMs = p.offsetMs;
    return t;
}

std::vector<int> TimedTrack::takeMissed() {
    std::vector<int> out;
    out.swap(missedQueue_);
    return out;
}

float TimedTrack::growth(int i) const {
    if (i < 0 || i >= (int)points_.size()) return 0.0f;
    const Point& p = points_[i];
    if (nowMs_ <= p.appearMs) return 0.0f;
    const int span = std::max(1, p.dueMs - p.appearMs);
    return (float)(nowMs_ - p.appearMs) / (float)span;
}

bool TimedTrack::finished() const { return nextOpen() < 0; }

int TimedTrack::hits() const {
    return (int)std::count_if(points_.begin(), points_.end(),
                              [](const Point& p) { return p.result == Grade::Perfect || p.result == Grade::Good; });
}

int TimedTrack::perfects() const {
    return (int)std::count_if(points_.begin(), points_.end(), [](const Point& p) { return p.result == Grade::Perfect; });
}

} // namespace toms
