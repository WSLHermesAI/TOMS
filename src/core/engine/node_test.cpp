// node_test.cpp — headless verification of the Node scene-graph (no rendering).
// Builds a parent->child->child tree, checks world = parent*local, dirty-cache,
// and traversal. Exits 0 on success, 1 on any assertion failure.
#include "node.h"
#include <cstdio>
#include <cmath>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

static bool f_Near2(const glm::vec2& l_v2A, const glm::vec2& l_v2B, float l_fEps=1e-3f) {
    return std::fabs(l_v2A.x-l_v2B.x)<l_fEps && std::fabs(l_v2A.y-l_v2B.y)<l_fEps;
}
static bool f_NearM(const glm::mat3& l_mat3A, const glm::mat3& l_mat3B, float l_fEps=1e-3f) {
    for (int l_iI=0;l_iI<3;l_iI++) for (int l_iJ=0;l_iJ<3;l_iJ++)
        if (std::fabs(l_mat3A[l_iI][l_iJ]-l_mat3B[l_iI][l_iJ])>l_fEps) return false;
    return true;
}

int main() {
    // Build: root -> panel -> slot(0), slot(1)
    Node l_oRoot("root");
    l_oRoot.f_SetLocalPosition(glm::vec2(100, 50));

    Node l_oPanel("panel");
    l_oPanel.f_SetLocalPosition(glm::vec2(20, 30));
    l_oRoot.f_AddChild(&l_oPanel);

    Node l_oSlot0("slot0"); l_oSlot0.f_SetLocalPosition(glm::vec2(10, 10));
    Node l_oSlot1("slot1"); l_oSlot1.f_SetLocalPosition(glm::vec2(70, 10));
    l_oPanel.f_AddChild(&l_oSlot0); l_oPanel.f_AddChild(&l_oSlot1);

    // 1) world = parent.world * local (cascade)
    glm::mat3 l_mat3RootW = l_oRoot.f_GetWorldTransform();
    glm::mat3 l_mat3PanelW = l_oPanel.f_GetWorldTransform();
    glm::mat3 l_mat3Slot0W = l_oSlot0.f_GetWorldTransform();
    CHECK(f_NearM(l_mat3PanelW, l_mat3RootW * l_oPanel.f_GetLocalTransform()), "panel.world == root.world * panel.local");
    CHECK(f_NearM(l_mat3Slot0W, l_mat3PanelW * l_oSlot0.f_GetLocalTransform()), "slot0.world == panel.world * slot0.local");

    // expected slot0 world position = root(100,50) + panel(20,30) + slot0(10,10) = (130,90)
    CHECK(f_Near2(l_oSlot0.f_GetWorldPosition(), glm::vec2(130, 90)), "slot0 world pos == (130,90)");

    // 2) WorldRect of a 40x40 box at slot0 should be at (130,90)
    glm::vec4 l_v4R = l_oSlot0.f_WorldRect(glm::vec2(40, 40));
    CHECK(f_Near2(glm::vec2(l_v4R.x, l_v4R.y), glm::vec2(130, 90)), "slot0 WorldRect origin == (130,90)");
    CHECK(std::fabs(l_v4R.z-40)<1e-3f && std::fabs(l_v4R.w-40)<1e-3f, "slot0 WorldRect size == 40x40");

    // 3) moving the root moves all descendants (translation cascade)
    l_oRoot.f_SetLocalPosition(glm::vec2(200, 150));
    CHECK(f_Near2(l_oSlot0.f_GetWorldPosition(), glm::vec2(230, 190)), "after root move slot0 == (230,190)");

    // 4) dirty-cache: after a change, cached value is recomputed lazily
    l_oRoot.f_SetLocalPosition(glm::vec2(0, 0));
    // set a sentinel then read -> must reflect new local
    CHECK(f_Near2(l_oSlot0.f_GetWorldPosition(), glm::vec2(30, 40)), "after reset slot0 == (30,40)");

    // 5) scale + rotation affect world
    Node l_oN("n"); l_oN.f_SetLocalScale(glm::vec2(2, 2)); l_oN.f_SetLocalPosition(glm::vec2(5,5));
    glm::vec2 l_v2Wp = l_oN.f_WorldPoint(glm::vec2(10, 10)); // = scale*local + pos = (25,25)
    CHECK(f_Near2(l_v2Wp, glm::vec2(25, 25)), "scaled node WorldPoint == (25,25)");

    Node l_oRot("rot"); l_oRot.f_SetLocalRotation(3.14159265f/2.0f); l_oRot.f_SetLocalPosition(glm::vec2(0,0));
    glm::vec2 l_v2Rp = l_oRot.f_WorldPoint(glm::vec2(1, 0)); // rotated +90deg -> (0,1)
    CHECK(f_Near2(l_v2Rp, glm::vec2(0, 1)), "rotated node WorldPoint(1,0) == (0,1)");

    // 6) traversal visits root, panel, slot0, slot1 (4 nodes)
    int l_iCount = 0; l_oRoot.f_Visit([&](Node*){ l_iCount++; });
    CHECK(l_iCount == 4, "traversal visits 4 nodes");

    // 7) reparent: move slot1 under root
    l_oSlot1.f_SetParent(&l_oRoot);
    CHECK(l_oRoot.f_IsChild(&l_oSlot1), "slot1 reparented under root");
    CHECK(l_oPanel.f_GetChildCount() == 1, "panel now has 1 child");

    // 8) SetWorldTransform decomposes via parent inverse
    Node l_oP2("p2"); l_oP2.f_SetLocalPosition(glm::vec2(100,100));
    Node l_oC2("c2"); l_oP2.f_AddChild(&l_oC2);
    l_oC2.f_SetWorldPosition(glm::vec2(150, 130));
    CHECK(f_Near2(l_oC2.f_GetWorldPosition(), glm::vec2(150,130)), "SetWorldPosition cascades to world");
    // local should be (50,30)
    CHECK(f_Near2(l_oC2.f_GetLocalPosition(), glm::vec2(50,30)), "SetWorldPosition decomposed local == (50,30)");

    if (g_iFail == 0) { printf("node_test: ALL PASS (%d checks)\n", 8); return 0; }
    printf("node_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
