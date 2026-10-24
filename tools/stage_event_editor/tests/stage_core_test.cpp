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

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-12; }

static void testRandom() {
    // node: hashStr("1:F02") = 3257597346, then mulberry gives these three
    CHECK(hashStr("1:F02") == 3257597346u, "hashStr(1:F02) = %u", hashStr("1:F02"));
    auto r = mulberry(hashStr("1:F02"));
    const double a = r(), b = r(), c = r();
    CHECK(near(a, 0.19637801591306925) && near(b, 0.4360330330673605) && near(c, 0.9664091821759939),
          "mulberry(1:F02) = %.17g %.17g %.17g", a, b, c);
    CHECK(hashStr("42:F10") == 834627190u, "hashStr(42:F10)");
    auto r2 = mulberry(hashStr("42:F10"));
    CHECK(near(r2(), 0.28418486448936164), "mulberry(42:F10)");
}

static void testLoad(Project& p) {
    CHECK(p.floors.size() == 70, "70 floors (%zu)", p.floors.size());
    CHECK(!p.events.empty() && p.event("ev_village_well"), "events loaded, ev_village_well defined");
    CHECK(!p.sprites.empty(), "sprite names loaded");
    CHECK(p.typeRec("monster:slime") && p.typeRec("item:potion_red"), "type records: slime, potion_red");
    int same = 0;
    for (const Floor& f : p.floors) {
        StageDoc& d = p.doc(f.id);
        if (!p.isDirty(f.id)) same++;
        else std::printf("  note: %s reads back changed (stage %s, spec %s)\n", f.id.c_str(),
                         p.stageText(d) == d.origStage ? "same" : "differs", p.specText(d) == d.origSpec ? "same" : "differs");
        // the file as written by either editor: what this editor would write for it unchanged
        std::string raw;
        if (readText(p.abs(f.stagePath), raw)) CHECK(raw == d.origStage, "%s: written back byte for byte", f.stagePath.c_str());
        if (readText(p.abs(f.specPath), raw)) CHECK(raw == d.origSpec, "%s: written back byte for byte", f.specPath.c_str());
    }
    CHECK(same == (int)p.floors.size(), "untouched floors are not changed (%d of %zu)", same, p.floors.size());
    CHECK(changedFiles(p).empty(), "nothing to save after loading (%zu)", changedFiles(p).size());
}

static void testEdits(Project& p) {
    StageDoc& d = p.doc("F02");
    // a free floor cell
    int fx = -1, fy = -1;
    for (int y = 1; y < d.height() - 1 && fx < 0; y++)
        for (int x = 1; x < d.width() - 1; x++) if (d.at(x, y) == '.') { fx = x; fy = y; break; }
    CHECK(fx >= 0, "F02 has a free cell");
    p.beginEdit(d);
    CHECK(p.putSem(d, fx, fy, "wall"), "paint a wall");
    CHECK(p.commitEdit(d, "Paint wall"), "the edit is an undo step");
    CHECK(p.isDirty("F02"), "F02 changed");
    CHECK(p.undo(d), "undo");
    CHECK(!p.isDirty("F02"), "after undo F02 is as on disk");
    CHECK(p.redo(d) && p.isDirty("F02"), "redo");
    p.undo(d);

    // an object needs free floor; a monster's footprint; the same object again does nothing
    const PlaceCheck onWall = p.placeCheck(d, 0, 0, "monster:slime");
    CHECK(!onWall.ok && onWall.why == PlaceCheck::Wall, "nothing goes on a wall");
    const PlaceCheck onFree = p.placeCheck(d, fx, fy, "item:coin");
    CHECK(onFree.ok, "an item on free floor");

    // an event the floor does not list is added to its spec
    bool listed = false;
    p.beginEdit(d);
    p.putSem(d, fx, fy, "event:ev_village_well_test", &listed);
    CHECK(listed, "painting an unlisted event lists it");
    p.commitEdit(d, "event");
    p.undo(d);
    CHECK(!p.isDirty("F02"), "undo removes the listed event too");

    // the reserved chars: a door always gets 'y'; whatever used 'y' moves to a free char
    StageDoc& d3 = p.doc("F03");
    p.beginEdit(d3);
    d3.st["legend"]["y"] = "item:coin";
    const char yc = p.charFor(d3, "door:yellow");
    CHECK(yc == 'y' && d3.st["legend"]["y"] == "door:yellow", "door:yellow takes 'y'");
    bool moved = false;
    for (auto& [c, v] : d3.st["legend"].items()) if (v == "item:coin" && c != "y") moved = true;
    CHECK(moved, "the item that used 'y' moved to another char");
    p.commitEdit(d3, "reserved");
    p.undo(d3);
    CHECK(!p.isDirty("F03"), "undo restores the legend");

    // object settings go with the object: erased with it, kept with a move
    const auto villager = p.find(d, "npc:villager");
    CHECK(villager.has_value(), "F02 has the villager");
    if (villager) {
        p.beginEdit(d);
        d.st["objects"][std::to_string(villager->x) + "," + std::to_string(villager->y)] = Json{{"events", Json::array({"ev_village_well"})}};
        CHECK(Project::objOf(d, villager->x, villager->y) != nullptr, "object settings set");
        p.putSem(d, villager->x, villager->y, "floor");
        CHECK(Project::objOf(d, villager->x, villager->y) == nullptr, "painting over drops them");
        p.commitEdit(d, "objects");
        p.undo(d);
        CHECK(!p.isDirty("F02"), "undo restores the cell");
    }

    // stable object ids (docs/gameplay/05_SAVE_SYSTEM.md): every saved-state object has a uid, which moves with it; a new object
    // gets a fresh one and a copied (duplicate) uid is replaced
    if (const auto mon = p.find(d, "monster:slime")) {
        const Json* o = Project::objOf(d, mon->x, mon->y);
        CHECK(o && o->contains("uid"), "a shipped monster has a uid");
        const std::string uid = o ? o->value("uid", std::string()) : std::string();
        int fx = -1, fy = -1;
        for (int y = 0; y < d.height() && fx < 0; y++)
            for (int x = 0; x < d.width(); x++)
                if (p.semAt(d, x, y) == "floor") { fx = x; fy = y; break; }
        p.beginEdit(d);
        const char ch = d.grid[(size_t)mon->y][(size_t)mon->x];
        d.grid[(size_t)mon->y][(size_t)mon->x] = '.';
        d.grid[(size_t)fy][(size_t)fx] = ch;
        const Json keep = *Project::objOf(d, mon->x, mon->y);
        Project::dropObj(d, mon->x, mon->y);
        d.st["objects"][std::to_string(fx) + "," + std::to_string(fy)] = keep;
        p.commitEdit(d, "move");
        const Json* moved = Project::objOf(d, fx, fy);
        CHECK(moved && moved->value("uid", std::string()) == uid, "a moved object keeps its uid");
        p.undo(d);
        p.beginEdit(d);
        p.putSem(d, fx, fy, "monster:bat");
        p.commitEdit(d, "new");
        const Json* fresh = Project::objOf(d, fx, fy);
        CHECK(fresh && !fresh->value("uid", std::string()).empty() && fresh->value("uid", std::string()) != uid,
              "a new object gets its own uid");
        p.beginEdit(d);
        d.st["objects"][std::to_string(fx) + "," + std::to_string(fy)]["uid"] = uid;   // as if its entry were copied
        p.commitEdit(d, "dup");
        const Json* dup = Project::objOf(d, fx, fy);
        const Json* orig = Project::objOf(d, mon->x, mon->y);
        CHECK(dup && orig && dup->value("uid", std::string()) != orig->value("uid", std::string()), "a duplicate uid is replaced");
        p.undo(d);
        p.undo(d);
        CHECK(!p.isDirty("F02"), "undo restores the uids");
    }
}

static void testChecks(Project& p) {
    int errors = 0, warnings = 0;
    for (const Floor& f : p.floors)
        for (const Issue& i : problems(p, p.doc(f.id))) (i.sev == Sev::Error ? errors : warnings)++;
    std::printf("  checks: %d errors, %d warnings/info on %zu floors\n", errors, warnings, p.floors.size());
    const std::vector<StoryRow>& rows = storyCheck(p);
    CHECK(rows.size() == p.floors.size(), "one story row per floor");
    CHECK(!rows.empty() && rows[0].fid == "F01", "the story starts on F01");
    int se = 0, sw = 0;
    for (const StoryRow& r : rows) for (const Issue& i : r.issues) (i.sev == Sev::Error ? se : sw)++;
    std::printf("  story: %d blocking, %d warnings\n", se, sw);
    CHECK(&storyCheck(p) == &rows, "the story check is cached until an edit");
    CHECK(!safeCells(p, "F02").empty(), "F02 has safe cells");
}

static void testAutoPlace(Project& p) {
    StageDoc& d = p.doc("F02");
    const auto label = [](int n) { return "Auto-place " + std::to_string(n); };
    const AutoPlaceResult a = autoPlace(p, {"F02"}, "all", "7", label);
    const std::vector<std::string> first = d.grid;
    if (a.floors) p.undo(d);
    CHECK(!p.isDirty("F02"), "auto-place undoes in one step");
    const AutoPlaceResult b = autoPlace(p, {"F02"}, "all", "7", label);
    CHECK(a.placed == b.placed && d.grid == first, "the same seed places the same cells (%d placed)", a.placed);
    if (b.floors) p.undo(d);
}

static void testFloorIndex(Project& p) {
    std::string disk;
    CHECK(readText(p.abs("assets/data/events/floor_index.json"), disk), "floor_index.json exists");
    const std::string built = buildFloorIndex(p.root).dump(1, ' ', false, Json::error_handler_t::replace) + "\n";
    CHECK(built == disk, "the floor index built here matches floor_index.json (%zu vs %zu bytes)", built.size(), disk.size());
    if (built != disk) {
        size_t i = 0;
        while (i < built.size() && i < disk.size() && built[i] == disk[i]) i++;
        std::printf("  first difference at byte %zu: built ...%s... disk ...%s...\n", i,
                    built.substr(i > 40 ? i - 40 : 0, 80).c_str(), disk.substr(i > 40 ? i - 40 : 0, 80).c_str());
    }
}

// The event editor's model: loads unchanged, writes nothing until edited, and an edit writes only its file, in the
// format the file already has (the HTML event editor wrote it: JSON.stringify with 2 spaces).
static void testEvents(const std::string& root) {
    EventProject e;
    std::string err;
    CHECK(e.load(root, err), "the event project loads (%s)", err.c_str());
    CHECK(e.S["events"].size() > 100, "events loaded (%zu)", e.S["events"].size());
    CHECK(!e.dirty() && e.liveWrites({}).empty(), "nothing to write after loading");
    // every pool, vars and kinds file is in the format a save writes (so a save changes only the edited lines)
    int same = 0, files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::u8path(root + "/assets/data/events"))) {
        const std::string n = entry.path().filename().u8string();
        if (n.rfind("pool_", 0) != 0) continue;
        std::string raw;
        readText(entry.path().u8string(), raw);
        Json j = Json::parse(raw, nullptr, false);
        files++;
        std::string out = j.dump(2) + "\n";
        if (raw.find("\r\n") != std::string::npos) { std::string c; for (char ch : out) { if (ch == '\n') c += '\r'; c += ch; } out = c; }
        if (out == raw) same++; else std::printf("  note: %s would be rewritten differently\n", n.c_str());
    }
    CHECK(files > 0 && same == files, "every pool file is in the save format (%d of %d)", same, files);
    // a text edit: only text.json, only that key
    const std::string key = (*e.ev("ev_village_well"))["text"].get<std::string>();
    e.edit("text", [&] { e.S["texts"][key]["en"] = "core test"; });
    auto w = e.liveWrites({});
    CHECK(w.size() == 1 && w[0].path == "assets/data/text.json", "a text edit writes only text.json");
    if (w.size() == 1) {
        Json a = Json::parse(w[0].before), b = Json::parse(w[0].text);
        CHECK(b["strings"][key]["en"] == "core test", "the new text is in it");
        b["strings"][key] = a["strings"][key];
        CHECK(a == b, "nothing else in text.json changes");
    }
    CHECK(e.undo() && !e.dirty(), "undo restores it");
    // a logic edit: only its pool, and the record keeps its other fields
    e.edit("logic", [&] { (*e.ev("ev_village_well"))["actions"].push_back(makeAction("addCounter", "insight", 1)); });
    w = e.liveWrites({});
    CHECK(w.size() == 1 && w[0].path.find("events/pool_act01.json") != std::string::npos, "a logic edit writes only its pool");
    e.undo();
    // a flag edit: flags.json keeps its hand-aligned lines; only the changed line differs
    if (!e.S["flags"].empty()) {
        const std::string fl = e.S["flags"].begin().key();
        e.edit("flag", [&] { e.S["flags"][fl]["category"] = "core test"; });
        w = e.liveWrites({});
        CHECK(w.size() == 1 && w[0].path == "assets/data/story/flags.json", "a flag edit writes only flags.json");
        if (w.size() == 1) {
            int changed = 0;
            for (const auto& [c, l] : lineDiff(w[0].before, w[0].text)) { (void)l; if (c == '+') changed++; }
            CHECK(changed == 1, "one line of flags.json changes (%d)", changed);
        }
        e.undo();
    }
    // conditions as rows and back
    const Json leaf = rowToLeaf("var:well_clues", ">=", 2);
    CHECK(leafToRow(leaf).subj == "var:well_clues" && leafToRow(leaf).op == ">=", "a condition row round-trips");
    const Json notLeaf = rowToLeaf("event:ev_village_well", "not", Json());
    CHECK(leafToRow(notLeaf).op == "not", "an 'is not done' row round-trips");
    CHECK(condToRows(Json{{"any", Json::array({leaf, notLeaf})}}).mode == "any", "any-of rows");
    CHECK(condToRows(Json{{"all", Json::array({Json{{"any", Json::array({leaf})}}})}}).raw, "nested logic stays JSON");
    // the validator and the talk preview
    const auto probs = e.problems();
    std::printf("  event problems: %zu\n", probs.size());
    TalkPreview pv;
    pv.reset(e, "villager");
    pv.talk(e, "villager", "en");
    CHECK(!pv.log.empty(), "the talk preview talks to the villager");
}

// --dump: per floor, the checks and the story issues by severity, and auto-place's cells for seed 7 on F02 -- the
// same lines a script prints from the HTML editor, to compare the two ports.
static int dump(Project& p) {
    for (const StoryRow& r : storyCheck(p)) {
        int pc[3] = {0, 0, 0}, sc[3] = {0, 0, 0};
        for (const Issue& i : problems(p, p.doc(r.fid))) pc[(int)i.sev]++;
        for (const Issue& i : r.issues) sc[(int)i.sev]++;
        std::printf("%s checks %d/%d/%d story %d/%d/%d safe %zu\n", r.fid.c_str(), pc[0], pc[1], pc[2], sc[0], sc[1], sc[2],
                    safeCells(p, r.fid).size());
    }
    autoPlace(p, {"F02"}, "all", "7", [](int n) { return std::to_string(n); });
    const StageDoc& d = p.doc("F02");
    for (int y = 0; y < d.height(); y++)
        for (int x = 0; x < d.width(); x++)
            if (p.semAt(d, x, y).rfind("event:", 0) == 0) std::printf("F02 %s %d,%d\n", p.semAt(d, x, y).c_str(), x, y);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: stage_core_test <TOMS folder> [--dump]\n"); return 2; }
    Project p;
    std::string err;
    if (!p.load(argv[1], err)) { std::printf("cannot load %s: %s\n", argv[1], err.c_str()); return 1; }
    if (argc > 2 && std::string(argv[2]) == "--dump") return dump(p);
    if (argc > 2 && std::string(argv[2]) == "--dump-write") {   // three edits, then what Save would write (vs the HTML editor)
        EventProject e;
        if (!e.load(argv[1], err)) return 1;
        (*e.ev("ev_village_well"))["actions"].push_back(makeAction("addCounter", "insight", 1));
        e.S["texts"][(*e.ev("ev_village_well"))["text"].get<std::string>()]["en"] = "port check";
        e.S["flags"][e.S["flags"].begin().key()]["category"] = "port check";
        for (const auto& f : e.liveWrites({})) std::fputs(("=== " + f.path + "\n" + f.text).c_str(), stdout);
        return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--dump-events") {   // one line per event problem, sorted (vs the HTML editor)
        EventProject e;
        if (!e.load(argv[1], err)) return 1;
        std::vector<std::string> lines;
        for (const EvProblem& x : e.problems()) lines.push_back(x.rule + " " + x.id);
        std::sort(lines.begin(), lines.end());
        for (const std::string& l : lines) std::fputs((l + "\n").c_str(), stdout);
        return 0;
    }
    testRandom();
    testLoad(p);
    testEdits(p);
    testChecks(p);
    testAutoPlace(p);
    testFloorIndex(p);
    testEvents(argv[1]);
    std::printf("stage_core_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
