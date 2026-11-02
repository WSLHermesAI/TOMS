// gltf_physics_test.cpp -- KHR_physics_rigid_bodies + KHR_implicit_shapes: reading (gltf_model) and simulating
// (gltf_physics, Jolt), headless, on tests/gltf/physics_scene.gltf (tools/gltf_viewer/make_test_models.py).
//
//   reading     shapes and their defaults, materials, filters, joints, motion, colliders, triggers
//   falling     a box comes to rest on the ground at its half height; a body asleep
//   bounce      the restitution-0.8 sphere rises again after hitting the ground
//   shapes      capsule, cone, compound (its parts keep their places) and convex hull all land
//   filters     the "ghost" box falls through the ground its filter skips
//   kinematic   the platform moves at exactly its velocity, never falls
//   triggers    the falling box enters, then leaves, the trigger zone
//   joints      the pendulum keeps its length while it swings
//   animation   the animated sweeper's collider follows the animation (kinematic)
//   determinism two runs give the same transforms bit for bit
//
// Run: ./toms_tests gltf_physics_test   (in assets/: the models are ../tests/gltf/)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "gltf_model.h"
#include "gltf_physics.h"

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

using namespace toms::gltf;

namespace {

bool f_Load(Model& l_oM) {
    std::string l_strErr;
    for (const char* l_pszDir : {"../tests/gltf/", "tests/gltf/"})
        if (f_LoadModel(std::string(l_pszDir) + "physics_scene.gltf", l_oM, &l_strErr)) return true;
    fprintf(stderr, "  physics_scene.gltf: %s\n", l_strErr.c_str());
    return false;
}

int f_NodeNamed(const Model& l_oM, const char* l_pszName) {
    for (size_t l_nI = 0; l_nI < l_oM.m_vNodes.size(); l_nI++)
        if (l_oM.m_vNodes[l_nI].m_strName == l_pszName) return (int)l_nI;
    return -1;
}

glm::vec3 f_WorldPos(const Pose& l_oP, int l_iN) { return glm::vec3(l_oP.f_World(l_iN)[3]); }

}  // namespace

int main() {
    Model l_oM;
    if (!f_Load(l_oM)) {
        CHECK(false, "tests/gltf/physics_scene.gltf loads (python tools/gltf_viewer/make_test_models.py)");
        printf("gltf_physics_test: %d checks, %d failed\n", g_iChecks, g_iFails);
        return 1;
    }
    const int l_iGround = f_NodeNamed(l_oM, "ground"), l_iBox = f_NodeNamed(l_oM, "box_drop"), l_iBall = f_NodeNamed(l_oM, "ball_bouncy");
    const int l_iCapsule = f_NodeNamed(l_oM, "capsule"), l_iCone = f_NodeNamed(l_oM, "cone"), l_iCompound = f_NodeNamed(l_oM, "compound");
    const int l_iPartA = f_NodeNamed(l_oM, "compound_a"), l_iPartB = f_NodeNamed(l_oM, "compound_b"), l_iConvex = f_NodeNamed(l_oM, "convex");
    const int l_iGhost = f_NodeNamed(l_oM, "ghost"), l_iPlatform = f_NodeNamed(l_oM, "platform"), l_iZone = f_NodeNamed(l_oM, "trigger_zone");
    const int l_iAnchor = f_NodeNamed(l_oM, "anchor"), l_iBob = f_NodeNamed(l_oM, "bob"), l_iSweeper = f_NodeNamed(l_oM, "sweeper");

    // ---- reading ----
    const Physics& l_oPh = l_oM.m_oPhysics;
    CHECK(l_oPh.f_Any() && l_oPh.m_vNodes.size() == l_oM.m_vNodes.size(), "the model has physics, one entry per node");
    CHECK(std::find(l_oM.m_vIgnoredExtensions.begin(), l_oM.m_vIgnoredExtensions.end(), "KHR_physics_rigid_bodies") == l_oM.m_vIgnoredExtensions.end(),
          "KHR_physics_rigid_bodies is read, not ignored");
    CHECK(l_oPh.m_vMaterials.size() == 2 && l_oPh.m_vMaterials[1].m_fRestitution > 0.79f &&
              l_oPh.m_vMaterials[1].m_eRestitutionCombine == PhysicsCombine::Maximum && l_oPh.m_vMaterials[0].m_eFrictionCombine == PhysicsCombine::Unset,
          "physics materials (restitution, combine modes)");
    CHECK(l_oPh.m_vFilters.size() == 2 && l_oPh.m_vFilters[1].m_vSystems == std::vector<std::string>{"ghost"} &&
              l_oPh.m_vFilters[1].m_vNotCollideWith == std::vector<std::string>{"world"},
          "collision filters");
    CHECK(l_oPh.m_vJoints.size() == 1 && l_oPh.m_vJoints[0].m_vLimits.size() == 1 && l_oPh.m_vJoints[0].m_vLimits[0].m_vLinearAxes.size() == 3 &&
              l_oPh.m_vJoints[0].m_vLimits[0].m_bHasMin && l_oPh.m_vJoints[0].m_vLimits[0].m_fStiffness < 0.0f,
          "the ball joint's limit");
    {
        const NodePhysics& l_oB = l_oPh.m_vNodes[(size_t)l_iBox];
        CHECK(l_oB.m_bHasMotion && !l_oB.m_bKinematic && l_oB.m_fMass == 2.0f && l_oB.m_bHasCollider && l_oB.m_oCollider.m_iShape >= 0, "box_drop: motion (2 kg) + box collider");
        const PhysicsShape& l_oS = l_oPh.m_vShapes[(size_t)l_oB.m_oCollider.m_iShape];
        CHECK(l_oS.m_eType == PhysicsShape::Type::Box && l_oS.m_v3Size == glm::vec3(1.0f), "a 1 m box shape (full extents)");
        const NodePhysics& l_oC = l_oPh.m_vNodes[(size_t)l_iCone];
        const PhysicsShape& l_oCs = l_oPh.m_vShapes[(size_t)l_oC.m_oCollider.m_iShape];
        CHECK(l_oCs.m_eType == PhysicsShape::Type::Cylinder && l_oCs.m_fRadiusTop == 0.0f && l_oCs.m_fRadiusBottom == 0.5f, "a cone: a cylinder with a 0 top radius");
        CHECK(l_oPh.m_vNodes[(size_t)l_iPlatform].m_bKinematic && l_oPh.m_vNodes[(size_t)l_iPlatform].m_v3LinearVelocity.x == 0.5f, "the kinematic platform's velocity");
        CHECK(l_oPh.m_vNodes[(size_t)l_iZone].m_bHasTrigger && !l_oPh.m_vNodes[(size_t)l_iZone].m_bHasCollider, "the trigger zone has a trigger, no collider");
        CHECK(l_oPh.m_vNodes[(size_t)l_iConvex].m_oCollider.m_iMesh >= 0 && l_oPh.m_vNodes[(size_t)l_iConvex].m_oCollider.m_bConvexHull, "a convex-hull mesh collider");
        CHECK(l_oPh.m_vNodes[(size_t)l_iCompound].m_bHasMotion && !l_oPh.m_vNodes[(size_t)l_iCompound].m_bHasCollider &&
                  l_oPh.m_vNodes[(size_t)l_iPartA].m_bHasCollider && !l_oPh.m_vNodes[(size_t)l_iPartA].m_bHasMotion,
              "a compound: motion on the parent, colliders on the children");
        const int l_iPivot = f_NodeNamed(l_oM, "anchor_pivot");
        CHECK(l_iPivot >= 0 && l_oPh.m_vNodes[(size_t)l_iPivot].m_bHasJoint && l_oPh.m_vNodes[(size_t)l_iPivot].m_iConnectedNode == f_NodeNamed(l_oM, "bob_pivot"),
              "the joint connects the two pivots");
        CHECK(!l_oPh.m_vNodes[(size_t)l_iGround].m_bHasMotion && l_oPh.m_vNodes[(size_t)l_iGround].m_iFilter == 0, "the ground: static, filter 0");
    }

    // ---- simulating ----
    Pose l_oPose;
    l_oPose.f_Bind(&l_oM);
    l_oPose.f_UpdateWorld();
    PhysicsWorld l_oWorld;
    std::string l_strErr;
    CHECK(l_oWorld.f_Build(l_oM, l_oPose, &l_strErr), "the world builds: %s", l_strErr.c_str());
    for (const std::string& l_strW : l_oWorld.f_Warnings()) fprintf(stderr, "  (warning) %s\n", l_strW.c_str());
    CHECK(l_oWorld.f_Warnings().empty(), "no warnings building the test scene");
    const PhysicsWorld::Stats l_oSt = l_oWorld.f_Stats();
    CHECK(l_oSt.m_iDynamic == 8 && l_oSt.m_iKinematic == 2 && l_oSt.m_iStatics == 2 && l_oSt.m_iTriggers == 1 && l_oSt.m_iJoints == 1,
          "bodies: %d dynamic, %d kinematic, %d static, %d triggers, %d joints", l_oSt.m_iDynamic, l_oSt.m_iKinematic, l_oSt.m_iStatics, l_oSt.m_iTriggers, l_oSt.m_iJoints);

    const glm::vec3 l_v3Platform0 = f_WorldPos(l_oPose, l_iPlatform);
    const glm::mat4 l_mat4PartRel0 = glm::inverse(l_oPose.f_World(l_iPartA)) * l_oPose.f_World(l_iPartB);
    const float l_fBobLength0 = glm::length(f_WorldPos(l_oPose, l_iBob) - f_WorldPos(l_oPose, l_iAnchor));

    // the ball's lowest point, then the height it climbs back to after the first bounce
    float l_fBallMin = 1e9f, l_fBallAfter = -1e9f;
    bool l_bBounced = false;
    float l_fBobMinX = 1e9f, l_fBobLengthMaxErr = 0.0f;
    std::vector<TriggerEvent> l_vEvents;
    for (int l_iI = 1; l_iI <= 240; l_iI++) {   // 4 s
        l_oPose.f_Reset();
        l_oPose.f_Apply(0, std::fmod(l_iI * PhysicsWorld::m_fStep, 4.0f));   // the sweeper's animation
        l_oWorld.f_Step(PhysicsWorld::m_fStep, l_oPose);
        const float l_fY = f_WorldPos(l_oPose, l_iBall).y;
        if (!l_bBounced) {
            if (l_fY < l_fBallMin) l_fBallMin = l_fY;
            else if (l_fBallMin < 0.6f) l_bBounced = true;
        } else {
            l_fBallAfter = std::max(l_fBallAfter, l_fY);
        }
        l_fBobMinX = std::min(l_fBobMinX, f_WorldPos(l_oPose, l_iBob).x);
        l_fBobLengthMaxErr = std::max(l_fBobLengthMaxErr, std::fabs(glm::length(f_WorldPos(l_oPose, l_iBob) - f_WorldPos(l_oPose, l_iAnchor)) - l_fBobLength0));
        for (const TriggerEvent& l_oE : l_oWorld.f_TakeEvents()) l_vEvents.push_back(l_oE);
        if (l_iI == 120) {   // 2 s
            CHECK(f_WorldPos(l_oPose, l_iGhost).y < -2.0f, "the ghost fell through the ground (y %.2f)", f_WorldPos(l_oPose, l_iGhost).y);
            const float l_fExpect = l_v3Platform0.x + 0.5f * 2.0f;
            CHECK(std::fabs(f_WorldPos(l_oPose, l_iPlatform).x - l_fExpect) < 1e-3f && std::fabs(f_WorldPos(l_oPose, l_iPlatform).y - l_v3Platform0.y) < 1e-4f,
                  "the kinematic platform moved 1 m in 2 s, level (x %.4f, want %.4f)", f_WorldPos(l_oPose, l_iPlatform).x, l_fExpect);
            const glm::mat4 l_mat4Sweep = l_oPose.f_World(l_iSweeper);
            CHECK(std::fabs(l_mat4Sweep[0][0] - (-1.0f)) < 0.05f, "the sweeper follows its animation (half a turn at 2 s)");
        }
    }
    CHECK(l_oWorld.f_Steps() == 240 && std::fabs(l_oWorld.f_Time() - 4.0) < 1e-5, "240 fixed steps in 4 s (%d)", l_oWorld.f_Steps());
    const glm::vec3 l_v3BoxEnd = f_WorldPos(l_oPose, l_iBox);
    // Jolt lets resting contacts sink up to its penetration slop (2 cm), so the 1 m box rests at 0.48..0.50.
    CHECK(l_v3BoxEnd.y > 0.47f && l_v3BoxEnd.y < 0.505f && std::fabs(l_v3BoxEnd.x + 4.0f) < 0.05f, "the box rests on the ground (y %.3f)", l_v3BoxEnd.y);
    CHECK(l_bBounced && l_fBallAfter > 1.2f, "the bouncy sphere rose to %.2f m after its first bounce", l_fBallAfter);
    CHECK(f_WorldPos(l_oPose, l_iCapsule).y > 0.2f && f_WorldPos(l_oPose, l_iCapsule).y < 0.45f, "the capsule lies on the ground (y %.3f)", f_WorldPos(l_oPose, l_iCapsule).y);
    CHECK(f_WorldPos(l_oPose, l_iCone).y > 0.3f && f_WorldPos(l_oPose, l_iCone).y < 0.7f, "the cone stands on the ground (y %.3f)", f_WorldPos(l_oPose, l_iCone).y);
    CHECK(f_WorldPos(l_oPose, l_iConvex).y > 0.2f && f_WorldPos(l_oPose, l_iConvex).y < 0.7f, "the convex hull landed (y %.3f)", f_WorldPos(l_oPose, l_iConvex).y);
    CHECK(f_WorldPos(l_oPose, l_iCompound).y > 0.2f && f_WorldPos(l_oPose, l_iCompound).y < 1.6f, "the compound landed (y %.3f)", f_WorldPos(l_oPose, l_iCompound).y);
    {
        const glm::mat4 l_mat4Rel = glm::inverse(l_oPose.f_World(l_iPartA)) * l_oPose.f_World(l_iPartB);
        float l_fD = 0.0f;
        for (int l_iC = 0; l_iC < 4; l_iC++) l_fD = std::max(l_fD, glm::length(l_mat4Rel[l_iC] - l_mat4PartRel0[l_iC]));
        CHECK(l_fD < 1e-3f, "the compound's parts keep their places (%.5f)", l_fD);
    }
    CHECK(l_fBobMinX < 2.6f, "the pendulum swung (bob x down to %.2f)", l_fBobMinX);
    CHECK(l_fBobLengthMaxErr < 0.03f, "the joint keeps the pendulum's length (worst %.4f m)", l_fBobLengthMaxErr);
    bool l_bEntered = false, l_bLeft = false;
    for (const TriggerEvent& l_oE : l_vEvents) {
        if (l_oE.m_iTrigger == l_iZone && l_oE.m_iOther == l_iBox) (l_oE.m_bEnter ? l_bEntered : l_bLeft) = true;
    }
    CHECK(l_bEntered && l_bLeft, "the falling box entered and left the trigger zone (%zu events)", l_vEvents.size());
    CHECK(l_oWorld.f_IsSleeping(l_iBox), "the box is asleep once at rest");
    CHECK(l_oWorld.f_Inside(l_iZone).empty(), "nothing is inside the trigger zone at the end");

    // ---- determinism: the same build and steps give the same numbers ----
    {
        Pose l_oA, l_oB;
        l_oA.f_Bind(&l_oM); l_oA.f_UpdateWorld();
        l_oB.f_Bind(&l_oM); l_oB.f_UpdateWorld();
        PhysicsWorld l_oWa, l_oWb;
        l_oWa.f_Build(l_oM, l_oA);
        l_oWb.f_Build(l_oM, l_oB);
        l_oWa.f_SimulateTo(2.5, l_oA);
        for (int l_iI = 0; l_iI < 150; l_iI++) l_oWb.f_Step(PhysicsWorld::m_fStep, l_oB);
        bool l_bSame = l_oWa.f_Steps() == l_oWb.f_Steps();
        for (size_t l_nN = 0; l_nN < l_oM.m_vNodes.size() && l_bSame; l_nN++)
            l_bSame = std::memcmp(&l_oA.f_World((int)l_nN), &l_oB.f_World((int)l_nN), sizeof(glm::mat4)) == 0;
        CHECK(l_bSame, "simulateTo(2.5) and 150 single steps give identical transforms");
    }

    // ---- no physics: build refuses ----
    {
        Model l_oPlain;
        Pose l_oP;
        l_oP.f_Bind(&l_oPlain);
        PhysicsWorld l_oW;
        std::string l_strE;
        CHECK(!l_oW.f_Build(l_oPlain, l_oP, &l_strE) && !l_strE.empty(), "a model without physics does not build (%s)", l_strE.c_str());
    }

    printf("gltf_physics_test: %d checks, %d failed\n", g_iChecks, g_iFails);
    return g_iFails == 0 ? 0 : 1;
}
