// sprite_mesh_test.cpp -- sprite meshes (sprite_mesh.h, docs/engine/16_MESH_ANIMATION.md): cutting a mesh from alpha, the
// mesh file as plain text and with a binary buffer (glTF style) read back the same, the errors, and the shipped
// game.mesh.json fitting the game atlas. Runs in assets/ (the shipped atlas). No GPU.
#include "sprite_mesh.h"
#include "atlas_file.h"
#include "vfs.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)

bool f_NoBuffer(const std::string&, std::string&) { return false; }
}  // namespace

int main() {
    // ---- cut from alpha ----
    // 16 x 8 pixels, cell 4: only the left 8 x 8 is opaque -> 2 x 2 cells, 3 x 3 corners, 8 triangles.
    std::vector<uint8_t> l_vPx(16 * 8 * 4, 0);
    for (int l_iY = 0; l_iY < 8; l_iY++)
        for (int l_iX = 0; l_iX < 8; l_iX++) l_vPx[((size_t)l_iY * 16 + l_iX) * 4 + 3] = 255;
    toms::SpriteMesh l_oM = toms::f_MeshFromAlpha(l_vPx.data(), 16, 16, 8, 5, 7, 4);
    CHECK(l_oM.f_VertexCount() == 9, "2 x 2 filled cells share their corners: 9 vertices");
    CHECK(l_oM.m_vTriangles.size() == 8 * 3, "two triangles per filled cell");
    float l_fMinX = 1e9f, l_fMaxX = -1e9f, l_fMinY = 1e9f, l_fMaxY = -1e9f;
    for (size_t l_nI = 0; l_nI < l_oM.m_vVertices.size(); l_nI += 2) {
        l_fMinX = std::min(l_fMinX, l_oM.m_vVertices[l_nI]); l_fMaxX = std::max(l_fMaxX, l_oM.m_vVertices[l_nI]);
        l_fMinY = std::min(l_fMinY, l_oM.m_vVertices[l_nI + 1]); l_fMaxY = std::max(l_fMaxY, l_oM.m_vVertices[l_nI + 1]);
    }
    CHECK(l_fMinX == 5 && l_fMaxX == 13 && l_fMinY == 7 && l_fMaxY == 15, "vertices in original pixels: the packed rect moved by the trim offset");
    CHECK(toms::f_MeshFromAlpha(l_vPx.data(), 16, 16, 8, 0, 0, 4, 256).m_vTriangles.empty(), "nothing at or above the threshold: no mesh");
    // A cell size that does not divide the sprite: the last cells are cut at its edge.
    const toms::SpriteMesh l_oEdge = toms::f_MeshFromAlpha(l_vPx.data(), 16, 6, 6, 0, 0, 4);
    bool l_bInside = true;
    for (float l_fV : l_oEdge.m_vVertices) l_bInside = l_bInside && l_fV >= 0 && l_fV <= 6;
    CHECK(l_bInside && l_oEdge.m_vTriangles.size() == 4 * 6, "a partial last cell ends at the sprite's edge");

    // ---- cut on a grid (the anim editor's Mesh grid) ----
    // The opaque left 8 x 8 of the 16 x 8 sprite on 4 x 2 cells (4 x 4 pixels each): 2 x 2 filled cells, as above.
    const toms::SpriteMesh l_oG = toms::f_MeshFromAlphaGrid(l_vPx.data(), 16, 16, 8, 0, 0, 4, 2);
    CHECK(l_oG.f_VertexCount() == 9 && l_oG.m_vTriangles.size() == 8 * 3 && l_oG.m_arrCells[0] == 4 && l_oG.m_arrCells[1] == 2,
          "a 4 x 2 grid: 2 x 2 filled cells, the grid remembered");
    const toms::SpriteMesh l_oFine = toms::f_MeshFromAlphaGrid(l_vPx.data(), 16, 16, 8, 0, 0, 64, 64);
    CHECK(l_oFine.m_arrCells[0] == 16 && l_oFine.m_arrCells[1] == 8 && l_oFine.m_vTriangles.size() == 8 * 8 * 6,
          "a grid finer than the sprite is cut to its pixels");
    const toms::SpriteMesh l_oOdd = toms::f_MeshFromAlphaGrid(l_vPx.data(), 16, 16, 8, 0, 0, 3, 3);
    bool l_bWhole = true;
    for (float l_fV : l_oOdd.m_vVertices) l_bWhole = l_bWhole && l_fV == (float)(int)l_fV;
    CHECK(l_bWhole, "an uneven grid still puts its corners on whole pixels");

    // ---- the file: text and binary read back the same ----
    toms::SpriteMeshFile l_oF;
    l_oF.m_mapMeshes["box"] = l_oM;
    toms::SpriteMesh l_oHand;
    l_oHand.m_strSource = "hand";
    l_oHand.m_vVertices = {0.5f, 0, 10, 0, 10, 10.25f};
    l_oHand.m_vTriangles = {0, 1, 2};
    l_oF.m_mapMeshes["tri"] = l_oHand;
    l_oF.m_mapMeshes["grid"] = l_oG;   // "cells" goes into the file and back
    const std::string l_strText = toms::f_SpriteMeshesToJson(l_oF);
    toms::SpriteMeshFile l_oBack;
    std::string l_strErr;
    CHECK(toms::f_ParseSpriteMeshes(l_strText, f_NoBuffer, l_oBack, &l_strErr), "the text form parses");
    CHECK(l_oBack.m_mapMeshes == l_oF.m_mapMeshes, "the text form reads back the same");
    CHECK(l_strText.find("\"buffers\"") == std::string::npos, "the text form has no buffer");
    CHECK(l_strText.find("8.0") == std::string::npos, "whole pixels are written as integers");
    CHECK(l_strText.rfind("{\"version\":1,", 0) == 0, "the file starts with its version (a header)");

    std::string l_strBin;
    const std::string l_strWithBin = toms::f_SpriteMeshesToJson(l_oF, "x.mesh.bin", &l_strBin);
    CHECK(!l_strBin.empty() && l_strBin.size() % 4 == 0, "the binary buffer is padded to 4 bytes");
    CHECK(l_strWithBin.rfind("{\"version\":1,", 0) == 0, "the binary form's JSON starts with its version too");
    auto l_fnReadBin = [&](const std::string& l_strUri, std::string& l_strBytes) { if (l_strUri != "x.mesh.bin") return false; l_strBytes = l_strBin; return true; };
    toms::SpriteMeshFile l_oBackBin;
    CHECK(toms::f_ParseSpriteMeshes(l_strWithBin, l_fnReadBin, l_oBackBin, &l_strErr), "the binary form parses");
    CHECK(l_oBackBin.m_mapMeshes == l_oF.m_mapMeshes, "the binary form reads back the same");
    toms::SpriteMeshFile l_oNone;
    CHECK(!toms::f_ParseSpriteMeshes(l_strWithBin, f_NoBuffer, l_oNone, &l_strErr), "a missing buffer is an error");
    std::string l_strShortBin = l_strBin.substr(0, l_strBin.size() / 2);
    auto l_fnReadShort = [&](const std::string&, std::string& l_strBytes) { l_strBytes = l_strShortBin; return true; };
    CHECK(!toms::f_ParseSpriteMeshes(l_strWithBin, l_fnReadShort, l_oNone, &l_strErr), "a cut-off buffer is an error");

    // ---- broken meshes ----
    CHECK(!toms::f_ParseSpriteMeshes(R"({"version":1,"meshes":{"a":{"vertices":[0,0,1,0,1,1],"triangles":[0,1,3]}}})", f_NoBuffer, l_oNone, &l_strErr),
          "an index past the last vertex");
    CHECK(!toms::f_ParseSpriteMeshes(R"({"version":1,"meshes":{"a":{"vertices":[0,0,1,0,1,1],"triangles":[0,1]}}})", f_NoBuffer, l_oNone, &l_strErr),
          "a triangle that is not whole");
    CHECK(!toms::f_ParseSpriteMeshes(R"({"version":1,"meshes":{"a":{"vertices":[0,0,1],"triangles":[]}}})", f_NoBuffer, l_oNone, &l_strErr),
          "vertices not in pairs");
    CHECK(!toms::f_ParseSpriteMeshes(R"({"version":2,"meshes":{}})", f_NoBuffer, l_oNone, &l_strErr), "a newer version");
    CHECK(!toms::f_ParseSpriteMeshes(R"({"meshes":{}})", f_NoBuffer, l_oNone, &l_strErr) && l_strErr.find("version") != std::string::npos,
          "no version: an error that says so");

    CHECK(toms::f_SpriteMeshPath("a/b/game.atlas") == "a/b/game.mesh.json", "game.atlas -> game.mesh.json");
    CHECK(toms::f_SpriteMeshPath("a.b/game") == "a.b/game.mesh.json", "no extension: appended");

    if (g_iFail) { std::printf("sprite_mesh_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("sprite_mesh_test: OK\n");
    return 0;
}
