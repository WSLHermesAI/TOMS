// footprint.h — 佔格（footprint）for multi-grid enemies. S1 of the 2026-09-13 design set; the rules
// live in docs/design/ART_AND_ABILITY_DESIGN.md section 1.7 (F1-F8).
//
// Why this exists: every enemy used to occupy exactly one tile. The art/ability document gives the
// roster three visual tiers -- 30 types at 1 grid, 8 "large" types at 2 grids, and 12 boss/event
// entities at 4 grids -- so that a boss reads as a boss on screen. The engine has to understand
// occupancy for that to be drawable, blocking, and fightable.
//
// Geometry (F1/F2): a maze *cell* is 2x2 tiles (= 4 grids) and passages are carved 2 tiles wide, so
// only 1, 2 (2x1 or 1x2) or 4 (2x2) grids are legal -- 3 grids cannot be aligned to the cell
// subdivision, and the generator could not guarantee a passage for it. Each grid stays 32x32 px of
// art density, so a 2-grid enemy is 64x32 of *more detail*, not a scaled-up 32x32.
//
// This header is pure logic (no Stage/Game/Renderer coupling) so the rules are unit-testable --
// see footprint_test.cpp.
#pragma once

#include <cstdio>
#include <string>
#include <json.hpp>

namespace toms {

struct Footprint {
    int w = 1, h = 1;

    int  cells() const { return w * h; }
    bool valid() const {
        return (w == 1 && h == 1) || (w == 2 && h == 1) || (w == 1 && h == 2) || (w == 2 && h == 2);
    }
    bool big() const { return cells() > 1; }
    // F1/F4: 4 grids means the entity fills a whole maze cell -- the boss / special-event tier
    // (the player cannot share its cell and must circle it), which is what earns a name banner.
    bool bossTier() const { return cells() == 4; }
};

// F5: the anchor is the entity's top-left occupied tile, and the entity is drawn from there.
inline bool footprintCovers(int ex, int ey, const Footprint& f, int x, int y) {
    return x >= ex && x < ex + f.w && y >= ey && y < ey + f.h;
}

// F5: y-sort key is the bottom-most occupied row, so a tall entity draws in front of whatever
// stands above it and behind whatever stands below it.
inline int footprintSortKey(int ey, const Footprint& f) { return ey + f.h - 1; }

// One entry of a stage's optional top-level "footprints" map: keyed by the tile's raw legend char,
// the same convention the existing "encounter_overrides" map uses. Absent key = 1x1, which is what
// every shipped stage was before S1, so old files parse byte-for-byte identically.
//
// Accepted forms:
//     "Z": [2, 2]                                      shorthand
//     "Z": { "w": 2, "h": 2, "roamer": true, "name": "王座之影" }
struct FootprintSpec {
    Footprint   fp;
    int cells() const { return fp.cells(); }
    bool        roamer = false;   // roamers wander the floor, spot the player and chase (F-Roamer)
    std::string name;             // banner text; empty = fall back to the entity id
};

inline FootprintSpec footprintSpecFromJson(const nlohmann::json& j, const std::string& key) {
    FootprintSpec spec;
    bool sized = false;
    if (j.is_array() && j.size() == 2 && j[0].is_number_integer() && j[1].is_number_integer()) {
        spec.fp.w = j[0].get<int>();
        spec.fp.h = j[1].get<int>();
        sized = true;
    } else if (j.is_object()) {
        if (j.contains("w") && j["w"].is_number_integer()) spec.fp.w = j["w"].get<int>();
        if (j.contains("h") && j["h"].is_number_integer()) spec.fp.h = j["h"].get<int>();
        if (j.contains("roamer") && j["roamer"].is_boolean()) spec.roamer = j["roamer"].get<bool>();
        if (j.contains("name") && j["name"].is_string()) spec.name = j["name"].get<std::string>();
        sized = true;
    }
    if (!sized) return spec;   // absent / unrecognised -> 1x1, exactly the pre-S1 behavior
    if (!spec.fp.valid()) {
        fprintf(stderr,
                "[footprint] '%s' asks for %dx%d, which is not a legal 佔格 (only 1x1, 2x1, 1x2 and "
                "2x2 are allowed -- docs/design/ART_AND_ABILITY_DESIGN.md section 1.7); using 1x1\n",
                key.c_str(), spec.fp.w, spec.fp.h);
        spec.fp = Footprint{};
    }
    return spec;
}

// ---- resolution: which footprint does an entity actually get? --------------------------------
// Three sources, in priority order (docs/design/ART_AND_ABILITY_DESIGN.md F8: 佔格 is a *character-level*
// property, so it normally comes from the type table, but a floor may lay a specific monster out
// differently, and a floor that says nothing must not accidentally shrink a boss):
//   1. the stage file's own "footprints" entry for that tile char  (explicit per-floor decision)
//   2. data/footprints.json, keyed by the legend kind ("monster:golem")  (the character's tier)
//   3. 1x1                                                          (everything that shipped pre-S1)
// Living here rather than inside Game::loadStage keeps it unit-testable and gives the data
// validator in footprint_test.cpp exactly the same answer the game will use at runtime.
inline FootprintSpec resolveFootprint(const FootprintSpec& stageSpec, bool stageExplicit,
                                      const FootprintSpec& typeSpec) {
    FootprintSpec out;
    if (stageExplicit) {
        out = stageSpec;
    } else if (typeSpec.fp.valid() && (typeSpec.cells() != 1 || typeSpec.fp.w != 1)) {
        out = typeSpec;
    } else {
        out = typeSpec.fp.valid() ? typeSpec : FootprintSpec{};
    }
    // A roamer needs somewhere to roam: a 1x1 "roamer" flag is dropped (see roamer.h), and a name
    // without a boss-tier footprint would put a banner over a normal slime.
    if (!out.fp.big()) out.roamer = false;
    if (!out.fp.bossTier() && out.name.empty()) { /* nothing to do: keeps ids as the label */ }
    return out;
}

} // namespace toms
