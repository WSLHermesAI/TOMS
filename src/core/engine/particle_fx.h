// particle_fx.h -- 2D particle effects on the packed sprite atlas(es): data, .particle files,
// simulation, drawing. The plan and the format: docs/engine/07_PARTICLES.md.
//
// A .particle file (JSON) holds effects; an effect is a group of emitters. Every emitter has the
// same fixed sections -- emission (rate + bursts), shape, start values (numbers or [min, max]),
// curves over the particle's life (0..1, the .anim keys and eases), forces, flipbook, render --
// so nothing depends on the order things were added in, and a particle always dies at the end of
// its life. Conventions as in .anim: pixels, y down, degrees, positive = clockwise.
//
//   toms::fx::ParticleFile file;  toms::fx::parseParticles(text, file, &err);
//   toms::fx::EffectInstance fx;  fx.setTransform(toms::anim::placement(x, y));  fx.play(file.find("torch_fire"), seed);
//   each frame:  fx.setTransform(toms::anim::placement(x, y));   // where the effect is
//                fx.update(dtSeconds);
//                fx.appendQuads(atlases, glm::mat3(1.0f), nullptr, quads);
//
// The simulation runs in fixed 1/60 s steps (time left over carries to the next update), and all
// randomness comes from the instance's seed: the same seed and the same updates give the same
// particles, in the editor, in the game and in the screenshot tests.
#pragma once
#include "anim_clip.h"     // Ease, AtlasRef
#include "anim_player.h"   // AtlasSet
#include "render_iface.h"

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace toms::fx {

using toms::anim::Ease;

// A start value: a number, or picked uniformly from [min, max] per particle.
struct Range {
    float m_fMin = 0, m_fMax = 0;
    Range() = default;
    Range(float l_fV) : m_fMin(l_fV), m_fMax(l_fV) {}
    Range(float l_fA, float l_fB) : m_fMin(l_fA), m_fMax(l_fB) {}
    bool f_Fixed() const { return m_fMin == m_fMax; }
    bool operator==(const Range& l_oO) const { return m_fMin == l_oO.m_fMin && m_fMax == l_oO.m_fMax; }
};

// A curve over the particle's normalised life (t = 0..1). Before the first key: the first value;
// after the last: the last; no keys = 1 (curves multiply the start values).
template <class T> struct CurveKey {
    float m_fT = 0;
    T m_oV{};
    Ease m_oEase;   // of the segment that starts here
};
template <class T> struct Curve {
    std::vector<CurveKey<T>> m_vKeys;
    bool f_Empty() const { return m_vKeys.empty(); }
    T f_At(float l_fT, const T& l_oNone) const;
};

// How a particle's colour (src) is combined with the screen (dst):
// result = src * srcFactor + dst * dstFactor. Presets: normal, add, multiply, screen.
struct Blend {
    enum Factor : uint8_t {
        Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DstColor, InvDstColor, DstAlpha, InvDstAlpha,
        FactorCount
    };
    Factor m_eSrc = SrcAlpha, m_eDst = InvSrcAlpha;
    bool operator==(const Blend& l_oO) const { return m_eSrc == l_oO.m_eSrc && m_eDst == l_oO.m_eDst; }
    bool operator!=(const Blend& l_oO) const { return !(*this == l_oO); }
    static Blend f_Normal() { return {SrcAlpha, InvSrcAlpha}; }
    static Blend f_Add() { return {SrcAlpha, One}; }
    static Blend f_Multiply() { return {DstColor, Zero}; }
    static Blend f_Screen() { return {One, InvSrcColor}; }
    std::string f_PresetName() const;              // "" when it is not a preset
    static bool f_FromPreset(const std::string& l_strName, Blend& l_oOut);
    static const char* f_FactorName(Factor l_eF);     // "srcAlpha", ...
    static bool f_FactorFromName(const std::string& l_strName, Factor& l_eOut);
};

struct Shape {
    enum Type { Point, Line, Box, Circle, Ring } m_eType = Point;
    float m_fLength = 0, m_fAngle = 0;        // line: centred, turned by angle (degrees)
    float m_fWidth = 0, m_fHeight = 0;        // box
    float m_fRadius = 0, m_fThickness = 0;    // circle / ring (a ring spawns within thickness inside radius)
    float m_fArcFrom = 0, m_fArcTo = 360;     // circle / ring: the part used, degrees (0 = right, clockwise)
    bool m_bEdge = false;                  // box / circle: only on the outline
    bool m_bOutward = false;               // fly away from the centre (direction is then ignored; spread still applies)
};
const char* f_ShapeName(Shape::Type l_eT);

struct Burst {
    float m_fT = 0;            // emitter time of the first one
    Range m_oCount = 10;
    int m_iRepeat = 0;         // more times after the first (-1 = while the emitter is active)
    float m_fInterval = 0.5f;  // seconds between them
};

struct Emitter {
    std::string m_strName;
    glm::vec2 m_v2Offset{0, 0};     // from the effect's origin
    float m_fStart = 0;            // active from this effect time...
    float m_fStop = 0;             // ...until this one (0 = until the effect ends)
    bool m_bLocalSpace = false;    // false: particles stay where they were born; true: they move with the effect
    int m_iMaxParticles = 100;

    float m_fRate = 10;            // particles per second while active
    std::vector<Burst> m_vBursts;
    Shape m_oShape;

    Range m_oLife = 1;             // seconds
    float m_fDirection = -90;      // degrees; -90 = up
    float m_fSpread = 0;           // +- degrees around the direction
    Range m_oSpeed = 50;           // px / s
    Range m_oSize = 16;            // px, the width; the height follows the sprite's aspect
    Range m_oRotation = 0;         // start angle, degrees
    Range m_oSpin = 0;             // degrees / s
    glm::vec4 m_v4Color{1, 1, 1, 1};
    glm::vec4 m_v4Color2{1, 1, 1, 1};   // a random colour between color and color2 (equal = fixed)
    std::string m_strSprite;             // "atlasId:name" or "name"

    Curve<glm::vec4> m_oColorOverLife;   // multiplies the start colour
    Curve<float> m_oSizeOverLife, m_oSpeedOverLife, m_oSpinOverLife;   // multiply the start values

    glm::vec2 m_v2Gravity{0, 0};    // px / s^2
    float m_fDrag = 0;             // 1 / s: velocity *= exp(-drag * dt)
    Range m_oRadial = 0;           // px / s^2 away from the emitter's origin (negative = towards it)
    Range m_oTangential = 0;       // px / s^2 around it (clockwise)

    std::vector<std::string> m_vFrames;   // flipbook (replaces `sprite` when not empty)
    bool m_bFramesByFps = false;          // false: the frames span the life; true: loop at fps
    float m_fFps = 12;
    bool m_bRandomFrame = false;          // start each particle on a random frame

    Blend m_oBlend;
    int m_iOrder = 0;                     // among the effect's emitters, higher = in front
    bool m_bAlignToVelocity = false;      // turn each particle to its direction of motion (+ rotation)
    bool m_bOldestOnTop = false;          // default: new particles draw over old ones

    // Where the game simulates it (set in the particle editor). Auto: by its size -- on the GPU when
    // maxParticles is above the game's threshold (GameSettings::particleGpuThreshold). Gpu: on the GPU
    // whenever the device can (compute shaders), else the CPU. Cpu: always the CPU.
    enum class Simulation { Auto, Cpu, Gpu };
    Simulation m_eSimulation = Simulation::Auto;
};
const char* f_SimulationName(Emitter::Simulation l_eS);   // "auto", "cpu", "gpu"

struct Effect {
    std::string m_strName;
    float m_fDuration = 0;         // seconds; 0 = endless (until stopped)
    bool m_bLoop = false;          // with a duration: start over at the end (bursts fire again)
    float m_fPrewarm = 0;          // seconds simulated before the first frame
    uint32_t m_uSeed = 0;          // 0 = the instance picks one
    std::vector<Emitter> m_vEmitters;
};

struct ParticleFile {
    int m_iVersion = 1;
    std::vector<toms::anim::AtlasRef> m_vAtlases;   // as .anim: paths relative to the file, ids for "id:name"
    std::vector<Effect> m_vEffects;
    const Effect* f_Find(const std::string& l_strName) const;
};

bool f_ParseParticles(const std::string& l_strJson, ParticleFile& l_oOut, std::string* l_pErr = nullptr);
std::string f_ParticlesToJson(const ParticleFile& l_oF);

// Problems a file can have without being unreadable (unsorted keys, t outside 0..1, a stop before
// the start, bursts bigger than maxParticles, ...). With atlases: also sprites no atlas has.
// Each is "effect/emitter: text"; `warning` marks the ones that are not errors.
struct Problem { std::string m_strWhere, m_strText; bool m_bWarning = false; };
std::vector<Problem> f_CheckParticles(const ParticleFile& l_oF, const toms::anim::AtlasSet* l_pAtlases = nullptr);

// One playing effect.
class EffectInstance {
public:
    static constexpr float m_fStep = 1.0f / 60.0f;   // simulation step
    static constexpr int m_iMaxSteps = 8;             // per update (a long hitch skips time instead)

    // From time 0 (then the effect's prewarm). seed 0: the effect's seed, else a new one each play.
    // Call setTransform first: the prewarm already makes world-space particles where the effect is.
    void f_Play(const Effect* l_pEffect, uint32_t l_uSeed = 0);
    void f_Stop(bool l_bClear = false);     // no more particles; clear = remove the live ones too
    void f_Update(float l_fDt);             // seconds
    // Effect time t from a fresh play with the same seed (the editor's scrubbing). With a prewarm
    // the effect starts at that time, so an earlier t stays there.
    void f_Seek(float l_fT);

    // Where the effect is: its origin, rotation and scale (anim::placement). World-space
    // emitters only use it when a particle is born; local-space ones every frame.
    void f_SetTransform(const glm::mat3& l_mat3M) { m_mat3Transform = l_mat3M; }
    const glm::mat3& f_Transform() const { return m_mat3Transform; }

    const Effect* f_Effect() const { return m_pEffect; }
    float f_Time() const { return m_fTime; }
    uint32_t f_Seed() const { return m_uSeed; }
    bool f_Emitting() const;             // still making particles
    bool f_Finished() const;             // not emitting and nothing alive
    int f_LiveCount() const;
    int f_LiveCount(size_t l_nEmitter) const;

    // The quads in draw order (emitters by order, then by their place in the list). placement
    // is applied after the transform (e.g. the camera); tint multiplies every colour. Only the
    // CPU-simulated emitters: draw() is what also draws the GPU ones.
    void f_AppendQuads(const toms::anim::AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, const float l_arrTint[4],
                     std::vector<Quad>& l_vOut, std::vector<std::string>* l_pMissing = nullptr) const;

    // GPU simulation, decided per emitter in play() (so call this first), where `ren` has compute
    // shaders (IRenderer::supportsGpuParticles): Emitter::simulation "gpu" = on the GPU, "cpu" = never,
    // "auto" = when its maxParticles is above `threshold` (0 = never). The CPU still spawns (the same particles as on the
    // CPU, from the same seed); the GPU moves them, applies the curves and builds their quads.
    // The game's setting: GameSettings::particleGpuThreshold (desktop 5000, phones 3000).
    void f_SetGpuSimulation(IRenderer* l_pRen, int l_iThreshold) { m_pGpuRen = l_pRen; m_iGpuThreshold = l_iThreshold; }
    int f_GpuEmitters() const;   // emitters simulated on the GPU now
    bool f_GpuEmitter(size_t l_nEmitter) const { return l_nEmitter < m_vGpu.size() && m_vGpu[l_nEmitter] != nullptr; }
    // Draws everything through `ren`, in draw order: CPU emitters as quads (as appendQuads), GPU
    // ones with IRenderer::drawGpuParticles (which also runs their simulation steps since the last draw).
    void f_Draw(IRenderer* l_pRen, const toms::anim::AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, const float l_arrTint[4],
              std::vector<std::string>* l_pMissing = nullptr);

private:
    // One GPU-simulated emitter: the renderer's buffers, which slots are free, when each used slot
    // frees (a particle's life is fixed at birth, so the CPU knows without reading anything back),
    // the newborns since the last draw, and the curves baked into tables.
    struct Gpu {
        IRenderer* m_pRen = nullptr;
        uint32_t m_uId = 0;
        int m_iCapacity = 0;
        std::vector<int> m_vFreeSlots;   // a stack
        std::priority_queue<std::pair<long long, int>, std::vector<std::pair<long long, int>>, std::greater<>> m_oFrees;   // (step, slot)
        std::vector<float> m_vSpawn;     // GpuParticleFrame spawn records
        uint32_t m_uPendingSteps = 0;
        std::vector<float> m_vColorLut, m_vScalarLut;
        ~Gpu() { if (m_pRen && m_uId) m_pRen->f_ReleaseGpuParticles(m_uId); }
    };
    void f_StartGpu(size_t l_nEmitter);
    void f_AppendEmitterQuads(size_t l_nEmitter, const toms::anim::AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, const glm::vec4& l_v4Tint,
                            std::vector<Quad>& l_vOut, std::vector<std::string>* l_pMissing) const;
    struct Pool {   // structure of arrays, in birth order (oldest first)
        std::vector<glm::vec2> m_vPos, m_vVel;
        std::vector<float> m_vAge, m_vLife, m_vSize, m_vRot, m_vSpin, m_vRadial, m_vTangential;
        std::vector<glm::vec4> m_vColor;
        std::vector<int> m_vFrame;
        int m_iCount = 0;
        double m_dSpawnCarry = 0;     // fraction of a particle owed by the rate
        std::vector<int> m_vBurstsDone;
        void f_Reserve(int l_iN);
    };
    void f_Step(float l_fDt);
    void f_Spawn(size_t l_nE, int l_iN);   // (not "emit": Qt defines that as a macro)
    uint32_t f_Next();
    float f_Rand01();
    float f_Pick(const Range& l_oR) { return l_oR.f_Fixed() ? l_oR.m_fMin : l_oR.m_fMin + (l_oR.m_fMax - l_oR.m_fMin) * f_Rand01(); }

    const Effect* m_pEffect = nullptr;
    std::vector<Pool> m_vPools;
    std::vector<size_t> m_vDrawOrder;
    glm::mat3 m_mat3Transform{1.0f};
    float m_fTime = 0;          // effect time (wraps when it loops)
    float m_fCarry = 0;         // update time not simulated yet
    uint32_t m_uSeed = 0, m_uRng = 0;
    bool m_bStopped = false;
    IRenderer* m_pGpuRen = nullptr;
    int m_iGpuThreshold = 0;
    std::vector<std::shared_ptr<Gpu>> m_vGpu;   // per emitter; null = simulated on the CPU
    long long m_i64StepIndex = 0;                 // steps since play()
};

}  // namespace toms::fx
