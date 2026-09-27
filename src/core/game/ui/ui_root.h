#pragma once
// UiRoot -- the single scaled root the game's UI hangs off.
//
// Why this exists (owner's rule, stated twice): to make the UI bigger on a phone you scale the UI
// OBJECTS and keep the game resolution. Lowering the design resolution also makes things bigger, but
// it softens the whole render and shrinks how much of the maze a player sees, so it is the wrong tool.
// This class is the "grow the objects" half: one scale, one pivot, and ONE mapping that both the
// drawing and the hit-testing go through -- so a control that looks bigger stays exactly as tappable.
// (Any change here must keep that invariant: screen->local and local->screen are inverses of each
// other, and every hit-test asks the same function the draw call asked.)
//
// Scaling happens about a pivot (the design centre by default) so a uniform scale never shifts the
// composition off-screen in one direction -- the failure mode when elements are individually scaled
// with hardcoded offsets. Aspect is preserved by construction: uniform scale only.

#include <algorithm>

#include <glm/glm.hpp>

namespace toms {

class UiRoot {
public:
    // 1.0 = today's desktop framing (this class is then the identity mapping). A phone build sets a
    // larger value once the screens below are all reading their geometry from here.
    void SetScale(float s) { if (s > 0.0f) scale_ = s; }   // ignore nonsense rather than invert the UI
    float Scale() const { return scale_; }

    // Where scaling happens about. Defaults to the 1024x768 design centre.
    void SetPivot(const glm::vec2& p) { pivot_ = p; }
    glm::vec2 Pivot() const { return pivot_; }

    // The design is 1024x768; that is what every authored rect in the code is written against.
    static glm::vec2 DesignSize() { return glm::vec2(1024.0f, 768.0f); }

    // Authored (design-space, local) rect {x,y,w,h} -> the rect to draw and to hit-test.
    glm::vec4 ScreenRect(const glm::vec4& local) const {
        return glm::vec4(pivot_.x + (local.x - pivot_.x) * scale_,
                         pivot_.y + (local.y - pivot_.y) * scale_,
                         local.z * scale_,
                         local.w * scale_);
    }

    // Inverse of the above, for one point: what the pointer position means in design space.
    glm::vec2 ScreenToLocal(const glm::vec2& screen) const {
        if (scale_ == 0.0f) return screen;          // never divide by zero in a hit-test
        return glm::vec2(pivot_.x + (screen.x - pivot_.x) / scale_,
                         pivot_.y + (screen.y - pivot_.y) / scale_);
    }

    // The hit-test. Deliberately defined as "does the pointer fall inside the rect the drawing used",
    // so drawing and hit-testing cannot drift apart.
    bool Hit(const glm::vec4& localRect, const glm::vec2& screenPoint) const {
        const glm::vec2 p = ScreenToLocal(screenPoint);
        return p.x >= localRect.x && p.x <= localRect.x + localRect.z &&
               p.y >= localRect.y && p.y <= localRect.y + localRect.w;
    }

    // A local rect expanded so it is at least `minScreen` px in every axis on screen. For touch
    // targets: a small authored rect (a 24px row) still gets a finger-sized target without redrawing
    // it larger. Uses the same mapping, so it lands exactly on the drawn rect.
    glm::vec4 TouchRect(const glm::vec4& localRect, float minScreen) const {
        if (scale_ <= 0.0f) return localRect;
        const float minLocal = minScreen / scale_;       // minScreen is measured ON SCREEN
        const float w = std::max(localRect.z, minLocal);
        const float h = std::max(localRect.w, minLocal);
        const glm::vec2 c(localRect.x + localRect.z * 0.5f, localRect.y + localRect.w * 0.5f);
        return glm::vec4(c.x - w * 0.5f, c.y - h * 0.5f, w, h);
    }

private:
    float scale_ = 1.0f;
    glm::vec2 pivot_ = glm::vec2(512.0f, 384.0f);
};

}  // namespace toms
