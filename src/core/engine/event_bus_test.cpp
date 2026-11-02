// event_bus_test.cpp — headless verification of the EventBus (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./event_bus_test
#include "event_bus.h"
#include <cstdio>
#include <memory>
#include <vector>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

int main() {
    // 1. Basic subscribe -> publish delivers the exact payload.
    {
        EventBus l_oBus;
        int l_iCalls = 0;
        std::string l_strLastId, l_strLastStage;
        l_oBus.f_Subscribe<EnemyDefeated>([&](const EnemyDefeated& l_oE) {
            l_iCalls++; l_strLastId = l_oE.m_strEnemyId; l_strLastStage = l_oE.m_strStageId;
        });
        CHECK(l_oBus.f_SubscriberCount<EnemyDefeated>() == 1, "one subscriber registered");
        l_oBus.f_Publish(EnemyDefeated{"slime", "stage_01"});
        CHECK(l_iCalls == 1, "handler invoked exactly once");
        CHECK(l_strLastId == "slime" && l_strLastStage == "stage_01", "payload fields delivered correctly");
    }

    // 2. Multiple subscribers to the same event all fire, in subscription order.
    {
        EventBus l_oBus;
        std::vector<int> l_vOrder;
        l_oBus.f_Subscribe<ItemCollected>([&](const ItemCollected&) { l_vOrder.push_back(1); });
        l_oBus.f_Subscribe<ItemCollected>([&](const ItemCollected&) { l_vOrder.push_back(2); });
        l_oBus.f_Publish(ItemCollected{"gem_atk", "stage_02"});
        CHECK((l_vOrder == std::vector<int>{1, 2}), "both subscribers fired in subscription order");
    }

    // 3. Publishing an event with zero subscribers is a safe no-op.
    {
        EventBus l_oBus;
        l_oBus.f_Publish(EnemyDefeated{"golem_stone", "stage_03"});
        CHECK(l_oBus.f_SubscriberCount<EnemyDefeated>() == 0, "publish with no subscribers doesn't crash/register anything");
    }

    // 4. Event types are isolated: publishing one type never invokes a handler for another.
    {
        EventBus l_oBus;
        int l_iWrongCalls = 0;
        l_oBus.f_Subscribe<ItemCollected>([&](const ItemCollected&) { l_iWrongCalls++; });
        l_oBus.f_Publish(EnemyDefeated{"wraith", "stage_05"});
        CHECK(l_iWrongCalls == 0, "ItemCollected handler not invoked by an EnemyDefeated publish");
    }

    // 5. Two independent EventBus instances don't share subscribers (no hidden global state).
    {
        EventBus l_oA, l_oB;
        int l_iACalls = 0;
        l_oA.f_Subscribe<ItemCollected>([&](const ItemCollected&) { l_iACalls++; });
        l_oB.f_Publish(ItemCollected{"coin", "stage_01"});
        CHECK(l_iACalls == 0, "publishing on bus b doesn't reach bus a's subscribers");
    }

    // 6. listen(): the handler goes when its Subscription does -- the 2026-10-21 crash: a destroyed Game's handler was
    //    still called when the next Game's battle published EnemyDefeated.
    {
        EventBus l_oBus;
        struct Owner { int m_iHits = 0; Subscription m_oSub; };
        auto l_upFirst = std::make_unique<Owner>();
        Owner* l_pP = l_upFirst.get();
        l_upFirst->m_oSub = l_oBus.f_Listen<EnemyDefeated>([l_pP](const EnemyDefeated&) { l_pP->m_iHits++; });
        l_oBus.f_Publish(EnemyDefeated{"slime", "F01"});
        CHECK(l_upFirst->m_iHits == 1, "listen: delivered while the subscription lives");
        l_upFirst.reset();   // the owner (a Game) is destroyed: its Subscription removes the handler
        CHECK(l_oBus.f_SubscriberCount<EnemyDefeated>() == 0, "listen: removed with its owner");
        Owner l_oSecond;
        l_oSecond.m_oSub = l_oBus.f_Listen<EnemyDefeated>([&l_oSecond](const EnemyDefeated&) { l_oSecond.m_iHits++; });
        l_oBus.f_Publish(EnemyDefeated{"slime", "F01"});   // with the old handler still there this used freed memory
        CHECK(l_oSecond.m_iHits == 1 && l_oBus.f_SubscriberCount<EnemyDefeated>() == 1, "listen: only the live owner is called");
    }

    // 7. A handler removing itself (or another) during a delivery: no crash, the removed one is not called again.
    {
        EventBus l_oBus;
        int l_iA = 0, l_iB = 0;
        Subscription l_oSa, l_oSb;
        l_oSa = l_oBus.f_Listen<ItemCollected>([&](const ItemCollected&) { l_iA++; l_oSa.f_Reset(); l_oSb.f_Reset(); });
        l_oSb = l_oBus.f_Listen<ItemCollected>([&](const ItemCollected&) { l_iB++; });
        l_oBus.f_Publish(ItemCollected{"coin", "F01"});
        CHECK(l_iA == 1 && l_iB == 0, "removed during the delivery: not called");
        l_oBus.f_Publish(ItemCollected{"coin", "F01"});
        CHECK(l_iA == 1 && l_iB == 0 && l_oBus.f_SubscriberCount<ItemCollected>() == 0, "and gone afterwards");
        Subscription l_oMoved;
        { Subscription l_oTmp = l_oBus.f_Listen<ItemCollected>([&](const ItemCollected&) { l_iA++; }); l_oMoved = std::move(l_oTmp); }
        l_oBus.f_Publish(ItemCollected{"coin", "F01"});
        CHECK(l_iA == 2, "a moved Subscription keeps its handler");
    }

    if (g_iFail == 0) { printf("event_bus_test: ALL PASS (7 checks)\n"); return 0; }
    printf("event_bus_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
