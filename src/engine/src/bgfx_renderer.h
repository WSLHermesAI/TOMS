// bgfx_renderer.h -- IRenderer on bgfx: the game's only renderer (it replaced the Vulkan one).
//
// The core game code does `ren = new Renderer();` (src/core/game/core/game_assets.cpp). The compat
// header game/compat/renderer.h aliases `Renderer` to this class, so that code compiles
// unmodified. Behaviour matches the old Vulkan renderer (removed 2026-09-27; see git history):
//   - a fixed 1024x768 design resolution, letterboxed into the real backbuffer
//   - sprites in one batch, one draw call (all text and UI is RmlUi, drawn in kViewUi)
//   - RGBA8 sRGB atlases, point sampling, clamp, straight alpha blending
//   - solid quads output the tint only
//   - the sprite batch has three ways to make its vertices, chosen by setSpritePath (`toms_game
//     --sprite-path=...`; Auto = instancing, the fastest measured, then compute, then CPU), all drawing
//     the same pixels:
//       Compute     a compute shader (cs_sprite.sc) expands every quad into 4 vertices in a GPU buffer
//                   that the draw then reads -- FM79979's cParticleBatchRender, without its readback.
//                   Needs compute shaders: D3D11/12, Vulkan, OpenGL 4.3 (not WebGL2 / GLES3).
//       Instancing  one 64-byte instance per quad, the vertex shader builds the corners. Every backend.
//       Cpu         four vertices per quad written on the CPU (the original path).
//     Particles (particle_fx.h), .anim clips and sprites all go through this batch.
//
// Unlike the Vulkan renderer it does NOT own the window or the device: the host (the SDL3 game
// or the Qt editor viewport, see bgfx_host.h) initializes bgfx, tells this class the backbuffer
// size every frame, and calls bgfx::frame(). This header deliberately does not include bgfx
// headers, so the core game code that includes it never sees bgfx (or its C++ settings).
#pragma once
#include "render_iface.h"
#include <cstdint>
#include <string>
#include <vector>

namespace toms::next {

class BgfxRenderer : public IRenderer {
public:
    // bgfx views this renderer uses. The host may use views after kViewOverlay (e.g. ImGui).
    static constexpr uint16_t kViewClear   = 0;   // full backbuffer, clears to the background colour
    static constexpr uint16_t kViewGame    = 1;   // letterboxed design-space view (the world's sprites)
    static constexpr uint16_t kViewUi      = 2;   // RmlUi documents (letterboxed like the game view)
    static constexpr uint16_t kViewOverlay = 3;   // first free view for the host (ImGui dev windows)

    // A 3D scene behind everything (GltfRenderer, e.g. the title's TitleScene) in views
    // first .. first + count - 1: from the next end() on they run after kViewClear and before
    // kViewGame / kViewUi / kViewOverlay (bgfx::setViewOrder). count 0 = none (plain id order).
    void setSceneViews(uint16_t first, uint16_t count) { sceneFirst_ = first; sceneCount_ = count; }

    BgfxRenderer() = default;
    ~BgfxRenderer() override;

    // ---- IRenderer ----
    void init(uint32_t w, uint32_t h) override;   // w/h ignored: the host owns the backbuffer size
    void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) override;
    void loadSpriteAtlas(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, const std::string& file) override;
    uint16_t loadTexture(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) override;
    void releaseTexture(uint16_t texture) override;
    void begin() override;
    void drawSprite(const Quad& q) override;
    void setNode(uint8_t n) override;
    void setNodeFilter(uint8_t n) override;
    void end() override;                          // submits to kViewClear/kViewGame; host calls bgfx::frame()
    uint32_t width()  const override { return kDesignW; }
    uint32_t height() const override { return kDesignH; }
    void savePNG(const std::string& path) override;   // bgfx screenshot of the next frame
    // GPU-simulated particles (cs_fx_spawn.sc, cs_fx_update.sc; drawn with vs_sprite_cs.sc).
    bool supportsGpuParticles() const override;
    uint32_t createGpuParticles(uint32_t capacity) override;
    void releaseGpuParticles(uint32_t emitter) override;
    void drawGpuParticles(const GpuParticleFrame& f) override;
    uint32_t lastGpuEmitters() const { return lastGpuEmitters_; }   // GPU particle emitters drawn last frame

    // ---- host interface ----
    void destroy();                                // free GPU handles (must run before bgfx::shutdown)
    void setDeviceSize(uint32_t w, uint32_t h) { devW_ = w; devH_ = h; }
    uint32_t deviceWidth()  const { return devW_; }
    uint32_t deviceHeight() const { return devH_; }
    uint32_t lastDrawCalls() const { return lastDrawCalls_; }
    enum class SpritePath { Auto, Compute, Instancing, Cpu };
    // The preferred path; a path the backend does not have falls back to the next one (after init()).
    void setSpritePath(SpritePath p) { wantPath_ = p; }
    SpritePath spritePath() const;                   // the one in use
    static const char* spritePathName(SpritePath p);  // "GPU compute", "GPU instancing", "CPU vertices"
    bool hasComputePath() const { return csProgram_ != kInvalid && csDrawProgram_ != kInvalid; }
    bool hasInstancingPath() const { return instProgram_ != kInvalid; }
    size_t   lastQuadCount() const { return lastQuadCount_; }   // quads asked for last frame
    size_t   lastDrawnQuads() const { return lastDrawnQuads_; } // quads actually submitted to the GPU

    // Same API the old Vulkan Renderer had, so callers ported 1:1.
    static inline uint32_t kDesignW = 1024, kDesignH = 768;
    static void setDesignSize(uint32_t w, uint32_t h) {
        if (w >= 480 && w <= 2048 && h >= 360 && h <= 1536) { kDesignW = w; kDesignH = h; }
    }
    // The editors (tools/studio_common/GameCanvasView): the design space is the widget, any size.
    static void setDesignSizeExact(uint32_t w, uint32_t h) { kDesignW = w ? w : 1; kDesignH = h ? h : 1; }
    // A texture whose pixels change (an editor's overlay): RGBA8 straight alpha, sampled like the
    // atlases. Freed by releaseTexture.
    uint16_t createDynamicTexture(uint32_t w, uint32_t h);
    void updateTexture(uint16_t texture, const uint8_t* rgba, uint32_t w, uint32_t h);
    struct ViewportRect { float x, y, width, height; };
    static ViewportRect computeAspectFitViewport(uint32_t deviceW, uint32_t deviceH,
                                                 uint32_t targetW, uint32_t targetH);
    // Device (backbuffer) pixel -> design space; false if the point is in a letterbox bar.
    bool deviceToDesign(double deviceX, double deviceY, float& outX, float& outY) const;

private:
    uint16_t sceneFirst_ = 0, sceneCount_ = 0;     // setSceneViews()
    uint16_t appliedSceneCount_ = 0;
    void submitQuads(const std::vector<Quad>& quads, uint16_t texture);
    // The GPU paths; false = they could not take these quads (no buffer space): use the next path.
    bool submitQuadsInstanced(const std::vector<Quad>& quads, uint16_t texture);
    bool submitQuadsCompute(const std::vector<Quad>& quads, uint16_t texture);
    uint64_t blendState(bool additive) const;
    // GPU particle draws are placed among the sprites: before quad `at` (sprites_ index).
    struct GpuDraw { size_t at; uint32_t emitter; uint16_t texture; bool additive; };
    // Draws the GPU emitters placed at quad index `at` (markers from `next` on); returns where the
    // next marker is (the quads up to it can go in one run).
    size_t drawGpuAt(size_t at, size_t& next, uint16_t texture);
    size_t nextGpuAt(size_t next) const { return next < gpuDraws_.size() ? gpuDraws_[next].at : SIZE_MAX; }
    uint16_t createAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h);
    static void destroyTexture(uint16_t& handle);

    static constexpr uint16_t kInvalid = UINT16_MAX;
    uint16_t program_  = kInvalid;
    uint16_t sampler_  = kInvalid;   // uniform s_tex
    uint16_t instProgram_ = kInvalid;   // ShaderProgram::SpriteInstanced
    uint16_t quadVb_ = kInvalid, quadIb_ = kInvalid;   // the shared unit quad the instances draw
    // Compute path: the quads in (read by cs_sprite.sc), their vertices out (drawn by vs_sprite_cs.sc),
    // a static index buffer for up to kComputeChunk quads per draw, and the quad count uniform.
    uint16_t csProgram_ = kInvalid, csDrawProgram_ = kInvalid, csParams_ = kInvalid;
    uint16_t csIn_ = kInvalid, csOut_ = kInvalid;
    uint32_t csCapacity_ = 0, csOutCap_ = 0;   // vertices / elements the buffers hold
    // Persistent, growing buffers of the CPU and instancing paths, and the CPU / compute index buffer.
    uint16_t cpuVb_ = kInvalid, instVb_ = kInvalid, chunkIb_ = kInvalid;
    uint32_t cpuCap_ = 0, instCap_ = 0;
    std::vector<float> csScratch_;      // this frame's quads, packed (compute path)
    std::vector<float> cpuScratch_, instScratch_;   // the same for the CPU / instancing paths
    SpritePath wantPath_ = SpritePath::Auto;
    // GPU particles: per emitter its state (4 vec4 per slot), its quads' vertices, and its newborns.
    struct GpuEmitterBuffers { uint16_t state = kInvalid, out = kInvalid, spawn = kInvalid; uint32_t capacity = 0, spawnCap = 0; };
    std::vector<GpuEmitterBuffers> gpuEmitters_;   // id - 1
    std::vector<GpuDraw> gpuDraws_;                // this frame's, in order
    uint16_t fxSpawn_ = kInvalid, fxUpdate_ = kInvalid;
    uint16_t uFxSim_ = kInvalid, uFxForce_ = kInvalid, uFxOrigin_ = kInvalid, uFxXform_ = kInvalid, uFxTint_ = kInvalid;
    uint16_t uFxColorLut_ = kInvalid, uFxScalarLut_ = kInvalid, uFxFrameUv_ = kInvalid, uFxFrameRect_ = kInvalid;
    uint32_t lastGpuEmitters_ = 0;
    uint16_t spriteTex_ = kInvalid;
    std::vector<uint16_t> extraTex_;   // loadTexture (freed by releaseTexture or destroy)
    uint32_t devW_ = 1280, devH_ = 720;
    uint8_t  node_ = 0, nodeFilter_ = 0;
    uint32_t lastDrawCalls_ = 0;
    size_t   lastQuadCount_ = 0;
    size_t   lastDrawnQuads_ = 0;
    std::vector<Quad> sprites_;
};

}  // namespace toms::next
