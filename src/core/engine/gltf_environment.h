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
    static constexpr int kRoughLevels = 6;   // mips 0..5: roughness 0, 0.2, .. 1
    int faceSize = 0;                        // mip 0's face edge (a power of two)
    // faces[mip * 6 + face]: (faceSize >> mip)^2 linear RGB floats, rows top first. Every mip down to 1 x 1.
    std::vector<std::vector<float>> faces;
    int mips() const { return (int)faces.size() / 6; }
    glm::vec3 sh[9];                         // the diffuse light (see evalSH)
    bool valid() const { return faceSize > 0 && !faces.empty(); }
};

// An equirectangular picture, 3 floats (linear RGB) a pixel, rows top first (what stbi_loadf gives).
struct Equirect {
    int width = 0, height = 0;
    std::vector<float> rgb;
};

// Reads a Radiance .hdr (through the vfs, so any file name works). False and why.
bool loadEquirect(const std::string& path, Equirect& out, std::string* error = nullptr);
bool loadEquirectFromMemory(const void* data, size_t size, Equirect& out, std::string* error = nullptr);

// The prefiltered cubemap and the diffuse terms. samples: GGX samples per texel of the blurred levels.
Environment prefilterEnvironment(const Equirect& picture, int faceSize = 256, int samples = 48);

// The direction of a point of a cube face; s, t in -1..1 (t = -1: the face's top row).
glm::vec3 directionOf(int face, float s, float t);
// The diffuse light from direction n (what fs_mesh.sc computes from Environment::sh).
glm::vec3 evalSH(const glm::vec3 sh[9], const glm::vec3& n);

}  // namespace toms::gltf
