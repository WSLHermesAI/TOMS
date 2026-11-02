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
    static constexpr uint16_t m_u16ViewClear   = 0;   // full backbuffer, clears to the background colour
    static constexpr uint16_t m_u16ViewGame    = 1;   // letterboxed design-space view (the world's sprites)
    static constexpr uint16_t m_u16ViewUi      = 2;   // RmlUi documents (letterboxed like the game view)
    static constexpr uint16_t m_u16ViewTop     = 3;   // sprites over the UI (setLayer(1)), letterboxed like the game view
    static constexpr uint16_t m_u16ViewOverlay = 4;   // first free view for the host (ImGui dev windows)

    // A 3D scene behind everything (GltfRenderer, e.g. the title's TitleScene) in views
    // first .. first + count - 1: from the next end() on they run after kViewClear and before
    // kViewGame / kViewUi / kViewOverlay (bgfx::setViewOrder). count 0 = none (plain id order).
    void f_SetSceneViews(uint16_t l_u16First, uint16_t l_u16Count) { m_u16SceneFirst = l_u16First; m_u16SceneCount = l_u16Count; }

    BgfxRenderer() = default;
    ~BgfxRenderer() override;

    // ---- IRenderer ----
    void f_Init(uint32_t l_uW, uint32_t l_uH) override;   // w/h ignored: the host owns the backbuffer size
    void f_LoadSprites(const std::vector<std::vector<uint8_t>>& l_vLayers, uint32_t l_uSw, uint32_t l_uSh) override;
    void f_LoadSpriteAtlas(const std::vector<uint8_t>& l_vRgba, uint32_t l_uW, uint32_t l_uH, const std::string& l_strFile) override;
    uint16_t f_LoadTexture(const std::vector<uint8_t>& l_vRgba, uint32_t l_uW, uint32_t l_uH) override;
    void f_ReleaseTexture(uint16_t l_u16Texture) override;
    void f_SetTextureFilter(uint16_t l_u16Texture, bool l_bLinear) override;
    // Layers (render_iface.h LayerEffect, docs/engine/18_NODE_EFFECTS.md): each layer is drawn into an RGBA16F picture
    // of the game view's size in a view of its own (kViewLayerFirst + n, run after the clear and before kViewGame, inner
    // layers before the layers around them), then drawn with fs_layer.sc where beginLayer was called. Pictures are
    // premultiplied: the sprites inside a layer blend into it keeping its alpha, and the picture is drawn with
    // (ONE, 1 - alpha). Not available: everything is drawn straight through.
    void f_BeginLayer(const LayerEffect& l_oFx) override;
    void f_EndLayer() override;
    bool f_SupportsLayers() const;
    // Offscreen capture (an editor's "export as GIF"): from the next end() on, the game view draws into a w x h picture
    // of its own instead of the window -- cleared transparent, design space 0..w x 0..h, blended premultiplied so the
    // alpha is right -- and that picture is read back. 0 x 0 = off (the window again). After end(), the pixels are in
    // capturePixels() once bgfx::frame() has returned captureFrame() or later: RGBA8, premultiplied alpha, the colours
    // as the screen shows them (sRGB on desktop).
    void f_SetCapture(uint16_t l_u16W, uint16_t l_u16H);
    uint32_t f_CaptureFrame() const { return m_uCaptureFrame; }
    const std::vector<uint8_t>& f_CapturePixels() const { return m_vCapturePx; }
    static constexpr uint16_t m_u16ViewCapture = 199;   // the read back: after the game view, before the layers' views (200+)
    size_t f_LastLayerCount() const { return m_nLastLayerCount; }    // layers drawn last frame
    static constexpr uint16_t m_u16ViewLayerFirst = 200;
    static constexpr uint32_t m_uMaxLayers = 48;                    // per frame; more are drawn straight through
    void begin() override;
    void f_DrawSprite(const Quad& l_oQ) override;
    void f_DrawMesh(const Mesh& l_oM) override;     // the CPU path's program, from one vertex / index buffer per frame
    void f_SetNode(uint8_t l_u8N) override;
    void f_SetLayer(int l_iLayer) override { m_iLayer = l_iLayer; }
    void f_SetNodeFilter(uint8_t l_u8N) override;
    void end() override;                          // submits to kViewClear/kViewGame; host calls bgfx::frame()
    uint32_t f_Width()  const override { return m_uDesignW; }
    uint32_t f_Height() const override { return m_uDesignH; }
    void f_SavePNG(const std::string& l_strPath) override;   // bgfx screenshot of the next frame
    // GPU-simulated particles (cs_fx_spawn.sc, cs_fx_update.sc; drawn with vs_sprite_cs.sc).
    bool f_SupportsGpuParticles() const override;
    uint32_t f_CreateGpuParticles(uint32_t l_uCapacity) override;
    void f_ReleaseGpuParticles(uint32_t l_uEmitter) override;
    void f_DrawGpuParticles(const GpuParticleFrame& l_oF) override;
    uint32_t f_LastGpuEmitters() const { return m_uLastGpuEmitters; }   // GPU particle emitters drawn last frame

    // ---- host interface ----
    void f_Destroy();                                // free GPU handles (must run before bgfx::shutdown)
    void f_SetDeviceSize(uint32_t l_uW, uint32_t l_uH) { m_uDevW = l_uW; m_uDevH = l_uH; }
    uint32_t f_DeviceWidth()  const { return m_uDevW; }
    uint32_t f_DeviceHeight() const { return m_uDevH; }
    uint32_t f_LastDrawCalls() const { return m_uLastDrawCalls; }
    enum class SpritePath { Auto, Compute, Instancing, Cpu };
    // The preferred path; a path the backend does not have falls back to the next one (after init()).
    void f_SetSpritePath(SpritePath l_eP) { m_eWantPath = l_eP; }
    SpritePath f_SpritePath() const;                   // the one in use
    static const char* f_SpritePathName(SpritePath l_eP);  // "GPU compute", "GPU instancing", "CPU vertices"
    bool f_HasComputePath() const { return m_u16CsProgram != m_u16Invalid && m_u16CsDrawProgram != m_u16Invalid; }
    bool f_HasInstancingPath() const { return m_u16InstProgram != m_u16Invalid; }
    size_t   f_LastQuadCount() const { return m_nLastQuadCount; }   // quads asked for last frame
    size_t   f_LastDrawnQuads() const { return m_nLastDrawnQuads; } // quads actually submitted to the GPU
    size_t   f_LastMeshCount() const { return m_nLastMeshCount; }   // meshes drawn last frame

    // Same API the old Vulkan Renderer had, so callers ported 1:1.
    static inline uint32_t m_uDesignW = 1024, m_uDesignH = 768;
    static void f_SetDesignSize(uint32_t l_uW, uint32_t l_uH) {
        if (l_uW >= 480 && l_uW <= 2048 && l_uH >= 360 && l_uH <= 1536) { m_uDesignW = l_uW; m_uDesignH = l_uH; }
    }
    // The editors (tools/studio_common/GameCanvasView): the design space is the widget, any size.
    static void f_SetDesignSizeExact(uint32_t l_uW, uint32_t l_uH) { m_uDesignW = l_uW ? l_uW : 1; m_uDesignH = l_uH ? l_uH : 1; }
    // A texture whose pixels change (an editor's overlay): RGBA8 straight alpha, sampled like the
    // atlases. Freed by releaseTexture.
    uint16_t f_CreateDynamicTexture(uint32_t l_uW, uint32_t l_uH);
    void f_UpdateTexture(uint16_t l_u16Texture, const uint8_t* l_pRgba, uint32_t l_uW, uint32_t l_uH);
    struct ViewportRect { float m_fX, m_fY, m_fWidth, m_fHeight; };
    static ViewportRect f_ComputeAspectFitViewport(uint32_t l_uDeviceW, uint32_t l_uDeviceH,
                                                 uint32_t l_uTargetW, uint32_t l_uTargetH);
    // Device (backbuffer) pixel -> design space; false if the point is in a letterbox bar.
    bool f_DeviceToDesign(double l_dDeviceX, double l_dDeviceY, float& l_fOutX, float& l_fOutY) const;

private:
    uint16_t m_u16SceneFirst = 0, m_u16SceneCount = 0;     // setSceneViews()
    bool m_bOrderApplied = false;                     // a view order is set (applyViewOrder)
    void f_SubmitQuads(const std::vector<Quad>& l_vQuads, uint16_t l_u16Texture);
    // The GPU paths; false = they could not take these quads (no buffer space): use the next path.
    bool f_SubmitQuadsInstanced(const std::vector<Quad>& l_vQuads, uint16_t l_u16Texture);
    bool f_SubmitQuadsCompute(const std::vector<Quad>& l_vQuads, uint16_t l_u16Texture);
    uint64_t f_BlendState(bool l_bAdditive) const;
    uint64_t f_LayerBlendState(bool l_bAdditive) const;   // into a layer's premultiplied picture
    // GPU particle and mesh draws are placed among the sprites: before quad `at` (sprites_ index).
    // emitter 0 = a mesh: its vertices and indices in meshVerts_ / meshIdx_ (indices local to its first vertex).
    struct GpuDraw {
        size_t m_nAt; uint32_t m_uEmitter; uint16_t m_u16Texture; bool m_bAdditive;
        uint32_t m_uFirstVertex = 0, m_uVertexCount = 0, m_uFirstIndex = 0, m_uIndexCount = 0;
        int m_iLayer = -1;   // >= 0: not a mesh or emitter but layer `layer`'s picture, drawn with its effect
    };
    // Draws the GPU emitters, meshes and layer pictures placed at quad index `at` (markers from `next` on) into the game
    // view; returns where the next marker is (the quads up to it can go in one run).
    size_t f_DrawGpuAt(size_t l_nAt, size_t& l_nNext, uint16_t l_u16Texture) { return f_DrawMarkers(m_u16ViewGame, m_vGpuDraws, false, l_nAt, l_nNext, l_u16Texture); }
    size_t f_NextGpuAt(size_t l_nNext) const { return l_nNext < m_vGpuDraws.size() ? m_vGpuDraws[l_nNext].m_nAt : SIZE_MAX; }
    // The same for any view and marker list; inLayer: blend into a layer's premultiplied picture.
    size_t f_DrawMarkers(uint16_t l_u16View, const std::vector<GpuDraw>& l_vDraws, bool l_bInLayer, size_t l_nAt, size_t& l_nNext, uint16_t l_u16Texture);
    void f_BindTexture(uint16_t l_u16Texture);   // stage 0, with its filter (setTextureFilter)
    std::vector<uint16_t> m_vLinearTex;
    // Layers: this frame's, by beginLayer order; each its own quads and markers.
    struct LayerDraw {
        LayerEffect m_oFx;
        std::vector<Quad> m_vQuads;
        std::vector<GpuDraw> m_vDraws;
        int m_iSlot = -1;    // its picture: layerFb_[slot], drawn in view kViewLayerFirst + slot
        // What it draws, design space (prepareLayers), and how far its effect reaches past that: only the content plus
        // twice the reach is cleared and drawn into, only the content plus the reach composited -- a small arm's blur
        // costs its size in pixels, not the screen's.
        bool m_bAny = false;
        float m_fX0 = 0, m_fY0 = 0, m_fX1 = 0, m_fY1 = 0, m_fReach = 0;
    };
    std::vector<LayerDraw> m_vLayers;
    std::vector<int> m_vLayerStack;     // the open layers (innermost last); -1 = one drawn straight through
    std::vector<int> m_vLayerClosed;    // in the order they closed: inner layers first (their views run in this order)
    std::vector<uint16_t> m_vLayerFb;   // the pictures (frame buffers, RGBA16F), reused from frame to frame
    uint16_t m_u16LayerW = 0, m_u16LayerH = 0;
    uint16_t m_u16LayerProgram = m_u16Invalid, m_u16ULayerMode = m_u16Invalid, m_u16ULayerParams = m_u16Invalid, m_u16ULayerColor = m_u16Invalid,
             m_u16ULayerTexel = m_u16Invalid;
    size_t m_nLastLayerCount = 0;
    std::vector<Quad>& f_CurrentQuads() { return m_vLayerStack.empty() || m_vLayerStack.back() < 0 ? m_vSprites : m_vLayers[(size_t)m_vLayerStack.back()].m_vQuads; }
    std::vector<GpuDraw>& f_CurrentDraws() { return m_vLayerStack.empty() || m_vLayerStack.back() < 0 ? m_vGpuDraws : m_vLayers[(size_t)m_vLayerStack.back()].m_vDraws; }
    void f_PrepareLayers();                                 // end(): pictures, views and the view order
    void f_SubmitLayer(const LayerDraw& l_oL, uint16_t l_u16View);  // a layer's contents into its picture
    void f_SubmitComposite(int l_iLayer, uint16_t l_u16View);       // a layer's picture, with its effect, into `view`
    void f_ApplyViewOrder();
    uint16_t m_u16CaptureW = 0, m_u16CaptureH = 0;          // setCapture
    uint16_t m_u16CaptureFb = m_u16Invalid, m_u16CaptureRead = m_u16Invalid, m_u16CaptureFbW = 0, m_u16CaptureFbH = 0;
    uint32_t m_uCaptureFrame = 0;
    std::vector<uint8_t> m_vCapturePx;
    void f_PrepareCapture();                          // end(): the game view into the capture picture, its read back
    uint16_t f_CreateAtlas(const std::vector<uint8_t>& l_vPx, uint32_t l_uW, uint32_t l_uH);
    static void f_DestroyTexture(uint16_t& l_u16Handle);

    static constexpr uint16_t m_u16Invalid = UINT16_MAX;
    uint16_t m_u16Program  = m_u16Invalid;
    uint16_t m_u16Sampler  = m_u16Invalid;   // uniform s_tex
    uint16_t m_u16InstProgram = m_u16Invalid;   // ShaderProgram::SpriteInstanced
    uint16_t m_u16QuadVb = m_u16Invalid, m_u16QuadIb = m_u16Invalid;   // the shared unit quad the instances draw
    // Compute path: the quads in (read by cs_sprite.sc), their vertices out (drawn by vs_sprite_cs.sc),
    // a static index buffer for up to kComputeChunk quads per draw, and the quad count uniform.
    uint16_t m_u16CsProgram = m_u16Invalid, m_u16CsDrawProgram = m_u16Invalid, m_u16CsParams = m_u16Invalid;
    uint16_t m_u16CsIn = m_u16Invalid, m_u16CsOut = m_u16Invalid;
    uint32_t m_uCsCapacity = 0, m_uCsOutCap = 0;   // vertices / elements the buffers hold
    // Persistent, growing buffers of the CPU and instancing paths, and the CPU / compute index buffer.
    uint16_t m_u16CpuVb = m_u16Invalid, m_u16InstVb = m_u16Invalid, m_u16ChunkIb = m_u16Invalid;
    uint32_t m_uCpuCap = 0, m_uInstCap = 0;
    std::vector<float> m_vCsScratch;      // this frame's quads, packed (compute path)
    std::vector<float> m_vCpuScratch, m_vInstScratch;   // the same for the CPU / instancing paths
    SpritePath m_eWantPath = SpritePath::Auto;
    // GPU particles: per emitter its state (4 vec4 per slot), its quads' vertices, and its newborns.
    struct GpuEmitterBuffers { uint16_t m_u16State = m_u16Invalid, m_u16Out = m_u16Invalid, m_u16Spawn = m_u16Invalid; uint32_t m_uCapacity = 0, m_uSpawnCap = 0; };
    std::vector<GpuEmitterBuffers> m_vGpuEmitters;   // id - 1
    std::vector<GpuDraw> m_vGpuDraws;                // this frame's, in order
    uint16_t m_u16FxSpawn = m_u16Invalid, m_u16FxUpdate = m_u16Invalid;
    uint16_t m_u16UFxSim = m_u16Invalid, m_u16UFxForce = m_u16Invalid, m_u16UFxOrigin = m_u16Invalid, m_u16UFxXform = m_u16Invalid, m_u16UFxTint = m_u16Invalid;
    uint16_t m_u16UFxColorLut = m_u16Invalid, m_u16UFxScalarLut = m_u16Invalid, m_u16UFxFrameUv = m_u16Invalid, m_u16UFxFrameRect = m_u16Invalid;
    uint32_t m_uLastGpuEmitters = 0;
    // drawMesh: this frame's mesh vertices (SpriteVertex, 9 floats each) and indices, uploaded in end().
    std::vector<float> m_vMeshVerts;
    std::vector<uint16_t> m_vMeshIdx;
    uint16_t m_u16MeshVb = m_u16Invalid, m_u16MeshIb = m_u16Invalid;   // dynamic buffers, grown as needed
    uint32_t m_uMeshVbCap = 0, m_uMeshIbCap = 0;
    size_t m_nLastMeshCount = 0;
    void f_UploadMeshes();
    uint16_t m_u16SpriteTex = m_u16Invalid;
    std::vector<uint16_t> m_vExtraTex;   // loadTexture (freed by releaseTexture or destroy)
    uint32_t m_uDevW = 1280, m_uDevH = 720;
    uint8_t  m_u8Node = 0, m_u8NodeFilter = 0;
    uint32_t m_uLastDrawCalls = 0;
    size_t   m_nLastQuadCount = 0;
    size_t   m_nLastDrawnQuads = 0;
    std::vector<Quad> m_vSprites;
    std::vector<Quad> m_vTopSprites;   // setLayer(1): drawn in kViewTop, over the UI
    int m_iLayer = 0;
    void f_SubmitTop();                // topSprites_ in transient buffers (the shared CPU buffer holds sprites_)
};

}  // namespace toms::next
