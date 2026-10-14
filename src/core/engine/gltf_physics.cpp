// gltf_physics.cpp -- see gltf_physics.h. Jolt Physics does the simulation; this file turns the glTF data into Jolt
// bodies, shapes and constraints, and Jolt's bodies back into node transforms.
#include <Jolt/Jolt.h>   // first: every other Jolt header needs it

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/Plane.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCylinderShape.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "gltf_physics.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

namespace toms::gltf {
namespace {

// ---- Jolt set-up, once per process ------------------------------------------------------------------------

void initJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();   // kept for the life of the process
        JPH::RegisterTypes();
    });
}

namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;   // static colliders: never collide with each other
constexpr JPH::ObjectLayer kMoving = 1;   // kinematic, dynamic and every trigger
}
const JPH::BroadPhaseLayer kBpStatic(0), kBpMoving(1);

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return l == Layers::kStatic ? kBpStatic : kBpMoving; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l == kBpStatic ? "static" : "moving"; }
#endif
};
class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l, JPH::BroadPhaseLayer bp) const override { return l == Layers::kMoving || bp == kBpMoving; }
};
class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == Layers::kMoving || b == Layers::kMoving; }
};

// ---- maths between glm and Jolt ---------------------------------------------------------------------------

JPH::Vec3 J(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
JPH::Quat J(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
glm::vec3 G(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
glm::quat G(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

struct TRS {
    glm::vec3 t{0.0f}, s{1.0f};
    glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
};
TRS decompose(const glm::mat4& m) {
    TRS o;
    o.t = glm::vec3(m[3]);
    glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
    o.s = {glm::length(c0), glm::length(c1), glm::length(c2)};
    if (glm::dot(glm::cross(c0, c1), c2) < 0.0f) { o.s.x = -o.s.x; }
    const glm::mat3 rot(c0 / (o.s.x != 0.0f ? o.s.x : 1.0f), c1 / (o.s.y != 0.0f ? o.s.y : 1.0f), c2 / (o.s.z != 0.0f ? o.s.z : 1.0f));
    o.r = glm::normalize(glm::quat_cast(rot));
    return o;
}
glm::mat4 rt(const glm::vec3& t, const glm::quat& r) { return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r); }

// ---- friction / restitution combine (KHR: average > minimum > maximum > multiply; none: Jolt's defaults) -----
// A body's two combine modes travel in its user data (bits 32..39); the low 32 bits are its slot.
constexpr int kFrictionShift = 32, kRestitutionShift = 36;
PhysicsCombine modeOf(JPH::uint64 ud, int shift) { return PhysicsCombine((ud >> shift) & 0xF); }
float combine(float a, float b, PhysicsCombine ma, PhysicsCombine mb, bool friction) {
    auto has = [&](PhysicsCombine c) { return ma == c || mb == c; };
    if (has(PhysicsCombine::Average)) return 0.5f * (a + b);
    if (has(PhysicsCombine::Minimum)) return std::min(a, b);
    if (has(PhysicsCombine::Maximum)) return std::max(a, b);
    if (has(PhysicsCombine::Multiply)) return a * b;
    return friction ? std::sqrt(a * b) : std::max(a, b);
}
float combineFriction(const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) {
    return combine(a.GetFriction(), b.GetFriction(), modeOf(a.GetUserData(), kFrictionShift), modeOf(b.GetUserData(), kFrictionShift), true);
}
float combineRestitution(const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) {
    return combine(a.GetRestitution(), b.GetRestitution(), modeOf(a.GetUserData(), kRestitutionShift),
                   modeOf(b.GetUserData(), kRestitutionShift), false);
}

bool intersects(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    for (const std::string& x : a)
        if (std::find(b.begin(), b.end(), x) != b.end()) return true;
    return false;
}
// One side's filter lets the other's systems through.
bool lets(const PhysicsFilter& f, const PhysicsFilter& other) {
    if (!f.collideWith.empty()) return intersects(f.collideWith, other.systems);
    if (!f.notCollideWith.empty()) return !intersects(f.notCollideWith, other.systems);
    return true;
}

}  // namespace

// ---- the world ----------------------------------------------------------------------------------------------

struct PhysicsWorld::Impl {
    enum class Kind { Static, Kinematic, Dynamic, Trigger };
    struct Slot {
        JPH::BodyID id;
        int node = -1;                   // the body's node (the motion node, the static collider, the trigger)
        Kind kind = Kind::Static;
        bool animated = false;           // moved to the pose's transform every step
        bool writeBack = false;          // a moving body: written into the pose
        int follow = -1;                 // a trigger inside a moving body: that body's slot
        glm::mat4 followRel{1.0f};       // the trigger relative to the body it follows
        int report = -1;                 // a trigger: the node its events name
        int filter = -1;
        glm::vec3 worldScale{1.0f};      // the node's world scale at build (kept when writing back)
    };

    // Collision filters + joints' "do not collide" pairs: the group filter every body shares (group id = slot).
    class Filter final : public JPH::GroupFilter {
    public:
        Impl* w = nullptr;
        bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
            const JPH::uint32 sa = a.GetGroupID(), sb = b.GetGroupID();
            if (w->noCollide.count({std::min(sa, sb), std::max(sa, sb)})) return false;
            if (sa >= w->slots.size() || sb >= w->slots.size()) return true;
            const int fa = w->slots[sa].filter, fb = w->slots[sb].filter;
            if (fa < 0 || fb < 0) return true;   // a collider without a filter collides with everything
            const auto& fs = w->model->physics.filters;
            return lets(fs[(size_t)fa], fs[(size_t)fb]) && lets(fs[(size_t)fb], fs[(size_t)fa]);
        }
    };

    // Trigger enter / leave, counted per (trigger, body) pair: a compound touches through several sub-shapes.
    class Contacts final : public JPH::ContactListener {
    public:
        Impl* w = nullptr;
        void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override {
            if (a.IsSensor() == b.IsSensor()) return;
            const JPH::Body& sensor = a.IsSensor() ? a : b;
            const JPH::Body& other = a.IsSensor() ? b : a;
            w->touch((int)(sensor.GetUserData() & 0xFFFFFFFFu), (int)(other.GetUserData() & 0xFFFFFFFFu), +1);
        }
        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            const int s1 = w->slotOf(pair.GetBody1ID()), s2 = w->slotOf(pair.GetBody2ID());
            if (s1 < 0 || s2 < 0) return;
            const bool t1 = w->slots[(size_t)s1].kind == Kind::Trigger, t2 = w->slots[(size_t)s2].kind == Kind::Trigger;
            if (t1 == t2) return;
            w->touch(t1 ? s1 : s2, t1 ? s2 : s1, -1);
        }
    };

    const Model* model = nullptr;
    BroadPhaseLayers bpLayers;
    ObjectVsBroadPhase objVsBp;
    ObjectPairs objPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
    std::unique_ptr<JPH::PhysicsSystem> system;
    JPH::Ref<Filter> filter;
    Contacts contacts;

    std::vector<Slot> slots;
    std::unordered_map<JPH::uint32, int> slotById;
    std::vector<int> nodeBody;           // node -> the slot of its body (motion or static collider), -1
    std::vector<int> order;              // nodes, parents before children
    std::set<std::pair<JPH::uint32, JPH::uint32>> noCollide;
    std::vector<JPH::Ref<JPH::Constraint>> constraints;
    std::vector<std::string> warnings;

    std::mutex eventLock;
    std::map<std::pair<int, int>, int> inside;   // (trigger report node, body node) -> touching sub-shape pairs
    std::vector<TriggerEvent> events;

    double acc = 0.0;
    int stepCount = 0;
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};

    int slotOf(const JPH::BodyID& id) const {
        auto it = slotById.find(id.GetIndexAndSequenceNumber());
        return it == slotById.end() ? -1 : it->second;
    }
    void touch(int triggerSlot, int otherSlot, int delta) {
        if (triggerSlot < 0 || otherSlot < 0 || triggerSlot >= (int)slots.size() || otherSlot >= (int)slots.size()) return;
        const std::pair<int, int> key{slots[(size_t)triggerSlot].report, slots[(size_t)otherSlot].node};
        std::lock_guard<std::mutex> g(eventLock);
        int& n = inside[key];
        const int before = n;
        n = std::max(0, n + delta);
        if (before == 0 && n > 0) events.push_back({key.first, key.second, true});
        if (before > 0 && n == 0) events.push_back({key.first, key.second, false});
    }

    // ---- shapes ----
    JPH::RefConst<JPH::Shape> meshShape(int mesh, const glm::vec3& scale, bool hull, const std::string& where) {
        JPH::VertexList verts;
        JPH::IndexedTriangleList tris;
        JPH::Array<JPH::Vec3> points;
        for (const Primitive& p : model->meshes[(size_t)mesh].primitives) {
            if (p.topology != Topology::Triangles) continue;
            const JPH::uint32 base = (JPH::uint32)verts.size();
            for (const glm::vec3& v : p.position) {
                const glm::vec3 s = v * scale;
                verts.push_back(JPH::Float3(s.x, s.y, s.z));
                points.push_back(J(s));
            }
            for (size_t i = 0; i + 2 < p.indices.size(); i += 3)
                tris.push_back(JPH::IndexedTriangle(base + p.indices[i], base + p.indices[i + 1], base + p.indices[i + 2]));
        }
        if (points.empty()) { warnings.push_back(where + ": the mesh has no triangles"); return nullptr; }
        JPH::ShapeSettings::ShapeResult r;
        if (hull) r = JPH::ConvexHullShapeSettings(points, 0.0f).Create();
        else r = JPH::MeshShapeSettings(std::move(verts), std::move(tris)).Create();
        if (r.HasError()) { warnings.push_back(where + ": " + std::string(r.GetError().c_str())); return nullptr; }
        return r.Get();
    }

    // A geometry at a (collider-relative) scale. `moving`: no infinite plane or triangle mesh (Jolt simulates
    // those only when static or kinematic); `dynamic`: a mesh becomes its hull.
    JPH::RefConst<JPH::Shape> makeShape(const PhysicsGeometry& g, const glm::vec3& scale, bool moving, bool dynamic,
                                        const std::string& where) {
        const glm::vec3 s = glm::abs(scale);
        if (g.mesh >= 0) {
            bool hull = g.convexHull;
            if (!hull && dynamic) {
                warnings.push_back(where + ": a dynamic body cannot use a triangle mesh; its convex hull is used");
                hull = true;
            }
            return meshShape(g.mesh, scale, hull, where);
        }
        if (g.shape < 0) return nullptr;
        const PhysicsShape& sh = model->physics.shapes[(size_t)g.shape];
        JPH::ShapeSettings::ShapeResult r;
        switch (sh.type) {
        case PhysicsShape::Type::Sphere:
            r = JPH::SphereShapeSettings(std::max(1e-4f, sh.radius * std::max({s.x, s.y, s.z}))).Create();
            break;
        case PhysicsShape::Type::Box: {
            const glm::vec3 h = glm::max(sh.size * s * 0.5f, glm::vec3(1e-4f));
            const float convex = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({h.x, h.y, h.z}));
            r = JPH::BoxShapeSettings(J(h), convex).Create();
            break;
        }
        case PhysicsShape::Type::Capsule: {
            const float half = std::max(1e-4f, 0.5f * sh.height * s.y), rs = std::max(s.x, s.z);
            const float top = std::max(1e-4f, sh.radiusTop * rs), bottom = std::max(1e-4f, sh.radiusBottom * rs);
            if (std::abs(top - bottom) < 1e-5f) r = JPH::CapsuleShapeSettings(half, top).Create();
            else {
                r = JPH::TaperedCapsuleShapeSettings(half, top, bottom).Create();
                if (r.HasError()) {
                    warnings.push_back(where + ": this tapered capsule is not valid for Jolt (" + std::string(r.GetError().c_str()) +
                                       "); a capsule of the larger radius is used");
                    r = JPH::CapsuleShapeSettings(half, std::max(top, bottom)).Create();
                }
            }
            break;
        }
        case PhysicsShape::Type::Cylinder: {
            const float half = std::max(1e-4f, 0.5f * sh.height * s.y), rs = std::max(s.x, s.z);
            const float top = sh.radiusTop * rs, bottom = sh.radiusBottom * rs;
            const float convex = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(half, std::max(top, bottom)));
            if (std::abs(top - bottom) < 1e-5f) r = JPH::CylinderShapeSettings(half, std::max(1e-4f, top), convex).Create();
            else r = JPH::TaperedCylinderShapeSettings(half, top, bottom, std::min(convex, 0.5f * std::min(top, bottom))).Create();
            break;
        }
        case PhysicsShape::Type::Plane: {
            const bool infinite = sh.sizeX <= 0.0f || sh.sizeZ <= 0.0f;
            if (infinite && !moving) {
                r = JPH::PlaneShapeSettings(JPH::Plane(JPH::Vec3::sAxisY(), 0.0f), nullptr, 1000.0f).Create();
            } else {
                // A sized (or moving) plane: a 10 cm slab whose top face is the plane.
                const float hx = 0.5f * (sh.sizeX > 0.0f ? sh.sizeX * s.x : 1000.0f), hz = 0.5f * (sh.sizeZ > 0.0f ? sh.sizeZ * s.z : 1000.0f);
                JPH::RefConst<JPH::Shape> slab = JPH::BoxShapeSettings(JPH::Vec3(hx, 0.05f, hz), 0.01f).Create().Get();
                r = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, -0.05f, 0.0f), JPH::Quat::sIdentity(), slab).Create();
            }
            break;
        }
        }
        if (r.HasError()) { warnings.push_back(where + ": " + std::string(r.GetError().c_str())); return nullptr; }
        return r.Get();
    }

    // ---- helpers over the node tree ----
    int motionOwner(int n) const {   // the nearest node with motion, n included; -1
        for (; n >= 0; n = model->nodes[(size_t)n].parent)
            if (model->physics.nodes[(size_t)n].hasMotion) return n;
        return -1;
    }
    std::string nodeName(int n) const {
        const std::string& s = model->nodes[(size_t)n].name;
        return "node '" + (s.empty() ? std::to_string(n) : s) + "'";
    }

    int addBody(JPH::BodyCreationSettings& bs, Slot slot) {
        const int index = (int)slots.size();
        bs.mUserData |= (JPH::uint64)(JPH::uint32)index;
        bs.mCollisionGroup = JPH::CollisionGroup(filter.GetPtr(), (JPH::CollisionGroup::GroupID)index, 0);
        JPH::BodyInterface& bi = system->GetBodyInterface();
        JPH::Body* body = bi.CreateBody(bs);
        if (!body) { warnings.push_back("physics: too many bodies"); return -1; }
        const bool active = bs.mMotionType != JPH::EMotionType::Static;
        bi.AddBody(body->GetID(), active ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        slot.id = body->GetID();
        slots.push_back(slot);
        slotById[slot.id.GetIndexAndSequenceNumber()] = index;
        return index;
    }
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() { clear(); }

bool PhysicsWorld::built() const { return impl_->system != nullptr; }

void PhysicsWorld::clear() {
    Impl& w = *impl_;
    if (w.system) {
        for (auto& c : w.constraints) w.system->RemoveConstraint(c);
        JPH::BodyInterface& bi = w.system->GetBodyInterface();
        for (const Impl::Slot& s : w.slots) {
            bi.RemoveBody(s.id);
            bi.DestroyBody(s.id);
        }
    }
    w.constraints.clear();
    w.system.reset();
    w.jobs.reset();
    w.temp.reset();
    w.filter = nullptr;
    w.slots.clear();
    w.slotById.clear();
    w.nodeBody.clear();
    w.order.clear();
    w.noCollide.clear();
    w.warnings.clear();
    w.inside.clear();
    w.events.clear();
    w.acc = 0.0;
    w.stepCount = 0;
    w.model = nullptr;
}

bool PhysicsWorld::build(const Model& model, const Pose& pose, std::string* error) {
    clear();
    if (!model.physics.any()) {
        if (error) *error = "the model has no KHR_physics_rigid_bodies data";
        return false;
    }
    initJolt();
    Impl& w = *impl_;
    w.model = &model;
    const Physics& ph = model.physics;
    const size_t nn = model.nodes.size();

    w.temp = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    w.jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);   // one thread: deterministic
    w.system = std::make_unique<JPH::PhysicsSystem>();
    w.system->Init(8192, 0, 16384, 8192, w.bpLayers, w.objVsBp, w.objPairs);
    w.system->SetGravity(J(w.gravity));
    w.system->SetCombineFriction(combineFriction);
    w.system->SetCombineRestitution(combineRestitution);
    w.filter = new Impl::Filter();
    w.filter->w = &w;
    w.contacts.w = &w;
    w.system->SetContactListener(&w.contacts);

    // Nodes in parent-before-child order, and the ones an animation moves (with their descendants).
    std::vector<bool> animated(nn, false);
    for (const Animation& a : model.animations)
        for (const Channel& c : a.channels)
            if (c.node >= 0 && c.node < (int)nn && c.path != Path::Weights) animated[(size_t)c.node] = true;
    for (size_t i = 0; i < nn; i++)
        if (model.nodes[i].parent < 0) {
            std::vector<int> stack{(int)i};
            while (!stack.empty()) {
                const int n = stack.back();
                stack.pop_back();
                w.order.push_back(n);
                for (int c : model.nodes[(size_t)n].children) {
                    if (animated[(size_t)n]) animated[(size_t)c] = true;
                    stack.push_back(c);
                }
            }
        }
    w.nodeBody.assign(nn, -1);

    auto userBits = [&](int material) {
        if (material < 0) return (JPH::uint64)0;
        const PhysicsMaterial& m = ph.materials[(size_t)material];
        return ((JPH::uint64)m.frictionCombine << kFrictionShift) | ((JPH::uint64)m.restitutionCombine << kRestitutionShift);
    };
    auto applyMaterial = [&](JPH::BodyCreationSettings& bs, int material) {
        if (material < 0) { bs.mFriction = 0.6f; bs.mRestitution = 0.0f; return; }
        bs.mFriction = ph.materials[(size_t)material].dynamicFriction;
        bs.mRestitution = ph.materials[(size_t)material].restitution;
        bs.mUserData = userBits(material);
    };

    // ---- rigid bodies: every node with motion, with the colliders it owns ----
    for (size_t b = 0; b < nn; b++) {
        const NodePhysics& np = ph.nodes[b];
        if (!np.hasMotion) continue;
        const TRS body = decompose(pose.world((int)b));
        const glm::mat4 bodyInv = glm::inverse(rt(body.t, body.r));
        const bool isAnimated = animated[b];
        const bool kinematic = np.kinematic || np.mass == 0.0f || isAnimated;
        JPH::StaticCompoundShapeSettings compound;
        int parts = 0, material = -1, filterIndex = -1;
        JPH::RefConst<JPH::Shape> single;
        glm::vec3 singleT(0.0f);
        glm::quat singleR(1.0f, 0.0f, 0.0f, 0.0f);
        for (size_t c = 0; c < nn; c++) {
            const NodePhysics& cp = ph.nodes[c];
            if (!cp.hasCollider || w.motionOwner((int)c) != (int)b) continue;
            const TRS rel = decompose(bodyInv * pose.world((int)c));
            JPH::RefConst<JPH::Shape> shape = w.makeShape(cp.collider, rel.s, true, !kinematic, "physics, " + w.nodeName((int)c));
            if (!shape) continue;
            if (material < 0) material = cp.material;
            if (filterIndex < 0) filterIndex = cp.filter;
            compound.AddShape(J(rel.t), J(rel.r), shape);
            single = shape;
            singleT = rel.t;
            singleR = rel.r;
            parts++;
        }
        if (parts == 0) {
            w.warnings.push_back("physics, " + w.nodeName((int)b) + ": a body with no collider (skipped)");
            continue;
        }
        JPH::RefConst<JPH::Shape> shape;
        if (parts == 1 && glm::length(singleT) < 1e-6f && std::abs(std::abs(singleR.w) - 1.0f) < 1e-6f) shape = single;
        else if (parts == 1) shape = JPH::RotatedTranslatedShapeSettings(J(singleT), J(singleR), single).Create().Get();
        else shape = compound.Create().Get();
        if (np.hasCenterOfMass) {
            const JPH::Vec3 offset = J(np.centerOfMass * body.s) - shape->GetCenterOfMass();
            shape = JPH::OffsetCenterOfMassShapeSettings(offset, shape).Create().Get();
        }
        JPH::BodyCreationSettings bs(shape, J(body.t), J(body.r), kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
                                     Layers::kMoving);
        applyMaterial(bs, material);
        bs.mGravityFactor = np.gravityFactor;
        bs.mLinearVelocity = J(body.r * np.linearVelocity);
        bs.mAngularVelocity = J(body.r * np.angularVelocity);
        bs.mAllowSleeping = true;
        if (!kinematic && np.mass > 0.0f) {
            bs.mMassPropertiesOverride.mMass = np.mass;
            if (np.hasInertia) {
                glm::vec3 d = np.inertiaDiagonal;
                for (int k = 0; k < 3; k++)
                    if (d[k] <= 0.0f) d[k] = 1.0e6f * np.mass;   // 0 = "infinite": no rotation about it
                const JPH::Mat44 r = JPH::Mat44::sRotation(J(np.inertiaOrientation));
                bs.mMassPropertiesOverride.mInertia = r * JPH::Mat44::sScale(J(d)) * r.Transposed();
                bs.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            } else {
                bs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            }
        }
        Impl::Slot slot;
        slot.node = (int)b;
        slot.kind = kinematic ? Impl::Kind::Kinematic : Impl::Kind::Dynamic;
        slot.animated = isAnimated;
        slot.writeBack = !isAnimated;
        slot.filter = filterIndex;
        slot.worldScale = body.s;
        const int index = w.addBody(bs, slot);
        if (index >= 0) w.nodeBody[b] = index;
    }

    // ---- static colliders: a collider with no motion in its ancestry (kinematic when an animation moves it) ----
    for (size_t c = 0; c < nn; c++) {
        const NodePhysics& cp = ph.nodes[c];
        if (!cp.hasCollider || w.motionOwner((int)c) >= 0) continue;
        const TRS t = decompose(pose.world((int)c));
        const bool moving = animated[c];
        JPH::RefConst<JPH::Shape> shape = w.makeShape(cp.collider, t.s, moving, false, "physics, " + w.nodeName((int)c));
        if (!shape) continue;
        JPH::BodyCreationSettings bs(shape, J(t.t), J(t.r), moving ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
                                     moving ? Layers::kMoving : Layers::kStatic);
        applyMaterial(bs, cp.material);
        Impl::Slot slot;
        slot.node = (int)c;
        slot.kind = moving ? Impl::Kind::Kinematic : Impl::Kind::Static;
        slot.animated = moving;
        slot.filter = cp.filter;
        slot.worldScale = t.s;
        const int index = w.addBody(bs, slot);
        if (index >= 0) w.nodeBody[c] = index;
    }

    // ---- triggers: sensors; inside a moving body they follow it ----
    for (size_t t = 0; t < nn; t++) {
        const NodePhysics& tp = ph.nodes[t];
        if (!tp.hasTrigger || !tp.trigger.valid()) continue;
        int report = (int)t;   // a part of a compound trigger reports the compound's node
        for (int a = model.nodes[t].parent; a >= 0; a = model.nodes[(size_t)a].parent) {
            const auto& tn = ph.nodes[(size_t)a].triggerNodes;
            if (std::find(tn.begin(), tn.end(), (int)t) != tn.end()) { report = a; break; }
        }
        const TRS tw = decompose(pose.world((int)t));
        const int owner = w.motionOwner((int)t);
        const bool moving = owner >= 0 || animated[t];
        JPH::RefConst<JPH::Shape> shape = w.makeShape(tp.trigger, tw.s, moving, false, "physics, trigger " + w.nodeName((int)t));
        if (!shape) continue;
        JPH::BodyCreationSettings bs(shape, J(tw.t), J(tw.r), moving ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
                                     Layers::kMoving);
        bs.mIsSensor = true;
        bs.mCollideKinematicVsNonDynamic = true;
        Impl::Slot slot;
        slot.node = (int)t;
        slot.kind = Impl::Kind::Trigger;
        slot.report = report;
        slot.filter = tp.triggerFilter;
        slot.animated = owner < 0 && animated[t];
        if (owner >= 0 && w.nodeBody[(size_t)owner] >= 0) {
            slot.follow = w.nodeBody[(size_t)owner];
            const TRS ow = decompose(pose.world(owner));
            slot.followRel = glm::inverse(rt(ow.t, ow.r)) * rt(tw.t, tw.r);
        }
        w.addBody(bs, slot);
    }

    // ---- joints: a six-degrees-of-freedom constraint between the two nodes' bodies ----
    auto bodyOf = [&](int n) {
        for (; n >= 0; n = model.nodes[(size_t)n].parent)
            if (w.nodeBody[(size_t)n] >= 0) return w.nodeBody[(size_t)n];
        return -1;
    };
    using Axis = JPH::SixDOFConstraintSettings::EAxis;
    for (size_t j = 0; j < nn; j++) {
        const NodePhysics& jp = ph.nodes[j];
        if (!jp.hasJoint) continue;
        const int sa = bodyOf((int)j), sb = bodyOf(jp.connectedNode);
        const std::string where = "physics, joint on " + w.nodeName((int)j);
        if (sa == sb) { w.warnings.push_back(where + ": both ends are the same body (skipped)"); continue; }
        const TRS fa = decompose(pose.world((int)j)), fb = decompose(pose.world(jp.connectedNode));
        JPH::SixDOFConstraintSettings s;
        s.mSpace = JPH::EConstraintSpace::WorldSpace;
        s.mPosition1 = J(fa.t);
        s.mAxisX1 = J(fa.r * glm::vec3(1, 0, 0));
        s.mAxisY1 = J(fa.r * glm::vec3(0, 1, 0));
        s.mPosition2 = J(fb.t);
        s.mAxisX2 = J(fb.r * glm::vec3(1, 0, 0));
        s.mAxisY2 = J(fb.r * glm::vec3(0, 1, 0));
        s.mSwingType = JPH::ESwingType::Pyramid;
        for (int a = 0; a < (int)Axis::Num; a++) s.MakeFreeAxis((Axis)a);
        const PhysicsJoint& def = ph.joints[(size_t)jp.joint];
        for (const PhysicsJointLimit& l : def.limits) {
            if (!l.hasMin && !l.hasMax) continue;
            auto limit = [&](int axisIndex, bool angular) {
                const Axis ax = (Axis)((angular ? (int)Axis::RotationX : (int)Axis::TranslationX) + std::clamp(axisIndex, 0, 2));
                float lo = l.hasMin ? l.min : -FLT_MAX, hi = l.hasMax ? l.max : FLT_MAX;
                if (angular) {
                    const float cap = ax == Axis::RotationX ? JPH::JPH_PI : 0.999f * JPH::JPH_PI;
                    lo = std::clamp(lo, -cap, cap);
                    hi = std::clamp(hi, -cap, cap);
                }
                if (lo == 0.0f && hi == 0.0f && (angular || l.stiffness < 0.0f)) s.MakeFixedAxis(ax);
                else s.SetLimitedAxis(ax, lo, hi);
                if (!angular && l.stiffness >= 0.0f)
                    s.mLimitsSpringSettings[(int)ax] = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, l.stiffness, l.damping);
            };
            for (int a : l.linearAxes) limit(a, false);
            for (int a : l.angularAxes) limit(a, true);
        }
        struct Motor { Axis axis; JPH::EMotorState state; };
        std::vector<Motor> motors;
        glm::vec3 linPos(0.0f), linVel(0.0f), angPos(0.0f), angVel(0.0f);
        for (const PhysicsJointDrive& d : def.drives) {
            const Axis ax = (Axis)((d.angular ? (int)Axis::RotationX : (int)Axis::TranslationX) + d.axis);
            JPH::MotorSettings& ms = s.mMotorSettings[(int)ax];
            ms.mSpringSettings = JPH::SpringSettings(d.acceleration ? JPH::ESpringMode::MassNormalizedStiffnessAndDamping
                                                                    : JPH::ESpringMode::StiffnessAndDamping,
                                                     std::max(0.0f, d.stiffness), std::max(0.0f, d.damping));
            if (d.maxForce >= 0.0f) {
                if (d.angular) ms.SetTorqueLimit(d.maxForce);
                else ms.SetForceLimit(d.maxForce);
            }
            const bool position = d.hasPositionTarget && d.stiffness > 0.0f;
            const bool velocity = !position && (d.hasVelocityTarget || d.damping > 0.0f);
            if (!position && !velocity) continue;
            motors.push_back({ax, position ? JPH::EMotorState::Position : JPH::EMotorState::Velocity});
            glm::vec3& target = d.angular ? (position ? angPos : angVel) : (position ? linPos : linVel);
            target[d.axis] = position ? d.positionTarget : d.velocityTarget;
        }
        JPH::BodyInterface& bi = w.system->GetBodyInterface();
        const JPH::BodyID ia = sa >= 0 ? w.slots[(size_t)sa].id : JPH::BodyID(), ib = sb >= 0 ? w.slots[(size_t)sb].id : JPH::BodyID();
        JPH::TwoBodyConstraint* c = bi.CreateConstraint(&s, ia, ib);
        if (!c) { w.warnings.push_back(where + ": could not be created"); continue; }
        auto* six = static_cast<JPH::SixDOFConstraint*>(c);
        for (const Motor& m : motors) six->SetMotorState(m.axis, m.state);
        six->SetTargetPositionCS(J(linPos));
        six->SetTargetVelocityCS(J(linVel));
        six->SetTargetAngularVelocityCS(J(angVel));
        six->SetTargetOrientationCS(JPH::Quat::sEulerAngles(J(angPos)));
        w.system->AddConstraint(c);
        w.constraints.push_back(c);
        if (!jp.enableCollision && sa >= 0 && sb >= 0)
            w.noCollide.insert({(JPH::uint32)std::min(sa, sb), (JPH::uint32)std::max(sa, sb)});
        if (!ia.IsInvalid()) bi.ActivateBody(ia);
        if (!ib.IsInvalid()) bi.ActivateBody(ib);
    }

    w.system->OptimizeBroadPhase();
    return true;
}

void PhysicsWorld::step(float dt, Pose& pose, int maxSteps) {
    Impl& w = *impl_;
    if (!w.system || dt <= 0.0f) return;
    w.acc += dt;
    int n = 0;
    JPH::BodyInterface& bi = w.system->GetBodyInterface();
    while (w.acc >= kStep - 1e-7 && (maxSteps <= 0 || n < maxSteps)) {
        // Animated bodies go where the pose puts them; triggers inside a moving body follow it.
        pose.updateWorld();
        for (const Impl::Slot& s : w.slots) {
            if (s.animated) {
                const TRS t = decompose(pose.world(s.node));
                bi.MoveKinematic(s.id, J(t.t), J(t.r), kStep);
            } else if (s.follow >= 0) {
                JPH::RVec3 p;
                JPH::Quat q;
                bi.GetPositionAndRotation(w.slots[(size_t)s.follow].id, p, q);
                const TRS t = decompose(rt(G(p), G(q)) * s.followRel);
                bi.MoveKinematic(s.id, J(t.t), J(t.r), kStep);
            }
        }
        w.system->Update(kStep, 1, w.temp.get(), w.jobs.get());
        w.acc -= kStep;
        w.stepCount++;
        n++;
    }
    if (maxSteps > 0 && n == maxSteps && w.acc > kStep) w.acc = 0.0;   // too far behind: drop the backlog
    if (n > 0) writePose(pose);
}

void PhysicsWorld::writePose(Pose& pose) const {
    const Impl& w = *impl_;
    if (!w.system) return;
    const JPH::BodyInterface& bi = w.system->GetBodyInterface();
    // Moving bodies into the pose, parents first (a body's node may sit under another body's node).
    const Model& m = *w.model;
    std::vector<glm::mat4> world(m.nodes.size(), glm::mat4(1.0f));
    for (int node : w.order) {
        const int parent = m.nodes[(size_t)node].parent;
        const glm::mat4 parentWorld = parent >= 0 ? world[(size_t)parent] : glm::mat4(1.0f);
        const int s = w.nodeBody[(size_t)node];
        if (s >= 0 && w.slots[(size_t)s].writeBack) {
            JPH::RVec3 p;
            JPH::Quat q;
            bi.GetPositionAndRotation(w.slots[(size_t)s].id, p, q);
            const glm::mat4 desired = rt(G(p), G(q)) * glm::scale(glm::mat4(1.0f), w.slots[(size_t)s].worldScale);
            const TRS local = decompose(glm::inverse(parentWorld) * desired);
            pose.setLocal(node, local.t, local.r, pose.scale(node));
        }
        world[(size_t)node] = parentWorld * composeTRS(pose.translation(node), pose.rotation(node), pose.scale(node));
    }
    pose.updateWorld();
}

void PhysicsWorld::simulateTo(double seconds, Pose& pose) {
    const int target = (int)std::floor(seconds / kStep + 1e-3);   // 2.5 s is 150 steps, not 149.9999
    const int todo = target - impl_->stepCount;
    if (todo > 0) step((float)(todo * (double)kStep) + 1e-6f, pose, 0);
}

double PhysicsWorld::time() const { return impl_->stepCount * (double)kStep; }
int PhysicsWorld::steps() const { return impl_->stepCount; }

void PhysicsWorld::setGravity(const glm::vec3& g) {
    impl_->gravity = g;
    if (impl_->system) impl_->system->SetGravity(J(g));
}
glm::vec3 PhysicsWorld::gravity() const { return impl_->gravity; }

std::vector<TriggerEvent> PhysicsWorld::takeEvents() {
    std::lock_guard<std::mutex> g(impl_->eventLock);
    std::vector<TriggerEvent> out;
    out.swap(impl_->events);
    return out;
}

std::vector<int> PhysicsWorld::inside(int triggerNode) const {
    std::lock_guard<std::mutex> g(impl_->eventLock);
    std::vector<int> out;
    for (const auto& [key, n] : impl_->inside)
        if (key.first == triggerNode && n > 0) out.push_back(key.second);
    return out;
}

bool PhysicsWorld::isSleeping(int node) const {
    const Impl& w = *impl_;
    if (!w.system || node < 0 || node >= (int)w.nodeBody.size() || w.nodeBody[(size_t)node] < 0) return false;
    return !w.system->GetBodyInterface().IsActive(w.slots[(size_t)w.nodeBody[(size_t)node]].id);
}

glm::vec3 PhysicsWorld::linearVelocity(int node) const {
    const Impl& w = *impl_;
    if (!w.system || node < 0 || node >= (int)w.nodeBody.size() || w.nodeBody[(size_t)node] < 0) return glm::vec3(0.0f);
    return G(w.system->GetBodyInterface().GetLinearVelocity(w.slots[(size_t)w.nodeBody[(size_t)node]].id));
}

PhysicsWorld::Stats PhysicsWorld::stats() const {
    Stats st;
    const Impl& w = *impl_;
    if (!w.system) return st;
    const JPH::BodyInterface& bi = w.system->GetBodyInterface();
    for (const Impl::Slot& s : w.slots) {
        switch (s.kind) {
        case Impl::Kind::Dynamic:
            st.dynamic++;
            if (bi.IsActive(s.id)) st.awake++;
            break;
        case Impl::Kind::Kinematic: st.kinematic++; break;
        case Impl::Kind::Static: st.statics++; break;
        case Impl::Kind::Trigger: st.triggers++; break;
        }
    }
    st.joints = (int)w.constraints.size();
    return st;
}

const std::vector<std::string>& PhysicsWorld::warnings() const { return impl_->warnings; }

void PhysicsWorld::debugBoxes(std::vector<PhysicsDebugBox>& out) const {
    out.clear();
    const Impl& w = *impl_;
    if (!w.system) return;
    const JPH::BodyInterface& bi = w.system->GetBodyInterface();
    for (const Impl::Slot& s : w.slots) {
        JPH::RefConst<JPH::Shape> shape = bi.GetShape(s.id);
        if (!shape) continue;
        JPH::AABox b = shape->GetLocalBounds();
        if (b.GetExtent().GetX() > 500.0f) b = JPH::AABox(JPH::Vec3(-5, -0.05f, -5), JPH::Vec3(5, 0, 5));   // an infinite plane
        JPH::RVec3 p;
        JPH::Quat q;
        bi.GetPositionAndRotation(s.id, p, q);
        PhysicsDebugBox d;
        // Jolt's body position is the shape's origin; local bounds are around the centre of mass.
        const glm::vec3 com = G(shape->GetCenterOfMass());
        d.world = rt(G(p), G(q)) * glm::translate(glm::mat4(1.0f), com + G(b.GetCenter()));
        d.halfExtents = G(b.GetExtent());
        d.kind = s.kind == Impl::Kind::Static ? PhysicsDebugBox::Kind::Static
               : s.kind == Impl::Kind::Kinematic ? PhysicsDebugBox::Kind::Kinematic
               : s.kind == Impl::Kind::Dynamic ? PhysicsDebugBox::Kind::Dynamic : PhysicsDebugBox::Kind::Trigger;
        d.sleeping = s.kind == Impl::Kind::Dynamic && !bi.IsActive(s.id);
        d.node = s.node;
        out.push_back(d);
    }
}

}  // namespace toms::gltf
