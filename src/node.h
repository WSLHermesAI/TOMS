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

constexpr float NODE_DIRTY_WORLD_CACHE = 1e10f;  // sentinel: world cache invalid

class Node : public Object {
public:
    Node() { resetCache(); }
    explicit Node(const std::string& name) { SetName(name); resetCache(); }
    virtual ~Node() { SetParent(nullptr); }

    const std::string& name() const { return Name(); }
    void setName(const std::string& n) { SetName(n); }

    // ---- hierarchy ----
    void AddChild(Node* child, bool updateRelatedPosition = true);
    void AddChildToLast(Node* child, bool updateRelatedPosition = true);
    void SetParent(Node* parent, bool updateRelatedPosition = true);  // nullptr = unparent
    void SetNextSibling(Node* sib);
    Node* GetParent() const { return m_parent; }
    Node* GetFirstChild() const { return m_firstChild; }
    Node* GetNextSibling() const { return m_nextSibling; }
    int GetChildCount() const {
        int c = 0; for (Node* p = m_firstChild; p; p = p->m_nextSibling) ++c; return c;
    }
    bool IsChild(Node* other) const {
        if (!other) return false;
        for (Node* p = m_firstChild; p; p = p->m_nextSibling) if (p == other) return true;
        return false;
    }
    bool IsAncestor(Node* other) const {
        if (!other) return true;
        if (other == this) return false;
        for (Node* p = m_parent; p; p = p->m_parent) if (p == other) return true;
        return false;
    }

    // ---- local transform ----
    const glm::mat3& GetLocalTransform() const { return m_local; }
    void SetLocalTransform(const glm::mat3& lt, bool allChildDirty = true);
    void SetLocalPosition(const glm::vec2& p);
    glm::vec2 GetLocalPosition() const { return glm::vec2(m_local[2]); }
    void SetLocalScale(const glm::vec2& s);
    void SetLocalRotation(float rad);  // Z rotation

    // ---- world transform ----
    glm::mat3 GetWorldTransform();
    void SetWorldTransform(const glm::mat3& wt);
    glm::vec2 GetWorldPosition();
    void SetWorldPosition(const glm::vec2& p);
    // transform a local point/size by this node's world transform
    glm::vec2 WorldPoint(const glm::vec2& local) {
        glm::vec3 r = GetWorldTransform() * glm::vec3(local, 1.0f); return glm::vec2(r);
    }
    // axis-aligned pixel rect [x,y,w,h] of a local box of `size` at local origin
    glm::vec4 WorldRect(const glm::vec2& size) {
        glm::vec2 a = WorldPoint(glm::vec2(0, 0));
        glm::vec2 b = WorldPoint(glm::vec2(size.x, 0));
        glm::vec2 c = WorldPoint(glm::vec2(0, size.y));
        glm::vec2 d = WorldPoint(glm::vec2(size.x, size.y));
        float minx = std::min({a.x, b.x, c.x, d.x});
        float miny = std::min({a.y, b.y, c.y, d.y});
        float maxx = std::max({a.x, b.x, c.x, d.x});
        float maxy = std::max({a.y, b.y, c.y, d.y});
        return glm::vec4(minx, miny, maxx - minx, maxy - miny);
    }
    void Visit(const std::function<void(Node*)>& fn);

    // ---- visibility ----
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }

    // shared_ptr convenience: make a shared Node-derived object
    template <class T, class... Args>
    static std::shared_ptr<T> Make(Args&&... args) { return Object::Make<T>(std::forward<Args>(args)...); }

protected:
    void UpdateCachedWorldTransformIfNeeded();
    void SetCachedWorldTransformDirty();
    void resetCache() {
        m_local = glm::mat3(1.0f);
        m_world = glm::mat3(1e10f);  // mark dirty (sentinel on [0][0])
        m_parent = nullptr; m_firstChild = nullptr; m_nextSibling = nullptr;
        m_visible = true;
    }

    glm::mat3   m_local;
    glm::mat3   m_world;     // cached; [0][0]==1e10 means dirty
    bool        m_visible;

    Node* m_parent = nullptr;
    Node* m_firstChild = nullptr;
    Node* m_nextSibling = nullptr;
};

// ---- free traversal helpers ----
inline void ForEachNode(Node* root, const std::function<void(Node*)>& fn) {
    if (root) root->Visit(fn);
}

} // namespace toms
