// node.h — 2D scene-graph node (port of FM79979 Frame, adapted to 2D + glm).
//
// Each Node has a parent, a first child and a next sibling (an intrusive tree).
// It carries a LOCAL transform and a cached WORLD transform. The world transform
// is recomputed lazily only when dirty:
//        world = (parent ? parent->world * local : local)
// This mirrors Frame's semantics: setting a local transform marks this node and
// all descendants dirty; GetWorldTransform() refreshes the cache on demand.
//
// Conventions (same as Frame): X = right, Y = up. For 2D we use glm::mat3 affine
// transforms (column-major: columns are [right, up, translation]).
//
// Node also inherits Object (src/object.h) so every node is a typed, named,
// unique-ID'd, shared_ptr-friendly game object.
#pragma once
#include "object.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

namespace toms {

constexpr float g_fNODEDIRTYWORLDCACHE = 1e10f;  // sentinel: world cache invalid

class Node : public Object {
public:
    Node() { f_ResetCache(); }
    explicit Node(const std::string& l_strName) { f_SetName(l_strName); f_ResetCache(); }
    virtual ~Node() { f_SetParent(nullptr); }

    // ---- hierarchy ----
    void f_AddChild(Node* l_pChild, bool l_bUpdateRelatedPosition = true);
    void f_AddChildToLast(Node* l_pChild, bool l_bUpdateRelatedPosition = true);
    void f_SetParent(Node* l_pParent, bool l_bUpdateRelatedPosition = true);  // nullptr = unparent
    void f_SetNextSibling(Node* l_pSib);
    Node* f_GetParent() const { return m_pParent; }
    Node* f_GetFirstChild() const { return m_pFirstChild; }
    Node* f_GetNextSibling() const { return m_pNextSibling; }
    int f_GetChildCount() const {
        int l_iC = 0; for (Node* l_pP = m_pFirstChild; l_pP; l_pP = l_pP->m_pNextSibling) ++l_iC; return l_iC;
    }
    bool f_IsChild(Node* l_pOther) const {
        if (!l_pOther) return false;
        for (Node* l_pP = m_pFirstChild; l_pP; l_pP = l_pP->m_pNextSibling) if (l_pP == l_pOther) return true;
        return false;
    }
    bool f_IsAncestor(Node* l_pOther) const {
        if (!l_pOther) return true;
        if (l_pOther == this) return false;
        for (Node* l_pP = m_pParent; l_pP; l_pP = l_pP->m_pParent) if (l_pP == l_pOther) return true;
        return false;
    }

    // ---- local transform ----
    const glm::mat3& f_GetLocalTransform() const { return m_mat3Local; }
    void f_SetLocalTransform(const glm::mat3& l_mat3Lt, bool l_bAllChildDirty = true);
    void f_SetLocalPosition(const glm::vec2& l_v2P);
    glm::vec2 f_GetLocalPosition() const { return glm::vec2(m_mat3Local[2]); }
    void f_SetLocalScale(const glm::vec2& l_v2S);
    void f_SetLocalRotation(float l_fRad);  // Z rotation

    // ---- world transform ----
    glm::mat3 f_GetWorldTransform();
    void f_SetWorldTransform(const glm::mat3& l_mat3Wt);
    glm::vec2 f_GetWorldPosition();
    void f_SetWorldPosition(const glm::vec2& l_v2P);
    // transform a local point/size by this node's world transform
    glm::vec2 f_WorldPoint(const glm::vec2& l_v2Local) {
        glm::vec3 l_v3R = f_GetWorldTransform() * glm::vec3(l_v2Local, 1.0f); return glm::vec2(l_v3R);
    }
    // axis-aligned pixel rect [x,y,w,h] of a local box of `size` at local origin
    glm::vec4 f_WorldRect(const glm::vec2& l_v2Size) {
        glm::vec2 l_v2A = f_WorldPoint(glm::vec2(0, 0));
        glm::vec2 l_v2B = f_WorldPoint(glm::vec2(l_v2Size.x, 0));
        glm::vec2 l_v2C = f_WorldPoint(glm::vec2(0, l_v2Size.y));
        glm::vec2 l_v2D = f_WorldPoint(glm::vec2(l_v2Size.x, l_v2Size.y));
        float l_fMinx = std::min({l_v2A.x, l_v2B.x, l_v2C.x, l_v2D.x});
        float l_fMiny = std::min({l_v2A.y, l_v2B.y, l_v2C.y, l_v2D.y});
        float l_fMaxx = std::max({l_v2A.x, l_v2B.x, l_v2C.x, l_v2D.x});
        float l_fMaxy = std::max({l_v2A.y, l_v2B.y, l_v2C.y, l_v2D.y});
        return glm::vec4(l_fMinx, l_fMiny, l_fMaxx - l_fMinx, l_fMaxy - l_fMiny);
    }
    // Uniform-scale convenience (a UI root is scaled by one number: 1.5 on a phone).
    void f_SetLocalScale(float l_fS) { f_SetLocalScale(glm::vec2(l_fS, l_fS)); }

    // ---- the INVERSE of the world transform (world -> local) ----
    // Needed by every hit-test that lives under a scaled/moved node: a pointer arrives in world
    // (screen/design) space and the control it must hit is positioned in the node's LOCAL space, so
    // the tap has to be mapped back through the same chain the drawing went through. Without this,
    // scaling a parent makes its children draw bigger and stop responding -- the bug this whole
    // design exists to avoid (see docs/project/progress_report/PROGRESS_REPORT.md, "wrong position and not working").
    glm::vec2 f_LocalPoint(const glm::vec2& l_v2World) {
        // Derived from WorldPoint() itself (the path the drawing uses, and the one the test already
        // proves): sample where the local origin and the two unit axes land in world space, then solve
        // the 2x2 system. Deliberately not glm::inverse() on the cached mat3 -- that returned ~0 here,
        // because the cache can still hold its dirty sentinel when read this way, and a silently wrong
        // inverse is exactly what makes a scaled UI draw big and stop responding to taps.
        const glm::vec2 l_v2O  = f_WorldPoint(glm::vec2(0.0f, 0.0f));
        const glm::vec2 l_v2Ex = f_WorldPoint(glm::vec2(1.0f, 0.0f)) - l_v2O;
        const glm::vec2 l_v2Ey = f_WorldPoint(glm::vec2(0.0f, 1.0f)) - l_v2O;
        const float l_fDet = l_v2Ex.x * l_v2Ey.y - l_v2Ey.x * l_v2Ex.y;
        if (std::abs(l_fDet) < 1e-6f) return l_v2World;          // degenerate: never NaN a hit-test
        const glm::vec2 l_v2D = l_v2World - l_v2O;
        return glm::vec2((l_v2D.x * l_v2Ey.y - l_v2D.y * l_v2Ey.x) / l_fDet,
                         (l_v2Ex.x * l_v2D.y - l_v2Ex.y * l_v2D.x) / l_fDet);
    }
    // Inverse of WorldRect: the world [x,y,w,h] box expressed in this node's local space.
    glm::vec4 f_LocalRect(const glm::vec4& l_v4WorldRect) {
        const glm::vec2 l_v2A = f_LocalPoint(glm::vec2(l_v4WorldRect.x, l_v4WorldRect.y));
        const glm::vec2 l_v2B = f_LocalPoint(glm::vec2(l_v4WorldRect.x + l_v4WorldRect.z, l_v4WorldRect.y + l_v4WorldRect.w));
        return glm::vec4(std::min(l_v2A.x, l_v2B.x), std::min(l_v2A.y, l_v2B.y),
                         std::abs(l_v2B.x - l_v2A.x), std::abs(l_v2B.y - l_v2A.y));
    }

    void f_Visit(const std::function<void(Node*)>& l_fnFn);

    // ---- visibility ----
    bool f_IsVisible() const { return m_bVisible; }
    void f_SetVisible(bool l_bV) { m_bVisible = l_bV; }

    // shared_ptr convenience: make a shared Node-derived object
    template <class T, class... Args>
    static std::shared_ptr<T> f_Make(Args&&... l_oArgs) { return Object::f_Make<T>(std::forward<Args>(l_oArgs)...); }

protected:
    void f_UpdateCachedWorldTransformIfNeeded();
    void f_SetCachedWorldTransformDirty();
    void f_ResetCache() {
        m_mat3Local = glm::mat3(1.0f);
        m_mat3World = glm::mat3(1e10f);  // mark dirty (sentinel on [0][0])
        m_pParent = nullptr; m_pFirstChild = nullptr; m_pNextSibling = nullptr;
        m_bVisible = true;
    }

    glm::mat3   m_mat3Local;
    glm::mat3   m_mat3World;     // cached; [0][0]==1e10 means dirty
    bool        m_bVisible;

    Node* m_pParent = nullptr;
    Node* m_pFirstChild = nullptr;
    Node* m_pNextSibling = nullptr;
};

// ---- free traversal helpers ----
inline void f_ForEachNode(Node* l_pRoot, const std::function<void(Node*)>& l_fnFn) {
    if (l_pRoot) l_pRoot->f_Visit(l_fnFn);
}

} // namespace toms
