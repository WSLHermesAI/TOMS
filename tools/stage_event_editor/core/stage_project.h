// stage_project.h -- the stage editor's data, without Qt (docs/gameplay/02_STAGE_EDITOR.md, section 6).
//
// What tools/web_editors/stage_editor.html reads from its bundle, read here straight from the TOMS folder:
//   assets/data/story/floors/F??.json (+ .stage.json, or stages/<handAuthoredStage>)   the floors
//   assets/data/events/pool_*.json, kinds.json                                       every event
//   assets/data/text.json                                                            the strings
//   assets/data/enemies.json, items.json, npcs/*.json                                the palette's types (edited)
//   assets/data/footprints.json, dialogue/*.json, media/anim/*.anim, media/atlas     sizes, choices, clips, sprites
// and the per-floor working copies (StageDoc) the editor changes, with undo and the rules for what may go on a cell.
// Files are written back with nlohmann's ordered_json, so keys keep their order and an unchanged file is unchanged.
#pragma once

#include <json.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace stage {

using Json = nlohmann::ordered_json;

// A stair's lock (W38, the owner's Q8 answer 2026-10-21): "unlock": { "eventDone": id } and / or { "eventsDone": [ids] }
// -- the stair opens when all are done. Read: eventDone first, then eventsDone, no repeats, no empty ids.
inline std::vector<std::string> stairUnlockEvents(const Json& stair) {
    std::vector<std::string> out;
    if (!stair.is_object() || !stair.contains("unlock") || !stair["unlock"].is_object()) return out;
    const Json& u = stair["unlock"];
    auto add = [&](const Json& v) {
        if (!v.is_string() || v.get<std::string>().empty()) return;
        for (const std::string& e : out) if (e == v.get<std::string>()) return;
        out.push_back(v.get<std::string>());
    };
    if (u.contains("eventDone")) add(u["eventDone"]);
    if (u.contains("eventsDone") && u["eventsDone"].is_array()) for (const Json& v : u["eventsDone"]) add(v);
    return out;
}
// Writes the list back: none -> no "unlock"; one -> { "eventDone": id } (as before W38); several -> { "eventsDone": [...] }.
inline void setStairUnlockEvents(Json& stair, const std::vector<std::string>& events) {
    std::vector<std::string> list;
    for (const std::string& e : events)
        if (!e.empty() && std::find(list.begin(), list.end(), e) == list.end()) list.push_back(e);
    if (list.empty()) { stair.erase("unlock"); return; }
    if (list.size() == 1) { stair["unlock"] = Json{{"eventDone", list[0]}}; return; }
    Json arr = Json::array();
    for (const std::string& e : list) arr.push_back(e);
    stair["unlock"] = Json{{"eventsDone", arr}};
}

// How a file ends its lines, so it is written back the way it was read.
struct Eol {
    bool crlf = false;
    bool finalNewline = true;
};
std::string fmtJson(const Json& j, const Eol& eol, int indent = 2);
bool readText(const std::string& path, std::string& out);
bool writeText(const std::string& path, const std::string& text);   // creates the folders
bool readJson(const std::string& path, Json& out, Eol* eol = nullptr, std::string* err = nullptr);

struct Event {
    std::string id, kind, pool, text, title;
    Json rec;              // the pool entry: trigger, requires, actions, next, dialogue, afterDialogue, ...
    bool pending = false;  // named by the event editor, not in a pool yet (place mode)
};

struct Floor {
    std::string id, specPath, stagePath;   // paths from the TOMS folder, "/"-separated
    Json spec, stage;
    Eol specEol, stageEol;
};

struct DataFile {          // a type record file the palette's detail windows edit
    Json obj;
    bool deleted = false;
    Eol eol;
};

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Cell& o) const { return !(*this == o); }
    bool operator<(const Cell& o) const { return y != o.y ? y < o.y : x < o.x; }
};

// One floor's working copy: the grid (rows of legend chars), the stage file and the floor spec. Undo is per floor.
struct StageDoc {
    const Floor* f = nullptr;
    Json st, spec;
    std::vector<std::string> grid;
    struct Step { std::string snap, label; };
    std::vector<Step> undo, redo;
    std::string origStage, origSpec;
    std::optional<std::string> pending;   // beginEdit's snapshot
    // "changed since it was read or saved": comparing the file's text is costly, so the answer is kept until the floor
    // changes again (ver goes up with every committed edit, undo, redo and save)
    int ver = 0;
    mutable int dirtyAt = -1;
    mutable bool dirtyVal = false;

    int width() const { return grid.empty() ? 0 : (int)grid[0].size(); }
    int height() const { return (int)grid.size(); }
    bool inside(int x, int y) const { return y >= 0 && x >= 0 && y < height() && x < (int)grid[(size_t)y].size(); }
    char at(int x, int y) const { return grid[(size_t)y][(size_t)x]; }
};

struct Footprint { int w = 1, h = 1; };

// Why something cannot be placed: the map's edge, a wall, or another object (what).
struct PlaceCheck {
    bool ok = true, same = false;
    std::vector<Cell> cells;
    enum Why { None, Edge, Wall, Obj } why = None;
    int x = 0, y = 0;
    std::string what;
};

class Project {
public:
    std::string root;                       // the TOMS folder, "/"-separated, no trailing "/"
    std::string lang = "en";                // "en" | "zh_TW": names (nameOf)
    std::vector<Floor> floors;
    std::vector<Event> events;
    Json strings = Json::object();          // text.json "strings"
    std::map<std::string, Json> textEdits;  // keys changed here (names, greetings): merged into text.json by Save
    Json footprints = Json::object(), kinds = Json::object();
    std::vector<std::string> sprites;       // the sprite names in assets/media/atlas/game.atlas
    std::map<std::string, Json> npcs;       // npcs/<id>.json
    std::map<std::string, std::vector<Json>> dialogueActions;   // dialogue id -> every action its choices run
    std::map<std::string, DataFile> dataFiles;                  // enemies.json, items.json, npcs/*.json (path -> file)
    std::map<std::string, std::string> dataOrig;                // as last read / written
    std::vector<std::pair<std::string, std::vector<std::string>>> anims;   // "anim/<file>.anim" -> its clip names
    int storyVer = 0;                       // bumped by every edit (the story check's cache)
    int dataVer = 0;
    // The open floor's tile theme (its stage file's "theme"): spriteOf gives wall_<theme> etc. where they exist.
    std::string theme;                        // bumped when a type record or a text changes (dataDirty's cache)

    bool load(const std::string& tomsRoot, std::string& err);
    // The events, NPC files, dialogue actions and strings as they are on disk now (the event editor saved them).
    bool reloadEvents(std::string& err);
    // Floors this editor has not changed: read again from disk (the event editor saved their events lists or stairs).
    int refreshCleanFloors();
    std::string abs(const std::string& rel) const { return root + "/" + rel; }

    const Floor* floor(const std::string& id) const;
    int floorIndex(const std::string& id) const;
    StageDoc& doc(const std::string& fid);
    bool hasDoc(const std::string& fid) const { return docs_.count(fid) > 0; }
    void dropDocs() { docs_.clear(); }
    const Event* event(const std::string& id) const;
    Event& addPendingEvent(const std::string& id, const std::string& kind);
    std::vector<std::string> pools() const;   // in first-seen order

    // Text: a localised name object ({ "zh_TW", "en" }) or a text.json key
    std::string localized(const Json& nameOrKey) const;
    Json textOf(const std::string& key) const;   // textEdits, then strings ({} when neither)
    std::string nameOf(const std::string& sem) const;

    // ---- types: a palette entry of a monster / item / NPC is a record in enemies.json / items.json / npcs/<id>.json
    static const char* enemiesPath() { return "assets/data/enemies.json"; }
    static const char* itemsPath() { return "assets/data/items.json"; }
    // The palette's own categories (the editor's; the game never reads it): {"groups": ["Bosses", ...]} in order.
    static const char* palettePath() { return "assets/data/editor_palette.json"; }
    static std::string npcPath(const std::string& id) { return "assets/data/npcs/" + id + ".json"; }
    static std::string dialoguePath(const std::string& id) { return "assets/data/dialogue/" + id + ".json"; }
    Json* typeRec(const std::string& sem);
    // ---- the palette's own categories: the list (palettePath, kept even when empty) and each type's "group" field ----
    std::vector<std::string> paletteGroups() const;
    void setPaletteGroups(const std::vector<std::string>& groups);
    std::string groupOf(const std::string& sem) const;               // "" = its usual group
    bool setGroup(const std::string& sem, const std::string& group);  // "" = back to its usual group; false: no record
    int renameGroup(const std::string& from, const std::string& to); // every type in it; returns how many
    int clearGroup(const std::string& group);                        // its types back to their usual groups
    const Json* typeRec(const std::string& sem) const { return const_cast<Project*>(this)->typeRec(sem); }
    Json typeLook(const std::string& sem) const;    // as Game::typeLook: the record's "sprite", then its "look"
    std::string spriteOf(const std::string& sem) const;   // the sprite a semantic draws with ("" = an event plate)
    std::vector<std::string> enemyIds() const;
    std::vector<std::string> itemIds() const;
    std::string itemType(const std::string& id) const;     // "consumable" when not set
    std::vector<std::string> npcIds() const;               // built-ins, NPC files, and NPCs placed on any floor
    std::vector<std::string> typeUses(const std::string& sem);   // floors that place it (it cannot be deleted then)

    // ---- the cell rules (the HTML editor's semAt / charFor / putSem / placeCheck)
    std::string semAt(const StageDoc& d, int x, int y) const;   // "" = outside or a char with no legend entry
    std::map<std::string, int> placedEvents(const StageDoc& d) const;   // event id -> cells on the grid (one legend look-up per char)
    static bool isEntity(const std::string& sem) { return !sem.empty() && sem != "wall" && sem != "floor"; }
    char charFor(StageDoc& d, const std::string& sem) const;
    // Paints one cell; an event the floor does not list is added to its spec (`listed` is set then).
    bool putSem(StageDoc& d, int x, int y, const std::string& sem, bool* listed = nullptr) const;
    Footprint footprintOf(const StageDoc& d, char ch, const std::string& sem) const;
    std::vector<Cell> fpCells(const StageDoc& d, const std::string& sem, int x, int y) const;
    PlaceCheck placeCheck(const StageDoc& d, int x, int y, const std::string& sem, const Cell* ignore = nullptr) const;
    static Json* objOf(StageDoc& d, int x, int y);
    static const Json* objOf(const StageDoc& d, int x, int y);
    static void dropObj(StageDoc& d, int x, int y);
    Json lookAt(const StageDoc& d, int x, int y, const std::string& sem) const;
    std::optional<Cell> find(const StageDoc& d, const std::string& sem) const;   // its first cell

    // ---- edits and undo (whole-floor snapshots, as the HTML editor)
    static std::string snapshot(const StageDoc& d);
    static void restore(StageDoc& d, const std::string& snap);
    void beginEdit(StageDoc& d) const { d.pending = snapshot(d); }
    bool commitEdit(StageDoc& d, const std::string& label);   // false: nothing changed
    // Stable object ids (docs/gameplay/05_SAVE_SYSTEM.md): every object whose state the game saves (monster, item, key, event,
    // gate, door) gets objects."x,y".uid -- "o1", "o2" ... unique on its floor -- which moves with it, so a player's
    // save still knows it after a designer moves it. Run on every committed edit; a duplicate (a copied entry) gets
    // a new one.
    static bool savesState(const std::string& sem);
    void ensureUids(StageDoc& d) const;
    bool undo(StageDoc& d, std::string* label = nullptr);
    bool redo(StageDoc& d, std::string* label = nullptr);

    // ---- what Save writes
    Json stageOut(const StageDoc& d) const;
    std::string stageText(const StageDoc& d) const { return fmtJson(stageOut(d), d.f->stageEol); }
    std::string specText(const StageDoc& d) const { return fmtJson(d.spec, d.f->specEol); }
    bool isDirty(const std::string& fid) const;
    bool anyDirty() const;
    bool dataDirty() const;                 // a type record or a name changed (enemies / items / npcs, text.json)

private:
    std::map<std::string, std::unique_ptr<StageDoc>> docs_;
    std::map<std::string, size_t> evIndex_;
    mutable int dataDirtyAt_ = -1;
    mutable bool dataDirtyVal_ = false;
    void indexEvents();
};

// The chars the game reads directly (game_input.cpp), not through the legend.
const std::map<std::string, char>& reservedChars();
const std::map<std::string, char>& preferredChars();

}  // namespace stage
