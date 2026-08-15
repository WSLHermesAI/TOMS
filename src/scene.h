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
    virtual void Render(IRenderer* ren) { (void)ren; }

    // Walk the tree and render. If this node is not visible, the entire subtree
    // is skipped (parent.visible=false closes everything below it).
    void RenderTree(IRenderer* ren) {
        if (!m_visible) return;
        Render(ren);
        for (Node* c = GetFirstChild(); c; c = c->GetNextSibling())
            static_cast<GameObject*>(c)->RenderTree(ren);  // children are GameObjects
    }
};

// The common render: assign a texture (uv) + size + tint, and it draws itself
// at the node's world rect. Perfect for map sprites, item icons, slots, etc.
class SpriteNode : public GameObject {
    TOMS_OBJECT(SpriteNode)
public:
    float size[2] = {32.0f, 32.0f};
    float uv[4]   = {0.0f, 0.0f, 1.0f, 1.0f};
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool  solid   = false;   // true => flat color quad (ignore the texture)

    void Render(IRenderer* ren) override {
        glm::vec4 r = WorldRect(glm::vec2(size[0], size[1]));
        Quad q;
        q.rect[0] = r.x; q.rect[1] = r.y; q.rect[2] = r.z; q.rect[3] = r.w;
        q.uv[0] = uv[0]; q.uv[1] = uv[1]; q.uv[2] = uv[2]; q.uv[3] = uv[3];
        q.tint[0] = tint[0]; q.tint[1] = tint[1]; q.tint[2] = tint[2]; q.tint[3] = tint[3];
        q.solid = solid;
        ren->drawSprite(q);
    }
};

// Draws a string at the node's world position. The actual glyph rasterization is
// delegated to DrawFn (set once by the game, since font/atlas lives there).
class TextNode : public GameObject {
    TOMS_OBJECT(TextNode)
public:
    std::string text;
    float size = 16.0f;
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    using DrawFn = void(*)(const std::string&, float, float, float, const float*);
    static DrawFn Draw;   // set by Game (e.g. &Game::drawTextStatic)

    void Render(IRenderer* ren) override {
        (void)ren;
        if (Draw && !text.empty()) {
            glm::vec2 p = GetWorldPosition();
            Draw(text, p.x, p.y, size, tint);
        }
    }
};

// Full-screen colored quad (used for the focus splash behind modals).
class FullScreenSplash : public GameObject {
    TOMS_OBJECT(FullScreenSplash)
public:
    float w = 1024.0f, h = 768.0f;
    float tint[4] = {0.0f, 0.0f, 0.0f, 0.8f};
    void Render(IRenderer* ren) override {
        Quad q; q.rect[0]=0; q.rect[1]=0; q.rect[2]=w; q.rect[3]=h;
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;   // flat color, not texture
        q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
        ren->drawSprite(q);
    }
};

} // namespace toms

