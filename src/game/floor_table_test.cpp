// floor_table_test.cpp — headless gate for S3.5 step (a)/(b): the 70-floor tower as data.
//
// Two halves, the same shape as footprint_test.cpp:
//   1. pure logic -- FloorTable::fromSpecs ordering, prev/next derivation, and the degenerate cases
//      (a broken chain link, a cycle) that must not drop floors or spin;
//   2. the shipped data -- the real data/story/floors set is held to the contract the run depends on:
//      F01..F70 in chain order, ten acts of seven, one boss floor per act naming a hand-authored map
//      that exists, every normal floor having a generated grid, and each grid's own exit ('U' tile /
//      connect.up) agreeing with the table's nextFloor. A floor whose map is missing, or a chain link
//      pointing nowhere, would strand the player mid-tower -- this catches it before it ships.
//
// Run: ./floor_table_test   (cwd = repo root, like the other tests)
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "floor_table.h"

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

namespace {

nlohmann::json spec(const std::string& id, const std::string& next, const std::string& role = "normal",
                    const std::string& stage = "", int inAct = 1, const std::string& act = "ch_01") {
    nlohmann::json j;
    j["id"] = id;
    j["act"] = act;
    j["indexInAct"] = inAct;
    j["role"] = role;
    j["nextFloor"] = next.empty() ? nlohmann::json(nullptr) : nlohmann::json(next);
    if (!stage.empty()) j["handAuthoredStage"] = stage;
    j["name"] = "story." + std::string(1, 'a') + ".name";
    j["events"] = nlohmann::json::array({"e1", "e2"});
    return j;
}

// --- pure logic ----------------------------------------------------------------------------------

void testOrdering() {
    // Deliberately handed to fromSpecs OUT of order: the chain must decide, not the input order.
    std::vector<nlohmann::json> specs = {
        spec("F03", "", "boss-stage", "stage01.json", 3),
        spec("F01", "F02", "normal", "", 1),
        spec("F02", "F03", "normal", "", 2),
    };
    toms::FloorTable t = toms::FloorTable::fromSpecs(specs);
    CHECK(t.size() == 3, "3 floors expected, got %d", t.size());
    CHECK(t.at(1).id == "F01", "chain head should be F01, got %s", t.at(1).id.c_str());
    CHECK(t.at(2).id == "F02", "second should be F02, got %s", t.at(2).id.c_str());
    CHECK(t.at(3).id == "F03", "third should be F03, got %s", t.at(3).id.c_str());
    CHECK(t.at(1).seq == 1 && t.at(3).seq == 3, "seq should follow chain order");
    CHECK(t.at(3).isBoss(), "role boss-stage should report isBoss()");
    CHECK(t.at(3).handAuthoredStage == "stage01.json", "boss floor keeps its hand-authored map");
    CHECK(t.at(1).eventCount == 2, "eventCount should count the spec's events, got %d", t.at(1).eventCount);

    // next()/prev()/seqOf()/mapRelPath() are what the run and the hub actually call.
    CHECK(t.next("F01") == "F02", "next(F01) should be F02");
    CHECK(t.next("F03").empty(), "next() of the last floor should be empty");
    CHECK(t.prev("F02") == "F01", "prev(F02) should be F01");
    CHECK(t.prev("F01").empty(), "prev() of the first floor should be empty");
    CHECK(t.seqOf("F03") == 3, "seqOf(F03) should be 3");
    CHECK(t.seqOf("F99") == 0, "seqOf() of an unknown floor should be 0");
    CHECK(t.find("F02") != nullptr && t.find("F99") == nullptr, "find() should miss unknown ids");
    CHECK(t.mapRelPath("F01") == "data/story/floors/F01.stage.json",
          "a normal floor's map is its generated grid, got %s", t.mapRelPath("F01").c_str());
    CHECK(t.mapRelPath("F03") == "data/stages/stage01.json",
          "a boss floor's map is the hand-authored stage, got %s", t.mapRelPath("F03").c_str());

    // at() is used from draw/input paths that must never abort mid-frame.
    CHECK(t.at(0).id == "F01" && t.at(99).id == "F03", "at() should clamp instead of overrunning");
    CHECK(toms::FloorTable().at(1).id.empty(), "an empty table's at() should be an empty floor");
}

void testActOrdinal() {
    std::vector<nlohmann::json> specs = { spec("F01", "", "normal", "", 1, "ch_07") };
    toms::FloorTable t = toms::FloorTable::fromSpecs(specs);
    CHECK(t.at(1).actIndex == 7, "ch_07 should parse to act 7, got %d", t.at(1).actIndex);

    std::vector<nlohmann::json> odd = { spec("F01", "", "normal", "", 1, "") };
    CHECK(toms::FloorTable::fromSpecs(odd).at(1).actIndex == 1, "a missing act should fall back to 1");
}

void testBrokenChainKeepsFloors() {
    // F02's nextFloor names a floor that does not exist: the tower is broken at that link, but no
    // floor may silently vanish -- a test/report has to be able to see the stranded one.
    std::vector<nlohmann::json> specs = {
        spec("F01", "F02"),
        spec("F02", "F99"),
        spec("F03", "F04"),
        spec("F04", ""),
    };
    toms::FloorTable t = toms::FloorTable::fromSpecs(specs);
    CHECK(t.size() == 4, "a dangling link must not drop floors, got %d", t.size());
    CHECK(t.at(1).id == "F01" && t.at(2).id == "F02", "chain order should still start at the entrance");
    CHECK(t.seqOf("F03") > 0 && t.seqOf("F04") > 0, "stranded floors should still be in the table");
}

void testCycleTerminates() {
    std::vector<nlohmann::json> specs = { spec("F01", "F02"), spec("F02", "F01") };
    toms::FloorTable t = toms::FloorTable::fromSpecs(specs);
    CHECK(t.size() == 2, "a cyclic chain should terminate and keep both floors, got %d", t.size());
}

// --- the shipped data ----------------------------------------------------------------------------

void testShippedTower() {
    toms::FloorTable t = toms::FloorTable::loadFromDirectory("data/story/floors");
    if (t.empty()) { CHECK(false, "data/story/floors did not load (run from the repo root)"); return; }

    CHECK(t.size() == 70, "the tower should hold 70 floors, got %d", t.size());
    for (int i = 1; i <= t.size(); i++) {
        const toms::FloorInfo& f = t.at(i);
        char want[8];
        snprintf(want, sizeof(want), "F%02d", i);
        CHECK(f.id == want, "floor %d should be %s, got %s", i, want, f.id.c_str());
        CHECK(f.seq == i, "%s should carry seq %d, got %d", f.id.c_str(), i, f.seq);
        CHECK(f.indexInAct >= 1 && f.indexInAct <= 7, "%s indexInAct out of range: %d", f.id.c_str(), f.indexInAct);
        CHECK(f.actIndex == (i - 1) / 7 + 1, "%s act %d does not match its position %d", f.id.c_str(), f.actIndex, i);
        CHECK(f.indexInAct == (i - 1) % 7 + 1, "%s indexInAct %d does not match its position %d",
              f.id.c_str(), f.indexInAct, i);
        CHECK(!f.nameKey.empty(), "%s has no name key (the HUD needs one)", f.id.c_str());
        CHECK(f.eventCount > 0, "%s carries no event slots", f.id.c_str());
    }

    // The chain is what makes stairs work; a mismatch here is a stranding bug.
    for (int i = 1; i < t.size(); i++) {
        CHECK(t.at(i).nextFloor == t.at(i + 1).id, "%s.nextFloor should be %s, got '%s'",
              t.at(i).id.c_str(), t.at(i + 1).id.c_str(), t.at(i).nextFloor.c_str());
        CHECK(t.next(t.at(i).id) == t.at(i + 1).id, "next(%s) disagrees with the chain", t.at(i).id.c_str());
    }
    CHECK(t.at(t.size()).nextFloor.empty(), "the top floor should end the tower, got '%s'",
          t.at(t.size()).nextFloor.c_str());
    CHECK(t.prev("F01").empty() && t.prev("F70") == "F69", "prev() should walk the chain backwards");

    // Per act: seven floors, the seventh a boss floor with a hand-authored map.
    std::vector<std::string> bossMaps;
    int bosses = 0, normals = 0;
    for (const toms::FloorInfo& f : t.all()) {
        if (f.isBoss()) {
            ++bosses;
            CHECK(f.indexInAct == 7, "%s is a boss floor but sits at indexInAct %d", f.id.c_str(), f.indexInAct);
            CHECK(!f.handAuthoredStage.empty(), "%s is a boss floor with no hand-authored map", f.id.c_str());
            FILE* mapFp = fopen(("data/stages/" + f.handAuthoredStage).c_str(), "rb");
            CHECK(mapFp != nullptr, "%s names a map that does not exist: data/stages/%s",
                  f.id.c_str(), f.handAuthoredStage.c_str());
            if (mapFp) fclose(mapFp);
            bossMaps.push_back(f.handAuthoredStage);
        } else {
            ++normals;
            CHECK(f.handAuthoredStage.empty(), "%s is a normal floor but names a hand-authored map", f.id.c_str());
            const std::string grid = "data/story/floors/" + f.id + ".stage.json";
            FILE* gridFp = fopen(grid.c_str(), "rb");
            CHECK(gridFp != nullptr, "%s has no generated grid at %s", f.id.c_str(), grid.c_str());
            if (gridFp) fclose(gridFp);
        }
    }
    CHECK(bosses == 10, "there should be 10 act-boss floors, got %d", bosses);
    CHECK(normals == 60, "there should be 60 generated floors, got %d", normals);
    std::sort(bossMaps.begin(), bossMaps.end());
    CHECK(std::unique(bossMaps.begin(), bossMaps.end()) == bossMaps.end(),
          "two boss floors share the same hand-authored map");

    // The grid's own exit must agree with the table: the generator puts the goal on 'U' (far from
    // the entrance) and the way back on 'D' (beside it), which is exactly how the engine's stairs
    // read st.up/st.down. If these disagree, stairs lead somewhere the table does not expect.
    int checkedGrids = 0;
    for (const toms::FloorInfo& f : t.all()) {
        if (f.isBoss()) continue;
        std::string path = "data/story/floors/" + f.id + ".stage.json";
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) continue;
        std::string buf; char chunk[8192]; size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) buf.append(chunk, n);
        fclose(fp);
        nlohmann::json j;
        try { j = nlohmann::json::parse(buf); } catch (...) { CHECK(false, "%s is not valid JSON", f.id.c_str()); continue; }
        ++checkedGrids;
        if (j.contains("connect") && j["connect"].contains("up")) {
            const std::string up = j["connect"]["up"].is_string() ? j["connect"]["up"].get<std::string>() : std::string();
            CHECK(up == f.nextFloor, "%s grid says connect.up='%s' but the table says nextFloor='%s'",
                  f.id.c_str(), up.c_str(), f.nextFloor.c_str());
        }
        int exits = 0, backs = 0;
        for (const auto& row : j.value("tiles", std::vector<std::string>())) {
            for (char c : row) { if (c == 'U') ++exits; else if (c == 'D') ++backs; }
        }
        CHECK(exits == 1, "%s should have exactly one exit tile ('U'), found %d", f.id.c_str(), exits);
        CHECK(backs >= 1, "%s has no way back down ('D'), found %d", f.id.c_str(), backs);
    }
    CHECK(checkedGrids == 60, "expected to read 60 grids, read %d", checkedGrids);

    fprintf(stderr, "  data: %d floors, %d acts, %d boss floors on %d hand-authored maps, %d grids cross-checked\n",
            t.size(), t.at(t.size()).actIndex, bosses, (int)bossMaps.size(), checkedGrids);
}

} // namespace

int main() {
    testOrdering();
    testActOrdinal();
    testBrokenChainKeepsFloors();
    testCycleTerminates();
    testShippedTower();
    printf("floor_table_test: %s (%d checks)\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_checks);
    return g_fails == 0 ? 0 : 1;
}
