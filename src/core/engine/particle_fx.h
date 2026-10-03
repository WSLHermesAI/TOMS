// particle_fx.h -- 2D particle effects on the packed sprite atlas(es): data, .particle files,
// simulation, drawing. The plan and the format: docs/17_PARTICLES.md.
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
#include <string>
#include <vector>

namespace toms::fx {

using toms::anim::Ease;

// A start value: a number, or picked uniformly from [min, max] per particle.
struct Range {
    float min = 0, max = 0;
    Range() = default;
    Range(float v) : min(v), max(v) {}
    Range(float a, float b) : min(a), max(b) {}
    bool fixed() const { return min == max; }
    bool operator==(const Range& o) const { return min == o.min && max == o.max; }
};

// A curve over the particle's normalised life (t = 0..1). Before the first key: the first value;
// after the last: the last; no keys = 1 (curves multiply the start values).
template <class T> struct CurveKey {
    float t = 0;
    T v{};
    Ease ease;   // of the segment that starts here
};
template <class T> struct Curve {
    std::vector<CurveKey<T>> keys;
    bool empty() const { return keys.empty(); }
    T at(float t, const T& none) const;
};

// How a particle's colour (src) is combined with the screen (dst):
// result = src * srcFactor + dst * dstFactor. Presets: normal, add, multiply, screen.
struct Blend {
    enum Factor : uint8_t {
        Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DstColor, InvDstColor, DstAlpha, InvDstAlpha,
        FactorCount
    };
    Factor src = SrcAlpha, dst = InvSrcAlpha;
    bool operator==(const Blend& o) const { return src == o.src && dst == o.dst; }
    bool operator!=(const Blend& o) const { return !(*this == o); }
    static Blend normal() { return {SrcAlpha, InvSrcAlpha}; }
    static Blend add() { return {SrcAlpha, One}; }
    static Blend multiply() { return {DstColor, Zero}; }
    static Blend screen() { return {One, InvSrcColor}; }
    std::string presetName() const;              // "" when it is not a preset
    static bool fromPreset(const std::string& name, Blend& out);
    static const char* factorName(Factor f);     // "srcAlpha", ...
    static bool factorFromName(const std::string& name, Factor& out);
};

struct Shape {
    enum Type { Point, Line, Box, Circle, Ring } type = Point;
    float length = 0, angle = 0;        // line: centred, turned by angle (degrees)
    float width = 0, height = 0;        // box
    float radius = 0, thickness = 0;    // circle / ring (a ring spawns within thickness inside radius)
    float arcFrom = 0, arcTo = 360;     // circle / ring: the part used, degrees (0 = right, clockwise)
    bool edge = false;                  // box / circle: only on the outline
    bool outward = false;               // fly away from the centre (direction is then ignored; spread still applies)
};
const char* shapeName(Shape::Type t);

struct Burst {
    float t = 0;            // emitter time of the first one
    Range count = 10;
    int repeat = 0;         // more times after the first (-1 = while the emitter is active)
    float interval = 0.5f;  // seconds between them
};

struct Emitter {
    std::string name;
    glm::vec2 offset{0, 0};     // from the effect's origin
    float start = 0;            // active from this effect time...
    float stop = 0;             // ...until this one (0 = until the effect ends)
    bool localSpace = false;    // false: particles stay where they were born; true: they move with the effect
    int maxParticles = 100;

    float rate = 10;            // particles per second while active
    std::vector<Burst> bursts;
    Shape shape;

    Range life = 1;             // seconds
    float direction = -90;      // degrees; -90 = up
    float spread = 0;           // +- degrees around the direction
    Range speed = 50;           // px / s
    Range size = 16;            // px, the width; the height follows the sprite's aspect
    Range rotation = 0;         // start angle, degrees
    Range spin = 0;             // degrees / s
    glm::vec4 color{1, 1, 1, 1};
    glm::vec4 color2{1, 1, 1, 1};   // a random colour between color and color2 (equal = fixed)
    std::string sprite;             // "atlasId:name" or "name"

    Curve<glm::vec4> colorOverLife;   // multiplies the start colour
    Curve<float> sizeOverLife, speedOverLife, spinOverLife;   // multiply the start values

    glm::vec2 gravity{0, 0};    // px / s^2
    float drag = 0;             // 1 / s: velocity *= exp(-drag * dt)
    Range radial = 0;           // px / s^2 away from the emitter's origin (negative = towards it)
    Range tangential = 0;       // px / s^2 around it (clockwise)

    std::vector<std::string> frames;   // flipbook (replaces `sprite` when not empty)
    bool framesByFps = false;          // false: the frames span the life; true: loop at fps
    float fps = 12;
    bool randomFrame = false;          // start each particle on a random frame

    Blend blend;
    int order = 0;                     // among the effect's emitters, higher = in front
    bool alignToVelocity = false;      // turn each particle to its direction of motion (+ rotation)
    bool oldestOnTop = false;          // default: new particles draw over old ones
};

struct Effect {
    std::string name;
    float duration = 0;         // seconds; 0 = endless (until stopped)
    bool loop = false;          // with a duration: start over at the end (bursts fire again)
    float prewarm = 0;          // seconds simulated before the first frame
    uint32_t seed = 0;          // 0 = the instance picks one
    std::vector<Emitter> emitters;
};

struct ParticleFile {
    int version = 1;
    std::vector<toms::anim::AtlasRef> atlases;   // as .anim: paths relative to the file, ids for "id:name"
    std::vector<Effect> effects;
    const Effect* find(const std::string& name) const;
};

bool parseParticles(const std::string& json, ParticleFile& out, std::string* err = nullptr);
std::string particlesToJson(const ParticleFile& f);

// Problems a file can have without being unreadable (unsorted keys, t outside 0..1, a stop before
// the start, bursts bigger than maxParticles, ...). With atlases: also sprites no atlas has.
// Each is "effect/emitter: text"; `warning` marks the ones that are not errors.
struct Problem { std::string where, text; bool warning = false; };
std::vector<Problem> checkParticles(const ParticleFile& f, const toms::anim::AtlasSet* atlases = nullptr);

// One playing effect.
class EffectInstance {
public:
    static constexpr float kStep = 1.0f / 60.0f;   // simulation step
    static constexpr int kMaxSteps = 8;             // per update (a long hitch skips time instead)

    // From time 0 (then the effect's prewarm). seed 0: the effect's seed, else a new one each play.
    // Call setTransform first: the prewarm already makes world-space particles where the effect is.
    void play(const Effect* effect, uint32_t seed = 0);
    void stop(bool clear = false);     // no more particles; clear = remove the live ones too
    void update(float dt);             // seconds
    // Effect time t from a fresh play with the same seed (the editor's scrubbing).
    void seek(float t);

    // Where the effect is: its origin, rotation and scale (anim::placement). World-space
    // emitters only use it when a particle is born; local-space ones every frame.
    void setTransform(const glm::mat3& m) { transform_ = m; }
    const glm::mat3& transform() const { return transform_; }

    const Effect* effect() const { return effect_; }
    float time() const { return time_; }
    uint32_t seed() const { return seed_; }
    bool emitting() const;             // still making particles
    bool finished() const;             // not emitting and nothing alive
    int liveCount() const;
    int liveCount(size_t emitter) const;

    // The quads in draw order (emitters by order, then by their place in the list). placement
    // is applied after the transform (e.g. the camera); tint multiplies every colour.
    void appendQuads(const toms::anim::AtlasSet& atlases, const glm::mat3& placement, const float tint[4],
                     std::vector<Quad>& out, std::vector<std::string>* missing = nullptr) const;

private:
    struct Pool {   // structure of arrays, in birth order (oldest first)
        std::vector<glm::vec2> pos, vel;
        std::vector<float> age, life, size, rot, spin, radial, tangential;
        std::vector<glm::vec4> color;
        std::vector<int> frame;
        int count = 0;
        double spawnCarry = 0;     // fraction of a particle owed by the rate
        std::vector<int> burstsDone;
        void reserve(int n);
    };
    void step(float dt);
    void emit(size_t e, int n);
    uint32_t next();
    float rand01();
    float pick(const Range& r) { return r.fixed() ? r.min : r.min + (r.max - r.min) * rand01(); }

    const Effect* effect_ = nullptr;
    std::vector<Pool> pools_;
    std::vector<size_t> drawOrder_;
    glm::mat3 transform_{1.0f};
    float time_ = 0;          // effect time (wraps when it loops)
    float carry_ = 0;         // update time not simulated yet
    uint32_t seed_ = 0, rng_ = 0;
    bool stopped_ = false;
};

}  // namespace toms::fx
