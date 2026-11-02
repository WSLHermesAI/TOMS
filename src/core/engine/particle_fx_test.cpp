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

static int g_iChecks = 0, g_iFails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_iChecks;                                                               \
        if (!(cond)) {                                                            \
            ++g_iFails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::fx;

namespace {

bool f_Near(float l_fA, float l_fB, float l_fEps = 1e-3f) { return std::fabs(l_fA - l_fB) <= l_fEps; }

// A 64x64 page: "dot" 16x16, "wide" 32x16 (2:1), "f0".."f2" flipbook frames.
toms::AtlasFile f_MakeAtlas() {
    toms::AtlasFile l_oA;
    std::string l_strErr;
    toms::f_ParseAtlas("fx.png\nsize: 64, 64\n"
                     "dot\n  bounds: 0, 0, 16, 16\n"
                     "wide\n  bounds: 16, 0, 32, 16\n"
                     "f0\n  bounds: 0, 16, 8, 8\nf1\n  bounds: 8, 16, 8, 8\nf2\n  bounds: 16, 16, 8, 8\n",
                     l_oA, &l_strErr);
    l_oA.f_ComputeUVs();
    return l_oA;
}

ParticleFile f_Parse(const std::string& l_strText) {
    ParticleFile l_oF;
    std::string l_strErr;
    CHECK(f_ParseParticles(l_strText, l_oF, &l_strErr), "parse: %s", l_strErr.c_str());
    return l_oF;
}

glm::vec2 f_Centre(const Quad& l_oQ) { return {(l_oQ.m_arrCorners[0] + l_oQ.m_arrCorners[4]) / 2, (l_oQ.m_arrCorners[1] + l_oQ.m_arrCorners[5]) / 2}; }

void f_TestJson() {
    const std::string l_strFull = R"({
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
    ParticleFile l_oF = f_Parse(l_strFull);
    const std::string l_strOnce = f_ParticlesToJson(l_oF);
    ParticleFile l_oG = f_Parse(l_strOnce);
    CHECK(f_ParticlesToJson(l_oG) == l_strOnce, "round trip is stable");
    const Emitter& l_oE = l_oG.m_vEffects[0].m_vEmitters[0];
    CHECK(l_oG.m_vAtlases.size() == 2 && l_oG.m_vAtlases[1].m_strId == "fx", "atlases with ids");
    CHECK(l_oG.m_vEffects[0].m_bLoop && l_oG.m_vEffects[0].m_uSeed == 42 && f_Near(l_oG.m_vEffects[0].m_fPrewarm, 0.5f), "effect fields");
    CHECK(l_oE.m_bLocalSpace && l_oE.m_iMaxParticles == 50 && l_oE.m_vBursts.size() == 2 && l_oE.m_vBursts[1].m_iRepeat == 2, "emitter fields");
    CHECK(l_oE.m_oShape.m_eType == Shape::Ring && l_oE.m_oShape.m_bOutward && f_Near(l_oE.m_oShape.m_fArcTo, 180), "shape");
    CHECK(l_oE.m_v4Color == glm::vec4(1, 0, 0, 1) && l_oE.m_v4Color2 == glm::vec4(1, 1, 0, 1), "two colours");
    CHECK(l_oE.m_oColorOverLife.m_vKeys.size() == 2 && l_oE.m_oColorOverLife.m_vKeys[1].m_oEase.m_iKind == Ease::m_iBezier, "bezier ease kept");
    CHECK(l_oE.m_vFrames.size() == 2 && l_oE.m_bFramesByFps && l_oE.m_bRandomFrame, "flipbook");
    CHECK(l_oE.m_oBlend == Blend::f_Screen() && l_oE.m_oBlend.f_PresetName() == "screen", "custom factors that equal a preset");
    CHECK(l_strOnce.find("\"blend\": \"screen\"") != std::string::npos, "written as the preset name");

    // Defaults.
    ParticleFile l_oD = f_Parse(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "sprite": "dot"}]}]})");
    const Emitter& l_oM = l_oD.m_vEffects[0].m_vEmitters[0];
    CHECK(l_oM.m_fRate == 10 && l_oM.m_oLife.m_fMin == 1 && l_oM.m_fDirection == -90 && l_oM.m_oSize.m_fMin == 16 && l_oM.m_oBlend == Blend::f_Normal() &&
              l_oM.m_iMaxParticles == 100 && !l_oM.m_bLocalSpace,
          "defaults");
    CHECK(f_ParticlesToJson(l_oD).find("\"rate\"") == std::string::npos, "defaults are not written");

    // Mistakes are errors, with where they are.
    std::string l_strErr;
    ParticleFile l_oBad;
    CHECK(!f_ParseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "colour": [1,1,1,1]}]}]})", l_oBad, &l_strErr) &&
              l_strErr.find("colour") != std::string::npos && l_strErr.find("x/") != std::string::npos,
          "unknown field reported with its place: %s", l_strErr.c_str());
    CHECK(!f_ParseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "render": {"blend": "glow"}}]}]})", l_oBad, &l_strErr),
          "unknown blend preset");
    CHECK(!f_ParseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "life": [1]}]}]})", l_oBad, &l_strErr),
          "a range needs [min, max]");
    CHECK(!f_ParseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "overLife": {"size": [{"t": 0, "v": 1, "ease": "wobbly"}]}}]}]})",
                          l_oBad, &l_strErr),
          "unknown ease");
    Blend l_oB;
    CHECK(Blend::f_FromPreset("multiply", l_oB) && l_oB == Blend::f_Multiply() && !Blend::f_FromPreset("x", l_oB), "presets");
}

// Count how many particles an effect has after `seconds` of 1/60 updates.
int f_LiveAfter(const Effect& l_oE, float l_fSeconds, uint32_t l_uSeed = 7) {
    EffectInstance l_oFx;
    l_oFx.f_Play(&l_oE, l_uSeed);
    for (int l_iI = 0; l_iI < (int)std::lround(l_fSeconds * 60); l_iI++) l_oFx.f_Update(1.0f / 60);
    return l_oFx.f_LiveCount();
}

void f_TestEmission() {
    ParticleFile l_oG = f_Parse(R"({"effects": [
      {"name": "rate", "emitters": [{"name": "m", "sprite": "dot", "life": 100, "emission": {"rate": 10}}]},
      {"name": "burst", "emitters": [{"name": "m", "sprite": "dot", "life": 100,
         "emission": {"rate": 0, "bursts": [{"t": 0.5, "count": 5, "repeat": 2, "interval": 0.25}]}}]},
      {"name": "cap", "emitters": [{"name": "m", "sprite": "dot", "life": 100, "maxParticles": 7, "emission": {"rate": 100}}]},
      {"name": "window", "emitters": [{"name": "m", "sprite": "dot", "life": 100, "start": 0.5, "stop": 1.0, "emission": {"rate": 10}}]},
      {"name": "oneshot", "duration": 0.5, "emitters": [{"name": "m", "sprite": "dot", "life": 0.25, "emission": {"rate": 40}}]},
      {"name": "loop", "duration": 1, "loop": true, "emitters": [{"name": "m", "sprite": "dot", "life": 100,
         "emission": {"rate": 0, "bursts": [{"t": 0, "count": 4}]}}]}
    ]})");
    CHECK(f_LiveAfter(*l_oG.f_Find("rate"), 1.0f) == 10, "rate 10/s: 10 after 1 s (%d)", f_LiveAfter(*l_oG.f_Find("rate"), 1.0f));
    CHECK(f_LiveAfter(*l_oG.f_Find("rate"), 2.5f) == 25, "25 after 2.5 s");
    const Effect& l_oBurst = *l_oG.f_Find("burst");
    CHECK(f_LiveAfter(l_oBurst, 0.45f) == 0 && f_LiveAfter(l_oBurst, 0.55f) == 5 && f_LiveAfter(l_oBurst, 0.8f) == 10 &&
              f_LiveAfter(l_oBurst, 1.1f) == 15 && f_LiveAfter(l_oBurst, 3.0f) == 15,
          "burst at 0.5 + 2 repeats every 0.25 s: 0, 5, 10, 15, then no more");
    CHECK(f_LiveAfter(*l_oG.f_Find("cap"), 2.0f) == 7, "maxParticles caps the pool");
    CHECK(f_LiveAfter(*l_oG.f_Find("window"), 0.45f) == 0 && f_LiveAfter(*l_oG.f_Find("window"), 3.0f) == 5, "start 0.5 / stop 1.0: 5 particles");
    {
        EffectInstance l_oFx;
        l_oFx.f_Play(l_oG.f_Find("oneshot"), 3);
        bool l_bSawEmitting = false;
        for (int l_iI = 0; l_iI < 30; l_iI++) { l_oFx.f_Update(1.0f / 60); l_bSawEmitting |= l_oFx.f_Emitting(); }
        CHECK(l_bSawEmitting && !l_oFx.f_Emitting() && l_oFx.f_LiveCount() > 0 && !l_oFx.f_Finished(), "after its duration: no more particles, the last still live");
        for (int l_iI = 0; l_iI < 30; l_iI++) l_oFx.f_Update(1.0f / 60);
        CHECK(l_oFx.f_Finished() && l_oFx.f_LiveCount() == 0, "then finished");
    }
    CHECK(f_LiveAfter(*l_oG.f_Find("loop"), 0.5f) == 4 && f_LiveAfter(*l_oG.f_Find("loop"), 1.5f) == 8 && f_LiveAfter(*l_oG.f_Find("loop"), 2.5f) == 12,
          "a looping effect fires its bursts each time");
    {
        EffectInstance l_oFx;
        l_oFx.f_Play(l_oG.f_Find("rate"), 1);
        l_oFx.f_Update(0.5f);
        l_oFx.f_Stop();
        const int l_iN = l_oFx.f_LiveCount();
        l_oFx.f_Update(0.5f);
        CHECK(l_iN > 0 && l_oFx.f_LiveCount() == l_iN && !l_oFx.f_Emitting(), "stop(): no new particles, the live ones stay");
        l_oFx.f_Stop(true);
        CHECK(l_oFx.f_LiveCount() == 0 && l_oFx.f_Finished(), "stop(true) clears them");
    }
    {   // a long hitch does not simulate forever
        EffectInstance l_oFx;
        l_oFx.f_Play(l_oG.f_Find("rate"), 1);
        l_oFx.f_Update(10.0f);
        CHECK(l_oFx.f_LiveCount() <= 2 && f_Near(l_oFx.f_Time(), EffectInstance::m_iMaxSteps * EffectInstance::m_fStep), "a 10 s update simulates %d steps",
              EffectInstance::m_iMaxSteps);
    }
}

void f_TestMotionAndQuads() {
    const toms::AtlasFile l_oAtlas = f_MakeAtlas();
    const toms::anim::AtlasSet l_oSet(l_oAtlas);
    ParticleFile l_oF = f_Parse(R"({"effects": [
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
    std::vector<Quad> l_vQ;
    auto l_fnRun = [&](const char* l_pszName, float l_fSeconds, const glm::mat3& l_mat3At = glm::mat3(1.0f)) {
        static EffectInstance l_oFx;
        l_oFx = EffectInstance();
        l_oFx.f_SetTransform(l_mat3At);
        l_oFx.f_Play(l_oF.f_Find(l_pszName), 5);
        for (int l_iI = 0; l_iI < (int)std::lround(l_fSeconds * 60); l_iI++) l_oFx.f_Update(1.0f / 60);
        l_vQ.clear();
        l_oFx.f_AppendQuads(l_oSet, glm::mat3(1.0f), nullptr, l_vQ);
        return &l_oFx;
    };
    l_fnRun("fly", 1.0f);
    CHECK(l_vQ.size() == 1 && f_Near(f_Centre(l_vQ[0]).x, 60, 1.5f) && f_Near(f_Centre(l_vQ[0]).y, 0, 0.01f), "60 px/s to the right: x=60 after 1 s (%f)",
          l_vQ.empty() ? 0 : f_Centre(l_vQ[0]).x);
    CHECK(l_vQ.size() == 1 && f_Near(l_vQ[0].m_arrRect[2], 32) && f_Near(l_vQ[0].m_arrRect[3], 32), "size 32 on a 16x16 sprite: 32x32");
    l_fnRun("fall", 1.0f);
    CHECK(l_vQ.size() == 1 && f_Near(f_Centre(l_vQ[0]).y, 50, 2.0f), "gravity 100: ~50 px in 1 s (%f)", l_vQ.empty() ? 0 : f_Centre(l_vQ[0]).y);
    l_fnRun("drag", 3.0f);
    const float l_fExpect = 100.0f * (1 - std::exp(-3.0f));   // distance with drag 1 over 3 s
    CHECK(l_vQ.size() == 1 && f_Near(f_Centre(l_vQ[0]).x, l_fExpect, 3.0f), "drag 1: %f px (expected ~%f)", l_vQ.empty() ? 0 : f_Centre(l_vQ[0]).x, l_fExpect);
    l_fnRun("fade", 0.5f);
    CHECK(l_vQ.size() == 1 && f_Near(l_vQ[0].m_arrTint[3], 0.5f, 0.02f) && f_Near(l_vQ[0].m_arrTint[1], 0.5f, 0.02f), "colour curve at half life: alpha 0.5, green 0.5");
    CHECK(l_vQ.size() == 1 && l_vQ[0].m_bAdditive, "blend add -> additive quad");
    // 20 px wide sprite with a 2:1 aspect, size x2 at half life (1 -> 3): 40x20, turned 45 degrees.
    {
        const float l_fW = std::hypot(l_vQ[0].m_arrCorners[2] - l_vQ[0].m_arrCorners[0], l_vQ[0].m_arrCorners[3] - l_vQ[0].m_arrCorners[1]);
        const float l_fH = std::hypot(l_vQ[0].m_arrCorners[6] - l_vQ[0].m_arrCorners[0], l_vQ[0].m_arrCorners[7] - l_vQ[0].m_arrCorners[1]);
        const float l_fAng = glm::degrees(std::atan2(l_vQ[0].m_arrCorners[3] - l_vQ[0].m_arrCorners[1], l_vQ[0].m_arrCorners[2] - l_vQ[0].m_arrCorners[0]));
        CHECK(f_Near(l_fW, 40, 0.5f) && f_Near(l_fH, 20, 0.5f) && f_Near(l_fAng, 45, 1.0f), "size curve + aspect + spin: %fx%f at %f deg", l_fW, l_fH, l_fAng);
    }
    l_fnRun("orbit", 0.5f);
    CHECK(l_vQ.size() == 1 && f_Centre(l_vQ[0]).y > 5 && f_Centre(l_vQ[0]).x > 40, "tangential: starts at (50,0), moves clockwise (down) (%f,%f)",
          f_Centre(l_vQ[0]).x, f_Centre(l_vQ[0]).y);
    {
        const float l_arrTs[] = {0.1f, 0.5f, 0.9f};
        float l_arrU0[3];
        for (int l_iI = 0; l_iI < 3; l_iI++) { l_fnRun("flip", l_arrTs[l_iI]); l_arrU0[l_iI] = l_vQ.empty() ? -1 : l_vQ[0].m_arrUv[0]; }
        CHECK(f_Near(l_arrU0[0], 0) && f_Near(l_arrU0[1], 8.0f / 64) && f_Near(l_arrU0[2], 16.0f / 64), "flipbook over life: f0, f1, f2");
    }
    // World space: the particle stays where it was born when the effect moves; local: it follows.
    for (const char* l_pszName : {"world", "local"}) {
        EffectInstance l_oFx;
        l_oFx.f_SetTransform(toms::anim::f_Placement(100, 0));
        l_oFx.f_Play(l_oF.f_Find(l_pszName), 1);
        l_oFx.f_Update(1.0f / 60);
        l_oFx.f_SetTransform(toms::anim::f_Placement(300, 0));
        l_oFx.f_Update(1.0f / 60);
        l_vQ.clear();
        l_oFx.f_AppendQuads(l_oSet, glm::mat3(1.0f), nullptr, l_vQ);
        const float l_fX = l_vQ.empty() ? -1 : f_Centre(l_vQ[0]).x;
        if (std::string(l_pszName) == "world") CHECK(f_Near(l_fX, 100, 0.1f), "world space stays at 100 (%f)", l_fX);
        else CHECK(f_Near(l_fX, 300, 0.1f), "local space follows to 300 (%f)", l_fX);
    }
    l_fnRun("order", 0.1f);
    CHECK(l_vQ.size() == 2 && f_Near(l_vQ[0].m_arrRect[2], 16) && f_Near(l_vQ[1].m_arrRect[2], 16) && l_vQ[0].m_arrUv[0] > l_vQ[1].m_arrUv[0],
          "order: 'front' (order -1, wide) draws first, then 'back'");
    l_fnRun("outward", 1.0f);
    {
        int l_arrQuadrant[4] = {0, 0, 0, 0};
        for (const Quad& l_oQq : l_vQ) { const glm::vec2 l_v2C = f_Centre(l_oQq); l_arrQuadrant[(l_v2C.x > 0 ? 1 : 0) + (l_v2C.y > 0 ? 2 : 0)]++; }
        CHECK(l_vQ.size() == 200 && l_arrQuadrant[0] > 30 && l_arrQuadrant[1] > 30 && l_arrQuadrant[2] > 30 && l_arrQuadrant[3] > 30,
              "outward from a point: every direction (%d %d %d %d)", l_arrQuadrant[0], l_arrQuadrant[1], l_arrQuadrant[2], l_arrQuadrant[3]);
    }
}

std::vector<Quad> f_FrameOf(const Effect& l_oE, uint32_t l_uSeed, float l_fSeconds, const toms::anim::AtlasSet& l_oSet, bool l_bBySeek) {
    EffectInstance l_oFx;
    l_oFx.f_SetTransform(toms::anim::f_Placement(10, 20, 1.5f, 30));
    l_oFx.f_Play(&l_oE, l_uSeed);
    if (l_bBySeek) l_oFx.f_Seek(l_fSeconds);
    else for (int l_iI = 0; l_iI < (int)std::lround(l_fSeconds * 60); l_iI++) l_oFx.f_Update(1.0f / 60);
    std::vector<Quad> l_vQ;
    l_oFx.f_AppendQuads(l_oSet, glm::mat3(1.0f), nullptr, l_vQ);
    return l_vQ;
}

bool f_SameQuads(const std::vector<Quad>& l_vA, const std::vector<Quad>& l_vB) {
    if (l_vA.size() != l_vB.size()) return false;
    for (size_t l_nI = 0; l_nI < l_vA.size(); l_nI++)
        for (int l_iK = 0; l_iK < 8; l_iK++)
            if (!f_Near(l_vA[l_nI].m_arrCorners[l_iK], l_vB[l_nI].m_arrCorners[l_iK], 1e-3f)) return false;
    return true;
}

void f_TestSeed() {
    const toms::AtlasFile l_oAtlas = f_MakeAtlas();
    const toms::anim::AtlasSet l_oSet(l_oAtlas);
    ParticleFile l_oF = f_Parse(R"({"effects": [{"name": "spray", "emitters": [{"name": "m", "sprite": "dot", "life": [0.5, 1.5],
      "speed": [20, 80], "spread": 60, "size": [4, 12], "spin": [-90, 90], "shape": {"type": "box", "size": [30, 10]},
      "forces": {"gravity": [0, 40], "radial": [-5, 5]}, "emission": {"rate": 40, "bursts": [{"t": 0, "count": [5, 10]}]}}]}]})");
    const Effect& l_oE = l_oF.m_vEffects[0];
    const auto l_vA = f_FrameOf(l_oE, 1234, 1.3f, l_oSet, false);
    const auto l_vB = f_FrameOf(l_oE, 1234, 1.3f, l_oSet, false);
    const auto l_vC = f_FrameOf(l_oE, 999, 1.3f, l_oSet, false);
    const auto l_vS = f_FrameOf(l_oE, 1234, 1.3f, l_oSet, true);
    CHECK(!l_vA.empty() && f_SameQuads(l_vA, l_vB), "same seed, same updates: the same particles (%zu)", l_vA.size());
    CHECK(!f_SameQuads(l_vA, l_vC), "another seed: other particles");
    CHECK(f_SameQuads(l_vA, l_vS), "seek(1.3) == play + 78 updates");
    {   // With a prewarm the effect starts at that time: seek(t) lands at t, an earlier t stays at the start.
        Effect l_oWarm = l_oE;
        l_oWarm.m_fPrewarm = 1.0f;
        EffectInstance l_oFx;
        l_oFx.f_Play(&l_oWarm, 9);
        CHECK(f_Near(l_oFx.f_Time(), 1.0f, 0.02f) && l_oFx.f_LiveCount() > 0, "prewarm 1 s: starts at 1 s with particles");
        l_oFx.f_Seek(1.5f);
        CHECK(f_Near(l_oFx.f_Time(), 1.5f, 0.02f), "seek(1.5) with a prewarm: time 1.5 (%f)", l_oFx.f_Time());
        l_oFx.f_Seek(0.5f);
        CHECK(f_Near(l_oFx.f_Time(), 1.0f, 0.02f), "seek(0.5) with a 1 s prewarm: stays at 1 s");
    }
    // Odd frame lengths still land on the same steps (fixed 1/60 s steps; time carries over).
    EffectInstance l_oX, l_oY;
    l_oX.f_Play(&l_oE, 5); l_oY.f_Play(&l_oE, 5);
    for (int l_iI = 0; l_iI < 60; l_iI++) l_oX.f_Update(1.0f / 60);
    for (int l_iI = 0; l_iI < 40; l_iI++) l_oY.f_Update(0.025f);
    CHECK(l_oX.f_LiveCount() == l_oY.f_LiveCount(), "60 x 1/60 s and 40 x 0.025 s: same particles (%d / %d)", l_oX.f_LiveCount(), l_oY.f_LiveCount());
}

void f_TestCheck() {
    const toms::AtlasFile l_oAtlas = f_MakeAtlas();
    const toms::anim::AtlasSet l_oSet(l_oAtlas);
    ParticleFile l_oF = f_Parse(R"({"effects": [
      {"name": "x", "emitters": [
        {"name": "a", "sprite": "nope", "stop": 0.5, "start": 1, "life": [2, 1], "maxParticles": 5, "emission": {"rate": 100,
           "bursts": [{"t": 0, "count": 10, "repeat": 3, "interval": 0}]}, "overLife": {"size": [{"t": 0, "v": 1}, {"t": 2, "v": 0}]},
           "render": {"blend": {"src": "dstAlpha", "dst": "one"}}},
        {"name": "b"}]},
      {"name": "x", "emitters": [{"name": "c", "sprite": "dot"}]}
    ]})");
    const std::vector<Problem> l_vP = f_CheckParticles(l_oF, &l_oSet);
    auto l_fnHas = [&](const std::string& l_strText, bool l_bWarning) {
        for (const Problem& l_oPr : l_vP)
            if (l_oPr.m_strText.find(l_strText) != std::string::npos && l_oPr.m_bWarning == l_bWarning) return true;
        return false;
    };
    CHECK(l_fnHas("is in no atlas", false), "missing sprite");
    CHECK(l_fnHas("stop must be after start", false), "stop before start");
    CHECK(l_fnHas("min is above max", false), "range min > max");
    CHECK(l_fnHas("more than maxParticles", true), "pool too small (warning)");
    CHECK(l_fnHas("bigger than maxParticles", true), "burst too big (warning)");
    CHECK(l_fnHas("needs an interval", false), "repeating burst without an interval");
    CHECK(l_fnHas("must be 0..1", false), "curve t outside 0..1");
    CHECK(l_fnHas("dstAlpha", true), "dstAlpha warning");
    CHECK(l_fnHas("draws nothing", false), "no sprite");
    CHECK(l_fnHas("two effects have this name", false), "duplicate effect names");
    ParticleFile l_oOk = f_Parse(R"({"effects": [{"name": "fine", "emitters": [{"name": "m", "sprite": "dot"}]}]})");
    CHECK(f_CheckParticles(l_oOk, &l_oSet).empty(), "a plain emitter has no problems");
}

void f_TestExamples() {
    // The recipes file the docs point to (docs/examples/fx_recipes.particle), with its real atlas.
    const char* l_arrCandidates[] = {"../docs/examples/fx_recipes.particle", "docs/examples/fx_recipes.particle"};
    std::string l_strPath, l_strText;
    for (const char* l_pszC : l_arrCandidates) {
        std::ifstream l_oIn(l_pszC, std::ios::binary);
        if (l_oIn) { std::stringstream l_oSs; l_oSs << l_oIn.rdbuf(); l_strText = l_oSs.str(); l_strPath = l_pszC; break; }
    }
    CHECK(!l_strText.empty(), "docs/examples/fx_recipes.particle found (run in assets/)");
    if (l_strText.empty()) return;
    ParticleFile l_oF = f_Parse(l_strText);
    std::vector<toms::AtlasFile> l_vAtlases(l_oF.m_vAtlases.size());
    toms::anim::AtlasSet l_oSet;
    const std::string l_strDir = l_strPath.substr(0, l_strPath.find_last_of('/') + 1);
    for (size_t l_nI = 0; l_nI < l_oF.m_vAtlases.size(); l_nI++) {
        std::ifstream l_oIn(l_strDir + l_oF.m_vAtlases[l_nI].m_strPath, std::ios::binary);
        std::stringstream l_oSs; l_oSs << l_oIn.rdbuf();
        std::string l_strErr;
        CHECK(toms::f_ParseAtlas(l_oSs.str(), l_vAtlases[l_nI], &l_strErr), "atlas %s: %s", l_oF.m_vAtlases[l_nI].m_strPath.c_str(), l_strErr.c_str());
        l_oSet.f_Add(l_vAtlases[l_nI], l_oF.m_vAtlases[l_nI].m_strId);
    }
    for (const Problem& l_oP : f_CheckParticles(l_oF, &l_oSet))
        CHECK(false, "%s %s: %s", l_oP.m_bWarning ? "warning" : "error", l_oP.m_strWhere.c_str(), l_oP.m_strText.c_str());
    CHECK(l_oF.m_vEffects.size() >= 8, "the recipes have their effects (%zu)", l_oF.m_vEffects.size());
    // Each one plays 2 s and draws something.
    for (const Effect& l_oE : l_oF.m_vEffects) {
        EffectInstance l_oFx;
        l_oFx.f_Play(&l_oE, 11);
        int l_iMost = 0;
        for (int l_iI = 0; l_iI < 120; l_iI++) { l_oFx.f_Update(1.0f / 60); l_iMost = std::max(l_iMost, l_oFx.f_LiveCount()); }
        CHECK(l_iMost > 0, "%s makes particles", l_oE.m_strName.c_str());
    }
}

// A renderer that "has" GPU particles: it records what EffectInstance asks of it.
struct GpuRecorder : IRenderer {
    int m_iCreated = 0, m_iReleased = 0, m_iDraws = 0, m_iQuads = 0;
    long long m_i64Spawned = 0, m_i64Steps = 0;
    uint32_t m_uLastCapacity = 0, m_uLastFrames = 0;
    void f_Init(uint32_t, uint32_t) override {}
    void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    void begin() override {}
    void f_DrawSprite(const Quad&) override { m_iQuads++; }
    void end() override {}
    uint32_t f_Width() const override { return 1024; }
    uint32_t f_Height() const override { return 768; }
    bool f_SupportsGpuParticles() const override { return true; }
    uint32_t f_CreateGpuParticles(uint32_t l_uCapacity) override { m_uLastCapacity = l_uCapacity; return uint32_t(++m_iCreated); }
    void f_ReleaseGpuParticles(uint32_t) override { m_iReleased++; }
    void f_DrawGpuParticles(const GpuParticleFrame& l_oF) override {
        m_iDraws++;
        m_i64Spawned += l_oF.m_uSpawnCount;
        m_i64Steps += l_oF.m_uSteps;
        m_uLastFrames = l_oF.m_uFrameCount;
    }
};

void f_TestGpuSimulation() {
    const toms::AtlasFile l_oAtlas = f_MakeAtlas();
    const toms::anim::AtlasSet l_oSet(l_oAtlas);
    ParticleFile l_oF = f_Parse(R"({"effects": [{"name": "g", "duration": 3, "emitters": [
      {"name": "big", "sprite": "dot", "maxParticles": 400, "life": [0.2, 1.3], "speed": [10, 50], "spread": 180,
       "emission": {"rate": 300, "bursts": [{"t": 0.5, "count": 150}]}},
      {"name": "small", "flipbook": {"frames": ["f0", "f1"]}, "maxParticles": 50, "life": 0.5, "emission": {"rate": 40}}]}]})");
    const Effect& l_oE = l_oF.m_vEffects[0];
    GpuRecorder l_oGpu;
    EffectInstance l_oOnGpu, l_oOnCpu;
    l_oOnGpu.f_SetGpuSimulation(&l_oGpu, 100);   // "big" (400) on the GPU, "small" (50) on the CPU
    l_oOnGpu.f_Play(&l_oE, 21);
    l_oOnCpu.f_Play(&l_oE, 21);
    CHECK(l_oOnGpu.f_GpuEmitters() == 1 && l_oOnGpu.f_GpuEmitter(0) && !l_oOnGpu.f_GpuEmitter(1) && l_oGpu.m_uLastCapacity == 400,
          "threshold 100: the 400-particle emitter on the GPU, the 50 one on the CPU");
    // Frame by frame the GPU emitter's live count (kept on the CPU from each particle's life) is the
    // CPU simulation's: the same particles are born and die in the same steps.
    bool l_bSame = true;
    int l_iMost = 0, l_iFrames = 0;
    for (int l_iI = 0; l_iI < 300; l_iI++) {
        l_oOnGpu.f_Update(1.0f / 60);
        l_oOnCpu.f_Update(1.0f / 60);
        l_oOnGpu.f_Draw(&l_oGpu, l_oSet, glm::mat3(1.0f), nullptr);
        l_iFrames++;
        if (l_oOnGpu.f_LiveCount(0) != l_oOnCpu.f_LiveCount(0) || l_oOnGpu.f_LiveCount(1) != l_oOnCpu.f_LiveCount(1)) {
            if (l_bSame) fprintf(stderr, "  frame %d: GPU %d/%d, CPU %d/%d\n", l_iI, l_oOnGpu.f_LiveCount(0), l_oOnGpu.f_LiveCount(1), l_oOnCpu.f_LiveCount(0), l_oOnCpu.f_LiveCount(1));
            l_bSame = false;
        }
        l_iMost = std::max(l_iMost, l_oOnGpu.f_LiveCount(0));
    }
    CHECK(l_bSame && l_iMost > 200, "GPU emitter live counts == the CPU simulation's, every frame (most %d)", l_iMost);
    CHECK(l_oGpu.m_iDraws == l_iFrames && l_oGpu.m_i64Steps == 300, "one GPU draw per frame; 300 steps handed over (%lld)", l_oGpu.m_i64Steps);
    CHECK(l_oGpu.m_iQuads > 0, "the CPU emitter still draws as quads");
    CHECK(l_oOnGpu.f_Finished() == l_oOnCpu.f_Finished(), "finished() agrees");
    // The pool cap holds on the GPU too: never more live than maxParticles.
    CHECK(l_iMost <= 400, "never above maxParticles");
    // Threshold 0 = never; a renderer without compute = never.
    EffectInstance l_oNever;
    l_oNever.f_SetGpuSimulation(&l_oGpu, 0);
    l_oNever.f_Play(&l_oE, 1);
    CHECK(l_oNever.f_GpuEmitters() == 0, "threshold 0: everything on the CPU");
    struct NoCompute : GpuRecorder {
        bool f_SupportsGpuParticles() const override { return false; }
    } l_oPlain;
    EffectInstance l_oNoCompute;
    l_oNoCompute.f_SetGpuSimulation(&l_oPlain, 1);
    l_oNoCompute.f_Play(&l_oE, 1);
    CHECK(l_oNoCompute.f_GpuEmitters() == 0, "a renderer without GPU particles: everything on the CPU");
    // The editor's per-emitter choice: "gpu" below the threshold, "cpu" above it.
    ParticleFile l_oForced = f_Parse(R"({"effects": [{"name": "f", "emitters": [
      {"name": "small", "sprite": "dot", "maxParticles": 10, "simulation": "gpu"},
      {"name": "big", "sprite": "dot", "maxParticles": 100000, "simulation": "cpu"},
      {"name": "auto", "sprite": "dot", "maxParticles": 100000}]}]})");
    EffectInstance l_oChosen;
    l_oChosen.f_SetGpuSimulation(&l_oGpu, 5000);
    l_oChosen.f_Play(&l_oForced.m_vEffects[0], 1);
    CHECK(l_oChosen.f_GpuEmitter(0) && !l_oChosen.f_GpuEmitter(1) && l_oChosen.f_GpuEmitter(2),
          "simulation: gpu (10 particles) on the GPU, cpu (100000) on the CPU, auto by the threshold");
    l_oNoCompute.f_Play(&l_oForced.m_vEffects[0], 1);
    CHECK(l_oNoCompute.f_GpuEmitters() == 0, "\"gpu\" without compute shaders: the CPU");
    CHECK(f_ParticlesToJson(l_oForced).find("\"simulation\": \"gpu\"") != std::string::npos &&
              f_ParticlesToJson(l_oForced).find("\"simulation\": \"auto\"") == std::string::npos,
          "written when not auto");
    ParticleFile l_oBad;
    std::string l_strErr;
    CHECK(!f_ParseParticles(R"({"effects": [{"name": "x", "emitters": [{"name": "m", "simulation": "fast"}]}]})", l_oBad, &l_strErr),
          "simulation: an unknown value is an error");
    // Released when the instance plays again or goes away.
    const int l_iBefore = l_oGpu.m_iReleased;
    l_oOnGpu.f_Play(nullptr);
    CHECK(l_oGpu.m_iReleased == l_iBefore + 1, "the GPU emitter's buffers are released");
}

// Not a pass/fail check (machines differ): the cost of 2000 and 30000 live particles per frame,
// simulation and quad building apart, printed.
void f_Benchmark() {
    const toms::AtlasFile l_oAtlas = f_MakeAtlas();
    const toms::anim::AtlasSet l_oSet(l_oAtlas);
    for (int l_iCount : {2000, 30000}) {
        const std::string l_strN = std::to_string(l_iCount);
        ParticleFile l_oF = f_Parse(R"({"effects": [{"name": "b", "emitters": [{"name": "m", "sprite": "dot", "maxParticles": )" + l_strN + R"(,
          "life": 4, "speed": [20, 60], "spread": 180, "spin": [-90, 90], "emission": {"rate": 0, "bursts": [{"t": 0, "count": )" + l_strN + R"(}]},
          "overLife": {"color": [{"t": 0, "v": [1, 1, 1, 1]}, {"t": 1, "v": [1, 1, 1, 0]}], "size": [{"t": 0, "v": 1}, {"t": 1, "v": 2}]},
          "forces": {"gravity": [0, 30], "drag": 0.5, "tangential": [-10, 10]}}]}]})");
        EffectInstance l_oFx;
        l_oFx.f_Play(&l_oF.m_vEffects[0], 3);
        std::vector<Quad> l_vQ;
        l_vQ.reserve(size_t(l_iCount));
        const int l_iFrames = 120;
        double l_dSimMs = 0, l_dQuadMs = 0;
        for (int l_iI = 0; l_iI < l_iFrames; l_iI++) {
            const auto l_oT0 = std::chrono::steady_clock::now();
            l_oFx.f_Update(1.0f / 60);
            const auto l_oT1 = std::chrono::steady_clock::now();
            l_vQ.clear();
            l_oFx.f_AppendQuads(l_oSet, glm::mat3(1.0f), nullptr, l_vQ);
            const auto l_oT2 = std::chrono::steady_clock::now();
            l_dSimMs += std::chrono::duration<double, std::milli>(l_oT1 - l_oT0).count();
            l_dQuadMs += std::chrono::duration<double, std::milli>(l_oT2 - l_oT1).count();
        }
        std::printf("particle_fx_test: %d particles: update %.3f ms + quads %.3f ms per frame\n", l_oFx.f_LiveCount(), l_dSimMs / l_iFrames,
                    l_dQuadMs / l_iFrames);
        CHECK(l_oFx.f_LiveCount() == l_iCount && int(l_vQ.size()) == l_iCount, "benchmark: %d live, %d quads", l_iCount, l_iCount);
    }
}

}  // namespace

int main() {
    f_TestJson();
    f_TestEmission();
    f_TestMotionAndQuads();
    f_TestSeed();
    f_TestCheck();
    f_TestExamples();
    f_TestGpuSimulation();
    f_Benchmark();
    std::printf("particle_fx_test: %d check(s), %d failed\n", g_iChecks, g_iFails);
    return g_iFails ? 1 : 0;
}
