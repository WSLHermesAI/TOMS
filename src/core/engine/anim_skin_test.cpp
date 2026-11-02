// anim_skin_test.cpp -- skinned nodes (anim_clip.h "skin", anim_player.h makeSkinnedMesh / appendDraws;
// docs/engine/16_MESH_ANIMATION.md): the skin in the .anim file (version 3 only when used), a mesh at rest lying exactly where
// the sprite's quad does, a turning bone moving the vertices near it and not the far ones, the automatic weights, and
// the fallbacks (no mesh, a rotated region: the quad). No GPU: a renderer that records quads and meshes.
#include "anim_player.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)
bool f_Near(float l_fA, float l_fB, float l_fEps = 1e-3f) { return std::fabs(l_fA - l_fB) <= l_fEps; }

struct Recorder : IRenderer {
    std::vector<char> m_vOrder;   // 'q' quad, 'm' mesh
    std::vector<MeshVertex> m_vLastMesh;
    void f_Init(uint32_t, uint32_t) override {}
    void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    void begin() override {}
    void f_DrawSprite(const Quad&) override { m_vOrder.push_back('q'); }
    void f_DrawMesh(const Mesh& l_oM) override { m_vOrder.push_back('m'); m_vLastMesh.assign(l_oM.m_pVertices, l_oM.m_pVertices + l_oM.m_uVertexCount); }
    void end() override {}
    uint32_t f_Width() const override { return 1024; }
    uint32_t f_Height() const override { return 768; }
};

// A 100 x 100 page; "bar" is 8 x 32 at (10, 10), pivot at the bottom centre. Its mesh: a column of 4 cells, 8 px each.
struct Fixture {
    toms::AtlasFile m_oAtlas;
    toms::SpriteMeshFile m_oMeshes;
    AtlasSet m_oSet;
    Fixture() {
        std::string l_strErr;
        toms::f_ParseAtlas("page.png\nsize: 100, 100\nbar\n  bounds: 10, 10, 8, 32\n  x_pivot: 0.5, 1\n"
                         "tilted\n  bounds: 30, 10, 32, 8\n  rotate: 90\n", m_oAtlas, &l_strErr);
        std::vector<uint8_t> l_vPx(8 * 32 * 4, 255);
        m_oMeshes.m_mapMeshes["bar"] = toms::f_MeshFromAlpha(l_vPx.data(), 8, 8, 32, 0, 0, 8);
        m_oMeshes.m_mapMeshes["tilted"] = m_oMeshes.m_mapMeshes["bar"];
        m_oSet.f_Add(m_oAtlas);
        m_oSet.m_vEntries.back().m_pMeshes = &m_oMeshes;   // the atlas's mesh file, by meshKey ("bar": 8-pixel cells)
    }
};

// bar skinned to "low" (at the feet) and "high" (16 px up, a child of low).
Clip f_SkinnedBar() {
    Clip l_oC;
    l_oC.m_strName = "bend";
    l_oC.m_oRoot.m_strName = "root";
    Node l_oBar;
    l_oBar.m_strName = "bar";
    l_oBar.m_strSprite = "bar";
    l_oBar.m_vSkinBones = {"low", "high"};
    Node l_oLow;
    l_oLow.m_strName = "low";
    Node l_oHigh;
    l_oHigh.m_strName = "high";
    l_oHigh.m_v3Pos = {0, -16, 0};
    l_oLow.m_vChildren.push_back(l_oHigh);
    l_oC.m_oRoot.m_vChildren.push_back(l_oBar);
    l_oC.m_oRoot.m_vChildren.push_back(l_oLow);
    return l_oC;
}

void f_TestFile() {
    AnimFile l_oF;
    l_oF.m_vClips.push_back(f_SkinnedBar());
    std::string l_strJson = f_AnimToJson(l_oF);
    CHECK(l_strJson.find("\"version\": 3") != std::string::npos, "a file with a skin is version 3");
    CHECK(l_strJson.find("\"weights\": \"auto\"") != std::string::npos, "automatic weights are written as \"auto\"");
    AnimFile l_oBack;
    std::string l_strErr;
    CHECK(f_ParseAnim(l_strJson, l_oBack, &l_strErr) && l_oBack == l_oF, "the skin reads back");

    l_oF.m_vClips[0].m_oRoot.m_vChildren[0].m_vSkinWeights = {0, 1, 0, 0, 0, 0, 0, 0,   1, 0.25f, 0, 0.75f, 0, 0, 0, 0};
    l_strJson = f_AnimToJson(l_oF);
    CHECK(f_ParseAnim(l_strJson, l_oBack, &l_strErr) && l_oBack == l_oF, "painted weights read back (zero weights left out, refilled)");

    AnimFile l_oPlain;
    l_oPlain.m_vClips.push_back(Clip());
    CHECK(f_AnimToJson(l_oPlain).find("\"version\": 2") != std::string::npos, "a file without skins stays version 2 (older builds read it)");

    CHECK(!f_ParseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","skin":{"bones":[]}}}]})", l_oBack, &l_strErr), "a skin without bones");
    CHECK(!f_ParseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","skin":{"bones":["a"],"weights":[[1,1]]}}}]})", l_oBack, &l_strErr),
          "a weight naming a bone the skin does not have");
    CHECK(!f_ParseAnim(R"({"version":5,"clips":[]})", l_oBack, &l_strErr), "a newer version");
}

void f_TestRest() {
    Fixture l_oFx;
    const Clip l_oC = f_SkinnedBar();
    std::vector<NodePose> l_vPoses;
    f_Evaluate(l_oC, 0, l_vPoses);
    DrawList l_oD;
    f_AppendDraws(l_oC, l_vPoses, l_oFx.m_oSet, f_Placement(100, 200), nullptr, l_oD);
    CHECK(l_oD.m_vMeshes.size() == 1 && l_oD.m_vQuads.empty(), "the skinned bar is a mesh");
    if (l_oD.m_vMeshes.size() != 1) return;
    // At rest the mesh lies exactly on the quad: x 96..104, y 168..200 (pivot bottom centre at (100, 200)).
    float l_fMinX = 1e9f, l_fMaxX = -1e9f, l_fMinY = 1e9f, l_fMaxY = -1e9f, l_fMinU = 1e9f, l_fMaxV = -1e9f;
    for (const MeshVertex& l_oV : l_oD.m_vVertices) {
        l_fMinX = std::min(l_fMinX, l_oV.m_fX); l_fMaxX = std::max(l_fMaxX, l_oV.m_fX); l_fMinY = std::min(l_fMinY, l_oV.m_fY); l_fMaxY = std::max(l_fMaxY, l_oV.m_fY);
        l_fMinU = std::min(l_fMinU, l_oV.m_fU); l_fMaxV = std::max(l_fMaxV, l_oV.m_fV);
    }
    CHECK(f_Near(l_fMinX, 96) && f_Near(l_fMaxX, 104) && f_Near(l_fMinY, 168) && f_Near(l_fMaxY, 200), "at rest the mesh covers the quad's rect");
    CHECK(f_Near(l_fMinU, 0.10f) && f_Near(l_fMaxV, 0.42f), "uv from the region");
    CHECK(l_oD.m_vIndices.size() == l_oFx.m_oMeshes.m_mapMeshes["bar"].m_vTriangles.size(), "the mesh's triangles");
}

void f_TestBend() {
    Fixture l_oFx;
    Clip l_oC = f_SkinnedBar();
    // "high" turns 90 degrees at t = 1.
    Key<glm::vec3> l_o0, l_o1;
    l_o1.m_fT = 1;
    l_o1.m_oV = {0, 0, 90};
    l_oC.m_oRoot.m_vChildren[1].m_vChildren[0].m_vRotKeys = {l_o0, l_o1};
    std::vector<NodePose> l_vPoses;
    f_Evaluate(l_oC, 1, l_vPoses);
    DrawList l_oD;
    f_AppendDraws(l_oC, l_vPoses, l_oFx.m_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vMeshes.size() == 1, "bent: still a mesh");
    if (l_oD.m_vMeshes.size() != 1) return;
    // The bottom row (at low's origin, y 0) hardly moves; the top row (y -32, 16 px past high: high reaches on as far
    // as from low to it) swings round high's origin.
    float l_fBottomMove = 0, l_fTopX = 0;
    int l_iBottoms = 0, l_iTops = 0;
    const toms::SpriteMesh& l_oM = l_oFx.m_oMeshes.m_mapMeshes["bar"];
    for (uint32_t l_uV = 0; l_uV < l_oM.f_VertexCount(); l_uV++) {
        const float l_fRestX = l_oM.m_vVertices[2 * l_uV] - 4, l_fRestY = l_oM.m_vVertices[2 * l_uV + 1] - 32;
        if (f_Near(l_fRestY, 0)) { l_fBottomMove += std::fabs(l_oD.m_vVertices[l_uV].m_fX - l_fRestX) + std::fabs(l_oD.m_vVertices[l_uV].m_fY - l_fRestY); l_iBottoms++; }
        if (f_Near(l_fRestY, -32)) { l_fTopX += l_oD.m_vVertices[l_uV].m_fX; l_iTops++; }
    }
    CHECK(l_iBottoms > 0 && l_fBottomMove / l_iBottoms < 2.0f, "the vertices at the still bone hardly move");
    // Turned 90 degrees clockwise on screen about (0, -16): the top (16 px above it) goes 16 px to the right.
    CHECK(l_iTops > 0 && l_fTopX / l_iTops > 12.0f, "the vertices at the turning bone swing with it");

    // Automatic weights: two influences per vertex here, summing to 1, the nearer bone leading.
    std::vector<float> l_vW;
    f_AutoSkinWeights({0, 0, 0, -32}, {glm::mat4(1.0f), f_Compose({0, -16, 0}, glm::vec3(0), glm::vec3(1))},
                    {glm::vec2(0, -16), glm::vec2(0, -32)}, l_vW);
    CHECK(l_vW.size() == 2 * 2 * g_iSkinInfluences, "kSkinInfluences pairs per vertex");
    CHECK(f_Near(l_vW[1] + l_vW[3], 1.0f) && f_Near(l_vW[9] + l_vW[11], 1.0f), "the weights sum to 1");
    CHECK(l_vW[0] == 0.0f && l_vW[1] > 0.5f, "a vertex on a bone's segment follows that bone most");
    CHECK(l_vW[8] == 1.0f && l_vW[9] > 0.9f, "a vertex far past one bone and near the other follows the near one");
}

void f_TestFallbacks() {
    Fixture l_oFx;
    Clip l_oC = f_SkinnedBar();
    std::vector<NodePose> l_vPoses;
    // No mesh for the sprite: its quad.
    toms::SpriteMeshFile l_oEmpty;
    AtlasSet l_oNoMeshes;
    l_oNoMeshes.f_Add(l_oFx.m_oAtlas);
    f_Evaluate(l_oC, 0, l_vPoses);
    DrawList l_oD;
    f_AppendDraws(l_oC, l_vPoses, l_oNoMeshes, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vMeshes.empty() && l_oD.m_vQuads.size() == 1, "a skinned sprite without a mesh draws as its quad");
    // ... and is named, so the check and the game can say so; with its mesh, nothing is.
    const std::vector<MissingMesh> l_vMissing = f_SkinnedWithoutMesh(l_oC, l_oNoMeshes);
    CHECK(l_vMissing.size() == 1 && l_vMissing[0].m_strNode == "root/bar" && l_vMissing[0].m_strSprite == "bar",
          "skinnedWithoutMesh: the skinned bar, by its node path and sprite");
    CHECK(f_SkinnedWithoutMesh(l_oC, l_oFx.m_oSet).empty(), "skinnedWithoutMesh: none when the mesh is there");
    l_oC.m_oRoot.m_vChildren[0].m_strSprite = "nowhere";
    CHECK(f_SkinnedWithoutMesh(l_oC, l_oNoMeshes).empty(), "skinnedWithoutMesh: a sprite no atlas has is another problem");
    l_oC.m_oRoot.m_vChildren[0].m_strSprite = "bar";
    f_Evaluate(l_oC, 0, l_vPoses);
    // A rotated region: its quad (meshes are cut from unrotated pixels).
    l_oC.m_oRoot.m_vChildren[0].m_strSprite = "tilted";
    f_Evaluate(l_oC, 0, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oC, l_vPoses, l_oFx.m_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vMeshes.empty() && l_oD.m_vQuads.size() == 1, "a rotated region draws as its quad");
    // Not skinned: a quad, as appendQuads.
    l_oC = f_SkinnedBar();
    l_oC.m_oRoot.m_vChildren[0].m_vSkinBones.clear();
    f_Evaluate(l_oC, 0, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oC, l_vPoses, l_oFx.m_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vMeshes.empty() && l_oD.m_vQuads.size() == 1, "a node without a skin is a quad");
}

void f_TestSkinView() {
    // What the editor's View > Show Skin draws: the bent mesh as drawn, the weights, the bones' segments now.
    Fixture l_oFx;
    Clip l_oC = f_SkinnedBar();
    l_oC.m_oRoot.m_vChildren[0].m_vSkinBones = {"low", "high", "nobody"};
    Key<glm::vec3> l_o0, l_o1;
    l_o1.m_fT = 1;
    l_o1.m_oV = {0, 0, 90};
    l_oC.m_oRoot.m_vChildren[1].m_vChildren[0].m_vRotKeys = {l_o0, l_o1};
    std::vector<NodePose> l_vPoses;
    f_Evaluate(l_oC, 1, l_vPoses);
    size_t l_nBar = 0;
    while (l_nBar < l_vPoses.size() && l_vPoses[l_nBar].m_pNode->m_strName != "bar") l_nBar++;
    SkinView l_oSv;
    CHECK(f_SkinView(l_oC, l_vPoses, l_nBar, l_oFx.m_oSet, glm::mat3(1.0f), &l_oSv) == SkinState::Ok, "the bar's skin can be shown");
    DrawList l_oD;
    f_AppendDraws(l_oC, l_vPoses, l_oFx.m_oSet, glm::mat3(1.0f), nullptr, l_oD);
    bool l_bSame = l_oD.m_vMeshes.size() == 1 && l_oSv.m_vVertices.size() == l_oD.m_vVertices.size();
    for (size_t l_nV = 0; l_bSame && l_nV < l_oSv.m_vVertices.size(); l_nV++) l_bSame = f_Near(l_oSv.m_vVertices[l_nV].x, l_oD.m_vVertices[l_nV].m_fX) && f_Near(l_oSv.m_vVertices[l_nV].y, l_oD.m_vVertices[l_nV].m_fY);
    CHECK(l_bSame, "the view's points are where the mesh is drawn");
    CHECK(l_oSv.m_vTriangles == l_oFx.m_oMeshes.m_mapMeshes["bar"].m_vTriangles && l_oSv.m_bAutoWeights, "its triangles; automatic weights");
    CHECK(l_oSv.m_vBones.size() == 3 && l_oSv.m_vBoneFound[0] && l_oSv.m_vBoneFound[1] && !l_oSv.m_vBoneFound[2], "a bone name no node has is marked");
    // low: (0, 0) -> high's origin (0, -16), not moved. high: (0, -16) on to (0, -32), turned 90 degrees round its origin.
    CHECK(f_Near(l_oSv.m_vBoneStart[0].x, 0) && f_Near(l_oSv.m_vBoneStart[0].y, 0) && f_Near(l_oSv.m_vBoneEnd[0].y, -16), "the still bone's segment");
    CHECK(f_Near(l_oSv.m_vBoneStart[1].x, 0) && f_Near(l_oSv.m_vBoneStart[1].y, -16) && f_Near(l_oSv.m_vBoneEnd[1].x, 16) && f_Near(l_oSv.m_vBoneEnd[1].y, -16),
          "the turned bone's segment, carried by it");
    bool l_bSums = true;
    for (size_t l_nV = 0; l_nV < l_oSv.m_vVertices.size(); l_nV++) {
        float l_fSum = 0;
        for (int l_iK = 0; l_iK < g_iSkinInfluences; l_iK++) l_fSum += l_oSv.m_vWeights[l_nV * 2 * g_iSkinInfluences + 2 * l_iK + 1];
        l_bSums = l_bSums && f_Near(l_fSum, 1.0f);
    }
    CHECK(l_bSums, "every point's weights sum to 1");

    // Why there is nothing to show.
    CHECK(f_SkinView(l_oC, l_vPoses, l_vPoses.size(), l_oFx.m_oSet, glm::mat3(1.0f), nullptr) == SkinState::NotSkinned, "no such pose");
    AtlasSet l_oNoMeshes;
    l_oNoMeshes.f_Add(l_oFx.m_oAtlas);
    CHECK(f_SkinView(l_oC, l_vPoses, l_nBar, l_oNoMeshes, glm::mat3(1.0f), &l_oSv) == SkinState::NoMesh && l_oSv.m_vVertices.empty(), "no mesh");
    l_oC.m_oRoot.m_vChildren[0].m_strSprite = "tilted";
    f_Evaluate(l_oC, 1, l_vPoses);
    CHECK(f_SkinView(l_oC, l_vPoses, l_nBar, l_oFx.m_oSet, glm::mat3(1.0f), nullptr) == SkinState::Rotated, "a rotated region");
    l_oC.m_oRoot.m_vChildren[0].m_strSprite = "nothere";
    f_Evaluate(l_oC, 1, l_vPoses);
    CHECK(f_SkinView(l_oC, l_vPoses, l_nBar, l_oFx.m_oSet, glm::mat3(1.0f), nullptr) == SkinState::NoSprite, "a sprite no atlas has");
}

void f_TestPlayer() {
    // AnimPlayer::draw: the skinned node goes to drawMesh, in draw order with the quads; the bend follows the time.
    Fixture l_oFx;
    Clip l_oC = f_SkinnedBar();
    Node l_oPlain;
    l_oPlain.m_strName = "plain";
    l_oPlain.m_strSprite = "bar";
    l_oC.m_oRoot.m_vChildren.push_back(l_oPlain);
    Key<glm::vec3> l_o0, l_o1;
    l_o1.m_fT = 1;
    l_o1.m_oV = {0, 0, 90};
    l_oC.m_oRoot.m_vChildren[1].m_vChildren[0].m_vRotKeys = {l_o0, l_o1};
    AnimPlayer l_oP;
    l_oP.f_Play(&l_oC);
    Recorder l_oR;
    l_oP.f_Draw(&l_oR, l_oFx.m_oSet, glm::mat3(1.0f));
    // one texture, one blending: the skin and the plain sprite's quad (4 more vertices, after it) go as ONE mesh
    CHECK(l_oR.m_vOrder == std::vector<char>({'m'}), "the mesh and the plain sprite's quad batched into one draw");
    const std::vector<MeshVertex> l_vAtRest = l_oR.m_vLastMesh;
    l_oP.f_Update(1000);
    l_oR.m_vOrder.clear();
    l_oP.f_Draw(&l_oR, l_oFx.m_oSet, glm::mat3(1.0f));
    CHECK(l_oR.m_vOrder == std::vector<char>({'m'}) && l_oR.m_vLastMesh.size() == l_vAtRest.size(), "drawn again the next frame");
    {   // a second player of the clip shares its skin cache: the same picture at the same time, its own at another
        AnimPlayer l_oTwin;
        l_oTwin.f_Play(&l_oC);
        Recorder l_oRt, l_oR0;
        l_oTwin.f_Draw(&l_oRt, l_oFx.m_oSet, glm::mat3(1.0f));
        AnimPlayer l_oFresh;
        l_oFresh.f_Play(&l_oC);
        l_oFresh.f_Update(1000);
        l_oFresh.f_Draw(&l_oR0, l_oFx.m_oSet, glm::mat3(1.0f));
        bool l_bSame = l_oRt.m_vLastMesh.size() == l_vAtRest.size() && l_oR0.m_vLastMesh.size() == l_oR.m_vLastMesh.size();
        for (size_t l_nI = 0; l_bSame && l_nI < l_vAtRest.size(); l_nI++) l_bSame = f_Near(l_oRt.m_vLastMesh[l_nI].m_fX, l_vAtRest[l_nI].m_fX) && f_Near(l_oRt.m_vLastMesh[l_nI].m_fY, l_vAtRest[l_nI].m_fY);
        for (size_t l_nI = 0; l_bSame && l_nI < l_oR.m_vLastMesh.size(); l_nI++) l_bSame = f_Near(l_oR0.m_vLastMesh[l_nI].m_fX, l_oR.m_vLastMesh[l_nI].m_fX) && f_Near(l_oR0.m_vLastMesh[l_nI].m_fY, l_oR.m_vLastMesh[l_nI].m_fY);
        CHECK(l_bSame, "players of one clip share its skin cache, each drawn at its own time");
    }
    {   // a clip of quads only stays quads (the sprite batch joins those)
        Clip l_oQ;
        l_oQ.m_oRoot.m_strName = "root";
        l_oQ.m_oRoot.m_vChildren = {l_oPlain, l_oPlain};
        AnimPlayer l_oQp;
        l_oQp.f_Play(&l_oQ);
        Recorder l_oRq;
        l_oQp.f_Draw(&l_oRq, l_oFx.m_oSet, glm::mat3(1.0f));
        CHECK(l_oRq.m_vOrder == std::vector<char>({'q', 'q'}), "quads only: drawSprite each");
    }
    bool l_bMoved = false;
    for (size_t l_nI = 0; l_nI < l_vAtRest.size() && l_nI < l_oR.m_vLastMesh.size(); l_nI++) l_bMoved = l_bMoved || !f_Near(l_vAtRest[l_nI].m_fX, l_oR.m_vLastMesh[l_nI].m_fX);
    CHECK(l_bMoved, "the mesh is bent again as the bones move");
}
void f_TestMeshKeys() {
    // An atlas's mesh file (docs/engine/16_MESH_ANIMATION.md): keyed by sprite and the node's grid, shared by every clip using
    // the atlas; what a clip file needs; cut from alpha on the node's grid.
    CHECK(f_MeshKey("golem") == "golem" && f_MeshKey("golem", 6, 8) == "golem@6x8", "meshKey");
    Fixture l_oFx;
    Clip l_oC = f_SkinnedBar();
    Node& l_oBar = l_oC.m_oRoot.m_vChildren[0];
    CHECK(l_oFx.m_oSet.f_MeshKeyFor("bar", l_oBar) == "bar" && l_oFx.m_oSet.f_FindMesh("bar", l_oBar) != nullptr, "no grid: the 8-pixel mesh");
    l_oBar.m_arrMeshGrid[0] = 1;
    l_oBar.m_arrMeshGrid[1] = 2;
    CHECK(l_oFx.m_oSet.f_MeshKeyFor("bar", l_oBar) == "bar@1x2" && l_oFx.m_oSet.f_FindMesh("bar", l_oBar) == nullptr, "a grid: its own key, not cut yet");
    Key<std::string> l_oSwap;
    l_oSwap.m_fT = 1;
    l_oSwap.m_oV = "tilted";
    l_oBar.m_vSpriteKeys = {l_oSwap};
    AnimFile l_oF;
    l_oF.m_vClips.push_back(l_oC);
    l_oF.m_vClips.push_back(l_oC);   // a second clip using the same: shared
    const std::vector<MeshNeed> l_vNeeds = f_MeshesNeeded(l_oF, l_oFx.m_oSet);
    CHECK(l_vNeeds.size() == 2 && l_vNeeds[0].m_strKey == "bar@1x2" && l_vNeeds[1].m_strKey == "tilted@1x2" && l_vNeeds[0].m_iCols == 1 && l_vNeeds[0].m_iRows == 2,
          "meshesNeeded: the skinned node's rest sprite and its sprite key, on its grid, once for both clips");
    std::vector<uint8_t> l_vPx(8 * 32 * 4, 255);
    const toms::SpriteMesh l_oCut = f_CutMesh(l_vNeeds[0], l_vPx.data(), 8);
    CHECK(l_oCut.f_VertexCount() == 6 && l_oCut.m_vTriangles.size() == 12, "cutMesh: 1 x 2 cells: 6 points, 4 triangles");
    CHECK(f_CutMesh(l_vNeeds[1], l_vPx.data(), 8).m_vTriangles.empty(), "cutMesh: a rotated region has none");
    toms::SpriteMeshFile l_oOwn;
    l_oOwn.m_mapMeshes["bar@1x2"] = l_oCut;
    AtlasSet l_oWithOwn = l_oFx.m_oSet;
    l_oWithOwn.m_vEntries[0].m_pMeshes = &l_oOwn;
    CHECK(l_oWithOwn.f_FindMesh("bar", l_oBar) == &l_oOwn.m_mapMeshes["bar@1x2"], "findMesh: the node's grid's mesh");
}
}  // namespace

int main() {
    f_TestMeshKeys();
    f_TestFile();
    f_TestRest();
    f_TestBend();
    f_TestFallbacks();
    f_TestSkinView();
    f_TestPlayer();
    if (g_iFail) { std::printf("anim_skin_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("anim_skin_test: OK\n");
    return 0;
}
