// node_test.cpp — headless verification of the Node scene-graph (no rendering).
// Builds a parent->child->child tree, checks world = parent*local, dirty-cache,
// and traversal. Exits 0 on success, 1 on any assertion failure.
#include "node.h"
#include <cstdio>
#include <cmath>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

static bool near2(const glm::vec2& a, const glm::vec2& b, float eps=1e-3f) {
    return std::fabs(a.x-b.x)<eps && std::fabs(a.y-b.y)<eps;
}
static bool nearM(const glm::mat3& a, const glm::mat3& b, float eps=1e-3f) {
    for (int i=0;i<3;i++) for (int j=0;j<3;j++)
        if (std::fabs(a[i][j]-b[i][j])>eps) return false;
    return true;
}

int main() {
    // Build: root -> panel -> slot(0), slot(1)
    Node root("root");
    root.SetLocalPosition(glm::vec2(100, 50));

    Node panel("panel");
    panel.SetLocalPosition(glm::vec2(20, 30));
    root.AddChild(&panel);

    Node slot0("slot0"); slot0.SetLocalPosition(glm::vec2(10, 10));
    Node slot1("slot1"); slot1.SetLocalPosition(glm::vec2(70, 10));
    panel.AddChild(&slot0); panel.AddChild(&slot1);

    // 1) world = parent.world * local (cascade)
    glm::mat3 rootW = root.GetWorldTransform();
    glm::mat3 panelW = panel.GetWorldTransform();
    glm::mat3 slot0W = slot0.GetWorldTransform();
    CHECK(nearM(panelW, rootW * panel.GetLocalTransform()), "panel.world == root.world * panel.local");
    CHECK(nearM(slot0W, panelW * slot0.GetLocalTransform()), "slot0.world == panel.world * slot0.local");

    // expected slot0 world position = root(100,50) + panel(20,30) + slot0(10,10) = (130,90)
    CHECK(near2(slot0.GetWorldPosition(), glm::vec2(130, 90)), "slot0 world pos == (130,90)");

    // 2) WorldRect of a 40x40 box at slot0 should be at (130,90)
    glm::vec4 r = slot0.WorldRect(glm::vec2(40, 40));
    CHECK(near2(glm::vec2(r.x, r.y), glm::vec2(130, 90)), "slot0 WorldRect origin == (130,90)");
    CHECK(std::fabs(r.z-40)<1e-3f && std::fabs(r.w-40)<1e-3f, "slot0 WorldRect size == 40x40");

    // 3) moving the root moves all descendants (translation cascade)
    root.SetLocalPosition(glm::vec2(200, 150));
    CHECK(near2(slot0.GetWorldPosition(), glm::vec2(230, 190)), "after root move slot0 == (230,190)");

    // 4) dirty-cache: after a change, cached value is recomputed lazily
    root.SetLocalPosition(glm::vec2(0, 0));
    // set a sentinel then read -> must reflect new local
    CHECK(near2(slot0.GetWorldPosition(), glm::vec2(30, 40)), "after reset slot0 == (30,40)");

    // 5) scale + rotation affect world
    Node n("n"); n.SetLocalScale(glm::vec2(2, 2)); n.SetLocalPosition(glm::vec2(5,5));
    glm::vec2 wp = n.WorldPoint(glm::vec2(10, 10)); // = scale*local + pos = (25,25)
    CHECK(near2(wp, glm::vec2(25, 25)), "scaled node WorldPoint == (25,25)");

    Node rot("rot"); rot.SetLocalRotation(3.14159265f/2.0f); rot.SetLocalPosition(glm::vec2(0,0));
    glm::vec2 rp = rot.WorldPoint(glm::vec2(1, 0)); // rotated +90deg -> (0,1)
    CHECK(near2(rp, glm::vec2(0, 1)), "rotated node WorldPoint(1,0) == (0,1)");

    // 6) traversal visits root, panel, slot0, slot1 (4 nodes)
    int count = 0; root.Visit([&](Node*){ count++; });
    CHECK(count == 4, "traversal visits 4 nodes");

    // 7) reparent: move slot1 under root
    slot1.SetParent(&root);
    CHECK(root.IsChild(&slot1), "slot1 reparented under root");
    CHECK(panel.GetChildCount() == 1, "panel now has 1 child");

    // 8) SetWorldTransform decomposes via parent inverse
    Node p2("p2"); p2.SetLocalPosition(glm::vec2(100,100));
    Node c2("c2"); p2.AddChild(&c2);
    c2.SetWorldPosition(glm::vec2(150, 130));
    CHECK(near2(c2.GetWorldPosition(), glm::vec2(150,130)), "SetWorldPosition cascades to world");
    // local should be (50,30)
    CHECK(near2(c2.GetLocalPosition(), glm::vec2(50,30)), "SetWorldPosition decomposed local == (50,30)");

    if (g_fail == 0) { printf("node_test: ALL PASS (%d checks)\n", 8); return 0; }
    printf("node_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
