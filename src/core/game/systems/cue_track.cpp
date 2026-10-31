// cue_track.cpp -- see cue_track.h.
#include "cue_track.h"

#include <algorithm>

namespace toms {

void CueTrack::start(int count, const CueTiming& t) {
    *this = CueTrack{};
    t_ = t;
    t_.cueMs = std::max(1, t_.cueMs);
    t_.inputMs = std::clamp(t_.inputMs, 1, t_.cueMs);
    states_.assign((size_t)std::max(0, count), Waiting);
}

bool CueTrack::open() const {
    return !done() && gapLeft_ <= 0 && ageMs_ >= windowFrom() && ageMs_ < windowTo();
}

bool CueTrack::warning() const {
    return t_.warnMs > 0 && !done() && gapLeft_ <= 0 && ageMs_ >= windowFrom() - t_.warnMs && ageMs_ < windowTo();
}

float CueTrack::timeLeft() const {
    if (done() || gapLeft_ > 0) return 0.0f;
    return std::clamp(1.0f - (float)ageMs_ / (float)std::max(1, windowTo()), 0.0f, 1.0f);
}

int CueTrack::update(int dtMs) {
    int missed = 0;
    while (dtMs > 0 && !done()) {
        if (gapLeft_ > 0) {   // between an answer and the next cue
            const int d = std::min(dtMs, gapLeft_);
            gapLeft_ -= d; dtMs -= d;
            continue;
        }
        const int end = windowTo();   // the cue ends when its window closes
        const int d = std::min(dtMs, end - ageMs_);
        ageMs_ += d; dtMs -= d;
        if (ageMs_ >= end) {   // its window closed unanswered: missed, the next one at once
            states_[(size_t)index_] = Missed;
            missed++;
            index_++;
            ageMs_ = 0;
        }
    }
    return missed;
}

bool CueTrack::answer(bool ok) {
    if (!open()) return false;
    states_[(size_t)index_] = ok ? Answered : Missed;
    index_++;
    ageMs_ = 0;
    gapLeft_ = done() ? 0 : t_.gapMs;
    return true;
}

} // namespace toms
