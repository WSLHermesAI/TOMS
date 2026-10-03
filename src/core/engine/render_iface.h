// render_iface.h — backend-agnostic 2D sprite/text renderer interface.
// The implementation is BgfxRenderer (src/engine/src/bgfx_renderer.h) on desktop and web.
// Game talks only to IRenderer, so bgfx never leaks into shared game logic.
#pragma once
#include <vector>
#include <cstdint>
#include <string>

// The one real background color players see behind every scene (map, HUD, dialogue, battle) on
// every platform. Was the same literal duplicated separately in the old Vulkan and WebGL
// renderers (2026-09-17 art/UI polish pass found it while unifying background color; both removed
// 2026-09-27) -- BgfxRenderer and the game both read it from here.
// A deep, neutral dark slate/navy -- unchanged in hue from what shipped before, just no longer a
// magic literal repeated in two files.
inline constexpr float kBackgroundClearColor[4] = {0.06f, 0.06f, 0.10f, 1.0f};

// Quad::texture of the sprite atlas (BgfxRenderer::loadSprites / loadSpriteAtlas).
inline constexpr uint16_t kSpriteAtlasTexture = 0xFFFF;

struct Quad {
    float rect[4];   // x,y,w,h in pixels (dst)
    float uv[4];     // u0,v0,u1,v1 (src atlas)
    float tint[4];   // rgba
    bool  solid = false;  // true => draw flat tint, ignore the texture (solid-color rect)
    // Rotated / scaled / mirrored sprites (anim_player.h): the four corners in pixels -- top-left,
    // top-right, bottom-right, bottom-left of the uv rect. When set, rect is ignored.
    bool  hasCorners = false;
    float corners[8] = {};
    bool  additive = false;   // blend by adding (glows, sparks) instead of alpha blending
    // The texture: the sprite atlas (default), or one from IRenderer::loadTexture (another atlas an
    // animation uses). Quads with the same texture and blending still go in one draw call.
    uint16_t texture = kSpriteAtlasTexture;
    // Diagnostic-only: which "node" (subsystem) produced this quad. 0 = unspecified.
    // Used by the 4-way split-screen render (TOMS_SPLIT=1) to isolate a stray-sprite bug.
    // 1=stage/map+entities+player, 2=character(player/HUD stat), 3=talk/dialogue, 4=battle/combat.
    uint8_t node = 0;
};

// Node tags (keep in sync with Quad::node comments above).
enum RenderNode : uint8_t { NODE_UNSPEC=0, NODE_STAGE=1, NODE_CHAR=2, NODE_TALK=3, NODE_BATTLE=4, NODE_STORE=5 };

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

// GPU-simulated particles (particle_fx.h, docs/17_PARTICLES.md): an emitter whose particles live
// in GPU buffers. The CPU only spawns (it decides when, where and with which random values, and
// which slot each newborn takes); a compute shader moves every particle, applies the curves and
// writes its quad, which is drawn where drawGpuParticles was called among the sprites.
//
// One spawn record = 5 vec4 (20 floats):
//   [0] pos.x, pos.y, vel.x, vel.y         [2] spin, radial, tangential, first flipbook frame
//   [1] age, life, size, rotation          [3] colour r, g, b, a
//   [4] slot, 0, 0, 0
// A newborn's age is -(steps it waits) * dt: born in the k-th of this draw's steps, it skips k.
struct GpuParticleFrame {
    uint32_t emitter = 0;                     // createGpuParticles
    const float* spawn = nullptr;             // spawnCount records
    uint32_t spawnCount = 0;
    uint32_t steps = 0;                       // simulation steps since the last draw
    float dt = 1.0f / 60.0f;
    float gravity[2] = {0, 0};
    float dragPerStep = 1;                    // velocity *= this each step
    float origin[2] = {0, 0};                 // radial / tangential forces act around this
    bool alignToVelocity = false;
    const float* colorLut = nullptr;          // kLut x rgba over the life (multiplies the colour)
    const float* scalarLut = nullptr;         // kLut x (size, speed, spin, 0) multipliers
    uint32_t frameCount = 1;                  // flipbook frames (1 = the sprite)
    float fps = 0;                            // 0: frames across the life; else loop at fps
    const float* frameUv = nullptr;           // frameCount x (u0, v0, u1, v1)
    const float* frameRect = nullptr;         // frameCount x (x0, y0, x1, y1) for size 1
    float xform[6] = {1, 0, 0, 0, 1, 0};      // particle space -> screen: x' = a x + b y + c, y' = d x + e y + f
    float tint[4] = {1, 1, 1, 1};
    uint16_t texture = kSpriteAtlasTexture;
    bool additive = false;
    static constexpr uint32_t kLut = 64;
    static constexpr uint32_t kMaxFrames = 16;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void init(uint32_t w, uint32_t h) = 0;
    // layers: one RGBA buffer per sprite (already decoded). sw/sh = sprite cell size.
    virtual void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) = 0;
    // A prebuilt sprite atlas page (RGBA8, w x h; tools/atlas). Replaces what loadSprites() made;
    // the game then takes each sprite's UVs from the .atlas file instead of the grid. `file` is the
    // PNG it came from: the UI draws its icons from that same file and borrows this texture instead
    // of loading it again.
    virtual void loadSpriteAtlas(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, const std::string& file) = 0;
    // Another texture (RGBA8, straight alpha, point sampled like the sprite atlas) for Quad::texture,
    // e.g. a second atlas an animation draws from. Returns kSpriteAtlasTexture when it could not be
    // made (the quads then draw from the sprite atlas). releaseTexture frees it.
    virtual uint16_t loadTexture(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) { (void)rgba; (void)w; (void)h; return kSpriteAtlasTexture; }
    virtual void releaseTexture(uint16_t texture) { (void)texture; }
    virtual void begin() = 0;
    virtual void drawSprite(const Quad& q) = 0;
    // Diagnostic: tag subsequent quads with a "node" id (for the 4-way split-screen
    // render). No-op by default; BgfxRenderer stamps it onto each quad.
    virtual void setNode(uint8_t n) { (void)n; }
    // Diagnostic: if n!=0, only emit quads whose node==n (isolate one subsystem).
    virtual void setNodeFilter(uint8_t n) { (void)n; }
    virtual void end() = 0;                       // present to target (offscreen/canvas)
    virtual uint32_t width()  const = 0;
    virtual uint32_t height() const = 0;
    // Desktop-only: dump the current frame to PNG. WebGL build overrides as no-op.
    virtual void savePNG(const std::string& path) { (void)path; }
    // GPU-simulated particles (GpuParticleFrame above): only where the backend runs compute shaders.
    virtual bool supportsGpuParticles() const { return false; }
    virtual uint32_t createGpuParticles(uint32_t capacity) { (void)capacity; return 0; }   // 0 = none
    virtual void releaseGpuParticles(uint32_t emitter) { (void)emitter; }
    // Between begin() and end(): simulates and draws the emitter at this point among the sprites.
    virtual void drawGpuParticles(const GpuParticleFrame& f) { (void)f; }
};
