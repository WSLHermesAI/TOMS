// safe_timer.h -- delayed calls that never run after their owner is gone (docs/gameplay/07_GAMEPLAY_PLAN.md §9).
//
// A battle schedules things for later: show the damage number when the swing lands, end the attack's pause when its
// animation is over. If the battle (or whatever scheduled the call) is destroyed or restarted first, the call must
// not run against freed or stale state. Each call is tied to a TimerOwner; the queue keeps only a weak link to it, so
// when the owner is destroyed (or reset()), its pending calls are dropped silently.
//
//   toms::TimerQueue timers;            // one per scene / system; update() it every frame
//   toms::TimerOwner owner;             // a member of the object the calls belong to
//   timers.after(owner, 250, [this] { showNumber(); });
//   owner.reset();                      // e.g. the battle restarts: its pending calls are cancelled
//
// Plain C++, no engine dependencies: other games can reuse it.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace toms {

// The thing a delayed call belongs to. Destroying it, or reset(), cancels every call scheduled with it.
class TimerOwner {
public:
    TimerOwner() : m_spToken(std::make_shared<char>(0)) {}
    TimerOwner(const TimerOwner&) : m_spToken(std::make_shared<char>(0)) {}   // a copy owns nothing of the original's
    TimerOwner& operator=(const TimerOwner&) { f_Reset(); return *this; }
    void f_Reset() { m_spToken = std::make_shared<char>(0); }   // cancels everything scheduled so far
    std::weak_ptr<char> f_Token() const { return m_spToken; }
private:
    std::shared_ptr<char> m_spToken;
};

class TimerQueue {
public:
    using Id = uint32_t;
    // Calls fn once, delayMs from now (0: on the next update). Returns an id for cancel().
    Id f_After(const TimerOwner& l_oOwner, int l_iDelayMs, std::function<void()> l_fnFn);
    // Calls fn every periodMs until cancelled or the owner is gone (the first call periodMs from now).
    Id f_Every(const TimerOwner& l_oOwner, int l_iPeriodMs, std::function<void()> l_fnFn);
    void f_Cancel(Id l_uId);
    void f_Clear() { m_vTimers.clear(); }
    // Advances the clock and runs what is due, in time order (equal times: the order they were scheduled).
    // A call may schedule or cancel others; one scheduled now for 0 ms runs on the next update, not this one.
    void f_Update(int l_iDtMs);
    size_t f_Pending() const;   // live calls still waiting (a gone owner's are not counted)
    int64_t f_Now() const { return m_i64NowMs; }

private:
    struct Timer {
        Id m_uId;
        int64_t m_i64DueMs;
        int m_iPeriodMs;           // 0: once
        uint64_t m_u64Order;         // scheduling order, for ties
        std::weak_ptr<char> m_wpOwner;
        std::function<void()> m_fnFn;
    };
    std::vector<Timer> m_vTimers;
    int64_t m_i64NowMs = 0;
    Id m_uNextId = 1;
    uint64_t m_u64NextOrder = 0;
};

} // namespace toms
