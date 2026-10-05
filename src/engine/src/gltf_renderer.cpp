// gltf_renderer.cpp -- see gltf_renderer.h.
#include "gltf_renderer.h"

#include "embedded_shaders.h"

#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace toms::next {
namespace {

// One vertex layout for every primitive: what a file lacks gets a neutral value (white colour, no
// skin, zero tangent -- the shader then derives a tangent frame).
struct Vertex {
    float pos[3];
    float nrm[3];
    float tan[4];
    float uv0[2];
    float uv1[2];
    float color[4];
    float joints[4];
    float weights[4];
};

bgfx::VertexLayout meshLayout() {
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
        .end();
    return l;
}

bgfx::VertexLayout lineLayout() {
    bgfx::VertexLayout l;
    l.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
    return l;
}

void fillVertices(const gltf::Primitive& p, const std::vector<glm::vec3>& pos, const std::vector<glm::vec3>& nrm,
                  const std::vector<glm::vec4>& tan, std::vector<Vertex>& out) {
    out.resize(pos.size());
    for (size_t i = 0; i < pos.size(); i++) {
        Vertex& v = out[i];
        std::memset(&v, 0, sizeof v);
        std::memcpy(v.pos, &pos[i], 12);
        const glm::vec3 n = i < nrm.size() ? nrm[i] : glm::vec3(0, 0, 1);
        std::memcpy(v.nrm, &n, 12);
        if (i < tan.size()) std::memcpy(v.tan, &tan[i], 16);
        if (i < p.uv0.size()) std::memcpy(v.uv0, &p.uv0[i], 8);
        if (i < p.uv1.size()) std::memcpy(v.uv1, &p.uv1[i], 8);
        const glm::vec4 c = i < p.color.size() ? p.color[i] : glm::vec4(1.0f);
        std::memcpy(v.color, &c, 16);
        if (p.skinned()) {
            std::memcpy(v.joints, &p.joints[i], 16);
            glm::vec4 w = p.weights[i];
            const float sum = w.x + w.y + w.z + w.w;
            if (sum > 0) w /= sum;   // exporters do not always normalize
            std::memcpy(v.weights, &w, 16);
        }
    }
}

// RGBA8 with a box-filtered mip chain (bgfx does not make mips itself).
bgfx::TextureHandle makeTexture(const gltf::Image& img, const gltf::Texture& tex) {
    if (img.rgba.empty() || img.width <= 0 || img.height <= 0) return BGFX_INVALID_HANDLE;
    std::vector<uint8_t> all(img.rgba);
    int w = img.width, h = img.height, levels = 1;
    std::vector<uint8_t> prev = img.rgba;
    while (w > 1 || h > 1) {
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<uint8_t> next((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    int sum = 0, n = 0;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                            sum += prev[((size_t)sy * w + sx) * 4 + c];
                            n++;
                        }
                    next[((size_t)y * nw + x) * 4 + c] = uint8_t(sum / n);
                }
        all.insert(all.end(), next.begin(), next.end());
        prev.swap(next);
        w = nw;
        h = nh;
        levels++;
    }
    uint64_t flags = 0;
    if (tex.srgb) flags |= BGFX_TEXTURE_SRGB;
    if (!tex.repeatS) flags |= tex.mirrorS ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_U_CLAMP;
    if (!tex.repeatT) flags |= tex.mirrorT ? BGFX_SAMPLER_V_MIRROR : BGFX_SAMPLER_V_CLAMP;
    if (tex.nearest) flags |= BGFX_SAMPLER_MAG_POINT;
    else flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    return bgfx::createTexture2D((uint16_t)img.width, (uint16_t)img.height, levels > 1, 1, bgfx::TextureFormat::RGBA8, flags,
                                 bgfx::copy(all.data(), (uint32_t)all.size()));
}

bgfx::TextureHandle solidTexture(uint32_t rgba) {
    const uint8_t px[4] = {uint8_t(rgba >> 24), uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba)};
    return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(px, 4));
}

}  // namespace

// ---- GPU resources ------------------------------------------------------------------------------------

struct GltfRenderer::GpuModel {
    struct Prim {
        bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle dvb = BGFX_INVALID_HANDLE;   // morph targets: rewritten on change
        bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        std::vector<Vertex> vertices;            // morphed primitives only (the rest is uploaded once)
        std::vector<float> lastWeights;
        bool morph = false;
    };
    const gltf::Model* model = nullptr;
    std::vector<std::vector<Prim>> meshes;
    std::vector<bgfx::TextureHandle> textures;

    ~GpuModel() {
        for (auto& m : meshes)
            for (Prim& p : m) {
                if (bgfx::isValid(p.vb)) bgfx::destroy(p.vb);
                if (bgfx::isValid(p.dvb)) bgfx::destroy(p.dvb);
                if (bgfx::isValid(p.ib)) bgfx::destroy(p.ib);
            }
        for (bgfx::TextureHandle t : textures)
            if (bgfx::isValid(t)) bgfx::destroy(t);
    }
};

struct GltfRenderer::Impl {
    bgfx::ProgramHandle mesh = BGFX_INVALID_HANDLE, meshInst = BGFX_INVALID_HANDLE, line = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadow = BGFX_INVALID_HANDLE, shadowInst = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout, lineLay;
    bgfx::UniformHandle sBase, sMR, sNormal, sOcc, sEmissive, sShadow;
    bgfx::UniformHandle uBaseColor, uPbr, uEmissiveCutoff, uTexSet, uTexSet2, uUvTransform, uUvRotation;
    bgfx::UniformHandle uCamPos, uSky, uGround, uJoints, uMeshFlags, uMatParams;
    bgfx::UniformHandle uLightPos, uLightDirs, uLightColors, uLightSpot, uLightCount, uShadowMtx, uShadowRect;
    bgfx::TextureHandle white = BGFX_INVALID_HANDLE, flatNormal = BGFX_INVALID_HANDLE;
    bool ready = false;
    uint16_t base = 0, view = 0;             // first view id; the opaque view (base + kMaxShadowTiles)
    Frame frame;
    std::vector<glm::mat4> joints;

    // Shadows.
    bool shadowCaps = false;
    bgfx::TextureHandle atlas = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle atlasFb = BGFX_INVALID_HANDLE;
    int atlasSize = 0;
    bgfx::TextureHandle dummyShadow = BGFX_INVALID_HANDLE;   // bound when shadows are off
    int tiles = 0;                           // shadow tiles this frame (views base .. base + tiles - 1)
    glm::mat4 shadowMtx[kMaxShadowTiles];
    glm::vec4 shadowRect[kMaxShadowTiles];
    glm::vec4 lightPos[kMaxLights], lightDirs[kMaxLights], lightColors[kMaxLights], lightSpot[kMaxLights], lightCount;

    void releaseAtlas() {
        if (bgfx::isValid(atlasFb)) bgfx::destroy(atlasFb);   // destroys the texture too (it owns it)
        atlasFb = BGFX_INVALID_HANDLE;
        atlas = BGFX_INVALID_HANDLE;
        atlasSize = 0;
    }
    bool ensureAtlas(int size) {
        if (atlasSize == size && bgfx::isValid(atlasFb)) return true;
        releaseAtlas();
        const bgfx::Caps* caps = bgfx::getCaps();
        const bool d32 = (caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
        atlas = bgfx::createTexture2D((uint16_t)size, (uint16_t)size, false, 1, d32 ? bgfx::TextureFormat::D32F : bgfx::TextureFormat::D16,
                                      BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(atlas)) return false;
        atlasFb = bgfx::createFrameBuffer(1, &atlas, true);
        atlasSize = size;
        return bgfx::isValid(atlasFb);
    }
};

std::vector<GltfRenderer::Light> GltfRenderer::defaultLights() {
    Light key;   // upper left-front, with shadows
    key.direction = glm::normalize(glm::vec3(0.45f, -0.75f, -0.5f));
    key.color = glm::vec3(1.0f, 0.97f, 0.92f);
    key.intensity = 3.0f;
    Light fill = key;   // the other side, softer, no shadows
    fill.direction = glm::normalize(glm::vec3(-0.45f, -0.35f, 0.5f));
    fill.intensity = 0.75f;
    fill.castShadows = false;
    return {key, fill};
}

GltfRenderer::GltfRenderer() : impl_(new Impl) {}
GltfRenderer::~GltfRenderer() { shutdown(); }
bool GltfRenderer::ready() const { return impl_->ready; }
bool GltfRenderer::shadowsSupported() const { return impl_->shadowCaps; }

bool GltfRenderer::init() {
    Impl& r = *impl_;
    if (r.ready) return true;
    r.mesh = createEmbeddedProgram(ShaderProgram::Mesh);
    r.meshInst = createEmbeddedProgram(ShaderProgram::MeshInstanced);
    r.line = createEmbeddedProgram(ShaderProgram::Line);
    r.shadow = createEmbeddedProgram(ShaderProgram::MeshShadow);
    r.shadowInst = createEmbeddedProgram(ShaderProgram::MeshShadowInstanced);
    if (!bgfx::isValid(r.mesh) || !bgfx::isValid(r.line)) {
        std::fprintf(stderr, "[gltf] no mesh shaders for this backend\n");
        return false;
    }
    r.layout = meshLayout();
    r.lineLay = lineLayout();
    r.sBase = bgfx::createUniform("s_baseColor", bgfx::UniformType::Sampler);
    r.sMR = bgfx::createUniform("s_metalRough", bgfx::UniformType::Sampler);
    r.sNormal = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler);
    r.sOcc = bgfx::createUniform("s_occlusion", bgfx::UniformType::Sampler);
    r.sEmissive = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    r.sShadow = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    r.uBaseColor = bgfx::createUniform("u_baseColor", bgfx::UniformType::Vec4);
    r.uPbr = bgfx::createUniform("u_pbr", bgfx::UniformType::Vec4);
    r.uEmissiveCutoff = bgfx::createUniform("u_emissiveCutoff", bgfx::UniformType::Vec4);
    r.uTexSet = bgfx::createUniform("u_texSet", bgfx::UniformType::Vec4);
    r.uTexSet2 = bgfx::createUniform("u_texSet2", bgfx::UniformType::Vec4);
    r.uUvTransform = bgfx::createUniform("u_uvTransform", bgfx::UniformType::Vec4);
    r.uUvRotation = bgfx::createUniform("u_uvRotation", bgfx::UniformType::Vec4);
    r.uCamPos = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
    r.uSky = bgfx::createUniform("u_skyColor", bgfx::UniformType::Vec4);
    r.uGround = bgfx::createUniform("u_groundColor", bgfx::UniformType::Vec4);
    r.uJoints = bgfx::createUniform("u_joints", bgfx::UniformType::Mat4, gltf::kMaxJoints);
    r.uMeshFlags = bgfx::createUniform("u_meshFlags", bgfx::UniformType::Vec4);
    r.uMatParams = bgfx::createUniform("u_matParams", bgfx::UniformType::Vec4);
    r.uLightPos = bgfx::createUniform("u_lightPos", bgfx::UniformType::Vec4, kMaxLights);
    r.uLightDirs = bgfx::createUniform("u_lightDirs", bgfx::UniformType::Vec4, kMaxLights);
    r.uLightColors = bgfx::createUniform("u_lightColors", bgfx::UniformType::Vec4, kMaxLights);
    r.uLightSpot = bgfx::createUniform("u_lightSpot", bgfx::UniformType::Vec4, kMaxLights);
    r.uLightCount = bgfx::createUniform("u_lightCount", bgfx::UniformType::Vec4);
    r.uShadowMtx = bgfx::createUniform("u_shadowMtx", bgfx::UniformType::Mat4, kMaxShadowTiles);
    r.uShadowRect = bgfx::createUniform("u_shadowRect", bgfx::UniformType::Vec4, kMaxShadowTiles);
    r.white = solidTexture(0xffffffff);
    r.flatNormal = solidTexture(0x8080ffff);
    r.joints.assign(gltf::kMaxJoints, glm::mat4(1.0f));
    const bgfx::Caps* caps = bgfx::getCaps();
    // Depth compare sampling is in every bgfx backend; what can differ is a depth format that can be
    // both rendered to and sampled.
    r.shadowCaps = bgfx::isValid(r.shadow) && (caps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0 &&
                   (caps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;
    if (!r.shadowCaps) std::fprintf(stderr, "[gltf] no shadow maps on this backend (no renderable + sampleable depth format)\n");
    // A 1x1 depth texture so the sampler always has something of the right kind bound.
    r.dummyShadow = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL);
    for (int i = 0; i < kMaxShadowTiles; i++) {
        r.shadowMtx[i] = glm::mat4(1.0f);
        r.shadowRect[i] = glm::vec4(0.0f);
    }
    r.ready = true;
    return true;
}

void GltfRenderer::shutdown() {
    Impl& r = *impl_;
    if (!r.ready) return;
    r.releaseAtlas();
    for (bgfx::ProgramHandle p : {r.mesh, r.meshInst, r.line, r.shadow, r.shadowInst})
        if (bgfx::isValid(p)) bgfx::destroy(p);
    for (bgfx::UniformHandle u : {r.sBase, r.sMR, r.sNormal, r.sOcc, r.sEmissive, r.sShadow, r.uBaseColor, r.uPbr, r.uEmissiveCutoff,
                                  r.uTexSet, r.uTexSet2, r.uUvTransform, r.uUvRotation, r.uCamPos, r.uSky, r.uGround, r.uJoints,
                                  r.uMeshFlags, r.uMatParams, r.uLightPos, r.uLightDirs, r.uLightColors, r.uLightSpot, r.uLightCount,
                                  r.uShadowMtx, r.uShadowRect})
        bgfx::destroy(u);
    bgfx::destroy(r.white);
    bgfx::destroy(r.flatNormal);
    if (bgfx::isValid(r.dummyShadow)) bgfx::destroy(r.dummyShadow);
    r.ready = false;
}

std::shared_ptr<GltfRenderer::GpuModel> GltfRenderer::upload(const gltf::Model& model) {
    auto gpu = std::make_shared<GpuModel>();
    gpu->model = &model;
    for (size_t i = 0; i < model.textures.size(); i++) {
        const gltf::Texture& t = model.textures[i];
        gpu->textures.push_back(t.image >= 0 && t.image < (int)model.images.size() ? makeTexture(model.images[(size_t)t.image], t)
                                                                                   : bgfx::TextureHandle BGFX_INVALID_HANDLE);
    }
    gpu->meshes.resize(model.meshes.size());
    for (size_t m = 0; m < model.meshes.size(); m++)
        for (const gltf::Primitive& p : model.meshes[m].primitives) {
            GpuModel::Prim g;
            std::vector<Vertex> verts;
            fillVertices(p, p.position, p.normal, p.tangent, verts);
            g.morph = !p.targets.empty();
            const bgfx::Memory* mem = bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(Vertex)));
            if (g.morph) {
                g.dvb = bgfx::createDynamicVertexBuffer(mem, impl_->layout);
                g.vertices = std::move(verts);
            } else {
                g.vb = bgfx::createVertexBuffer(mem, impl_->layout);
            }
            const bool big = p.position.size() > 65535;
            if (big) {
                g.ib = bgfx::createIndexBuffer(bgfx::copy(p.indices.data(), uint32_t(p.indices.size() * 4)), BGFX_BUFFER_INDEX32);
            } else {
                std::vector<uint16_t> i16(p.indices.begin(), p.indices.end());
                g.ib = bgfx::createIndexBuffer(bgfx::copy(i16.data(), uint32_t(i16.size() * 2)));
            }
            g.indexCount = (uint32_t)p.indices.size();
            gpu->meshes[m].push_back(std::move(g));
        }
    return gpu;
}

glm::mat4 GltfRenderer::projection(float fovYDegrees, float aspect, float zNear, float zFar) {
    const bool gl = bgfx::getCaps() && bgfx::getCaps()->homogeneousDepth;
    return gl ? glm::perspectiveRH_NO(glm::radians(fovYDegrees), aspect, zNear, zFar)
              : glm::perspectiveRH_ZO(glm::radians(fovYDegrees), aspect, zNear, zFar);
}

namespace {

glm::mat4 orthoFor(float r, float n, float f) {
    const bool gl = bgfx::getCaps()->homogeneousDepth;
    return gl ? glm::orthoRH_NO(-r, r, -r, r, n, f) : glm::orthoRH_ZO(-r, r, -r, r, n, f);
}

glm::mat4 lookAlong(const glm::vec3& eye, const glm::vec3& dir) {
    const glm::vec3 d = glm::normalize(dir);
    const glm::vec3 up = std::fabs(d.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    return glm::lookAt(eye, eye + d, up);
}

}  // namespace

void GltfRenderer::begin(uint16_t viewId, const Frame& frame, uint32_t width, uint32_t height, uint32_t clearRgba) {
    Impl& r = *impl_;
    r.base = viewId;
    r.view = uint16_t(viewId + kMaxShadowTiles);
    r.frame = frame;
    stats_ = Stats();

    // ---- lights and their shadow tiles ----
    const bgfx::Caps* caps = bgfx::getCaps();
    const glm::vec3 c = (frame.boundsMin + frame.boundsMax) * 0.5f;
    const float radius = std::max(glm::length(frame.boundsMax - frame.boundsMin) * 0.5f, 1e-3f);
    struct TileJob {
        glm::mat4 view, proj;
    };
    std::vector<TileJob> jobs;
    const int count = std::min((int)frame.lights.size(), kMaxLights);
    const bool shadows = frame.shadows && r.shadowCaps;
    // Tile size first: how many tiles the casting lights need.
    int wanted = 0;
    for (int i = 0; i < count; i++)
        if (shadows && frame.lights[(size_t)i].castShadows) wanted += frame.lights[(size_t)i].type == Light::Type::Point ? 6 : 1;
    wanted = std::min(wanted, kMaxShadowTiles);
    const int grid = wanted > 0 ? (int)std::ceil(std::sqrt((double)wanted)) : 1;
    const int size = std::clamp(frame.shadowMapSize, 256, 8192);
    const int tilePx = size / grid;
    const float texelUv = 1.0f / (float)size;

    for (int i = 0; i < count; i++) {
        const Light& L = frame.lights[(size_t)i];
        const glm::vec3 dir = glm::length(L.direction) > 0 ? glm::normalize(L.direction) : glm::vec3(0, -1, 0);
        const float type = L.type == Light::Type::Directional ? 0.0f : L.type == Light::Type::Point ? 1.0f : 2.0f;
        r.lightPos[i] = glm::vec4(L.position, type);
        r.lightDirs[i] = glm::vec4(dir, std::max(L.range, 0.0f));
        const float cosOuter = std::cos(std::clamp(L.outerCone, 0.001f, 1.57f));
        const float cosInner = std::cos(std::clamp(std::min(L.innerCone, L.outerCone), 0.0f, 1.57f));
        float bias = 0.0f, normalOffset = 0.0f;
        int first = -1;
        const int need = L.type == Light::Type::Point ? 6 : 1;
        if (shadows && L.castShadows && (int)jobs.size() + need <= kMaxShadowTiles) {
            first = (int)jobs.size();
            // How far the light reaches: its range, else the far side of the bounds.
            const float reach = L.range > 0 ? L.range : glm::length(L.position - c) + radius;
            if (L.type == Light::Type::Directional) {
                const glm::mat4 v = lookAlong(c - dir * radius * 2.0f, dir);
                jobs.push_back({v, orthoFor(radius, 0.0f, radius * 4.0f)});
                normalOffset = 2.0f * radius / (float)tilePx * 2.5f;   // world size of a texel, x 2.5
                bias = 0.001f;   // orthographic: depth is linear, 0.1% of the 4 r depth range
            } else {
                const float fovY = L.type == Light::Type::Spot ? std::min(2.0f * std::clamp(L.outerCone, 0.01f, 1.5f) + 0.1f, 3.0f)
                                                               : glm::radians(91.0f);   // a cube face (a hair wider: no seams)
                const float zf = std::max(reach, 0.01f), zn = std::max(zf * 0.01f, 0.01f);   // near: depth precision
                const glm::mat4 proj = projection(glm::degrees(fovY), 1.0f, zn, zf);
                if (L.type == Light::Type::Spot) {
                    jobs.push_back({lookAlong(L.position, dir), proj});
                } else {   // faces in the shader's order: +X -X +Y -Y +Z -Z
                    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (const glm::vec3& a : axes) jobs.push_back({lookAlong(L.position, a), proj});
                }
                normalOffset = 2.0f * std::tan(fovY * 0.5f) / (float)tilePx * 2.0f;   // x distance in the shader
                bias = 0.0f;   // perspective: the shader steps towards the light instead (in world units)
            }
        }
        r.lightColors[i] = glm::vec4(L.color * L.intensity, (float)first);
        r.lightSpot[i] = glm::vec4(cosOuter, 1.0f / std::max(cosInner - cosOuter, 1e-4f), bias * frame.shadowBias,
                                   normalOffset * frame.shadowBias);
    }
    r.tiles = (int)jobs.size();
    r.lightCount = glm::vec4((float)count, frame.ambient, texelUv, r.tiles > 0 ? 1.0f : 0.0f);
    stats_.shadowTiles = r.tiles;

    // The shadow views: one per tile, into its square of the atlas.
    if (r.tiles > 0 && r.ensureAtlas(size)) {
        const float sy = caps->originBottomLeft ? 0.5f : -0.5f;
        const float sz = caps->homogeneousDepth ? 0.5f : 1.0f, tz = caps->homogeneousDepth ? 0.5f : 0.0f;
        glm::mat4 crop(1.0f);   // clip -> texture (uv 0..1, depth 0..1)
        crop[0][0] = 0.5f;
        crop[1][1] = sy;
        crop[2][2] = sz;
        crop[3] = glm::vec4(0.5f, 0.5f, tz, 1.0f);
        const float s = (float)tilePx / (float)size;
        for (int k = 0; k < r.tiles; k++) {
            const int tx = (k % grid) * tilePx, ty = (k / grid) * tilePx;   // pixels from the top-left
            const uint16_t v = uint16_t(r.base + k);
            bgfx::setViewFrameBuffer(v, r.atlasFb);
            bgfx::setViewRect(v, (uint16_t)tx, (uint16_t)ty, (uint16_t)tilePx, (uint16_t)tilePx);
            bgfx::setViewClear(v, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(v, glm::value_ptr(jobs[(size_t)k].view), glm::value_ptr(jobs[(size_t)k].proj));
            bgfx::touch(v);
            const float u0 = (float)tx / (float)size;
            const float v0 = caps->originBottomLeft ? 1.0f - (float)(ty + tilePx) / (float)size : (float)ty / (float)size;
            glm::mat4 tile(1.0f);
            tile[0][0] = s;
            tile[1][1] = s;
            tile[3] = glm::vec4(u0, v0, 0.0f, 1.0f);
            r.shadowMtx[k] = tile * crop * jobs[(size_t)k].proj * jobs[(size_t)k].view;
            r.shadowRect[k] = glm::vec4(u0, v0, u0 + s, v0 + s);
        }
    } else {
        r.tiles = 0;
        r.lightCount.w = 0.0f;
    }

    // ---- the colour views ----
    bgfx::setViewClear(r.view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearRgba, 1.0f, 0);
    bgfx::setViewClear(uint16_t(r.view + 1), BGFX_CLEAR_NONE);
    for (uint16_t v : {r.view, uint16_t(r.view + 1)}) {
        bgfx::setViewFrameBuffer(v, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(v, 0, 0, (uint16_t)width, (uint16_t)height);
        bgfx::setViewTransform(v, glm::value_ptr(frame.view), glm::value_ptr(frame.proj));
    }
    bgfx::setViewMode(uint16_t(r.view + 1), bgfx::ViewMode::DepthDescending);   // blended: far to near
    bgfx::touch(r.view);
}

void GltfRenderer::draw(GpuModel& gpu, const gltf::Pose& pose, const glm::mat4& placement, bool castShadows) {
    Impl& r = *impl_;
    if (!r.ready || !gpu.model || pose.worlds().size() != gpu.model->nodes.size()) return;
    const gltf::Model& model = *gpu.model;
    const Frame& f = r.frame;
    static const gltf::Material kDefault = [] {
        gltf::Material m;
        m.metallic = 0.0f;
        m.roughness = 0.6f;
        return m;
    }();

    if (model.scenes.empty()) return;
    const gltf::Scene& scene = model.scenes[(size_t)std::clamp(model.scene, 0, (int)model.scenes.size() - 1)];
    std::vector<int> stack(scene.roots.rbegin(), scene.roots.rend());
    while (!stack.empty()) {
        const int ni = stack.back();
        stack.pop_back();
        const gltf::Node& node = model.nodes[(size_t)ni];
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) stack.push_back(*it);
        if (node.mesh < 0 || node.mesh >= (int)gpu.meshes.size()) continue;
        const gltf::Mesh& mesh = model.meshes[(size_t)node.mesh];
        const glm::mat4 world = placement * pose.world(ni);
        const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0f;
        const bool skinNode = node.skin >= 0;
        if (skinNode) {
            pose.jointMatrices(node.skin, r.joints);
            if (r.joints.empty()) r.joints.assign(1, glm::mat4(1.0f));
        }
        for (size_t pi = 0; pi < mesh.primitives.size() && pi < gpu.meshes[(size_t)node.mesh].size(); pi++) {
            const gltf::Primitive& p = mesh.primitives[pi];
            GpuModel::Prim& g = gpu.meshes[(size_t)node.mesh][pi];
            const gltf::Material& mat = p.material >= 0 && p.material < (int)model.materials.size() ? model.materials[(size_t)p.material] : kDefault;

            // Morph targets: CPU morph when this node's weights differ from the last upload.
            if (g.morph) {
                const std::vector<float>& w = pose.weights(ni);
                if (w != g.lastWeights) {
                    std::vector<glm::vec3> pos, nrm;
                    std::vector<glm::vec4> tan;
                    gltf::morph(p, w, pos, nrm, tan);
                    fillVertices(p, pos, nrm, tan, g.vertices);
                    bgfx::update(g.dvb, 0, bgfx::copy(g.vertices.data(), uint32_t(g.vertices.size() * sizeof(Vertex))));
                    g.lastWeights = w;
                    stats_.morphUploads++;
                }
            }

            // Placement: skinned vertices come out of the joints in model space; instances multiply in.
            const bool skinned = skinNode && p.skinned();
            const glm::mat4 base = skinned ? placement : world;
            const size_t instances = node.instances.size();
            bgfx::InstanceDataBuffer idb{};
            bool instanced = false;
            if (instances > 0 && bgfx::isValid(r.meshInst)) {
                if (bgfx::getAvailInstanceDataBuffer((uint32_t)instances, 64) < instances) continue;
                bgfx::allocInstanceDataBuffer(&idb, (uint32_t)instances, 64);
                glm::mat4* out = (glm::mat4*)idb.data;
                for (size_t k = 0; k < instances; k++) out[k] = base * node.instances[k];
                instanced = true;
                stats_.instances += (int)instances;
            }
            const glm::vec4 meshFlags(skinned ? 1.0f : 0.0f, 0, 0, 0);
            const uint16_t jointCount = (uint16_t)std::min<size_t>(r.joints.size(), gltf::kMaxJoints);
            auto geometry = [&] {   // what the colour and the shadow draws share
                if (instanced) bgfx::setInstanceDataBuffer(&idb);
                else bgfx::setTransform(glm::value_ptr(base));
                if (skinned) bgfx::setUniform(r.uJoints, r.joints.data(), jointCount);
                bgfx::setUniform(r.uMeshFlags, &meshFlags);
                if (bgfx::isValid(g.dvb)) bgfx::setVertexBuffer(0, g.dvb);
                else bgfx::setVertexBuffer(0, g.vb);
                bgfx::setIndexBuffer(g.ib, 0, g.indexCount);
            };
            const bool glass = mat.transmission > 0.0f;
            const bool blend = glass || mat.alphaMode == gltf::AlphaMode::Blend;

            // Shadow casters: opaque and masked triangles, into every tile.
            if (castShadows && r.tiles > 0 && !blend && p.topology == gltf::Topology::Triangles) {
                const bgfx::ProgramHandle sp = instanced ? r.shadowInst : r.shadow;
                for (int k = 0; k < r.tiles; k++) {
                    geometry();
                    bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);   // both faces: thin parts cast too
                    bgfx::submit(uint16_t(r.base + k), sp);
                    stats_.shadowDraws++;
                }
            }

            geometry();
            if (skinned) stats_.skinnedDraws++;

            // Material.
            auto slot = [&](const gltf::TextureRef& ref, uint8_t stage, bgfx::UniformHandle sampler, bgfx::TextureHandle fallback) {
                bgfx::TextureHandle t = ref.valid() && ref.texture < (int)gpu.textures.size() ? gpu.textures[(size_t)ref.texture]
                                                                                             : bgfx::TextureHandle BGFX_INVALID_HANDLE;
                const bool use = bgfx::isValid(t);
                bgfx::setTexture(stage, sampler, use ? t : fallback);
                return use ? float(ref.uvSet + 1) : 0.0f;
            };
            const glm::vec4 texSet(slot(mat.baseColorTex, 0, r.sBase, r.white), slot(mat.metalRoughTex, 1, r.sMR, r.white),
                                   slot(mat.normalTex, 2, r.sNormal, r.flatNormal), slot(mat.occlusionTex, 3, r.sOcc, r.white));
            const glm::vec4 texSet2(slot(mat.emissiveTex, 4, r.sEmissive, r.white), mat.unlit ? 1.0f : 0.0f,
                                    p.tangent.empty() ? 0.0f : 1.0f, float(f.debug));
            bgfx::setTexture(5, r.sShadow, r.tiles > 0 ? r.atlas : r.dummyShadow);
            const gltf::TextureRef& xf = mat.baseColorTex.valid() ? mat.baseColorTex : mat.normalTex;
            const glm::vec4 uvT(xf.offset, xf.scale), uvR(xf.rotation, 0, 0, 0);
            const glm::vec4 pbr(mat.metallic, mat.roughness, mat.normalScale, mat.occlusionStrength);
            const glm::vec4 emc(mat.emissive, mat.alphaMode == gltf::AlphaMode::Mask ? mat.alphaCutoff : -1.0f);
            bgfx::setUniform(r.uBaseColor, &mat.baseColor);
            bgfx::setUniform(r.uPbr, &pbr);
            bgfx::setUniform(r.uEmissiveCutoff, &emc);
            bgfx::setUniform(r.uTexSet, &texSet);
            bgfx::setUniform(r.uTexSet2, &texSet2);
            bgfx::setUniform(r.uUvTransform, &uvT);
            bgfx::setUniform(r.uUvRotation, &uvR);
            const glm::vec4 matParams(mat.doubleSided ? 1.0f : 0.0f, mat.transmission, 0.0f, 0.0f);
            bgfx::setUniform(r.uMatParams, &matParams);
            const glm::vec4 cam(f.eye, f.exposure);
            const glm::vec4 sky(f.sky, f.srgbOut ? 1.0f : 0.0f), ground(f.ground, f.tonemap ? 1.0f : 0.0f);
            bgfx::setUniform(r.uCamPos, &cam);
            bgfx::setUniform(r.uSky, &sky);
            bgfx::setUniform(r.uGround, &ground);
            bgfx::setUniform(r.uLightPos, r.lightPos, kMaxLights);
            bgfx::setUniform(r.uLightDirs, r.lightDirs, kMaxLights);
            bgfx::setUniform(r.uLightColors, r.lightColors, kMaxLights);
            bgfx::setUniform(r.uLightSpot, r.lightSpot, kMaxLights);
            bgfx::setUniform(r.uLightCount, &r.lightCount);
            bgfx::setUniform(r.uShadowMtx, r.shadowMtx, kMaxShadowTiles);
            bgfx::setUniform(r.uShadowRect, r.shadowRect, kMaxShadowTiles);

            // State. glTF front faces are counter-clockwise: cull the clockwise ones (the other way
            // round under a mirroring transform).
            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
            if (!mat.doubleSided) state |= mirrored ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
            if (p.topology == gltf::Topology::Lines) state |= BGFX_STATE_PT_LINES;
            else if (p.topology == gltf::Topology::Points) state |= BGFX_STATE_PT_POINTS;
            // Glass (transmission): premultiplied, so its reflections stay bright over what is behind.
            if (glass) state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            else if (blend) state |= BGFX_STATE_BLEND_ALPHA;
            else state |= BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z;
            bgfx::setState(state);

            const bgfx::ProgramHandle prog = instanced ? r.meshInst : r.mesh;
            if (blend) {   // the second view sorts these far to near
                const glm::vec3 cc = glm::vec3(base * glm::vec4((p.boundsMin + p.boundsMax) * 0.5f, 1.0f));
                const float dist = glm::length(cc - f.eye);
                bgfx::submit(uint16_t(r.view + 1), prog, (uint32_t)std::min(dist * 1000.0f, 4.0e9f));
            } else {
                bgfx::submit(r.view, prog);
            }
            stats_.drawCalls++;
        }
    }
}

void GltfRenderer::lines(const std::vector<LinePoint>& points, bool depthTest) {
    Impl& r = *impl_;
    if (!r.ready || points.size() < 2) return;
    const uint32_t n = (uint32_t)(points.size() & ~size_t(1));
    if (bgfx::getAvailTransientVertexBuffer(n, r.lineLay) < n) return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, n, r.lineLay);
    std::memcpy(tvb.data, points.data(), n * sizeof(LinePoint));
    bgfx::setVertexBuffer(0, &tvb);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_PT_LINES | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_LINEAA;
    if (depthTest) state |= BGFX_STATE_DEPTH_TEST_LESS;
    bgfx::setState(state);
    bgfx::submit(uint16_t(r.view + 1), r.line, 0xffffffffu);   // with the blended draws, after the opaque ones
    stats_.drawCalls++;
}

}  // namespace toms::next
