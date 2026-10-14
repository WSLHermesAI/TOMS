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

static int g_checks = 0, g_fails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_fails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::gltf;

namespace {

bool load(Model& m) {
    std::string err;
    for (const char* dir : {"../tests/gltf/", "tests/gltf/"})
        if (loadModel(std::string(dir) + "physics_scene.gltf", m, &err)) return true;
    fprintf(stderr, "  physics_scene.gltf: %s\n", err.c_str());
    return false;
}

int nodeNamed(const Model& m, const char* name) {
    for (size_t i = 0; i < m.nodes.size(); i++)
        if (m.nodes[i].name == name) return (int)i;
    return -1;
}

glm::vec3 worldPos(const Pose& p, int n) { return glm::vec3(p.world(n)[3]); }

}  // namespace

int main() {
    Model m;
    if (!load(m)) {
        CHECK(false, "tests/gltf/physics_scene.gltf loads (python tools/gltf_viewer/make_test_models.py)");
        printf("gltf_physics_test: %d checks, %d failed\n", g_checks, g_fails);
        return 1;
    }
    const int ground = nodeNamed(m, "ground"), box = nodeNamed(m, "box_drop"), ball = nodeNamed(m, "ball_bouncy");
    const int capsule = nodeNamed(m, "capsule"), cone = nodeNamed(m, "cone"), compound = nodeNamed(m, "compound");
    const int partA = nodeNamed(m, "compound_a"), partB = nodeNamed(m, "compound_b"), convex = nodeNamed(m, "convex");
    const int ghost = nodeNamed(m, "ghost"), platform = nodeNamed(m, "platform"), zone = nodeNamed(m, "trigger_zone");
    const int anchor = nodeNamed(m, "anchor"), bob = nodeNamed(m, "bob"), sweeper = nodeNamed(m, "sweeper");

    // ---- reading ----
    const Physics& ph = m.physics;
    CHECK(ph.any() && ph.nodes.size() == m.nodes.size(), "the model has physics, one entry per node");
    CHECK(std::find(m.ignoredExtensions.begin(), m.ignoredExtensions.end(), "KHR_physics_rigid_bodies") == m.ignoredExtensions.end(),
          "KHR_physics_rigid_bodies is read, not ignored");
    CHECK(ph.materials.size() == 2 && ph.materials[1].restitution > 0.79f &&
              ph.materials[1].restitutionCombine == PhysicsCombine::Maximum && ph.materials[0].frictionCombine == PhysicsCombine::Unset,
          "physics materials (restitution, combine modes)");
    CHECK(ph.filters.size() == 2 && ph.filters[1].systems == std::vector<std::string>{"ghost"} &&
              ph.filters[1].notCollideWith == std::vector<std::string>{"world"},
          "collision filters");
    CHECK(ph.joints.size() == 1 && ph.joints[0].limits.size() == 1 && ph.joints[0].limits[0].linearAxes.size() == 3 &&
              ph.joints[0].limits[0].hasMin && ph.joints[0].limits[0].stiffness < 0.0f,
          "the ball joint's limit");
    {
        const NodePhysics& b = ph.nodes[(size_t)box];
        CHECK(b.hasMotion && !b.kinematic && b.mass == 2.0f && b.hasCollider && b.collider.shape >= 0, "box_drop: motion (2 kg) + box collider");
        const PhysicsShape& s = ph.shapes[(size_t)b.collider.shape];
        CHECK(s.type == PhysicsShape::Type::Box && s.size == glm::vec3(1.0f), "a 1 m box shape (full extents)");
        const NodePhysics& c = ph.nodes[(size_t)cone];
        const PhysicsShape& cs = ph.shapes[(size_t)c.collider.shape];
        CHECK(cs.type == PhysicsShape::Type::Cylinder && cs.radiusTop == 0.0f && cs.radiusBottom == 0.5f, "a cone: a cylinder with a 0 top radius");
        CHECK(ph.nodes[(size_t)platform].kinematic && ph.nodes[(size_t)platform].linearVelocity.x == 0.5f, "the kinematic platform's velocity");
        CHECK(ph.nodes[(size_t)zone].hasTrigger && !ph.nodes[(size_t)zone].hasCollider, "the trigger zone has a trigger, no collider");
        CHECK(ph.nodes[(size_t)convex].collider.mesh >= 0 && ph.nodes[(size_t)convex].collider.convexHull, "a convex-hull mesh collider");
        CHECK(ph.nodes[(size_t)compound].hasMotion && !ph.nodes[(size_t)compound].hasCollider &&
                  ph.nodes[(size_t)partA].hasCollider && !ph.nodes[(size_t)partA].hasMotion,
              "a compound: motion on the parent, colliders on the children");
        const int pivot = nodeNamed(m, "anchor_pivot");
        CHECK(pivot >= 0 && ph.nodes[(size_t)pivot].hasJoint && ph.nodes[(size_t)pivot].connectedNode == nodeNamed(m, "bob_pivot"),
              "the joint connects the two pivots");
        CHECK(!ph.nodes[(size_t)ground].hasMotion && ph.nodes[(size_t)ground].filter == 0, "the ground: static, filter 0");
    }

    // ---- simulating ----
    Pose pose;
    pose.bind(&m);
    pose.updateWorld();
    PhysicsWorld world;
    std::string err;
    CHECK(world.build(m, pose, &err), "the world builds: %s", err.c_str());
    for (const std::string& w : world.warnings()) fprintf(stderr, "  (warning) %s\n", w.c_str());
    CHECK(world.warnings().empty(), "no warnings building the test scene");
    const PhysicsWorld::Stats st = world.stats();
    CHECK(st.dynamic == 8 && st.kinematic == 2 && st.statics == 2 && st.triggers == 1 && st.joints == 1,
          "bodies: %d dynamic, %d kinematic, %d static, %d triggers, %d joints", st.dynamic, st.kinematic, st.statics, st.triggers, st.joints);

    const glm::vec3 platform0 = worldPos(pose, platform);
    const glm::mat4 partRel0 = glm::inverse(pose.world(partA)) * pose.world(partB);
    const float bobLength0 = glm::length(worldPos(pose, bob) - worldPos(pose, anchor));

    // the ball's lowest point, then the height it climbs back to after the first bounce
    float ballMin = 1e9f, ballAfter = -1e9f;
    bool bounced = false;
    float bobMinX = 1e9f, bobLengthMaxErr = 0.0f;
    std::vector<TriggerEvent> events;
    for (int i = 1; i <= 240; i++) {   // 4 s
        pose.reset();
        pose.apply(0, std::fmod(i * PhysicsWorld::kStep, 4.0f));   // the sweeper's animation
        world.step(PhysicsWorld::kStep, pose);
        const float y = worldPos(pose, ball).y;
        if (!bounced) {
            if (y < ballMin) ballMin = y;
            else if (ballMin < 0.6f) bounced = true;
        } else {
            ballAfter = std::max(ballAfter, y);
        }
        bobMinX = std::min(bobMinX, worldPos(pose, bob).x);
        bobLengthMaxErr = std::max(bobLengthMaxErr, std::fabs(glm::length(worldPos(pose, bob) - worldPos(pose, anchor)) - bobLength0));
        for (const TriggerEvent& e : world.takeEvents()) events.push_back(e);
        if (i == 120) {   // 2 s
            CHECK(worldPos(pose, ghost).y < -2.0f, "the ghost fell through the ground (y %.2f)", worldPos(pose, ghost).y);
            const float expect = platform0.x + 0.5f * 2.0f;
            CHECK(std::fabs(worldPos(pose, platform).x - expect) < 1e-3f && std::fabs(worldPos(pose, platform).y - platform0.y) < 1e-4f,
                  "the kinematic platform moved 1 m in 2 s, level (x %.4f, want %.4f)", worldPos(pose, platform).x, expect);
            const glm::mat4 sweep = pose.world(sweeper);
            CHECK(std::fabs(sweep[0][0] - (-1.0f)) < 0.05f, "the sweeper follows its animation (half a turn at 2 s)");
        }
    }
    CHECK(world.steps() == 240 && std::fabs(world.time() - 4.0) < 1e-5, "240 fixed steps in 4 s (%d)", world.steps());
    const glm::vec3 boxEnd = worldPos(pose, box);
    // Jolt lets resting contacts sink up to its penetration slop (2 cm), so the 1 m box rests at 0.48..0.50.
    CHECK(boxEnd.y > 0.47f && boxEnd.y < 0.505f && std::fabs(boxEnd.x + 4.0f) < 0.05f, "the box rests on the ground (y %.3f)", boxEnd.y);
    CHECK(bounced && ballAfter > 1.2f, "the bouncy sphere rose to %.2f m after its first bounce", ballAfter);
    CHECK(worldPos(pose, capsule).y > 0.2f && worldPos(pose, capsule).y < 0.45f, "the capsule lies on the ground (y %.3f)", worldPos(pose, capsule).y);
    CHECK(worldPos(pose, cone).y > 0.3f && worldPos(pose, cone).y < 0.7f, "the cone stands on the ground (y %.3f)", worldPos(pose, cone).y);
    CHECK(worldPos(pose, convex).y > 0.2f && worldPos(pose, convex).y < 0.7f, "the convex hull landed (y %.3f)", worldPos(pose, convex).y);
    CHECK(worldPos(pose, compound).y > 0.2f && worldPos(pose, compound).y < 1.6f, "the compound landed (y %.3f)", worldPos(pose, compound).y);
    {
        const glm::mat4 rel = glm::inverse(pose.world(partA)) * pose.world(partB);
        float d = 0.0f;
        for (int c = 0; c < 4; c++) d = std::max(d, glm::length(rel[c] - partRel0[c]));
        CHECK(d < 1e-3f, "the compound's parts keep their places (%.5f)", d);
    }
    CHECK(bobMinX < 2.6f, "the pendulum swung (bob x down to %.2f)", bobMinX);
    CHECK(bobLengthMaxErr < 0.03f, "the joint keeps the pendulum's length (worst %.4f m)", bobLengthMaxErr);
    bool entered = false, left = false;
    for (const TriggerEvent& e : events) {
        if (e.trigger == zone && e.other == box) (e.enter ? entered : left) = true;
    }
    CHECK(entered && left, "the falling box entered and left the trigger zone (%zu events)", events.size());
    CHECK(world.isSleeping(box), "the box is asleep once at rest");
    CHECK(world.inside(zone).empty(), "nothing is inside the trigger zone at the end");

    // ---- determinism: the same build and steps give the same numbers ----
    {
        Pose a, b;
        a.bind(&m); a.updateWorld();
        b.bind(&m); b.updateWorld();
        PhysicsWorld wa, wb;
        wa.build(m, a);
        wb.build(m, b);
        wa.simulateTo(2.5, a);
        for (int i = 0; i < 150; i++) wb.step(PhysicsWorld::kStep, b);
        bool same = wa.steps() == wb.steps();
        for (size_t n = 0; n < m.nodes.size() && same; n++)
            same = std::memcmp(&a.world((int)n), &b.world((int)n), sizeof(glm::mat4)) == 0;
        CHECK(same, "simulateTo(2.5) and 150 single steps give identical transforms");
    }

    // ---- no physics: build refuses ----
    {
        Model plain;
        Pose p;
        p.bind(&plain);
        PhysicsWorld w;
        std::string e;
        CHECK(!w.build(plain, p, &e) && !e.empty(), "a model without physics does not build (%s)", e.c_str());
    }

    printf("gltf_physics_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
