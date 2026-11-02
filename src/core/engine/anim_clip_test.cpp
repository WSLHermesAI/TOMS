// anim_clip_test.cpp — the node animation runtime (anim_clip.h, anim_player.h), headless.
//
//   easing     Tweeny's Penner easings by name, stepped, cubic Bezier, overshoot
//   tracks     linear / eased / stepped segments, before-first and after-last, unwrapped rotation
//   tree       world = parent * T R S, draw order by `order`, colour inheritance, hidden subtrees
//   cache      PoseCache == evaluate() (any seek order); only keyed tracks / changed poses update
//   child      a child's keys outlive its parent's: it keeps animating, the clip lasts to its last key
//   playback   per node: once + stay, once + hide, loop (its subtree's key range, also after the end)
//   player     play count, looping, stay-at-last-frame, events (time 0, loop wrap, exactly once)
//   quads      corners from pivot + trim offsets + world transform, uv, tint, additive, missing sprites
//   xyz        pos / rot / scale x, y, z: rotation order Rx * Ry * Rz (the FM79979 engine's), z depth
//              kept in the 3D world, the 2D view's orthographic flatten; version 1 files ([x, y], one
//              rot number) read as z = 0 / scale z = 1 / the z rotation and save as version 2; ==
//   json       round trip and errors
//
// Run: ./anim_clip_test
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "anim_player.h"

static int g_iChecks = 0, g_iFails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_iChecks;                                                               \
        if (!(cond)) {                                                            \
            ++g_iFails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::anim;

namespace {

bool f_Near(float l_fA, float l_fB, float l_fEps = 1e-4f) { return std::fabs(l_fA - l_fB) <= l_fEps; }
bool f_Near2(const glm::vec2& l_v2A, float l_fX, float l_fY, float l_fEps = 1e-4f) { return f_Near(l_v2A.x, l_fX, l_fEps) && f_Near(l_v2A.y, l_fY, l_fEps); }
glm::vec2 f_Apply(const glm::mat3& l_mat3M, float l_fX, float l_fY) { const glm::vec3 l_v3V = l_mat3M * glm::vec3(l_fX, l_fY, 1); return {l_v3V.x, l_v3V.y}; }
// A 3D world as the screen shows it (x, y of the point in the node's plane).
glm::vec2 f_Apply(const glm::mat4& l_mat4M, float l_fX, float l_fY) { return f_Apply(f_Flatten(l_mat4M), l_fX, l_fY); }

Ease f_Ease(const char* l_pszName) { Ease l_oE; f_ParseEase(l_pszName, l_oE); return l_oE; }

void f_TestEasing() {
    CHECK(f_Near(f_Ease("linear").f_Apply(0.5f), 0.5f), "linear");
    CHECK(f_Near(f_Ease("quadraticIn").f_Apply(0.5f), 0.25f), "quadraticIn(0.5) = 0.25");
    CHECK(f_Near(f_Ease("quadraticOut").f_Apply(0.5f), 0.75f), "quadraticOut(0.5) = 0.75");
    CHECK(f_Ease("stepped").f_Apply(0.9f) == 0.0f, "stepped holds");
    for (const char* l_pszN : {"cubicInOut", "sinusoidalIn", "exponentialOut", "circularInOut", "bounceOut", "elasticIn", "backInOut"}) {
        const Ease l_oE = f_Ease(l_pszN);
        CHECK(f_Near(l_oE.f_Apply(0.0f), 0.0f, 1e-3f) && f_Near(l_oE.f_Apply(1.0f), 1.0f, 1e-3f), "%s starts at 0 and ends at 1", l_pszN);
    }
    float l_fMaxBack = 0;
    for (int l_iI = 0; l_iI <= 100; l_iI++) l_fMaxBack = std::max(l_fMaxBack, f_Ease("backOut").f_Apply(l_iI / 100.0f));
    CHECK(l_fMaxBack > 1.05f, "backOut overshoots (%f)", l_fMaxBack);
    // Every name round-trips; unknown names are rejected.
    for (const std::string& l_strN : f_EaseNames()) { Ease l_oE; CHECK(f_ParseEase(l_strN, l_oE) && f_EaseName(l_oE) == l_strN, "ease name %s", l_strN.c_str()); }
    Ease l_oBad;
    CHECK(!f_ParseEase("wobbly", l_oBad), "unknown ease rejected");
    // Bezier: (0,0,1,1) is linear; CSS ease-in (0.42,0,1,1) is below the diagonal.
    Ease l_oBz; l_oBz.m_iKind = Ease::m_iBezier;
    for (int l_iI = 0; l_iI <= 10; l_iI++) CHECK(f_Near(l_oBz.f_Apply(l_iI / 10.0f), l_iI / 10.0f, 1e-3f), "bezier(0,0,1,1) linear at %d", l_iI);
    Ease l_oEaseIn; l_oEaseIn.m_iKind = Ease::m_iBezier; l_oEaseIn.m_arrBezier[0] = 0.42f; l_oEaseIn.m_arrBezier[1] = 0; l_oEaseIn.m_arrBezier[2] = 1; l_oEaseIn.m_arrBezier[3] = 1;
    CHECK(l_oEaseIn.f_Apply(0.5f) < 0.4f && l_oEaseIn.f_Apply(0.5f) > 0.2f, "ease-in bezier at 0.5 = %f", l_oEaseIn.f_Apply(0.5f));
}

void f_TestTracks() {
    Node l_oN;
    l_oN.m_v3Pos = {7, 7, 0};
    CHECK(f_Near2(f_PositionAt(l_oN, 3), 7, 7), "no keys: rest value");
    l_oN.m_vPosKeys = {{0.0f, {0, 0, 0}, {}}, {1.0f, {10, 20, 0}, {}}, {2.0f, {10, 0, 0}, f_Ease("stepped")}, {3.0f, {0, 0, 0}, {}}};
    CHECK(f_Near2(f_PositionAt(l_oN, -1), 0, 0), "before the first key");
    CHECK(f_Near2(f_PositionAt(l_oN, 0.5f), 5, 10), "linear midpoint");
    CHECK(f_Near2(f_PositionAt(l_oN, 1.5f), 10, 10), "second segment");
    CHECK(f_Near2(f_PositionAt(l_oN, 2.9f), 10, 0), "stepped key holds");
    CHECK(f_Near2(f_PositionAt(l_oN, 3.0f), 0, 0), "on the last key");
    CHECK(f_Near2(f_PositionAt(l_oN, 9.0f), 0, 0), "after the last key");
    l_oN.m_vPosKeys[0].m_oEase = f_Ease("quadraticIn");
    CHECK(f_Near2(f_PositionAt(l_oN, 0.5f), 2.5f, 5), "the key's ease shapes its segment");
    l_oN.m_vRotKeys = {{0.0f, {0, 0, 0.0f}, {}}, {1.0f, {0, 0, 720.0f}, {}}};
    CHECK(f_Near(f_RotationAt(l_oN, 0.5f).z, 360.0f), "rotation is not wrapped");
    l_oN.m_vSpriteKeys = {{0.0f, "a", {}}, {0.25f, "b", {}}};
    CHECK(f_SpriteAt(l_oN, 0.1f) == "a" && f_SpriteAt(l_oN, 0.25f) == "b" && f_SpriteAt(l_oN, 5) == "b", "sprite steps");
    l_oN.m_vColorKeys = {{0.0f, {1, 1, 1, 1}, f_Ease("backIn")}, {1.0f, {1, 1, 1, 0}, {}}};
    for (int l_iI = 0; l_iI <= 10; l_iI++) { const glm::vec4 l_v4C = f_ColorAt(l_oN, l_iI / 10.0f); CHECK(l_v4C.a >= 0 && l_v4C.a <= 1, "colour clamped at %d", l_iI); }
    CHECK(f_Near(l_oN.f_LastKeyTime(), 3.0f), "last key time");
}

// root (no sprite) at (100,0) turned 90 degrees, with children "shadow" (order -1), "body",
// "fx" (order 2) and "hat" (order 0, child of body).
Clip f_TreeClip() {
    Clip l_oC;
    l_oC.m_strName = "tree";
    l_oC.m_oRoot.m_strName = "root";
    l_oC.m_oRoot.m_v3Pos = {100, 0, 0};
    l_oC.m_oRoot.m_v3Rot = {0, 0, 90};
    l_oC.m_oRoot.m_v4Color = {1, 0.5f, 1, 1};
    Node l_oBody; l_oBody.m_strName = "body"; l_oBody.m_strSprite = "s"; l_oBody.m_v3Pos = {10, 0, 0};
    Node l_oHat; l_oHat.m_strName = "hat"; l_oHat.m_strSprite = "s"; l_oHat.m_v3Pos = {0, -5, 0};
    l_oBody.m_vChildren.push_back(l_oHat);
    Node l_oFx; l_oFx.m_strName = "fx"; l_oFx.m_iOrder = 2; l_oFx.m_bInheritColor = false; l_oFx.m_eBlend = Blend::Add; l_oFx.m_strSprite = "s";
    Node l_oShadow; l_oShadow.m_strName = "shadow"; l_oShadow.m_iOrder = -1; l_oShadow.m_strSprite = "s";
    l_oC.m_oRoot.m_vChildren = {l_oFx, l_oBody, l_oShadow};
    return l_oC;
}

void f_TestTree() {
    Clip l_oC = f_TreeClip();
    std::vector<NodePose> l_vPoses;
    f_Evaluate(l_oC, 0, l_vPoses);
    std::vector<std::string> l_vOrder;
    for (const NodePose& l_oP : l_vPoses) l_vOrder.push_back(l_oP.m_pNode->m_strName);
    CHECK((l_vOrder == std::vector<std::string>{"shadow", "root", "body", "hat", "fx"}), "draw order");
    const NodePose& l_oBody = l_vPoses[2];
    // (10,0) turned 90 degrees clockwise (y down) is (0,10); plus the root's (100,0).
    CHECK(f_Near2(f_Apply(l_oBody.m_mat4World, 0, 0), 100, 10), "child position through a rotated parent");
    const NodePose& l_oHat = l_vPoses[3];
    CHECK(f_Near2(f_Apply(l_oHat.m_mat4World, 0, 0), 105, 10), "grandchild: (0,-5) turned is (5,0)");
    CHECK(f_Near(l_oBody.m_v4Color.g, 0.5f) && f_Near(l_vPoses[4].m_v4Color.g, 1.0f), "colour inherited unless inheritColor is false");
    l_oC.m_oRoot.m_vChildren[1].m_vVisibleKeys = {{0.0f, false, {}}};
    f_Evaluate(l_oC, 0, l_vPoses);
    CHECK(!l_vPoses[2].m_bVisible && !l_vPoses[3].m_bVisible && l_vPoses[4].m_bVisible, "a hidden node hides its subtree only");
    // Scale and mirror.
    Node l_oM; l_oM.m_v3Scale = {-2, 1, 1};
    CHECK(f_Near2(f_Apply(f_LocalTransform(l_oM, 0), 3, 4), -6, 4), "negative scale mirrors");
}

bool f_SamePoses(const std::vector<NodePose>& l_vA, const std::vector<NodePose>& l_vB, float l_fT) {
    if (l_vA.size() != l_vB.size()) return false;
    for (size_t l_nI = 0; l_nI < l_vA.size(); l_nI++) {
        bool l_bSame = l_vA[l_nI].m_pNode == l_vB[l_nI].m_pNode && l_vA[l_nI].m_iDepth == l_vB[l_nI].m_iDepth && l_vA[l_nI].m_bVisible == l_vB[l_nI].m_bVisible &&
                    l_vA[l_nI].m_strSprite == l_vB[l_nI].m_strSprite;
        for (int l_iC = 0; l_iC < 4; l_iC++)
            for (int l_iR = 0; l_iR < 4; l_iR++) l_bSame &= f_Near(l_vA[l_nI].m_mat4World[l_iC][l_iR], l_vB[l_nI].m_mat4World[l_iC][l_iR], 1e-4f);
        for (int l_iK = 0; l_iK < 4; l_iK++) l_bSame &= f_Near(l_vA[l_nI].m_v4Color[l_iK], l_vB[l_nI].m_v4Color[l_iK], 1e-6f);
        if (!l_bSame) {
            fprintf(stderr, "  pose %zu (%s) differs at t=%f\n", l_nI, l_vA[l_nI].m_pNode ? l_vA[l_nI].m_pNode->m_strName.c_str() : "?", l_fT);
            return false;
        }
    }
    return true;
}

void f_TestPoseCache() {
    // Each node animates only what it needs: the root turns, body only switches sprites, fx only
    // fades (with a stepped key) and blinks, shadow only scales, hat has no keys at all.
    Clip l_oC = f_TreeClip();
    l_oC.m_oRoot.m_vRotKeys = {{0.0f, {0, 0, 0.0f}, f_Ease("quadraticInOut")}, {1.0f, {0, 0, 360.0f}, {}}};
    Node& l_oFx = l_oC.m_oRoot.m_vChildren[0];
    Node& l_oBody = l_oC.m_oRoot.m_vChildren[1];
    Node& l_oShadow = l_oC.m_oRoot.m_vChildren[2];
    l_oBody.m_vSpriteKeys = {{0.0f, "s", {}}, {0.3f, "p", {}}, {0.6f, "s", {}}};
    l_oFx.m_vColorKeys = {{0.2f, {1, 1, 1, 0}, {}}, {0.5f, {1, 1, 1, 1}, f_Ease("stepped")}, {0.8f, {1, 0, 0, 1.5f}, {}}};
    l_oFx.m_vVisibleKeys = {{0.0f, true, {}}, {0.4f, false, {}}, {0.45f, true, {}}};
    l_oShadow.m_vScaleKeys = {{0.1f, {1, 1, 1}, f_Ease("backOut")}, {0.9f, {2, 0.5f, 1}, {}}};

    PoseCache l_oCache;
    l_oCache.f_Bind(&l_oC);
    CHECK(l_oCache.f_AnimatedNodes() == 4, "4 animated nodes (hat has no keys): %d", l_oCache.f_AnimatedNodes());
    std::vector<NodePose> l_vRef;
    bool l_bSame = true;
    auto l_fnAt = [&](float l_fT) {
        l_oCache.f_Seek(l_fT);
        f_Evaluate(l_oC, l_fT, l_vRef);
        l_bSame = l_bSame && f_SamePoses(l_oCache.f_Poses(), l_vRef, l_fT);
    };
    for (int l_iF = 0; l_iF <= 150; l_iF++) l_fnAt(l_iF / 60.0f);                                   // forward, past the end
    for (float l_fT : {0.1f, 0.0f, 0.3f, 0.2999f, 0.5f, 0.45f, 0.4f, 0.8f, 0.9f, 1.0f}) l_fnAt(l_fT);   // jumps, exactly on keys
    unsigned l_uSeed = 7;
    for (int l_iI = 0; l_iI < 300; l_iI++) {                                                  // anywhere, either way
        l_uSeed = l_uSeed * 1103515245u + 12345u;
        l_fnAt(float((l_uSeed >> 8) % 1300) / 1000.0f - 0.1f);
    }
    CHECK(l_bSame, "PoseCache == evaluate() forward, backward, on keys, at random times");

    // Only what has keys moves.
    Clip l_oS;
    l_oS.m_oRoot.m_strName = "root";
    Node l_oA; l_oA.m_strName = "a"; l_oA.m_strSprite = "s"; l_oA.m_vSpriteKeys = {{0.0f, "s", {}}, {0.5f, "p", {}}};
    Node l_oB; l_oB.m_strName = "b"; l_oB.m_strSprite = "s";
    Node l_oKid; l_oKid.m_strName = "kid"; l_oKid.m_strSprite = "s";
    Node l_oMover; l_oMover.m_strName = "mover"; l_oMover.m_vPosKeys = {{0.0f, {0, 0, 0}, {}}, {1.0f, {10, 0, 0}, {}}};
    l_oMover.m_vChildren.push_back(l_oKid);
    l_oS.m_oRoot.m_vChildren = {l_oA, l_oB, l_oMover};
    l_oCache.f_Bind(&l_oS);
    l_oCache.f_Seek(0);
    CHECK(l_oCache.f_ChangedCount() == 5, "first seek: every pose (%d)", l_oCache.f_ChangedCount());
    const NodePose& l_oPa = l_oCache.f_Poses()[1];
    const glm::mat4 l_mat4AWorld = l_oPa.m_mat4World;
    l_oCache.f_Seek(0.25f);
    CHECK(l_oCache.f_ChangedCount() == 2 && l_oCache.f_Changed(3) && l_oCache.f_Changed(4),
          "mid-way: only mover and its child (%d)", l_oCache.f_ChangedCount());
    l_oCache.f_Seek(0.6f);
    CHECK(l_oCache.f_Changed(1) && l_oPa.m_strSprite == "p" && l_oPa.m_mat4World == l_mat4AWorld && !l_oCache.f_Changed(2) && l_oCache.f_ChangedCount() == 3,
          "a's sprite key: a changes (sprite only), b does not");
    l_oCache.f_Seek(0.7f);
    CHECK(!l_oCache.f_Changed(1) && l_oCache.f_ChangedCount() == 2, "a holds its sprite: not touched");
    l_oCache.f_Seek(1.5f);
    l_oCache.f_Seek(2.0f);
    CHECK(l_oCache.f_ChangedCount() == 0, "after every last key nothing changes");
    f_Evaluate(l_oS, 2.0f, l_vRef);
    CHECK(f_SamePoses(l_oCache.f_Poses(), l_vRef, 2.0f), "and the poses are still right");
}

// A child's keys can start and end at other times than its parent's: it keeps animating after the
// parent's last key (and before its own first key it holds that key's value).
void f_TestChildOutlivesParent() {
    Clip l_oC;
    l_oC.m_strName = "c";
    l_oC.m_oRoot.m_strName = "root";
    l_oC.m_oRoot.m_vPosKeys = {{0.0f, {0, 0, 0}, {}}, {0.5f, {100, 0, 0}, {}}};   // the parent stops at 0.5
    Node l_oKid;
    l_oKid.m_strName = "kid";
    l_oKid.m_strSprite = "s";
    l_oKid.m_vPosKeys = {{0.3f, {0, 0, 0}, {}}, {1.5f, {0, 120, 0}, {}}};        // the child runs 0.3 .. 1.5
    l_oKid.m_vRotKeys = {{1.0f, {0, 0, 0.0f}, {}}, {2.0f, {0, 0, 90.0f}, {}}};            // and turns 1.0 .. 2.0
    l_oC.m_oRoot.m_vChildren.push_back(l_oKid);
    CHECK(f_Near(l_oC.f_Duration(), 2.0f), "duration = the child's last key: %f", l_oC.f_Duration());

    AnimPlayer l_oP;   // its time, and a PoseCache seeked to it each frame (as AnimPlayer::draw does)
    PoseCache l_oCache;
    l_oCache.f_Bind(&l_oC);
    l_oP.f_Play(&l_oC);
    l_oP.f_Update(0);
    l_oCache.f_Seek(l_oP.f_Time());
    std::vector<NodePose> l_vRef;
    bool l_bSame = true, l_bMoved = true;
    glm::vec2 l_v2Last = f_Apply(l_oCache.f_Poses()[1].m_mat4World, 0, 0);
    for (int l_iF = 1; l_iF <= 120; l_iF++) {   // 60 fps to 2.0 s
        l_oP.f_Update(l_iF * 1000 / 60 - (l_iF - 1) * 1000 / 60);
        const float l_fT = l_oP.f_Time();
        l_oCache.f_Seek(l_fT);
        f_Evaluate(l_oC, l_fT, l_vRef);
        l_bSame = l_bSame && f_SamePoses(l_oCache.f_Poses(), l_vRef, l_fT);
        const glm::vec2 l_v2At = f_Apply(l_oCache.f_Poses()[1].m_mat4World, 0, 0);
        if (l_fT > 0.55f && l_fT < 1.45f) l_bMoved = l_bMoved && l_v2At.y > l_v2Last.y;   // still moving after the parent stopped
        l_v2Last = l_v2At;
    }
    CHECK(l_bSame, "the player's poses == evaluate() every frame");
    CHECK(l_bMoved, "the child keeps moving after the parent's last key");
    CHECK(f_Near2(f_Apply(l_oCache.f_Poses()[1].m_mat4World, 0, 0), 100, 120, 1e-3f), "child at the end: parent (100,0) + own (0,120)");
    l_oP.f_Update(50);   // (the 60 fps steps sum to a hair under 2 s)
    CHECK(f_Near(l_oP.f_Time(), 2.0f, 1e-3f) && l_oP.f_Finished(), "the clip ends at the child's last key, not the parent's");
}

// Per-node playback: a node plays once and stays (default), plays once and hides, or loops its
// subtree's key range -- also after the clip's own timeline has ended.
void f_TestNodePlayback() {
    Clip l_oC;
    l_oC.m_strName = "popup";
    l_oC.m_oRoot.m_strName = "root";
    Node l_oPop; l_oPop.m_strName = "pop"; l_oPop.m_strSprite = "s";                      // once, stays
    l_oPop.m_vScaleKeys = {{0.0f, {0, 0, 1}, {}}, {0.3f, {1, 1, 1}, {}}};
    Node l_oFlash; l_oFlash.m_strName = "flash"; l_oFlash.m_strSprite = "s"; l_oFlash.m_bStayAtLastFrame = false;   // once, then hides
    l_oFlash.m_vColorKeys = {{0.0f, {1, 1, 1, 1}, {}}, {0.2f, {1, 1, 1, 0.5f}, {}}};
    Node l_oSpark; l_oSpark.m_strName = "spark"; l_oSpark.m_strSprite = "s"; l_oSpark.m_bLoop = true;              // loops 0.5..1.0
    l_oSpark.m_vPosKeys = {{0.5f, {0, 0, 0}, {}}, {1.0f, {10, 0, 0}, {}}};
    Node l_oOrbit; l_oOrbit.m_strName = "orbit"; l_oOrbit.m_bLoop = true;                                 // a looping group
    l_oOrbit.m_vRotKeys = {{0.0f, {0, 0, 0.0f}, {}}, {1.0f, {0, 0, 360.0f}, {}}};
    Node l_oDot; l_oDot.m_strName = "dot"; l_oDot.m_strSprite = "s";                                        // runs in orbit's time
    l_oDot.m_vPosKeys = {{0.0f, {20, 0, 0}, {}}, {0.5f, {40, 0, 0}, {}}};
    l_oOrbit.m_vChildren.push_back(l_oDot);
    l_oC.m_oRoot.m_vChildren = {l_oPop, l_oFlash, l_oSpark, l_oOrbit};
    CHECK(f_Near(l_oC.f_Duration(), 1.0f) && f_HasLoopingNodes(l_oC), "duration 1.0, has looping nodes");

    std::vector<NodePose> l_vPs;
    auto l_fnFind = [&](const char* l_pszName) -> const NodePose& {
        for (const NodePose& l_oP : l_vPs)
            if (l_oP.m_pNode->m_strName == l_pszName) return l_oP;
        return l_vPs.front();
    };
    f_Evaluate(l_oC, 0.75f, l_vPs);
    CHECK(f_Near2(f_Apply(l_fnFind("spark").m_mat4World, 0, 0), 5, 0), "spark mid-range at 0.75");
    f_Evaluate(l_oC, 1.25f, l_vPs);
    CHECK(f_Near2(f_Apply(l_fnFind("spark").m_mat4World, 0, 0), 5, 0), "spark looped: 1.25 plays as 0.75");
    CHECK(f_Near2(f_Apply(l_fnFind("dot").m_mat4World, 0, 0), 0, 30, 1e-3f), "orbit looped to 0.25 (90 deg), dot in orbit's time (x 30): %f %f",
          f_Apply(l_fnFind("dot").m_mat4World, 0, 0).x, f_Apply(l_fnFind("dot").m_mat4World, 0, 0).y);
    f_Evaluate(l_oC, 0.1f, l_vPs);
    CHECK(l_fnFind("flash").m_bVisible, "flash shows during its keys");
    f_Evaluate(l_oC, 0.3f, l_vPs);
    CHECK(!l_fnFind("flash").m_bVisible && l_fnFind("pop").m_bVisible, "flash hidden after its last key; pop stays");
    f_Evaluate(l_oC, 7.6f, l_vPs);
    CHECK(f_Near(l_fnFind("pop").m_mat4World[0][0], 1.0f) && !l_fnFind("flash").m_bVisible && f_Near2(f_Apply(l_fnFind("spark").m_mat4World, 0, 0), 2, 0, 1e-3f),
          "long after the end: pop holds, flash hidden, spark still looping (7.6 -> 0.6)");

    PoseCache l_oCache;
    l_oCache.f_Bind(&l_oC);
    std::vector<NodePose> l_vRef;
    bool l_bSame = true;
    auto l_fnAt = [&](float l_fT) {
        l_oCache.f_Seek(l_fT);
        f_Evaluate(l_oC, l_fT, l_vRef);
        l_bSame = l_bSame && f_SamePoses(l_oCache.f_Poses(), l_vRef, l_fT);
    };
    for (int l_iF = 0; l_iF <= 300; l_iF++) l_fnAt(l_iF / 60.0f);   // 0 .. 5 s
    for (float l_fT : {0.2f, 0.19f, 1.0f, 1.5f, 0.3f, 4.0f, 0.0f}) l_fnAt(l_fT);
    unsigned l_uSeed = 3;
    for (int l_iI = 0; l_iI < 300; l_iI++) {
        l_uSeed = l_uSeed * 1103515245u + 12345u;
        l_fnAt(float((l_uSeed >> 8) % 5000) / 1000.0f);
    }
    CHECK(l_bSame, "PoseCache == evaluate() with looping / hiding nodes, any seek order");

    AnimPlayer l_oP;
    l_oP.f_Play(&l_oC);
    l_oP.f_Update(0);
    l_oP.f_Update(1750);
    CHECK(l_oP.f_Finished() && f_Near(l_oP.f_Time(), 1.0f) && f_Near(l_oP.f_PoseTime(), 1.75f, 1e-3f),
          "after the clip's end: time() holds 1.0, poseTime() runs on (%f)", l_oP.f_PoseTime());
    Clip l_oStill = l_oC;
    l_oStill.m_oRoot.m_vChildren[2].m_bLoop = false;
    l_oStill.m_oRoot.m_vChildren[3].m_bLoop = false;
    AnimPlayer l_oQ;
    l_oQ.f_Play(&l_oStill);
    l_oQ.f_Update(0);
    l_oQ.f_Update(1750);
    CHECK(f_Near(l_oQ.f_PoseTime(), 1.0f), "without looping nodes poseTime() stops at the end");

    AnimFile l_oF;
    l_oF.m_vClips.push_back(l_oC);
    AnimFile l_oBack;
    std::string l_strErr;
    CHECK(f_ParseAnim(f_AnimToJson(l_oF), l_oBack, &l_strErr) && l_oBack.m_vClips[0].m_oRoot.m_vChildren[1].m_bStayAtLastFrame == false &&
              l_oBack.m_vClips[0].m_oRoot.m_vChildren[2].m_bLoop && l_oBack.m_vClips[0].m_oRoot.m_vChildren[0].m_bStayAtLastFrame &&
              !l_oBack.m_vClips[0].m_oRoot.m_vChildren[0].m_bLoop,
          "loop / stayAtLastFrame round-trip through JSON");
}

void f_TestPlayer() {
    Clip l_oC;
    l_oC.m_strName = "c";
    l_oC.m_oRoot.m_vPosKeys = {{0.0f, {0, 0, 0}, {}}, {1.0f, {10, 0, 0}, {}}};
    l_oC.m_oRoot.m_vEventKeys = {{0.0f, "start", {}}, {0.5f, "hit", {}}, {1.0f, "end", {}}};
    l_oC.m_iPlayCount = 2;
    AnimPlayer l_oP;
    l_oP.f_Play(&l_oC);
    l_oP.f_Update(0);
    CHECK((l_oP.f_TakeEvents() == std::vector<std::string>{"start"}), "time-0 event on the first update");
    l_oP.f_Update(600);
    CHECK((l_oP.f_TakeEvents() == std::vector<std::string>{"hit"}), "event at 0.5");
    CHECK(f_Near(l_oP.f_Time(), 0.6f, 1e-3f), "time 0.6");
    l_oP.f_Update(900);   // 1.5: crosses the end of play 1 and the start of play 2
    CHECK((l_oP.f_TakeEvents() == std::vector<std::string>{"end", "start", "hit"}), "events across the wrap, once each");
    CHECK(f_Near(l_oP.f_Time(), 0.5f, 1e-3f) && !l_oP.f_Finished(), "second play at 0.5");
    l_oP.f_Update(5000);
    CHECK((l_oP.f_TakeEvents() == std::vector<std::string>{"end"}), "only up to the last play");
    CHECK(l_oP.f_Finished() && l_oP.f_Showing() && f_Near(l_oP.f_Time(), 1.0f), "finished, stays at the last frame");
    l_oC.m_bStayAtLastFrame = false;
    CHECK(!l_oP.f_Showing(), "hidden after the end without stayAtLastFrame");
    l_oC.m_iPlayCount = -1;
    l_oP.f_Play(&l_oC);
    l_oP.f_Update(0);
    l_oP.f_TakeEvents();
    l_oP.f_Update(10250);   // ten plays and a quarter
    int l_iHits = 0;
    for (const std::string& l_strE : l_oP.f_TakeEvents()) l_iHits += l_strE == "hit";
    CHECK(l_iHits == 10 && !l_oP.f_Finished() && f_Near(l_oP.f_Time(), 0.25f, 1e-3f), "looping: %d hits", l_iHits);
}

struct CaptureRenderer : IRenderer {
    std::vector<Quad> m_vQuads;
    void f_Init(uint32_t, uint32_t) override {}
    void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    void begin() override {}
    void f_DrawSprite(const Quad& l_oQ) override { m_vQuads.push_back(l_oQ); }
    void end() override {}
    uint32_t f_Width() const override { return 1024; }
    uint32_t f_Height() const override { return 768; }
};

void f_TestQuads() {
    // A 100x100 page: "s" is a 20x10 image trimmed to 16x8 at offset (2,1); "p" has a top-left pivot.
    toms::AtlasFile l_oAtlas;
    std::string l_strErr;
    CHECK(toms::f_ParseAtlas("page.png\nsize: 100, 100\n"
                           "s\n  bounds: 10, 20, 16, 8\n  offsets: 2, 1, 20, 10\n"
                           "p\n  bounds: 50, 50, 10, 10\n  x_pivot: 0, 0\n", l_oAtlas, &l_strErr), "atlas: %s", l_strErr.c_str());
    Clip l_oC;
    l_oC.m_oRoot.m_strSprite = "s";
    l_oC.m_oRoot.m_v3Pos = {100, 100, 0};
    std::vector<NodePose> l_vPoses;
    f_Evaluate(l_oC, 0, l_vPoses);
    std::vector<Quad> l_vQ;
    f_AppendQuads(l_vPoses, l_oAtlas, glm::mat3(1.0f), nullptr, l_vQ);
    CHECK(l_vQ.size() == 1, "one quad");
    // Centre pivot (10,5) of the 20x10 original; trimmed pixels start at (2,1): x 92..108, y 96..104.
    CHECK(l_vQ.size() == 1 && f_Near(l_vQ[0].m_arrCorners[0], 92) && f_Near(l_vQ[0].m_arrCorners[1], 96) && f_Near(l_vQ[0].m_arrCorners[4], 108) &&
              f_Near(l_vQ[0].m_arrCorners[5], 104), "corners from pivot and trim offset");
    CHECK(l_vQ.size() == 1 && f_Near(l_vQ[0].m_arrUv[0], 0.10f) && f_Near(l_vQ[0].m_arrUv[3], 0.28f), "uv from the atlas");
    // Rotated 90 degrees and placed at (10,10) scaled x2 by the placement.
    l_oC.m_oRoot.m_v3Rot = {0, 0, 90};
    l_oC.m_oRoot.m_eBlend = Blend::Add;
    l_oC.m_oRoot.m_v4Color = {1, 1, 1, 0.5f};
    f_Evaluate(l_oC, 0, l_vPoses);
    l_vQ.clear();
    const float l_arrTint[4] = {1, 0, 1, 1};
    f_AppendQuads(l_vPoses, l_oAtlas, f_Placement(10, 10, 2), l_arrTint, l_vQ);
    // Top-left corner (-8,-4) from the pivot, turned 90 -> (4,-8), at (100,100) -> (104,92), x2 +10 -> (218,194).
    CHECK(l_vQ.size() == 1 && f_Near(l_vQ[0].m_arrCorners[0], 218) && f_Near(l_vQ[0].m_arrCorners[1], 194), "rotated corner (%f,%f)",
          l_vQ.empty() ? 0 : l_vQ[0].m_arrCorners[0], l_vQ.empty() ? 0 : l_vQ[0].m_arrCorners[1]);
    CHECK(l_vQ.size() == 1 && l_vQ[0].m_bAdditive && f_Near(l_vQ[0].m_arrTint[1], 0) && f_Near(l_vQ[0].m_arrTint[3], 0.5f), "blend and tint");
    // The atlas's own pivot, and a missing sprite reported once.
    l_oC.m_oRoot = Node();
    l_oC.m_oRoot.m_strSprite = "p";
    Node l_oA; l_oA.m_strSprite = "nope"; Node l_oB; l_oB.m_strSprite = "nope";
    l_oC.m_oRoot.m_vChildren = {l_oA, l_oB};
    f_Evaluate(l_oC, 0, l_vPoses);
    l_vQ.clear();
    std::vector<std::string> l_vMissing;
    f_AppendQuads(l_vPoses, l_oAtlas, glm::mat3(1.0f), nullptr, l_vQ, &l_vMissing);
    CHECK(l_vQ.size() == 1 && f_Near(l_vQ[0].m_arrCorners[0], 0) && f_Near(l_vQ[0].m_arrCorners[1], 0), "x_pivot 0,0 = top-left");
    CHECK(l_vMissing == std::vector<std::string>{"nope"}, "missing sprite reported once");
    // The player draws through the renderer.
    CaptureRenderer l_oRen;
    AnimPlayer l_oP;
    l_oP.f_Play(&l_oC);
    l_oP.f_Draw(&l_oRen, l_oAtlas, glm::mat3(1.0f));
    CHECK(l_oRen.m_vQuads.size() == 1 && l_oRen.m_vQuads[0].m_u16Texture == g_u16SpriteAtlasTexture, "player drew %zu quad(s) from the sprite atlas",
          l_oRen.m_vQuads.size());

    // Several atlases: looked up in order (first match wins), each page with its own texture.
    toms::AtlasFile l_oSecond;
    CHECK(toms::f_ParseAtlas("a.png\nsize: 10, 10\nonly2\n  bounds: 0, 0, 4, 4\n"
                           "\nb.png\nsize: 20, 20\ns\n  bounds: 0, 0, 2, 2\n", l_oSecond, &l_strErr), "second atlas: %s", l_strErr.c_str());
    AtlasSet l_oSet;
    l_oSet.f_Add(l_oAtlas, "game");             // the sprite atlas: kSpriteAtlasTexture
    l_oSet.f_Add(l_oSecond, "fx", {7, 9});      // its two pages: textures 7 and 9
    uint16_t l_u16Tex = 0;
    const toms::AtlasRegion* l_pR = l_oSet.f_Find("s", &l_u16Tex);
    CHECK(l_pR && l_pR->m_iW == 16 && l_u16Tex == g_u16SpriteAtlasTexture, "'s' from the first atlas that has it");
    l_pR = l_oSet.f_Find("only2", &l_u16Tex);
    CHECK(l_pR && l_u16Tex == 7, "'only2' from the second atlas, page 0 -> texture 7");
    CHECK(!l_oSet.f_Find("nope"), "in no atlas");
    // "atlas:sprite" picks the atlas, like MPDI's PIName + ImageName: the same name from either.
    int l_iEntry = -1;
    l_pR = l_oSet.f_Find("fx:s", &l_u16Tex, &l_iEntry);
    CHECK(l_pR && l_pR->m_iW == 2 && l_u16Tex == 9 && l_iEntry == 1, "'fx:s' is the second atlas's s (page 1 -> texture 9)");
    l_pR = l_oSet.f_Find("game:s", &l_u16Tex, &l_iEntry);
    CHECK(l_pR && l_pR->m_iW == 16 && l_u16Tex == g_u16SpriteAtlasTexture && l_iEntry == 0, "'game:s' is the first atlas's s");
    CHECK(!l_oSet.f_Find("game:only2"), "a named atlas without the sprite: not found, no fallback");
    CHECK(l_oSet.f_Find("other:only2", &l_u16Tex) && l_u16Tex == 7, "an atlas id the set does not have: looked up by order");
    l_oC.m_oRoot = Node();
    l_oC.m_oRoot.m_strSprite = "only2";
    l_oC.m_oRoot.m_vSpriteKeys = {{0.0f, "only2", {}}, {0.5f, "fx:s", {}}};
    f_Evaluate(l_oC, 0, l_vPoses);
    l_vQ.clear();
    f_AppendQuads(l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_vQ);
    CHECK(l_vQ.size() == 1 && l_vQ[0].m_u16Texture == 7 && f_Near(l_vQ[0].m_arrUv[2], 0.4f), "quad carries its atlas page's texture and uv");
    f_Evaluate(l_oC, 0.6f, l_vPoses);   // the sprite key switched atlas and page
    l_vQ.clear();
    f_AppendQuads(l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_vQ);
    CHECK(l_vQ.size() == 1 && l_vQ[0].m_u16Texture == 9 && f_Near(l_vQ[0].m_arrUv[2], 0.1f), "a sprite key from another atlas page");

    // The player keeps each quad until its pose changes: frame by frame it draws what
    // appendQuads(evaluate()) would, also when the placement or tint changes.
    Node l_oStill; l_oStill.m_strName = "still"; l_oStill.m_strSprite = "game:s"; l_oStill.m_v3Pos = {30, 0, 0};
    Node l_oSpin; l_oSpin.m_strName = "spin"; l_oSpin.m_strSprite = "s"; l_oSpin.m_vRotKeys = {{0.0f, {0, 0, 0.0f}, {}}, {1.0f, {0, 0, 90.0f}, {}}};
    l_oC.m_oRoot.m_vChildren = {l_oStill, l_oSpin};
    l_oC.m_oRoot.m_vColorKeys = {{0.2f, {1, 1, 1, 1}, {}}, {0.4f, {1, 1, 1, 0}, {}}, {0.7f, {1, 1, 1, 1}, {}}};
    l_oC.m_fLength = 1.0f;
    l_oC.m_iPlayCount = -1;
    l_oP.f_Play(&l_oC);
    bool l_bSame = true;
    for (int l_iF = 0; l_iF < 140; l_iF++) {
        const glm::mat3 l_mat3Place = f_Placement(l_iF < 70 ? 0.0f : 5.0f, 0);
        const float l_arrHalf[4] = {1, 1, 1, 0.5f};
        const float* l_pTint = l_iF >= 100 ? l_arrHalf : nullptr;
        l_oRen.m_vQuads.clear();
        l_oP.f_Draw(&l_oRen, l_oSet, l_mat3Place, l_pTint);
        f_Evaluate(l_oC, l_oP.f_Time(), l_vPoses);
        l_vQ.clear();
        f_AppendQuads(l_vPoses, l_oSet, l_mat3Place, l_pTint, l_vQ);
        bool l_bFrame = l_oRen.m_vQuads.size() == l_vQ.size();
        for (size_t l_nI = 0; l_bFrame && l_nI < l_vQ.size(); l_nI++) {
            l_bFrame = l_vQ[l_nI].m_u16Texture == l_oRen.m_vQuads[l_nI].m_u16Texture && l_vQ[l_nI].m_bAdditive == l_oRen.m_vQuads[l_nI].m_bAdditive;
            for (int l_iK = 0; l_iK < 8; l_iK++) l_bFrame = l_bFrame && f_Near(l_vQ[l_nI].m_arrCorners[l_iK], l_oRen.m_vQuads[l_nI].m_arrCorners[l_iK], 1e-3f);
            for (int l_iK = 0; l_iK < 4; l_iK++) l_bFrame = l_bFrame && f_Near(l_vQ[l_nI].m_arrTint[l_iK], l_oRen.m_vQuads[l_nI].m_arrTint[l_iK], 1e-5f) && l_vQ[l_nI].m_arrUv[l_iK] == l_oRen.m_vQuads[l_nI].m_arrUv[l_iK];
        }
        if (!l_bFrame) fprintf(stderr, "  frame %d (t=%f): player %zu quads, reference %zu\n", l_iF, l_oP.f_Time(), l_oRen.m_vQuads.size(), l_vQ.size());
        l_bSame = l_bSame && l_bFrame;
        l_oP.f_Update(16);
    }
    CHECK(l_bSame, "player's cached quads == appendQuads(evaluate()) every frame");
}

void f_TestJson() {
    AnimFile l_oF;
    l_oF.m_vAtlases = {{"game", "../media/atlas/game.atlas"}, {"dark16", "../styles/dark16/atlas/game.atlas"}, {"fx", "../fx/fx.atlas"}};
    Clip l_oC = f_TreeClip();
    l_oC.m_iPlayCount = -1;
    l_oC.m_fLength = 2;
    l_oC.m_oRoot.m_vChildren[1].m_vPosKeys = {{0.0f, {1, 2, 0}, f_Ease("backOut")}, {0.5f, {3, 4, 0}, {}}};
    Ease l_oBz; l_oBz.m_iKind = Ease::m_iBezier; l_oBz.m_arrBezier[0] = 0.1f; l_oBz.m_arrBezier[3] = 0.9f;
    l_oC.m_oRoot.m_vChildren[1].m_vRotKeys = {{0.0f, {0, 0, 0.0f}, l_oBz}, {1.0f, {0, 0, 90.0f}, {}}};
    l_oC.m_oRoot.m_vChildren[1].m_vSpriteKeys = {{0.0f, "s", {}}, {0.2f, "t", {}}};
    l_oC.m_oRoot.m_vChildren[1].m_vEventKeys = {{0.2f, "hit", {}}};
    l_oC.m_oRoot.m_vChildren[1].m_vVisibleKeys = {{0.0f, true, {}}, {0.9f, false, {}}};
    l_oF.m_vClips.push_back(l_oC);
    const std::string l_strText = f_AnimToJson(l_oF);
    AnimFile l_oBack;
    std::string l_strErr;
    CHECK(f_ParseAnim(l_strText, l_oBack, &l_strErr), "parse: %s", l_strErr.c_str());
    CHECK(f_AnimToJson(l_oBack) == l_strText, "round trip is exact");
    CHECK(l_oBack.f_Find("tree") && l_oBack.f_Find("tree")->m_iPlayCount == -1 && !l_oBack.f_Find("other"), "find");
    CHECK(l_oBack.m_vAtlases == l_oF.m_vAtlases, "atlases and their ids kept in order");
    CHECK(l_strText.find("\"../fx/fx.atlas\"") != std::string::npos && l_strText.find("\"id\": \"dark16\"") != std::string::npos,
          "default ids written as plain paths, others as {id, path}");
    AnimFile l_oLegacy;
    CHECK(f_ParseAnim(R"({"atlas":"old/game.atlas","clips":[]})", l_oLegacy, &l_strErr) && l_oLegacy.m_vAtlases.size() == 1 &&
              l_oLegacy.m_vAtlases[0].m_strId == "game" && l_oLegacy.m_vAtlases[0].m_strPath == "old/game.atlas", "an older single \"atlas\" reads as a list");
    CHECK(!f_ParseAnim(R"({"atlases":["a/game.atlas","b/game.atlas"],"clips":[]})", l_oLegacy, &l_strErr) && l_strErr.find("game") != std::string::npos,
          "two atlases with the same id are an error: %s", l_strErr.c_str());
    const SpriteRef l_oSr = f_ParseSpriteRef("fx:slash_0");
    CHECK(l_oSr.m_strAtlas == "fx" && l_oSr.m_strName == "slash_0" && f_ParseSpriteRef("coin").m_strAtlas.empty() && f_SpriteRef("fx", "a") == "fx:a" &&
              f_SpriteRef("", "a") == "a" && f_DefaultAtlasId("x/y/fx.atlas") == "fx", "sprite references");
    const Node& l_oBody = l_oBack.m_vClips[0].m_oRoot.m_vChildren[1];
    CHECK(l_oBody.m_vPosKeys.size() == 2 && f_EaseName(l_oBody.m_vPosKeys[0].m_oEase) == "backOut", "ease kept");
    CHECK(l_oBody.m_vRotKeys.size() == 2 && l_oBody.m_vRotKeys[0].m_oEase.m_iKind == Ease::m_iBezier && f_Near(l_oBody.m_vRotKeys[0].m_oEase.m_arrBezier[0], 0.1f), "bezier kept");
    // Keys come back sorted; bad input is an error, not a crash.
    AnimFile l_oSorted;
    CHECK(f_ParseAnim(R"({"clips":[{"name":"x","root":{"name":"r","tracks":{"pos":[{"t":1,"v":[1,1]},{"t":0,"v":[0,0]}]}}}]})", l_oSorted, &l_strErr) &&
              l_oSorted.m_vClips[0].m_oRoot.m_vPosKeys[0].m_fT == 0, "keys sorted by time");
    CHECK(!f_ParseAnim(R"({"clips":[{"name":"x","root":{"tracks":{"pos":[{"t":0,"v":[0,0],"ease":"wobbly"}]}}}]})", l_oSorted, &l_strErr) &&
              l_strErr.find("wobbly") != std::string::npos, "unknown ease: %s", l_strErr.c_str());
    CHECK(!f_ParseAnim("{not json", l_oSorted, &l_strErr), "invalid JSON");
}

void f_TestXyz() {
    // x tilt 60 degrees: the node's y axis leans into the screen, so it shows at half its height.
    CHECK(f_Near2(f_Apply(f_Compose({0, 0, 0}, {60, 0, 0}, {1, 1, 1}), 0, 10), 0, 5), "x tilt foreshortens y");
    CHECK(f_Near2(f_Apply(f_Compose({0, 0, 0}, {0, 60, 0}, {1, 1, 1}), 10, 0), 5, 0), "y tilt foreshortens x");
    // The order: z, then y, then x (Rx * Ry * Rz). With x = y = 90 the node's x axis ends up on screen y;
    // Unity's order (Ry * Rx * Rz) would send it into the screen.
    const glm::mat4 l_mat4Xy = f_Compose({0, 0, 0}, {90, 90, 0}, {1, 1, 1});
    CHECK(f_Near2(f_Apply(l_mat4Xy, 10, 0), 0, 10, 1e-4f), "rotation order Rx * Ry * Rz: (%f, %f)", f_Apply(l_mat4Xy, 10, 0).x, f_Apply(l_mat4Xy, 10, 0).y);
    // The FM79979 engine's cMatrix44::RotationMatrix(30, 45, 10), its first row: the image of x.
    const float l_fCx = std::cos(glm::radians(30.0f)), l_fSx = std::sin(glm::radians(30.0f)), l_fCy = std::cos(glm::radians(45.0f)),
                l_fSy = std::sin(glm::radians(45.0f)), l_fCz = std::cos(glm::radians(10.0f)), l_fSz = std::sin(glm::radians(10.0f));
    const glm::mat4 l_mat4E = f_Compose({0, 0, 0}, {30, 45, 10}, {1, 1, 1});
    CHECK(f_Near(l_mat4E[0][0], l_fCy * l_fCz) && f_Near(l_mat4E[0][1], l_fCx * l_fSz + l_fSx * l_fSy * l_fCz) && f_Near(l_mat4E[0][2], l_fSx * l_fSz - l_fCx * l_fSy * l_fCz) &&
              f_Near(l_mat4E[2][0], l_fSy) && f_Near(l_mat4E[2][1], -l_fSx * l_fCy) && f_Near(l_mat4E[2][2], l_fCx * l_fCy),
          "same matrix as the old engine's RotationMatrix");
    // z: depth in the world, no move on the 2D screen.
    Node l_oDeep;
    l_oDeep.m_v3Pos = {5, 6, 50};
    l_oDeep.m_vPosKeys = {{0.0f, {5, 6, 50}, {}}, {1.0f, {5, 6, 150}, {}}};
    const glm::mat4 l_mat4W = f_LocalTransform(l_oDeep, 0.5f);
    CHECK(f_Near(l_mat4W[3].z, 100.0f) && f_Near2(f_Apply(l_mat4W, 0, 0), 5, 6), "pos z interpolates in the world, the screen ignores it");
    // Version 1 files.
    AnimFile l_oV1;
    std::string l_strErr;
    CHECK(f_ParseAnim(R"({"version":1,"clips":[{"name":"x","root":{"name":"r","pos":[1,2],"rot":30,"scale":[2,3],)"
                    R"("tracks":{"pos":[{"t":0,"v":[5,6]}],"rot":[{"t":0,"v":45}],"scale":[{"t":0,"v":[1,2]}]}}}]})",
                    l_oV1, &l_strErr),
          "version 1 parses: %s", l_strErr.c_str());
    const Node& l_oR = l_oV1.m_vClips.empty() ? l_oDeep : l_oV1.m_vClips[0].m_oRoot;
    CHECK(l_oV1.m_iVersion == 1 && l_oR.m_v3Pos == glm::vec3(1, 2, 0) && l_oR.m_v3Rot == glm::vec3(0, 0, 30) && l_oR.m_v3Scale == glm::vec3(2, 3, 1),
          "version 1 rest: z = 0, rot = the z turn, scale z = 1");
    CHECK(l_oR.m_vPosKeys.size() == 1 && l_oR.m_vPosKeys[0].m_oV == glm::vec3(5, 6, 0) && l_oR.m_vRotKeys.size() == 1 &&
              l_oR.m_vRotKeys[0].m_oV == glm::vec3(0, 0, 45) && l_oR.m_vScaleKeys.size() == 1 && l_oR.m_vScaleKeys[0].m_oV == glm::vec3(1, 2, 1),
          "version 1 keys likewise");
    const std::string l_strV2 = f_AnimToJson(l_oV1);
    AnimFile l_oBack;
    CHECK(l_strV2.find("\"version\": 2") != std::string::npos && f_ParseAnim(l_strV2, l_oBack, &l_strErr) && l_oBack == l_oV1 && l_oBack.m_iVersion == 2,
          "saved as version 2, reads back the same");
    CHECK(!f_ParseAnim(R"({"version":5,"clips":[]})", l_oBack, &l_strErr) && l_strErr.find("newer") != std::string::npos,   // 3 = skins, 4 = effects
          "a newer version is refused: %s", l_strErr.c_str());
    // == (the editor's undo compares with it)
    AnimFile l_oChanged = l_oV1;
    CHECK(l_oChanged == l_oV1, "a copy is equal");
    l_oChanged.m_vClips[0].m_oRoot.m_vRotKeys[0].m_oV.x = 1;
    CHECK(l_oChanged != l_oV1, "a key's rot x differs");
    l_oChanged = l_oV1;
    l_oChanged.m_vClips[0].m_oRoot.m_vRotKeys[0].m_oEase.m_iKind = Ease::m_iBezier;
    CHECK(l_oChanged != l_oV1, "an ease differs");
}

}  // namespace

int main() {
    f_TestEasing();
    f_TestTracks();
    f_TestTree();
    f_TestPoseCache();
    f_TestPlayer();
    f_TestChildOutlivesParent();
    f_TestNodePlayback();
    f_TestQuads();
    f_TestJson();
    f_TestXyz();
    std::printf("anim_clip_test: %d check(s), %d failed\n", g_iChecks, g_iFails);
    return g_iFails ? 1 : 0;
}
