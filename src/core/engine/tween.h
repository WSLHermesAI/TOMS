// tween.h -- a value moving from one number to another over time, with easing (docs/gameplay/07_GAMEPLAY_PLAN.md §4, §9).
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
float f_Ease(Ease l_eE, float l_fT);
// "linear", "inQuad", "outBack" ... (as in data files); unknown names give Linear.
Ease f_EaseFromName(const std::string& l_strName);

class Tween {
public:
    enum class Loop { Once, Repeat, PingPong };
    Tween() = default;
    Tween(float l_fFrom, float l_fTo, int l_iDurationMs, Ease l_eE = Ease::Linear, Loop l_eLoop = Loop::Once)
        : m_fFrom(l_fFrom), m_fTo(l_fTo), m_iDurationMs(l_iDurationMs > 0 ? l_iDurationMs : 1), m_eEase(l_eE), m_eLoop(l_eLoop) {}

    void f_Update(int l_iDtMs);
    void f_Restart() { m_iElapsedMs = 0; m_bForward = true; }
    // Speeds the tween up (2 = twice as fast), keeping where it is: a marker that speeds up as power charges.
    void f_SetDuration(int l_iDurationMs);

    float f_Value() const;
    float f_Progress() const;      // 0..1 along the current pass (before easing)
    bool f_Done() const { return m_eLoop == Loop::Once && m_iElapsedMs >= m_iDurationMs; }
    bool f_Forward() const { return m_bForward; }   // PingPong: going from -> to
    int f_DurationMs() const { return m_iDurationMs; }

private:
    float m_fFrom = 0, m_fTo = 1;
    int m_iDurationMs = 1;
    Ease m_eEase = Ease::Linear;
    Loop m_eLoop = Loop::Once;
    int m_iElapsedMs = 0;          // within the current pass
    bool m_bForward = true;
};

} // namespace toms
