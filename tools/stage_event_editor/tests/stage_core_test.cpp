// stage_core_test.cpp -- the native stage editor's rules (tools/stage_event_editor/core) on the real floors.
//
//   stage_core_test <TOMS folder>
//
// Checks: every floor loads and reads back unchanged (an untouched floor is not "changed"); painting, moving the
// reserved door / stair chars, object settings and undo; the checks and the story check run on every floor; auto-place
// is repeatable and uses the HTML editor's random numbers (values from node); the floor index built here is the one on
// disk, byte for byte (tools/web_editors/floor_index.js wrote it).
#include "event_project.h"
#include "stage_checks.h"
#include "stage_project.h"
#include "stage_save.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <string>

using namespace stage;

static int g_iChecks = 0;
static int g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool f_Near(double l_dA, double l_dB) { return std::fabs(l_dA - l_dB) < 1e-12; }

static void f_TestRandom() {
    // node: hashStr("1:F02") = 3257597346, then mulberry gives these three
    CHECK(f_HashStr("1:F02") == 3257597346u, "hashStr(1:F02) = %u", f_HashStr("1:F02"));
    auto l_fnR = f_Mulberry(f_HashStr("1:F02"));
    const double l_dA = l_fnR(), l_dB = l_fnR(), l_dC = l_fnR();
    CHECK(f_Near(l_dA, 0.19637801591306925) && f_Near(l_dB, 0.4360330330673605) && f_Near(l_dC, 0.9664091821759939),
          "mulberry(1:F02) = %.17g %.17g %.17g", l_dA, l_dB, l_dC);
    CHECK(f_HashStr("42:F10") == 834627190u, "hashStr(42:F10)");
    auto l_fnR2 = f_Mulberry(f_HashStr("42:F10"));
    CHECK(f_Near(l_fnR2(), 0.28418486448936164), "mulberry(42:F10)");
}

static void f_TestLoad(Project& l_oP) {
    CHECK(l_oP.m_vFloors.size() == 70, "70 floors (%zu)", l_oP.m_vFloors.size());
    CHECK(!l_oP.m_vEvents.empty() && l_oP.f_Event("ev_village_well"), "events loaded, ev_village_well defined");
    CHECK(!l_oP.m_vSprites.empty(), "sprite names loaded");
    CHECK(l_oP.f_TypeRec("monster:slime") && l_oP.f_TypeRec("item:potion_red"), "type records: slime, potion_red");
    int l_iSame = 0;
    for (const Floor& l_oF : l_oP.m_vFloors) {
        StageDoc& l_oD = l_oP.f_Doc(l_oF.m_strId);
        if (!l_oP.f_IsDirty(l_oF.m_strId)) l_iSame++;
        else std::printf("  note: %s reads back changed (stage %s, spec %s)\n", l_oF.m_strId.c_str(),
                         l_oP.f_StageText(l_oD) == l_oD.m_strOrigStage ? "same" : "differs", l_oP.f_SpecText(l_oD) == l_oD.m_strOrigSpec ? "same" : "differs");
        // the file as written by either editor: what this editor would write for it unchanged
        std::string l_strRaw;
        if (f_ReadText(l_oP.f_Abs(l_oF.m_strStagePath), l_strRaw)) CHECK(l_strRaw == l_oD.m_strOrigStage, "%s: written back byte for byte", l_oF.m_strStagePath.c_str());
        if (f_ReadText(l_oP.f_Abs(l_oF.m_strSpecPath), l_strRaw)) CHECK(l_strRaw == l_oD.m_strOrigSpec, "%s: written back byte for byte", l_oF.m_strSpecPath.c_str());
    }
    CHECK(l_iSame == (int)l_oP.m_vFloors.size(), "untouched floors are not changed (%d of %zu)", l_iSame, l_oP.m_vFloors.size());
    CHECK(f_ChangedFiles(l_oP).empty(), "nothing to save after loading (%zu)", f_ChangedFiles(l_oP).size());
}

static void f_TestEdits(Project& l_oP) {
    StageDoc& l_oD = l_oP.f_Doc("F02");
    // a free floor cell
    int l_iFx = -1, l_iFy = -1;
    for (int l_iY = 1; l_iY < l_oD.f_Height() - 1 && l_iFx < 0; l_iY++)
        for (int l_iX = 1; l_iX < l_oD.f_Width() - 1; l_iX++) if (l_oD.f_At(l_iX, l_iY) == '.') { l_iFx = l_iX; l_iFy = l_iY; break; }
    CHECK(l_iFx >= 0, "F02 has a free cell");
    l_oP.f_BeginEdit(l_oD);
    CHECK(l_oP.f_PutSem(l_oD, l_iFx, l_iFy, "wall"), "paint a wall");
    CHECK(l_oP.f_CommitEdit(l_oD, "Paint wall"), "the edit is an undo step");
    CHECK(l_oP.f_IsDirty("F02"), "F02 changed");
    CHECK(l_oP.f_Undo(l_oD), "undo");
    CHECK(!l_oP.f_IsDirty("F02"), "after undo F02 is as on disk");
    CHECK(l_oP.f_Redo(l_oD) && l_oP.f_IsDirty("F02"), "redo");
    l_oP.f_Undo(l_oD);

    // an object needs free floor; a monster's footprint; the same object again does nothing
    const PlaceCheck l_oOnWall = l_oP.f_PlaceCheck(l_oD, 0, 0, "monster:slime");
    CHECK(!l_oOnWall.m_bOk && l_oOnWall.m_eWhy == PlaceCheck::Wall, "nothing goes on a wall");
    const PlaceCheck l_oOnFree = l_oP.f_PlaceCheck(l_oD, l_iFx, l_iFy, "item:coin");
    CHECK(l_oOnFree.m_bOk, "an item on free floor");

    // an event the floor does not list is added to its spec
    bool l_bListed = false;
    l_oP.f_BeginEdit(l_oD);
    l_oP.f_PutSem(l_oD, l_iFx, l_iFy, "event:ev_village_well_test", &l_bListed);
    CHECK(l_bListed, "painting an unlisted event lists it");
    l_oP.f_CommitEdit(l_oD, "event");
    l_oP.f_Undo(l_oD);
    CHECK(!l_oP.f_IsDirty("F02"), "undo removes the listed event too");

    // the reserved chars: a door always gets 'y'; whatever used 'y' moves to a free char
    StageDoc& l_oD3 = l_oP.f_Doc("F03");
    l_oP.f_BeginEdit(l_oD3);
    l_oD3.m_jSt["legend"]["y"] = "item:coin";
    const char l_cYc = l_oP.f_CharFor(l_oD3, "door:yellow");
    CHECK(l_cYc == 'y' && l_oD3.m_jSt["legend"]["y"] == "door:yellow", "door:yellow takes 'y'");
    bool l_bMoved = false;
    for (auto& [l_strC, l_jV] : l_oD3.m_jSt["legend"].items()) if (l_jV == "item:coin" && l_strC != "y") l_bMoved = true;
    CHECK(l_bMoved, "the item that used 'y' moved to another char");
    l_oP.f_CommitEdit(l_oD3, "reserved");
    l_oP.f_Undo(l_oD3);
    CHECK(!l_oP.f_IsDirty("F03"), "undo restores the legend");

    // object settings go with the object: erased with it, kept with a move
    const auto l_optVillager = l_oP.f_Find(l_oD, "npc:villager");
    CHECK(l_optVillager.has_value(), "F02 has the villager");
    if (l_optVillager) {
        l_oP.f_BeginEdit(l_oD);
        l_oD.m_jSt["objects"][std::to_string(l_optVillager->m_iX) + "," + std::to_string(l_optVillager->m_iY)] = Json{{"events", Json::array({"ev_village_well"})}};
        CHECK(Project::f_ObjOf(l_oD, l_optVillager->m_iX, l_optVillager->m_iY) != nullptr, "object settings set");
        l_oP.f_PutSem(l_oD, l_optVillager->m_iX, l_optVillager->m_iY, "floor");
        CHECK(Project::f_ObjOf(l_oD, l_optVillager->m_iX, l_optVillager->m_iY) == nullptr, "painting over drops them");
        l_oP.f_CommitEdit(l_oD, "objects");
        l_oP.f_Undo(l_oD);
        CHECK(!l_oP.f_IsDirty("F02"), "undo restores the cell");
    }

    // stable object ids (docs/gameplay/05_SAVE_SYSTEM.md): every saved-state object has a uid, which moves with it; a new object
    // gets a fresh one and a copied (duplicate) uid is replaced
    if (const auto l_optMon = l_oP.f_Find(l_oD, "monster:slime")) {
        const Json* l_pO = Project::f_ObjOf(l_oD, l_optMon->m_iX, l_optMon->m_iY);
        CHECK(l_pO && l_pO->contains("uid"), "a shipped monster has a uid");
        const std::string l_strUid = l_pO ? l_pO->value("uid", std::string()) : std::string();
        int l_iFx = -1, l_iFy = -1;
        for (int l_iY = 0; l_iY < l_oD.f_Height() && l_iFx < 0; l_iY++)
            for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++)
                if (l_oP.f_SemAt(l_oD, l_iX, l_iY) == "floor") { l_iFx = l_iX; l_iFy = l_iY; break; }
        l_oP.f_BeginEdit(l_oD);
        const char l_cCh = l_oD.m_vGrid[(size_t)l_optMon->m_iY][(size_t)l_optMon->m_iX];
        l_oD.m_vGrid[(size_t)l_optMon->m_iY][(size_t)l_optMon->m_iX] = '.';
        l_oD.m_vGrid[(size_t)l_iFy][(size_t)l_iFx] = l_cCh;
        const Json l_jKeep = *Project::f_ObjOf(l_oD, l_optMon->m_iX, l_optMon->m_iY);
        Project::f_DropObj(l_oD, l_optMon->m_iX, l_optMon->m_iY);
        l_oD.m_jSt["objects"][std::to_string(l_iFx) + "," + std::to_string(l_iFy)] = l_jKeep;
        l_oP.f_CommitEdit(l_oD, "move");
        const Json* l_pMoved = Project::f_ObjOf(l_oD, l_iFx, l_iFy);
        CHECK(l_pMoved && l_pMoved->value("uid", std::string()) == l_strUid, "a moved object keeps its uid");
        l_oP.f_Undo(l_oD);
        l_oP.f_BeginEdit(l_oD);
        l_oP.f_PutSem(l_oD, l_iFx, l_iFy, "monster:bat");
        l_oP.f_CommitEdit(l_oD, "new");
        const Json* l_pFresh = Project::f_ObjOf(l_oD, l_iFx, l_iFy);
        CHECK(l_pFresh && !l_pFresh->value("uid", std::string()).empty() && l_pFresh->value("uid", std::string()) != l_strUid,
              "a new object gets its own uid");
        l_oP.f_BeginEdit(l_oD);
        l_oD.m_jSt["objects"][std::to_string(l_iFx) + "," + std::to_string(l_iFy)]["uid"] = l_strUid;   // as if its entry were copied
        l_oP.f_CommitEdit(l_oD, "dup");
        const Json* l_pDup = Project::f_ObjOf(l_oD, l_iFx, l_iFy);
        const Json* l_pOrig = Project::f_ObjOf(l_oD, l_optMon->m_iX, l_optMon->m_iY);
        CHECK(l_pDup && l_pOrig && l_pDup->value("uid", std::string()) != l_pOrig->value("uid", std::string()), "a duplicate uid is replaced");
        l_oP.f_Undo(l_oD);
        l_oP.f_Undo(l_oD);
        CHECK(!l_oP.f_IsDirty("F02"), "undo restores the uids");
    }
}

static void f_TestChecks(Project& l_oP) {
    int l_iErrors = 0, l_iWarnings = 0;
    for (const Floor& l_oF : l_oP.m_vFloors)
        for (const Issue& l_oI : f_Problems(l_oP, l_oP.f_Doc(l_oF.m_strId))) (l_oI.m_eSev == Sev::Error ? l_iErrors : l_iWarnings)++;
    std::printf("  checks: %d errors, %d warnings/info on %zu floors\n", l_iErrors, l_iWarnings, l_oP.m_vFloors.size());
    const std::vector<StoryRow>& l_vRows = f_StoryCheck(l_oP);
    CHECK(l_vRows.size() == l_oP.m_vFloors.size(), "one story row per floor");
    CHECK(!l_vRows.empty() && l_vRows[0].m_strFid == "F01", "the story starts on F01");
    int l_iSe = 0, l_iSw = 0;
    for (const StoryRow& l_oR : l_vRows) for (const Issue& l_oI : l_oR.m_vIssues) (l_oI.m_eSev == Sev::Error ? l_iSe : l_iSw)++;
    std::printf("  story: %d blocking, %d warnings\n", l_iSe, l_iSw);
    CHECK(&f_StoryCheck(l_oP) == &l_vRows, "the story check is cached until an edit");
    CHECK(!f_SafeCells(l_oP, "F02").empty(), "F02 has safe cells");
}

static void f_TestAutoPlace(Project& l_oP) {
    StageDoc& l_oD = l_oP.f_Doc("F02");
    const auto l_fnLabel = [](int l_iN) { return "Auto-place " + std::to_string(l_iN); };
    const AutoPlaceResult l_oA = f_AutoPlace(l_oP, {"F02"}, "all", "7", l_fnLabel);
    const std::vector<std::string> l_vFirst = l_oD.m_vGrid;
    if (l_oA.m_iFloors) l_oP.f_Undo(l_oD);
    CHECK(!l_oP.f_IsDirty("F02"), "auto-place undoes in one step");
    const AutoPlaceResult l_oB = f_AutoPlace(l_oP, {"F02"}, "all", "7", l_fnLabel);
    CHECK(l_oA.m_iPlaced == l_oB.m_iPlaced && l_oD.m_vGrid == l_vFirst, "the same seed places the same cells (%d placed)", l_oA.m_iPlaced);
    if (l_oB.m_iFloors) l_oP.f_Undo(l_oD);
}

static void f_TestFloorIndex(Project& l_oP) {
    std::string l_strDisk;
    CHECK(f_ReadText(l_oP.f_Abs("assets/data/events/floor_index.json"), l_strDisk), "floor_index.json exists");
    const std::string l_strBuilt = f_BuildFloorIndex(l_oP.m_strRoot).dump(1, ' ', false, Json::error_handler_t::replace) + "\n";
    CHECK(l_strBuilt == l_strDisk, "the floor index built here matches floor_index.json (%zu vs %zu bytes)", l_strBuilt.size(), l_strDisk.size());
    if (l_strBuilt != l_strDisk) {
        size_t l_nI = 0;
        while (l_nI < l_strBuilt.size() && l_nI < l_strDisk.size() && l_strBuilt[l_nI] == l_strDisk[l_nI]) l_nI++;
        std::printf("  first difference at byte %zu: built ...%s... disk ...%s...\n", l_nI,
                    l_strBuilt.substr(l_nI > 40 ? l_nI - 40 : 0, 80).c_str(), l_strDisk.substr(l_nI > 40 ? l_nI - 40 : 0, 80).c_str());
    }
}

// The event editor's model: loads unchanged, writes nothing until edited, and an edit writes only its file, in the
// format the file already has (the HTML event editor wrote it: JSON.stringify with 2 spaces).
static void f_TestEvents(const std::string& l_strRoot) {
    EventProject l_oE;
    std::string l_strErr;
    CHECK(l_oE.f_Load(l_strRoot, l_strErr), "the event project loads (%s)", l_strErr.c_str());
    CHECK(l_oE.m_jS["events"].size() > 100, "events loaded (%zu)", l_oE.m_jS["events"].size());
    CHECK(!l_oE.f_Dirty() && l_oE.f_LiveWrites({}).empty(), "nothing to write after loading");
    // every pool, vars and kinds file is in the format a save writes (so a save changes only the edited lines)
    int l_iSame = 0, l_iFiles = 0;
    for (const auto& l_oEntry : std::filesystem::directory_iterator(std::filesystem::u8path(l_strRoot + "/assets/data/events"))) {
        const std::string l_strN = l_oEntry.path().filename().u8string();
        if (l_strN.rfind("pool_", 0) != 0) continue;
        std::string l_strRaw;
        f_ReadText(l_oEntry.path().u8string(), l_strRaw);
        Json l_jJ = Json::parse(l_strRaw, nullptr, false);
        l_iFiles++;
        std::string l_strOut = l_jJ.dump(2) + "\n";
        if (l_strRaw.find("\r\n") != std::string::npos) { std::string l_strC; for (char l_cCh : l_strOut) { if (l_cCh == '\n') l_strC += '\r'; l_strC += l_cCh; } l_strOut = l_strC; }
        if (l_strOut == l_strRaw) l_iSame++; else std::printf("  note: %s would be rewritten differently\n", l_strN.c_str());
    }
    CHECK(l_iFiles > 0 && l_iSame == l_iFiles, "every pool file is in the save format (%d of %d)", l_iSame, l_iFiles);
    // a text edit: only text.json, only that key
    const std::string l_strKey = (*l_oE.f_Ev("ev_village_well"))["text"].get<std::string>();
    l_oE.f_Edit("text", [&] { l_oE.m_jS["texts"][l_strKey]["en"] = "core test"; });
    auto l_vW = l_oE.f_LiveWrites({});
    CHECK(l_vW.size() == 1 && l_vW[0].m_strPath == "assets/data/text.json", "a text edit writes only text.json");
    if (l_vW.size() == 1) {
        Json l_jA = Json::parse(l_vW[0].m_strBefore), l_jB = Json::parse(l_vW[0].m_strText);
        CHECK(l_jB["strings"][l_strKey]["en"] == "core test", "the new text is in it");
        l_jB["strings"][l_strKey] = l_jA["strings"][l_strKey];
        CHECK(l_jA == l_jB, "nothing else in text.json changes");
    }
    CHECK(l_oE.f_Undo() && !l_oE.f_Dirty(), "undo restores it");
    // a logic edit: only its pool, and the record keeps its other fields
    l_oE.f_Edit("logic", [&] { (*l_oE.f_Ev("ev_village_well"))["actions"].push_back(f_MakeAction("addCounter", "insight", 1)); });
    l_vW = l_oE.f_LiveWrites({});
    CHECK(l_vW.size() == 1 && l_vW[0].m_strPath.find("events/pool_act01.json") != std::string::npos, "a logic edit writes only its pool");
    l_oE.f_Undo();
    // a flag edit: flags.json keeps its hand-aligned lines; only the changed line differs
    if (!l_oE.m_jS["flags"].empty()) {
        const std::string l_strFl = l_oE.m_jS["flags"].begin().key();
        l_oE.f_Edit("flag", [&] { l_oE.m_jS["flags"][l_strFl]["category"] = "core test"; });
        l_vW = l_oE.f_LiveWrites({});
        CHECK(l_vW.size() == 1 && l_vW[0].m_strPath == "assets/data/story/flags.json", "a flag edit writes only flags.json");
        if (l_vW.size() == 1) {
            int l_iChanged = 0;
            for (const auto& [l_cC, l_strL] : f_LineDiff(l_vW[0].m_strBefore, l_vW[0].m_strText)) { (void)l_strL; if (l_cC == '+') l_iChanged++; }
            CHECK(l_iChanged == 1, "one line of flags.json changes (%d)", l_iChanged);
        }
        l_oE.f_Undo();
    }
    // conditions as rows and back
    const Json l_jLeaf = f_RowToLeaf("var:well_clues", ">=", 2);
    CHECK(f_LeafToRow(l_jLeaf).m_strSubj == "var:well_clues" && f_LeafToRow(l_jLeaf).m_strOp == ">=", "a condition row round-trips");
    const Json l_jNotLeaf = f_RowToLeaf("event:ev_village_well", "not", Json());
    CHECK(f_LeafToRow(l_jNotLeaf).m_strOp == "not", "an 'is not done' row round-trips");
    CHECK(f_CondToRows(Json{{"any", Json::array({l_jLeaf, l_jNotLeaf})}}).m_strMode == "any", "any-of rows");
    CHECK(f_CondToRows(Json{{"all", Json::array({Json{{"any", Json::array({l_jLeaf})}}})}}).m_bRaw, "nested logic stays JSON");
    // the validator and the talk preview
    const auto l_vProbs = l_oE.f_Problems();
    std::printf("  event problems: %zu\n", l_vProbs.size());
    TalkPreview l_oPv;
    l_oPv.f_Reset(l_oE, "villager");
    l_oPv.f_Talk(l_oE, "villager", "en");
    CHECK(!l_oPv.m_vLog.empty(), "the talk preview talks to the villager");
}

// --dump: per floor, the checks and the story issues by severity, and auto-place's cells for seed 7 on F02 -- the
// same lines a script prints from the HTML editor, to compare the two ports.
static int f_Dump(Project& l_oP) {
    for (const StoryRow& l_oR : f_StoryCheck(l_oP)) {
        int l_arrPc[3] = {0, 0, 0}, l_arrSc[3] = {0, 0, 0};
        for (const Issue& l_oI : f_Problems(l_oP, l_oP.f_Doc(l_oR.m_strFid))) l_arrPc[(int)l_oI.m_eSev]++;
        for (const Issue& l_oI : l_oR.m_vIssues) l_arrSc[(int)l_oI.m_eSev]++;
        std::printf("%s checks %d/%d/%d story %d/%d/%d safe %zu\n", l_oR.m_strFid.c_str(), l_arrPc[0], l_arrPc[1], l_arrPc[2], l_arrSc[0], l_arrSc[1], l_arrSc[2],
                    f_SafeCells(l_oP, l_oR.m_strFid).size());
    }
    f_AutoPlace(l_oP, {"F02"}, "all", "7", [](int l_iN) { return std::to_string(l_iN); });
    const StageDoc& l_oD = l_oP.f_Doc("F02");
    for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++)
            if (l_oP.f_SemAt(l_oD, l_iX, l_iY).rfind("event:", 0) == 0) std::printf("F02 %s %d,%d\n", l_oP.f_SemAt(l_oD, l_iX, l_iY).c_str(), l_iX, l_iY);
    return 0;
}

int main(int l_iArgc, char** l_pArgv) {
    if (l_iArgc < 2) { std::printf("usage: stage_core_test <TOMS folder> [--dump]\n"); return 2; }
    Project l_oP;
    std::string l_strErr;
    if (!l_oP.f_Load(l_pArgv[1], l_strErr)) { std::printf("cannot load %s: %s\n", l_pArgv[1], l_strErr.c_str()); return 1; }
    if (l_iArgc > 2 && std::string(l_pArgv[2]) == "--dump") return f_Dump(l_oP);
    if (l_iArgc > 2 && std::string(l_pArgv[2]) == "--dump-write") {   // three edits, then what Save would write (vs the HTML editor)
        EventProject l_oE;
        if (!l_oE.f_Load(l_pArgv[1], l_strErr)) return 1;
        (*l_oE.f_Ev("ev_village_well"))["actions"].push_back(f_MakeAction("addCounter", "insight", 1));
        l_oE.m_jS["texts"][(*l_oE.f_Ev("ev_village_well"))["text"].get<std::string>()]["en"] = "port check";
        l_oE.m_jS["flags"][l_oE.m_jS["flags"].begin().key()]["category"] = "port check";
        for (const auto& l_oF : l_oE.f_LiveWrites({})) std::fputs(("=== " + l_oF.m_strPath + "\n" + l_oF.m_strText).c_str(), stdout);
        return 0;
    }
    if (l_iArgc > 2 && std::string(l_pArgv[2]) == "--dump-events") {   // one line per event problem, sorted (vs the HTML editor)
        EventProject l_oE;
        if (!l_oE.f_Load(l_pArgv[1], l_strErr)) return 1;
        std::vector<std::string> l_vLines;
        for (const EvProblem& l_oX : l_oE.f_Problems()) l_vLines.push_back(l_oX.m_strRule + " " + l_oX.m_strId);
        std::sort(l_vLines.begin(), l_vLines.end());
        for (const std::string& l_strL : l_vLines) std::fputs((l_strL + "\n").c_str(), stdout);
        return 0;
    }
    f_TestRandom();
    f_TestLoad(l_oP);
    f_TestEdits(l_oP);
    f_TestChecks(l_oP);
    f_TestAutoPlace(l_oP);
    f_TestFloorIndex(l_oP);
    f_TestEvents(l_pArgv[1]);
    std::printf("stage_core_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed ? 1 : 0;
}
