// combo_play_test.cpp -- Battle v3's point play (combo_play.h) and the weapon levels (weapon_play.h). Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the data file is "data/weapon_play.json".
#include "combo_play.h"

#include <cmath>
#include <cstdio>

using namespace toms;

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

// Runs the marker until it is within `w` of position p (or the combo ends).
void runTo(ComboPlay& c, float p, float w = 0.01f) {
    for (int i = 0; i < 20000 && c.running() && std::fabs(c.marker() - p) > w; i++) c.update(1);
}
}  // namespace

int main() {
    // 1. Weapon levels: points and bonuses by level, the level from the wins.
    {
        const WeaponPlay w = weaponPlayFromJson("bow", nlohmann::json::parse(R"({
            "levels": [ {"lv": 3, "points": 4, "stats": {"str": 12, "agi": 10}}, {"lv": 1, "points": 3, "stats": {"str": 10}},
                        {"lv": 5, "points": 9} ],
            "usesToLevel": [2, 3, 4, 5]})"));
        CHECK(w.levels.size() == 3 && w.levels[0].lv == 1, "levels sorted");
        CHECK(weaponLevelEntry(w, 1).points == 3 && weaponLevelEntry(w, 2).points == 3 && weaponLevelEntry(w, 3).points == 4,
              "the highest entry at or below the level");
        CHECK(weaponLevelEntry(w, 5).points == kMaxComboPoints, "points capped at five");
        CHECK(weaponLevelEntry(w, 3).stats.at("agi") == 10, "the level's stats");
        CHECK(weaponLevelFromUses(w, 0) == 1 && weaponLevelFromUses(w, 1) == 1 && weaponLevelFromUses(w, 2) == 2 &&
                  weaponLevelFromUses(w, 5) == 3 && weaponLevelFromUses(w, 999) == 5,
              "levels from wins (2, then 3 more, ...), capped at the last entry");
        CHECK(usesToNextLevel(w, 1) == 2 && usesToNextLevel(w, 2) == 3 && usesToNextLevel(w, 5) == 0, "wins to the next level");
        const WeaponPlay none = weaponPlayFromJson("x", nlohmann::json::object());
        CHECK(weaponLevelEntry(none, 7).points == 3 && weaponMaxLevel(none) == 1 && none.turnBack, "no data: Lv 1, 3 points");
    }

    ComboSettings s;
    s.points = 3;
    s.sweepMs = 1000;
    s.maxSweeps = 3;
    s.hitWindow = 0.05f;
    s.missBack = -1;
    // 2. Three points at 1/4, 2/4, 3/4; hit them all: the combo ends at once, power 100.
    {
        ComboPlay c;
        c.start(s, 0.0f);
        CHECK(c.running() && c.pointsAt().size() == 3 && std::fabs(c.pointsAt()[1] - 0.5f) < 1e-5f, "points evenly on the track");
        for (float p : {0.25f, 0.5f, 0.75f}) {
            runTo(c, p);
            CHECK(c.tap() == ComboPlay::Tap::Hit, "a tap on a point hits it");
        }
        CHECK(c.done() && c.result().hits == 3 && c.result().power() == 100.0f && !c.result().failed, "all hit: done, full power");
    }
    // 3. Charge makes the marker faster: with a full charge (speedUp 1) the first point comes in half the time.
    {
        ComboPlay slow, fast;
        slow.start(s, 0.0f);
        fast.start(s, 1.0f);
        slow.update(100);
        fast.update(100);
        CHECK(std::fabs(fast.marker() - 2.0f * slow.marker()) < 1e-4f, "a full charge: twice as fast");
        CHECK(std::fabs(fast.result().charge) < 1e-6f && fast.running(), "the charge is reported at the end");
    }
    // 4. A miss takes the combo back (to the start here); two in a row are a fail.
    {
        ComboPlay c;
        c.start(s, 0.5f);
        runTo(c, 0.25f);
        c.tap();
        runTo(c, 0.4f);
        CHECK(c.tap() == ComboPlay::Tap::Miss && c.hits() == 0 && c.missesInRow() == 1, "a miss: back to the start");
        runTo(c, 0.5f);
        CHECK(c.tap() == ComboPlay::Tap::Hit && c.missesInRow() == 0, "a hit resets the misses in a row");
        runTo(c, 0.6f);
        c.tap();
        runTo(c, 0.65f);
        CHECK(c.tap() == ComboPlay::Tap::Miss && c.done() && c.result().failed && c.result().power() == 0.0f,
              "two misses in a row: a fail (power 0, a plain attack)");
    }
    // 5. missBack 1 takes back only the newest hit; missWaitMs ignores taps for a while.
    {
        ComboSettings b = s;
        b.missBack = 1;
        b.missWaitMs = 200;
        ComboPlay c;
        c.start(b, 0.0f);
        runTo(c, 0.25f);
        c.tap();
        runTo(c, 0.5f);
        c.tap();
        runTo(c, 0.6f);
        CHECK(c.tap() == ComboPlay::Tap::Miss && c.hits() == 1 && c.pointsHit()[0] && !c.pointsHit()[1], "back one: the newest hit lost");
        CHECK(c.waiting() && c.tap() == ComboPlay::Tap::Ignored, "after a miss, taps wait");
    }
    // 6. Turning back: a missed point can be taken on the way back; the sweeps run out.
    {
        ComboPlay c;
        c.start(s, 0.0f);
        runTo(c, 0.75f);
        c.tap();               // the last point first: the order is free
        runTo(c, 1.0f, 0.0f);  // the end: the marker turns back
        c.update(1);
        runTo(c, 0.25f);
        CHECK(c.running() && c.tap() == ComboPlay::Tap::Hit && c.hits() == 2, "turned back: point 1 taken on the way back");
        while (c.running()) c.update(10);
        CHECK(c.done() && !c.result().failed && c.result().hits == 2 && std::fabs(c.result().power() - 200.0f / 3.0f) < 0.01f,
              "the sweeps ran out: the hits so far");
    }
    // 7. No turning back (the sword): one sweep ends the combo.
    {
        ComboSettings one = s;
        one.turnBack = false;
        ComboPlay c;
        c.start(one, 0.0f);
        int ms = 0;
        while (c.running() && ms < 5000) { c.update(10); ms += 10; }
        CHECK(c.done() && ms <= 1010 && c.result().hits == 0, "one sweep, then done");
    }
    // 8. The shipped data: every weapon has levels with points, and the starting bow starts at 3 points.
    {
        const WeaponPlayTable t = loadWeaponPlay("data/weapon_play.json");
        const WeaponPlay& bow = t.get("bow");
        CHECK(!bow.levels.empty() && weaponLevelEntry(bow, 1).points == 3, "bow: Lv 1 has 3 points");
        CHECK(weaponLevelEntry(bow, weaponMaxLevel(bow)).points == kMaxComboPoints, "bow: the top level has five");
        CHECK(!bow.usesToLevel.empty(), "bow: wins per level");
        const ComboSettings cs = comboSettings(bow, 1);
        CHECK(cs.points == 3 && cs.sweepMs == bow.sweepMs, "comboSettings from the weapon");
    }

    if (g_fail) { std::printf("combo_play_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("combo_play_test: OK\n");
    return 0;
}
