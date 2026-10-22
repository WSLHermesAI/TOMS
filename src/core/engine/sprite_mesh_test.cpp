// sprite_mesh_test.cpp -- sprite meshes (sprite_mesh.h, docs/34_MESH_ANIMATION.md): cutting a mesh from alpha, the
// mesh file as plain text and with a binary buffer (glTF style) read back the same, the errors, and the shipped
// game.mesh.json fitting the game atlas. Runs in assets/ (the shipped atlas). No GPU.
#include "sprite_mesh.h"
#include "atlas_file.h"
#include "vfs.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

bool noBuffer(const std::string&, std::string&) { return false; }
}  // namespace

int main() {
    // ---- cut from alpha ----
    // 16 x 8 pixels, cell 4: only the left 8 x 8 is opaque -> 2 x 2 cells, 3 x 3 corners, 8 triangles.
    std::vector<uint8_t> px(16 * 8 * 4, 0);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) px[((size_t)y * 16 + x) * 4 + 3] = 255;
    toms::SpriteMesh m = toms::meshFromAlpha(px.data(), 16, 16, 8, 5, 7, 4);
    CHECK(m.vertexCount() == 9, "2 x 2 filled cells share their corners: 9 vertices");
    CHECK(m.triangles.size() == 8 * 3, "two triangles per filled cell");
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (size_t i = 0; i < m.vertices.size(); i += 2) {
        minX = std::min(minX, m.vertices[i]); maxX = std::max(maxX, m.vertices[i]);
        minY = std::min(minY, m.vertices[i + 1]); maxY = std::max(maxY, m.vertices[i + 1]);
    }
    CHECK(minX == 5 && maxX == 13 && minY == 7 && maxY == 15, "vertices in original pixels: the packed rect moved by the trim offset");
    CHECK(toms::meshFromAlpha(px.data(), 16, 16, 8, 0, 0, 4, 256).triangles.empty(), "nothing at or above the threshold: no mesh");
    // A cell size that does not divide the sprite: the last cells are cut at its edge.
    const toms::SpriteMesh edge = toms::meshFromAlpha(px.data(), 16, 6, 6, 0, 0, 4);
    bool inside = true;
    for (float v : edge.vertices) inside = inside && v >= 0 && v <= 6;
    CHECK(inside && edge.triangles.size() == 4 * 6, "a partial last cell ends at the sprite's edge");

    // ---- cut on a grid (the anim editor's Mesh grid) ----
    // The opaque left 8 x 8 of the 16 x 8 sprite on 4 x 2 cells (4 x 4 pixels each): 2 x 2 filled cells, as above.
    const toms::SpriteMesh g = toms::meshFromAlphaGrid(px.data(), 16, 16, 8, 0, 0, 4, 2);
    CHECK(g.vertexCount() == 9 && g.triangles.size() == 8 * 3 && g.cells[0] == 4 && g.cells[1] == 2,
          "a 4 x 2 grid: 2 x 2 filled cells, the grid remembered");
    const toms::SpriteMesh fine = toms::meshFromAlphaGrid(px.data(), 16, 16, 8, 0, 0, 64, 64);
    CHECK(fine.cells[0] == 16 && fine.cells[1] == 8 && fine.triangles.size() == 8 * 8 * 6,
          "a grid finer than the sprite is cut to its pixels");
    const toms::SpriteMesh odd = toms::meshFromAlphaGrid(px.data(), 16, 16, 8, 0, 0, 3, 3);
    bool whole = true;
    for (float v : odd.vertices) whole = whole && v == (float)(int)v;
    CHECK(whole, "an uneven grid still puts its corners on whole pixels");

    // ---- the file: text and binary read back the same ----
    toms::SpriteMeshFile f;
    f.meshes["box"] = m;
    toms::SpriteMesh hand;
    hand.source = "hand";
    hand.vertices = {0.5f, 0, 10, 0, 10, 10.25f};
    hand.triangles = {0, 1, 2};
    f.meshes["tri"] = hand;
    f.meshes["grid"] = g;   // "cells" goes into the file and back
    const std::string text = toms::spriteMeshesToJson(f);
    toms::SpriteMeshFile back;
    std::string err;
    CHECK(toms::parseSpriteMeshes(text, noBuffer, back, &err), "the text form parses");
    CHECK(back.meshes == f.meshes, "the text form reads back the same");
    CHECK(text.find("\"buffers\"") == std::string::npos, "the text form has no buffer");
    CHECK(text.find("8.0") == std::string::npos, "whole pixels are written as integers");
    CHECK(text.rfind("{\"version\":1,", 0) == 0, "the file starts with its version (a header)");

    std::string bin;
    const std::string withBin = toms::spriteMeshesToJson(f, "x.mesh.bin", &bin);
    CHECK(!bin.empty() && bin.size() % 4 == 0, "the binary buffer is padded to 4 bytes");
    CHECK(withBin.rfind("{\"version\":1,", 0) == 0, "the binary form's JSON starts with its version too");
    auto readBin = [&](const std::string& uri, std::string& bytes) { if (uri != "x.mesh.bin") return false; bytes = bin; return true; };
    toms::SpriteMeshFile backBin;
    CHECK(toms::parseSpriteMeshes(withBin, readBin, backBin, &err), "the binary form parses");
    CHECK(backBin.meshes == f.meshes, "the binary form reads back the same");
    toms::SpriteMeshFile none;
    CHECK(!toms::parseSpriteMeshes(withBin, noBuffer, none, &err), "a missing buffer is an error");
    std::string shortBin = bin.substr(0, bin.size() / 2);
    auto readShort = [&](const std::string&, std::string& bytes) { bytes = shortBin; return true; };
    CHECK(!toms::parseSpriteMeshes(withBin, readShort, none, &err), "a cut-off buffer is an error");

    // ---- broken meshes ----
    CHECK(!toms::parseSpriteMeshes(R"({"version":1,"meshes":{"a":{"vertices":[0,0,1,0,1,1],"triangles":[0,1,3]}}})", noBuffer, none, &err),
          "an index past the last vertex");
    CHECK(!toms::parseSpriteMeshes(R"({"version":1,"meshes":{"a":{"vertices":[0,0,1,0,1,1],"triangles":[0,1]}}})", noBuffer, none, &err),
          "a triangle that is not whole");
    CHECK(!toms::parseSpriteMeshes(R"({"version":1,"meshes":{"a":{"vertices":[0,0,1],"triangles":[]}}})", noBuffer, none, &err),
          "vertices not in pairs");
    CHECK(!toms::parseSpriteMeshes(R"({"version":2,"meshes":{}})", noBuffer, none, &err), "a newer version");
    CHECK(!toms::parseSpriteMeshes(R"({"meshes":{}})", noBuffer, none, &err) && err.find("version") != std::string::npos,
          "no version: an error that says so");

    CHECK(toms::spriteMeshPath("a/b/game.atlas") == "a/b/game.mesh.json", "game.atlas -> game.mesh.json");
    CHECK(toms::spriteMeshPath("a.b/game") == "a.b/game.mesh.json", "no extension: appended");

    if (g_fail) { std::printf("sprite_mesh_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("sprite_mesh_test: OK\n");
    return 0;
}
