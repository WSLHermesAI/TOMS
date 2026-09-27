// node_transform_test.cpp — the invariant the node-based UI scale rests on (owner's design):
// a child's FINAL transform is its parent's world transform composed with its local one, so scaling
// a UI root makes everything under it bigger -- and the inverse maps a pointer back into the child's
// local space so what is drawn bigger is also hit correctly. Run: ./node_transform_test
#include <cmath>
#include <cstdio>

#include "scene.h"

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...)                                                          \
    do { ++g_checks; if (!(cond)) { ++g_fails;                                    \
        fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);                   \
        fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static bool near(float a, float b, float eps = 0.01f) { return std::abs(a - b) < eps; }

int main() {
    // ---- a UI root at (100,50) scaled 1.5, with a child panel at local (10,10) sized 40x20 ----
    auto uiRoot = toms::GameObject::Make<toms::GameObject>("ui");
    uiRoot->SetLocalPosition(glm::vec2(100.0f, 50.0f));
    uiRoot->SetLocalScale(1.5f);
    auto panel = toms::GameObject::Make<toms::GameObject>("panel");
    uiRoot->AddChild(panel.get());
    panel->SetLocalPosition(glm::vec2(10.0f, 10.0f));

    // the parent's scale reaches the child: world = parent.world * local
    glm::vec2 w = panel->GetWorldPosition();
    CHECK(near(w.x, 115.0f) && near(w.y, 65.0f), "child world pos should be (115,65), got (%.2f,%.2f)", w.x, w.y);

    // ...and it enlarges the child's RECT, which is what makes a UI object look bigger at the very
    // same game resolution (the owner's explicit correction: never lower the resolution to scale UI)
    glm::vec4 wr = panel->WorldRect(glm::vec2(40.0f, 20.0f));
    CHECK(near(wr.z, 60.0f) && near(wr.w, 30.0f), "child rect should scale to 60x30, got %.1fx%.1f", wr.z, wr.w);

    // ---- the inverse: a pointer at the child's world position maps back to its local (10,10) ----
    // A node's WORLD position is its local ORIGIN (0,0), not its local position -- the parent's scale
    // moves the origin as well. (Getting this wrong is what made this check fail the first time; the
    // code was right, the expectation was not.)
    glm::vec2 lp = panel->LocalPoint(w);
    CHECK(near(lp.x, 0.0f) && near(lp.y, 0.0f), "a node's world position maps back to its local origin, got (%.2f,%.2f)", lp.x, lp.y);
    // the local position maps to its own world point, and back
    glm::vec2 lpPos = panel->LocalPoint(panel->WorldPoint(glm::vec2(10.0f, 10.0f)));
    CHECK(near(lpPos.x, 10.0f) && near(lpPos.y, 10.0f), "local (10,10) must round-trip, got (%.2f,%.2f)", lpPos.x, lpPos.y);
    glm::vec2 rt = panel->LocalPoint(panel->WorldPoint(glm::vec2(7.0f, -3.0f)));
    CHECK(near(rt.x, 7.0f) && near(rt.y, -3.0f), "LocalPoint(WorldPoint(p)) must round-trip, got (%.2f,%.2f)", rt.x, rt.y);

    // the hit-test a UI control actually does: a world point inside -> local point inside the local rect
    glm::vec4 localBox = panel->LocalRect(wr);
    CHECK(near(localBox.x, 0.0f) && near(localBox.y, 0.0f) && near(localBox.z, 40.0f) && near(localBox.w, 20.0f),
          "LocalRect(WorldRect) should give the local box (0,0,40,20), got (%.1f,%.1f,%.1f,%.1f)",
          localBox.x, localBox.y, localBox.z, localBox.w);

    // ---- a deeper chain: grandparent 2x, parent 1.5x, child at (10,10) -> 30x ----
    auto root2 = toms::GameObject::Make<toms::GameObject>("r2");
    root2->SetLocalScale(2.0f);
    auto mid = toms::GameObject::Make<toms::GameObject>("mid");
    root2->AddChild(mid.get());
    mid->SetLocalScale(1.5f);
    auto leaf = toms::GameObject::Make<toms::GameObject>("leaf");
    mid->AddChild(leaf.get());
    leaf->SetLocalPosition(glm::vec2(10.0f, 0.0f));
    CHECK(near(leaf->GetWorldPosition().x, 30.0f), "2x * 1.5x must compose to 30, got %.2f",
          leaf->GetWorldPosition().x);
    CHECK(near(leaf->LocalPoint(leaf->WorldPoint(glm::vec2(10.0f, 0.0f))).x, 10.0f),
          "the inverse must walk the whole chain (2x * 1.5x)");

    // ---- a move on the parent must be visible to the child (dirty propagation) ----
    uiRoot->SetLocalPosition(glm::vec2(0.0f, 0.0f));
    CHECK(near(panel->GetWorldPosition().x, 15.0f), "moving the parent must move the child, got %.2f",
          panel->GetWorldPosition().x);
    // ---- visibility inherits down the subtree (RenderTree's rule) ----
    uiRoot->SetVisible(false);
    CHECK(!uiRoot->IsVisible(), "the root reports itself hidden");
    uiRoot->SetVisible(true);
    CHECK(uiRoot->IsVisible(), "and visible again");

    printf("node_transform_test: %s (%d checks)\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_checks);
    return g_fails == 0 ? 0 : 1;
}
