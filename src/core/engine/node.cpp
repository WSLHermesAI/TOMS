// node.cpp — implementation of the 2D scene-graph Node (port of FM79979 Frame).
#include "node.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

namespace toms {

void Node::f_AddChild(Node* l_pChild, bool /*updateRelatedPosition*/) {
    assert(l_pChild);
    l_pChild->f_SetParent(this);
    l_pChild->f_SetCachedWorldTransformDirty();
}

void Node::f_AddChildToLast(Node* l_pChild, bool /*updateRelatedPosition*/) {
    assert(l_pChild);
    Node* l_pFirst = f_GetFirstChild();
    l_pChild->f_SetParent(nullptr);
    l_pChild->m_pParent = this;
    if (l_pFirst) {
        Node* l_pP = l_pFirst;
        while (l_pP) {
            if (l_pP->m_pNextSibling) l_pP = l_pP->m_pNextSibling;
            else { l_pP->m_pNextSibling = l_pChild; break; }
        }
    } else {
        m_pFirstChild = l_pChild;
    }
    l_pChild->f_SetCachedWorldTransformDirty();
}

void Node::f_SetParent(Node* l_pParent, bool /*updateRelatedPosition*/) {
    f_UpdateCachedWorldTransformIfNeeded();
    if (m_pParent == l_pParent) return;
    // unlink from old parent's child list
    if (m_pParent) {
        Node* l_pLast = nullptr;
        for (Node* l_pS = m_pParent->m_pFirstChild; l_pS; l_pS = l_pS->m_pNextSibling) {
            if (l_pS == this) break;
            l_pLast = l_pS;
        }
        assert(l_pParent != m_pParent || true);  // (parent arg may differ; keep simple)
        if (l_pLast) l_pLast->m_pNextSibling = m_pNextSibling;
        else m_pParent->m_pFirstChild = m_pNextSibling;
        m_pNextSibling = nullptr;
        m_pParent = nullptr;
    }
    // link into new parent's child list (at front)
    if (l_pParent) {
        m_pParent = l_pParent;
        m_pNextSibling = l_pParent->m_pFirstChild;
        l_pParent->m_pFirstChild = this;
    }
    f_SetCachedWorldTransformDirty();
}

void Node::f_SetNextSibling(Node* l_pSib) {
    m_pNextSibling = l_pSib;
    if (l_pSib) l_pSib->m_pParent = this->m_pParent;
}

void Node::f_SetLocalTransform(const glm::mat3& l_mat3Lt, bool l_bAllChildDirty) {
    if (l_bAllChildDirty) f_SetCachedWorldTransformDirty();
    m_mat3Local = l_mat3Lt;
}

void Node::f_SetLocalPosition(const glm::vec2& l_v2P) {
    f_SetCachedWorldTransformDirty();
    m_mat3Local[2][0] = l_v2P.x; m_mat3Local[2][1] = l_v2P.y;
}

void Node::f_SetLocalScale(const glm::vec2& l_v2S) {
    f_SetCachedWorldTransformDirty();
    m_mat3Local[0][0] = l_v2S.x; m_mat3Local[1][1] = l_v2S.y;
}

void Node::f_SetLocalRotation(float l_fRad) {
    f_SetCachedWorldTransformDirty();
    float l_fC = std::cos(l_fRad), l_fS = std::sin(l_fRad);
    m_mat3Local[0][0] = l_fC;  m_mat3Local[0][1] = l_fS;
    m_mat3Local[1][0] = -l_fS; m_mat3Local[1][1] = l_fC;
}

glm::mat3 Node::f_GetWorldTransform() {
    f_UpdateCachedWorldTransformIfNeeded();
    return m_mat3World;
}

void Node::f_SetWorldTransform(const glm::mat3& l_mat3Wt) {
    if (m_pParent) {
        m_pParent->f_UpdateCachedWorldTransformIfNeeded();
        glm::mat3 l_mat3Pinv = glm::inverse(m_pParent->m_mat3World);
        m_mat3Local = l_mat3Pinv * l_mat3Wt;
    } else {
        m_mat3Local = l_mat3Wt;
    }
    f_SetCachedWorldTransformDirty();
}

glm::vec2 Node::f_GetWorldPosition() {
    f_UpdateCachedWorldTransformIfNeeded();
    return glm::vec2(m_mat3World[2]);
}

void Node::f_SetWorldPosition(const glm::vec2& l_v2P) {
    f_UpdateCachedWorldTransformIfNeeded();
    glm::mat3 l_mat3Wt = m_mat3World;
    l_mat3Wt[2][0] = l_v2P.x; l_mat3Wt[2][1] = l_v2P.y;
    f_SetWorldTransform(l_mat3Wt);
}

void Node::f_Visit(const std::function<void(Node*)>& l_fnFn) {
    if (!this) return;
    l_fnFn(this);
    if (m_pFirstChild) m_pFirstChild->f_Visit(l_fnFn);
    if (m_pNextSibling) m_pNextSibling->f_Visit(l_fnFn);
}

void Node::f_UpdateCachedWorldTransformIfNeeded() {
    if (m_mat3World[0][0] == g_fNODEDIRTYWORLDCACHE) {
        if (m_pParent) {
            m_pParent->f_UpdateCachedWorldTransformIfNeeded();
            m_mat3World = m_pParent->m_mat3World * m_mat3Local;
        } else {
            m_mat3World = m_mat3Local;
        }
    }
}

void Node::f_SetCachedWorldTransformDirty() {
    if (m_mat3World[0][0] != g_fNODEDIRTYWORLDCACHE) {
        m_mat3World[0][0] = g_fNODEDIRTYWORLDCACHE;
        for (Node* l_pC = m_pFirstChild; l_pC; l_pC = l_pC->m_pNextSibling)
            l_pC->f_SetCachedWorldTransformDirty();
    }
}

} // namespace toms
