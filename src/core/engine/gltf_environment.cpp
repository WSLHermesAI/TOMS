// gltf_environment.cpp -- see gltf_environment.h.
#include "gltf_environment.h"

#include "vfs.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace toms::gltf {

namespace {

constexpr float kPi = 3.14159265358979f;

// The picture and its halvings (box-filtered; the columns wrap): sampled at a level that matches how much of the
// sphere a sample stands for, so a blurred level does not alias.
struct Pyramid {
    struct Level {
        int w = 0, h = 0;
        std::vector<float> rgb;
    };
    std::vector<Level> levels;

    explicit Pyramid(const Equirect& p) {
        levels.push_back({p.width, p.height, p.rgb});
        while (levels.back().w > 8 && levels.back().h > 4) {
            const Level& a = levels.back();
            Level b{a.w / 2, a.h / 2, {}};
            b.rgb.resize((size_t)b.w * b.h * 3);
            for (int y = 0; y < b.h; y++)
                for (int x = 0; x < b.w; x++)
                    for (int c = 0; c < 3; c++) {
                        float s = 0.0f;
                        for (int dy = 0; dy < 2; dy++)
                            for (int dx = 0; dx < 2; dx++)
                                s += a.rgb[((size_t)(2 * y + dy) * a.w + (size_t)(2 * x + dx)) * 3 + c];
                        b.rgb[((size_t)y * b.w + x) * 3 + c] = s * 0.25f;
                    }
            levels.push_back(std::move(b));
        }
    }

    // Bilinear at (u, v) of one level: u wraps, v clamps.
    glm::vec3 fetch(int level, float u, float v) const {
        const Level& L = levels[(size_t)std::clamp(level, 0, (int)levels.size() - 1)];
        const float fx = u * L.w - 0.5f, fy = std::clamp(v * L.h - 0.5f, 0.0f, (float)(L.h - 1));
        const int x0 = (int)std::floor(fx), y0 = (int)fy;
        const float tx = fx - (float)x0, ty = fy - (float)y0;
        auto at = [&](int x, int y) {
            x = ((x % L.w) + L.w) % L.w;
            y = std::min(y, L.h - 1);
            const float* p = &L.rgb[((size_t)y * L.w + x) * 3];
            return glm::vec3(p[0], p[1], p[2]);
        };
        return glm::mix(glm::mix(at(x0, y0), at(x0 + 1, y0), tx), glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx), ty);
    }

    // The picture in direction d, blurred to `lod` (0 = full size; fractions blend two levels).
    glm::vec3 sample(const glm::vec3& d, float lod) const {
        const float u = 0.5f + std::atan2(-d.x, d.z) / (2.0f * kPi);   // the Khronos sample viewer's way round
        const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
        lod = std::clamp(lod, 0.0f, (float)(levels.size() - 1));
        const int l0 = (int)lod;
        const float f = lod - (float)l0;
        const glm::vec3 a = fetch(l0, u, v);
        return f > 0.0f ? glm::mix(a, fetch(l0 + 1, u, v), f) : a;
    }
};

float radicalInverse(uint32_t b) {   // Van der Corput, for the Hammersley points
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return (float)b * 2.3283064365386963e-10f;
}

// The 9 real spherical harmonics (bands 0..2) at unit direction n.
void shBasis(const glm::vec3& n, float y[9]) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * n.y;
    y[2] = 0.488603f * n.z;
    y[3] = 0.488603f * n.x;
    y[4] = 1.092548f * n.x * n.y;
    y[5] = 1.092548f * n.y * n.z;
    y[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    y[7] = 1.092548f * n.x * n.z;
    y[8] = 0.546274f * (n.x * n.x - n.y * n.y);
}

}  // namespace

glm::vec3 directionOf(int face, float s, float t) {
    glm::vec3 d;
    switch (face) {
    case 0: d = glm::vec3(1.0f, -t, -s); break;    // +X
    case 1: d = glm::vec3(-1.0f, -t, s); break;    // -X
    case 2: d = glm::vec3(s, 1.0f, t); break;      // +Y
    case 3: d = glm::vec3(s, -1.0f, -t); break;    // -Y
    case 4: d = glm::vec3(s, -t, 1.0f); break;     // +Z
    default: d = glm::vec3(-s, -t, -1.0f); break;  // -Z
    }
    return glm::normalize(d);
}

glm::vec3 evalSH(const glm::vec3 sh[9], const glm::vec3& n) {
    float y[9];
    shBasis(n, y);
    glm::vec3 c(0.0f);
    for (int i = 0; i < 9; i++) c += sh[i] * y[i];
    return glm::max(c, glm::vec3(0.0f));
}

bool loadEquirectFromMemory(const void* data, size_t size, Equirect& out, std::string* error) {
    int w = 0, h = 0, n = 0;
    float* px = stbi_loadf_from_memory((const stbi_uc*)data, (int)size, &w, &h, &n, 3);
    if (!px) {
        if (error) *error = std::string("not a picture stb_image can read (") + stbi_failure_reason() + ")";
        return false;
    }
    if (w < 8 || h < 4) {
        stbi_image_free(px);
        if (error) *error = "the picture is too small";
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgb.assign(px, px + (size_t)w * h * 3);
    stbi_image_free(px);
    return true;
}

bool loadEquirect(const std::string& path, Equirect& out, std::string* error) {
    std::string bytes;
    if (!vfsReadAll(path, bytes)) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    return loadEquirectFromMemory(bytes.data(), bytes.size(), out, error);
}

Environment prefilterEnvironment(const Equirect& picture, int faceSize, int samples) {
    Environment env;
    if (picture.width <= 0 || picture.height <= 0 || picture.rgb.size() < (size_t)picture.width * picture.height * 3) return env;
    int size = 1;
    while (size < faceSize) size <<= 1;
    env.faceSize = size;
    const Pyramid pyr(picture);
    // How much of the sphere one pixel of the picture covers (at the equator); a sample covering more reads a
    // smaller level.
    const float texelP = (2.0f * kPi / picture.width) * (kPi / picture.height);

    for (int mip = 0, s = size; s >= 1; mip++, s >>= 1) {
        const float rough = std::min((float)mip / (float)(Environment::kRoughLevels - 1), 1.0f);
        const float a = rough * rough, a2 = a * a;   // GGX alpha, as fs_mesh.sc (a = rough^2)
        // The GGX lobe around +Z (the normal = the view = the reflection), the same for every texel of this level:
        // each sample's direction, weight and pyramid level.
        struct Tap {
            glm::vec3 l;
            float weight, lod;
        };
        std::vector<Tap> taps;
        const float texelCube = (kPi * 0.5f / (float)s) * (kPi * 0.5f / (float)s);
        if (mip == 0 || rough <= 0.0f) {
            taps.push_back({glm::vec3(0, 0, 1), 1.0f, std::max(0.5f * std::log2(texelCube / texelP), 0.0f)});
        } else {
            for (int i = 0; i < samples; i++) {
                const float u1 = (i + 0.5f) / (float)samples, u2 = radicalInverse((uint32_t)i);
                const float phi = 2.0f * kPi * u1;
                const float cosT = std::sqrt((1.0f - u2) / (1.0f + (a2 - 1.0f) * u2));
                const float sinT = std::sqrt(std::max(1.0f - cosT * cosT, 0.0f));
                const glm::vec3 hv(sinT * std::cos(phi), sinT * std::sin(phi), cosT);
                const glm::vec3 l = 2.0f * hv.z * hv - glm::vec3(0, 0, 1);
                if (l.z <= 0.0f) continue;
                // pdf of l = D(h) * NoH / (4 VoH) = D / 4 here (NoH = VoH); the solid angle the sample stands for.
                const float f = cosT * cosT * (a2 - 1.0f) + 1.0f;
                const float d = a2 / (kPi * f * f);
                const float omegaS = 1.0f / ((float)samples * d * 0.25f + 1e-6f);
                const float lod = std::max(0.5f * std::log2(omegaS / texelP) + 1.0f, 0.0f);
                taps.push_back({l, l.z, lod});
            }
        }
        for (int face = 0; face < 6; face++) {
            std::vector<float> px((size_t)s * s * 3);
            for (int y = 0; y < s; y++)
                for (int x = 0; x < s; x++) {
                    const glm::vec3 n = directionOf(face, 2.0f * (x + 0.5f) / s - 1.0f, 2.0f * (y + 0.5f) / s - 1.0f);
                    const glm::vec3 up = std::fabs(n.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    const glm::vec3 tx = glm::normalize(glm::cross(up, n)), ty = glm::cross(n, tx);
                    glm::vec3 sum(0.0f);
                    float wsum = 0.0f;
                    for (const Tap& t : taps) {
                        sum += pyr.sample(tx * t.l.x + ty * t.l.y + n * t.l.z, t.lod) * t.weight;
                        wsum += t.weight;
                    }
                    const glm::vec3 c = wsum > 0.0f ? sum / wsum : glm::vec3(0.0f);
                    float* o = &px[((size_t)y * s + x) * 3];
                    o[0] = c.r;
                    o[1] = c.g;
                    o[2] = c.b;
                }
            env.faces.push_back(std::move(px));
        }
    }

    // The diffuse light: project a small level of the picture on the 9 harmonics (each pixel weighted by the solid
    // angle it covers), then convolve with the cosine lobe and divide by pi (bands x 1, 2/3, 1/4).
    size_t li = 0;
    while (li + 1 < pyr.levels.size() && pyr.levels[li].w > 128) li++;
    const Pyramid::Level& L = pyr.levels[li];
    glm::vec3 sh[9] = {};
    for (int y = 0; y < L.h; y++) {
        const float theta = kPi * (y + 0.5f) / L.h;
        const float dOmega = (2.0f * kPi / L.w) * (kPi / L.h) * std::sin(theta);
        for (int x = 0; x < L.w; x++) {
            const float phi = 2.0f * kPi * ((x + 0.5f) / L.w - 0.5f);   // u = 0.5 + atan2(-x, z) / 2 pi
            const glm::vec3 d(-std::sin(theta) * std::sin(phi), std::cos(theta), std::sin(theta) * std::cos(phi));
            const float* p = &L.rgb[((size_t)y * L.w + x) * 3];
            float b[9];
            shBasis(d, b);
            for (int i = 0; i < 9; i++) sh[i] += glm::vec3(p[0], p[1], p[2]) * (b[i] * dOmega);
        }
    }
    const float band[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    for (int i = 0; i < 9; i++) env.sh[i] = sh[i] * band[i];
    return env;
}

}  // namespace toms::gltf
