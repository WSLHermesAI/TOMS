// gltf_physics.h -- rigid-body simulation of a glTF model's KHR_physics_rigid_bodies + KHR_implicit_shapes
// (read by gltf_model into Model::physics), on Jolt Physics. docs/18_GLTF.md §7.
//
//   PhysicsWorld world;
//   pose.updateWorld();
//   world.build(model, pose);         // bodies, colliders, triggers and joints where the pose puts them
//   every frame: pose.apply(...)       (animations, if any; an animated body follows them, as a kinematic body)
//                world.step(dt, pose); // fixed 1/60 s steps; the moving bodies are written into the pose
//
// What the file's data becomes:
//   * a node with `motion` is one rigid body: its own collider plus the colliders of its descendants that have no
//     `motion` of their own (a compound). isKinematic, or a mass of 0, makes it kinematic: it moves at its own
//     velocity and is not pushed. A node with a collider and no `motion` in its ancestry is a static collider.
//   * an animated node (an animation channel on it or an ancestor) is moved by the animation, never by physics.
//   * a `trigger` is a sensor: takeEvents() reports what enters and leaves it (a compound trigger reports its own
//     node). It follows the body it sits in.
//   * a `joint` constrains the body of its node to the body of `connectedNode`; the two nodes are the joint frames.
//     Limits and drives map to a six-degrees-of-freedom constraint, one axis at a time.
// Approximations (Jolt has no direct equivalent): one friction value per body (dynamicFriction), and the
// material / filter of a compound's first collider for the whole body; a limit on 2 or 3 linear axes is a box, not a
// circle / sphere; angular limits are hard (no spring); a dynamic triangle-mesh collider becomes its convex hull; an
// inertia component of 0 ("infinite") becomes a very large one.
#pragma once
#include "gltf_model.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace toms::gltf {

// Something entered (enter) or left a trigger. Node indices: the trigger (the compound trigger's node, if it is part
// of one) and the body (the node with `motion`, or the static collider's node).
struct TriggerEvent {
    int trigger = -1;
    int other = -1;
    bool enter = true;
};

// A box around one body's collision shape, for drawing (PhysicsWorld::debugBoxes).
struct PhysicsDebugBox {
    glm::mat4 world{1.0f};               // the box's centre and rotation
    glm::vec3 halfExtents{0.5f};
    enum class Kind { Static, Kinematic, Dynamic, Trigger } kind = Kind::Static;
    bool sleeping = false;
    int node = -1;
};

class PhysicsWorld {
public:
    static constexpr float kStep = 1.0f / 60.0f;   // the simulation's fixed time step, seconds

    struct Stats {
        int dynamic = 0, kinematic = 0, statics = 0, triggers = 0, joints = 0;
        int awake = 0;                     // dynamic bodies not asleep
    };

    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Creates everything from model.physics at the pose's current world transforms (call pose.updateWorld() first).
    // False (+ error) when the model has no physics. The model must outlive the world.
    bool build(const Model& model, const Pose& pose, std::string* error = nullptr);
    void clear();
    bool built() const;

    // Advances by dt seconds in fixed kStep steps (the remainder carries over to the next call), at most maxSteps
    // of them (0 = no limit). Writes every moving body into the pose (local transforms) and updates its world.
    void step(float dt, Pose& pose, int maxSteps = 8);
    // Writes every moving body into the pose (local transforms + world), without stepping: after something reset the
    // pose (an animation applied from the rest pose), so the bodies stay where the simulation has them.
    void writePose(Pose& pose) const;
    // Simulates from the current time up to `seconds` (no step limit): a frozen time for tests and screenshots.
    void simulateTo(double seconds, Pose& pose);
    double time() const;                   // simulated seconds since build()
    int steps() const;

    void setGravity(const glm::vec3& g);   // default (0, -9.81, 0): glTF is metres, +Y up
    glm::vec3 gravity() const;

    std::vector<TriggerEvent> takeEvents();          // since the last call
    std::vector<int> inside(int triggerNode) const;  // the bodies (nodes) inside a trigger now

    bool isSleeping(int node) const;                 // a body node that has come to rest
    glm::vec3 linearVelocity(int node) const;        // world space, m/s (0 for a node without a body)

    Stats stats() const;
    const std::vector<std::string>& warnings() const;   // what was approximated or skipped when building
    void debugBoxes(std::vector<PhysicsDebugBox>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace toms::gltf
