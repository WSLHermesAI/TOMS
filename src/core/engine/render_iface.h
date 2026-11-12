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
inline constexpr float g_arrBackgroundClearColor[4] = {0.06f, 0.06f, 0.10f, 1.0f};

// Quad::texture of the sprite atlas (BgfxRenderer::loadSprites / loadSpriteAtlas).
inline constexpr uint16_t g_u16SpriteAtlasTexture = 0xFFFF;

struct Quad {
    float m_arrRect[4];   // x,y,w,h in pixels (dst)
    float m_arrUv[4];     // u0,v0,u1,v1 (src atlas)
    float m_arrTint[4];   // rgba
    bool  m_bSolid = false;  // true => draw flat tint, ignore the texture (solid-color rect)
    // Rotated / scaled / mirrored sprites (anim_player.h): the four corners in pixels -- top-left,
    // top-right, bottom-right, bottom-left of the uv rect. When set, rect is ignored.
    bool  m_bHasCorners = false;
    float m_arrCorners[8] = {};
    bool  m_bAdditive = false;   // blend by adding (glows, sparks) instead of alpha blending
    // The texture: the sprite atlas (default), or one from IRenderer::loadTexture (another atlas an
    // animation uses). Quads with the same texture and blending still go in one draw call.
    uint16_t m_u16Texture = g_u16SpriteAtlasTexture;
    // Diagnostic-only: which "node" (subsystem) produced this quad. 0 = unspecified.
    // Used by the 4-way split-screen render (TOMS_SPLIT=1) to isolate a stray-sprite bug.
    // 1=stage/map+entities+player, 2=character(player/HUD stat), 3=talk/dialogue, 4=battle/combat.
    uint8_t m_u8Node = 0;
};

// A textured triangle mesh (docs/engine/16_MESH_ANIMATION.md): a sprite cut into triangles whose corners can
// move on their own -- what a skinned or deformed (morphing) sprite is drawn with. Drawn in submission
// order among the quads, from the same textures, with the same blending.
struct MeshVertex {
    float m_fX, m_fY;   // pixels (dst)
    float m_fU, m_fV;   // the texture (src atlas)
};
struct Mesh {
    const MeshVertex* m_pVertices = nullptr;
    uint32_t m_uVertexCount = 0;              // at most 65536 (16-bit indices)
    const uint16_t* m_pIndices = nullptr;     // three per triangle, into vertices
    uint32_t m_uIndexCount = 0;
    float m_arrTint[4] = {1, 1, 1, 1};          // rgba, the whole mesh
    const float* m_pColors = nullptr;         // optional: rgba per vertex (times tint) -- quads batched into a mesh keep theirs
    bool m_bAdditive = false;
    uint16_t m_u16Texture = g_u16SpriteAtlasTexture;   // as Quad::texture
};
// What IRenderer::drawMesh draws: whole triangles, every index naming a vertex, 16-bit indices.
inline bool f_ValidMesh(const Mesh& l_oM) {
    if (!l_oM.m_pVertices || !l_oM.m_pIndices || l_oM.m_uVertexCount == 0 || l_oM.m_uVertexCount > 65536u) return false;
    if (l_oM.m_uIndexCount == 0 || l_oM.m_uIndexCount % 3 != 0) return false;
    for (uint32_t l_uI = 0; l_uI < l_oM.m_uIndexCount; l_uI++)
        if (l_oM.m_pIndices[l_uI] >= l_oM.m_uVertexCount) return false;
    return true;
}

// A layer's effect (IRenderer::beginLayer, docs/engine/18_NODE_EFFECTS.md): everything drawn between beginLayer and
// endLayer -- an .anim node and its whole subtree -- is drawn into a picture of its own (HDR: values above 1 are kept),
// and that picture is drawn into what is under it through the effect's shader. Lengths are design pixels.
struct LayerEffect {
    enum Kind : uint8_t {
        None,         // the picture as it is (a group drawn as one: its own alpha over what is under it)
        MotionBlur,   // params: blur vector x, y (the picture smeared along it, centred)
        Glow,         // params: intensity (x the colours, > 1 = HDR), bloom radius, bloom threshold, bloom amount;
                      // color: the bloom's colour
        Flash,        // params: amount 0..1 the picture turns to `color` (its alpha kept)
        Outline,      // params: width, intensity (> 1 = HDR); color: the outline's colour, drawn around the alpha
        Count
    };
    Kind m_eKind = None;
    float m_arrParams[4] = {0, 0, 0, 0};
    float m_arrColor[4] = {1, 1, 1, 1};
    float m_fOpacity = 1;   // the whole picture's alpha
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
inline bool f_PackAtlas(const std::vector<std::vector<uint8_t>>& l_vLayers,
                      uint32_t l_uSw, uint32_t l_uSh, uint32_t l_uCols,
                      std::vector<uint8_t>& l_vOut, uint32_t& l_uOutW, uint32_t& l_uOutH) {
    if (l_vLayers.empty()) return false;
    for (auto& l_vL : l_vLayers) if (l_vL.size() != l_uSw*l_uSh*4) return false;
    uint32_t l_uRows = (uint32_t)((l_vLayers.size() + l_uCols - 1) / l_uCols);
    l_uOutW = l_uCols * l_uSw; l_uOutH = l_uRows * l_uSh;
    l_vOut.assign((size_t)l_uOutW * l_uOutH * 4, 0);
    for (size_t l_nI = 0; l_nI < l_vLayers.size(); l_nI++) {
        uint32_t l_uGx = (uint32_t)(l_nI % l_uCols), l_uGy = (uint32_t)(l_nI / l_uCols);
        const uint8_t* l_pSrc = l_vLayers[l_nI].data();
        for (uint32_t l_uY = 0; l_uY < l_uSh; l_uY++)
            for (uint32_t l_uX = 0; l_uX < l_uSw; l_uX++) {
                uint32_t l_uAx = l_uGx*l_uSw + l_uX, l_uAy = l_uGy*l_uSh + l_uY;
                const uint8_t* l_pS = l_pSrc + (size_t)(l_uY*l_uSw + l_uX)*4;
                uint8_t* l_pD = l_vOut.data() + (size_t)(l_uAy*l_uOutW + l_uAx)*4;
                l_pD[0]=l_pS[0]; l_pD[1]=l_pS[1]; l_pD[2]=l_pS[2]; l_pD[3]=l_pS[3];
            }
    }
    return true;
}

// GPU-simulated particles (particle_fx.h, docs/engine/07_PARTICLES.md): an emitter whose particles live
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
    uint32_t m_uEmitter = 0;                     // createGpuParticles
    const float* m_pSpawn = nullptr;             // spawnCount records
    uint32_t m_uSpawnCount = 0;
    uint32_t m_uSteps = 0;                       // simulation steps since the last draw
    float m_fDt = 1.0f / 60.0f;
    float m_arrGravity[2] = {0, 0};
    float m_fDragPerStep = 1;                    // velocity *= this each step
    float m_arrOrigin[2] = {0, 0};                 // radial / tangential forces act around this
    bool m_bAlignToVelocity = false;
    const float* m_pColorLut = nullptr;          // kLut x rgba over the life (multiplies the colour)
    const float* m_pScalarLut = nullptr;         // kLut x (size, speed, spin, 0) multipliers
    uint32_t m_uFrameCount = 1;                  // flipbook frames (1 = the sprite)
    float m_fFps = 0;                            // 0: frames across the life; else loop at fps
    const float* m_pFrameUv = nullptr;           // frameCount x (u0, v0, u1, v1)
    const float* m_pFrameRect = nullptr;         // frameCount x (x0, y0, x1, y1) for size 1
    float m_arrXform[6] = {1, 0, 0, 0, 1, 0};      // particle space -> screen: x' = a x + b y + c, y' = d x + e y + f
    float m_arrTint[4] = {1, 1, 1, 1};
    uint16_t m_u16Texture = g_u16SpriteAtlasTexture;
    bool m_bAdditive = false;
    static constexpr uint32_t m_uLut = 64;
    static constexpr uint32_t m_uMaxFrames = 16;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void f_Init(uint32_t l_uW, uint32_t l_uH) = 0;
    // layers: one RGBA buffer per sprite (already decoded). sw/sh = sprite cell size.
    virtual void f_LoadSprites(const std::vector<std::vector<uint8_t>>& l_vLayers, uint32_t l_uSw, uint32_t l_uSh) = 0;
    // A prebuilt sprite atlas page (RGBA8, w x h; tools/atlas). Replaces what loadSprites() made;
    // the game then takes each sprite's UVs from the .atlas file instead of the grid. `file` is the
    // PNG it came from: the UI draws its icons from that same file and borrows this texture instead
    // of loading it again.
    virtual void f_LoadSpriteAtlas(const std::vector<uint8_t>& l_vRgba, uint32_t l_uW, uint32_t l_uH, const std::string& l_strFile) = 0;
    // Another texture (RGBA8, straight alpha, point sampled like the sprite atlas) for Quad::texture,
    // e.g. a second atlas an animation draws from. Returns kSpriteAtlasTexture when it could not be
    // made (the quads then draw from the sprite atlas). releaseTexture frees it.
    virtual uint16_t f_LoadTexture(const std::vector<uint8_t>& l_vRgba, uint32_t l_uW, uint32_t l_uH) { (void)l_vRgba; (void)l_uW; (void)l_uH; return g_u16SpriteAtlasTexture; }
    virtual void f_ReleaseTexture(uint16_t l_u16Texture) { (void)l_u16Texture; }
    // How a loadTexture texture is sampled: point (the default, pixel art) or linear (painted art that turns and scales:
    // an atlas page with "filter: Linear").
    virtual void f_SetTextureFilter(uint16_t l_u16Texture, bool l_bLinear) { (void)l_u16Texture; (void)l_bLinear; }
    virtual void begin() = 0;
    virtual void f_DrawSprite(const Quad& l_oQ) = 0;
    // A triangle mesh (Mesh above), between the quads drawn before and after it. The data is copied.
    // A mesh with an index out of range, an index count that is not a multiple of 3, or more than
    // 65536 vertices is not drawn. No-op by default (a backend without meshes).
    virtual void f_DrawMesh(const Mesh& l_oM) { (void)l_oM; }
    // Diagnostic: tag subsequent quads with a "node" id (for the 4-way split-screen
    // render). No-op by default; BgfxRenderer stamps it onto each quad.
    virtual void f_SetNode(uint8_t l_u8N) { (void)l_u8N; }
    // Where the next sprites go: 0 = the game layer (under the UI, the default after begin()), 1 = the top layer, drawn
    // over the UI (a hint on a battle button). Only quads: meshes and GPU particles stay in the game layer.
    virtual void f_SetLayer(int l_iLayer) { (void)l_iLayer; }
    // A layer with an effect (LayerEffect above): the quads, meshes and GPU particles drawn until the matching endLayer
    // go into a picture of their own, drawn with the effect where beginLayer was called. Layers nest (a layer's picture is
    // drawn into the layer around it). Without layer support (the default) everything is drawn straight through, as if
    // there were no layer. Only in the game layer (setLayer(0)).
    virtual void f_BeginLayer(const LayerEffect& l_oFx) { (void)l_oFx; }
    virtual void f_EndLayer() {}
    // A world group (docs/gameplay/21 §4.3: a 2D fighter as an object of the 3D stage): the quads and meshes drawn until
    // f_EndWorld are not drawn on the screen but placed in the host's 3D scene -- each design pixel (x, y) goes to the
    // world point mat4 x (x, y, 0, 1) (16 floats, column major, metres) -- and the host draws them there, lit by the
    // scene's lights (lit) or as they are (not lit), depth tested against it. Nothing is cut: there is no picture in
    // between. Inside a group, layers are drawn straight through (no effect) and GPU particles are not drawn.
    // f_SupportsWorld false: the group is drawn on the screen as before.
    virtual bool f_SupportsWorld() const { return false; }
    virtual void f_BeginWorld(const float* l_pMat4, bool l_bLit) { (void)l_pMat4; (void)l_bLit; }
    virtual void f_EndWorld() {}
    // Diagnostic: if n!=0, only emit quads whose node==n (isolate one subsystem).
    virtual void f_SetNodeFilter(uint8_t l_u8N) { (void)l_u8N; }
    virtual void end() = 0;                       // present to target (offscreen/canvas)
    virtual uint32_t f_Width()  const = 0;
    virtual uint32_t f_Height() const = 0;
    // Desktop-only: dump the current frame to PNG. WebGL build overrides as no-op.
    virtual void f_SavePNG(const std::string& l_strPath) { (void)l_strPath; }
    // GPU-simulated particles (GpuParticleFrame above): only where the backend runs compute shaders.
    virtual bool f_SupportsGpuParticles() const { return false; }
    virtual uint32_t f_CreateGpuParticles(uint32_t l_uCapacity) { (void)l_uCapacity; return 0; }   // 0 = none
    virtual void f_ReleaseGpuParticles(uint32_t l_uEmitter) { (void)l_uEmitter; }
    // Between begin() and end(): simulates and draws the emitter at this point among the sprites.
    virtual void f_DrawGpuParticles(const GpuParticleFrame& l_oF) { (void)l_oF; }
};
