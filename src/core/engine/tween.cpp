// tween.cpp -- see tween.h.
#include "tween.h"

#include <algorithm>
#include <cmath>

namespace toms {

float ease(Ease e, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (e) {
        case Ease::Linear:     return t;
        case Ease::InQuad:     return t * t;
        case Ease::OutQuad:    return t * (2.0f - t);
        case Ease::InOutQuad:  return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
        case Ease::InCubic:    return t * t * t;
        case Ease::OutCubic:   return 1.0f - std::pow(1.0f - t, 3.0f);
        case Ease::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
        case Ease::OutBack: {
            const float c1 = 1.70158f, c3 = c1 + 1.0f;
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        }
        case Ease::OutBounce: {
            const float n = 7.5625f, d = 2.75f;
            if (t < 1.0f / d) return n * t * t;
            if (t < 2.0f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
            if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
            t -= 2.625f / d;
            return n * t * t + 0.984375f;
        }
    }
    return t;
}

Ease easeFromName(const std::string& n) {
    static const struct { const char* name; Ease e; } table[] = {
        {"linear", Ease::Linear}, {"inQuad", Ease::InQuad}, {"outQuad", Ease::OutQuad}, {"inOutQuad", Ease::InOutQuad},
        {"inCubic", Ease::InCubic}, {"outCubic", Ease::OutCubic}, {"inOutCubic", Ease::InOutCubic},
        {"outBack", Ease::OutBack}, {"outBounce", Ease::OutBounce},
    };
    for (const auto& r : table) if (n == r.name) return r.e;
    return Ease::Linear;
}

void Tween::update(int dtMs) {
    if (dtMs <= 0) return;
    elapsedMs_ += dtMs;
    if (elapsedMs_ < durationMs_) return;
    switch (loop_) {
        case Loop::Once:     elapsedMs_ = durationMs_; break;
        case Loop::Repeat:   elapsedMs_ %= durationMs_; break;
        case Loop::PingPong: {
            const int passes = elapsedMs_ / durationMs_;   // a long frame can cross more than one end
            elapsedMs_ %= durationMs_;
            if (passes % 2 == 1) forward_ = !forward_;
            break;
        }
    }
}

void Tween::setDuration(int durationMs) {
    durationMs = std::max(1, durationMs);
    const float p = progress();
    durationMs_ = durationMs;
    elapsedMs_ = (int)std::lround(p * durationMs_);
}

float Tween::progress() const { return std::clamp((float)elapsedMs_ / (float)durationMs_, 0.0f, 1.0f); }

float Tween::value() const {
    const float p = progress();
    const float k = ease(ease_, forward_ ? p : 1.0f - p);
    return from_ + (to_ - from_) * k;
}

} // namespace toms
