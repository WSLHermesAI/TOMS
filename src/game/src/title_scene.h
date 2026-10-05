// title_scene.h -- a glTF scene behind the title screen (docs/18_GLTF.md, "In the game").
//
// Loads a model (assets/media/models/VirtualCity.glb by default), plays its first animation in a loop
// and looks through the file's own cameras, cutting to the next every few seconds. Each camera keeps
// its path and where it looks, but its horizon is held level (a banking camera looks odd behind a
// menu). A file without cameras gets a slow orbit. Drawn with GltfRenderer in bgfx views
// kFirstView .. + GltfRenderer::kViews - 1, which BgfxRenderer::setSceneViews() puts after the screen
// clear and before the sprites and the UI.
#pragma once
#include "gltf_model.h"
#include "gltf_renderer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toms::next {

class TitleScene {
public:
    static constexpr uint16_t kFirstView = 16;   // after the game's views (0..3) and the host's

    // path: the .glb / .gltf (read through vfs, so it works from an APK too). False + error when it
    // does not load or bgfx has no mesh shaders; the title then keeps its plain background.
    bool load(const std::string& path, std::string& error);
    void shutdown();
    bool ready() const { return gpu_ != nullptr; }

    // Advances the animation by dtSeconds and queues the scene for this frame (the whole backbuffer).
    void draw(double dtSeconds, uint32_t width, uint32_t height);

    size_t cameraCount() const { return cameras_.size(); }

private:
    gltf::Model model_;
    gltf::Pose pose_;
    GltfRenderer renderer_;
    std::shared_ptr<GltfRenderer::GpuModel> gpu_;
    std::vector<int> cameras_;          // indices into pose_.cameras(): the perspective ones
    double time_ = 0.0;
    glm::vec3 boundsMin_{-1.0f}, boundsMax_{1.0f};
};

}  // namespace toms::next
