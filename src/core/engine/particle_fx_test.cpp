// particle_fx_test.cpp — the particle runtime (particle_fx.h), headless.
//
//   json       round trip of every field, defaults, unknown fields and bad values rejected, blend
//   emission   rate, bursts (repeat, interval), maxParticles cap, emitter start/stop, effect end, loop
//   motion     speed and direction, gravity, drag, radial/tangential, curves over life, spin
//   space      world-space particles stay where they were born, local-space ones follow the effect
//   quads      size from the sprite's aspect, rotation, colour curves, flipbook frames, draw order
//   seed       same seed = same particles; seek(t) = play + updates; a different seed differs
//   check      checkParticles errors and warnings
//   examples   docs/examples/fx_recipes.particle parses and checks clean against its atlases
//
// Run: ./particle_fx_test   (in assets/, like the other unit tests)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "particle_fx.h"

static int g_checks = 0, g_fails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_fails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::fx;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// A 64x64 page: "dot" 16x16, "wide" 32x16 (2:1), "f0".."f2" flipbook frames.
toms::AtlasFile makeAtlas() {
    toms::AtlasFile a;
    std::string err;
    toms::parseAtlas("fx.png\nsize: 64, 64\n"
                     "dot\n  bounds: 0, 0, 16, 16\n"
                     "wide\n  bounds: 16, 0, 32, 16\n"
                     "f0\n  bounds: 0, 16, 8, 8\nf1\n  bounds: 8, 16, 8, 8\nf2\n  bounds: 16, 16, 8, 8\n",
                     a, &err);
    a.computeUVs();
    return a;
}

ParticleFile parse(const std::string& text) {
    ParticleFile f;
    std::string err;
    CHECK(parseParticles(text, f, &err), "parse: %s", err.c_str());
    return f;
}

glm::vec2 centre(const Quad& q) { return {(q.corners[0] + q.corners[4]) / 2, (q.corners[1] + q.corners[5]) / 2}; }

void testJson() {
    const std::string full = R"({
      "version": 1,
      "atlases": ["../atlas/game.atlas", {"id": "fx", "path": "../atlas/other/fx.atlas"}],
      "effects": [{
        "name": "all", "duration": 2, "loop": true, "prewarm": 0.5, "seed": 42,
        "emitters": [{
          "name": "e", "offset": [3, 4], "start": 0.1, "stop": 1.5, "space": "local", "maxParticles": 50,
          "emission": {"rate": 20, "bursts": [{"t": 0, "count": [5, 8]}, {"t": 0.5, "count": 3, "repeat": 2, "interval": 0.25}]},
          "shape": {"type": "ring", "radius": 10, "thickness": 4, "arc": [0, 180], "outward": true},
          "life": [0.5, 1], "direction": 45, "spread": 10, "speed": [10, 20], "size": 12,
          "rotation": [0, 90], "spin": -30, "color": [[1, 0, 0, 1], [1, 1, 0, 1]], "sprite": "fx:dot",
          "overLife": {"color": [{"t": 0, "v": [1, 1, 1, 0], "ease": "quadraticOut"}, {"t": 1, "v": [1, 1, 1, 1], "ease": [0.1, 0.2, 0.3, 0.4]}],
                       "size": [{"t": 0, "v": 0.5}, {"t": 1, "v": 2}], "speed": [{"t": 0, "v": 1}], "spin": [{"t": 1, "v": 0}]},
          "forces": {"gravity": [0, 30], "drag": 0.5, "radial": [1, 2], "tangential": 3},
          "flipbook": {"frames": ["f0", "f1"], "mode": "fps", "fps": 8, "randomStart": true},
          "render": {"blend": {"src": "one", "dst": "invSrcColor"}, "order": 2, "alignToVelocity": true, "oldestOnTop": true}
        }]
      }]})";
    ParticleFile f = parse(full);
    const std::string once = particlesToJson(f);
    ParticleFile g = parse(once);
    CHECK(particlesToJson(g) == once, "round trip is stable");
    const Emitter& e = g.effects[0].emitters[0];
    CHECK(g.atlases.size() == 2 && g.atlases[1].id == "fx", "atlases with ids");
    CHECK(g.effects[0].loop && g.effects[0].seed == 42 && near(g.effects[0].prewarm, 0.5f), "effect fields");
    CHECK(e.localSpace && e.maxParticles == 50 && e.bursts.size() == 2 && e.bursts[1].repeat == 2, "emitter fields");
    CHECK(e.shape.type == Shape::Ring && e.shape.outward && near(e.shape.arcTo, 180), "shape");
    CHECK(e.color == glm::vec4(1, 0, 0, 1) && e.color2 == glm::vec4(1, 1, 0, 1), "two colours");
    CHECK(e.colorOverLife.keys.size() == 2 && e.colorOverLife.keys[1].ease.kind == Ease::kBezier, "bezier ease kept");
    CHECK(e.frames.size() == 2 && e.framesByFps && e.randomFrame, "flipbook");
    CHECK(e.blend == Blend::screen() && e.blend.presetName() == "screen", "custom factors that equal a preset");
    CHECK(once.find("\"blend\": \"screen\"") != std::string::npos, "written as the preset name");

    // Defaults.
    ParticleFile d = parse(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "sprite": "dot"}]}]})");
    const Emitter& m = d.effects[0].emitters[0];
    CHECK(m.rate == 10 && m.life.min == 1 && m.direction == -90 && m.size.min == 16 && m.blend == Blend::normal() &&
              m.maxParticles == 100 && !m.localSpace,
          "defaults");
    CHECK(particlesToJson(d).find("\"rate\"") == std::string::npos, "defaults are not written");

    // Mistakes are errors, with where they are.
    std::string err;
    ParticleFile bad;
    CHECK(!parseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "colour": [1,1,1,1]}]}]})", bad, &err) &&
              err.find("colour") != std::string::npos && err.find("x/") != std::string::npos,
          "unknown field reported with its place: %s", err.c_str());
    CHECK(!parseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "render": {"blend": "glow"}}]}]})", bad, &err),
          "unknown blend preset");
    CHECK(!parseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "life": [1]}]}]})", bad, &err),
          "a range needs [min, max]");
    CHECK(!parseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "overLife": {"size": [{"t": 0, "v": 1, "ease": "wobbly"}]}}]}]})",
                          bad, &err),
          "unknown ease");
    Blend b;
    CHECK(Blend::fromPreset("multiply", b) && b == Blend::multiply() && !Blend::fromPreset("x", b), "presets");
}

// Count how many particles an effect has after `seconds` of 1/60 updates.
int liveAfter(const Effect& e, float seconds, uint32_t seed = 7) {
    EffectInstance fx;
    fx.play(&e, seed);
    for (int i = 0; i < (int)std::lround(seconds * 60); i++) fx.update(1.0f / 60);
    return fx.liveCount();
}

void testEmission() {
    ParticleFile g = parse(R"({"effects": [
      {"name": "rate", "emitters": [{"name": "m", "sprite": "dot", "life": 100, "emission": {"rate": 10}}]},
      {"name": "burst", "emitters": [{"name": "m", "sprite": "dot", "life": 100,
         "emission": {"rate": 0, "bursts": [{"t": 0.5, "count": 5, "repeat": 2, "interval": 0.25}]}}]},
      {"name": "cap", "emitters": [{"name": "m", "sprite": "dot", "life": 100, "maxParticles": 7, "emission": {"rate": 100}}]},
      {"name": "window", "emitters": [{"name": "m", "sprite": "dot", "life": 100, "start": 0.5, "stop": 1.0, "emission": {"rate": 10}}]},
      {"name": "oneshot", "duration": 0.5, "emitters": [{"name": "m", "sprite": "dot", "life": 0.25, "emission": {"rate": 40}}]},
      {"name": "loop", "duration": 1, "loop": true, "emitters": [{"name": "m", "sprite": "dot", "life": 100,
         "emission": {"rate": 0, "bursts": [{"t": 0, "count": 4}]}}]}
    ]})");
    CHECK(liveAfter(*g.find("rate"), 1.0f) == 10, "rate 10/s: 10 after 1 s (%d)", liveAfter(*g.find("rate"), 1.0f));
    CHECK(liveAfter(*g.find("rate"), 2.5f) == 25, "25 after 2.5 s");
    const Effect& burst = *g.find("burst");
    CHECK(liveAfter(burst, 0.45f) == 0 && liveAfter(burst, 0.55f) == 5 && liveAfter(burst, 0.8f) == 10 &&
              liveAfter(burst, 1.1f) == 15 && liveAfter(burst, 3.0f) == 15,
          "burst at 0.5 + 2 repeats every 0.25 s: 0, 5, 10, 15, then no more");
    CHECK(liveAfter(*g.find("cap"), 2.0f) == 7, "maxParticles caps the pool");
    CHECK(liveAfter(*g.find("window"), 0.45f) == 0 && liveAfter(*g.find("window"), 3.0f) == 5, "start 0.5 / stop 1.0: 5 particles");
    {
        EffectInstance fx;
        fx.play(g.find("oneshot"), 3);
        bool sawEmitting = false;
        for (int i = 0; i < 30; i++) { fx.update(1.0f / 60); sawEmitting |= fx.emitting(); }
        CHECK(sawEmitting && !fx.emitting() && fx.liveCount() > 0 && !fx.finished(), "after its duration: no more particles, the last still live");
        for (int i = 0; i < 30; i++) fx.update(1.0f / 60);
        CHECK(fx.finished() && fx.liveCount() == 0, "then finished");
    }
    CHECK(liveAfter(*g.find("loop"), 0.5f) == 4 && liveAfter(*g.find("loop"), 1.5f) == 8 && liveAfter(*g.find("loop"), 2.5f) == 12,
          "a looping effect fires its bursts each time");
    {
        EffectInstance fx;
        fx.play(g.find("rate"), 1);
        fx.update(0.5f);
        fx.stop();
        const int n = fx.liveCount();
        fx.update(0.5f);
        CHECK(n > 0 && fx.liveCount() == n && !fx.emitting(), "stop(): no new particles, the live ones stay");
        fx.stop(true);
        CHECK(fx.liveCount() == 0 && fx.finished(), "stop(true) clears them");
    }
    {   // a long hitch does not simulate forever
        EffectInstance fx;
        fx.play(g.find("rate"), 1);
        fx.update(10.0f);
        CHECK(fx.liveCount() <= 2 && near(fx.time(), EffectInstance::kMaxSteps * EffectInstance::kStep), "a 10 s update simulates %d steps",
              EffectInstance::kMaxSteps);
    }
}

void testMotionAndQuads() {
    const toms::AtlasFile atlas = makeAtlas();
    const toms::anim::AtlasSet set(atlas);
    ParticleFile f = parse(R"({"effects": [
      {"name": "fly", "emitters": [{"name": "m", "sprite": "dot", "life": 10, "speed": 60, "direction": 0, "size": 32,
         "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "fall", "emitters": [{"name": "m", "sprite": "dot", "life": 10, "speed": 0,
         "forces": {"gravity": [0, 100]}, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "drag", "emitters": [{"name": "m", "sprite": "dot", "life": 10, "speed": 100, "direction": 0,
         "forces": {"drag": 1}, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "fade", "emitters": [{"name": "m", "sprite": "wide", "life": 1, "speed": 0, "size": 20, "spin": 90,
         "overLife": {"color": [{"t": 0, "v": [1, 1, 1, 1]}, {"t": 1, "v": [1, 0, 1, 0]}], "size": [{"t": 0, "v": 1}, {"t": 1, "v": 3}]},
         "render": {"blend": "add"}, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "orbit", "emitters": [{"name": "m", "sprite": "dot", "life": 10, "speed": 0, "offset": [0, 0],
         "shape": {"type": "circle", "radius": 50, "edge": true, "arc": [0, 0]},
         "forces": {"tangential": 100}, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "flip", "emitters": [{"name": "m", "flipbook": {"frames": ["f0", "f1", "f2"]}, "life": 1.2, "speed": 0,
         "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "world", "emitters": [{"name": "m", "sprite": "dot", "life": 10, "speed": 0, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "local", "emitters": [{"name": "m", "sprite": "dot", "life": 10, "speed": 0, "space": "local",
         "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "order", "emitters": [
         {"name": "back", "sprite": "dot", "life": 10, "render": {"order": 1}, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}},
         {"name": "front", "sprite": "wide", "life": 10, "render": {"order": -1}, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 1}]}}]},
      {"name": "outward", "emitters": [{"name": "m", "sprite": "dot", "life": 10, "speed": 10, "maxParticles": 200,
         "shape": {"type": "point", "outward": true}, "emission": {"rate": 0, "bursts": [{"t": 0, "count": 200}]}}]}
    ]})");
    std::vector<Quad> q;
    auto run = [&](const char* name, float seconds, const glm::mat3& at = glm::mat3(1.0f)) {
        static EffectInstance fx;
        fx = EffectInstance();
        fx.setTransform(at);
        fx.play(f.find(name), 5);
        for (int i = 0; i < (int)std::lround(seconds * 60); i++) fx.update(1.0f / 60);
        q.clear();
        fx.appendQuads(set, glm::mat3(1.0f), nullptr, q);
        return &fx;
    };
    run("fly", 1.0f);
    CHECK(q.size() == 1 && near(centre(q[0]).x, 60, 1.5f) && near(centre(q[0]).y, 0, 0.01f), "60 px/s to the right: x=60 after 1 s (%f)",
          q.empty() ? 0 : centre(q[0]).x);
    CHECK(q.size() == 1 && near(q[0].rect[2], 32) && near(q[0].rect[3], 32), "size 32 on a 16x16 sprite: 32x32");
    run("fall", 1.0f);
    CHECK(q.size() == 1 && near(centre(q[0]).y, 50, 2.0f), "gravity 100: ~50 px in 1 s (%f)", q.empty() ? 0 : centre(q[0]).y);
    run("drag", 3.0f);
    const float expect = 100.0f * (1 - std::exp(-3.0f));   // distance with drag 1 over 3 s
    CHECK(q.size() == 1 && near(centre(q[0]).x, expect, 3.0f), "drag 1: %f px (expected ~%f)", q.empty() ? 0 : centre(q[0]).x, expect);
    run("fade", 0.5f);
    CHECK(q.size() == 1 && near(q[0].tint[3], 0.5f, 0.02f) && near(q[0].tint[1], 0.5f, 0.02f), "colour curve at half life: alpha 0.5, green 0.5");
    CHECK(q.size() == 1 && q[0].additive, "blend add -> additive quad");
    // 20 px wide sprite with a 2:1 aspect, size x2 at half life (1 -> 3): 40x20, turned 45 degrees.
    {
        const float w = std::hypot(q[0].corners[2] - q[0].corners[0], q[0].corners[3] - q[0].corners[1]);
        const float h = std::hypot(q[0].corners[6] - q[0].corners[0], q[0].corners[7] - q[0].corners[1]);
        const float ang = glm::degrees(std::atan2(q[0].corners[3] - q[0].corners[1], q[0].corners[2] - q[0].corners[0]));
        CHECK(near(w, 40, 0.5f) && near(h, 20, 0.5f) && near(ang, 45, 1.0f), "size curve + aspect + spin: %fx%f at %f deg", w, h, ang);
    }
    run("orbit", 0.5f);
    CHECK(q.size() == 1 && centre(q[0]).y > 5 && centre(q[0]).x > 40, "tangential: starts at (50,0), moves clockwise (down) (%f,%f)",
          centre(q[0]).x, centre(q[0]).y);
    {
        const float ts[] = {0.1f, 0.5f, 0.9f};
        float u0[3];
        for (int i = 0; i < 3; i++) { run("flip", ts[i]); u0[i] = q.empty() ? -1 : q[0].uv[0]; }
        CHECK(near(u0[0], 0) && near(u0[1], 8.0f / 64) && near(u0[2], 16.0f / 64), "flipbook over life: f0, f1, f2");
    }
    // World space: the particle stays where it was born when the effect moves; local: it follows.
    for (const char* name : {"world", "local"}) {
        EffectInstance fx;
        fx.setTransform(toms::anim::placement(100, 0));
        fx.play(f.find(name), 1);
        fx.update(1.0f / 60);
        fx.setTransform(toms::anim::placement(300, 0));
        fx.update(1.0f / 60);
        q.clear();
        fx.appendQuads(set, glm::mat3(1.0f), nullptr, q);
        const float x = q.empty() ? -1 : centre(q[0]).x;
        if (std::string(name) == "world") CHECK(near(x, 100, 0.1f), "world space stays at 100 (%f)", x);
        else CHECK(near(x, 300, 0.1f), "local space follows to 300 (%f)", x);
    }
    run("order", 0.1f);
    CHECK(q.size() == 2 && near(q[0].rect[2], 16) && near(q[1].rect[2], 16) && q[0].uv[0] > q[1].uv[0],
          "order: 'front' (order -1, wide) draws first, then 'back'");
    run("outward", 1.0f);
    {
        int quadrant[4] = {0, 0, 0, 0};
        for (const Quad& qq : q) { const glm::vec2 c = centre(qq); quadrant[(c.x > 0 ? 1 : 0) + (c.y > 0 ? 2 : 0)]++; }
        CHECK(q.size() == 200 && quadrant[0] > 30 && quadrant[1] > 30 && quadrant[2] > 30 && quadrant[3] > 30,
              "outward from a point: every direction (%d %d %d %d)", quadrant[0], quadrant[1], quadrant[2], quadrant[3]);
    }
}

std::vector<Quad> frameOf(const Effect& e, uint32_t seed, float seconds, const toms::anim::AtlasSet& set, bool bySeek) {
    EffectInstance fx;
    fx.setTransform(toms::anim::placement(10, 20, 1.5f, 30));
    fx.play(&e, seed);
    if (bySeek) fx.seek(seconds);
    else for (int i = 0; i < (int)std::lround(seconds * 60); i++) fx.update(1.0f / 60);
    std::vector<Quad> q;
    fx.appendQuads(set, glm::mat3(1.0f), nullptr, q);
    return q;
}

bool sameQuads(const std::vector<Quad>& a, const std::vector<Quad>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        for (int k = 0; k < 8; k++)
            if (!near(a[i].corners[k], b[i].corners[k], 1e-3f)) return false;
    return true;
}

void testSeed() {
    const toms::AtlasFile atlas = makeAtlas();
    const toms::anim::AtlasSet set(atlas);
    ParticleFile f = parse(R"({"effects": [{"name": "spray", "emitters": [{"name": "m", "sprite": "dot", "life": [0.5, 1.5],
      "speed": [20, 80], "spread": 60, "size": [4, 12], "spin": [-90, 90], "shape": {"type": "box", "size": [30, 10]},
      "forces": {"gravity": [0, 40], "radial": [-5, 5]}, "emission": {"rate": 40, "bursts": [{"t": 0, "count": [5, 10]}]}}]}]})");
    const Effect& e = f.effects[0];
    const auto a = frameOf(e, 1234, 1.3f, set, false);
    const auto b = frameOf(e, 1234, 1.3f, set, false);
    const auto c = frameOf(e, 999, 1.3f, set, false);
    const auto s = frameOf(e, 1234, 1.3f, set, true);
    CHECK(!a.empty() && sameQuads(a, b), "same seed, same updates: the same particles (%zu)", a.size());
    CHECK(!sameQuads(a, c), "another seed: other particles");
    CHECK(sameQuads(a, s), "seek(1.3) == play + 78 updates");
    {   // With a prewarm the effect starts at that time: seek(t) lands at t, an earlier t stays at the start.
        Effect warm = e;
        warm.prewarm = 1.0f;
        EffectInstance fx;
        fx.play(&warm, 9);
        CHECK(near(fx.time(), 1.0f, 0.02f) && fx.liveCount() > 0, "prewarm 1 s: starts at 1 s with particles");
        fx.seek(1.5f);
        CHECK(near(fx.time(), 1.5f, 0.02f), "seek(1.5) with a prewarm: time 1.5 (%f)", fx.time());
        fx.seek(0.5f);
        CHECK(near(fx.time(), 1.0f, 0.02f), "seek(0.5) with a 1 s prewarm: stays at 1 s");
    }
    // Odd frame lengths still land on the same steps (fixed 1/60 s steps; time carries over).
    EffectInstance x, y;
    x.play(&e, 5); y.play(&e, 5);
    for (int i = 0; i < 60; i++) x.update(1.0f / 60);
    for (int i = 0; i < 40; i++) y.update(0.025f);
    CHECK(x.liveCount() == y.liveCount(), "60 x 1/60 s and 40 x 0.025 s: same particles (%d / %d)", x.liveCount(), y.liveCount());
}

void testCheck() {
    const toms::AtlasFile atlas = makeAtlas();
    const toms::anim::AtlasSet set(atlas);
    ParticleFile f = parse(R"({"effects": [
      {"name": "x", "emitters": [
        {"name": "a", "sprite": "nope", "stop": 0.5, "start": 1, "life": [2, 1], "maxParticles": 5, "emission": {"rate": 100,
           "bursts": [{"t": 0, "count": 10, "repeat": 3, "interval": 0}]}, "overLife": {"size": [{"t": 0, "v": 1}, {"t": 2, "v": 0}]},
           "render": {"blend": {"src": "dstAlpha", "dst": "one"}}},
        {"name": "b"}]},
      {"name": "x", "emitters": [{"name": "c", "sprite": "dot"}]}
    ]})");
    const std::vector<Problem> p = checkParticles(f, &set);
    auto has = [&](const std::string& text, bool warning) {
        for (const Problem& pr : p)
            if (pr.text.find(text) != std::string::npos && pr.warning == warning) return true;
        return false;
    };
    CHECK(has("is in no atlas", false), "missing sprite");
    CHECK(has("stop must be after start", false), "stop before start");
    CHECK(has("min is above max", false), "range min > max");
    CHECK(has("more than maxParticles", true), "pool too small (warning)");
    CHECK(has("bigger than maxParticles", true), "burst too big (warning)");
    CHECK(has("needs an interval", false), "repeating burst without an interval");
    CHECK(has("must be 0..1", false), "curve t outside 0..1");
    CHECK(has("dstAlpha", true), "dstAlpha warning");
    CHECK(has("draws nothing", false), "no sprite");
    CHECK(has("two effects have this name", false), "duplicate effect names");
    ParticleFile ok = parse(R"({"effects": [{"name": "fine", "emitters": [{"name": "m", "sprite": "dot"}]}]})");
    CHECK(checkParticles(ok, &set).empty(), "a plain emitter has no problems");
}

void testExamples() {
    // The recipes file the docs point to (docs/examples/fx_recipes.particle), with its real atlas.
    const char* candidates[] = {"../docs/examples/fx_recipes.particle", "docs/examples/fx_recipes.particle"};
    std::string path, text;
    for (const char* c : candidates) {
        std::ifstream in(c, std::ios::binary);
        if (in) { std::stringstream ss; ss << in.rdbuf(); text = ss.str(); path = c; break; }
    }
    CHECK(!text.empty(), "docs/examples/fx_recipes.particle found (run in assets/)");
    if (text.empty()) return;
    ParticleFile f = parse(text);
    std::vector<toms::AtlasFile> atlases(f.atlases.size());
    toms::anim::AtlasSet set;
    const std::string dir = path.substr(0, path.find_last_of('/') + 1);
    for (size_t i = 0; i < f.atlases.size(); i++) {
        std::ifstream in(dir + f.atlases[i].path, std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        std::string err;
        CHECK(toms::parseAtlas(ss.str(), atlases[i], &err), "atlas %s: %s", f.atlases[i].path.c_str(), err.c_str());
        set.add(atlases[i], f.atlases[i].id);
    }
    for (const Problem& p : checkParticles(f, &set))
        CHECK(false, "%s %s: %s", p.warning ? "warning" : "error", p.where.c_str(), p.text.c_str());
    CHECK(f.effects.size() >= 8, "the recipes have their effects (%zu)", f.effects.size());
    // Each one plays 2 s and draws something.
    for (const Effect& e : f.effects) {
        EffectInstance fx;
        fx.play(&e, 11);
        int most = 0;
        for (int i = 0; i < 120; i++) { fx.update(1.0f / 60); most = std::max(most, fx.liveCount()); }
        CHECK(most > 0, "%s makes particles", e.name.c_str());
    }
}

// A renderer that "has" GPU particles: it records what EffectInstance asks of it.
struct GpuRecorder : IRenderer {
    int created = 0, released = 0, draws = 0, quads = 0;
    long long spawned = 0, steps = 0;
    uint32_t lastCapacity = 0, lastFrames = 0;
    void init(uint32_t, uint32_t) override {}
    void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    void begin() override {}
    void drawSprite(const Quad&) override { quads++; }
    void end() override {}
    uint32_t width() const override { return 1024; }
    uint32_t height() const override { return 768; }
    bool supportsGpuParticles() const override { return true; }
    uint32_t createGpuParticles(uint32_t capacity) override { lastCapacity = capacity; return uint32_t(++created); }
    void releaseGpuParticles(uint32_t) override { released++; }
    void drawGpuParticles(const GpuParticleFrame& f) override {
        draws++;
        spawned += f.spawnCount;
        steps += f.steps;
        lastFrames = f.frameCount;
    }
};

void testGpuSimulation() {
    const toms::AtlasFile atlas = makeAtlas();
    const toms::anim::AtlasSet set(atlas);
    ParticleFile f = parse(R"({"effects": [{"name": "g", "duration": 3, "emitters": [
      {"name": "big", "sprite": "dot", "maxParticles": 400, "life": [0.2, 1.3], "speed": [10, 50], "spread": 180,
       "emission": {"rate": 300, "bursts": [{"t": 0.5, "count": 150}]}},
      {"name": "small", "flipbook": {"frames": ["f0", "f1"]}, "maxParticles": 50, "life": 0.5, "emission": {"rate": 40}}]}]})");
    const Effect& e = f.effects[0];
    GpuRecorder gpu;
    EffectInstance onGpu, onCpu;
    onGpu.setGpuSimulation(&gpu, 100);   // "big" (400) on the GPU, "small" (50) on the CPU
    onGpu.play(&e, 21);
    onCpu.play(&e, 21);
    CHECK(onGpu.gpuEmitters() == 1 && onGpu.gpuEmitter(0) && !onGpu.gpuEmitter(1) && gpu.lastCapacity == 400,
          "threshold 100: the 400-particle emitter on the GPU, the 50 one on the CPU");
    // Frame by frame the GPU emitter's live count (kept on the CPU from each particle's life) is the
    // CPU simulation's: the same particles are born and die in the same steps.
    bool same = true;
    int most = 0, frames = 0;
    for (int i = 0; i < 300; i++) {
        onGpu.update(1.0f / 60);
        onCpu.update(1.0f / 60);
        onGpu.draw(&gpu, set, glm::mat3(1.0f), nullptr);
        frames++;
        if (onGpu.liveCount(0) != onCpu.liveCount(0) || onGpu.liveCount(1) != onCpu.liveCount(1)) {
            if (same) fprintf(stderr, "  frame %d: GPU %d/%d, CPU %d/%d\n", i, onGpu.liveCount(0), onGpu.liveCount(1), onCpu.liveCount(0), onCpu.liveCount(1));
            same = false;
        }
        most = std::max(most, onGpu.liveCount(0));
    }
    CHECK(same && most > 200, "GPU emitter live counts == the CPU simulation's, every frame (most %d)", most);
    CHECK(gpu.draws == frames && gpu.steps == 300, "one GPU draw per frame; 300 steps handed over (%lld)", gpu.steps);
    CHECK(gpu.quads > 0, "the CPU emitter still draws as quads");
    CHECK(onGpu.finished() == onCpu.finished(), "finished() agrees");
    // The pool cap holds on the GPU too: never more live than maxParticles.
    CHECK(most <= 400, "never above maxParticles");
    // Threshold 0 = never; a renderer without compute = never.
    EffectInstance never;
    never.setGpuSimulation(&gpu, 0);
    never.play(&e, 1);
    CHECK(never.gpuEmitters() == 0, "threshold 0: everything on the CPU");
    struct NoCompute : GpuRecorder {
        bool supportsGpuParticles() const override { return false; }
    } plain;
    EffectInstance noCompute;
    noCompute.setGpuSimulation(&plain, 1);
    noCompute.play(&e, 1);
    CHECK(noCompute.gpuEmitters() == 0, "a renderer without GPU particles: everything on the CPU");
    // The editor's per-emitter choice: "gpu" below the threshold, "cpu" above it.
    ParticleFile forced = parse(R"({"effects": [{"name": "f", "emitters": [
      {"name": "small", "sprite": "dot", "maxParticles": 10, "simulation": "gpu"},
      {"name": "big", "sprite": "dot", "maxParticles": 100000, "simulation": "cpu"},
      {"name": "auto", "sprite": "dot", "maxParticles": 100000}]}]})");
    EffectInstance chosen;
    chosen.setGpuSimulation(&gpu, 5000);
    chosen.play(&forced.effects[0], 1);
    CHECK(chosen.gpuEmitter(0) && !chosen.gpuEmitter(1) && chosen.gpuEmitter(2),
          "simulation: gpu (10 particles) on the GPU, cpu (100000) on the CPU, auto by the threshold");
    noCompute.play(&forced.effects[0], 1);
    CHECK(noCompute.gpuEmitters() == 0, "\"gpu\" without compute shaders: the CPU");
    CHECK(particlesToJson(forced).find("\"simulation\": \"gpu\"") != std::string::npos &&
              particlesToJson(forced).find("\"simulation\": \"auto\"") == std::string::npos,
          "written when not auto");
    ParticleFile bad;
    std::string err;
    CHECK(!parseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "simulation": "fast"}]}]})", bad, &err),
          "simulation: an unknown value is an error");
    // Released when the instance plays again or goes away.
    const int before = gpu.released;
    onGpu.play(nullptr);
    CHECK(gpu.released == before + 1, "the GPU emitter's buffers are released");
}

// Not a pass/fail check (machines differ): the cost of 2000 and 30000 live particles per frame,
// simulation and quad building apart, printed.
void benchmark() {
    const toms::AtlasFile atlas = makeAtlas();
    const toms::anim::AtlasSet set(atlas);
    for (int count : {2000, 30000}) {
        const std::string n = std::to_string(count);
        ParticleFile f = parse(R"({"effects": [{"name": "b", "emitters": [{"name": "m", "sprite": "dot", "maxParticles": )" + n + R"(,
          "life": 4, "speed": [20, 60], "spread": 180, "spin": [-90, 90], "emission": {"rate": 0, "bursts": [{"t": 0, "count": )" + n + R"(}]},
          "overLife": {"color": [{"t": 0, "v": [1, 1, 1, 1]}, {"t": 1, "v": [1, 1, 1, 0]}], "size": [{"t": 0, "v": 1}, {"t": 1, "v": 2}]},
          "forces": {"gravity": [0, 30], "drag": 0.5, "tangential": [-10, 10]}}]}]})");
        EffectInstance fx;
        fx.play(&f.effects[0], 3);
        std::vector<Quad> q;
        q.reserve(size_t(count));
        const int frames = 120;
        double simMs = 0, quadMs = 0;
        for (int i = 0; i < frames; i++) {
            const auto t0 = std::chrono::steady_clock::now();
            fx.update(1.0f / 60);
            const auto t1 = std::chrono::steady_clock::now();
            q.clear();
            fx.appendQuads(set, glm::mat3(1.0f), nullptr, q);
            const auto t2 = std::chrono::steady_clock::now();
            simMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
            quadMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
        }
        std::printf("particle_fx_test: %d particles: update %.3f ms + quads %.3f ms per frame\n", fx.liveCount(), simMs / frames,
                    quadMs / frames);
        CHECK(fx.liveCount() == count && int(q.size()) == count, "benchmark: %d live, %d quads", count, count);
    }
}

}  // namespace

int main() {
    testJson();
    testEmission();
    testMotionAndQuads();
    testSeed();
    testCheck();
    testExamples();
    testGpuSimulation();
    benchmark();
    std::printf("particle_fx_test: %d check(s), %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
