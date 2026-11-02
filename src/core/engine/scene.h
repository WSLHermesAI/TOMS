// scene.h — render binding for the Node scene-graph.
//
// Binds rendering to the scene-graph: every GameObject can draw itself via
// Render(IRenderer*), and RenderTree() walks the tree applying the visibility
// rule: if a node is not visible, it AND its whole subtree are skipped.
//
//   - GameObject : Node         -> adds virtual Render() + RenderTree()
//   - SpriteNode : GameObject    -> the COMMON case: assign a texture (uv),
//                                     size and tint; it draws itself at its
//                                     world rect. No custom code required.
//   - TextNode   : GameObject    -> draws a string at its world position.
//                                     Text drawing is delegated to a DrawFn set
//                                     by the game (font/atlas specifics live there).
//   - special objects subclass GameObject and override Render() with their own
//     drawing (still positioned by the node's world transform).
//
// This is header-only (no .cpp) so it can be included anywhere without CMake
// source changes. It depends only on node.h + render_iface.h.
#pragma once
#include "node.h"
#include "render_iface.h"
#include <string>

namespace toms {

class GameObject : public Node {
    TOMS_OBJECT(GameObject)
public:
    using Node::Node;   // inherit Node(const char* name) etc.
    virtual ~GameObject() {}

    // Override to draw this object (using its world transform). Default: nothing.
    virtual void f_Render(IRenderer* l_pRen) { (void)l_pRen; }

    // Walk the tree and render. If this node is not visible, the entire subtree
    // is skipped (parent.visible=false closes everything below it).
    void f_RenderTree(IRenderer* l_pRen) {
        if (!m_bVisible) return;
        f_Render(l_pRen);
        for (Node* l_pC = f_GetFirstChild(); l_pC; l_pC = l_pC->f_GetNextSibling())
            static_cast<GameObject*>(l_pC)->f_RenderTree(l_pRen);  // children are GameObjects
    }
};

// The common render: assign a texture (uv) + size + tint, and it draws itself
// at the node's world rect. Perfect for map sprites, item icons, slots, etc.
class SpriteNode : public GameObject {
    TOMS_OBJECT(SpriteNode)
public:
    float m_arrSize[2] = {32.0f, 32.0f};
    float m_arrUv[4]   = {0.0f, 0.0f, 1.0f, 1.0f};
    float m_arrTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool  m_bSolid   = false;   // true => flat color quad (ignore the texture)

    void f_Render(IRenderer* l_pRen) override {
        glm::vec4 l_v4R = f_WorldRect(glm::vec2(m_arrSize[0], m_arrSize[1]));
        Quad l_oQ;
        l_oQ.m_arrRect[0] = l_v4R.x; l_oQ.m_arrRect[1] = l_v4R.y; l_oQ.m_arrRect[2] = l_v4R.z; l_oQ.m_arrRect[3] = l_v4R.w;
        l_oQ.m_arrUv[0] = m_arrUv[0]; l_oQ.m_arrUv[1] = m_arrUv[1]; l_oQ.m_arrUv[2] = m_arrUv[2]; l_oQ.m_arrUv[3] = m_arrUv[3];
        l_oQ.m_arrTint[0] = m_arrTint[0]; l_oQ.m_arrTint[1] = m_arrTint[1]; l_oQ.m_arrTint[2] = m_arrTint[2]; l_oQ.m_arrTint[3] = m_arrTint[3];
        l_oQ.m_bSolid = m_bSolid;
        l_pRen->f_DrawSprite(l_oQ);
    }
};

// Draws a string at the node's world position. The actual glyph rasterization is
// delegated to DrawFn (set once by the game, since font/atlas lives there).
class TextNode : public GameObject {
    TOMS_OBJECT(TextNode)
public:
    std::string m_strText;
    float m_fSize = 16.0f;
    float m_arrTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    using DrawFn = void(*)(const std::string&, float, float, float, const float*);
    static DrawFn m_fnDraw;   // set by Game (e.g. &Game::drawTextStatic)

    void f_Render(IRenderer* l_pRen) override {
        (void)l_pRen;
        if (m_fnDraw && !m_strText.empty()) {
            glm::vec2 l_v2P = f_GetWorldPosition();
            m_fnDraw(m_strText, l_v2P.x, l_v2P.y, m_fSize, m_arrTint);
        }
    }
};

// Full-screen colored quad (used for the focus splash behind modals).
class FullScreenSplash : public GameObject {
    TOMS_OBJECT(FullScreenSplash)
public:
    float m_fW = 1024.0f, m_fH = 768.0f;
    float m_arrTint[4] = {0.0f, 0.0f, 0.0f, 0.8f};
    void f_Render(IRenderer* l_pRen) override {
        Quad l_oQ; l_oQ.m_arrRect[0]=0; l_oQ.m_arrRect[1]=0; l_oQ.m_arrRect[2]=m_fW; l_oQ.m_arrRect[3]=m_fH;
        l_oQ.m_arrUv[0]=0; l_oQ.m_arrUv[1]=0; l_oQ.m_arrUv[2]=1; l_oQ.m_arrUv[3]=1; l_oQ.m_bSolid=true;   // flat color, not texture
        l_oQ.m_arrTint[0]=m_arrTint[0]; l_oQ.m_arrTint[1]=m_arrTint[1]; l_oQ.m_arrTint[2]=m_arrTint[2]; l_oQ.m_arrTint[3]=m_arrTint[3];
        l_pRen->f_DrawSprite(l_oQ);
    }
};

} // namespace toms

