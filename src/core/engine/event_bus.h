// event_bus.h — minimal, type-safe pub/sub used to decouple gameplay systems.
// See docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §13 #10 and §8.4 (Mission System progress
// ticks). Deliberately small: no priorities, no async dispatch — publish() calls every subscriber for that exact
// event type synchronously, in subscription order.
//
// Two ways to subscribe:
//   subscribe<E>(fn)        for the life of the bus (tests, process-wide listeners).
//   listen<E>(fn)           returns a Subscription; the handler is removed when the Subscription is destroyed.
//                           Use it whenever the handler captures an object that can go away before the bus:
//                           the global bus outlives every Game, and the editor makes a new Game on every restart
//                           and scenario, so a Game's handlers must leave with it (2026-10-21: a handler of a
//                           destroyed Game crashed when the next Game's battle published EnemyDefeated).
// A handler may be removed while an event is being delivered (also its own): it is not called again, and the list is
// tidied when the delivery is over.
#pragma once
#include <any>
#include <cstdint>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace toms {

class EventBus;

// Removes its handler from the bus when destroyed (or reset()). Move-only.
class Subscription {
public:
    Subscription() = default;
    Subscription(EventBus* l_pBus, uint64_t l_u64Id) : m_pBus(l_pBus), m_u64Id(l_u64Id) {}
    Subscription(Subscription&& l_oO) noexcept : m_pBus(l_oO.m_pBus), m_u64Id(l_oO.m_u64Id) { l_oO.m_pBus = nullptr; l_oO.m_u64Id = 0; }
    Subscription& operator=(Subscription&& l_oO) noexcept {
        if (this != &l_oO) { f_Reset(); m_pBus = l_oO.m_pBus; m_u64Id = l_oO.m_u64Id; l_oO.m_pBus = nullptr; l_oO.m_u64Id = 0; }
        return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { f_Reset(); }
    inline void f_Reset();
    bool f_Active() const { return m_pBus != nullptr; }
private:
    EventBus* m_pBus = nullptr;
    uint64_t m_u64Id = 0;
};

class EventBus {
public:
    template <typename Event>
    void f_Subscribe(std::function<void(const Event&)> l_fnHandler) {
        f_Add<Event>(std::move(l_fnHandler));
    }

    template <typename Event>
    [[nodiscard]] Subscription f_Listen(std::function<void(const Event&)> l_fnHandler) {
        return Subscription(this, f_Add<Event>(std::move(l_fnHandler)));
    }

    template <typename Event>
    void f_Publish(const Event& l_oEvt) {
        auto l_itIt = m_mapHandlers.find(std::type_index(typeid(Event)));
        if (l_itIt == m_mapHandlers.end()) return;
        std::any l_oBoxed = l_oEvt;
        ++m_iPublishing;
        // by index: a handler may subscribe more (they wait for the next event) or remove some (skipped from now on)
        const size_t l_nN = l_itIt->second.size();
        for (size_t l_nI = 0; l_nI < l_nN; ++l_nI) {
            Entry& l_oE = l_itIt->second[l_nI];
            if (l_oE.m_fnFn) { auto l_fnFn = l_oE.m_fnFn; l_fnFn(l_oBoxed); }   // a copy: the handler may remove itself
        }
        if (--m_iPublishing == 0 && m_bDirty) f_Compact();
    }

    template <typename Event>
    size_t f_SubscriberCount() const {
        auto l_itIt = m_mapHandlers.find(std::type_index(typeid(Event)));
        if (l_itIt == m_mapHandlers.end()) return 0;
        size_t l_nN = 0;
        for (const Entry& l_oE : l_itIt->second) if (l_oE.m_fnFn) ++l_nN;
        return l_nN;
    }

    void f_Unsubscribe(uint64_t l_u64Id) {
        for (auto& [l_oType, l_vList] : m_mapHandlers)
            for (Entry& l_oE : l_vList)
                if (l_oE.m_u64Id == l_u64Id && l_oE.m_fnFn) {
                    l_oE.m_fnFn = nullptr;   // removed now; erased once no delivery is running
                    m_bDirty = true;
                    if (m_iPublishing == 0) f_Compact();
                    return;
                }
    }

private:
    struct Entry { uint64_t m_u64Id; std::function<void(const std::any&)> m_fnFn; };

    template <typename Event>
    uint64_t f_Add(std::function<void(const Event&)> l_fnHandler) {
        const uint64_t l_u64Id = m_u64NextId++;
        m_mapHandlers[std::type_index(typeid(Event))].push_back({l_u64Id, [handler = std::move(l_fnHandler)](const std::any& l_oE) {
            handler(std::any_cast<const Event&>(l_oE));
        }});
        return l_u64Id;
    }
    void f_Compact() {
        for (auto& [l_oType, l_vList] : m_mapHandlers) {
            std::vector<Entry> l_vKept;
            l_vKept.reserve(l_vList.size());
            for (Entry& l_oE : l_vList) if (l_oE.m_fnFn) l_vKept.push_back(std::move(l_oE));
            l_vList.swap(l_vKept);
        }
        m_bDirty = false;
    }

    std::unordered_map<std::type_index, std::vector<Entry>> m_mapHandlers;
    uint64_t m_u64NextId = 1;
    int m_iPublishing = 0;
    bool m_bDirty = false;
};

inline void Subscription::f_Reset() {
    if (m_pBus) m_pBus->f_Unsubscribe(m_u64Id);
    m_pBus = nullptr;
    m_u64Id = 0;
}

// Process-wide bus for real gameplay code (mirrors TextureManager's singleton convention).
inline EventBus& f_GlobalEventBus() {
    static EventBus l_oBus;
    return l_oBus;
}

// --- Concrete event payloads. Grows as new systems need them (architecture doc §8.4). ---
struct EnemyDefeated { std::string m_strEnemyId; std::string m_strStageId; };
struct ItemCollected { std::string m_strItemId; std::string m_strStageId; };
// Fired whenever a dialogue choice is confirmed (Milestone 4) — npcId identifies the
// conversation, choiceLabel identifies which choice (dialogue choices have no stable id in the
// current data/dialogue/*.json schema, so the label is the closest thing to one).
struct ChoiceMade { std::string m_strNpcId; std::string m_strChoiceLabel; };

} // namespace toms
