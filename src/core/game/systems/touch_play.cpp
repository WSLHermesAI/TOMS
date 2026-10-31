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
    if (s == "shape") return PlayKind::Shape;
    if (s == "mash") return PlayKind::Mash;
    return PlayKind::Track;
}

const char* playKindName(PlayKind k) {
    switch (k) {
    case PlayKind::Bow: return "bow";
    case PlayKind::Slice: return "slice";
    case PlayKind::Circles: return "circles";
    case PlayKind::Shape: return "shape";
    case PlayKind::Mash: return "mash";
    default: return "track";
    }
}

void TouchPlay::start(PlayKind kind, int points, bool ultimate, const glm::vec4& area, uint32_t seed,
                      const TouchPlaySettings& s) {
    *this = TouchPlay{};
    kind_ = kind; s_ = s; area_ = area; rng_.seed(seed);
    points_ = std::max(1, points); ultimate_ = ultimate;
    running_ = true;
    started_ = ultimate || kind_ != PlayKind::Bow;   // the bow waits for its first pull; the others run at once
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    if (kind_ == PlayKind::Bow) nextTarget();
    if (kind_ == PlayKind::Slice) {
        for (int k = 0; k < points_; k++) {
            cueKinds_.push_back(k < (int)s_.sliceCues.size() && !s_.sliceCues[(size_t)k].empty() ? s_.sliceCues[(size_t)k] : "way");
            cueDirs_.push_back(std::uniform_int_distribution<int>(0, 7)(rng_));
        }
        cueTrack_.start(points_, s_.sliceCue);
    }
    if (kind_ == PlayKind::Circles) {
        const float m = s_.circleRadius;
        for (int k = 0; k < points_; k++) {   // apart from the others (a few tries; a small area takes what it gets)
            Circle c;
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
    if (kind_ == PlayKind::Shape) {
        trace_.start(s_.shape, {area_.x + area_.z * 0.5f, area_.y + area_.w * 0.5f}, s_.shapeSize, s_.shapeCornerRadius);
        points_ = std::max(1, trace_.stepCount());
    }
}

// ---- bow ----

void TouchPlay::nextTarget() {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    target_.r = s_.bowRadius * (ultimate_ ? 0.8f : 1.0f);
    // the targets keep to the monster's middle; a move's spread over the whole area
    const float spread = ultimate_ ? 1.0f : 0.45f;
    const float cx = area_.x + area_.z * 0.5f, cy = area_.y + area_.w * 0.5f;
    const float hw = std::max(0.0f, area_.z * 0.5f - target_.r), hh = std::max(0.0f, area_.w * 0.5f - target_.r);
    targetTo_ = {cx + (u(rng_) - 0.5f) * 2.0f * hw * spread, cy + (u(rng_) - 0.5f) * 2.0f * hh * spread};
    const float a = u(rng_) * 2.0f * kPi;   // it comes in from a little way off, then stops (the owner: 0.5 s)
    targetFrom_ = targetTo_ + glm::vec2(std::cos(a), std::sin(a)) * s_.bowMoveDist;
    targetFrom_.x = std::clamp(targetFrom_.x, area_.x + target_.r, area_.x + std::max(target_.r, area_.z - target_.r));
    targetFrom_.y = std::clamp(targetFrom_.y, area_.y + target_.r, area_.y + std::max(target_.r, area_.w - target_.r));
    targetAgeMs_ = 0;
    oneMs_ = 0;
    aiming_ = false;
    holdMs_ = 0;
    const glm::vec2 p = targetPos();
    target_.x = p.x; target_.y = p.y;
}

glm::vec2 TouchPlay::targetPos() const {
    const float k = std::clamp((float)targetAgeMs_ / (float)std::max(1, s_.bowMoveMs), 0.0f, 1.0f);
    const float e = 1.0f - (1.0f - k) * (1.0f - k);   // eased out: it slows down to a stop
    return targetFrom_ + (targetTo_ - targetFrom_) * e;
}

float TouchPlay::charge() const {
    return aiming_ ? std::clamp((float)holdMs_ / (float)std::max(1, s_.bowHoldMs), 0.0f, 1.0f) : 0.0f;
}

glm::vec2 TouchPlay::crosshair() const {   // drag back, the crosshair goes the other way (a slingshot), and it shakes
    glm::vec2 c = aimFrom_ - (aimTo_ - aimFrom_) * s_.bowPull;
    const float amp = s_.bowShakePx * (float)holdMs_ / 1000.0f;
    c += glm::vec2(std::sin((float)ageMs_ * 0.021f), std::cos((float)ageMs_ * 0.017f)) * amp;
    return c;
}

void TouchPlay::release() {
    const glm::vec2 d = aimTo_ - aimFrom_;
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    const float ch = charge();
    aiming_ = false;
    if (len < s_.bowMinPull) { holdMs_ = 0; return; }   // not a shot
    const glm::vec2 c = crosshair();
    holdMs_ = 0;
    chargeSum_ += ch;
    shots_++;
    const float dist = std::hypot(c.x - target_.x, c.y - target_.y);
    finishOne(dist <= target_.r, dist <= target_.r * 0.4f, c.x, c.y);
}

// ---- slice ----

glm::vec2 TouchPlay::promptAt() const { return {area_.x + area_.z * 0.5f, area_.y + area_.w - 50.0f}; }

void TouchPlay::judgeStroke() {
    if (stroke_.size() < 2 || !cueTrack_.open()) return;   // only the window takes the answer (a swipe too soon: nothing)
    const int k = cueTrack_.index();
    const glm::vec2 d = stroke_.back() - stroke_.front();
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    bool ok = false, perfect = false;
    if (cueKinds_[(size_t)k] == "way") {
        if (len < s_.sliceMinLength) return;   // not a cut
        const float want = (float)cueDirs_[(size_t)k] * kPi / 4.0f, got = std::atan2(d.y, d.x);
        const float diff = std::fabs(std::fmod(got - want + 3.0f * kPi, 2.0f * kPi) - kPi);
        ok = diff < s_.sliceTolerance;
        perfect = diff < s_.sliceTolerance * 0.45f && len > 2.0f * s_.sliceMinLength;
    } else {
        glm::vec2 lo = stroke_[0], hi = stroke_[0];
        for (glm::vec2 p : stroke_) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
        if (std::max(hi.x - lo.x, hi.y - lo.y) < s_.sliceMinLength) return;   // too small to be a shape
        ok = matchStroke(stroke_, cueKinds_[(size_t)k]);
        perfect = ok && matchStroke(stroke_, cueKinds_[(size_t)k], 0.13f);
    }
    cueTrack_.answer(ok);
    const glm::vec2 at = promptAt();
    finishOne(ok, perfect, at.x, at.y);
}

// ---- the plays ----

int TouchPlay::circlesLit() const {
    if (circlePhase_ != CirclePhase::Count) return (int)circles_.size();
    return std::min((int)circles_.size(), phaseMs_ / std::max(1, s_.circleCountMs) + 1);
}

float TouchPlay::timeLeft() const {
    if (!started_) return 1.0f;
    auto left = [](int used, int all) { return std::clamp(1.0f - (float)used / (float)std::max(1, all), 0.0f, 1.0f); };
    switch (kind_) {
    case PlayKind::Bow: return left(oneMs_, s_.bowTimeMs);
    case PlayKind::Slice: return cueTrack_.timeLeft();
    case PlayKind::Circles: return circlePhase_ == CirclePhase::Tap ? left(phaseMs_, s_.circleTapMs * points_) : 1.0f;
    case PlayKind::Shape: return left(clockMs_, s_.shapeTimeMs);
    case PlayKind::Mash: return left(clockMs_, s_.mashTimeMs);
    default: return 1.0f;
    }
}

void TouchPlay::finishOne(bool ok, bool perfect, float x, float y) {
    marks_.push_back({x, y, ok, 0});
    hits_.push_back({ok, ok && perfect, x, y});
    if (ok) { hitCount_++; if (perfect) perfects_++; }
    lastAnswerMs_ = clockMs_;
    index_++;
    oneMs_ = 0;
    if (index_ >= points_) { done_ = true; return; }
    if (kind_ == PlayKind::Bow) nextTarget();
}

void TouchPlay::pointer(int phase, float x, float y) {
    if (!running()) return;
    const glm::vec2 p(x, y);
    switch (kind_) {
    case PlayKind::Bow:
        if (phase == 0) { aiming_ = true; aimFrom_ = aimTo_ = p; holdMs_ = 0; started_ = true; return; }
        if (!aiming_) return;
        aimTo_ = p;
        if (phase == 2) release();
        return;
    case PlayKind::Slice:
        if (phase == 0) { aiming_ = true; stroke_.assign(1, p); return; }
        if (!aiming_) return;
        if (stroke_.empty() || glm::length(p - stroke_.back()) >= 3.0f) stroke_.push_back(p);
        if (phase == 2) { aiming_ = false; judgeStroke(); stroke_.clear(); }
        return;
    case PlayKind::Circles: {
        if (phase != 0 || circlePhase_ != CirclePhase::Tap) return;   // only once they are back and the "!" has gone
        for (size_t k = 0; k < circles_.size(); k++) {
            Circle& c = circles_[k];
            if (c.state != 0 || std::hypot(x - c.x, y - c.y) > s_.circleRadius * 1.2f) continue;
            if ((int)k == index_) {   // the next in the remembered order: a spell
                const bool perfect = clockMs_ - lastAnswerMs_ <= s_.circlePerfectMs;
                c.state = perfect ? 2 : 1;
                finishOne(true, perfect, c.x, c.y);
            } else {                  // a wrong circle: the one due is missed, the play goes on (Q40)
                Circle& due = circles_[(size_t)index_];
                due.state = 3;
                finishOne(false, false, due.x, due.y);
            }
            return;
        }
        return;
    }
    case PlayKind::Shape: {
        const int before = trace_.steps();
        if (trace_.pointer(phase, x, y) <= 0) return;
        for (int k = before; k < trace_.steps(); k++) {
            const glm::vec2 c = trace_.points()[(size_t)k + 1];
            finishOne(true, clockMs_ - lastAnswerMs_ <= s_.shapeStepPerfectMs, c.x, c.y);
        }
        return;
    }
    case PlayKind::Mash:
        if (phase == 0) finishOne(true, true, x, y);
        return;
    default:
        return;
    }
}

void TouchPlay::update(int dtMs) {
    for (Mark& m : marks_) m.ageMs += dtMs;
    if (!running()) return;
    ageMs_ += dtMs;
    if (kind_ == PlayKind::Bow) {   // the target moves a moment, then stops; it does so before the first pull too
        targetAgeMs_ += dtMs;
        const glm::vec2 p = targetPos();
        target_.x = p.x; target_.y = p.y;
        if (aiming_) {
            holdMs_ += dtMs;
            if (holdMs_ >= s_.bowHoldMs) { holdMs_ = s_.bowHoldMs; release(); }   // the charge limit: the arrow goes (Q41)
        }
    }
    if (!started_ || !running()) return;
    clockMs_ += dtMs;
    oneMs_ += dtMs;
    switch (kind_) {
    case PlayKind::Bow:
        if (oneMs_ > s_.bowTimeMs) { aiming_ = false; holdMs_ = 0; finishOne(false, false, target_.x, target_.y); }
        break;
    case PlayKind::Slice: {
        const int missed = cueTrack_.update(dtMs);
        for (int k = 0; k < missed; k++) {   // a prompt let pass: a miss (finishOne counts it; the track has moved on)
            const glm::vec2 at = promptAt();
            finishOne(false, false, at.x, at.y);
        }
        break;
    }
    case PlayKind::Circles: {
        phaseMs_ += dtMs;
        const int n = (int)circles_.size();
        if (circlePhase_ == CirclePhase::Count && phaseMs_ >= n * s_.circleCountMs) { circlePhase_ = CirclePhase::Hide; phaseMs_ = 0; }
        if (circlePhase_ == CirclePhase::Hide && phaseMs_ >= s_.circleHideMs) { circlePhase_ = CirclePhase::Warn; phaseMs_ = 0; }
        if (circlePhase_ == CirclePhase::Warn && phaseMs_ >= s_.circleWarnMs) {
            circlePhase_ = CirclePhase::Tap; phaseMs_ = 0; lastAnswerMs_ = clockMs_;
        }
        if (circlePhase_ == CirclePhase::Tap && phaseMs_ >= s_.circleTapMs * n)   // time up: the rest are missed
            while (!done_) { circles_[(size_t)index_].state = 3; finishOne(false, false, circles_[(size_t)index_].x, circles_[(size_t)index_].y); }
        break;
    }
    case PlayKind::Shape:
    case PlayKind::Mash:
        if (clockMs_ >= (kind_ == PlayKind::Shape ? s_.shapeTimeMs : s_.mashTimeMs)) {   // time up: what is done counts
            failed_ = hitCount_ == 0;
            index_ = points_;
            done_ = true;
        }
        break;
    default:
        break;
    }
}

TouchPlaySettings touchPlayFromJson(const nlohmann::json& b) {
    TouchPlaySettings s;
    if (!b.is_object() || !b.contains("plays") || !b["plays"].is_object()) return s;
    const nlohmann::json& j = b["plays"];
    s.bowTimeMs = std::max(500, j.value("bowTimeMs", s.bowTimeMs));
    s.bowRadius = std::max(8.0f, j.value("bowRadius", s.bowRadius));
    s.bowPull = std::max(0.1f, j.value("bowPull", s.bowPull));
    s.bowMoveMs = std::max(0, j.value("bowMoveMs", s.bowMoveMs));
    s.bowMoveDist = std::max(0.0f, j.value("bowMoveDist", s.bowMoveDist));
    s.bowHoldMs = std::max(200, j.value("bowHoldMs", s.bowHoldMs));
    s.bowShakePx = std::max(0.0f, j.value("bowShakePx", s.bowShakePx));
    s.bowHoldPerStr = std::max(0, j.value("bowHoldPerStr", s.bowHoldPerStr));
    s.bowShakePerDex = std::max(0.0f, j.value("bowShakePerDex", s.bowShakePerDex));
    s.sliceCue.cueMs = std::max(200, j.value("cueMs", s.sliceCue.cueMs));
    s.sliceCue.inputMs = std::clamp(j.value("inputMs", s.sliceCue.inputMs), 50, s.sliceCue.cueMs);
    s.sliceCue.windowAtEnd = j.value("inputAtEnd", s.sliceCue.windowAtEnd);
    s.sliceCue.gapMs = std::max(0, j.value("cueGapMs", s.sliceCue.gapMs));
    s.circleCountMs = std::max(100, j.value("circleCountMs", s.circleCountMs));
    s.circleHideMs = std::max(0, j.value("circleHideMs", s.circleHideMs));
    s.circleWarnMs = std::max(0, j.value("circleWarnMs", s.circleWarnMs));
    s.circleTapMs = std::max(200, j.value("circleTapMs", s.circleTapMs));
    s.circlePerfectMs = std::max(0, j.value("circlePerfectMs", s.circlePerfectMs));
    return s;
}

ComboResult TouchPlay::result() const {
    ComboResult r;
    r.points = points_;
    r.hits = hitCount_;
    r.perfects = perfects_;
    r.failed = failed_ || hitCount_ == 0;
    return r;
}

} // namespace toms
