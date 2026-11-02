// roamer.h — the floor "event wanderer" AI for 4-grid roamers (王座之影, 前世道兵王 -- see
// docs/design/ART_AND_ABILITY_DESIGN.md section 1.7 and docs/story/STORY_BIBLE.md's floor event pools).
//
// S1 deliverable. A roamer is a normal entity with `"roamer": true` in its footprint entry: it
// wanders the floor by itself, and once the player comes inside its detection radius it switches to
// chasing -- straight-line greedy, not pathfinding, which is what makes it feel like a predator
// and keeps it cheap.
//
// Two rules are hard requirements rather than tuning knobs:
//   * it never phases through walls (F3/F4) -- a step is only legal when the entity's WHOLE
//     footprint lands on walkable tiles inside the grid;
//   * it never leaves the grid (a 4-grid roamer anchored at the last row would otherwise half
//     stand outside the maze).
//
// The class is pure logic: walkability arrives through the GridQuery interface, so it can be
// unit-tested on a hand-written maze with no Stage, renderer or game loop (see footprint_test.cpp).
#pragma once

#include <cstdint>

#include "footprint.h"

namespace toms {

// What the controller may ask about the world. Deliberately minimal: "can an entity stand here".
class GridQuery {
public:
    virtual ~GridQuery() = default;
    // True only if (x,y) is inside the grid and is a walkable tile (the caller also excludes tiles
    // occupied by OTHER entities -- a roamer must not walk into the player or another monster).
    virtual bool f_Walkable(int l_iX, int l_iY) const = 0;
};

class Roamer {
public:
    enum class Mode { Wander = 0, Chase = 1 };

    static constexpr int m_iDetectRadius = 6;   // tiles: inside this, start chasing
    static constexpr int m_iLoseRadius   = 9;   // hysteresis, so it doesn't flicker at the edge

    Roamer() = default;
    Roamer(int l_iX, int l_iY, Footprint l_oFp, uint32_t l_uSeed)
        : m_iX(l_iX), m_iY(l_iY), m_oFp(l_oFp), m_uRng(l_uSeed ? l_uSeed : 1u) {}

    int  f_X() const { return m_iX; }
    int  f_Y() const { return m_iY; }
    Mode f_Mode() const { return m_eMode; }
    const Footprint& f_Footprint() const { return m_oFp; }
    int  f_TurnsTaken() const { return m_iTurns; }

    // Advance one turn. Returns true when the roamer actually moved. Call once per player turn
    // (the game is turn-based: one step per player step), which keeps chases deterministic and
    // therefore testable and reproducible from a save.
    bool f_Step(const GridQuery& l_oGrid, int l_iPlayerX, int l_iPlayerY, int l_iGridW, int l_iGridH);

private:
    bool f_CanStand(const GridQuery& l_oG, int l_iNx, int l_iNy, int l_iGw, int l_iGh) const;
    bool f_TryMove(const GridQuery& l_oG, int l_iNx, int l_iNy, int l_iGw, int l_iGh);
    bool f_ChaseStep(const GridQuery& l_oG, int l_iPx, int l_iPy, int l_iGw, int l_iGh);
    bool f_WanderStep(const GridQuery& l_oG, int l_iGw, int l_iGh);
    int  f_Manhattan(int l_iAx, int l_iAy, int l_iBx, int l_iBy) const {
        return (l_iAx > l_iBx ? l_iAx - l_iBx : l_iBx - l_iAx) + (l_iAy > l_iBy ? l_iAy - l_iBy : l_iBy - l_iAy);
    }
    uint32_t f_Rnd() { m_uRng = m_uRng * 1664525u + 1013904223u; return m_uRng >> 8; }

    int        m_iX = 0, m_iY = 0;
    Footprint  m_oFp{};
    Mode       m_eMode = Mode::Wander;
    uint32_t   m_uRng = 1u;
    int        m_iLastDir = -1;   // 0=+x 1=-x 2=+y 3=-y; continues in a straight line while possible
    int        m_iTurns = 0;
};

} // namespace toms
