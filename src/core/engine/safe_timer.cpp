// safe_timer.cpp -- see safe_timer.h.
#include "safe_timer.h"

#include <algorithm>

namespace toms {

TimerQueue::Id TimerQueue::f_After(const TimerOwner& l_oOwner, int l_iDelayMs, std::function<void()> l_fnFn) {
    const Id l_uId = m_uNextId++;
    m_vTimers.push_back({l_uId, m_i64NowMs + std::max(0, l_iDelayMs), 0, m_u64NextOrder++, l_oOwner.f_Token(), std::move(l_fnFn)});
    return l_uId;
}

TimerQueue::Id TimerQueue::f_Every(const TimerOwner& l_oOwner, int l_iPeriodMs, std::function<void()> l_fnFn) {
    const Id l_uId = m_uNextId++;
    const int l_iP = std::max(1, l_iPeriodMs);
    m_vTimers.push_back({l_uId, m_i64NowMs + l_iP, l_iP, m_u64NextOrder++, l_oOwner.f_Token(), std::move(l_fnFn)});
    return l_uId;
}

void TimerQueue::f_Cancel(Id l_uId) {
    m_vTimers.erase(std::remove_if(m_vTimers.begin(), m_vTimers.end(), [l_uId](const Timer& l_oT) { return l_oT.m_uId == l_uId; }),
                  m_vTimers.end());
}

void TimerQueue::f_Update(int l_iDtMs) {
    m_i64NowMs += std::max(0, l_iDtMs);
    const uint64_t l_u64Cutoff = m_u64NextOrder;   // calls scheduled from inside a call wait for the next update
    for (;;) {
        // the earliest due call that existed when this update began, and whose owner is still there
        m_vTimers.erase(std::remove_if(m_vTimers.begin(), m_vTimers.end(), [](const Timer& l_oT) { return l_oT.m_wpOwner.expired(); }),
                      m_vTimers.end());
        auto l_itIt = m_vTimers.end();
        for (auto l_itI = m_vTimers.begin(); l_itI != m_vTimers.end(); ++l_itI) {
            if (l_itI->m_i64DueMs > m_i64NowMs || l_itI->m_u64Order >= l_u64Cutoff) continue;
            if (l_itIt == m_vTimers.end() || l_itI->m_i64DueMs < l_itIt->m_i64DueMs || (l_itI->m_i64DueMs == l_itIt->m_i64DueMs && l_itI->m_u64Order < l_itIt->m_u64Order)) l_itIt = l_itI;
        }
        if (l_itIt == m_vTimers.end()) return;
        std::function<void()> l_fnFn = l_itIt->m_fnFn;   // a copy: the call may change timers_
        if (l_itIt->m_iPeriodMs > 0) {
            l_itIt->m_i64DueMs += l_itIt->m_iPeriodMs;
            if (l_itIt->m_i64DueMs <= m_i64NowMs) l_itIt->m_i64DueMs = m_i64NowMs + l_itIt->m_iPeriodMs;   // a long frame skips the missed periods
            l_itIt->m_u64Order = m_u64NextOrder++;        // a repeating call runs at most once per update
        } else {
            m_vTimers.erase(l_itIt);
        }
        if (l_fnFn) l_fnFn();
    }
}

size_t TimerQueue::f_Pending() const {
    return (size_t)std::count_if(m_vTimers.begin(), m_vTimers.end(), [](const Timer& l_oT) { return !l_oT.m_wpOwner.expired(); });
}

} // namespace toms
