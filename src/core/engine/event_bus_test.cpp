// event_bus_test.cpp — headless verification of the EventBus (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./event_bus_test
#include "event_bus.h"
#include <cstdio>
#include <memory>
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

    // 6. listen(): the handler goes when its Subscription does -- the 2026-10-21 crash: a destroyed Game's handler was
    //    still called when the next Game's battle published EnemyDefeated.
    {
        EventBus bus;
        struct Owner { int hits = 0; Subscription sub; };
        auto first = std::make_unique<Owner>();
        Owner* p = first.get();
        first->sub = bus.listen<EnemyDefeated>([p](const EnemyDefeated&) { p->hits++; });
        bus.publish(EnemyDefeated{"slime", "F01"});
        CHECK(first->hits == 1, "listen: delivered while the subscription lives");
        first.reset();   // the owner (a Game) is destroyed: its Subscription removes the handler
        CHECK(bus.subscriberCount<EnemyDefeated>() == 0, "listen: removed with its owner");
        Owner second;
        second.sub = bus.listen<EnemyDefeated>([&second](const EnemyDefeated&) { second.hits++; });
        bus.publish(EnemyDefeated{"slime", "F01"});   // with the old handler still there this used freed memory
        CHECK(second.hits == 1 && bus.subscriberCount<EnemyDefeated>() == 1, "listen: only the live owner is called");
    }

    // 7. A handler removing itself (or another) during a delivery: no crash, the removed one is not called again.
    {
        EventBus bus;
        int a = 0, b = 0;
        Subscription sa, sb;
        sa = bus.listen<ItemCollected>([&](const ItemCollected&) { a++; sa.reset(); sb.reset(); });
        sb = bus.listen<ItemCollected>([&](const ItemCollected&) { b++; });
        bus.publish(ItemCollected{"coin", "F01"});
        CHECK(a == 1 && b == 0, "removed during the delivery: not called");
        bus.publish(ItemCollected{"coin", "F01"});
        CHECK(a == 1 && b == 0 && bus.subscriberCount<ItemCollected>() == 0, "and gone afterwards");
        Subscription moved;
        { Subscription tmp = bus.listen<ItemCollected>([&](const ItemCollected&) { a++; }); moved = std::move(tmp); }
        bus.publish(ItemCollected{"coin", "F01"});
        CHECK(a == 2, "a moved Subscription keeps its handler");
    }

    if (g_fail == 0) { printf("event_bus_test: ALL PASS (7 checks)\n"); return 0; }
    printf("event_bus_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
