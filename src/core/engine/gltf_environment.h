// gltf_environment.h -- image-based lighting for GltfRenderer (docs/engine/08_GLTF.md §8): an equirectangular HDR
// picture (a .hdr: the room around the model) made into what the mesh shader samples:
//   - a cubemap whose mip levels are the picture blurred for rougher and rougher surfaces (GGX, prefiltered here:
//     level 0 the sharp picture, level kRoughLevels - 1 roughness 1; the smaller levels stay at roughness 1) --
//     what a surface reflects;
//   - 9 spherical-harmonic terms of the light that reaches a surface from every side (cosine-convolved and / pi:
//     evalSH(n) x the diffuse colour is the diffuse light) -- what lights it.
// CPU only: the web build does the same work, and the tests check it without a GPU.
//
// Cube faces: +X -X +Y -Y +Z -Z, each face's rows top first, in the convention every bgfx backend samples with
// (directionOf). The picture's u turns about +Y (the middle column looks down +Z, the edges down -Z: as the Khronos
// glTF Sample Viewer, so its screenshots compare), its rows run from up to down.
#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace toms::gltf {

struct Environment {
    static constexpr int m_iRoughLevels = 6;   // mips 0..5: roughness 0, 0.2, .. 1
    int m_iFaceSize = 0;                        // mip 0's face edge (a power of two)
    // faces[mip * 6 + face]: (faceSize >> mip)^2 linear RGB floats, rows top first. Every mip down to 1 x 1.
    std::vector<std::vector<float>> m_vFaces;
    int f_Mips() const { return (int)m_vFaces.size() / 6; }
    glm::vec3 m_arrSh[9];                         // the diffuse light (see evalSH)
    // Optional: the faces already as the GPU takes them (RGBA16F, alpha 1), made by f_PackHalf -- on a worker thread,
    // so the upload on the main thread is a copy (GltfRenderer::f_SetEnvironment uses them when present).
    std::vector<std::vector<uint16_t>> m_vHalfFaces;
    bool f_Valid() const { return m_iFaceSize > 0 && !m_vFaces.empty(); }
};

// An equirectangular picture, 3 floats (linear RGB) a pixel, rows top first (what stbi_loadf gives).
struct Equirect {
    int m_iWidth = 0, m_iHeight = 0;
    std::vector<float> m_vRgb;
};

// Reads a Radiance .hdr (through the vfs, so any file name works). False and why.
bool f_LoadEquirect(const std::string& l_strPath, Equirect& l_oOut, std::string* l_pError = nullptr);
bool f_LoadEquirectFromMemory(const void* l_pData, size_t l_nSize, Equirect& l_oOut, std::string* l_pError = nullptr);

// The prefiltered cubemap and the diffuse terms. samples: GGX samples per texel of the blurred levels.
Environment f_PrefilterEnvironment(const Equirect& l_oPicture, int l_iFaceSize = 256, int l_iSamples = 48);

// Fills env.m_vHalfFaces from m_vFaces (values clamped to 0 .. 60000, NaN -> 0).
void f_PackHalf(Environment& l_oEnv);
uint16_t f_HalfFromFloat(float l_fV);

// The direction of a point of a cube face; s, t in -1..1 (t = -1: the face's top row).
glm::vec3 f_DirectionOf(int l_iFace, float l_fS, float l_fT);
// The diffuse light from direction n (what fs_mesh.sc computes from Environment::sh).
glm::vec3 f_EvalSH(const glm::vec3 l_arrSh[9], const glm::vec3& l_v3N);

}  // namespace toms::gltf
