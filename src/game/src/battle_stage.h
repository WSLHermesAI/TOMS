// battle_stage.h -- the battle's 3D stage (docs/gameplay/21_BATTLE_3D.md): glTF props on a ground, an HDR sky around
// them and a moonlight, drawn behind the 2D fighters and the UI.
//
// battle.json "stages3d" (one row per floor range, like "backdrops"):
//   "sky":    { "hdr": "<file>", "intensity": 1, "yaw": 0 (degrees), "blur": 0, "faceSize": 512 }
//   "ground": "<file>"            a model drawn first, receiving shadows, casting none
//   "shadow": "<file>"            the soft disc put under each fighter (Game::f_Battle3DShadows)
//   "props":  [ { "model": "<file>", "node": "<a root node: only it is drawn>", "at": [x, y, z],
//                 "turn": degrees about +Y, "scale": 1 }, ... ]
//   "light":  { "dir": [x, y, z], "color": [r, g, b], "intensity": 2, "ambient": 0.4 }, "exposure": 1
//   "camera", "marks", "heights": read by the game (Game::f_Battle3DCamera / f_Battle3DMark)
// Files are relative to the asset folder (assets/media); a model several props use is read once.
//
// Loading never stops the game: f_Request starts it on a worker thread (reading, parsing, decoding the textures,
// prefiltering the sky); f_Poll, each frame, uploads the result to the GPU when it is done. The host asks for the
// floor's stage as soon as the floor is entered, so it is ready before the first monster. A scripted run waits
// (f_Request's wait), so its pictures never depend on the disk's speed. Drawn with GltfRenderer in views
// m_u16FirstView.. (the title's: the two are never on together).
#pragma once
#include "bgfx_renderer.h"   // (the world groups: plain structs, no bgfx)
#include "gltf_environment.h"
#include "gltf_model.h"
#include "gltf_renderer.h"

#include <json.hpp>

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace toms::next {

class BattleStage {
public:
    static constexpr uint16_t m_u16FirstView = 16;   // = TitleScene's
    ~BattleStage();
    // Starts loading this row's stage (nothing when it is the one loaded or loading). wait: finish it now (tests).
    void f_Request(const nlohmann::json& l_jRow, const std::string& l_strAssetDir, bool l_bWait);
    // Each frame: a finished load goes to the GPU. False + why when it failed (once; that row is not tried again).
    bool f_Poll(std::string& l_strError);
    void f_Shutdown();
    // The stage of this row is on the GPU, ready to draw.
    bool f_ReadyFor(const std::string& l_strRowKey) const { return m_bReady && m_strRowKey == l_strRowKey; }
    uint16_t f_ViewCount() const { return GltfRenderer::m_u16Views; }

    struct Camera {
        glm::vec3 m_v3Eye{0.0f}, m_v3Look{0.0f, 0.0f, -1.0f};
        float m_fFovYDeg = 45.0f;
        float m_fDim = 0.0f;   // 0..1: darker (a special move); never black, the stage stays faintly there
        float m_fMoon = 1.0f;  // the moonlight's share now (clouds passing)
        struct Point { glm::vec3 m_v3Pos{0.0f}, m_v3Color{1.0f}; float m_fIntensity = 1.0f, m_fRange = 6.0f; };
        std::vector<Point> m_vPoints;   // fires, flashes: strongest first; the renderer takes up to 3 beside the moon
        bool m_bLight = false;          // Settings > 3D battle: light (Q95): no shadows, no point lights (the moon and the sky only)
    };
    // Queues the stage for this frame (the whole backbuffer) and returns its view-projection, for placing the 2D
    // fighters on it. shadows: x, y, z the feet, w the radius (metres).
    // dt: the fade in from black when the stage comes on (f_HiddenFrame starts it again).
    glm::mat4 f_Draw(float l_fDtSeconds, uint32_t l_uWidth, uint32_t l_uHeight, const Camera& l_oCam, const std::vector<glm::vec4>& l_vShadows);
    // After the game has drawn (the same frame, after f_Draw): its world groups -- the fighters' and the fires' 2D sprites
    // placed in metres (docs/gameplay/21 §4.3) -- in this frame's scene, lit by its lights, depth tested.
    void f_DrawSprites(const std::vector<BgfxRenderer::WorldGroup>& l_vGroups);
    void f_HiddenFrame() { m_fShown = 0.0f; }   // a frame without the stage: the next battle fades in again
    // This row cannot load (it failed): the battle keeps its painted backdrop.
    bool f_Failed(const std::string& l_strRowKey) const { return m_strFailedKey == l_strRowKey; }

private:
    struct Model {
        gltf::Model m_oModel;
        gltf::Pose m_oPose;
        std::shared_ptr<GltfRenderer::GpuModel> m_spGpu;
    };
    struct Prop {
        std::shared_ptr<Model> m_spModel;
        std::shared_ptr<gltf::Pose> m_spPose;   // a "node" prop: its own pose, only that node shown
        glm::mat4 m_mat4Place{1.0f};
    };
    // What the worker thread makes (no GPU): everything but the uploads.
    struct Loaded {
        std::string m_strRowKey, m_strError;
        std::map<std::string, std::shared_ptr<Model>> m_mapModels;
        std::shared_ptr<Model> m_spGround, m_spShadow;
        std::vector<Prop> m_vProps;
        float m_fSpriteGlow = 0.6f;
        bool m_bSky = false, m_bSkyCached = false;
        gltf::Environment m_oEnv;
        GltfRenderer::Frame m_oLook;
        glm::vec3 m_v3BoundsMin{-10.0f, 0.0f, -10.0f}, m_v3BoundsMax{10.0f, 8.0f, 10.0f};
        double m_dCpuMs = 0.0;
    };
    static std::shared_ptr<Loaded> f_LoadCpu(nlohmann::json l_jRow, std::string l_strAssetDir);
    bool f_Upload(Loaded& l_oL, std::string& l_strError);

    GltfRenderer m_oRenderer;
    bool m_bInit = false, m_bReady = false;
    std::string m_strRowKey;                    // the stage on the GPU
    std::string m_strLoadingKey;                // the one the worker is making
    std::string m_strFailedKey;                 // a row that would not load (not tried again)
    std::future<std::shared_ptr<Loaded>> m_oPending;
    nlohmann::json m_jNext;                     // asked for while another was loading: started after it
    std::string m_strNextDir;
    std::map<std::string, std::shared_ptr<Model>> m_mapModels;
    std::shared_ptr<Model> m_spGround, m_spShadow;
    std::vector<Prop> m_vProps;
    float m_fSpriteGlow = 0.6f;                 // battle.json stage "sprites" "glow": the fighters' own light (x 1 / exposure)
    glm::vec3 m_v3Eye{0.0f};                    // this frame's (f_Draw), for f_DrawSprites
    float m_fExposure = 1.0f;
    GltfRenderer::Frame m_oLook;                // the light, the sky's settings, exposure
    glm::vec3 m_v3BoundsMin{-10.0f, 0.0f, -10.0f}, m_v3BoundsMax{10.0f, 8.0f, 10.0f};
    float m_fShown = 0.0f;                      // the fade in: 0 dark .. 1 as lit
    int m_iFramesDrawn = 0;                     // (the log's one line of draw counts)
};

}  // namespace toms::next
