// gltf_anim_texture.cpp -- see gltf_anim_texture.h.
#include "gltf_anim_texture.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace toms::gltf {

namespace {

constexpr char kMagic[8] = {'T', 'O', 'M', 'S', 'A', 'N', 'I', 'M'};
constexpr uint32_t kVersion = 1;

uint64_t fnv(uint64_t h, const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// The rows of one clip a time falls between, and how far (the last segment may be shorter than 1 / fps).
FramePair pairFor(const AnimClip& c, float fps, float t, bool loop) {
    FramePair out;
    out.row0 = out.row1 = (float)c.firstRow;
    if (c.frames <= 1 || c.duration <= 0.0f) return out;
    if (loop) {
        t = std::fmod(t, c.duration);
        if (t < 0.0f) t += c.duration;
    } else {
        t = std::clamp(t, 0.0f, c.duration);
    }
    const float step = 1.0f / fps;
    int i0 = std::min((int)std::floor(t * fps), c.frames - 1);
    const int i1 = std::min(i0 + 1, c.frames - 1);
    const float t0 = (float)i0 * step, t1 = std::min((float)i1 * step, c.duration);
    out.row0 = (float)(c.firstRow + i0);
    out.row1 = (float)(c.firstRow + i1);
    out.blend = t1 > t0 ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
    return out;
}

glm::vec4 texel(const AnimTexture& tex, int row, int x) {
    row = std::clamp(row, 0, std::max(0, tex.rows - 1));
    const size_t i = ((size_t)row * tex.width + x) * 4;
    if (i + 3 >= tex.texels.size()) return glm::vec4(0, 0, 0, 1);
    return glm::vec4(tex.texels[i], tex.texels[i + 1], tex.texels[i + 2], tex.texels[i + 3]);
}

}  // namespace

int AnimTexture::clipIndex(const std::string& name) const {
    for (size_t i = 0; i < clips.size(); i++)
        if (clips[i].name == name) return (int)i;
    return -1;
}

uint64_t modelHash(const Model& model, int skin) {
    uint64_t h = 1469598103934665603ull;
    if (skin < 0 || skin >= (int)model.skins.size()) return h;
    const Skin& s = model.skins[(size_t)skin];
    for (size_t j = 0; j < s.joints.size(); j++) {
        const int n = s.joints[j];
        if (n >= 0 && n < (int)model.nodes.size()) h = fnv(h, model.nodes[(size_t)n].name.data(), model.nodes[(size_t)n].name.size());
        if (j < s.inverseBind.size()) h = fnv(h, &s.inverseBind[j], sizeof(glm::mat4));
    }
    for (const Animation& a : model.animations) {
        h = fnv(h, a.name.data(), a.name.size());
        h = fnv(h, &a.duration, sizeof a.duration);
        const size_t ch = a.channels.size();
        h = fnv(h, &ch, sizeof ch);
    }
    return h;
}

bool bakeAnimTexture(const Model& model, int skin, float fps, AnimTexture& out, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    if (skin < 0 || skin >= (int)model.skins.size()) return fail("no skin " + std::to_string(skin));
    const Skin& s = model.skins[(size_t)skin];
    if (s.joints.empty()) return fail("the skin has no joints");
    if ((int)s.joints.size() > kMaxJoints) return fail("the skin has " + std::to_string(s.joints.size()) + " joints (at most " +
                                                       std::to_string(kMaxJoints) + ")");
    fps = std::clamp(fps, 1.0f, 240.0f);
    out = AnimTexture();
    out.skin = skin;
    out.joints = (int)s.joints.size();
    out.fps = fps;
    out.width = out.joints * 2;
    out.sourceHash = modelHash(model, skin);

    Pose pose;
    pose.bind(&model);
    std::vector<glm::mat4> mats;
    std::vector<glm::quat> previous((size_t)out.joints);
    auto bakeFrame = [&](int anim, float t, bool first) {
        pose.reset();
        if (anim >= 0) pose.apply(anim, t);
        pose.updateWorld();
        pose.jointMatrices(skin, mats);
        for (int j = 0; j < out.joints; j++) {
            const glm::mat4& m = mats[(size_t)j];
            const glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
            const float sx = glm::length(c0), sy = glm::length(c1), sz = glm::length(c2);
            const float su = (sx + sy + sz) / 3.0f;
            if (su > 1e-8f)
                out.maxScaleError = std::max(out.maxScaleError, std::max({std::fabs(sx - su), std::fabs(sy - su), std::fabs(sz - su)}) / su);
            glm::mat3 r(sx > 1e-8f ? c0 / sx : c0, sy > 1e-8f ? c1 / sy : c1, sz > 1e-8f ? c2 / sz : c2);
            if (glm::determinant(r) < 0.0f) out.mirroredJoints = true;
            glm::quat q = glm::normalize(glm::quat_cast(r));
            // Keep each joint's quaternion on the same side as its previous frame, so two frames always blend
            // the short way round (the shader also aligns signs, this keeps neighbouring frames consistent).
            if (!first && glm::dot(q, previous[(size_t)j]) < 0.0f) q = -q;
            previous[(size_t)j] = q;
            out.texels.insert(out.texels.end(), {q.x, q.y, q.z, q.w, m[3].x, m[3].y, m[3].z, su});
        }
        out.rows++;
    };

    for (size_t a = 0; a < model.animations.size(); a++) {
        AnimClip c;
        c.name = model.animations[a].name.empty() ? "animation_" + std::to_string(a) : model.animations[a].name;
        c.animation = (int)a;
        c.firstRow = out.rows;
        c.duration = std::max(0.0f, model.animations[a].duration);
        c.frames = c.duration > 0.0f ? (int)std::ceil(c.duration * fps - 1e-4f) + 1 : 1;
        for (int f = 0; f < c.frames; f++) bakeFrame((int)a, std::min((float)f / fps, c.duration), f == 0);
        out.clips.push_back(c);
    }
    if (out.clips.empty()) {   // no animation: one row, the rest pose, so the model still draws
        AnimClip c;
        c.name = "rest";
        c.firstRow = 0;
        out.clips.push_back(c);
        bakeFrame(-1, 0.0f, true);
    }
    return true;
}

bool saveAnimTexture(const AnimTexture& tex, const std::string& path, std::string* error) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    auto put = [&](const void* p, size_t n) { f.write(static_cast<const char*>(p), (std::streamsize)n); };
    auto put32 = [&](uint32_t v) { put(&v, 4); };
    put(kMagic, 8);
    put32(kVersion);
    put(&tex.sourceHash, 8);
    put32((uint32_t)tex.skin);
    put32((uint32_t)tex.joints);
    put(&tex.fps, 4);
    put32((uint32_t)tex.width);
    put32((uint32_t)tex.rows);
    put(&tex.maxScaleError, 4);
    put32(tex.mirroredJoints ? 1u : 0u);
    put32((uint32_t)tex.clips.size());
    for (const AnimClip& c : tex.clips) {
        put32((uint32_t)c.name.size());
        put(c.name.data(), c.name.size());
        put32((uint32_t)c.animation);
        put32((uint32_t)c.firstRow);
        put32((uint32_t)c.frames);
        put(&c.duration, 4);
    }
    put(tex.texels.data(), tex.bytes());
    if (!f) { if (error) *error = "write failed: " + path; return false; }
    return true;
}

bool loadAnimTexture(const std::string& path, AnimTexture& out, const Model* checkAgainst, int skin, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = path + ": " + e; return false; };
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("cannot open");
    auto get = [&](void* p, size_t n) { f.read(static_cast<char*>(p), (std::streamsize)n); return (bool)f; };
    auto get32 = [&](uint32_t& v) { return get(&v, 4); };
    char magic[8];
    uint32_t version = 0, u = 0;
    if (!get(magic, 8) || std::memcmp(magic, kMagic, 8) != 0) return fail("not an animation texture");
    if (!get32(version) || version != kVersion) return fail("version " + std::to_string(version) + " (expected " + std::to_string(kVersion) + ")");
    AnimTexture t;
    if (!get(&t.sourceHash, 8)) return fail("truncated");
    get32(u); t.skin = (int)u;
    get32(u); t.joints = (int)u;
    get(&t.fps, 4);
    get32(u); t.width = (int)u;
    get32(u); t.rows = (int)u;
    get(&t.maxScaleError, 4);
    get32(u); t.mirroredJoints = u != 0;
    uint32_t clips = 0;
    if (!get32(clips) || clips > 100000 || t.joints <= 0 || t.joints > kMaxJoints || t.width != t.joints * 2 || t.rows <= 0)
        return fail("bad header");
    for (uint32_t i = 0; i < clips; i++) {
        AnimClip c;
        uint32_t len = 0;
        if (!get32(len) || len > 4096) return fail("bad clip table");
        c.name.resize(len);
        if (len) get(c.name.data(), len);
        get32(u); c.animation = (int)u;
        get32(u); c.firstRow = (int)u;
        get32(u); c.frames = (int)u;
        if (!get(&c.duration, 4)) return fail("truncated clip table");
        if (c.firstRow < 0 || c.frames < 1 || c.firstRow + c.frames > t.rows) return fail("clip '" + c.name + "' is outside the texture");
        t.clips.push_back(c);
    }
    t.texels.resize((size_t)t.rows * t.width * 4);
    if (!get(t.texels.data(), t.bytes())) return fail("truncated texels");
    if (checkAgainst && t.sourceHash != modelHash(*checkAgainst, skin >= 0 ? skin : t.skin))
        return fail("stale: the model or its animations changed since it was baked (bake it again)");
    out = std::move(t);
    return true;
}

FramePair framePair(const AnimTexture& tex, int clip, float t, bool loop) {
    if (clip < 0 || clip >= (int)tex.clips.size()) return FramePair{};
    return pairFor(tex.clips[(size_t)clip], tex.fps, t, loop);
}

glm::mat4 sampleJoint(const AnimTexture& tex, int joint, const FramePair& a, const FramePair* b, float w) {
    // The same steps as vs_mesh_animtex.sc: nlerp the rotations (signs aligned), lerp translation and scale.
    auto clipPose = [&](const FramePair& p, glm::vec4& q, glm::vec4& ts) {
        const glm::vec4 q0 = texel(tex, (int)p.row0, joint * 2), q1 = texel(tex, (int)p.row1, joint * 2);
        q = q0 * (1.0f - p.blend) + q1 * (glm::dot(q0, q1) < 0.0f ? -p.blend : p.blend);
        ts = glm::mix(texel(tex, (int)p.row0, joint * 2 + 1), texel(tex, (int)p.row1, joint * 2 + 1), p.blend);
    };
    glm::vec4 q, ts;
    clipPose(a, q, ts);
    if (b && w > 0.0f) {
        glm::vec4 qb, tsb;
        clipPose(*b, qb, tsb);
        q = q * (1.0f - w) + qb * (glm::dot(q, qb) < 0.0f ? -w : w);
        ts = glm::mix(ts, tsb, w);
    }
    const float len = glm::length(q);
    q = len > 1e-8f ? q / len : glm::vec4(0, 0, 0, 1);
    const glm::mat3 r = glm::mat3_cast(glm::quat(q.w, q.x, q.y, q.z)) * ts.w;
    glm::mat4 m(r);
    m[3] = glm::vec4(ts.x, ts.y, ts.z, 1.0f);
    return m;
}

// ---- Crowd ----

int Crowd::add(const glm::mat4& placement, int clip, float time, bool loop) {
    CrowdInstance c;
    c.placement = placement;
    c.a.clip = clip;
    c.a.time = time;
    c.a.loop = loop;
    instances_.push_back(c);
    return (int)instances_.size() - 1;
}

void Crowd::remove(int i) {
    if (i >= 0 && i < size()) instances_.erase(instances_.begin() + i);
}

void Crowd::play(int i, int clip, bool loop, float time) {
    if (i < 0 || i >= size()) return;
    CrowdInstance& c = instances_[(size_t)i];
    c.a = CrowdTrack{clip, time, c.a.speed, loop};
    c.weight = 0.0f;
    c.fadeSeconds = c.fadeElapsed = 0.0f;
}

void Crowd::crossFade(int i, int clip, float seconds, bool loop) {
    if (i < 0 || i >= size()) return;
    CrowdInstance& c = instances_[(size_t)i];
    if (seconds <= 0.0f) { play(i, clip, loop); return; }
    if (c.weight > 0.5f) c.a = c.b;   // already mostly B (a fade or blend in progress): fade on from B
    c.b = CrowdTrack{clip, 0.0f, c.a.speed, loop};
    c.weight = 0.0f;
    c.fadeSeconds = seconds;
    c.fadeElapsed = 0.0f;
}

void Crowd::setBlend(int i, int clipA, int clipB, float weight) {
    if (i < 0 || i >= size()) return;
    CrowdInstance& c = instances_[(size_t)i];
    if (c.a.clip != clipA) c.a = CrowdTrack{clipA, 0.0f, c.a.speed, true};
    if (c.b.clip != clipB || c.weight <= 0.0f) c.b = CrowdTrack{clipB, c.b.clip == clipB ? c.b.time : 0.0f, c.a.speed, true};
    c.weight = std::clamp(weight, 0.0f, 1.0f);
    c.fadeSeconds = c.fadeElapsed = 0.0f;
}

void Crowd::update(float dt) {
    for (CrowdInstance& c : instances_) {
        c.a.time += dt * c.a.speed;
        if (c.weight > 0.0f || c.fadeSeconds > 0.0f) c.b.time += dt * c.b.speed;
        if (c.fadeSeconds > 0.0f) {
            c.fadeElapsed += dt;
            c.weight = std::clamp(c.fadeElapsed / c.fadeSeconds, 0.0f, 1.0f);
            if (c.fadeElapsed >= c.fadeSeconds) {   // done: B is what it plays now
                c.a = c.b;
                c.weight = 0.0f;
                c.fadeSeconds = c.fadeElapsed = 0.0f;
            }
        }
    }
}

void Crowd::gpuInstances(std::vector<CrowdGpuInstance>& out) const {
    out.clear();
    if (!tex_) return;
    out.reserve(instances_.size());
    for (const CrowdInstance& c : instances_) {
        if (c.hidden) continue;
        CrowdGpuInstance g;
        for (int r = 0; r < 3; r++) g.rows[r] = glm::vec4(c.placement[0][r], c.placement[1][r], c.placement[2][r], c.placement[3][r]);
        const FramePair a = framePair(*tex_, c.a.clip, c.a.time, c.a.loop);
        g.a = glm::vec4(a.row0, a.row1, a.blend, 0.0f);
        if (c.weight > 0.0f) {
            const FramePair b = framePair(*tex_, c.b.clip, c.b.time, c.b.loop);
            g.b = glm::vec4(b.row0, b.row1, b.blend, c.weight);
        } else {
            g.b = glm::vec4(a.row0, a.row1, a.blend, 0.0f);
        }
        out.push_back(g);
    }
}

}  // namespace toms::gltf
