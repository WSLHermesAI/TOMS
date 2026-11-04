// title_scene.h -- a glTF scene behind the title screen (docs/engine/08_GLTF.md, "In the game").
//
// Loads a model (assets/media/models/VirtualCity.glb by default), plays its first animation in a loop
// and looks through the file's own cameras, cutting to the next every few seconds. Each camera keeps
// its path and where it looks, but its horizon is held level (a banking camera looks odd behind a
// menu). A file without cameras gets a slow orbit. Drawn with GltfRenderer in bgfx views
// kFirstView .. + GltfRenderer::kViews - 1, which BgfxRenderer::setSceneViews() puts after the screen
// clear and before the sprites and the UI.
//
// Or the sea (f_LoadOcean, docs/engine/21_OCEAN_VIEWER.md): the ocean and its sky instead of a model. In normal play the
// host takes them in turns, one each start (game_session.cpp f_NextTitleBackground).
#pragma once
#include "gltf_model.h"
#include "gltf_renderer.h"
#include "ocean_renderer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toms::next {

class TitleScene {
public:
    static constexpr uint16_t m_u16FirstView = 16;   // after the game's views (0..3) and the host's

    // path: the .glb / .gltf (read through vfs, so it works from an APK too). False + error when it
    // does not load or bgfx has no mesh shaders; the title then keeps its plain background.
    bool f_Load(const std::string& l_strPath, std::string& l_strError);
    // The sea instead of a model. False + error when this backend cannot draw it.
    bool f_LoadOcean(std::string& l_strError);
    void f_Shutdown();
    bool f_Ready() const { return m_spGpu != nullptr || m_upOcean != nullptr; }
    uint16_t f_ViewCount() const { return m_upOcean ? (uint16_t)1 : GltfRenderer::m_u16Views; }   // how many views it draws in, from kFirstView

    // Advances the animation by dtSeconds and queues the scene for this frame (the whole backbuffer).
    void f_Draw(double l_dDtSeconds, uint32_t l_uWidth, uint32_t l_uHeight);

    size_t f_CameraCount() const { return m_vCameras.size(); }

private:
    gltf::Model m_oModel;
    gltf::Pose m_oPose;
    GltfRenderer m_oRenderer;
    std::shared_ptr<GltfRenderer::GpuModel> m_spGpu;
    std::vector<int> m_vCameras;          // indices into pose_.cameras(): the perspective ones
    double m_dTime = 0.0;
    glm::vec3 m_v3BoundsMin{-1.0f}, m_v3BoundsMax{1.0f};
    std::unique_ptr<ocean::OceanRenderer> m_upOcean;    // set: the sea is drawn, not the model
    ocean::Look m_oOceanLook;
    int m_iOceanRows = 0, m_iOceanSimEvery = 1, m_iOceanFrame = 0;
    float m_fOceanPendingDt = 0.0f;                     // time the waves have not been simulated for yet
    void f_DrawOcean(double l_dDtSeconds, uint32_t l_uWidth, uint32_t l_uHeight);
};

}  // namespace toms::next
