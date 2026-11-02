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

namespace {

nlohmann::json f_Spec(const std::string& l_strId, const std::string& l_strNext, const std::string& l_strRole = "normal",
                    const std::string& l_strStage = "", int l_iInAct = 1, const std::string& l_strAct = "ch_01") {
    nlohmann::json l_jJ;
    l_jJ["id"] = l_strId;
    l_jJ["act"] = l_strAct;
    l_jJ["indexInAct"] = l_iInAct;
    l_jJ["role"] = l_strRole;
    l_jJ["nextFloor"] = l_strNext.empty() ? nlohmann::json(nullptr) : nlohmann::json(l_strNext);
    if (!l_strStage.empty()) l_jJ["handAuthoredStage"] = l_strStage;
    l_jJ["name"] = "story." + std::string(1, 'a') + ".name";
    l_jJ["events"] = nlohmann::json::array({"e1", "e2"});
    return l_jJ;
}

// --- pure logic ----------------------------------------------------------------------------------

void f_TestOrdering() {
    // Deliberately handed to fromSpecs OUT of order: the chain must decide, not the input order.
    std::vector<nlohmann::json> l_vSpecs = {
        f_Spec("F03", "", "boss-stage", "stage01.json", 3),
        f_Spec("F01", "F02", "normal", "", 1),
        f_Spec("F02", "F03", "normal", "", 2),
    };
    toms::FloorTable l_oT = toms::FloorTable::f_FromSpecs(l_vSpecs);
    CHECK(l_oT.size() == 3, "3 floors expected, got %d", l_oT.size());
    CHECK(l_oT.f_At(1).m_strId == "F01", "chain head should be F01, got %s", l_oT.f_At(1).m_strId.c_str());
    CHECK(l_oT.f_At(2).m_strId == "F02", "second should be F02, got %s", l_oT.f_At(2).m_strId.c_str());
    CHECK(l_oT.f_At(3).m_strId == "F03", "third should be F03, got %s", l_oT.f_At(3).m_strId.c_str());
    CHECK(l_oT.f_At(1).m_iSeq == 1 && l_oT.f_At(3).m_iSeq == 3, "seq should follow chain order");
    CHECK(l_oT.f_At(3).f_IsBoss(), "role boss-stage should report isBoss()");
    CHECK(l_oT.f_At(3).m_strHandAuthoredStage == "stage01.json", "boss floor keeps its hand-authored map");
    CHECK(l_oT.f_At(1).m_iEventCount == 2, "eventCount should count the spec's events, got %d", l_oT.f_At(1).m_iEventCount);

    // next()/prev()/seqOf()/mapRelPath() are what the run and the hub actually call.
    CHECK(l_oT.f_Next("F01") == "F02", "next(F01) should be F02");
    CHECK(l_oT.f_Next("F03").empty(), "next() of the last floor should be empty");
    CHECK(l_oT.f_Prev("F02") == "F01", "prev(F02) should be F01");
    CHECK(l_oT.f_Prev("F01").empty(), "prev() of the first floor should be empty");
    CHECK(l_oT.f_SeqOf("F03") == 3, "seqOf(F03) should be 3");
    CHECK(l_oT.f_SeqOf("F99") == 0, "seqOf() of an unknown floor should be 0");
    CHECK(l_oT.f_Find("F02") != nullptr && l_oT.f_Find("F99") == nullptr, "find() should miss unknown ids");
    CHECK(l_oT.f_MapRelPath("F01") == "data/story/floors/F01.stage.json",
          "a normal floor's map is its generated grid, got %s", l_oT.f_MapRelPath("F01").c_str());
    CHECK(l_oT.f_MapRelPath("F03") == "data/stages/stage01.json",
          "a boss floor's map is the hand-authored stage, got %s", l_oT.f_MapRelPath("F03").c_str());

    // at() is used from draw/input paths that must never abort mid-frame.
    CHECK(l_oT.f_At(0).m_strId == "F01" && l_oT.f_At(99).m_strId == "F03", "at() should clamp instead of overrunning");
    CHECK(toms::FloorTable().f_At(1).m_strId.empty(), "an empty table's at() should be an empty floor");
}

void f_TestActOrdinal() {
    std::vector<nlohmann::json> l_vSpecs = { f_Spec("F01", "", "normal", "", 1, "ch_07") };
    toms::FloorTable l_oT = toms::FloorTable::f_FromSpecs(l_vSpecs);
    CHECK(l_oT.f_At(1).m_iActIndex == 7, "ch_07 should parse to act 7, got %d", l_oT.f_At(1).m_iActIndex);

    std::vector<nlohmann::json> l_vOdd = { f_Spec("F01", "", "normal", "", 1, "") };
    CHECK(toms::FloorTable::f_FromSpecs(l_vOdd).f_At(1).m_iActIndex == 1, "a missing act should fall back to 1");
}

void f_TestBrokenChainKeepsFloors() {
    // F02's nextFloor names a floor that does not exist: the tower is broken at that link, but no
    // floor may silently vanish -- a test/report has to be able to see the stranded one.
    std::vector<nlohmann::json> l_vSpecs = {
        f_Spec("F01", "F02"),
        f_Spec("F02", "F99"),
        f_Spec("F03", "F04"),
        f_Spec("F04", ""),
    };
    toms::FloorTable l_oT = toms::FloorTable::f_FromSpecs(l_vSpecs);
    CHECK(l_oT.size() == 4, "a dangling link must not drop floors, got %d", l_oT.size());
    CHECK(l_oT.f_At(1).m_strId == "F01" && l_oT.f_At(2).m_strId == "F02", "chain order should still start at the entrance");
    CHECK(l_oT.f_SeqOf("F03") > 0 && l_oT.f_SeqOf("F04") > 0, "stranded floors should still be in the table");
}

void f_TestCycleTerminates() {
    std::vector<nlohmann::json> l_vSpecs = { f_Spec("F01", "F02"), f_Spec("F02", "F01") };
    toms::FloorTable l_oT = toms::FloorTable::f_FromSpecs(l_vSpecs);
    CHECK(l_oT.size() == 2, "a cyclic chain should terminate and keep both floors, got %d", l_oT.size());
}

// --- the shipped data ----------------------------------------------------------------------------

void f_TestShippedTower() {
    toms::FloorTable l_oT = toms::FloorTable::f_LoadFromDirectory("data/story/floors");
    if (l_oT.f_Empty()) { CHECK(false, "data/story/floors did not load (run from the repo root)"); return; }

    CHECK(l_oT.size() == 70, "the tower should hold 70 floors, got %d", l_oT.size());
    for (int l_iI = 1; l_iI <= l_oT.size(); l_iI++) {
        const toms::FloorInfo& l_oF = l_oT.f_At(l_iI);
        char l_pszWant[8];
        snprintf(l_pszWant, sizeof(l_pszWant), "F%02d", l_iI);
        CHECK(l_oF.m_strId == l_pszWant, "floor %d should be %s, got %s", l_iI, l_pszWant, l_oF.m_strId.c_str());
        CHECK(l_oF.m_iSeq == l_iI, "%s should carry seq %d, got %d", l_oF.m_strId.c_str(), l_iI, l_oF.m_iSeq);
        CHECK(l_oF.m_iIndexInAct >= 1 && l_oF.m_iIndexInAct <= 7, "%s indexInAct out of range: %d", l_oF.m_strId.c_str(), l_oF.m_iIndexInAct);
        CHECK(l_oF.m_iActIndex == (l_iI - 1) / 7 + 1, "%s act %d does not match its position %d", l_oF.m_strId.c_str(), l_oF.m_iActIndex, l_iI);
        CHECK(l_oF.m_iIndexInAct == (l_iI - 1) % 7 + 1, "%s indexInAct %d does not match its position %d",
              l_oF.m_strId.c_str(), l_oF.m_iIndexInAct, l_iI);
        CHECK(!l_oF.m_strNameKey.empty(), "%s has no name key (the HUD needs one)", l_oF.m_strId.c_str());
        // actKey is the act id minus its underscore -- the form the story i18n keys use
        // ("story.ch01.title"), so the act id and the key stem cannot drift apart.
        std::string l_strWantActKey = l_oF.m_strAct;
        l_strWantActKey.erase(std::remove(l_strWantActKey.begin(), l_strWantActKey.end(), '_'), l_strWantActKey.end());
        CHECK(l_oF.m_strActKey == l_strWantActKey, "%s actKey should be '%s', got '%s'",
              l_oF.m_strId.c_str(), l_strWantActKey.c_str(), l_oF.m_strActKey.c_str());
        CHECK(!l_oF.m_strActKey.empty() && l_oF.m_strActKey.find('_') == std::string::npos,
              "%s actKey should carry no underscore, got '%s'", l_oF.m_strId.c_str(), l_oF.m_strActKey.c_str());
        CHECK(l_oF.m_iEventCount > 0, "%s carries no event slots", l_oF.m_strId.c_str());
    }

    // The chain is what makes stairs work; a mismatch here is a stranding bug.
    for (int l_iI = 1; l_iI < l_oT.size(); l_iI++) {
        CHECK(l_oT.f_At(l_iI).m_strNextFloor == l_oT.f_At(l_iI + 1).m_strId, "%s.nextFloor should be %s, got '%s'",
              l_oT.f_At(l_iI).m_strId.c_str(), l_oT.f_At(l_iI + 1).m_strId.c_str(), l_oT.f_At(l_iI).m_strNextFloor.c_str());
        CHECK(l_oT.f_Next(l_oT.f_At(l_iI).m_strId) == l_oT.f_At(l_iI + 1).m_strId, "next(%s) disagrees with the chain", l_oT.f_At(l_iI).m_strId.c_str());
    }
    CHECK(l_oT.f_At(l_oT.size()).m_strNextFloor.empty(), "the top floor should end the tower, got '%s'",
          l_oT.f_At(l_oT.size()).m_strNextFloor.c_str());
    CHECK(l_oT.f_Prev("F01").empty() && l_oT.f_Prev("F70") == "F69", "prev() should walk the chain backwards");

    // Per act: seven floors, the seventh a boss floor with a hand-authored map.
    std::vector<std::string> l_vBossMaps;
    int l_iBosses = 0, l_iNormals = 0;
    for (const toms::FloorInfo& l_oF : l_oT.f_All()) {
        if (l_oF.f_IsBoss()) {
            ++l_iBosses;
            CHECK(l_oF.m_iIndexInAct == 7, "%s is a boss floor but sits at indexInAct %d", l_oF.m_strId.c_str(), l_oF.m_iIndexInAct);
            CHECK(!l_oF.m_strHandAuthoredStage.empty(), "%s is a boss floor with no hand-authored map", l_oF.m_strId.c_str());
            FILE* l_pMapFp = fopen(("data/stages/" + l_oF.m_strHandAuthoredStage).c_str(), "rb");
            CHECK(l_pMapFp != nullptr, "%s names a map that does not exist: data/stages/%s",
                  l_oF.m_strId.c_str(), l_oF.m_strHandAuthoredStage.c_str());
            if (l_pMapFp) fclose(l_pMapFp);
            l_vBossMaps.push_back(l_oF.m_strHandAuthoredStage);
        } else {
            ++l_iNormals;
            CHECK(l_oF.m_strHandAuthoredStage.empty(), "%s is a normal floor but names a hand-authored map", l_oF.m_strId.c_str());
            const std::string l_strGrid = "data/story/floors/" + l_oF.m_strId + ".stage.json";
            FILE* l_pGridFp = fopen(l_strGrid.c_str(), "rb");
            CHECK(l_pGridFp != nullptr, "%s has no generated grid at %s", l_oF.m_strId.c_str(), l_strGrid.c_str());
            if (l_pGridFp) fclose(l_pGridFp);
        }
    }
    CHECK(l_iBosses == 10, "there should be 10 act-boss floors, got %d", l_iBosses);
    CHECK(l_iNormals == 60, "there should be 60 generated floors, got %d", l_iNormals);
    std::sort(l_vBossMaps.begin(), l_vBossMaps.end());
    CHECK(std::unique(l_vBossMaps.begin(), l_vBossMaps.end()) == l_vBossMaps.end(),
          "two boss floors share the same hand-authored map");

    // The grid's own exit must agree with the table: the generator puts the goal on 'U' (far from
    // the entrance) and the way back on 'D' (beside it), which is exactly how the engine's stairs
    // read st.up/st.down. If these disagree, stairs lead somewhere the table does not expect.
    int l_iCheckedGrids = 0;
    for (const toms::FloorInfo& l_oF : l_oT.f_All()) {
        if (l_oF.f_IsBoss()) continue;
        std::string l_strPath = "data/story/floors/" + l_oF.m_strId + ".stage.json";
        FILE* l_pFp = fopen(l_strPath.c_str(), "rb");
        if (!l_pFp) continue;
        std::string l_strBuf; char l_pszChunk[8192]; size_t l_nN;
        while ((l_nN = fread(l_pszChunk, 1, sizeof(l_pszChunk), l_pFp)) > 0) l_strBuf.append(l_pszChunk, l_nN);
        fclose(l_pFp);
        nlohmann::json l_jJ;
        try { l_jJ = nlohmann::json::parse(l_strBuf); } catch (...) { CHECK(false, "%s is not valid JSON", l_oF.m_strId.c_str()); continue; }
        ++l_iCheckedGrids;
        if (l_jJ.contains("connect") && l_jJ["connect"].contains("up")) {
            const std::string l_strUp = l_jJ["connect"]["up"].is_string() ? l_jJ["connect"]["up"].get<std::string>() : std::string();
            CHECK(l_strUp == l_oF.m_strNextFloor, "%s grid says connect.up='%s' but the table says nextFloor='%s'",
                  l_oF.m_strId.c_str(), l_strUp.c_str(), l_oF.m_strNextFloor.c_str());
        }
        int l_iExits = 0, l_iBacks = 0;
        for (const auto& l_strRow : l_jJ.value("tiles", std::vector<std::string>())) {
            for (char l_cC : l_strRow) { if (l_cC == 'U') ++l_iExits; else if (l_cC == 'D') ++l_iBacks; }
        }
        CHECK(l_iExits == 1, "%s should have exactly one exit tile ('U'), found %d", l_oF.m_strId.c_str(), l_iExits);
        // M9 (stair alignment): F01 is the chain's true head -- there is no floor below it, so it
        // correctly has zero 'D' tiles now (it used to carry one anyway, but that tile was always
        // inert: floors_.prev("F01") is empty regardless of what the grid itself contains).
        // Every other floor's stairs_down is real and forced to match the previous floor's exit.
        if (l_oF.m_strId == "F01") CHECK(l_iBacks == 0, "F01 (the chain head) should have no 'D' tile, found %d", l_iBacks);
        else CHECK(l_iBacks >= 1, "%s has no way back down ('D'), found %d", l_oF.m_strId.c_str(), l_iBacks);
    }
    CHECK(l_iCheckedGrids == 60, "expected to read 60 grids, read %d", l_iCheckedGrids);

    fprintf(stderr, "  data: %d floors, %d acts, %d boss floors on %d hand-authored maps, %d grids cross-checked\n",
            l_oT.size(), l_oT.f_At(l_oT.size()).m_iActIndex, l_iBosses, (int)l_vBossMaps.size(), l_iCheckedGrids);
}

} // namespace

int main() {
    f_TestOrdering();
    f_TestActOrdinal();
    f_TestBrokenChainKeepsFloors();
    f_TestCycleTerminates();
    f_TestShippedTower();
    printf("floor_table_test: %s (%d checks)\n", g_iFails == 0 ? "ALL PASS" : "FAILED", g_iChecks);
    return g_iFails == 0 ? 0 : 1;
}
