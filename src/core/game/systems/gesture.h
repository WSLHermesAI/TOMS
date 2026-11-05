// gesture.h -- drawn shapes (docs/gameplay/16_SKILLS_AND_PLAYS.md §2, block B2): the shapes a play can ask for, by name,
// and three ways to read them:
//   ShapeTrace   a shape shown on the screen at a place and size: the finger reaches its corners in order (the
//                staff's pentagram: each corner reached is a step, "1/5" ... "5/5").
//   matchStroke  a free stroke drawn anywhere at any size: does it go through the shape's corners in order? (the
//                moves' triangle and circle prompts)
//   matchGesture a swipe tier (docs/gameplay/17 §4.2): a V, an M or a square in one stroke, in any direction.
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
// right), "v" (a tick, like the Nike mark: a short arm, then a long one), "m". Unknown: empty. The corners are in [0, 1] x [0, 1], in drawing order.
std::vector<glm::vec2> f_ShapeCorners(const std::string& l_strName);
bool f_ShapeClosed(const std::string& l_strName);   // it ends where it began (its last corner is its first again)

// The point a fraction `t` (0..1) along the shape's outline, the shape placed in a square of `size` centred at `centre`.
glm::vec2 f_ShapePoint(const std::string& l_strName, glm::vec2 l_v2Centre, float l_fSize, float l_fT);

// A shape on the screen, traced corner by corner.
class ShapeTrace {
public:
    void f_Start(const std::string& l_strShape, glm::vec2 l_v2Centre, float l_fSize, float l_fCornerRadius);
    // The pointer (0 down, 1 held, 2 up). Returns how many steps it made with this move (0 or more). The first corner
    // only starts the trace; each next corner reached in order is a step (a pentagram: 5 steps, back at its top).
    int f_Pointer(int l_iPhase, float l_fX, float l_fY);
    int f_Reached() const { return m_iReached; }                      // corners reached, the first one too
    int f_Steps() const { return std::max(0, m_iReached - 1); }
    int f_StepCount() const { return std::max(0, (int)m_vPts.size() - 1); }
    bool f_Done() const { return !m_vPts.empty() && m_iReached >= (int)m_vPts.size(); }
    const std::vector<glm::vec2>& f_Points() const { return m_vPts; }   // on the screen
    glm::vec2 f_Last() const { return m_v2Last; }
    bool f_Drawing() const { return m_bDown; }
private:
    std::vector<glm::vec2> m_vPts;
    float m_fRadius = 40.0f;
    int m_iReached = 0;
    bool m_bDown = false;
    glm::vec2 m_v2Last{0.0f};
};

// A free stroke (the finger's points, in order) against a shape: true when, scaled to its own box, it passes near
// each corner in order (a closed shape: from any corner, either way round). tolerance: as a share of the box.
bool f_MatchStroke(const std::vector<glm::vec2>& l_vStroke, const std::string& l_strShape, float l_fTolerance = 0.2f);

// The swipe tiers (docs/gameplay/17 §4.2): 1 a way, 2 "v", 3 "m", 4 "square"; 0: not a gesture.
int f_GestureTier(const std::string& l_strCue);
// A stroke against a gesture, by its straight parts and the turns between them, so any place, size and direction
// counts: "v" two parts and one sharp turn; "m" four parts with the turns going back and forth; "square" four parts,
// three right-angle turns the same way round, ending near its start (from any corner, either way).
// l_fExtraDeg widens the angles (DEX; negative: stricter, for a perfect).
bool f_MatchGesture(const std::vector<glm::vec2>& l_vStroke, const std::string& l_strCue, float l_fExtraDeg = 0.0f);

} // namespace toms
