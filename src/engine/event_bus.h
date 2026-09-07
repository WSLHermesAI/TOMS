// event_bus.h — minimal, type-safe pub/sub used to decouple gameplay systems.
// See docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §13 #10 and §8.4 (Mission System progress
// ticks). Deliberately small: no priorities, no unsubscribe, no async dispatch — publish() calls
// every subscriber for that exact event type synchronously, in subscription order. Grow this only
// when a real consumer (Milestone 4's Mission System) actually needs more.
#pragma once
#include <any>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace toms {

class EventBus {
public:
    template <typename Event>
    void subscribe(std::function<void(const Event&)> handler) {
        auto& list = handlers_[std::type_index(typeid(Event))];
        list.push_back([handler = std::move(handler)](const std::any& e) {
            handler(std::any_cast<const Event&>(e));
        });
    }

    template <typename Event>
    void publish(const Event& evt) {
        auto it = handlers_.find(std::type_index(typeid(Event)));
        if (it == handlers_.end()) return;
        std::any boxed = evt;
        for (auto& fn : it->second) fn(boxed);
    }

    template <typename Event>
    size_t subscriberCount() const {
        auto it = handlers_.find(std::type_index(typeid(Event)));
        return it == handlers_.end() ? 0 : it->second.size();
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>> handlers_;
};

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
