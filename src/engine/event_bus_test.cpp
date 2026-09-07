// event_bus_test.cpp — headless verification of the EventBus (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./event_bus_test
#include "event_bus.h"
#include <cstdio>
#include <vector>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    // 1. Basic subscribe -> publish delivers the exact payload.
    {
        EventBus bus;
        int calls = 0;
        std::string lastId, lastStage;
        bus.subscribe<EnemyDefeated>([&](const EnemyDefeated& e) {
            calls++; lastId = e.enemyId; lastStage = e.stageId;
        });
        CHECK(bus.subscriberCount<EnemyDefeated>() == 1, "one subscriber registered");
        bus.publish(EnemyDefeated{"slime", "stage_01"});
        CHECK(calls == 1, "handler invoked exactly once");
        CHECK(lastId == "slime" && lastStage == "stage_01", "payload fields delivered correctly");
    }

    // 2. Multiple subscribers to the same event all fire, in subscription order.
    {
        EventBus bus;
        std::vector<int> order;
        bus.subscribe<ItemCollected>([&](const ItemCollected&) { order.push_back(1); });
        bus.subscribe<ItemCollected>([&](const ItemCollected&) { order.push_back(2); });
        bus.publish(ItemCollected{"gem_atk", "stage_02"});
        CHECK((order == std::vector<int>{1, 2}), "both subscribers fired in subscription order");
    }

    // 3. Publishing an event with zero subscribers is a safe no-op.
    {
        EventBus bus;
        bus.publish(EnemyDefeated{"golem_stone", "stage_03"});
        CHECK(bus.subscriberCount<EnemyDefeated>() == 0, "publish with no subscribers doesn't crash/register anything");
    }

    // 4. Event types are isolated: publishing one type never invokes a handler for another.
    {
        EventBus bus;
        int wrongCalls = 0;
        bus.subscribe<ItemCollected>([&](const ItemCollected&) { wrongCalls++; });
        bus.publish(EnemyDefeated{"wraith", "stage_05"});
        CHECK(wrongCalls == 0, "ItemCollected handler not invoked by an EnemyDefeated publish");
    }

    // 5. Two independent EventBus instances don't share subscribers (no hidden global state).
    {
        EventBus a, b;
        int aCalls = 0;
        a.subscribe<ItemCollected>([&](const ItemCollected&) { aCalls++; });
        b.publish(ItemCollected{"coin", "stage_01"});
        CHECK(aCalls == 0, "publishing on bus b doesn't reach bus a's subscribers");
    }

    if (g_fail == 0) { printf("event_bus_test: ALL PASS (5 checks)\n"); return 0; }
    printf("event_bus_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
