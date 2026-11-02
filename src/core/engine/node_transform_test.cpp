// node_transform_test.cpp — the invariant the node-based UI scale rests on (owner's design):
// a child's FINAL transform is its parent's world transform composed with its local one, so scaling
// a UI root makes everything under it bigger -- and the inverse maps a pointer back into the child's
// local space so what is drawn bigger is also hit correctly. Run: ./node_transform_test
#include <cmath>
#include <cstdio>

#include "scene.h"

static int g_iChecks = 0, g_iFails = 0;
#define CHECK(cond, ...)                                                          \
    do { ++g_iChecks; if (!(cond)) { ++g_iFails;                                    \
        fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);                   \
        fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static bool f_Near(float l_fA, float l_fB, float l_fEps = 0.01f) { return std::abs(l_fA - l_fB) < l_fEps; }

int main() {
    // ---- a UI root at (100,50) scaled 1.5, with a child panel at local (10,10) sized 40x20 ----
    auto l_spUiRoot = toms::GameObject::f_Make<toms::GameObject>("ui");
    l_spUiRoot->f_SetLocalPosition(glm::vec2(100.0f, 50.0f));
    l_spUiRoot->f_SetLocalScale(1.5f);
    auto l_spPanel = toms::GameObject::f_Make<toms::GameObject>("panel");
    l_spUiRoot->f_AddChild(l_spPanel.get());
    l_spPanel->f_SetLocalPosition(glm::vec2(10.0f, 10.0f));

    // the parent's scale reaches the child: world = parent.world * local
    glm::vec2 l_v2W = l_spPanel->f_GetWorldPosition();
    CHECK(f_Near(l_v2W.x, 115.0f) && f_Near(l_v2W.y, 65.0f), "child world pos should be (115,65), got (%.2f,%.2f)", l_v2W.x, l_v2W.y);

    // ...and it enlarges the child's RECT, which is what makes a UI object look bigger at the very
    // same game resolution (the owner's explicit correction: never lower the resolution to scale UI)
    glm::vec4 l_v4Wr = l_spPanel->f_WorldRect(glm::vec2(40.0f, 20.0f));
    CHECK(f_Near(l_v4Wr.z, 60.0f) && f_Near(l_v4Wr.w, 30.0f), "child rect should scale to 60x30, got %.1fx%.1f", l_v4Wr.z, l_v4Wr.w);

    // ---- the inverse: a pointer at the child's world position maps back to its local (10,10) ----
    // A node's WORLD position is its local ORIGIN (0,0), not its local position -- the parent's scale
    // moves the origin as well. (Getting this wrong is what made this check fail the first time; the
    // code was right, the expectation was not.)
    glm::vec2 l_v2Lp = l_spPanel->f_LocalPoint(l_v2W);
    CHECK(f_Near(l_v2Lp.x, 0.0f) && f_Near(l_v2Lp.y, 0.0f), "a node's world position maps back to its local origin, got (%.2f,%.2f)", l_v2Lp.x, l_v2Lp.y);
    // the local position maps to its own world point, and back
    glm::vec2 l_v2LpPos = l_spPanel->f_LocalPoint(l_spPanel->f_WorldPoint(glm::vec2(10.0f, 10.0f)));
    CHECK(f_Near(l_v2LpPos.x, 10.0f) && f_Near(l_v2LpPos.y, 10.0f), "local (10,10) must round-trip, got (%.2f,%.2f)", l_v2LpPos.x, l_v2LpPos.y);
    glm::vec2 l_v2Rt = l_spPanel->f_LocalPoint(l_spPanel->f_WorldPoint(glm::vec2(7.0f, -3.0f)));
    CHECK(f_Near(l_v2Rt.x, 7.0f) && f_Near(l_v2Rt.y, -3.0f), "LocalPoint(WorldPoint(p)) must round-trip, got (%.2f,%.2f)", l_v2Rt.x, l_v2Rt.y);

    // the hit-test a UI control actually does: a world point inside -> local point inside the local rect
    glm::vec4 l_v4LocalBox = l_spPanel->f_LocalRect(l_v4Wr);
    CHECK(f_Near(l_v4LocalBox.x, 0.0f) && f_Near(l_v4LocalBox.y, 0.0f) && f_Near(l_v4LocalBox.z, 40.0f) && f_Near(l_v4LocalBox.w, 20.0f),
          "LocalRect(WorldRect) should give the local box (0,0,40,20), got (%.1f,%.1f,%.1f,%.1f)",
          l_v4LocalBox.x, l_v4LocalBox.y, l_v4LocalBox.z, l_v4LocalBox.w);

    // ---- a deeper chain: grandparent 2x, parent 1.5x, child at (10,10) -> 30x ----
    auto l_spRoot2 = toms::GameObject::f_Make<toms::GameObject>("r2");
    l_spRoot2->f_SetLocalScale(2.0f);
    auto l_spMid = toms::GameObject::f_Make<toms::GameObject>("mid");
    l_spRoot2->f_AddChild(l_spMid.get());
    l_spMid->f_SetLocalScale(1.5f);
    auto l_spLeaf = toms::GameObject::f_Make<toms::GameObject>("leaf");
    l_spMid->f_AddChild(l_spLeaf.get());
    l_spLeaf->f_SetLocalPosition(glm::vec2(10.0f, 0.0f));
    CHECK(f_Near(l_spLeaf->f_GetWorldPosition().x, 30.0f), "2x * 1.5x must compose to 30, got %.2f",
          l_spLeaf->f_GetWorldPosition().x);
    CHECK(f_Near(l_spLeaf->f_LocalPoint(l_spLeaf->f_WorldPoint(glm::vec2(10.0f, 0.0f))).x, 10.0f),
          "the inverse must walk the whole chain (2x * 1.5x)");

    // ---- a move on the parent must be visible to the child (dirty propagation) ----
    l_spUiRoot->f_SetLocalPosition(glm::vec2(0.0f, 0.0f));
    CHECK(f_Near(l_spPanel->f_GetWorldPosition().x, 15.0f), "moving the parent must move the child, got %.2f",
          l_spPanel->f_GetWorldPosition().x);
    // ---- visibility inherits down the subtree (RenderTree's rule) ----
    l_spUiRoot->f_SetVisible(false);
    CHECK(!l_spUiRoot->f_IsVisible(), "the root reports itself hidden");
    l_spUiRoot->f_SetVisible(true);
    CHECK(l_spUiRoot->f_IsVisible(), "and visible again");

    printf("node_transform_test: %s (%d checks)\n", g_iFails == 0 ? "ALL PASS" : "FAILED", g_iChecks);
    return g_iFails == 0 ? 0 : 1;
}
