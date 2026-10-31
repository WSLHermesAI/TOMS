// play_blocks_test.cpp -- the reusable blocks of docs/gameplay/16 §2: drawn shapes (gesture.h), the cue track
// (cue_track.h), skills and their shows (special_moves.h). Exits 0 on success.
#include "cue_track.h"
#include "gesture.h"
#include "special_moves.h"

#include <cmath>
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

static std::vector<glm::vec2> polyline(const std::vector<glm::vec2>& corners, glm::vec2 at, float size) {
    std::vector<glm::vec2> v;
    for (size_t k = 1; k < corners.size(); k++)
        for (int s = 0; s < 10; s++) v.push_back(at + (corners[k - 1] + (corners[k] - corners[k - 1]) * (s / 10.0f)) * size);
    v.push_back(at + corners.back() * size);
    return v;
}

int main() {
    // 1. Shapes
    CHECK(shapeCorners("pentagram").size() == 6 && shapeClosed("pentagram"), "a pentagram: 5 corners, closed");
    CHECK(shapeCorners("nothing").empty(), "an unknown shape: none");
    {
        const glm::vec2 top = shapePoint("pentagram", {100, 100}, 200, 0.0f);
        CHECK(std::fabs(top.x - 100) < 0.01f && std::fabs(top.y - 0) < 0.01f, "the pentagram starts at its top");
        const glm::vec2 end = shapePoint("pentagram", {100, 100}, 200, 1.0f);
        CHECK(std::fabs(end.x - top.x) < 0.01f && std::fabs(end.y - top.y) < 0.01f, "and ends there");
    }
    {   // free strokes: any place and size, either way round, from any corner
        const auto tri = polyline(shapeCorners("triangle"), {40, 70}, 130);
        CHECK(matchStroke(tri, "triangle"), "a triangle is a triangle");
        std::vector<glm::vec2> back(tri.rbegin(), tri.rend());
        CHECK(matchStroke(back, "triangle"), "the other way round too");
        CHECK(!matchStroke(tri, "circle"), "a triangle is not a circle");
        const auto circ = polyline(shapeCorners("circle"), {0, 0}, 90);
        CHECK(matchStroke(circ, "circle") && !matchStroke(circ, "triangle"), "a circle is a circle, not a triangle");
        CHECK(!matchStroke({{0, 0}, {100, 0}}, "triangle"), "a line is no triangle");
    }
    {   // traced on the screen: the first corner starts it, each next corner in order is a step
        ShapeTrace t;
        t.start("pentagram", {300, 300}, 200, 30);
        CHECK(t.stepCount() == 5, "five steps");
        const auto& p = t.points();
        CHECK(t.pointer(0, p[0].x, p[0].y) == 0, "the start is no step");
        CHECK(t.pointer(1, p[2].x, p[2].y) == 0, "a corner out of order: nothing");
        CHECK(t.pointer(1, p[1].x, p[1].y) == 1 && t.steps() == 1, "the next corner: a step");
        t.pointer(2, 0, 0);
        CHECK(t.pointer(1, p[2].x, p[2].y) == 0, "lifted: a move without a press does nothing");
        CHECK(t.pointer(0, p[2].x, p[2].y) == 1, "pressed again on the next corner: a step");
        for (int k = 3; k <= 5; k++) t.pointer(1, p[(size_t)k].x, p[(size_t)k].y);
        CHECK(t.done() && t.steps() == 5, "back at the top: done");
    }
    // 2. The cue track: 1000 ms cues, the last 500 take the answer, a "!" 300 ms before.
    {
        CueTiming tm;
        tm.warnMs = 300;
        CueTrack c;
        c.start(3, tm);
        c.update(100);
        CHECK(!c.open() && !c.warning(), "at first: only shown");
        c.update(150);   // 250
        CHECK(c.warning() && !c.open(), "the ! before the window");
        CHECK(!c.answer(true), "an answer before the window is ignored");
        c.update(300);   // 550
        CHECK(c.open() && c.answer(true) && c.index() == 1 && c.state(0) == CueTrack::Answered, "answered in the window");
        CHECK(c.cueAgeMs() == -1, "a short gap before the next");
        CHECK(c.update(150 + 1000) == 1 && c.state(1) == CueTrack::Missed && c.index() == 2, "the next let pass: missed");
        c.update(600);
        CHECK(c.answer(false) && c.done() && c.state(2) == CueTrack::Missed, "a wrong answer: missed; done");
    }
    // 3. Skills and shows
    {
        const nlohmann::json j = nlohmann::json::parse(R"({
          "_comment": "x",
          "star_seal": { "name": "skill.star_seal", "weapon": "staff", "use": "ultimate", "damage": 10,
            "play": { "kind": "shape", "shape": "pentagram", "timeMs": 6000 },
            "demo": [ { "at": 2100, "do": "dim" }, { "at": 0, "do": "pathFx", "shape": "pentagram", "fx": "t", "ms": 2000 } ],
            "finish": [ { "at": 400, "do": "hits", "count": 10, "gapMs": 110 } ] },
          "tiger": { "weapon": "sword", "use": "ultimate", "play": { "kind": "slice", "cues": ["way", "way", "triangle"] } },
          "guard_mash": { "use": "guard", "play": { "kind": "mash", "points": 12, "timeMs": 2000 } }
        })");
        const SpecialMoveTable t = specialMovesFromJson(j);
        CHECK(t.skills.size() == 3, "three skills, the comment skipped");
        const SpecialMove* s = t.find("star_seal");
        CHECK(s && s->play.points == 5 && s->damage == 10.0f, "the pentagram: 5 steps, x10");
        CHECK(s && s->demo.size() == 2 && s->demo[0].what == "pathFx", "beats sorted by time");
        CHECK(s && showLength(s->demo) == 2100 && showLength(s->finish) == 400 + 1100, "show lengths");
        CHECK(s && beatsDue(s->demo, -1, 0).size() == 1 && beatsDue(s->demo, 0, 2100).size() == 1, "beats due");
        CHECK(t.forWeapon("sword", "ultimate") && t.forWeapon("sword", "ultimate")->play.points == 3, "a slice's points: its cues");
        CHECK(t.find("guard_mash") && t.find("guard_mash")->play.points == 12, "a guard skill");
        CHECK(!t.forWeapon("bow", "ultimate"), "none for the bow");
    }
    CHECK(namePart("SUPER", 2, 5) == "SU" && namePart("SUPER", 5, 5) == "SUPER" && namePart("SUPER", 0, 5).empty(), "name parts");
    CHECK(namePart("星之印", 1, 3) == "星", "name parts keep letters whole");
    if (g_fail) { std::printf("play_blocks_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("play_blocks_test: all passed\n");
    return 0;
}
