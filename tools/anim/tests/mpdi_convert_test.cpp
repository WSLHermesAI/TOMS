// mpdi_convert_test.cpp -- the .mpdi converter (core/mpdi_convert.h) on the hand-made files in tests/mpdi/:
//   old_format.mpdi      no version: CurvePoint / CurveTime / "w,h,angle,r,g,b,a,image,mirror" points, centred
//                        positions, stepped data (no BehaviorUpdate), 0..255 colour, mirror
//   version_header.mpdi  <Version MPDIVersion="1.0"/> before <MPDIList> (two top-level elements), centred positions
//   v13_features.mpdi    1.3: top-left positions (+ half the original size), Scale over a trimmed sprite, LOD 2
//                        (Chaikin), StartTime + Loop (a start gate), additive blending, a sound hint, a mirror,
//                        a CenterDown anchor, a RotationPosOffset
// tests/mpdi/sheet.pi + sheet.png: "box" 20x10 (untrimmed) and "ring" 16x16 trimmed to 8x8 at 4,4 (made with
// `atlaspack pack ... --format pi`).
//
// Run: mpdi_convert_test <tests/mpdi> <scratch folder>
#include "anim_clip.h"
#include "mpdi_convert.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace toms::anim;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
static bool near2(const glm::vec2& a, const glm::vec2& b, float e = 1e-3f) { return near(a.x, b.x, e) && near(a.y, b.y, e); }

static bool load(const fs::path& p, AnimFile& f) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string err;
    const bool ok = parseAnim(ss.str(), f, &err);
    if (!ok) std::printf("  %s: %s\n", p.u8string().c_str(), err.c_str());
    return ok;
}

static const Node* child(const Node& n, const std::string& name) {
    for (const Node& c : n.children) if (c.name == name) return &c;
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: mpdi_convert_test <tests/mpdi> <scratch folder>\n"); return 3; }
    const fs::path in = fs::u8path(argv[1]), out = fs::u8path(argv[2]);
    std::error_code ec;
    fs::remove_all(out, ec);

    const animed::MpdiConvertReport r = animed::convertMpdiFolder(in.u8string(), out.u8string());
    for (const std::string& e : r.errors) std::printf("  error: %s\n", e.c_str());
    for (const std::string& w : r.warnings) std::printf("  warning: %s\n", w.c_str());
    CHECK(r.mpdiFiles == 3 && r.animFiles == 3 && r.errors.empty(), "3 files found and converted (%d / %d)", r.mpdiFiles, r.animFiles);
    CHECK(r.piFiles == 1, "the one .pi converted once (%d)", r.piFiles);
    CHECK(fs::exists(out / "sheet.atlasproj") && fs::exists(out / "sheet.atlas") && fs::exists(out / "sheet.png"),
          "sheet.pi -> sheet.atlasproj + sheet.atlas + sheet.png");

    // ---- the oldest format ----
    AnimFile f;
    if (load(out / "old_format.anim", f)) {
        CHECK(f.atlases.size() == 1 && f.atlases[0].id == "sheet" && f.atlases[0].path == "sheet.atlas", "the atlas list");
        const Clip* c = f.find("walk");
        CHECK(c && near(c->length, 1.0f) && c->playCount == 1 && !c->stayAtLastFrame, "clip walk: 1 s, once, hidden after");
        const Node* p = c ? child(c->root, "p") : nullptr;
        CHECK(p != nullptr, "node p");
        if (p) {
            CHECK(p->sprite == "sheet:box", "sprite sheet:box (%s)", p->sprite.c_str());
            CHECK(p->posKeys.size() == 2 && near2(p->posKeys[0].v, {10, 20}) && near2(p->posKeys[1].v, {110, 20}),
                  "old positions are the image centre");
            CHECK(p->scaleKeys.size() == 2 && near2(p->scaleKeys[0].v, {1, 1}) && near2(p->scaleKeys[1].v, {-2, 2}) &&
                      p->scaleKeys[0].ease.isStepped(),
                  "scale = Size / trimmed size, mirror = negative x, stepped without BehaviorUpdate");
            CHECK(p->rotKeys.size() == 2 && near(p->rotKeys[1].v.z, 90.0f), "rotation 0 -> 90");
            CHECK(p->colorKeys.size() == 2 && near(p->colorKeys[1].v.y, 0.0f) && near(p->colorKeys[1].v.w, 128.0f / 255.0f),
                  "0..255 colour -> 0..1");
        }
    }

    // ---- <Version/> before <MPDIList>, 1.0: centred ----
    f = AnimFile();
    if (load(out / "version_header.anim", f)) {
        const Clip* c = f.find("still");
        const Node* n = c ? child(c->root, "ring") : nullptr;
        CHECK(c && c->playCount == -1, "PlayCount -1 -> loop forever");
        CHECK(n && n->posKeys.size() == 2 && near2(n->posKeys[0].v, {200, 100}) && near2(n->posKeys[1].v, {200, 140}),
              "1.0 positions are the image centre");
        CHECK(n && n->scaleKeys.empty(), "Size = the trimmed size: no scale keys");
    }

    // ---- 1.3 features ----
    f = AnimFile();
    if (load(out / "v13_features.anim", f)) {
        const Clip* c = f.find("fx");
        CHECK(c && c->playCount == 2 && c->stayAtLastFrame && near(c->length, 2.0f), "clip fx: twice, stays at the last frame");
        const Node* gate = c ? child(c->root, "a_start") : nullptr;
        CHECK(gate && gate->visibleKeys.size() == 2 && !gate->visibleKeys[0].v && near(gate->visibleKeys[1].t, 0.5f) && gate->visibleKeys[1].v,
              "StartTime 0.5 + Loop: a gate hidden until 0.5 s");
        const Node* a = gate ? child(*gate, "a") : nullptr;
        CHECK(a && a->loop && a->blend == Blend::Add, "path a loops inside the gate; destination GL_ONE = additive");
        if (a) {
            // 1.3: top-left + half the ring's 16x16 original = the centre; LOD 2 = one Chaikin pass (6 keys)
            const glm::vec2 want[6] = {{8, 8}, {33, 8}, {83, 8}, {108, 33}, {108, 83}, {108, 108}};
            const float wantT[6] = {0.5f, 0.625f, 0.875f, 1.125f, 1.375f, 1.5f};
            bool ok = a->posKeys.size() == 6;
            for (size_t i = 0; ok && i < 6; i++) ok = near2(a->posKeys[i].v, want[i]) && near(a->posKeys[i].t, wantT[i]);
            CHECK(ok, "LOD 2: the engine's Chaikin smoothing of points and times, shifted by StartTime (%zu keys)", a->posKeys.size());
            CHECK(a->scaleKeys.size() == 3 && near2(a->scaleKeys[0].v, {2, 2}) && near2(a->scaleKeys[2].v, {-2, 2}) &&
                      a->scaleKeys[0].ease.isLinear(),
                  "Scale attribute, mirror on the last point, linear with BehaviorUpdate");
            CHECK(a->eventKeys.size() == 1 && near(a->eventKeys[0].t, 1.0f) && a->eventKeys[0].v == "sound:ding.wav",
                  "the sound hint on point 1 -> an event at 1.0 s");
        }
        // CenterDown: the drawn rect's bottom-centre convention, as the engine places it (image centre at 50,45)
        const Node* bp = c ? child(c->root, "b_pivot") : nullptr;
        const Node* b = bp ? child(*bp, "b") : nullptr;
        CHECK(bp && b && near2(bp->pos, {60, 55}), "CenterDown: the turning point at the top-left + half the original size");
        CHECK(b && near2(b->pos + bp->pos, {50, 45}), "CenterDown: the image centre where the engine draws it (%.1f, %.1f)",
              b ? (b->pos + bp->pos).x : 0.f, b ? (b->pos + bp->pos).y : 0.f);
        // RotationPosOffset: a turning group with the image 30 px out
        const Node* cp = c ? child(c->root, "c_pivot") : nullptr;
        const Node* ci = cp ? child(*cp, "c") : nullptr;
        CHECK(cp && near2(cp->pos, {10, 5}) && cp->rotKeys.size() == 1 && near(cp->rotKeys[0].v.z, 90.0f) && ci && near2(ci->pos, {30, 0}),
              "RotationPosOffset: the group turns 90 at the centre, the image 30 px out");
        // Angles x, y, z: kept as they are (the runtime turns in the engine's order, Rx * Ry * Rz)
        const Node* d = c ? child(c->root, "d") : nullptr;
        CHECK(d && d->rotKeys.size() == 1 && near(d->rotKeys[0].v.x, 30.0f) && near(d->rotKeys[0].v.y, 45.0f) &&
                  near(d->rotKeys[0].v.z, 10.0f),
              "Angles x, y, z -> rot x, y, z");
    }

    std::printf("mpdi_convert_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
