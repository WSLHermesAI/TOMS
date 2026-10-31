// touch_play.cpp -- see touch_play.h.
#include "touch_play.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {
constexpr float kPi = 3.14159265f;
}

PlayKind playKindFromString(const std::string& s) {
    if (s == "bow") return PlayKind::Bow;
    if (s == "slice") return PlayKind::Slice;
    if (s == "circles") return PlayKind::Circles;
    return PlayKind::Track;
}

const char* playKindName(PlayKind k) {
    switch (k) {
    case PlayKind::Bow: return "bow";
    case PlayKind::Slice: return "slice";
    case PlayKind::Circles: return "circles";
    default: return "track";
    }
}

void TouchPlay::start(PlayKind kind, int points, bool ultimate, const glm::vec4& area, uint32_t seed,
                      const TouchPlaySettings& s) {
    *this = TouchPlay{};
    kind_ = kind; s_ = s; area_ = area; rng_.seed(seed);
    points_ = std::max(1, points); ultimate_ = ultimate;
    running_ = true;
    started_ = ultimate;   // the ultimate was chosen: it runs at once
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    if (kind_ == PlayKind::Bow) nextTarget();
    if (kind_ == PlayKind::Slice)
        for (int k = 0; k < points_; k++) arrows_.push_back({std::uniform_int_distribution<int>(0, 7)(rng_), 0});
    if (kind_ == PlayKind::Circles) {
        const int gap = ultimate_ ? (int)std::lround(s_.circleGapMs * 0.7f) : s_.circleGapMs;
        const float m = s_.circleRadius;
        for (int k = 0; k < points_; k++) {   // apart from the one before it (a few tries; a small area takes what it gets)
            Circle c{0, 0, s_.circleFirstMs + k * gap, 0};
            for (int tryNo = 0; tryNo < 12; tryNo++) {
                c.x = area_.x + m + u(rng_) * std::max(0.0f, area_.z - 2 * m);
                c.y = area_.y + m + u(rng_) * std::max(0.0f, area_.w - 2 * m);
                bool clear = true;
                for (const Circle& o : circles_) clear = clear && std::hypot(c.x - o.x, c.y - o.y) >= 2.6f * m;
                if (clear) break;
            }
            circles_.push_back(c);
        }
    }
}

void TouchPlay::nextTarget() {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    target_.r = s_.bowRadius * (ultimate_ ? 0.8f : 1.0f);
    // the targets keep to the monster's middle; the ultimate's spread over the whole area
    const float spread = ultimate_ ? 1.0f : 0.45f;
    const float cx = area_.x + area_.z * 0.5f, cy = area_.y + area_.w * 0.5f;
    target_.x = cx + (u(rng_) - 0.5f) * std::max(0.0f, area_.z - 2 * target_.r) * spread;
    target_.y = cy + (u(rng_) - 0.5f) * std::max(0.0f, area_.w - 2 * target_.r) * spread;
    oneMs_ = 0;
    aiming_ = false;
}

glm::vec2 TouchPlay::crosshair() const {   // drag back, the crosshair goes the other way (a slingshot)
    return aimFrom_ - (aimTo_ - aimFrom_) * s_.bowPull;
}

bool TouchPlay::arrowsShown() const {
    return !(ultimate_ && kind_ == PlayKind::Slice) || clockMs_ < s_.sliceShowMs;
}

float TouchPlay::timeLeft() const {
    if (!started_) return 1.0f;
    const int limit = kind_ == PlayKind::Bow ? s_.bowTimeMs
                    : kind_ == PlayKind::Slice ? (int)std::lround(s_.sliceTimeMs * (ultimate_ ? 1.4f : 1.0f)) : 1;
    return std::clamp(1.0f - (float)oneMs_ / (float)std::max(1, limit), 0.0f, 1.0f);
}

void TouchPlay::finishOne(bool ok, bool perfect, float x, float y) {
    marks_.push_back({x, y, ok, 0});
    if (ok) { hits_++; if (perfect) perfects_++; }
    index_++;
    oneMs_ = 0;
    if (index_ >= points_) { done_ = true; return; }
    if (kind_ == PlayKind::Bow) nextTarget();
}

void TouchPlay::pointer(int phase, float x, float y) {
    if (!running()) return;
    const glm::vec2 p(x, y);
    if (kind_ == PlayKind::Bow || kind_ == PlayKind::Slice) {
        if (phase == 0) { aiming_ = true; aimFrom_ = aimTo_ = p; started_ = true; return; }
        if (!aiming_) return;
        if (phase == 1) { aimTo_ = p; return; }
        aimTo_ = p;   // up: let go
        aiming_ = false;
        const glm::vec2 d = aimTo_ - aimFrom_;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (kind_ == PlayKind::Bow) {
            if (len < s_.bowMinPull) return;   // not a shot
            const glm::vec2 c = crosshair();
            const float dist = std::hypot(c.x - target_.x, c.y - target_.y);
            finishOne(dist <= target_.r, dist <= target_.r * 0.4f, c.x, c.y);
        } else {
            if (len < s_.sliceMinLength) return;   // not a cut
            const float want = arrows_[(size_t)index_].dir * kPi / 4.0f, got = std::atan2(d.y, d.x);
            const float diff = std::fabs(std::fmod(got - want + 3.0f * kPi, 2.0f * kPi) - kPi);
            const bool ok = diff < s_.sliceTolerance, perfect = diff < s_.sliceTolerance * 0.45f && len > 2.0f * s_.sliceMinLength;
            arrows_[(size_t)index_].state = ok ? 1 : 2;
            const glm::vec2 mid = (aimFrom_ + aimTo_) * 0.5f;
            finishOne(ok, perfect, mid.x, mid.y);
            if (!ok && ultimate_) {   // the memory chain: a wrong way ends it
                failed_ = hits_ == 0;
                for (size_t k = (size_t)index_; k < arrows_.size(); k++) arrows_[k].state = 2;
                index_ = points_;
                done_ = true;
            }
        }
        return;
    }
    if (kind_ == PlayKind::Circles && phase == 0) {
        if (!started_) { started_ = true; clockMs_ = 0; return; }   // the first tap starts the beat
        for (Circle& c : circles_) {
            if (c.state != 0 || std::hypot(x - c.x, y - c.y) > s_.circleRadius * 1.2f) continue;
            const int off = std::abs(clockMs_ - c.atMs);
            if (off > s_.circleWindowMs * 3) return;   // far too early: ignored (it is not its time yet)
            const bool ok = off <= s_.circleWindowMs, perfect = off <= s_.circlePerfectMs;
            c.state = ok ? (perfect ? 2 : 1) : 3;
            finishOne(ok, perfect, c.x, c.y);
            return;
        }
    }
}

void TouchPlay::update(int dtMs) {
    for (Mark& m : marks_) m.ageMs += dtMs;
    if (!running() || !started_) return;
    clockMs_ += dtMs;
    oneMs_ += dtMs;
    if (kind_ == PlayKind::Bow && oneMs_ > s_.bowTimeMs) { aiming_ = false; finishOne(false, false, target_.x, target_.y); }
    if (kind_ == PlayKind::Slice && oneMs_ > (int)std::lround(s_.sliceTimeMs * (ultimate_ ? 1.4f : 1.0f))) {
        aiming_ = false;
        arrows_[(size_t)index_].state = 2;
        finishOne(false, false, area_.x + area_.z * 0.5f, area_.y + area_.w * 0.5f);
    }
    if (kind_ == PlayKind::Circles)
        for (Circle& c : circles_)
            if (c.state == 0 && clockMs_ > c.atMs + s_.circleWindowMs) { c.state = 3; finishOne(false, false, c.x, c.y); }
}

ComboResult TouchPlay::result() const {
    ComboResult r;
    r.points = points_;
    r.hits = hits_;
    r.failed = failed_ || hits_ == 0;
    return r;
}

} // namespace toms
