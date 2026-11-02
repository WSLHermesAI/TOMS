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
    int m_iW = 1, m_iH = 1;

    int  f_Cells() const { return m_iW * m_iH; }
    bool f_Valid() const {
        return (m_iW == 1 && m_iH == 1) || (m_iW == 2 && m_iH == 1) || (m_iW == 1 && m_iH == 2) || (m_iW == 2 && m_iH == 2);
    }
    bool f_Big() const { return f_Cells() > 1; }
    // F1/F4: 4 grids means the entity fills a whole maze cell -- the boss / special-event tier
    // (the player cannot share its cell and must circle it), which is what earns a name banner.
    bool f_BossTier() const { return f_Cells() == 4; }
};

// F5: the anchor is the entity's top-left occupied tile, and the entity is drawn from there.
inline bool f_FootprintCovers(int l_iEx, int l_iEy, const Footprint& l_oF, int l_iX, int l_iY) {
    return l_iX >= l_iEx && l_iX < l_iEx + l_oF.m_iW && l_iY >= l_iEy && l_iY < l_iEy + l_oF.m_iH;
}

// F5: y-sort key is the bottom-most occupied row, so a tall entity draws in front of whatever
// stands above it and behind whatever stands below it.
inline int f_FootprintSortKey(int l_iEy, const Footprint& l_oF) { return l_iEy + l_oF.m_iH - 1; }

// One entry of a stage's optional top-level "footprints" map: keyed by the tile's raw legend char,
// the same convention the existing "encounter_overrides" map uses. Absent key = 1x1, which is what
// every shipped stage was before S1, so old files parse byte-for-byte identically.
//
// Accepted forms:
//     "Z": [2, 2]                                      shorthand
//     "Z": { "w": 2, "h": 2, "roamer": true, "name": "王座之影" }
struct FootprintSpec {
    Footprint   m_oFp;
    int f_Cells() const { return m_oFp.f_Cells(); }
    bool        m_bRoamer = false;   // roamers wander the floor, spot the player and chase (F-Roamer)
    std::string m_strName;             // banner text; empty = fall back to the entity id
};

inline FootprintSpec f_FootprintSpecFromJson(const nlohmann::json& l_jJ, const std::string& l_strKey) {
    FootprintSpec l_oSpec;
    bool l_bSized = false;
    if (l_jJ.is_array() && l_jJ.size() == 2 && l_jJ[0].is_number_integer() && l_jJ[1].is_number_integer()) {
        l_oSpec.m_oFp.m_iW = l_jJ[0].get<int>();
        l_oSpec.m_oFp.m_iH = l_jJ[1].get<int>();
        l_bSized = true;
    } else if (l_jJ.is_object()) {
        if (l_jJ.contains("w") && l_jJ["w"].is_number_integer()) l_oSpec.m_oFp.m_iW = l_jJ["w"].get<int>();
        if (l_jJ.contains("h") && l_jJ["h"].is_number_integer()) l_oSpec.m_oFp.m_iH = l_jJ["h"].get<int>();
        if (l_jJ.contains("roamer") && l_jJ["roamer"].is_boolean()) l_oSpec.m_bRoamer = l_jJ["roamer"].get<bool>();
        if (l_jJ.contains("name") && l_jJ["name"].is_string()) l_oSpec.m_strName = l_jJ["name"].get<std::string>();
        l_bSized = true;
    }
    if (!l_bSized) return l_oSpec;   // absent / unrecognised -> 1x1, exactly the pre-S1 behavior
    if (!l_oSpec.m_oFp.f_Valid()) {
        fprintf(stderr,
                "[footprint] '%s' asks for %dx%d, which is not a legal 佔格 (only 1x1, 2x1, 1x2 and "
                "2x2 are allowed -- docs/design/ART_AND_ABILITY_DESIGN.md section 1.7); using 1x1\n",
                l_strKey.c_str(), l_oSpec.m_oFp.m_iW, l_oSpec.m_oFp.m_iH);
        l_oSpec.m_oFp = Footprint{};
    }
    return l_oSpec;
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
inline FootprintSpec f_ResolveFootprint(const FootprintSpec& l_oStageSpec, bool l_bStageExplicit,
                                      const FootprintSpec& l_oTypeSpec) {
    FootprintSpec l_oOut;
    if (l_bStageExplicit) {
        l_oOut = l_oStageSpec;
    } else if (l_oTypeSpec.m_oFp.f_Valid() && (l_oTypeSpec.f_Cells() != 1 || l_oTypeSpec.m_oFp.m_iW != 1)) {
        l_oOut = l_oTypeSpec;
    } else {
        l_oOut = l_oTypeSpec.m_oFp.f_Valid() ? l_oTypeSpec : FootprintSpec{};
    }
    // A roamer needs somewhere to roam: a 1x1 "roamer" flag is dropped (see roamer.h), and a name
    // without a boss-tier footprint would put a banner over a normal slime.
    if (!l_oOut.m_oFp.f_Big()) l_oOut.m_bRoamer = false;
    if (!l_oOut.m_oFp.f_BossTier() && l_oOut.m_strName.empty()) { /* nothing to do: keeps ids as the label */ }
    return l_oOut;
}

} // namespace toms
