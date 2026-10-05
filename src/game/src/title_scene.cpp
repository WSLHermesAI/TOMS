// title_scene.cpp -- see title_scene.h.
#include "title_scene.h"

#include "vfs.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace toms::next {
namespace {

constexpr double kSecondsPerCamera = 10.0;

std::string dirOf(const std::string& path) {
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : path.substr(0, s);
}

// A camera's view with its horizon level: same place, same direction, world up (no roll).
glm::mat4 levelView(const glm::mat4& world) {
    const glm::vec3 eye(world[3]), fwd = -glm::normalize(glm::vec3(world[2]));
    if (std::fabs(fwd.y) > 0.98f) return glm::inverse(world);   // straight up / down: no horizon to level
    return glm::lookAt(eye, eye + fwd, glm::vec3(0, 1, 0));
}

}  // namespace

bool TitleScene::load(const std::string& path, std::string& error) {
    std::string bytes;
    if (!toms::vfsReadAll(path, bytes)) {
        error = "cannot read " + path;
        return false;
    }
    if (!gltf::loadModelFromMemory(bytes.data(), bytes.size(), dirOf(path), model_, &error)) return false;
    if (!renderer_.init()) {
        error = "no mesh shaders for this backend";
        return false;
    }
    pose_.bind(&model_);
    if (!model_.animations.empty()) pose_.apply(0, 0.0f);
    pose_.updateWorld();
    if (!pose_.bounds(boundsMin_, boundsMax_)) boundsMin_ = glm::vec3(-1.0f), boundsMax_ = glm::vec3(1.0f);
    const std::vector<gltf::PlacedCamera> cams = pose_.cameras();
    for (size_t i = 0; i < cams.size(); i++)
        if (model_.cameras[(size_t)cams[i].camera].perspective) cameras_.push_back((int)i);
    gpu_ = renderer_.upload(model_);
    std::fprintf(stderr, "[title] scene %s: %zu triangles, %zu of %zu cameras used\n", path.c_str(), model_.triangleCount(),
                 cameras_.size(), model_.cameras.size());
    return true;
}

void TitleScene::shutdown() {
    gpu_.reset();   // its buffers first, while bgfx is up
    renderer_.shutdown();
}

void TitleScene::draw(double dtSeconds, uint32_t width, uint32_t height) {
    if (!gpu_ || width == 0 || height == 0) return;
    time_ += dtSeconds;
    pose_.reset();
    if (!model_.animations.empty()) {
        const float d = model_.animations[0].duration;
        pose_.apply(0, d > 0 ? (float)std::fmod(time_, (double)d) : 0.0f);
    }
    pose_.updateWorld();

    GltfRenderer::Frame f;
    const float aspect = float(width) / float(height);
    const glm::vec3 c = (boundsMin_ + boundsMax_) * 0.5f;
    const float radius = std::max(glm::length(boundsMax_ - boundsMin_) * 0.5f, 1e-3f);
    const std::vector<gltf::PlacedCamera> cams = pose_.cameras();
    if (!cameras_.empty() && !cams.empty()) {   // the file's cameras, levelled, a cut every kSecondsPerCamera
        const size_t pick = (size_t)cameras_[(size_t)(time_ / kSecondsPerCamera) % cameras_.size()];
        const gltf::PlacedCamera& pc = cams[std::min(pick, cams.size() - 1)];
        const gltf::Camera& cam = model_.cameras[(size_t)pc.camera];
        f.eye = glm::vec3(pc.world[3]);
        f.view = levelView(pc.world);
        const float zn = std::max(cam.znear, 0.01f);
        const float zf = cam.zfar > zn ? cam.zfar : zn + radius * 20.0f;
        f.proj = GltfRenderer::projection(glm::degrees(cam.yfov), aspect, zn, zf);
    } else {   // no usable camera: a slow orbit around the whole scene
        const float a = (float)time_ * 0.15f;
        const float dist = radius * 1.6f;
        f.eye = c + glm::vec3(std::sin(a) * dist, radius * 0.6f, std::cos(a) * dist);
        f.view = glm::lookAt(f.eye, c, glm::vec3(0, 1, 0));
        f.proj = GltfRenderer::projection(45.0f, aspect, radius * 0.01f, radius * 10.0f);
    }
    f.boundsMin = boundsMin_;
    f.boundsMax = boundsMax_;
#if defined(__EMSCRIPTEN__)
    f.srgbOut = true;     // WebGL has no sRGB backbuffer: the shader encodes
#else
    f.srgbOut = false;    // the desktop / Android backbuffer is sRGB
#endif
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    f.shadows = false;    // phones and the browser: keep the title cheap
#else
    f.shadowMapSize = 2048;
#endif
    f.exposure = 0.75f;   // a little dark: the menu stays readable over it
    renderer_.begin(kFirstView, f, width, height, 0x101018ff);
    renderer_.draw(*gpu_, pose_);
}

}  // namespace toms::next
