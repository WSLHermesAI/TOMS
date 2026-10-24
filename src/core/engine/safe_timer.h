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
    TimerOwner() : token_(std::make_shared<char>(0)) {}
    TimerOwner(const TimerOwner&) : token_(std::make_shared<char>(0)) {}   // a copy owns nothing of the original's
    TimerOwner& operator=(const TimerOwner&) { reset(); return *this; }
    void reset() { token_ = std::make_shared<char>(0); }   // cancels everything scheduled so far
    std::weak_ptr<char> token() const { return token_; }
private:
    std::shared_ptr<char> token_;
};

class TimerQueue {
public:
    using Id = uint32_t;
    // Calls fn once, delayMs from now (0: on the next update). Returns an id for cancel().
    Id after(const TimerOwner& owner, int delayMs, std::function<void()> fn);
    // Calls fn every periodMs until cancelled or the owner is gone (the first call periodMs from now).
    Id every(const TimerOwner& owner, int periodMs, std::function<void()> fn);
    void cancel(Id id);
    void clear() { timers_.clear(); }
    // Advances the clock and runs what is due, in time order (equal times: the order they were scheduled).
    // A call may schedule or cancel others; one scheduled now for 0 ms runs on the next update, not this one.
    void update(int dtMs);
    size_t pending() const;   // live calls still waiting (a gone owner's are not counted)
    int64_t now() const { return nowMs_; }

private:
    struct Timer {
        Id id;
        int64_t dueMs;
        int periodMs;           // 0: once
        uint64_t order;         // scheduling order, for ties
        std::weak_ptr<char> owner;
        std::function<void()> fn;
    };
    std::vector<Timer> timers_;
    int64_t nowMs_ = 0;
    Id nextId_ = 1;
    uint64_t nextOrder_ = 0;
};

} // namespace toms
