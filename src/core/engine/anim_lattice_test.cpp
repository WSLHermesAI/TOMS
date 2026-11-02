// anim_lattice_test.cpp -- the deform lattice (anim_clip.h deformAt / lattice, anim_player.h latticeOffset /
// latticeView / the mesh path; docs/engine/16_MESH_ANIMATION.md step 4): the file (version 3 only when used, the errors), the
// eased offsets between keys, the bilinear offset inside a cell, a deformed sprite drawn as a bent mesh (a plain grid
// when it has no mesh file), and the lattice the editor shows. No GPU.
#include "anim_player.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)
bool f_Near(float l_fA, float l_fB, float l_fEps = 1e-3f) { return std::fabs(l_fA - l_fB) <= l_fEps; }

// "box": 16 x 16 at (0, 0) of a 64 x 64 page, pivot at the bottom centre; a node squashing it at t = 1.
Clip f_Squash() {
    Clip l_oC;
    l_oC.m_strName = "squash";
    l_oC.m_oRoot.m_strName = "box";
    l_oC.m_oRoot.m_strSprite = "box";
    l_oC.m_oRoot.m_bHasPivot = true;
    l_oC.m_oRoot.m_v2Pivot = {0.5f, 1.0f};
    l_oC.m_oRoot.m_arrLattice[0] = 3;
    l_oC.m_oRoot.m_arrLattice[1] = 3;
    Key<Deform> l_o0, l_o1;
    l_o1.m_fT = 1;
    l_o1.m_oV = {{1, {0, 4}}, {4, {2, 2}}};   // the top middle point down 4; the centre right 2 and down 2
    l_oC.m_oRoot.m_vDeformKeys = {l_o0, l_o1};
    return l_oC;
}
}  // namespace

int main() {
    // ---- the file ----
    AnimFile l_oF;
    l_oF.m_vClips.push_back(f_Squash());
    std::string l_strJson = f_AnimToJson(l_oF), l_strErr;
    CHECK(l_strJson.find("\"version\": 3") != std::string::npos && l_strJson.find("\"lattice\"") != std::string::npos &&
              l_strJson.find("\"deform\"") != std::string::npos, "a lattice and its keys: version 3");
    AnimFile l_oBack;
    CHECK(f_ParseAnim(l_strJson, l_oBack, &l_strErr) && l_oBack == l_oF, "they read back the same");
    CHECK(!f_ParseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","lattice":[1,4]}}]})", l_oBack, &l_strErr), "a lattice under 2");
    CHECK(!f_ParseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","lattice":[17,4]}}]})", l_oBack, &l_strErr), "a lattice over 16");
    CHECK(!f_ParseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","tracks":{"deform":[{"t":0,"v":{"16":[1,1]}}]}}}]})",
                     l_oBack, &l_strErr), "a point past the lattice (4 x 4 has 0 .. 15)");
    Node l_oPlain;
    CHECK(l_oPlain.f_LatticeCols() == 4 && l_oPlain.f_LatticeRows() == 4 && !l_oPlain.f_UsesMesh(), "the default lattice: 4 x 4; no keys, a quad");

    // ---- the offsets ----
    const Clip l_oC = f_Squash();
    std::vector<glm::vec2> l_vAt0 = f_DeformAt(l_oC.m_oRoot, 0), l_vHalf = f_DeformAt(l_oC.m_oRoot, 0.5f), l_vAt1 = f_DeformAt(l_oC.m_oRoot, 1);
    CHECK(l_vAt0.size() == 9 && l_vAt0[1] == glm::vec2(0.0f), "every point; at the first key, none moved");
    CHECK(f_Near(l_vHalf[1].y, 2) && f_Near(l_vHalf[4].x, 1) && f_Near(l_vHalf[0].x, 0), "half way: half the offset (linear ease)");
    CHECK(f_Near(l_vAt1[1].y, 4) && f_Near(l_vAt1[4].x, 2) && f_Near(l_vAt1[4].y, 2), "at the key: the key's offsets");
    // bilinear: the middle of the top-left cell of a 3 x 3 lattice over 16 x 16 is (4, 4); point 4 (the centre) is one
    // of its corners and moves (2, 2); point 1 (top middle) moves (0, 4): a quarter of each.
    const glm::vec2 l_v2Mid = f_LatticeOffset(l_vAt1, 3, 3, 4, 4, 16, 16);
    CHECK(f_Near(l_v2Mid.x, 0.5f) && f_Near(l_v2Mid.y, 1.5f), "inside a cell: the corners' offsets, bilinear");
    CHECK(f_Near(f_LatticeOffset(l_vAt1, 3, 3, 8, 0, 16, 16).y, 4), "on a point: exactly its offset");
    CHECK(f_LatticeOffset(l_vAt1, 3, 3, 0, 16, 16, 16) == glm::vec2(0.0f), "a corner nobody moved: no offset");

    // ---- drawn as a bent mesh (no mesh file: a plain grid) ----
    toms::AtlasFile l_oAtlas;
    toms::f_ParseAtlas("page.png\nsize: 64, 64\nbox\n  bounds: 0, 0, 16, 16\n", l_oAtlas, &l_strErr);
    AtlasSet l_oSet(l_oAtlas);
    std::vector<NodePose> l_vPoses;
    f_Evaluate(l_oC, 1, l_vPoses);
    DrawList l_oD;
    f_AppendDraws(l_oC, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vMeshes.size() == 1 && l_oD.m_vQuads.empty(), "a deformed sprite is a mesh, also without a mesh file");
    if (l_oD.m_vMeshes.size() == 1) {
        // the grid has a vertex at the top middle (8, 0) of the image: around the pivot (8, 16) that is (0, -16), moved 4 down
        bool l_bTopMiddle = false;
        for (const MeshVertex& l_oV : l_oD.m_vVertices) l_bTopMiddle = l_bTopMiddle || (f_Near(l_oV.m_fX, 0) && f_Near(l_oV.m_fY, -12));
        CHECK(l_bTopMiddle, "the top middle vertex moved down 4");
        CHECK(l_oD.m_vMeshes[0].m_uVertexCount == 25, "a plain grid twice the lattice: 5 x 5 vertices");
    }
    f_Evaluate(l_oC, 0, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oC, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    float l_fMinY = 0;
    for (const MeshVertex& l_oV : l_oD.m_vVertices) l_fMinY = std::min(l_fMinY, l_oV.m_fY);
    CHECK(l_oD.m_vMeshes.size() == 1 && f_Near(l_fMinY, -16), "at rest the mesh covers the sprite as its quad would");

    // ---- the lattice the editor shows ----
    f_Evaluate(l_oC, 1, l_vPoses);
    LatticeView l_oLv;
    CHECK(f_LatticeView(l_vPoses, 0, l_oSet, f_Placement(100, 100, 2), l_oLv) && l_oLv.m_iCols == 3 && l_oLv.m_iRows == 3 && l_oLv.m_vPoints.size() == 9,
          "the editor's lattice: every point");
    // point 1 at rest is (8, 0) in the sprite: around the pivot (0, -16), placed x2 at (100, 100): (100, 68); moved 4 down: 76
    CHECK(f_Near(l_oLv.m_vPoints[1].x, 100) && f_Near(l_oLv.m_vPoints[1].y, 76), "a point placed, with its offset");
    CHECK(l_oLv.m_vRest[8] == glm::vec2(16, 16), "its place at rest, in the sprite's pixels");

    // ---- a skinned node deforms before its bones carry it ----
    Clip l_oS = f_Squash();
    l_oS.m_oRoot.m_vSkinBones = {"box"};
    f_Evaluate(l_oS, 1, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oS, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vMeshes.empty() && l_oD.m_vQuads.size() == 1, "skinned with no mesh file: as before, its quad (no grid stands in for a skin)");

    if (g_iFail) { std::printf("anim_lattice_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("anim_lattice_test: OK\n");
    return 0;
}
