// roamer.h — the floor "event wanderer" AI for 4-grid roamers (王座之影, 前世道兵王 -- see
// docs/ART_AND_ABILITY_DESIGN.md section 1.7 and docs/STORY_BIBLE.md's floor event pools).
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
    virtual bool walkable(int x, int y) const = 0;
};

class Roamer {
public:
    enum class Mode { Wander = 0, Chase = 1 };

    static constexpr int kDetectRadius = 6;   // tiles: inside this, start chasing
    static constexpr int kLoseRadius   = 9;   // hysteresis, so it doesn't flicker at the edge

    Roamer() = default;
    Roamer(int x, int y, Footprint fp, uint32_t seed)
        : x_(x), y_(y), fp_(fp), rng_(seed ? seed : 1u) {}

    int  x() const { return x_; }
    int  y() const { return y_; }
    Mode mode() const { return mode_; }
    const Footprint& footprint() const { return fp_; }
    int  turnsTaken() const { return turns_; }

    // Advance one turn. Returns true when the roamer actually moved. Call once per player turn
    // (the game is turn-based: one step per player step), which keeps chases deterministic and
    // therefore testable and reproducible from a save.
    bool step(const GridQuery& grid, int playerX, int playerY, int gridW, int gridH);

private:
    bool canStand(const GridQuery& g, int nx, int ny, int gw, int gh) const;
    bool tryMove(const GridQuery& g, int nx, int ny, int gw, int gh);
    bool chaseStep(const GridQuery& g, int px, int py, int gw, int gh);
    bool wanderStep(const GridQuery& g, int gw, int gh);
    int  manhattan(int ax, int ay, int bx, int by) const {
        return (ax > bx ? ax - bx : bx - ax) + (ay > by ? ay - by : by - ay);
    }
    uint32_t rnd() { rng_ = rng_ * 1664525u + 1013904223u; return rng_ >> 8; }

    int        x_ = 0, y_ = 0;
    Footprint  fp_{};
    Mode       mode_ = Mode::Wander;
    uint32_t   rng_ = 1u;
    int        lastDir_ = -1;   // 0=+x 1=-x 2=+y 3=-y; continues in a straight line while possible
    int        turns_ = 0;
};

} // namespace toms
