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
    Subscription(EventBus* bus, uint64_t id) : bus_(bus), id_(id) {}
    Subscription(Subscription&& o) noexcept : bus_(o.bus_), id_(o.id_) { o.bus_ = nullptr; o.id_ = 0; }
    Subscription& operator=(Subscription&& o) noexcept {
        if (this != &o) { reset(); bus_ = o.bus_; id_ = o.id_; o.bus_ = nullptr; o.id_ = 0; }
        return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { reset(); }
    inline void reset();
    bool active() const { return bus_ != nullptr; }
private:
    EventBus* bus_ = nullptr;
    uint64_t id_ = 0;
};

class EventBus {
public:
    template <typename Event>
    void subscribe(std::function<void(const Event&)> handler) {
        add<Event>(std::move(handler));
    }

    template <typename Event>
    [[nodiscard]] Subscription listen(std::function<void(const Event&)> handler) {
        return Subscription(this, add<Event>(std::move(handler)));
    }

    template <typename Event>
    void publish(const Event& evt) {
        auto it = handlers_.find(std::type_index(typeid(Event)));
        if (it == handlers_.end()) return;
        std::any boxed = evt;
        ++publishing_;
        // by index: a handler may subscribe more (they wait for the next event) or remove some (skipped from now on)
        const size_t n = it->second.size();
        for (size_t i = 0; i < n; ++i) {
            Entry& e = it->second[i];
            if (e.fn) { auto fn = e.fn; fn(boxed); }   // a copy: the handler may remove itself
        }
        if (--publishing_ == 0 && dirty_) compact();
    }

    template <typename Event>
    size_t subscriberCount() const {
        auto it = handlers_.find(std::type_index(typeid(Event)));
        if (it == handlers_.end()) return 0;
        size_t n = 0;
        for (const Entry& e : it->second) if (e.fn) ++n;
        return n;
    }

    void unsubscribe(uint64_t id) {
        for (auto& [type, list] : handlers_)
            for (Entry& e : list)
                if (e.id == id && e.fn) {
                    e.fn = nullptr;   // removed now; erased once no delivery is running
                    dirty_ = true;
                    if (publishing_ == 0) compact();
                    return;
                }
    }

private:
    struct Entry { uint64_t id; std::function<void(const std::any&)> fn; };

    template <typename Event>
    uint64_t add(std::function<void(const Event&)> handler) {
        const uint64_t id = nextId_++;
        handlers_[std::type_index(typeid(Event))].push_back({id, [handler = std::move(handler)](const std::any& e) {
            handler(std::any_cast<const Event&>(e));
        }});
        return id;
    }
    void compact() {
        for (auto& [type, list] : handlers_) {
            std::vector<Entry> kept;
            kept.reserve(list.size());
            for (Entry& e : list) if (e.fn) kept.push_back(std::move(e));
            list.swap(kept);
        }
        dirty_ = false;
    }

    std::unordered_map<std::type_index, std::vector<Entry>> handlers_;
    uint64_t nextId_ = 1;
    int publishing_ = 0;
    bool dirty_ = false;
};

inline void Subscription::reset() {
    if (bus_) bus_->unsubscribe(id_);
    bus_ = nullptr;
    id_ = 0;
}

// Process-wide bus for real gameplay code (mirrors TextureManager's singleton convention).
inline EventBus& globalEventBus() {
    static EventBus bus;
    return bus;
}

// --- Concrete event payloads. Grows as new systems need them (architecture doc §8.4). ---
struct EnemyDefeated { std::string enemyId; std::string stageId; };
struct ItemCollected { std::string itemId; std::string stageId; };
// Fired whenever a dialogue choice is confirmed (Milestone 4) — npcId identifies the
// conversation, choiceLabel identifies which choice (dialogue choices have no stable id in the
// current data/dialogue/*.json schema, so the label is the closest thing to one).
struct ChoiceMade { std::string npcId; std::string choiceLabel; };

} // namespace toms
