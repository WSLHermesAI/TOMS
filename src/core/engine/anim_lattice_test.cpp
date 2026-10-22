// anim_lattice_test.cpp -- the deform lattice (anim_clip.h deformAt / lattice, anim_player.h latticeOffset /
// latticeView / the mesh path; docs/34_MESH_ANIMATION.md step 4): the file (version 3 only when used, the errors), the
// eased offsets between keys, the bilinear offset inside a cell, a deformed sprite drawn as a bent mesh (a plain grid
// when it has no mesh file), and the lattice the editor shows. No GPU.
#include "anim_player.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// "box": 16 x 16 at (0, 0) of a 64 x 64 page, pivot at the bottom centre; a node squashing it at t = 1.
Clip squash() {
    Clip c;
    c.name = "squash";
    c.root.name = "box";
    c.root.sprite = "box";
    c.root.hasPivot = true;
    c.root.pivot = {0.5f, 1.0f};
    c.root.lattice[0] = 3;
    c.root.lattice[1] = 3;
    Key<Deform> k0, k1;
    k1.t = 1;
    k1.v = {{1, {0, 4}}, {4, {2, 2}}};   // the top middle point down 4; the centre right 2 and down 2
    c.root.deformKeys = {k0, k1};
    return c;
}
}  // namespace

int main() {
    // ---- the file ----
    AnimFile f;
    f.clips.push_back(squash());
    std::string json = animToJson(f), err;
    CHECK(json.find("\"version\": 3") != std::string::npos && json.find("\"lattice\"") != std::string::npos &&
              json.find("\"deform\"") != std::string::npos, "a lattice and its keys: version 3");
    AnimFile back;
    CHECK(parseAnim(json, back, &err) && back == f, "they read back the same");
    CHECK(!parseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","lattice":[1,4]}}]})", back, &err), "a lattice under 2");
    CHECK(!parseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","lattice":[17,4]}}]})", back, &err), "a lattice over 16");
    CHECK(!parseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","tracks":{"deform":[{"t":0,"v":{"16":[1,1]}}]}}}]})",
                     back, &err), "a point past the lattice (4 x 4 has 0 .. 15)");
    Node plain;
    CHECK(plain.latticeCols() == 4 && plain.latticeRows() == 4 && !plain.usesMesh(), "the default lattice: 4 x 4; no keys, a quad");

    // ---- the offsets ----
    const Clip c = squash();
    std::vector<glm::vec2> at0 = deformAt(c.root, 0), half = deformAt(c.root, 0.5f), at1 = deformAt(c.root, 1);
    CHECK(at0.size() == 9 && at0[1] == glm::vec2(0.0f), "every point; at the first key, none moved");
    CHECK(near(half[1].y, 2) && near(half[4].x, 1) && near(half[0].x, 0), "half way: half the offset (linear ease)");
    CHECK(near(at1[1].y, 4) && near(at1[4].x, 2) && near(at1[4].y, 2), "at the key: the key's offsets");
    // bilinear: the middle of the top-left cell of a 3 x 3 lattice over 16 x 16 is (4, 4); point 4 (the centre) is one
    // of its corners and moves (2, 2); point 1 (top middle) moves (0, 4): a quarter of each.
    const glm::vec2 mid = latticeOffset(at1, 3, 3, 4, 4, 16, 16);
    CHECK(near(mid.x, 0.5f) && near(mid.y, 1.5f), "inside a cell: the corners' offsets, bilinear");
    CHECK(near(latticeOffset(at1, 3, 3, 8, 0, 16, 16).y, 4), "on a point: exactly its offset");
    CHECK(latticeOffset(at1, 3, 3, 0, 16, 16, 16) == glm::vec2(0.0f), "a corner nobody moved: no offset");

    // ---- drawn as a bent mesh (no mesh file: a plain grid) ----
    toms::AtlasFile atlas;
    toms::parseAtlas("page.png\nsize: 64, 64\nbox\n  bounds: 0, 0, 16, 16\n", atlas, &err);
    AtlasSet set(atlas);
    std::vector<NodePose> poses;
    evaluate(c, 1, poses);
    DrawList d;
    appendDraws(c, poses, set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.meshes.size() == 1 && d.quads.empty(), "a deformed sprite is a mesh, also without a mesh file");
    if (d.meshes.size() == 1) {
        // the grid has a vertex at the top middle (8, 0) of the image: around the pivot (8, 16) that is (0, -16), moved 4 down
        bool topMiddle = false;
        for (const MeshVertex& v : d.vertices) topMiddle = topMiddle || (near(v.x, 0) && near(v.y, -12));
        CHECK(topMiddle, "the top middle vertex moved down 4");
        CHECK(d.meshes[0].vertexCount == 25, "a plain grid twice the lattice: 5 x 5 vertices");
    }
    evaluate(c, 0, poses);
    d.clear();
    appendDraws(c, poses, set, glm::mat3(1.0f), nullptr, d);
    float minY = 0;
    for (const MeshVertex& v : d.vertices) minY = std::min(minY, v.y);
    CHECK(d.meshes.size() == 1 && near(minY, -16), "at rest the mesh covers the sprite as its quad would");

    // ---- the lattice the editor shows ----
    evaluate(c, 1, poses);
    LatticeView lv;
    CHECK(latticeView(poses, 0, set, placement(100, 100, 2), lv) && lv.cols == 3 && lv.rows == 3 && lv.points.size() == 9,
          "the editor's lattice: every point");
    // point 1 at rest is (8, 0) in the sprite: around the pivot (0, -16), placed x2 at (100, 100): (100, 68); moved 4 down: 76
    CHECK(near(lv.points[1].x, 100) && near(lv.points[1].y, 76), "a point placed, with its offset");
    CHECK(lv.rest[8] == glm::vec2(16, 16), "its place at rest, in the sprite's pixels");

    // ---- a skinned node deforms before its bones carry it ----
    Clip s = squash();
    s.root.skinBones = {"box"};
    evaluate(s, 1, poses);
    d.clear();
    appendDraws(s, poses, set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.meshes.empty() && d.quads.size() == 1, "skinned with no mesh file: as before, its quad (no grid stands in for a skin)");

    if (g_fail) { std::printf("anim_lattice_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("anim_lattice_test: OK\n");
    return 0;
}
