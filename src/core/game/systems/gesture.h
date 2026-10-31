// gesture.h -- drawn shapes (docs/gameplay/16_SKILLS_AND_PLAYS.md §2, block B2): the shapes a play can ask for, by name,
// and two ways to read them:
//   ShapeTrace   a shape shown on the screen at a place and size: the finger reaches its corners in order (the
//                staff's pentagram: each corner reached is a step, "1/5" ... "5/5").
//   matchStroke  a free stroke drawn anywhere at any size: does it go through the shape's corners in order? (the
//                sword's triangle and circle prompts)
// Shapes are data: corners from 0 to 1 (x right, y down), in drawing order; a closed shape ends where it began.
//
// Pure: positions in design pixels; no Game dependency.
#pragma once
#include <glm/glm.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace toms {

// The named shapes: "pentagram" (a star in one stroke), "triangle", "square", "circle" (8 points), "line" (left to
// right). Unknown: empty. The corners are in [0, 1] x [0, 1], in drawing order.
std::vector<glm::vec2> shapeCorners(const std::string& name);
bool shapeClosed(const std::string& name);   // it ends where it began (its last corner is its first again)

// The point a fraction `t` (0..1) along the shape's outline, the shape placed in a square of `size` centred at `centre`.
glm::vec2 shapePoint(const std::string& name, glm::vec2 centre, float size, float t);

// A shape on the screen, traced corner by corner.
class ShapeTrace {
public:
    void start(const std::string& shape, glm::vec2 centre, float size, float cornerRadius);
    // The pointer (0 down, 1 held, 2 up). Returns how many steps it made with this move (0 or more). The first corner
    // only starts the trace; each next corner reached in order is a step (a pentagram: 5 steps, back at its top).
    int pointer(int phase, float x, float y);
    int reached() const { return reached_; }                      // corners reached, the first one too
    int steps() const { return std::max(0, reached_ - 1); }
    int stepCount() const { return std::max(0, (int)pts_.size() - 1); }
    bool done() const { return !pts_.empty() && reached_ >= (int)pts_.size(); }
    const std::vector<glm::vec2>& points() const { return pts_; }   // on the screen
    glm::vec2 last() const { return last_; }
    bool drawing() const { return down_; }
private:
    std::vector<glm::vec2> pts_;
    float radius_ = 40.0f;
    int reached_ = 0;
    bool down_ = false;
    glm::vec2 last_{0.0f};
};

// A free stroke (the finger's points, in order) against a shape: true when, scaled to its own box, it passes near
// each corner in order (a closed shape: from any corner, either way round). tolerance: as a share of the box.
bool matchStroke(const std::vector<glm::vec2>& stroke, const std::string& shape, float tolerance = 0.2f);

} // namespace toms
