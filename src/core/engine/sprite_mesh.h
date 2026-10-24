// sprite_mesh.h -- triangle meshes of sprites (docs/engine/16_MESH_ANIMATION.md): what a skinned or deformed sprite is bent
// with. Each atlas has one mesh file beside it, "game.atlas" -> "game.mesh.json" (spriteMeshPath), keyed by
// anim_player.h meshKey ("golem", "golem@6x8" for a node's grid) and shared by every clip using the atlas. Only the anim
// tools write it (the editor merges a clip's meshes in on save; `anim_editor --headless meshes`); the atlas tool never
// touches it. Like glTF, the numbers are either in the JSON itself (plain text) or in a binary buffer beside it
// ("game.mesh.bin"); the writer picks, the reader takes both. "version" is the first key.
//
//   { "version": 1,
//     "buffers": [{"uri": "game.mesh.bin", "byteLength": 1234}],          // only with a binary buffer
//     "meshes": {
//       "golem@8x8": { "source": "alpha", "cells": [8, 8],          // made from the alpha, or "hand"; the grid
//                  "vertices": [x, y, ...],  "triangles": [i, j, k, ...] },          // plain text, or
//       "slime": { "vertexCount": 25, "vertices":  {"buffer": 0, "byteOffset": 0},    // float32 x, y each
//                  "indexCount": 96, "triangles": {"buffer": 0, "byteOffset": 200} }  // uint16, little-endian
//     } }
//
// Vertices are in the sprite's ORIGINAL pixels (x right, y down from its top-left, as AtlasRegion's offsets), so a
// mesh fits any packing of the atlas: the UVs come from the region when it is drawn.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace toms {

struct SpriteMesh {
    std::vector<float> vertices;       // x, y pairs, original sprite pixels
    std::vector<uint16_t> triangles;   // three per triangle
    std::string source = "alpha";      // "alpha": cut again by the anim editor; "hand": kept as it is
    // The grid it was cut on, cells across and down (meshFromAlphaGrid; a node's meshGrid). 0 = cut on square cells of
    // anim::kMeshCell pixels (meshFromAlpha).
    int cells[2] = {0, 0};

    uint32_t vertexCount() const { return (uint32_t)(vertices.size() / 2); }
    bool operator==(const SpriteMesh& o) const {
        return vertices == o.vertices && triangles == o.triangles && source == o.source && cells[0] == o.cells[0] &&
               cells[1] == o.cells[1];
    }
};

struct SpriteMeshFile {
    std::map<std::string, SpriteMesh> meshes;   // by sprite name (sorted: the file is written in a stable order)

    const SpriteMesh* find(const std::string& sprite) const {
        auto it = meshes.find(sprite);
        return it == meshes.end() ? nullptr : &it->second;
    }
};

// The format version a mesh file starts with ("version", always the first key) and the newest this build reads. A
// file without one is an error.
constexpr int kSpriteMeshVersion = 1;

// "dir/game.atlas" -> "dir/game.mesh.json" (any extension is replaced; none: appended).
std::string spriteMeshPath(const std::string& atlasPath);

// Reads a mesh file. readBuffer(uri, bytes) loads a binary buffer the file names (relative to the file); it is only
// called when the file has buffers. A mesh with an index out of range, a triangle count that is not whole, or more
// than 65536 vertices is an error.
bool parseSpriteMeshes(const std::string& json, const std::function<bool(const std::string& uri, std::string& bytes)>& readBuffer,
                       SpriteMeshFile& out, std::string* err = nullptr);
// Writes a mesh file. binaryUri empty: plain text. Else the numbers go to *binary (written by the caller as binaryUri,
// next to the JSON) and the JSON points at it.
std::string spriteMeshesToJson(const SpriteMeshFile& f, const std::string& binaryUri = std::string(),
                               std::string* binary = nullptr);

// A mesh cut from a sprite's alpha: the sprite's packed pixels (w x h, RGBA8, rows `stride` pixels apart) on a grid of
// `cell`-pixel cells; every cell with a pixel of alpha >= alphaMin becomes two triangles, sharing corners with its
// neighbours. (offX, offY) is where the packed pixels sit in the original image (the vertices are in original
// pixels). An empty sprite gives an empty mesh.
SpriteMesh meshFromAlpha(const uint8_t* rgba, int stride, int w, int h, int offX, int offY, int cell = 8, int alphaMin = 1);
// The same on a grid of cols x rows cells over the packed pixels (each at most one pixel narrow: a grid finer than the
// sprite is cut to its pixels). Cell edges fall on whole pixels; the mesh's `cells` is set.
SpriteMesh meshFromAlphaGrid(const uint8_t* rgba, int stride, int w, int h, int offX, int offY, int cols, int rows, int alphaMin = 1);

// The box of a sprite's visible pixels (alpha >= alphaMin), in its ORIGINAL pixels (x right, y down from its top-left),
// with the original size: what fitting a character to a clip measures (anim_clip.h fitCharacter). Arguments as
// meshFromAlpha. A sprite with no visible pixel gives an empty box (right <= left).
struct SpriteBox {
    float left = 0, top = 0, right = 0, bottom = 0;   // right / bottom exclusive
    float origW = 0, origH = 0;
    bool empty() const { return right <= left || bottom <= top; }
};
SpriteBox visibleBox(const uint8_t* rgba, int stride, int w, int h, int offX, int offY, int origW, int origH, int alphaMin = 1);

}  // namespace toms
