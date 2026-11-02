// footprint_test.cpp — headless verification for S1 (佔格 footprints + roamers), plus a data
// validator over the shipped stage files. Run: ./footprint_test
//
// Two halves:
//   1. pure logic — the footprint rules from docs/design/ART_AND_ABILITY_DESIGN.md section 1.7 (F1-F8) and
//      the roamer controller (src/game/roamer.h), which never phases through walls;
//   2. data validation — every shipped stage is re-read through the SAME resolution path the game
//      uses (parseStage + the type table + toms::resolveFootprint) and checked so a future floor
//      edit that declares an illegal size, stands an entity on a wall, overlaps two entities, or
//      walls the stairs off cannot ship silently.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "footprint.h"
#include "roamer.h"
#include "stage.h"

static nlohmann::json f_ReadJsonLocal(const std::string& l_strPath) {
    FILE* l_pFp = fopen(l_strPath.c_str(), "rb");
    if (!l_pFp) { fprintf(stderr, "  (cannot open %s)\n", l_strPath.c_str()); return {}; }
    std::string l_strBuf;
    char l_pszChunk[4096];
    size_t l_nN;
    while ((l_nN = fread(l_pszChunk, 1, sizeof(l_pszChunk), l_pFp)) > 0) l_strBuf.append(l_pszChunk, l_nN);
    fclose(l_pFp);
    try { return nlohmann::json::parse(l_strBuf); } catch (...) { return {}; }
}

static int g_iChecks = 0, g_iFails = 0;

static toms::Footprint f_Fp(int l_iW, int l_iH) { toms::Footprint l_oF; l_oF.m_iW = l_iW; l_oF.m_iH = l_iH; return l_oF; }

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_iChecks;                                                               \
        if (!(cond)) {                                                            \
            ++g_iFails;                                                            \
            fprintf(stderr, "  FAIL: %s\n", std::string(__VA_ARGS__).c_str());    \
        }                                                                         \
    } while (0)

// ---------------------------------------------------------------- helpers
// Hand-written maze for the roamer tests: '#' wall, '.' floor.
struct Maze : public toms::GridQuery {
    std::vector<std::string> m_vTiles;
    bool f_Walkable(int l_iX, int l_iY) const override {
        if (l_iY < 0 || l_iY >= (int)m_vTiles.size()) return false;
        if (l_iX < 0 || l_iX >= (int)m_vTiles[l_iY].size()) return false;
        return m_vTiles[l_iY][l_iX] != '#';
    }
    // Verify a whole footprint stands on open floor (used to catch wall-phasing).
    bool f_Stands(int l_iX, int l_iY, const toms::Footprint& l_oFp) const {
        for (int l_iDy = 0; l_iDy < l_oFp.m_iH; ++l_iDy)
            for (int l_iDx = 0; l_iDx < l_oFp.m_iW; ++l_iDx)
                if (!f_Walkable(l_iX + l_iDx, l_iY + l_iDy)) return false;
        return true;
    }
};

static void f_TestFootprintMath() {
    CHECK(f_Fp(1, 1).f_Valid() && f_Fp(2, 1).f_Valid() && f_Fp(1, 2).f_Valid()
          && f_Fp(2, 2).f_Valid(), "1x1, 2x1, 1x2 and 2x2 are the legal 佔格");
    CHECK(!f_Fp(3, 1).f_Valid() && !f_Fp(1, 3).f_Valid() && !f_Fp(3, 3).f_Valid()
          && !f_Fp(2, 3).f_Valid() && !f_Fp(4, 4).f_Valid(),
          "3x1 / 1x3 / 2x3 / 3x3 / 4x4 are rejected (F1: cannot align to the 2x2 cell)");
    CHECK(f_Fp(1, 1).f_Cells() == 1 && f_Fp(2, 1).f_Cells() == 2 && f_Fp(2, 2).f_Cells() == 4,
          "cells() counts grids");
    CHECK(!f_Fp(1, 1).f_Big() && f_Fp(2, 1).f_Big() && f_Fp(2, 2).f_Big(), "big() = 2 or 4 grids");
    CHECK(!f_Fp(2, 1).f_BossTier() && f_Fp(2, 2).f_BossTier(),
          "bossTier() is exactly the 4-grid tier (F4)");

    // coverage: the anchor is the top-left occupied tile (F5)
    const toms::Footprint l_oBoss = f_Fp(2, 2);
    CHECK(toms::f_FootprintCovers(4, 7, l_oBoss, 4, 7) && toms::f_FootprintCovers(4, 7, l_oBoss, 5, 7)
          && toms::f_FootprintCovers(4, 7, l_oBoss, 4, 8) && toms::f_FootprintCovers(4, 7, l_oBoss, 5, 8),
          "a 2x2 covers exactly its four grids");
    CHECK(!toms::f_FootprintCovers(4, 7, l_oBoss, 6, 7) && !toms::f_FootprintCovers(4, 7, l_oBoss, 4, 9)
          && !toms::f_FootprintCovers(4, 7, l_oBoss, 3, 7) && !toms::f_FootprintCovers(4, 7, l_oBoss, 4, 6),
          "a 2x2 covers nothing outside its four grids");

    CHECK(toms::f_FootprintSortKey(5, f_Fp(1, 1)) == 5
          && toms::f_FootprintSortKey(5, f_Fp(2, 1)) == 5
          && toms::f_FootprintSortKey(5, f_Fp(1, 2)) == 6
          && toms::f_FootprintSortKey(5, f_Fp(2, 2)) == 6,
          "y-sort key is the bottom-most occupied row (F5)");
}

static void f_TestFootprintJson() {
    using namespace toms;
    // absent entry -> exactly the pre-S1 behavior
    CHECK(f_FootprintSpecFromJson(nlohmann::json{{"w", 2}, {"h", 1}}, "3").m_oFp.m_iW == 2,
          "array/object forms parse");
    CHECK(f_FootprintSpecFromJson(nlohmann::json(), "3").m_oFp.m_iW == 1
          && f_FootprintSpecFromJson(nlohmann::json(), "3").f_Cells() == 1,
          "a missing stage entry means 1x1");
    FootprintSpec l_oArr = f_FootprintSpecFromJson(nlohmann::json::array({2, 2}), "Z");
    CHECK(l_oArr.m_oFp.m_iW == 2 && l_oArr.m_oFp.m_iH == 2 && !l_oArr.m_bRoamer, "[2,2] shorthand parses");
    FootprintSpec l_oObj = f_FootprintSpecFromJson(
        nlohmann::json{{"w", 2}, {"h", 2}, {"roamer", true}, {"name", "王座之影"}}, "Z");
    CHECK(l_oObj.m_oFp.m_iW == 2 && l_oObj.m_oFp.m_iH == 2 && l_oObj.m_bRoamer && l_oObj.m_strName == "王座之影",
          "object form parses w/h/roamer/name");
    FootprintSpec l_oBad = f_FootprintSpecFromJson(nlohmann::json::array({3, 1}), "Q");
    CHECK(l_oBad.m_oFp.m_iW == 1 && l_oBad.m_oFp.m_iH == 1, "an illegal size falls back to 1x1 instead of shipping");

    // resolution priority: stage file > character type table > 1x1 (F8)
    FootprintSpec l_oType2x1 = f_FootprintSpecFromJson(nlohmann::json{{"w", 2}, {"h", 1}}, "3");
    FootprintSpec l_oStage4  = f_FootprintSpecFromJson(
        nlohmann::json{{"w", 2}, {"h", 2}, {"name", "floor boss"}}, "Z");
    CHECK(f_ResolveFootprint(l_oStage4, true, l_oType2x1).m_oFp.m_iH == 2, "an explicit stage entry wins");
    CHECK(f_ResolveFootprint(l_oStage4, false, l_oType2x1).m_oFp.m_iH == 1, "the type table applies when the stage is silent");
    CHECK(f_ResolveFootprint(FootprintSpec{}, false, l_oType2x1).m_oFp.m_iW == 2, "type table alone still applies");
    CHECK(f_ResolveFootprint(FootprintSpec{}, false, FootprintSpec{}).m_oFp.f_Cells() == 1,
          "no stage entry and no type entry = 1x1");
    FootprintSpec l_oOneByOneRoamer;
    l_oOneByOneRoamer.m_oFp = f_Fp(1, 1);
    l_oOneByOneRoamer.m_bRoamer = true;
    l_oOneByOneRoamer.m_strName = "x";
    CHECK(!f_ResolveFootprint(FootprintSpec{}, false, l_oOneByOneRoamer).m_bRoamer,
          "a 1x1 'roamer' is dropped: it has nowhere to roam");
}

static void f_TestRoamer() {
    using toms::Roamer;

    // 20x12 open room with a wall ring and two interior pillars
    Maze l_oM;
    l_oM.m_vTiles.assign(12, std::string(20, '.'));
    for (int l_iX = 0; l_iX < 20; ++l_iX) { l_oM.m_vTiles[0][l_iX] = '#'; l_oM.m_vTiles[11][l_iX] = '#'; }
    for (int l_iY = 0; l_iY < 12; ++l_iY) { l_oM.m_vTiles[l_iY][0] = '#'; l_oM.m_vTiles[l_iY][19] = '#'; }
    l_oM.m_vTiles[5][9] = '#'; l_oM.m_vTiles[5][10] = '#'; l_oM.m_vTiles[6][9] = '#'; l_oM.m_vTiles[6][10] = '#';

    // never phases through walls, never leaves the grid (F3/F4)
    {
        Roamer l_oR(2, 2, f_Fp(2, 2), 12345u);
        bool l_bOk = true;
        for (int l_iI = 0; l_iI < 400; ++l_iI) {
            l_oR.f_Step(l_oM, 15, 9, 20, 12);
            if (!l_oM.f_Stands(l_oR.f_X(), l_oR.f_Y(), l_oR.f_Footprint())) { l_bOk = false; break; }
            if (l_oR.f_X() < 0 || l_oR.f_Y() < 0 || l_oR.f_X() + 2 > 20 || l_oR.f_Y() + 2 > 12) { l_bOk = false; break; }
        }
        CHECK(l_bOk, "400 turns as a 2x2 roamer: always inside the grid, never on a wall tile");
    }

    // forced into a corner: still cannot escape the maze bounds
    {
        Roamer l_oR(17, 9, f_Fp(2, 2), 777u);
        bool l_bOk = true;
        for (int l_iI = 0; l_iI < 200; ++l_iI) {
            l_oR.f_Step(l_oM, 1, 1, 20, 12);
            if (!l_oM.f_Stands(l_oR.f_X(), l_oR.f_Y(), l_oR.f_Footprint())) { l_bOk = false; break; }
        }
        CHECK(l_bOk, "a roamer pushed toward the border is clamped by its footprint");
    }

    // detection + chase: inside kDetectRadius it closes the distance
    {
        Roamer l_oR(3, 3, f_Fp(1, 1), 99u);
        const int l_iPx = 5, l_iPy = 4;   // manhattan distance 3 from (3,3): inside the radius
        CHECK(l_oR.f_Mode() == Roamer::Mode::Wander, "starts in Wander");
        CHECK(std::abs(l_oR.f_X() - l_iPx) + std::abs(l_oR.f_Y() - l_iPy) <= Roamer::m_iDetectRadius,
              "test setup: player is inside the detect radius");
        const int l_iBefore = std::abs(l_oR.f_X() - l_iPx) + std::abs(l_oR.f_Y() - l_iPy);
        l_oR.f_Step(l_oM, l_iPx, l_iPy, 20, 12);
        CHECK(l_oR.f_Mode() == Roamer::Mode::Chase, "inside the detect radius it switches to Chase");
        CHECK(std::abs(l_oR.f_X() - l_iPx) + std::abs(l_oR.f_Y() - l_iPy) < l_iBefore,
              "a chasing roamer closes the distance to the player");
    }

    // hysteresis: it gives up only past kLoseRadius, not the moment it leaves the detect radius.
    // The roamer sits in a walled pocket so it cannot move -- this isolates the mode machine from
    // pathing, which the cases above already cover.
    {
        Maze l_oPocket;
        l_oPocket.m_vTiles.assign(9, std::string(9, '#'));
        l_oPocket.m_vTiles[4][4] = '.';
        Roamer l_oR(4, 4, f_Fp(1, 1), 5u);
        l_oR.f_Step(l_oPocket, 4 + Roamer::m_iDetectRadius + 1, 4, 9, 9);   // just outside detect
        CHECK(l_oR.f_Mode() == Roamer::Mode::Wander, "outside the detect radius it wanders");
        l_oR.f_Step(l_oPocket, 4 + Roamer::m_iDetectRadius, 4, 9, 9);       // inside
        CHECK(l_oR.f_Mode() == Roamer::Mode::Chase, "it starts chasing inside the radius");
        l_oR.f_Step(l_oPocket, 4 + Roamer::m_iLoseRadius - 1, 4, 9, 9);     // between the two radii
        CHECK(l_oR.f_Mode() == Roamer::Mode::Chase, "between detect and lose radius it keeps chasing");
        l_oR.f_Step(l_oPocket, 4 + Roamer::m_iLoseRadius + 2, 4, 9, 9);     // past the lose radius
        CHECK(l_oR.f_Mode() == Roamer::Mode::Wander, "past the lose radius it returns to wandering");
    }

    // a pocket it cannot fit in: no move, and no phasing out of it
    {
        Maze l_oPocket;
        l_oPocket.m_vTiles.assign(7, std::string(7, '#'));
        l_oPocket.m_vTiles[3][3] = '.';                      // a single open tile
        Roamer l_oR(3, 3, f_Fp(2, 2), 3u);           // 2x2 simply cannot exist here
        bool l_bMoved = false, l_bInside = true;
        for (int l_iI = 0; l_iI < 50; ++l_iI) l_bMoved = l_oR.f_Step(l_oPocket, 6, 6, 7, 7) || l_bMoved;
        l_bInside = (l_oR.f_X() == 3 && l_oR.f_Y() == 3);
        CHECK(!l_bMoved && l_bInside, "a roamer that cannot fit anywhere never moves and never escapes");
    }

    // determinism: same seed + same maze + same player = identical walk (reloading a floor
    // reproduces the same chase, which is what makes it testable and save-safe)
    {
        Roamer l_oA(3, 3, f_Fp(1, 1), 42u), l_oB(3, 3, f_Fp(1, 1), 42u);
        bool l_bSame = true;
        for (int l_iI = 0; l_iI < 60; ++l_iI) {
            l_oA.f_Step(l_oM, 18, 10, 20, 12);
            l_oB.f_Step(l_oM, 18, 10, 20, 12);
            if (l_oA.f_X() != l_oB.f_X() || l_oA.f_Y() != l_oB.f_Y() || l_oA.f_Mode() != l_oB.f_Mode()) { l_bSame = false; break; }
        }
        CHECK(l_bSame, "the same seed reproduces the same trajectory");
    }

    // a 1x1 roamer in a straight corridor keeps its heading (patrol-like, not twitchy)
    {
        Maze l_oCorr;
        l_oCorr.m_vTiles.assign(3, std::string(12, '#'));
        for (int l_iX = 1; l_iX < 11; ++l_iX) l_oCorr.m_vTiles[1][l_iX] = '.';
        Roamer l_oR(1, 1, f_Fp(1, 1), 8u);
        const int l_iX1 = l_oR.f_X();
        l_oR.f_Step(l_oCorr, 10, 1, 12, 3);     // the player is down the corridor: a chase advances toward it
        CHECK(l_oR.f_X() > l_iX1, "in a corridor a chase advances along the corridor");
    }
}

// ---------------------------------------------------------------- data validation
struct DataEntity { std::string m_strKind, m_strId; int m_iX, m_iY; toms::Footprint m_oFp; };

static void f_TestShippedStages() {
    using namespace toms;
    // The type table, read exactly as Game::loadAssets() reads it.
    std::map<std::string, FootprintSpec> l_mapTypeTable;
    {
        nlohmann::json l_jJ = f_ReadJsonLocal("data/footprints.json");
        if (l_jJ.is_object()) {
            for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
                if (l_oIt.key().empty() || l_oIt.key()[0] == '_') continue;
                l_mapTypeTable[l_oIt.key()] = f_FootprintSpecFromJson(l_oIt.value(), l_oIt.key());
            }
        }
    }
    CHECK(l_mapTypeTable.count("monster:demonlord_vorkath") == 1, "data/footprints.json carries the boss tier");
    const nlohmann::json l_jItemDefs = f_ReadJsonLocal("data/items.json");   // what the game can pick up (applyItem)

    Locale l_oLocale;   // stage names are plain strings in every shipped file; no table needed
    // The 11 hand-authored stages AND the 60 generated floors from S2 (tools/gen_floors.py), so the
    // generator's output is gated by the runtime's own parser + the same footprint rules rather than
    // only by the Python validator. Anything the generator emits that this test rejects is data the
    // game would have mis-loaded.
    std::vector<std::string> l_vStagePaths;
    const char* l_arrStages[] = {"stage01", "stage02", "stage03", "stage04", "stage05", "stage06",
                             "stage07", "stage08", "stage09", "stage10", "stage_11"};
    for (const char* l_pszSid : l_arrStages) l_vStagePaths.push_back(std::string("data/stages/") + l_pszSid + ".json");
    for (int l_iF = 1; l_iF <= 70; ++l_iF) {
        char l_pszBuf[64];
        snprintf(l_pszBuf, sizeof(l_pszBuf), "data/story/floors/F%02d.stage.json", l_iF);
        const std::string l_strP(l_pszBuf);
        if (FILE* l_pProbe = fopen(l_strP.c_str(), "rb")) { fclose(l_pProbe); l_vStagePaths.push_back(l_strP); }
    }
    int l_iBigTotal = 0, l_iRoamerTotal = 0;
    for (const std::string& l_strSpath : l_vStagePaths) {
        const std::string& l_strPath = l_strSpath;
        const std::string l_strSid = l_strPath;
        Stage l_oSt = f_ParseStage(l_strPath, l_oLocale);
        if (l_oSt.m_iWidth <= 0 || l_oSt.m_vTiles.empty()) { CHECK(false, (std::string("stage parses: ") + l_strSid).c_str()); continue; }
        // Every key and item on a floor must be one the game draws and picks up: "key:yellow" (what the generator and
        // the stage editors write) loads as the item key_yellow, and each item id is defined in data/items.json.
        // (Before this check, every generated floor's key was invisible and could not be picked up.)
        for (const auto& l_oE : l_oSt.m_vEntities) {
            CHECK(l_oE.m_strKind.rfind("key:", 0) != 0, (l_strSid + ": a key still loads as " + l_oE.m_strKind).c_str());
            if (l_oE.m_strKind.rfind("item:", 0) == 0)
                CHECK(l_jItemDefs.is_object() && l_jItemDefs.contains(l_oE.m_strId), (l_strSid + ": item '" + l_oE.m_strId + "' is not in data/items.json").c_str());
        }

        // resolve footprints exactly like Game::loadStage()
        std::vector<DataEntity> l_vEnts;
        for (auto& l_oE : l_oSt.m_vEntities) {
            auto l_itIt = l_mapTypeTable.find(l_oE.m_strKind);
            FootprintSpec l_oTypeSpec = (l_itIt == l_mapTypeTable.end()) ? FootprintSpec{} : l_itIt->second;
            FootprintSpec l_oSpec = f_ResolveFootprint(FootprintSpec{l_oE.m_oFp, l_oE.m_bRoamer, l_oE.m_strDisplayName},
                                                  l_oE.m_bFpExplicit, l_oTypeSpec);
            if (l_oSpec.m_oFp.f_Big()) ++l_iBigTotal;
            if (l_oSpec.m_bRoamer) ++l_iRoamerTotal;
            l_vEnts.push_back({l_oE.m_strKind, l_oE.m_strId, l_oE.m_iX, l_oE.m_iY, l_oSpec.m_oFp});
        }

        // 1) every footprint is a legal size and sits inside the grid
        bool l_bLegal = true;
        for (auto& l_oD : l_vEnts) {
            if (!l_oD.m_oFp.f_Valid()) l_bLegal = false;
            if (l_oD.m_iX + l_oD.m_oFp.m_iW > l_oSt.m_iWidth || l_oD.m_iY + l_oD.m_oFp.m_iH > (int)l_oSt.m_vTiles.size()) l_bLegal = false;
            if (l_oD.m_iY < 0 || l_oD.m_iX < 0) l_bLegal = false;
        }
        CHECK(l_bLegal, (l_strSid + std::string(": every entity footprint is legal and inside the grid")).c_str());

        // 2) every occupied tile is walkable -- a big entity must not stand in a wall (F7)
        bool l_bOnFloor = true;
        for (auto& l_oD : l_vEnts)
            for (int l_iYy = l_oD.m_iY; l_iYy < l_oD.m_iY + l_oD.m_oFp.m_iH; ++l_iYy)
                for (int l_iXx = l_oD.m_iX; l_iXx < l_oD.m_iX + l_oD.m_oFp.m_iW; ++l_iXx)
                    if (l_oSt.f_At(l_iXx, l_iYy) == '#') l_bOnFloor = false;
        CHECK(l_bOnFloor, (l_strSid + std::string(": no entity occupies a wall tile")).c_str());

        // 3) no two entities overlap (F7)
        bool l_bOverlap = false;
        for (size_t l_nI = 0; l_nI < l_vEnts.size() && !l_bOverlap; ++l_nI)
            for (size_t l_nK = l_nI + 1; l_nK < l_vEnts.size() && !l_bOverlap; ++l_nK)
                for (int l_iYy = l_vEnts[l_nI].m_iY; l_iYy < l_vEnts[l_nI].m_iY + l_vEnts[l_nI].m_oFp.m_iH && !l_bOverlap; ++l_iYy)
                    for (int l_iXx = l_vEnts[l_nI].m_iX; l_iXx < l_vEnts[l_nI].m_iX + l_vEnts[l_nI].m_oFp.m_iW; ++l_iXx)
                        if (f_FootprintCovers(l_vEnts[l_nK].m_iX, l_vEnts[l_nK].m_iY, l_vEnts[l_nK].m_oFp, l_iXx, l_iYy)) l_bOverlap = true;
        CHECK(!l_bOverlap, (l_strSid + std::string(": no two entities overlap")).c_str());

        // 4) anti-softlock: the stairs must still be reachable from the player start when the
        //    enlarged monsters block their tiles (a 2-tile-wide golem in a 2-tile corridor could
        //    otherwise seal the floor).
        int l_iSx = -1, l_iSy = -1;
        std::vector<std::pair<int,int>> l_vGoals;
        for (int l_iY = 0; l_iY < (int)l_oSt.m_vTiles.size(); ++l_iY)
            for (int l_iX = 0; l_iX < (int)l_oSt.m_vTiles[l_iY].size(); ++l_iX) {
                if (l_oSt.m_vTiles[l_iY][l_iX] == '@') { l_iSx = l_iX; l_iSy = l_iY; }
                if (l_oSt.m_vTiles[l_iY][l_iX] == 'D' || l_oSt.m_vTiles[l_iY][l_iX] == 'U') l_vGoals.push_back({l_iX, l_iY});
            }
        // M9 (stair alignment): most floors no longer carry a '@' at all -- arrival now happens
        // directly on the matching stairs tile (Game::loadStage's new arrival-direction param),
        // so a floor with no '@' still has a real, walkable start point: whichever stair it has.
        // Only F01 (the chain head, no incoming stairs) still ships an explicit '@'.
        if (l_iSx < 0 && !l_vGoals.empty()) { l_iSx = l_vGoals.back().first; l_iSy = l_vGoals.back().second; }
        CHECK(l_iSx >= 0 && !l_vGoals.empty(), (l_strSid + std::string(": has a player start and stairs")).c_str());
        if (l_iSx >= 0 && !l_vGoals.empty()) {
            auto l_fnBlockedByMonster = [&](int l_iX, int l_iY) {
                for (auto& l_oD : l_vEnts) {
                    if (l_oD.m_strKind.rfind("monster:", 0) != 0) continue;
                    if (f_FootprintCovers(l_oD.m_iX, l_oD.m_iY, l_oD.m_oFp, l_iX, l_iY)) return true;
                }
                return false;
            };
            std::set<std::pair<int,int>> l_setSeen{{l_iSx, l_iSy}};
            std::vector<std::pair<int,int>> l_vFrontier{{l_iSx, l_iSy}};
            while (!l_vFrontier.empty()) {
                auto [l_iCx, l_iCy] = l_vFrontier.back(); l_vFrontier.pop_back();
                const int l_arrDx[4] = {1,-1,0,0}, l_arrDy[4] = {0,0,1,-1};
                for (int l_iI = 0; l_iI < 4; ++l_iI) {
                    int l_iNx = l_iCx + l_arrDx[l_iI], l_iNy = l_iCy + l_arrDy[l_iI];
                    if (l_iNx < 0 || l_iNy < 0 || l_iNy >= (int)l_oSt.m_vTiles.size() || l_iNx >= l_oSt.m_iWidth) continue;
                    if (l_oSt.f_At(l_iNx, l_iNy) == '#') continue;
                    if (l_fnBlockedByMonster(l_iNx, l_iNy)) continue;
                    if (!l_setSeen.insert({l_iNx, l_iNy}).second) continue;
                    l_vFrontier.push_back({l_iNx, l_iNy});
                }
            }
            // Check every OTHER stair reaches from here -- excluding the start tile itself
            // (trivially "reachable" from itself, which no longer proves anything now that a
            // floor's start point can genuinely BE one of its own stairs).
            bool l_bReachable = true;
            for (auto& l_pairG : l_vGoals) if (l_pairG != std::make_pair(l_iSx, l_iSy) && !l_setSeen.count(l_pairG)) l_bReachable = false;
            CHECK(l_bReachable, (l_strSid + std::string(": stairs reachable with multi-grid monsters blocking")).c_str());
        }
    }
    // Sanity: the type table really does make some shipped monsters big, otherwise this whole
    // validator would pass vacuously.
    CHECK(l_iBigTotal > 0, "the shipped stages actually contain multi-grid entities");
    fprintf(stderr, "  data: %d multi-grid entities across %d stage files (%d roamers)\n",
            l_iBigTotal, (int)l_vStagePaths.size(), l_iRoamerTotal);
}

int main() {
    f_TestFootprintMath();
    f_TestFootprintJson();
    f_TestRoamer();
    f_TestShippedStages();
    printf("footprint_test: %s (%d checks)\n", g_iFails == 0 ? "ALL PASS" : "FAILED", g_iChecks);
    return g_iFails == 0 ? 0 : 1;
}
