// safe_timer.cpp -- see safe_timer.h.
#include "safe_timer.h"

#include <algorithm>

namespace toms {

TimerQueue::Id TimerQueue::after(const TimerOwner& owner, int delayMs, std::function<void()> fn) {
    const Id id = nextId_++;
    timers_.push_back({id, nowMs_ + std::max(0, delayMs), 0, nextOrder_++, owner.token(), std::move(fn)});
    return id;
}

TimerQueue::Id TimerQueue::every(const TimerOwner& owner, int periodMs, std::function<void()> fn) {
    const Id id = nextId_++;
    const int p = std::max(1, periodMs);
    timers_.push_back({id, nowMs_ + p, p, nextOrder_++, owner.token(), std::move(fn)});
    return id;
}

void TimerQueue::cancel(Id id) {
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; }),
                  timers_.end());
}

void TimerQueue::update(int dtMs) {
    nowMs_ += std::max(0, dtMs);
    const uint64_t cutoff = nextOrder_;   // calls scheduled from inside a call wait for the next update
    for (;;) {
        // the earliest due call that existed when this update began, and whose owner is still there
        timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const Timer& t) { return t.owner.expired(); }),
                      timers_.end());
        auto it = timers_.end();
        for (auto i = timers_.begin(); i != timers_.end(); ++i) {
            if (i->dueMs > nowMs_ || i->order >= cutoff) continue;
            if (it == timers_.end() || i->dueMs < it->dueMs || (i->dueMs == it->dueMs && i->order < it->order)) it = i;
        }
        if (it == timers_.end()) return;
        std::function<void()> fn = it->fn;   // a copy: the call may change timers_
        if (it->periodMs > 0) {
            it->dueMs += it->periodMs;
            if (it->dueMs <= nowMs_) it->dueMs = nowMs_ + it->periodMs;   // a long frame skips the missed periods
            it->order = nextOrder_++;        // a repeating call runs at most once per update
        } else {
            timers_.erase(it);
        }
        if (fn) fn();
    }
}

size_t TimerQueue::pending() const {
    return (size_t)std::count_if(timers_.begin(), timers_.end(), [](const Timer& t) { return !t.owner.expired(); });
}

} // namespace toms
