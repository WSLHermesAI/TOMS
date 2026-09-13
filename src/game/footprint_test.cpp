// footprint_test.cpp — headless verification for S1 (佔格 footprints + roamers), plus a data
// validator over the shipped stage files. Run: ./footprint_test
//
// Two halves:
//   1. pure logic — the footprint rules from docs/ART_AND_ABILITY_DESIGN.md section 1.7 (F1-F8) and
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

static nlohmann::json readJsonLocal(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) { fprintf(stderr, "  (cannot open %s)\n", path.c_str()); return {}; }
    std::string buf;
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) buf.append(chunk, n);
    fclose(fp);
    try { return nlohmann::json::parse(buf); } catch (...) { return {}; }
}

static int g_checks = 0, g_fails = 0;

static toms::Footprint fp(int w, int h) { toms::Footprint f; f.w = w; f.h = h; return f; }

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_fails;                                                            \
            fprintf(stderr, "  FAIL: %s\n", std::string(__VA_ARGS__).c_str());    \
        }                                                                         \
    } while (0)

// ---------------------------------------------------------------- helpers
// Hand-written maze for the roamer tests: '#' wall, '.' floor.
struct Maze : public toms::GridQuery {
    std::vector<std::string> tiles;
    bool walkable(int x, int y) const override {
        if (y < 0 || y >= (int)tiles.size()) return false;
        if (x < 0 || x >= (int)tiles[y].size()) return false;
        return tiles[y][x] != '#';
    }
    // Verify a whole footprint stands on open floor (used to catch wall-phasing).
    bool stands(int x, int y, const toms::Footprint& fp) const {
        for (int dy = 0; dy < fp.h; ++dy)
            for (int dx = 0; dx < fp.w; ++dx)
                if (!walkable(x + dx, y + dy)) return false;
        return true;
    }
};

static void testFootprintMath() {
    CHECK(fp(1, 1).valid() && fp(2, 1).valid() && fp(1, 2).valid()
          && fp(2, 2).valid(), "1x1, 2x1, 1x2 and 2x2 are the legal 佔格");
    CHECK(!fp(3, 1).valid() && !fp(1, 3).valid() && !fp(3, 3).valid()
          && !fp(2, 3).valid() && !fp(4, 4).valid(),
          "3x1 / 1x3 / 2x3 / 3x3 / 4x4 are rejected (F1: cannot align to the 2x2 cell)");
    CHECK(fp(1, 1).cells() == 1 && fp(2, 1).cells() == 2 && fp(2, 2).cells() == 4,
          "cells() counts grids");
    CHECK(!fp(1, 1).big() && fp(2, 1).big() && fp(2, 2).big(), "big() = 2 or 4 grids");
    CHECK(!fp(2, 1).bossTier() && fp(2, 2).bossTier(),
          "bossTier() is exactly the 4-grid tier (F4)");

    // coverage: the anchor is the top-left occupied tile (F5)
    const toms::Footprint boss = fp(2, 2);
    CHECK(toms::footprintCovers(4, 7, boss, 4, 7) && toms::footprintCovers(4, 7, boss, 5, 7)
          && toms::footprintCovers(4, 7, boss, 4, 8) && toms::footprintCovers(4, 7, boss, 5, 8),
          "a 2x2 covers exactly its four grids");
    CHECK(!toms::footprintCovers(4, 7, boss, 6, 7) && !toms::footprintCovers(4, 7, boss, 4, 9)
          && !toms::footprintCovers(4, 7, boss, 3, 7) && !toms::footprintCovers(4, 7, boss, 4, 6),
          "a 2x2 covers nothing outside its four grids");

    CHECK(toms::footprintSortKey(5, fp(1, 1)) == 5
          && toms::footprintSortKey(5, fp(2, 1)) == 5
          && toms::footprintSortKey(5, fp(1, 2)) == 6
          && toms::footprintSortKey(5, fp(2, 2)) == 6,
          "y-sort key is the bottom-most occupied row (F5)");
}

static void testFootprintJson() {
    using namespace toms;
    // absent entry -> exactly the pre-S1 behavior
    CHECK(footprintSpecFromJson(nlohmann::json{{"w", 2}, {"h", 1}}, "3").fp.w == 2,
          "array/object forms parse");
    CHECK(footprintSpecFromJson(nlohmann::json(), "3").fp.w == 1
          && footprintSpecFromJson(nlohmann::json(), "3").cells() == 1,
          "a missing stage entry means 1x1");
    FootprintSpec arr = footprintSpecFromJson(nlohmann::json::array({2, 2}), "Z");
    CHECK(arr.fp.w == 2 && arr.fp.h == 2 && !arr.roamer, "[2,2] shorthand parses");
    FootprintSpec obj = footprintSpecFromJson(
        nlohmann::json{{"w", 2}, {"h", 2}, {"roamer", true}, {"name", "王座之影"}}, "Z");
    CHECK(obj.fp.w == 2 && obj.fp.h == 2 && obj.roamer && obj.name == "王座之影",
          "object form parses w/h/roamer/name");
    FootprintSpec bad = footprintSpecFromJson(nlohmann::json::array({3, 1}), "Q");
    CHECK(bad.fp.w == 1 && bad.fp.h == 1, "an illegal size falls back to 1x1 instead of shipping");

    // resolution priority: stage file > character type table > 1x1 (F8)
    FootprintSpec type2x1 = footprintSpecFromJson(nlohmann::json{{"w", 2}, {"h", 1}}, "3");
    FootprintSpec stage4  = footprintSpecFromJson(
        nlohmann::json{{"w", 2}, {"h", 2}, {"name", "floor boss"}}, "Z");
    CHECK(resolveFootprint(stage4, true, type2x1).fp.h == 2, "an explicit stage entry wins");
    CHECK(resolveFootprint(stage4, false, type2x1).fp.h == 1, "the type table applies when the stage is silent");
    CHECK(resolveFootprint(FootprintSpec{}, false, type2x1).fp.w == 2, "type table alone still applies");
    CHECK(resolveFootprint(FootprintSpec{}, false, FootprintSpec{}).fp.cells() == 1,
          "no stage entry and no type entry = 1x1");
    FootprintSpec oneByOneRoamer;
    oneByOneRoamer.fp = fp(1, 1);
    oneByOneRoamer.roamer = true;
    oneByOneRoamer.name = "x";
    CHECK(!resolveFootprint(FootprintSpec{}, false, oneByOneRoamer).roamer,
          "a 1x1 'roamer' is dropped: it has nowhere to roam");
}

static void testRoamer() {
    using toms::Roamer;

    // 20x12 open room with a wall ring and two interior pillars
    Maze m;
    m.tiles.assign(12, std::string(20, '.'));
    for (int x = 0; x < 20; ++x) { m.tiles[0][x] = '#'; m.tiles[11][x] = '#'; }
    for (int y = 0; y < 12; ++y) { m.tiles[y][0] = '#'; m.tiles[y][19] = '#'; }
    m.tiles[5][9] = '#'; m.tiles[5][10] = '#'; m.tiles[6][9] = '#'; m.tiles[6][10] = '#';

    // never phases through walls, never leaves the grid (F3/F4)
    {
        Roamer r(2, 2, fp(2, 2), 12345u);
        bool ok = true;
        for (int i = 0; i < 400; ++i) {
            r.step(m, 15, 9, 20, 12);
            if (!m.stands(r.x(), r.y(), r.footprint())) { ok = false; break; }
            if (r.x() < 0 || r.y() < 0 || r.x() + 2 > 20 || r.y() + 2 > 12) { ok = false; break; }
        }
        CHECK(ok, "400 turns as a 2x2 roamer: always inside the grid, never on a wall tile");
    }

    // forced into a corner: still cannot escape the maze bounds
    {
        Roamer r(17, 9, fp(2, 2), 777u);
        bool ok = true;
        for (int i = 0; i < 200; ++i) {
            r.step(m, 1, 1, 20, 12);
            if (!m.stands(r.x(), r.y(), r.footprint())) { ok = false; break; }
        }
        CHECK(ok, "a roamer pushed toward the border is clamped by its footprint");
    }

    // detection + chase: inside kDetectRadius it closes the distance
    {
        Roamer r(3, 3, fp(1, 1), 99u);
        const int px = 5, py = 4;   // manhattan distance 3 from (3,3): inside the radius
        CHECK(r.mode() == Roamer::Mode::Wander, "starts in Wander");
        CHECK(std::abs(r.x() - px) + std::abs(r.y() - py) <= Roamer::kDetectRadius,
              "test setup: player is inside the detect radius");
        const int before = std::abs(r.x() - px) + std::abs(r.y() - py);
        r.step(m, px, py, 20, 12);
        CHECK(r.mode() == Roamer::Mode::Chase, "inside the detect radius it switches to Chase");
        CHECK(std::abs(r.x() - px) + std::abs(r.y() - py) < before,
              "a chasing roamer closes the distance to the player");
    }

    // hysteresis: it gives up only past kLoseRadius, not the moment it leaves the detect radius.
    // The roamer sits in a walled pocket so it cannot move -- this isolates the mode machine from
    // pathing, which the cases above already cover.
    {
        Maze pocket;
        pocket.tiles.assign(9, std::string(9, '#'));
        pocket.tiles[4][4] = '.';
        Roamer r(4, 4, fp(1, 1), 5u);
        r.step(pocket, 4 + Roamer::kDetectRadius + 1, 4, 9, 9);   // just outside detect
        CHECK(r.mode() == Roamer::Mode::Wander, "outside the detect radius it wanders");
        r.step(pocket, 4 + Roamer::kDetectRadius, 4, 9, 9);       // inside
        CHECK(r.mode() == Roamer::Mode::Chase, "it starts chasing inside the radius");
        r.step(pocket, 4 + Roamer::kLoseRadius - 1, 4, 9, 9);     // between the two radii
        CHECK(r.mode() == Roamer::Mode::Chase, "between detect and lose radius it keeps chasing");
        r.step(pocket, 4 + Roamer::kLoseRadius + 2, 4, 9, 9);     // past the lose radius
        CHECK(r.mode() == Roamer::Mode::Wander, "past the lose radius it returns to wandering");
    }

    // a pocket it cannot fit in: no move, and no phasing out of it
    {
        Maze pocket;
        pocket.tiles.assign(7, std::string(7, '#'));
        pocket.tiles[3][3] = '.';                      // a single open tile
        Roamer r(3, 3, fp(2, 2), 3u);           // 2x2 simply cannot exist here
        bool moved = false, inside = true;
        for (int i = 0; i < 50; ++i) moved = r.step(pocket, 6, 6, 7, 7) || moved;
        inside = (r.x() == 3 && r.y() == 3);
        CHECK(!moved && inside, "a roamer that cannot fit anywhere never moves and never escapes");
    }

    // determinism: same seed + same maze + same player = identical walk (reloading a floor
    // reproduces the same chase, which is what makes it testable and save-safe)
    {
        Roamer a(3, 3, fp(1, 1), 42u), b(3, 3, fp(1, 1), 42u);
        bool same = true;
        for (int i = 0; i < 60; ++i) {
            a.step(m, 18, 10, 20, 12);
            b.step(m, 18, 10, 20, 12);
            if (a.x() != b.x() || a.y() != b.y() || a.mode() != b.mode()) { same = false; break; }
        }
        CHECK(same, "the same seed reproduces the same trajectory");
    }

    // a 1x1 roamer in a straight corridor keeps its heading (patrol-like, not twitchy)
    {
        Maze corr;
        corr.tiles.assign(3, std::string(12, '#'));
        for (int x = 1; x < 11; ++x) corr.tiles[1][x] = '.';
        Roamer r(1, 1, fp(1, 1), 8u);
        const int x1 = r.x();
        r.step(corr, 10, 1, 12, 3);     // the player is down the corridor: a chase advances toward it
        CHECK(r.x() > x1, "in a corridor a chase advances along the corridor");
    }
}

// ---------------------------------------------------------------- data validation
struct DataEntity { std::string kind, id; int x, y; toms::Footprint fp; };

static void testShippedStages() {
    using namespace toms;
    // The type table, read exactly as Game::loadAssets() reads it.
    std::map<std::string, FootprintSpec> typeTable;
    {
        nlohmann::json j = readJsonLocal("data/footprints.json");
        if (j.is_object()) {
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key().empty() || it.key()[0] == '_') continue;
                typeTable[it.key()] = footprintSpecFromJson(it.value(), it.key());
            }
        }
    }
    CHECK(typeTable.count("monster:demonlord_vorkath") == 1, "data/footprints.json carries the boss tier");

    Locale locale;   // stage names are plain strings in every shipped file; no table needed
    // The 11 hand-authored stages AND the 60 generated floors from S2 (tools/gen_floors.py), so the
    // generator's output is gated by the runtime's own parser + the same footprint rules rather than
    // only by the Python validator. Anything the generator emits that this test rejects is data the
    // game would have mis-loaded.
    std::vector<std::string> stagePaths;
    const char* kStages[] = {"stage01", "stage02", "stage03", "stage04", "stage05", "stage06",
                             "stage07", "stage08", "stage09", "stage10", "stage_11"};
    for (const char* sid : kStages) stagePaths.push_back(std::string("data/stages/") + sid + ".json");
    for (int f = 1; f <= 70; ++f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "data/story/floors/F%02d.stage.json", f);
        const std::string p(buf);
        if (FILE* probe = fopen(p.c_str(), "rb")) { fclose(probe); stagePaths.push_back(p); }
    }
    int bigTotal = 0, roamerTotal = 0;
    for (const std::string& spath : stagePaths) {
        const std::string& path = spath;
        const std::string sid = path;
        Stage st = parseStage(path, locale);
        if (st.width <= 0 || st.tiles.empty()) { CHECK(false, (std::string("stage parses: ") + sid).c_str()); continue; }

        // resolve footprints exactly like Game::loadStage()
        std::vector<DataEntity> ents;
        for (auto& e : st.entities) {
            auto it = typeTable.find(e.kind);
            FootprintSpec typeSpec = (it == typeTable.end()) ? FootprintSpec{} : it->second;
            FootprintSpec spec = resolveFootprint(FootprintSpec{e.fp, e.roamer, e.displayName},
                                                  e.fpExplicit, typeSpec);
            if (spec.fp.big()) ++bigTotal;
            if (spec.roamer) ++roamerTotal;
            ents.push_back({e.kind, e.id, e.x, e.y, spec.fp});
        }

        // 1) every footprint is a legal size and sits inside the grid
        bool legal = true;
        for (auto& d : ents) {
            if (!d.fp.valid()) legal = false;
            if (d.x + d.fp.w > st.width || d.y + d.fp.h > (int)st.tiles.size()) legal = false;
            if (d.y < 0 || d.x < 0) legal = false;
        }
        CHECK(legal, (sid + std::string(": every entity footprint is legal and inside the grid")).c_str());

        // 2) every occupied tile is walkable -- a big entity must not stand in a wall (F7)
        bool onFloor = true;
        for (auto& d : ents)
            for (int yy = d.y; yy < d.y + d.fp.h; ++yy)
                for (int xx = d.x; xx < d.x + d.fp.w; ++xx)
                    if (st.at(xx, yy) == '#') onFloor = false;
        CHECK(onFloor, (sid + std::string(": no entity occupies a wall tile")).c_str());

        // 3) no two entities overlap (F7)
        bool overlap = false;
        for (size_t i = 0; i < ents.size() && !overlap; ++i)
            for (size_t k = i + 1; k < ents.size() && !overlap; ++k)
                for (int yy = ents[i].y; yy < ents[i].y + ents[i].fp.h && !overlap; ++yy)
                    for (int xx = ents[i].x; xx < ents[i].x + ents[i].fp.w; ++xx)
                        if (footprintCovers(ents[k].x, ents[k].y, ents[k].fp, xx, yy)) overlap = true;
        CHECK(!overlap, (sid + std::string(": no two entities overlap")).c_str());

        // 4) anti-softlock: the stairs must still be reachable from the player start when the
        //    enlarged monsters block their tiles (a 2-tile-wide golem in a 2-tile corridor could
        //    otherwise seal the floor).
        int sx = -1, sy = -1;
        std::vector<std::pair<int,int>> goals;
        for (int y = 0; y < (int)st.tiles.size(); ++y)
            for (int x = 0; x < (int)st.tiles[y].size(); ++x) {
                if (st.tiles[y][x] == '@') { sx = x; sy = y; }
                if (st.tiles[y][x] == 'D' || st.tiles[y][x] == 'U') goals.push_back({x, y});
            }
        CHECK(sx >= 0 && !goals.empty(), (sid + std::string(": has a player start and stairs")).c_str());
        if (sx >= 0 && !goals.empty()) {
            auto blockedByMonster = [&](int x, int y) {
                for (auto& d : ents) {
                    if (d.kind.rfind("monster:", 0) != 0) continue;
                    if (footprintCovers(d.x, d.y, d.fp, x, y)) return true;
                }
                return false;
            };
            std::set<std::pair<int,int>> seen{{sx, sy}};
            std::vector<std::pair<int,int>> frontier{{sx, sy}};
            while (!frontier.empty()) {
                auto [cx, cy] = frontier.back(); frontier.pop_back();
                const int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
                for (int i = 0; i < 4; ++i) {
                    int nx = cx + dx[i], ny = cy + dy[i];
                    if (nx < 0 || ny < 0 || ny >= (int)st.tiles.size() || nx >= st.width) continue;
                    if (st.at(nx, ny) == '#') continue;
                    if (blockedByMonster(nx, ny)) continue;
                    if (!seen.insert({nx, ny}).second) continue;
                    frontier.push_back({nx, ny});
                }
            }
            bool reachable = false;
            for (auto& g : goals) if (seen.count(g)) reachable = true;
            CHECK(reachable, (sid + std::string(": stairs reachable with multi-grid monsters blocking")).c_str());
        }
    }
    // Sanity: the type table really does make some shipped monsters big, otherwise this whole
    // validator would pass vacuously.
    CHECK(bigTotal > 0, "the shipped stages actually contain multi-grid entities");
    fprintf(stderr, "  data: %d multi-grid entities across %d stage files (%d roamers)\n",
            bigTotal, (int)stagePaths.size(), roamerTotal);
}

int main() {
    testFootprintMath();
    testFootprintJson();
    testRoamer();
    testShippedStages();
    printf("footprint_test: %s (%d checks)\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_checks);
    return g_fails == 0 ? 0 : 1;
}
