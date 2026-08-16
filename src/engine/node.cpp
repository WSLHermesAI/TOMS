// node.cpp — implementation of the 2D scene-graph Node (port of FM79979 Frame).
#include "node.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

namespace toms {

void Node::AddChild(Node* child, bool /*updateRelatedPosition*/) {
    assert(child);
    child->SetParent(this);
    child->SetCachedWorldTransformDirty();
}

void Node::AddChildToLast(Node* child, bool /*updateRelatedPosition*/) {
    assert(child);
    Node* first = GetFirstChild();
    child->SetParent(nullptr);
    child->m_parent = this;
    if (first) {
        Node* p = first;
        while (p) {
            if (p->m_nextSibling) p = p->m_nextSibling;
            else { p->m_nextSibling = child; break; }
        }
    } else {
        m_firstChild = child;
    }
    child->SetCachedWorldTransformDirty();
}

void Node::SetParent(Node* parent, bool /*updateRelatedPosition*/) {
    UpdateCachedWorldTransformIfNeeded();
    if (m_parent == parent) return;
    // unlink from old parent's child list
    if (m_parent) {
        Node* last = nullptr;
        for (Node* s = m_parent->m_firstChild; s; s = s->m_nextSibling) {
            if (s == this) break;
            last = s;
        }
        assert(parent != m_parent || true);  // (parent arg may differ; keep simple)
        if (last) last->m_nextSibling = m_nextSibling;
        else m_parent->m_firstChild = m_nextSibling;
        m_nextSibling = nullptr;
        m_parent = nullptr;
    }
    // link into new parent's child list (at front)
    if (parent) {
        m_parent = parent;
        m_nextSibling = parent->m_firstChild;
        parent->m_firstChild = this;
    }
    SetCachedWorldTransformDirty();
}

void Node::SetNextSibling(Node* sib) {
    m_nextSibling = sib;
    if (sib) sib->m_parent = this->m_parent;
}

void Node::SetLocalTransform(const glm::mat3& lt, bool allChildDirty) {
    if (allChildDirty) SetCachedWorldTransformDirty();
    m_local = lt;
}

void Node::SetLocalPosition(const glm::vec2& p) {
    SetCachedWorldTransformDirty();
    m_local[2][0] = p.x; m_local[2][1] = p.y;
}

void Node::SetLocalScale(const glm::vec2& s) {
    SetCachedWorldTransformDirty();
    m_local[0][0] = s.x; m_local[1][1] = s.y;
}

void Node::SetLocalRotation(float rad) {
    SetCachedWorldTransformDirty();
    float c = std::cos(rad), s = std::sin(rad);
    m_local[0][0] = c;  m_local[0][1] = s;
    m_local[1][0] = -s; m_local[1][1] = c;
}

glm::mat3 Node::GetWorldTransform() {
    UpdateCachedWorldTransformIfNeeded();
    return m_world;
}

void Node::SetWorldTransform(const glm::mat3& wt) {
    if (m_parent) {
        m_parent->UpdateCachedWorldTransformIfNeeded();
        glm::mat3 pinv = glm::inverse(m_parent->m_world);
        m_local = pinv * wt;
    } else {
        m_local = wt;
    }
    SetCachedWorldTransformDirty();
}

glm::vec2 Node::GetWorldPosition() {
    UpdateCachedWorldTransformIfNeeded();
    return glm::vec2(m_world[2]);
}

void Node::SetWorldPosition(const glm::vec2& p) {
    UpdateCachedWorldTransformIfNeeded();
    glm::mat3 wt = m_world;
    wt[2][0] = p.x; wt[2][1] = p.y;
    SetWorldTransform(wt);
}

void Node::Visit(const std::function<void(Node*)>& fn) {
    if (!this) return;
    fn(this);
    if (m_firstChild) m_firstChild->Visit(fn);
    if (m_nextSibling) m_nextSibling->Visit(fn);
}

void Node::UpdateCachedWorldTransformIfNeeded() {
    if (m_world[0][0] == NODE_DIRTY_WORLD_CACHE) {
        if (m_parent) {
            m_parent->UpdateCachedWorldTransformIfNeeded();
            m_world = m_parent->m_world * m_local;
        } else {
            m_world = m_local;
        }
    }
}

void Node::SetCachedWorldTransformDirty() {
    if (m_world[0][0] != NODE_DIRTY_WORLD_CACHE) {
        m_world[0][0] = NODE_DIRTY_WORLD_CACHE;
        for (Node* c = m_firstChild; c; c = c->m_nextSibling)
            c->SetCachedWorldTransformDirty();
    }
}

} // namespace toms
