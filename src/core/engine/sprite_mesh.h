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
    std::vector<float> m_vVertices;       // x, y pairs, original sprite pixels
    std::vector<uint16_t> m_vTriangles;   // three per triangle
    std::string m_strSource = "alpha";      // "alpha": cut again by the anim editor; "hand": kept as it is
    // The grid it was cut on, cells across and down (meshFromAlphaGrid; a node's meshGrid). 0 = cut on square cells of
    // anim::kMeshCell pixels (meshFromAlpha).
    int m_arrCells[2] = {0, 0};

    uint32_t f_VertexCount() const { return (uint32_t)(m_vVertices.size() / 2); }
    bool operator==(const SpriteMesh& l_oO) const {
        return m_vVertices == l_oO.m_vVertices && m_vTriangles == l_oO.m_vTriangles && m_strSource == l_oO.m_strSource && m_arrCells[0] == l_oO.m_arrCells[0] &&
               m_arrCells[1] == l_oO.m_arrCells[1];
    }
};

struct SpriteMeshFile {
    std::map<std::string, SpriteMesh> m_mapMeshes;   // by sprite name (sorted: the file is written in a stable order)

    const SpriteMesh* f_Find(const std::string& l_strSprite) const {
        auto l_itIt = m_mapMeshes.find(l_strSprite);
        return l_itIt == m_mapMeshes.end() ? nullptr : &l_itIt->second;
    }
};

// The format version a mesh file starts with ("version", always the first key) and the newest this build reads. A
// file without one is an error.
constexpr int g_iSpriteMeshVersion = 1;

// "dir/game.atlas" -> "dir/game.mesh.json" (any extension is replaced; none: appended).
std::string f_SpriteMeshPath(const std::string& l_strAtlasPath);

// Reads a mesh file. readBuffer(uri, bytes) loads a binary buffer the file names (relative to the file); it is only
// called when the file has buffers. A mesh with an index out of range, a triangle count that is not whole, or more
// than 65536 vertices is an error.
bool f_ParseSpriteMeshes(const std::string& l_strJson, const std::function<bool(const std::string& l_strUri, std::string& l_strBytes)>& l_fnReadBuffer,
                       SpriteMeshFile& l_oOut, std::string* l_pErr = nullptr);
// Writes a mesh file. binaryUri empty: plain text. Else the numbers go to *binary (written by the caller as binaryUri,
// next to the JSON) and the JSON points at it.
std::string f_SpriteMeshesToJson(const SpriteMeshFile& l_oF, const std::string& l_strBinaryUri = std::string(),
                               std::string* l_pBinary = nullptr);

// A mesh cut from a sprite's alpha: the sprite's packed pixels (w x h, RGBA8, rows `stride` pixels apart) on a grid of
// `cell`-pixel cells; every cell with a pixel of alpha >= alphaMin becomes two triangles, sharing corners with its
// neighbours. (offX, offY) is where the packed pixels sit in the original image (the vertices are in original
// pixels). An empty sprite gives an empty mesh.
SpriteMesh f_MeshFromAlpha(const uint8_t* l_pRgba, int l_iStride, int l_iW, int l_iH, int l_iOffX, int l_iOffY, int l_iCell = 8, int l_iAlphaMin = 1);
// The same on a grid of cols x rows cells over the packed pixels (each at most one pixel narrow: a grid finer than the
// sprite is cut to its pixels). Cell edges fall on whole pixels; the mesh's `cells` is set.
SpriteMesh f_MeshFromAlphaGrid(const uint8_t* l_pRgba, int l_iStride, int l_iW, int l_iH, int l_iOffX, int l_iOffY, int l_iCols, int l_iRows, int l_iAlphaMin = 1);

// The box of a sprite's visible pixels (alpha >= alphaMin), in its ORIGINAL pixels (x right, y down from its top-left),
// with the original size: what fitting a character to a clip measures (anim_clip.h fitCharacter). Arguments as
// meshFromAlpha. A sprite with no visible pixel gives an empty box (right <= left).
struct SpriteBox {
    float m_fLeft = 0, m_fTop = 0, m_fRight = 0, m_fBottom = 0;   // right / bottom exclusive
    float m_fOrigW = 0, m_fOrigH = 0;
    bool f_Empty() const { return m_fRight <= m_fLeft || m_fBottom <= m_fTop; }
};
SpriteBox f_VisibleBox(const uint8_t* l_pRgba, int l_iStride, int l_iW, int l_iH, int l_iOffX, int l_iOffY, int l_iOrigW, int l_iOrigH, int l_iAlphaMin = 1);

}  // namespace toms
