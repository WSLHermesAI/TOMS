// combo_play.cpp -- see combo_play.h.
#include "combo_play.h"

#include <algorithm>
#include <cmath>

namespace toms {

ComboSettings comboSettings(const WeaponPlay& w, int weaponLevel) {
    ComboSettings s;
    s.points = std::clamp(weaponLevelEntry(w, weaponLevel).points, 1, kMaxComboPoints);
    s.turnBack = w.turnBack;
    s.sweepMs = w.sweepMs;
    s.maxSweeps = w.maxSweeps;
    s.speedUp = w.chargeSpeedUp;
    s.hitWindow = w.hitWindow;
    s.missBack = missBackPoints(w, weaponLevel);
    s.missWaitMs = w.missWaitMs;
    return s;
}

void ComboPlay::start(const ComboSettings& s, float charge) {
    s_ = s;
    s_.points = std::clamp(s_.points, 1, kMaxComboPoints);
    charge_ = std::clamp(charge, 0.0f, 1.0f);
    pos_ = 0;
    dir_ = 1;
    sweeps_ = 0;
    waitMs_ = 0;
    missesInRow_ = 0;
    speed_ = (1.0f + s_.speedUp * charge_) / float(std::max(1, s_.sweepMs));
    at_.clear();
    hit_.assign((size_t)s_.points, false);
    for (int i = 0; i < s_.points; i++) at_.push_back(float(i + 1) / float(s_.points + 1));   // evenly, off the ends
    order_.clear();
    result_ = ComboResult();
    running_ = true;
    done_ = false;
}

int ComboPlay::hits() const { return (int)std::count(hit_.begin(), hit_.end(), true); }

void ComboPlay::finish(bool failed) {
    running_ = false;
    done_ = true;
    result_.hits = failed ? 0 : hits();
    result_.points = s_.points;
    result_.charge = charge_;
    result_.failed = failed;
}

void ComboPlay::update(int dtMs) {
    if (!running_ || dtMs <= 0) return;
    waitMs_ = std::max(0, waitMs_ - dtMs);
    pos_ += float(dir_) * speed_ * float(dtMs);
    if (pos_ >= 1.0f || pos_ <= 0.0f) {   // an end: one sweep done
        pos_ = std::clamp(pos_, 0.0f, 1.0f);
        sweeps_++;
        if (!s_.turnBack || sweeps_ >= s_.maxSweeps) {
            finish(false);
            return;
        }
        dir_ = -dir_;
    }
}

ComboPlay::Tap ComboPlay::tap() {
    if (!running_ || waitMs_ > 0) return Tap::Ignored;
    // The nearest point not hit yet, within the window.
    int best = -1;
    float bestD = s_.hitWindow;
    for (size_t i = 0; i < at_.size(); i++) {
        if (hit_[i]) continue;
        const float d = std::fabs(at_[i] - pos_);
        if (d <= bestD) {
            bestD = d;
            best = (int)i;
        }
    }
    if (best >= 0) {
        hit_[(size_t)best] = true;
        order_.push_back(best);
        missesInRow_ = 0;
        if (hits() == s_.points) finish(false);
        return Tap::Hit;
    }
    missesInRow_++;
    if (missesInRow_ >= 2) {   // two misses in a row: a fail
        finish(true);
        return Tap::Miss;
    }
    // The combo goes back: the newest hits are lost (all of them for "to the start").
    int back = s_.missBack < 0 ? (int)order_.size() : std::min(s_.missBack, (int)order_.size());
    while (back-- > 0) {
        hit_[(size_t)order_.back()] = false;
        order_.pop_back();
    }
    waitMs_ = s_.missWaitMs;
    return Tap::Miss;
}

} // namespace toms
