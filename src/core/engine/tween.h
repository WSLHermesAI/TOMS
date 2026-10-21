// tween.h -- a value moving from one number to another over time, with easing (docs/30_GAMEPLAY_PLAN.md §4, §9).
//
// The battle's markers ("use tween to move the lighting indicator"), floating damage numbers, growing hit lines:
// anything that moves over a known time. Once, looping, or back and forth (a marker sweeping a bar).
//
//   toms::Tween marker(0, 100, 800, toms::Ease::InOutQuad, toms::Tween::Loop::PingPong);
//   marker.update(dtMs);  float x = marker.value();
//
// Plain C++, no engine dependencies: other games can reuse it.
#pragma once

#include <string>

namespace toms {

enum class Ease { Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, OutBack, OutBounce };

// t in [0, 1] -> eased [0, 1] (OutBack overshoots a little past 1 on the way).
float ease(Ease e, float t);
// "linear", "inQuad", "outBack" ... (as in data files); unknown names give Linear.
Ease easeFromName(const std::string& name);

class Tween {
public:
    enum class Loop { Once, Repeat, PingPong };
    Tween() = default;
    Tween(float from, float to, int durationMs, Ease e = Ease::Linear, Loop loop = Loop::Once)
        : from_(from), to_(to), durationMs_(durationMs > 0 ? durationMs : 1), ease_(e), loop_(loop) {}

    void update(int dtMs);
    void restart() { elapsedMs_ = 0; forward_ = true; }
    // Speeds the tween up (2 = twice as fast), keeping where it is: a marker that speeds up as power charges.
    void setDuration(int durationMs);

    float value() const;
    float progress() const;      // 0..1 along the current pass (before easing)
    bool done() const { return loop_ == Loop::Once && elapsedMs_ >= durationMs_; }
    bool forward() const { return forward_; }   // PingPong: going from -> to
    int durationMs() const { return durationMs_; }

private:
    float from_ = 0, to_ = 1;
    int durationMs_ = 1;
    Ease ease_ = Ease::Linear;
    Loop loop_ = Loop::Once;
    int elapsedMs_ = 0;          // within the current pass
    bool forward_ = true;
};

} // namespace toms
