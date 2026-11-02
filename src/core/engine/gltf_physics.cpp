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

void f_InitJolt() {
    static std::once_flag l_oOnce;
    std::call_once(l_oOnce, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();   // kept for the life of the process
        JPH::RegisterTypes();
    });
}

namespace Layers {
constexpr JPH::ObjectLayer g_u16Static = 0;   // static colliders: never collide with each other
constexpr JPH::ObjectLayer g_u16Moving = 1;   // kinematic, dynamic and every trigger
}
const JPH::BroadPhaseLayer g_oBpStatic(0), g_oBpMoving(1);

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l_u16L) const override { return l_u16L == Layers::g_u16Static ? g_oBpStatic : g_oBpMoving; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l == kBpStatic ? "static" : "moving"; }
#endif
};
class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l_u16L, JPH::BroadPhaseLayer l_oBp) const override { return l_u16L == Layers::g_u16Moving || l_oBp == g_oBpMoving; }
};
class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l_u16A, JPH::ObjectLayer l_u16B) const override { return l_u16A == Layers::g_u16Moving || l_u16B == Layers::g_u16Moving; }
};

// ---- maths between glm and Jolt ---------------------------------------------------------------------------

JPH::Vec3 f_J(const glm::vec3& l_v3V) { return JPH::Vec3(l_v3V.x, l_v3V.y, l_v3V.z); }
JPH::Quat f_J(const glm::quat& l_qQ) { return JPH::Quat(l_qQ.x, l_qQ.y, l_qQ.z, l_qQ.w).Normalized(); }
glm::vec3 f_G(JPH::Vec3Arg l_oV) { return {l_oV.GetX(), l_oV.GetY(), l_oV.GetZ()}; }
glm::quat f_G(JPH::QuatArg l_oQ) { return glm::quat(l_oQ.GetW(), l_oQ.GetX(), l_oQ.GetY(), l_oQ.GetZ()); }

struct TRS {
    glm::vec3 m_v3T{0.0f}, m_v3S{1.0f};
    glm::quat m_qR{1.0f, 0.0f, 0.0f, 0.0f};
};
TRS f_Decompose(const glm::mat4& l_mat4M) {
    TRS l_oO;
    l_oO.m_v3T = glm::vec3(l_mat4M[3]);
    glm::vec3 l_v3C0(l_mat4M[0]), l_v3C1(l_mat4M[1]), l_v3C2(l_mat4M[2]);
    l_oO.m_v3S = {glm::length(l_v3C0), glm::length(l_v3C1), glm::length(l_v3C2)};
    if (glm::dot(glm::cross(l_v3C0, l_v3C1), l_v3C2) < 0.0f) { l_oO.m_v3S.x = -l_oO.m_v3S.x; }
    const glm::mat3 l_mat3Rot(l_v3C0 / (l_oO.m_v3S.x != 0.0f ? l_oO.m_v3S.x : 1.0f), l_v3C1 / (l_oO.m_v3S.y != 0.0f ? l_oO.m_v3S.y : 1.0f), l_v3C2 / (l_oO.m_v3S.z != 0.0f ? l_oO.m_v3S.z : 1.0f));
    l_oO.m_qR = glm::normalize(glm::quat_cast(l_mat3Rot));
    return l_oO;
}
glm::mat4 f_Rt(const glm::vec3& l_v3T, const glm::quat& l_qR) { return glm::translate(glm::mat4(1.0f), l_v3T) * glm::mat4_cast(l_qR); }

// ---- friction / restitution combine (KHR: average > minimum > maximum > multiply; none: Jolt's defaults) -----
// A body's two combine modes travel in its user data (bits 32..39); the low 32 bits are its slot.
constexpr int g_iFrictionShift = 32, g_iRestitutionShift = 36;
PhysicsCombine f_ModeOf(JPH::uint64 l_u64Ud, int l_iShift) { return PhysicsCombine((l_u64Ud >> l_iShift) & 0xF); }
float f_Combine(float l_fA, float l_fB, PhysicsCombine l_eMa, PhysicsCombine l_eMb, bool l_bFriction) {
    auto l_fnHas = [&](PhysicsCombine l_eC) { return l_eMa == l_eC || l_eMb == l_eC; };
    if (l_fnHas(PhysicsCombine::Average)) return 0.5f * (l_fA + l_fB);
    if (l_fnHas(PhysicsCombine::Minimum)) return std::min(l_fA, l_fB);
    if (l_fnHas(PhysicsCombine::Maximum)) return std::max(l_fA, l_fB);
    if (l_fnHas(PhysicsCombine::Multiply)) return l_fA * l_fB;
    return l_bFriction ? std::sqrt(l_fA * l_fB) : std::max(l_fA, l_fB);
}
float f_CombineFriction(const JPH::Body& l_oA, const JPH::SubShapeID&, const JPH::Body& l_oB, const JPH::SubShapeID&) {
    return f_Combine(l_oA.GetFriction(), l_oB.GetFriction(), f_ModeOf(l_oA.GetUserData(), g_iFrictionShift), f_ModeOf(l_oB.GetUserData(), g_iFrictionShift), true);
}
float f_CombineRestitution(const JPH::Body& l_oA, const JPH::SubShapeID&, const JPH::Body& l_oB, const JPH::SubShapeID&) {
    return f_Combine(l_oA.GetRestitution(), l_oB.GetRestitution(), f_ModeOf(l_oA.GetUserData(), g_iRestitutionShift),
                   f_ModeOf(l_oB.GetUserData(), g_iRestitutionShift), false);
}

bool f_Intersects(const std::vector<std::string>& l_vA, const std::vector<std::string>& l_vB) {
    for (const std::string& l_strX : l_vA)
        if (std::find(l_vB.begin(), l_vB.end(), l_strX) != l_vB.end()) return true;
    return false;
}
// One side's filter lets the other's systems through.
bool f_Lets(const PhysicsFilter& l_oF, const PhysicsFilter& l_oOther) {
    if (!l_oF.m_vCollideWith.empty()) return f_Intersects(l_oF.m_vCollideWith, l_oOther.m_vSystems);
    if (!l_oF.m_vNotCollideWith.empty()) return !f_Intersects(l_oF.m_vNotCollideWith, l_oOther.m_vSystems);
    return true;
}

}  // namespace

// ---- the world ----------------------------------------------------------------------------------------------

struct PhysicsWorld::Impl {
    enum class Kind { Static, Kinematic, Dynamic, Trigger };
    struct Slot {
        JPH::BodyID m_oId;
        int m_iNode = -1;                   // the body's node (the motion node, the static collider, the trigger)
        Kind m_eKind = Kind::Static;
        bool m_bAnimated = false;           // moved to the pose's transform every step
        bool m_bWriteBack = false;          // a moving body: written into the pose
        int m_iFollow = -1;                 // a trigger inside a moving body: that body's slot
        glm::mat4 m_mat4FollowRel{1.0f};       // the trigger relative to the body it follows
        int m_iReport = -1;                 // a trigger: the node its events name
        int m_iFilter = -1;
        glm::vec3 m_v3WorldScale{1.0f};      // the node's world scale at build (kept when writing back)
    };

    // Collision filters + joints' "do not collide" pairs: the group filter every body shares (group id = slot).
    class Filter final : public JPH::GroupFilter {
    public:
        Impl* m_pW = nullptr;
        bool CanCollide(const JPH::CollisionGroup& l_oA, const JPH::CollisionGroup& l_oB) const override {
            const JPH::uint32 l_uSa = l_oA.GetGroupID(), l_uSb = l_oB.GetGroupID();
            if (m_pW->m_setNoCollide.count({std::min(l_uSa, l_uSb), std::max(l_uSa, l_uSb)})) return false;
            if (l_uSa >= m_pW->m_vSlots.size() || l_uSb >= m_pW->m_vSlots.size()) return true;
            const int l_iFa = m_pW->m_vSlots[l_uSa].m_iFilter, l_iFb = m_pW->m_vSlots[l_uSb].m_iFilter;
            if (l_iFa < 0 || l_iFb < 0) return true;   // a collider without a filter collides with everything
            const auto& l_vFs = m_pW->m_pModel->m_oPhysics.m_vFilters;
            return f_Lets(l_vFs[(size_t)l_iFa], l_vFs[(size_t)l_iFb]) && f_Lets(l_vFs[(size_t)l_iFb], l_vFs[(size_t)l_iFa]);
        }
    };

    // Trigger enter / leave, counted per (trigger, body) pair: a compound touches through several sub-shapes.
    class Contacts final : public JPH::ContactListener {
    public:
        Impl* m_pW = nullptr;
        void OnContactAdded(const JPH::Body& l_oA, const JPH::Body& l_oB, const JPH::ContactManifold&, JPH::ContactSettings&) override {
            if (l_oA.IsSensor() == l_oB.IsSensor()) return;
            const JPH::Body& l_oSensor = l_oA.IsSensor() ? l_oA : l_oB;
            const JPH::Body& l_oOther = l_oA.IsSensor() ? l_oB : l_oA;
            m_pW->f_Touch((int)(l_oSensor.GetUserData() & 0xFFFFFFFFu), (int)(l_oOther.GetUserData() & 0xFFFFFFFFu), +1);
        }
        void OnContactRemoved(const JPH::SubShapeIDPair& l_oPair) override {
            const int l_iS1 = m_pW->f_SlotOf(l_oPair.GetBody1ID()), l_iS2 = m_pW->f_SlotOf(l_oPair.GetBody2ID());
            if (l_iS1 < 0 || l_iS2 < 0) return;
            const bool l_bT1 = m_pW->m_vSlots[(size_t)l_iS1].m_eKind == Kind::Trigger, l_bT2 = m_pW->m_vSlots[(size_t)l_iS2].m_eKind == Kind::Trigger;
            if (l_bT1 == l_bT2) return;
            m_pW->f_Touch(l_bT1 ? l_iS1 : l_iS2, l_bT1 ? l_iS2 : l_iS1, -1);
        }
    };

    const Model* m_pModel = nullptr;
    BroadPhaseLayers m_oBpLayers;
    ObjectVsBroadPhase m_oObjVsBp;
    ObjectPairs m_oObjPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> m_upTemp;
    std::unique_ptr<JPH::JobSystemSingleThreaded> m_upJobs;
    std::unique_ptr<JPH::PhysicsSystem> m_upSystem;
    JPH::Ref<Filter> m_oFilter;
    Contacts m_oContacts;

    std::vector<Slot> m_vSlots;
    std::unordered_map<JPH::uint32, int> m_mapSlotById;
    std::vector<int> m_vNodeBody;           // node -> the slot of its body (motion or static collider), -1
    std::vector<int> m_vOrder;              // nodes, parents before children
    std::set<std::pair<JPH::uint32, JPH::uint32>> m_setNoCollide;
    std::vector<JPH::Ref<JPH::Constraint>> m_vConstraints;
    std::vector<std::string> m_vWarnings;

    std::mutex m_oEventLock;
    std::map<std::pair<int, int>, int> m_mapInside;   // (trigger report node, body node) -> touching sub-shape pairs
    std::vector<TriggerEvent> m_vEvents;

    double m_dAcc = 0.0;
    int m_iStepCount = 0;
    glm::vec3 m_v3Gravity{0.0f, -9.81f, 0.0f};

    int f_SlotOf(const JPH::BodyID& l_oId) const {
        auto l_itIt = m_mapSlotById.find(l_oId.GetIndexAndSequenceNumber());
        return l_itIt == m_mapSlotById.end() ? -1 : l_itIt->second;
    }
    void f_Touch(int l_iTriggerSlot, int l_iOtherSlot, int l_iDelta) {
        if (l_iTriggerSlot < 0 || l_iOtherSlot < 0 || l_iTriggerSlot >= (int)m_vSlots.size() || l_iOtherSlot >= (int)m_vSlots.size()) return;
        const std::pair<int, int> l_pairKey{m_vSlots[(size_t)l_iTriggerSlot].m_iReport, m_vSlots[(size_t)l_iOtherSlot].m_iNode};
        std::lock_guard<std::mutex> l_oG(m_oEventLock);
        int& l_iN = m_mapInside[l_pairKey];
        const int l_iBefore = l_iN;
        l_iN = std::max(0, l_iN + l_iDelta);
        if (l_iBefore == 0 && l_iN > 0) m_vEvents.push_back({l_pairKey.first, l_pairKey.second, true});
        if (l_iBefore > 0 && l_iN == 0) m_vEvents.push_back({l_pairKey.first, l_pairKey.second, false});
    }

    // ---- shapes ----
    JPH::RefConst<JPH::Shape> f_MeshShape(int l_iMesh, const glm::vec3& l_v3Scale, bool l_bHull, const std::string& l_strWhere) {
        JPH::VertexList l_oVerts;
        JPH::IndexedTriangleList l_oTris;
        JPH::Array<JPH::Vec3> l_oPoints;
        for (const Primitive& l_oP : m_pModel->m_vMeshes[(size_t)l_iMesh].m_vPrimitives) {
            if (l_oP.m_eTopology != Topology::Triangles) continue;
            const JPH::uint32 l_uBase = (JPH::uint32)l_oVerts.size();
            for (const glm::vec3& l_v3V : l_oP.m_vPosition) {
                const glm::vec3 l_v3S = l_v3V * l_v3Scale;
                l_oVerts.push_back(JPH::Float3(l_v3S.x, l_v3S.y, l_v3S.z));
                l_oPoints.push_back(f_J(l_v3S));
            }
            for (size_t l_nI = 0; l_nI + 2 < l_oP.m_vIndices.size(); l_nI += 3)
                l_oTris.push_back(JPH::IndexedTriangle(l_uBase + l_oP.m_vIndices[l_nI], l_uBase + l_oP.m_vIndices[l_nI + 1], l_uBase + l_oP.m_vIndices[l_nI + 2]));
        }
        if (l_oPoints.empty()) { m_vWarnings.push_back(l_strWhere + ": the mesh has no triangles"); return nullptr; }
        JPH::ShapeSettings::ShapeResult l_oR;
        if (l_bHull) l_oR = JPH::ConvexHullShapeSettings(l_oPoints, 0.0f).Create();
        else l_oR = JPH::MeshShapeSettings(std::move(l_oVerts), std::move(l_oTris)).Create();
        if (l_oR.HasError()) { m_vWarnings.push_back(l_strWhere + ": " + std::string(l_oR.GetError().c_str())); return nullptr; }
        return l_oR.Get();
    }

    // A geometry at a (collider-relative) scale. `moving`: no infinite plane or triangle mesh (Jolt simulates
    // those only when static or kinematic); `dynamic`: a mesh becomes its hull.
    JPH::RefConst<JPH::Shape> f_MakeShape(const PhysicsGeometry& l_oG, const glm::vec3& l_v3Scale, bool l_bMoving, bool l_bDynamic,
                                        const std::string& l_strWhere) {
        const glm::vec3 l_v3S = glm::abs(l_v3Scale);
        if (l_oG.m_iMesh >= 0) {
            bool l_bHull = l_oG.m_bConvexHull;
            if (!l_bHull && l_bDynamic) {
                m_vWarnings.push_back(l_strWhere + ": a dynamic body cannot use a triangle mesh; its convex hull is used");
                l_bHull = true;
            }
            return f_MeshShape(l_oG.m_iMesh, l_v3Scale, l_bHull, l_strWhere);
        }
        if (l_oG.m_iShape < 0) return nullptr;
        const PhysicsShape& l_oSh = m_pModel->m_oPhysics.m_vShapes[(size_t)l_oG.m_iShape];
        JPH::ShapeSettings::ShapeResult l_oR;
        switch (l_oSh.m_eType) {
        case PhysicsShape::Type::Sphere:
            l_oR = JPH::SphereShapeSettings(std::max(1e-4f, l_oSh.m_fRadius * std::max({l_v3S.x, l_v3S.y, l_v3S.z}))).Create();
            break;
        case PhysicsShape::Type::Box: {
            const glm::vec3 l_v3H = glm::max(l_oSh.m_v3Size * l_v3S * 0.5f, glm::vec3(1e-4f));
            const float l_fConvex = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({l_v3H.x, l_v3H.y, l_v3H.z}));
            l_oR = JPH::BoxShapeSettings(f_J(l_v3H), l_fConvex).Create();
            break;
        }
        case PhysicsShape::Type::Capsule: {
            const float l_fHalf = std::max(1e-4f, 0.5f * l_oSh.m_fHeight * l_v3S.y), l_fRs = std::max(l_v3S.x, l_v3S.z);
            const float l_fTop = std::max(1e-4f, l_oSh.m_fRadiusTop * l_fRs), l_fBottom = std::max(1e-4f, l_oSh.m_fRadiusBottom * l_fRs);
            if (std::abs(l_fTop - l_fBottom) < 1e-5f) l_oR = JPH::CapsuleShapeSettings(l_fHalf, l_fTop).Create();
            else {
                l_oR = JPH::TaperedCapsuleShapeSettings(l_fHalf, l_fTop, l_fBottom).Create();
                if (l_oR.HasError()) {
                    m_vWarnings.push_back(l_strWhere + ": this tapered capsule is not valid for Jolt (" + std::string(l_oR.GetError().c_str()) +
                                       "); a capsule of the larger radius is used");
                    l_oR = JPH::CapsuleShapeSettings(l_fHalf, std::max(l_fTop, l_fBottom)).Create();
                }
            }
            break;
        }
        case PhysicsShape::Type::Cylinder: {
            const float l_fHalf = std::max(1e-4f, 0.5f * l_oSh.m_fHeight * l_v3S.y), l_fRs = std::max(l_v3S.x, l_v3S.z);
            const float l_fTop = l_oSh.m_fRadiusTop * l_fRs, l_fBottom = l_oSh.m_fRadiusBottom * l_fRs;
            const float l_fConvex = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(l_fHalf, std::max(l_fTop, l_fBottom)));
            if (std::abs(l_fTop - l_fBottom) < 1e-5f) l_oR = JPH::CylinderShapeSettings(l_fHalf, std::max(1e-4f, l_fTop), l_fConvex).Create();
            else l_oR = JPH::TaperedCylinderShapeSettings(l_fHalf, l_fTop, l_fBottom, std::min(l_fConvex, 0.5f * std::min(l_fTop, l_fBottom))).Create();
            break;
        }
        case PhysicsShape::Type::Plane: {
            const bool l_bInfinite = l_oSh.m_fSizeX <= 0.0f || l_oSh.m_fSizeZ <= 0.0f;
            if (l_bInfinite && !l_bMoving) {
                l_oR = JPH::PlaneShapeSettings(JPH::Plane(JPH::Vec3::sAxisY(), 0.0f), nullptr, 1000.0f).Create();
            } else {
                // A sized (or moving) plane: a 10 cm slab whose top face is the plane.
                const float l_fHx = 0.5f * (l_oSh.m_fSizeX > 0.0f ? l_oSh.m_fSizeX * l_v3S.x : 1000.0f), l_fHz = 0.5f * (l_oSh.m_fSizeZ > 0.0f ? l_oSh.m_fSizeZ * l_v3S.z : 1000.0f);
                JPH::RefConst<JPH::Shape> l_oSlab = JPH::BoxShapeSettings(JPH::Vec3(l_fHx, 0.05f, l_fHz), 0.01f).Create().Get();
                l_oR = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, -0.05f, 0.0f), JPH::Quat::sIdentity(), l_oSlab).Create();
            }
            break;
        }
        }
        if (l_oR.HasError()) { m_vWarnings.push_back(l_strWhere + ": " + std::string(l_oR.GetError().c_str())); return nullptr; }
        return l_oR.Get();
    }

    // ---- helpers over the node tree ----
    int f_MotionOwner(int l_iN) const {   // the nearest node with motion, n included; -1
        for (; l_iN >= 0; l_iN = m_pModel->m_vNodes[(size_t)l_iN].m_iParent)
            if (m_pModel->m_oPhysics.m_vNodes[(size_t)l_iN].m_bHasMotion) return l_iN;
        return -1;
    }
    std::string f_NodeName(int l_iN) const {
        const std::string& l_strS = m_pModel->m_vNodes[(size_t)l_iN].m_strName;
        return "node '" + (l_strS.empty() ? std::to_string(l_iN) : l_strS) + "'";
    }

    int f_AddBody(JPH::BodyCreationSettings& l_oBs, Slot l_oSlot) {
        const int l_iIndex = (int)m_vSlots.size();
        l_oBs.mUserData |= (JPH::uint64)(JPH::uint32)l_iIndex;
        l_oBs.mCollisionGroup = JPH::CollisionGroup(m_oFilter.GetPtr(), (JPH::CollisionGroup::GroupID)l_iIndex, 0);
        JPH::BodyInterface& l_oBi = m_upSystem->GetBodyInterface();
        JPH::Body* l_pBody = l_oBi.CreateBody(l_oBs);
        if (!l_pBody) { m_vWarnings.push_back("physics: too many bodies"); return -1; }
        const bool l_bActive = l_oBs.mMotionType != JPH::EMotionType::Static;
        l_oBi.AddBody(l_pBody->GetID(), l_bActive ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        l_oSlot.m_oId = l_pBody->GetID();
        m_vSlots.push_back(l_oSlot);
        m_mapSlotById[l_oSlot.m_oId.GetIndexAndSequenceNumber()] = l_iIndex;
        return l_iIndex;
    }
};

PhysicsWorld::PhysicsWorld() : m_upImpl(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() { f_Clear(); }

bool PhysicsWorld::f_Built() const { return m_upImpl->m_upSystem != nullptr; }

void PhysicsWorld::f_Clear() {
    Impl& l_oW = *m_upImpl;
    if (l_oW.m_upSystem) {
        for (auto& l_oC : l_oW.m_vConstraints) l_oW.m_upSystem->RemoveConstraint(l_oC);
        JPH::BodyInterface& l_oBi = l_oW.m_upSystem->GetBodyInterface();
        for (const Impl::Slot& l_oS : l_oW.m_vSlots) {
            l_oBi.RemoveBody(l_oS.m_oId);
            l_oBi.DestroyBody(l_oS.m_oId);
        }
    }
    l_oW.m_vConstraints.clear();
    l_oW.m_upSystem.reset();
    l_oW.m_upJobs.reset();
    l_oW.m_upTemp.reset();
    l_oW.m_oFilter = nullptr;
    l_oW.m_vSlots.clear();
    l_oW.m_mapSlotById.clear();
    l_oW.m_vNodeBody.clear();
    l_oW.m_vOrder.clear();
    l_oW.m_setNoCollide.clear();
    l_oW.m_vWarnings.clear();
    l_oW.m_mapInside.clear();
    l_oW.m_vEvents.clear();
    l_oW.m_dAcc = 0.0;
    l_oW.m_iStepCount = 0;
    l_oW.m_pModel = nullptr;
}

bool PhysicsWorld::f_Build(const Model& l_oModel, const Pose& l_oPose, std::string* l_pError) {
    f_Clear();
    if (!l_oModel.m_oPhysics.f_Any()) {
        if (l_pError) *l_pError = "the model has no KHR_physics_rigid_bodies data";
        return false;
    }
    f_InitJolt();
    Impl& l_oW = *m_upImpl;
    l_oW.m_pModel = &l_oModel;
    const Physics& l_oPh = l_oModel.m_oPhysics;
    const size_t l_nNn = l_oModel.m_vNodes.size();

    l_oW.m_upTemp = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    l_oW.m_upJobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);   // one thread: deterministic
    l_oW.m_upSystem = std::make_unique<JPH::PhysicsSystem>();
    l_oW.m_upSystem->Init(8192, 0, 16384, 8192, l_oW.m_oBpLayers, l_oW.m_oObjVsBp, l_oW.m_oObjPairs);
    l_oW.m_upSystem->SetGravity(f_J(l_oW.m_v3Gravity));
    l_oW.m_upSystem->SetCombineFriction(f_CombineFriction);
    l_oW.m_upSystem->SetCombineRestitution(f_CombineRestitution);
    l_oW.m_oFilter = new Impl::Filter();
    l_oW.m_oFilter->m_pW = &l_oW;
    l_oW.m_oContacts.m_pW = &l_oW;
    l_oW.m_upSystem->SetContactListener(&l_oW.m_oContacts);

    // Nodes in parent-before-child order, and the ones an animation moves (with their descendants).
    std::vector<bool> l_vAnimated(l_nNn, false);
    for (const Animation& l_oA : l_oModel.m_vAnimations)
        for (const Channel& l_oC : l_oA.m_vChannels)
            if (l_oC.m_iNode >= 0 && l_oC.m_iNode < (int)l_nNn && l_oC.m_ePath != Path::Weights) l_vAnimated[(size_t)l_oC.m_iNode] = true;
    for (size_t l_nI = 0; l_nI < l_nNn; l_nI++)
        if (l_oModel.m_vNodes[l_nI].m_iParent < 0) {
            std::vector<int> l_vStack{(int)l_nI};
            while (!l_vStack.empty()) {
                const int l_iN = l_vStack.back();
                l_vStack.pop_back();
                l_oW.m_vOrder.push_back(l_iN);
                for (int l_iC : l_oModel.m_vNodes[(size_t)l_iN].m_vChildren) {
                    if (l_vAnimated[(size_t)l_iN]) l_vAnimated[(size_t)l_iC] = true;
                    l_vStack.push_back(l_iC);
                }
            }
        }
    l_oW.m_vNodeBody.assign(l_nNn, -1);

    auto l_fnUserBits = [&](int l_iMaterial) {
        if (l_iMaterial < 0) return (JPH::uint64)0;
        const PhysicsMaterial& l_oM = l_oPh.m_vMaterials[(size_t)l_iMaterial];
        return ((JPH::uint64)l_oM.m_eFrictionCombine << g_iFrictionShift) | ((JPH::uint64)l_oM.m_eRestitutionCombine << g_iRestitutionShift);
    };
    auto l_fnApplyMaterial = [&](JPH::BodyCreationSettings& l_oBs, int l_iMaterial) {
        if (l_iMaterial < 0) { l_oBs.mFriction = 0.6f; l_oBs.mRestitution = 0.0f; return; }
        l_oBs.mFriction = l_oPh.m_vMaterials[(size_t)l_iMaterial].m_fDynamicFriction;
        l_oBs.mRestitution = l_oPh.m_vMaterials[(size_t)l_iMaterial].m_fRestitution;
        l_oBs.mUserData = l_fnUserBits(l_iMaterial);
    };

    // ---- rigid bodies: every node with motion, with the colliders it owns ----
    for (size_t l_nB = 0; l_nB < l_nNn; l_nB++) {
        const NodePhysics& l_oNp = l_oPh.m_vNodes[l_nB];
        if (!l_oNp.m_bHasMotion) continue;
        const TRS l_oBody = f_Decompose(l_oPose.f_World((int)l_nB));
        const glm::mat4 l_mat4BodyInv = glm::inverse(f_Rt(l_oBody.m_v3T, l_oBody.m_qR));
        const bool l_bIsAnimated = l_vAnimated[l_nB];
        const bool l_bKinematic = l_oNp.m_bKinematic || l_oNp.m_fMass == 0.0f || l_bIsAnimated;
        JPH::StaticCompoundShapeSettings l_oCompound;
        int l_iParts = 0, l_iMaterial = -1, l_iFilterIndex = -1;
        JPH::RefConst<JPH::Shape> l_oSingle;
        glm::vec3 l_v3SingleT(0.0f);
        glm::quat l_qSingleR(1.0f, 0.0f, 0.0f, 0.0f);
        for (size_t l_nC = 0; l_nC < l_nNn; l_nC++) {
            const NodePhysics& l_oCp = l_oPh.m_vNodes[l_nC];
            if (!l_oCp.m_bHasCollider || l_oW.f_MotionOwner((int)l_nC) != (int)l_nB) continue;
            const TRS l_oRel = f_Decompose(l_mat4BodyInv * l_oPose.f_World((int)l_nC));
            JPH::RefConst<JPH::Shape> l_oShape = l_oW.f_MakeShape(l_oCp.m_oCollider, l_oRel.m_v3S, true, !l_bKinematic, "physics, " + l_oW.f_NodeName((int)l_nC));
            if (!l_oShape) continue;
            if (l_iMaterial < 0) l_iMaterial = l_oCp.m_iMaterial;
            if (l_iFilterIndex < 0) l_iFilterIndex = l_oCp.m_iFilter;
            l_oCompound.AddShape(f_J(l_oRel.m_v3T), f_J(l_oRel.m_qR), l_oShape);
            l_oSingle = l_oShape;
            l_v3SingleT = l_oRel.m_v3T;
            l_qSingleR = l_oRel.m_qR;
            l_iParts++;
        }
        if (l_iParts == 0) {
            l_oW.m_vWarnings.push_back("physics, " + l_oW.f_NodeName((int)l_nB) + ": a body with no collider (skipped)");
            continue;
        }
        JPH::RefConst<JPH::Shape> l_oShape;
        if (l_iParts == 1 && glm::length(l_v3SingleT) < 1e-6f && std::abs(std::abs(l_qSingleR.w) - 1.0f) < 1e-6f) l_oShape = l_oSingle;
        else if (l_iParts == 1) l_oShape = JPH::RotatedTranslatedShapeSettings(f_J(l_v3SingleT), f_J(l_qSingleR), l_oSingle).Create().Get();
        else l_oShape = l_oCompound.Create().Get();
        if (l_oNp.m_bHasCenterOfMass) {
            const JPH::Vec3 l_oOffset = f_J(l_oNp.m_v3CenterOfMass * l_oBody.m_v3S) - l_oShape->GetCenterOfMass();
            l_oShape = JPH::OffsetCenterOfMassShapeSettings(l_oOffset, l_oShape).Create().Get();
        }
        JPH::BodyCreationSettings l_oBs(l_oShape, f_J(l_oBody.m_v3T), f_J(l_oBody.m_qR), l_bKinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
                                     Layers::g_u16Moving);
        l_fnApplyMaterial(l_oBs, l_iMaterial);
        l_oBs.mGravityFactor = l_oNp.m_fGravityFactor;
        l_oBs.mLinearVelocity = f_J(l_oBody.m_qR * l_oNp.m_v3LinearVelocity);
        l_oBs.mAngularVelocity = f_J(l_oBody.m_qR * l_oNp.m_v3AngularVelocity);
        l_oBs.mAllowSleeping = true;
        if (!l_bKinematic && l_oNp.m_fMass > 0.0f) {
            l_oBs.mMassPropertiesOverride.mMass = l_oNp.m_fMass;
            if (l_oNp.m_bHasInertia) {
                glm::vec3 l_v3D = l_oNp.m_v3InertiaDiagonal;
                for (int l_iK = 0; l_iK < 3; l_iK++)
                    if (l_v3D[l_iK] <= 0.0f) l_v3D[l_iK] = 1.0e6f * l_oNp.m_fMass;   // 0 = "infinite": no rotation about it
                const JPH::Mat44 l_oR = JPH::Mat44::sRotation(f_J(l_oNp.m_qInertiaOrientation));
                l_oBs.mMassPropertiesOverride.mInertia = l_oR * JPH::Mat44::sScale(f_J(l_v3D)) * l_oR.Transposed();
                l_oBs.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            } else {
                l_oBs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            }
        }
        Impl::Slot l_oSlot;
        l_oSlot.m_iNode = (int)l_nB;
        l_oSlot.m_eKind = l_bKinematic ? Impl::Kind::Kinematic : Impl::Kind::Dynamic;
        l_oSlot.m_bAnimated = l_bIsAnimated;
        l_oSlot.m_bWriteBack = !l_bIsAnimated;
        l_oSlot.m_iFilter = l_iFilterIndex;
        l_oSlot.m_v3WorldScale = l_oBody.m_v3S;
        const int l_iIndex = l_oW.f_AddBody(l_oBs, l_oSlot);
        if (l_iIndex >= 0) l_oW.m_vNodeBody[l_nB] = l_iIndex;
    }

    // ---- static colliders: a collider with no motion in its ancestry (kinematic when an animation moves it) ----
    for (size_t l_nC = 0; l_nC < l_nNn; l_nC++) {
        const NodePhysics& l_oCp = l_oPh.m_vNodes[l_nC];
        if (!l_oCp.m_bHasCollider || l_oW.f_MotionOwner((int)l_nC) >= 0) continue;
        const TRS l_oT = f_Decompose(l_oPose.f_World((int)l_nC));
        const bool l_bMoving = l_vAnimated[l_nC];
        JPH::RefConst<JPH::Shape> l_oShape = l_oW.f_MakeShape(l_oCp.m_oCollider, l_oT.m_v3S, l_bMoving, false, "physics, " + l_oW.f_NodeName((int)l_nC));
        if (!l_oShape) continue;
        JPH::BodyCreationSettings l_oBs(l_oShape, f_J(l_oT.m_v3T), f_J(l_oT.m_qR), l_bMoving ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
                                     l_bMoving ? Layers::g_u16Moving : Layers::g_u16Static);
        l_fnApplyMaterial(l_oBs, l_oCp.m_iMaterial);
        Impl::Slot l_oSlot;
        l_oSlot.m_iNode = (int)l_nC;
        l_oSlot.m_eKind = l_bMoving ? Impl::Kind::Kinematic : Impl::Kind::Static;
        l_oSlot.m_bAnimated = l_bMoving;
        l_oSlot.m_iFilter = l_oCp.m_iFilter;
        l_oSlot.m_v3WorldScale = l_oT.m_v3S;
        const int l_iIndex = l_oW.f_AddBody(l_oBs, l_oSlot);
        if (l_iIndex >= 0) l_oW.m_vNodeBody[l_nC] = l_iIndex;
    }

    // ---- triggers: sensors; inside a moving body they follow it ----
    for (size_t l_nT = 0; l_nT < l_nNn; l_nT++) {
        const NodePhysics& l_oTp = l_oPh.m_vNodes[l_nT];
        if (!l_oTp.m_bHasTrigger || !l_oTp.m_oTrigger.f_Valid()) continue;
        int l_iReport = (int)l_nT;   // a part of a compound trigger reports the compound's node
        for (int l_iA = l_oModel.m_vNodes[l_nT].m_iParent; l_iA >= 0; l_iA = l_oModel.m_vNodes[(size_t)l_iA].m_iParent) {
            const auto& l_vTn = l_oPh.m_vNodes[(size_t)l_iA].m_vTriggerNodes;
            if (std::find(l_vTn.begin(), l_vTn.end(), (int)l_nT) != l_vTn.end()) { l_iReport = l_iA; break; }
        }
        const TRS l_oTw = f_Decompose(l_oPose.f_World((int)l_nT));
        const int l_iOwner = l_oW.f_MotionOwner((int)l_nT);
        const bool l_bMoving = l_iOwner >= 0 || l_vAnimated[l_nT];
        JPH::RefConst<JPH::Shape> l_oShape = l_oW.f_MakeShape(l_oTp.m_oTrigger, l_oTw.m_v3S, l_bMoving, false, "physics, trigger " + l_oW.f_NodeName((int)l_nT));
        if (!l_oShape) continue;
        JPH::BodyCreationSettings l_oBs(l_oShape, f_J(l_oTw.m_v3T), f_J(l_oTw.m_qR), l_bMoving ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
                                     Layers::g_u16Moving);
        l_oBs.mIsSensor = true;
        l_oBs.mCollideKinematicVsNonDynamic = true;
        Impl::Slot l_oSlot;
        l_oSlot.m_iNode = (int)l_nT;
        l_oSlot.m_eKind = Impl::Kind::Trigger;
        l_oSlot.m_iReport = l_iReport;
        l_oSlot.m_iFilter = l_oTp.m_iTriggerFilter;
        l_oSlot.m_bAnimated = l_iOwner < 0 && l_vAnimated[l_nT];
        if (l_iOwner >= 0 && l_oW.m_vNodeBody[(size_t)l_iOwner] >= 0) {
            l_oSlot.m_iFollow = l_oW.m_vNodeBody[(size_t)l_iOwner];
            const TRS l_oOw = f_Decompose(l_oPose.f_World(l_iOwner));
            l_oSlot.m_mat4FollowRel = glm::inverse(f_Rt(l_oOw.m_v3T, l_oOw.m_qR)) * f_Rt(l_oTw.m_v3T, l_oTw.m_qR);
        }
        l_oW.f_AddBody(l_oBs, l_oSlot);
    }

    // ---- joints: a six-degrees-of-freedom constraint between the two nodes' bodies ----
    auto l_fnBodyOf = [&](int l_iN) {
        for (; l_iN >= 0; l_iN = l_oModel.m_vNodes[(size_t)l_iN].m_iParent)
            if (l_oW.m_vNodeBody[(size_t)l_iN] >= 0) return l_oW.m_vNodeBody[(size_t)l_iN];
        return -1;
    };
    using Axis = JPH::SixDOFConstraintSettings::EAxis;
    for (size_t l_nJ = 0; l_nJ < l_nNn; l_nJ++) {
        const NodePhysics& l_oJp = l_oPh.m_vNodes[l_nJ];
        if (!l_oJp.m_bHasJoint) continue;
        const int l_iSa = l_fnBodyOf((int)l_nJ), l_iSb = l_fnBodyOf(l_oJp.m_iConnectedNode);
        const std::string l_strWhere = "physics, joint on " + l_oW.f_NodeName((int)l_nJ);
        if (l_iSa == l_iSb) { l_oW.m_vWarnings.push_back(l_strWhere + ": both ends are the same body (skipped)"); continue; }
        const TRS l_oFa = f_Decompose(l_oPose.f_World((int)l_nJ)), l_oFb = f_Decompose(l_oPose.f_World(l_oJp.m_iConnectedNode));
        JPH::SixDOFConstraintSettings l_oS;
        l_oS.mSpace = JPH::EConstraintSpace::WorldSpace;
        l_oS.mPosition1 = f_J(l_oFa.m_v3T);
        l_oS.mAxisX1 = f_J(l_oFa.m_qR * glm::vec3(1, 0, 0));
        l_oS.mAxisY1 = f_J(l_oFa.m_qR * glm::vec3(0, 1, 0));
        l_oS.mPosition2 = f_J(l_oFb.m_v3T);
        l_oS.mAxisX2 = f_J(l_oFb.m_qR * glm::vec3(1, 0, 0));
        l_oS.mAxisY2 = f_J(l_oFb.m_qR * glm::vec3(0, 1, 0));
        l_oS.mSwingType = JPH::ESwingType::Pyramid;
        for (int l_iA = 0; l_iA < (int)Axis::Num; l_iA++) l_oS.MakeFreeAxis((Axis)l_iA);
        const PhysicsJoint& l_oDef = l_oPh.m_vJoints[(size_t)l_oJp.m_iJoint];
        for (const PhysicsJointLimit& l_oL : l_oDef.m_vLimits) {
            if (!l_oL.m_bHasMin && !l_oL.m_bHasMax) continue;
            auto l_fnLimit = [&](int l_iAxisIndex, bool l_bAngular) {
                const Axis l_eAx = (Axis)((l_bAngular ? (int)Axis::RotationX : (int)Axis::TranslationX) + std::clamp(l_iAxisIndex, 0, 2));
                float l_fLo = l_oL.m_bHasMin ? l_oL.m_fMin : -FLT_MAX, l_fHi = l_oL.m_bHasMax ? l_oL.m_fMax : FLT_MAX;
                if (l_bAngular) {
                    const float l_fCap = l_eAx == Axis::RotationX ? JPH::JPH_PI : 0.999f * JPH::JPH_PI;
                    l_fLo = std::clamp(l_fLo, -l_fCap, l_fCap);
                    l_fHi = std::clamp(l_fHi, -l_fCap, l_fCap);
                }
                if (l_fLo == 0.0f && l_fHi == 0.0f && (l_bAngular || l_oL.m_fStiffness < 0.0f)) l_oS.MakeFixedAxis(l_eAx);
                else l_oS.SetLimitedAxis(l_eAx, l_fLo, l_fHi);
                if (!l_bAngular && l_oL.m_fStiffness >= 0.0f)
                    l_oS.mLimitsSpringSettings[(int)l_eAx] = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, l_oL.m_fStiffness, l_oL.m_fDamping);
            };
            for (int l_iA : l_oL.m_vLinearAxes) l_fnLimit(l_iA, false);
            for (int l_iA : l_oL.m_vAngularAxes) l_fnLimit(l_iA, true);
        }
        struct Motor { Axis m_eAxis; JPH::EMotorState m_eState; };
        std::vector<Motor> l_vMotors;
        glm::vec3 l_v3LinPos(0.0f), l_v3LinVel(0.0f), l_v3AngPos(0.0f), l_v3AngVel(0.0f);
        for (const PhysicsJointDrive& l_oD : l_oDef.m_vDrives) {
            const Axis l_eAx = (Axis)((l_oD.m_bAngular ? (int)Axis::RotationX : (int)Axis::TranslationX) + l_oD.m_iAxis);
            JPH::MotorSettings& l_oMs = l_oS.mMotorSettings[(int)l_eAx];
            l_oMs.mSpringSettings = JPH::SpringSettings(l_oD.m_bAcceleration ? JPH::ESpringMode::MassNormalizedStiffnessAndDamping
                                                                    : JPH::ESpringMode::StiffnessAndDamping,
                                                     std::max(0.0f, l_oD.m_fStiffness), std::max(0.0f, l_oD.m_fDamping));
            if (l_oD.m_fMaxForce >= 0.0f) {
                if (l_oD.m_bAngular) l_oMs.SetTorqueLimit(l_oD.m_fMaxForce);
                else l_oMs.SetForceLimit(l_oD.m_fMaxForce);
            }
            const bool l_bPosition = l_oD.m_bHasPositionTarget && l_oD.m_fStiffness > 0.0f;
            const bool l_bVelocity = !l_bPosition && (l_oD.m_bHasVelocityTarget || l_oD.m_fDamping > 0.0f);
            if (!l_bPosition && !l_bVelocity) continue;
            l_vMotors.push_back({l_eAx, l_bPosition ? JPH::EMotorState::Position : JPH::EMotorState::Velocity});
            glm::vec3& l_v3Target = l_oD.m_bAngular ? (l_bPosition ? l_v3AngPos : l_v3AngVel) : (l_bPosition ? l_v3LinPos : l_v3LinVel);
            l_v3Target[l_oD.m_iAxis] = l_bPosition ? l_oD.m_fPositionTarget : l_oD.m_fVelocityTarget;
        }
        JPH::BodyInterface& l_oBi = l_oW.m_upSystem->GetBodyInterface();
        const JPH::BodyID l_oIa = l_iSa >= 0 ? l_oW.m_vSlots[(size_t)l_iSa].m_oId : JPH::BodyID(), l_oIb = l_iSb >= 0 ? l_oW.m_vSlots[(size_t)l_iSb].m_oId : JPH::BodyID();
        JPH::TwoBodyConstraint* l_pC = l_oBi.CreateConstraint(&l_oS, l_oIa, l_oIb);
        if (!l_pC) { l_oW.m_vWarnings.push_back(l_strWhere + ": could not be created"); continue; }
        auto* l_pSix = static_cast<JPH::SixDOFConstraint*>(l_pC);
        for (const Motor& l_oM : l_vMotors) l_pSix->SetMotorState(l_oM.m_eAxis, l_oM.m_eState);
        l_pSix->SetTargetPositionCS(f_J(l_v3LinPos));
        l_pSix->SetTargetVelocityCS(f_J(l_v3LinVel));
        l_pSix->SetTargetAngularVelocityCS(f_J(l_v3AngVel));
        l_pSix->SetTargetOrientationCS(JPH::Quat::sEulerAngles(f_J(l_v3AngPos)));
        l_oW.m_upSystem->AddConstraint(l_pC);
        l_oW.m_vConstraints.push_back(l_pC);
        if (!l_oJp.m_bEnableCollision && l_iSa >= 0 && l_iSb >= 0)
            l_oW.m_setNoCollide.insert({(JPH::uint32)std::min(l_iSa, l_iSb), (JPH::uint32)std::max(l_iSa, l_iSb)});
        if (!l_oIa.IsInvalid()) l_oBi.ActivateBody(l_oIa);
        if (!l_oIb.IsInvalid()) l_oBi.ActivateBody(l_oIb);
    }

    l_oW.m_upSystem->OptimizeBroadPhase();
    return true;
}

void PhysicsWorld::f_Step(float l_fDt, Pose& l_oPose, int l_iMaxSteps) {
    Impl& l_oW = *m_upImpl;
    if (!l_oW.m_upSystem || l_fDt <= 0.0f) return;
    l_oW.m_dAcc += l_fDt;
    int l_iN = 0;
    JPH::BodyInterface& l_oBi = l_oW.m_upSystem->GetBodyInterface();
    while (l_oW.m_dAcc >= m_fStep - 1e-7 && (l_iMaxSteps <= 0 || l_iN < l_iMaxSteps)) {
        // Animated bodies go where the pose puts them; triggers inside a moving body follow it.
        l_oPose.f_UpdateWorld();
        for (const Impl::Slot& l_oS : l_oW.m_vSlots) {
            if (l_oS.m_bAnimated) {
                const TRS l_oT = f_Decompose(l_oPose.f_World(l_oS.m_iNode));
                l_oBi.MoveKinematic(l_oS.m_oId, f_J(l_oT.m_v3T), f_J(l_oT.m_qR), m_fStep);
            } else if (l_oS.m_iFollow >= 0) {
                JPH::RVec3 l_oP;
                JPH::Quat l_oQ;
                l_oBi.GetPositionAndRotation(l_oW.m_vSlots[(size_t)l_oS.m_iFollow].m_oId, l_oP, l_oQ);
                const TRS l_oT = f_Decompose(f_Rt(f_G(l_oP), f_G(l_oQ)) * l_oS.m_mat4FollowRel);
                l_oBi.MoveKinematic(l_oS.m_oId, f_J(l_oT.m_v3T), f_J(l_oT.m_qR), m_fStep);
            }
        }
        l_oW.m_upSystem->Update(m_fStep, 1, l_oW.m_upTemp.get(), l_oW.m_upJobs.get());
        l_oW.m_dAcc -= m_fStep;
        l_oW.m_iStepCount++;
        l_iN++;
    }
    if (l_iMaxSteps > 0 && l_iN == l_iMaxSteps && l_oW.m_dAcc > m_fStep) l_oW.m_dAcc = 0.0;   // too far behind: drop the backlog
    if (l_iN > 0) f_WritePose(l_oPose);
}

void PhysicsWorld::f_WritePose(Pose& l_oPose) const {
    const Impl& l_oW = *m_upImpl;
    if (!l_oW.m_upSystem) return;
    const JPH::BodyInterface& l_oBi = l_oW.m_upSystem->GetBodyInterface();
    // Moving bodies into the pose, parents first (a body's node may sit under another body's node).
    const Model& l_oM = *l_oW.m_pModel;
    std::vector<glm::mat4> l_vWorld(l_oM.m_vNodes.size(), glm::mat4(1.0f));
    for (int l_iNode : l_oW.m_vOrder) {
        const int l_iParent = l_oM.m_vNodes[(size_t)l_iNode].m_iParent;
        const glm::mat4 l_mat4ParentWorld = l_iParent >= 0 ? l_vWorld[(size_t)l_iParent] : glm::mat4(1.0f);
        const int l_iS = l_oW.m_vNodeBody[(size_t)l_iNode];
        if (l_iS >= 0 && l_oW.m_vSlots[(size_t)l_iS].m_bWriteBack) {
            JPH::RVec3 l_oP;
            JPH::Quat l_oQ;
            l_oBi.GetPositionAndRotation(l_oW.m_vSlots[(size_t)l_iS].m_oId, l_oP, l_oQ);
            const glm::mat4 l_mat4Desired = f_Rt(f_G(l_oP), f_G(l_oQ)) * glm::scale(glm::mat4(1.0f), l_oW.m_vSlots[(size_t)l_iS].m_v3WorldScale);
            const TRS l_oLocal = f_Decompose(glm::inverse(l_mat4ParentWorld) * l_mat4Desired);
            l_oPose.f_SetLocal(l_iNode, l_oLocal.m_v3T, l_oLocal.m_qR, l_oPose.f_Scale(l_iNode));
        }
        l_vWorld[(size_t)l_iNode] = l_mat4ParentWorld * f_ComposeTRS(l_oPose.f_Translation(l_iNode), l_oPose.f_Rotation(l_iNode), l_oPose.f_Scale(l_iNode));
    }
    l_oPose.f_UpdateWorld();
}

void PhysicsWorld::f_SimulateTo(double l_dSeconds, Pose& l_oPose) {
    const int l_iTarget = (int)std::floor(l_dSeconds / m_fStep + 1e-3);   // 2.5 s is 150 steps, not 149.9999
    const int l_iTodo = l_iTarget - m_upImpl->m_iStepCount;
    if (l_iTodo > 0) f_Step((float)(l_iTodo * (double)m_fStep) + 1e-6f, l_oPose, 0);
}

double PhysicsWorld::f_Time() const { return m_upImpl->m_iStepCount * (double)m_fStep; }
int PhysicsWorld::f_Steps() const { return m_upImpl->m_iStepCount; }

void PhysicsWorld::f_SetGravity(const glm::vec3& l_v3G) {
    m_upImpl->m_v3Gravity = l_v3G;
    if (m_upImpl->m_upSystem) m_upImpl->m_upSystem->SetGravity(f_J(l_v3G));
}
glm::vec3 PhysicsWorld::f_Gravity() const { return m_upImpl->m_v3Gravity; }

std::vector<TriggerEvent> PhysicsWorld::f_TakeEvents() {
    std::lock_guard<std::mutex> l_oG(m_upImpl->m_oEventLock);
    std::vector<TriggerEvent> l_vOut;
    l_vOut.swap(m_upImpl->m_vEvents);
    return l_vOut;
}

std::vector<int> PhysicsWorld::f_Inside(int l_iTriggerNode) const {
    std::lock_guard<std::mutex> l_oG(m_upImpl->m_oEventLock);
    std::vector<int> l_vOut;
    for (const auto& [l_pairKey, l_iN] : m_upImpl->m_mapInside)
        if (l_pairKey.first == l_iTriggerNode && l_iN > 0) l_vOut.push_back(l_pairKey.second);
    return l_vOut;
}

bool PhysicsWorld::f_IsSleeping(int l_iNode) const {
    const Impl& l_oW = *m_upImpl;
    if (!l_oW.m_upSystem || l_iNode < 0 || l_iNode >= (int)l_oW.m_vNodeBody.size() || l_oW.m_vNodeBody[(size_t)l_iNode] < 0) return false;
    return !l_oW.m_upSystem->GetBodyInterface().IsActive(l_oW.m_vSlots[(size_t)l_oW.m_vNodeBody[(size_t)l_iNode]].m_oId);
}

glm::vec3 PhysicsWorld::f_LinearVelocity(int l_iNode) const {
    const Impl& l_oW = *m_upImpl;
    if (!l_oW.m_upSystem || l_iNode < 0 || l_iNode >= (int)l_oW.m_vNodeBody.size() || l_oW.m_vNodeBody[(size_t)l_iNode] < 0) return glm::vec3(0.0f);
    return f_G(l_oW.m_upSystem->GetBodyInterface().GetLinearVelocity(l_oW.m_vSlots[(size_t)l_oW.m_vNodeBody[(size_t)l_iNode]].m_oId));
}

PhysicsWorld::Stats PhysicsWorld::f_Stats() const {
    Stats l_oSt;
    const Impl& l_oW = *m_upImpl;
    if (!l_oW.m_upSystem) return l_oSt;
    const JPH::BodyInterface& l_oBi = l_oW.m_upSystem->GetBodyInterface();
    for (const Impl::Slot& l_oS : l_oW.m_vSlots) {
        switch (l_oS.m_eKind) {
        case Impl::Kind::Dynamic:
            l_oSt.m_iDynamic++;
            if (l_oBi.IsActive(l_oS.m_oId)) l_oSt.m_iAwake++;
            break;
        case Impl::Kind::Kinematic: l_oSt.m_iKinematic++; break;
        case Impl::Kind::Static: l_oSt.m_iStatics++; break;
        case Impl::Kind::Trigger: l_oSt.m_iTriggers++; break;
        }
    }
    l_oSt.m_iJoints = (int)l_oW.m_vConstraints.size();
    return l_oSt;
}

const std::vector<std::string>& PhysicsWorld::f_Warnings() const { return m_upImpl->m_vWarnings; }

void PhysicsWorld::f_DebugBoxes(std::vector<PhysicsDebugBox>& l_vOut) const {
    l_vOut.clear();
    const Impl& l_oW = *m_upImpl;
    if (!l_oW.m_upSystem) return;
    const JPH::BodyInterface& l_oBi = l_oW.m_upSystem->GetBodyInterface();
    for (const Impl::Slot& l_oS : l_oW.m_vSlots) {
        JPH::RefConst<JPH::Shape> l_oShape = l_oBi.GetShape(l_oS.m_oId);
        if (!l_oShape) continue;
        JPH::AABox l_oB = l_oShape->GetLocalBounds();
        if (l_oB.GetExtent().GetX() > 500.0f) l_oB = JPH::AABox(JPH::Vec3(-5, -0.05f, -5), JPH::Vec3(5, 0, 5));   // an infinite plane
        JPH::RVec3 l_oP;
        JPH::Quat l_oQ;
        l_oBi.GetPositionAndRotation(l_oS.m_oId, l_oP, l_oQ);
        PhysicsDebugBox l_oD;
        // Jolt's body position is the shape's origin; local bounds are around the centre of mass.
        const glm::vec3 l_v3Com = f_G(l_oShape->GetCenterOfMass());
        l_oD.m_mat4World = f_Rt(f_G(l_oP), f_G(l_oQ)) * glm::translate(glm::mat4(1.0f), l_v3Com + f_G(l_oB.GetCenter()));
        l_oD.m_v3HalfExtents = f_G(l_oB.GetExtent());
        l_oD.m_eKind = l_oS.m_eKind == Impl::Kind::Static ? PhysicsDebugBox::Kind::Static
               : l_oS.m_eKind == Impl::Kind::Kinematic ? PhysicsDebugBox::Kind::Kinematic
               : l_oS.m_eKind == Impl::Kind::Dynamic ? PhysicsDebugBox::Kind::Dynamic : PhysicsDebugBox::Kind::Trigger;
        l_oD.m_bSleeping = l_oS.m_eKind == Impl::Kind::Dynamic && !l_oBi.IsActive(l_oS.m_oId);
        l_oD.m_iNode = l_oS.m_iNode;
        l_vOut.push_back(l_oD);
    }
}

}  // namespace toms::gltf
