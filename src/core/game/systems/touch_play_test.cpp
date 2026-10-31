// touch_play_test.cpp -- the plays (touch_play.h): bow, slice, circles, shape, mash. Exits 0 on success.
#include "touch_play.h"

#include <cmath>
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

static void swipe(TouchPlay& p, float x, float y, float deg, float len) {
    const float a = deg * 3.14159265f / 180.0f;
    p.pointer(0, x, y);
    p.pointer(1, x + std::cos(a) * len * 0.5f, y + std::sin(a) * len * 0.5f);
    p.pointer(2, x + std::cos(a) * len, y + std::sin(a) * len);
}

int main() {
    const glm::vec4 area(500, 100, 300, 200);
    // 1. Bow: the target moves 0.5 s, then stops; a pull charges and shakes; a normal attack is one arrow.
    {
        TouchPlay p;
        p.start(PlayKind::Bow, 1, false, area, 7);
        CHECK(p.running() && !p.started(), "the bow waits for its pull");
        const auto a = p.target();
        p.update(250);
        const auto b = p.target();
        p.update(400);
        const auto c = p.target();
        p.update(400);
        const auto d = p.target();
        CHECK(std::hypot(b.x - a.x, b.y - a.y) > 1.0f, "the target moves at first");
        CHECK(std::hypot(d.x - c.x, d.y - c.y) < 0.01f, "then stops (after 0.5 s)");
        CHECK(d.x >= area.x && d.x <= area.x + area.z && d.y >= area.y && d.y <= area.y + area.w, "in the area");
        // pull 50 px straight down from 90 px under it: the crosshair (no shake at once) on the target
        p.pointer(0, d.x, d.y + 90);
        p.pointer(1, d.x, d.y + 140);
        CHECK(p.aiming() && p.charge() == 0.0f && std::fabs(p.crosshair().y - d.y) < 0.01f, "aimed, not charged yet");
        p.update(750);
        CHECK(std::fabs(p.charge() - 0.5f) < 0.01f, "half the 1.5 s limit: half charged");
        p.pointer(2, d.x, d.y + 140);
        const ComboResult r = p.result();
        CHECK(p.done() && r.hits == 1 && r.points == 1 && std::fabs(p.shotCharge() - 0.5f) < 0.01f, "one arrow, charged half");
    }
    {   // held to the charge limit: the arrow goes by itself, fully charged (Q41)
        TouchPlaySettings s;
        s.bowShakePx = 0.0f;
        TouchPlay p;
        p.start(PlayKind::Bow, 1, false, area, 3, s);
        p.update(600);
        const auto t = p.target();
        p.pointer(0, t.x, t.y + 90);
        p.pointer(1, t.x, t.y + 140);
        p.update(1600);
        CHECK(p.done() && p.result().hits == 1 && p.shotCharge() == 1.0f, "the charge limit lets it go at full charge");
    }
    {   // a short pull is not a shot; an arrow not shot in time is a miss
        TouchPlay p;
        p.start(PlayKind::Bow, 1, false, area, 4);
        p.pointer(0, 600, 300); p.pointer(2, 605, 300);
        CHECK(p.started() && p.index() == 0, "too short a pull: no shot");
        p.update(4100);
        CHECK(p.done() && p.result().failed, "the arrow's time ran out: a miss");
    }
    // 2. Slice: one prompt at a time; only its last 0.5 s take the answer; each answer is a hit at once.
    {
        TouchPlaySettings s;
        s.sliceCues = {"way", "way", "triangle"};
        TouchPlay p;
        p.start(PlayKind::Slice, 3, false, area, 11, s);
        CHECK(p.started() && p.cueKind(2) == "triangle", "the sword starts at once; its third prompt a triangle");
        swipe(p, 650, 200, p.cueDir(0) * 45.0f, 160);
        CHECK(p.index() == 0, "a swipe before the window: nothing");
        p.update(600);   // the window is open (500..1000)
        swipe(p, 650, 200, p.cueDir(0) * 45.0f, 160);
        auto hits = p.takeHits();
        CHECK(p.index() == 1 && hits.size() == 1 && hits[0].ok && hits[0].perfect, "the right way in the window: a hit, at once");
        p.update(150 + 1000);   // the next prompt passes unanswered
        hits = p.takeHits();
        CHECK(p.index() == 2 && hits.size() == 1 && !hits[0].ok, "a prompt let pass: a miss");
        p.update(600);
        std::vector<glm::vec2> tri = {{650, 150}, {700, 250}, {600, 250}, {650, 150}};
        p.pointer(0, tri[0].x, tri[0].y);
        for (float t = 0.0f; t <= 3.0f; t += 0.1f) {
            const int k = std::min(2, (int)t);
            const glm::vec2 q = tri[(size_t)k] + (tri[(size_t)k + 1] - tri[(size_t)k]) * (t - (float)k);
            p.pointer(1, q.x, q.y);
        }
        p.pointer(2, tri[3].x, tri[3].y);
        CHECK(p.done() && p.result().hits == 2 && p.result().points == 3, "a triangle drawn: the third prompt cut");
    }
    // 3. Circles: count, hide, "!", then tapped in the remembered order; each tap a spell.
    {
        TouchPlay p;
        p.start(PlayKind::Circles, 3, false, area, 9);
        CHECK(p.started() && p.circlePhase() == TouchPlay::CirclePhase::Count && p.circlesLit() == 1, "starts by itself, counting");
        const auto c = p.circles();
        for (size_t i = 0; i < c.size(); i++)
            for (size_t j = i + 1; j < c.size(); j++) CHECK(std::hypot(c[i].x - c[j].x, c[i].y - c[j].y) >= 2.6f * 34.0f, "apart");
        p.update(500);
        CHECK(p.circlesLit() == 2, "the second lit");
        p.pointer(0, c[0].x, c[0].y);
        CHECK(p.index() == 0, "no tap while counting");
        p.update(900);    // 1400: hiding
        CHECK(p.circlePhase() == TouchPlay::CirclePhase::Hide, "then hidden");
        p.update(500);    // 1900: back, with the "!"
        CHECK(p.circlePhase() == TouchPlay::CirclePhase::Warn, "back, with the !");
        p.update(500);    // 2400: tap
        CHECK(p.circlePhase() == TouchPlay::CirclePhase::Tap, "now to be tapped");
        p.pointer(0, c[0].x, c[0].y);
        p.update(700);
        p.pointer(0, c[2].x, c[2].y);   // the wrong one: the second is missed
        p.pointer(0, c[2].x, c[2].y);   // now the third is due
        const auto hits = p.takeHits();
        CHECK(p.done() && p.result().hits == 2 && p.circles()[1].state == 3 && hits.size() == 3, "in order; a wrong one misses the due one");
        CHECK(p.circles()[0].state == 2 && p.circles()[2].state == 2, "quick taps are perfect");
    }
    // 4. Shape: the pentagram traced corner by corner (5 steps); time up keeps what is done.
    {
        TouchPlaySettings s;
        s.shapeTimeMs = 3000;
        TouchPlay p;
        p.start(PlayKind::Shape, 0, true, area, 1, s);
        CHECK(p.points() == 5 && p.trace().points().size() == 6, "a pentagram: 5 steps");
        const auto& pts = p.trace().points();
        p.pointer(0, pts[0].x, pts[0].y);
        for (int k = 1; k <= 3; k++) p.pointer(1, pts[(size_t)k].x, pts[(size_t)k].y);
        CHECK(p.index() == 3 && p.takeHits().size() == 3, "three corners: three steps");
        p.update(3000);
        CHECK(p.done() && p.result().hits == 3 && !p.result().failed, "time up: 3 of 5");
    }
    {   // mash
        TouchPlay p;
        p.start(PlayKind::Mash, 4, false, area, 1);
        for (int k = 0; k < 4; k++) p.pointer(0, 600, 200);
        CHECK(p.done() && p.result().hits == 4, "four taps");
    }
    CHECK(playKindFromString("bow") == PlayKind::Bow && playKindFromString("shape") == PlayKind::Shape &&
              playKindFromString("anything") == PlayKind::Track && std::string(playKindName(PlayKind::Mash)) == "mash", "names");
    if (g_fail) { std::printf("touch_play_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("touch_play_test: all passed\n");
    return 0;
}
