// render_iface.h — backend-agnostic 2D sprite/text renderer interface.
// Desktop (Windows/Linux) uses the Vulkan implementation (renderer.h).
// Emscripten/WebGL2 uses renderer_webgl.h. Game talks only to IRenderer,
// so neither backend leaks into shared game logic.
#pragma once
#include <vector>
#include <cstdint>
#include <string>

struct Quad {
    float rect[4];   // x,y,w,h in pixels (dst)
    float uv[4];     // u0,v0,u1,v1 (src atlas)
    float tint[4];   // rgba
};

// Shared sprite-grid layout (must match Game::SPRITE_ORDER in game.cpp).
enum SpriteIdx {
    SP_FLOOR=0, SP_WALL, SP_STAIRS_UP, SP_STAIRS_DOWN, SP_DOOR_YELLOW, SP_DOOR_BLUE,
    SP_DOOR_RED, SP_KEY_YELLOW, SP_KEY_BLUE, SP_KEY_RED, SP_PLAYER, SP_SLIME,
    SP_BAT, SP_GOLEM, SP_SKELETON, SP_WRAITH, SP_DEMON, SP_VILLAGER, SP_SORCERER,
    SP_KING, SP_PRINCESS, SP_BOSS, SP_HP_POTION, SP_ATK_GEM, SP_DEF_GEM, SP_GOLD,
    SP_COUNT
};

// Pack N uniform sprite layers into a single RGBA atlas (grid of `cols` columns).
// Returns false if sizes mismatch. outW/outH receive the atlas dimensions.
inline bool packAtlas(const std::vector<std::vector<uint8_t>>& layers,
                      uint32_t sw, uint32_t sh, uint32_t cols,
                      std::vector<uint8_t>& out, uint32_t& outW, uint32_t& outH) {
    if (layers.empty()) return false;
    for (auto& l : layers) if (l.size() != sw*sh*4) return false;
    uint32_t rows = (uint32_t)((layers.size() + cols - 1) / cols);
    outW = cols * sw; outH = rows * sh;
    out.assign((size_t)outW * outH * 4, 0);
    for (size_t i = 0; i < layers.size(); i++) {
        uint32_t gx = (uint32_t)(i % cols), gy = (uint32_t)(i / cols);
        const uint8_t* src = layers[i].data();
        for (uint32_t y = 0; y < sh; y++)
            for (uint32_t x = 0; x < sw; x++) {
                uint32_t ax = gx*sw + x, ay = gy*sh + y;
                const uint8_t* s = src + (size_t)(y*sw + x)*4;
                uint8_t* d = out.data() + (size_t)(ay*outW + ax)*4;
                d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
            }
    }
    return true;
}

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void init(uint32_t w, uint32_t h) = 0;
    // layers: one RGBA buffer per sprite (already decoded). sw/sh = sprite cell size.
    virtual void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) = 0;
    virtual void loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) = 0;
    virtual void begin() = 0;
    virtual void drawSprite(const Quad& q) = 0;
    virtual void drawText(const Quad& q) = 0;
    virtual void end() = 0;                       // present to target (offscreen/canvas)
    virtual uint32_t width()  const = 0;
    virtual uint32_t height() const = 0;
    // Desktop-only: dump the current frame to PNG. WebGL build overrides as no-op.
    virtual void savePNG(const std::string& path) { (void)path; }
};
