// bgfx_renderer.h -- IRenderer on bgfx: the drop-in replacement for the Vulkan `Renderer`.
//
// The old game code does `ren = new Renderer();` (src/game/core/game_assets.cpp). The compat
// header game/compat/renderer.h aliases `Renderer` to this class, so that code compiles
// unmodified. Behaviour matches src/engine/renderer.cpp:
//   - a fixed 1024x768 design resolution, letterboxed into the real backbuffer
//   - sprites drawn first, then text (two batches, one draw call each)
//   - RGBA8 sRGB atlases, point sampling, clamp, straight alpha blending
//   - solid quads output the tint only
//
// Unlike the Vulkan renderer it does NOT own the window or the device: the host (the SDL3 game
// or the Qt editor viewport, see bgfx_host.h) initializes bgfx, tells this class the backbuffer
// size every frame, and calls bgfx::frame(). This header deliberately does not include bgfx
// headers, so the legacy game code that includes it never sees bgfx (or its C++ settings).
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
    static constexpr uint16_t kViewGame    = 1;   // letterboxed design-space view (sprites, then text)
    static constexpr uint16_t kViewOverlay = 2;   // first free view for the host (ImGui)

    BgfxRenderer() = default;
    ~BgfxRenderer() override;

    // ---- IRenderer ----
    void init(uint32_t w, uint32_t h) override;   // w/h ignored: the host owns the backbuffer size
    void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) override;
    void loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) override;
    void updateFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) override;
    void begin() override;
    void drawSprite(const Quad& q) override;
    void drawText(const Quad& q) override;
    void setNode(uint8_t n) override;
    void setNodeFilter(uint8_t n) override;
    void end() override;                          // submits to kViewClear/kViewGame; host calls bgfx::frame()
    uint32_t width()  const override { return kDesignW; }
    uint32_t height() const override { return kDesignH; }
    void savePNG(const std::string& path) override;   // bgfx screenshot of the next frame

    // ---- host interface ----
    void destroy();                                // free GPU handles (must run before bgfx::shutdown)
    void setDeviceSize(uint32_t w, uint32_t h) { devW_ = w; devH_ = h; }
    uint32_t deviceWidth()  const { return devW_; }
    uint32_t deviceHeight() const { return devH_; }
    uint32_t lastDrawCalls() const { return lastDrawCalls_; }
    size_t   lastQuadCount() const { return lastQuadCount_; }

    // Same API as the Vulkan Renderer (src/engine/renderer.h), so callers port 1:1.
    static inline uint32_t kDesignW = 1024, kDesignH = 768;
    static void setDesignSize(uint32_t w, uint32_t h) {
        if (w >= 480 && w <= 2048 && h >= 360 && h <= 1536) { kDesignW = w; kDesignH = h; }
    }
    struct ViewportRect { float x, y, width, height; };
    static ViewportRect computeAspectFitViewport(uint32_t deviceW, uint32_t deviceH,
                                                 uint32_t targetW, uint32_t targetH);
    // Device (backbuffer) pixel -> design space; false if the point is in a letterbox bar.
    bool deviceToDesign(double deviceX, double deviceY, float& outX, float& outY) const;

private:
    void submitQuads(const std::vector<Quad>& quads, uint16_t texture);
    uint16_t createAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h);
    static void destroyTexture(uint16_t& handle);

    static constexpr uint16_t kInvalid = UINT16_MAX;
    uint16_t program_  = kInvalid;
    uint16_t sampler_  = kInvalid;   // uniform s_tex
    uint16_t spriteTex_ = kInvalid;
    uint16_t fontTex_   = kInvalid;
    uint32_t devW_ = 1280, devH_ = 720;
    uint8_t  node_ = 0, nodeFilter_ = 0;
    uint32_t lastDrawCalls_ = 0;
    size_t   lastQuadCount_ = 0;
    std::vector<Quad> sprites_, texts_;
};

}  // namespace toms::next
