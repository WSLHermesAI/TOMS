// weapon_play_test.cpp -- toms::WeaponPlay rules and the shipped data/weapon_play.json (weapon_play.h). Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the data file is "data/weapon_play.json".
#include "weapon_play.h"

#include <cstdio>
#include <fstream>
#include <set>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    // 1. The owner's example (2026-10-20): Lv 5 back to the start, Lv 10 back 5 points, Lv 15 back 4.
    {
        const nlohmann::json j = nlohmann::json::parse(R"({"missBack":[{"lv":15,"back":4},{"lv":5,"back":"start"},{"lv":10,"back":5}]})");
        const WeaponPlay w = weaponPlayFromJson("example", j);
        CHECK(w.missBack.size() == 3 && w.missBack[0].lv == 5, "rules sorted by level");
        CHECK(missBackPoints(w, 1) == -1, "below every rule: back to the start");
        CHECK(missBackPoints(w, 5) == -1 && missBackPoints(w, 9) == -1, "Lv 5-9: back to the start");
        CHECK(missBackPoints(w, 10) == 5 && missBackPoints(w, 14) == 5, "Lv 10-14: back 5");
        CHECK(missBackPoints(w, 15) == 4 && missBackPoints(w, 99) == 4, "Lv 15+: back 4");
        CHECK(stepAfterMiss(w, 5, 7) == 0, "Lv 5: step 7 -> the start");
        CHECK(stepAfterMiss(w, 10, 7) == 2, "Lv 10: step 7 -> 2");
        CHECK(stepAfterMiss(w, 15, 3) == 0, "never below the start");
    }
    // 2. Lookup: own entry, else "default", else the built-in rule.
    {
        WeaponPlayTable t;
        CHECK(missBackPoints(t.get("anything"), 30) == -1, "no file: the built-in rule (back to the start)");
        t.weapons["default"] = weaponPlayFromJson("default", nlohmann::json::parse(R"({"missBack":[{"lv":1,"back":2}]})"));
        t.weapons["wand"] = weaponPlayFromJson("wand", nlohmann::json::parse(R"({"missBack":[{"lv":1,"back":1}],"missWaitMs":400})"));
        CHECK(t.get("wand").missWaitMs == 400 && missBackPoints(t.get("wand"), 1) == 1, "own entry");
        CHECK(missBackPoints(t.get("unknown_sword"), 1) == 2, "default entry");
    }
    // 3. The shipped file: readable, every key a weapon of items.json (or unarmed / default), and the default
    //    entry follows the owner's example.
    {
        const WeaponPlayTable t = loadWeaponPlay("data/weapon_play.json");
        CHECK(!t.weapons.empty(), "data/weapon_play.json loads");
        std::ifstream f("data/items.json");
        const nlohmann::json items = nlohmann::json::parse(f, nullptr, false);
        std::set<std::string> weapons = {"unarmed", "default"};
        if (items.is_object())
            for (auto it = items.begin(); it != items.end(); ++it)
                if (it.value().is_object() && it.value().value("type", std::string()) == "weapon") weapons.insert(it.key());
        for (const auto& [id, w] : t.weapons) {
            if (!weapons.count(id)) std::printf("FAIL: weapon_play.json key \"%s\" is not a weapon in items.json\n", id.c_str());
            if (!weapons.count(id)) g_fail++;
        }
        const WeaponPlay& d = t.get("default");
        CHECK(missBackPoints(d, 5) == -1 && missBackPoints(d, 10) == 5 && missBackPoints(d, 15) == 4,
              "the default entry is the owner's example");
    }

    if (g_fail) { std::printf("weapon_play_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("weapon_play_test: all passed\n");
    return 0;
}
