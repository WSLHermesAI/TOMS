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
inline std::vector<std::string> f_StairUnlockEvents(const Json& l_jStair) {
    std::vector<std::string> l_vOut;
    if (!l_jStair.is_object() || !l_jStair.contains("unlock") || !l_jStair["unlock"].is_object()) return l_vOut;
    const Json& l_jU = l_jStair["unlock"];
    auto l_fnAdd = [&](const Json& l_jV) {
        if (!l_jV.is_string() || l_jV.get<std::string>().empty()) return;
        for (const std::string& l_strE : l_vOut) if (l_strE == l_jV.get<std::string>()) return;
        l_vOut.push_back(l_jV.get<std::string>());
    };
    if (l_jU.contains("eventDone")) l_fnAdd(l_jU["eventDone"]);
    if (l_jU.contains("eventsDone") && l_jU["eventsDone"].is_array()) for (const Json& l_jV : l_jU["eventsDone"]) l_fnAdd(l_jV);
    return l_vOut;
}
// Writes the list back: none -> no "unlock"; one -> { "eventDone": id } (as before W38); several -> { "eventsDone": [...] }.
inline void f_SetStairUnlockEvents(Json& l_jStair, const std::vector<std::string>& l_vEvents) {
    std::vector<std::string> l_vList;
    for (const std::string& l_strE : l_vEvents)
        if (!l_strE.empty() && std::find(l_vList.begin(), l_vList.end(), l_strE) == l_vList.end()) l_vList.push_back(l_strE);
    if (l_vList.empty()) { l_jStair.erase("unlock"); return; }
    if (l_vList.size() == 1) { l_jStair["unlock"] = Json{{"eventDone", l_vList[0]}}; return; }
    Json l_jArr = Json::array();
    for (const std::string& l_strE : l_vList) l_jArr.push_back(l_strE);
    l_jStair["unlock"] = Json{{"eventsDone", l_jArr}};
}

// How a file ends its lines, so it is written back the way it was read.
struct Eol {
    bool m_bCrlf = false;
    bool m_bFinalNewline = true;
};
std::string f_FmtJson(const Json& l_jJ, const Eol& l_oEol, int l_iIndent = 2);
bool f_ReadText(const std::string& l_strPath, std::string& l_strOut);
bool f_WriteText(const std::string& l_strPath, const std::string& l_strText);   // creates the folders
bool f_ReadJson(const std::string& l_strPath, Json& l_jOut, Eol* l_pEol = nullptr, std::string* l_pErr = nullptr);

struct Event {
    std::string m_strId, m_strKind, m_strPool, m_strText, m_strTitle;
    Json m_jRec;              // the pool entry: trigger, requires, actions, next, dialogue, afterDialogue, ...
    bool m_bPending = false;  // named by the event editor, not in a pool yet (place mode)
};

struct Floor {
    std::string m_strId, m_strSpecPath, m_strStagePath;   // paths from the TOMS folder, "/"-separated
    Json m_jSpec, m_jStage;
    Eol m_oSpecEol, m_oStageEol;
};

struct DataFile {          // a type record file the palette's detail windows edit
    Json m_jObj;
    bool m_bDeleted = false;
    Eol m_oEol;
};

struct Cell {
    int m_iX = 0, m_iY = 0;
    bool operator==(const Cell& l_oO) const { return m_iX == l_oO.m_iX && m_iY == l_oO.m_iY; }
    bool operator!=(const Cell& l_oO) const { return !(*this == l_oO); }
    bool operator<(const Cell& l_oO) const { return m_iY != l_oO.m_iY ? m_iY < l_oO.m_iY : m_iX < l_oO.m_iX; }
};

// One floor's working copy: the grid (rows of legend chars), the stage file and the floor spec. Undo is per floor.
struct StageDoc {
    const Floor* m_pF = nullptr;
    Json m_jSt, m_jSpec;
    std::vector<std::string> m_vGrid;
    struct Step { std::string m_strSnap, m_strLabel; };
    std::vector<Step> m_vUndo, m_vRedo;
    std::string m_strOrigStage, m_strOrigSpec;
    std::optional<std::string> m_optPending;   // beginEdit's snapshot
    // "changed since it was read or saved": comparing the file's text is costly, so the answer is kept until the floor
    // changes again (ver goes up with every committed edit, undo, redo and save)
    int m_iVer = 0;
    mutable int m_iDirtyAt = -1;
    mutable bool m_bDirtyVal = false;

    int f_Width() const { return m_vGrid.empty() ? 0 : (int)m_vGrid[0].size(); }
    int f_Height() const { return (int)m_vGrid.size(); }
    bool f_Inside(int l_iX, int l_iY) const { return l_iY >= 0 && l_iX >= 0 && l_iY < f_Height() && l_iX < (int)m_vGrid[(size_t)l_iY].size(); }
    char f_At(int l_iX, int l_iY) const { return m_vGrid[(size_t)l_iY][(size_t)l_iX]; }
};

struct Footprint { int m_iW = 1, m_iH = 1; };

// Why something cannot be placed: the map's edge, a wall, or another object (what).
struct PlaceCheck {
    bool m_bOk = true, m_bSame = false;
    std::vector<Cell> m_vCells;
    enum Why { None, Edge, Wall, Obj } m_eWhy = None;
    int m_iX = 0, m_iY = 0;
    std::string m_strWhat;
};

class Project {
public:
    std::string m_strRoot;                       // the TOMS folder, "/"-separated, no trailing "/"
    std::string m_strLang = "en";                // "en" | "zh_TW": names (nameOf)
    std::vector<Floor> m_vFloors;
    std::vector<Event> m_vEvents;
    Json m_jStrings = Json::object();          // text.json "strings"
    std::map<std::string, Json> m_mapTextEdits;  // keys changed here (names, greetings): merged into text.json by Save
    Json m_jFootprints = Json::object(), m_jKinds = Json::object();
    std::vector<std::string> m_vSprites;       // the sprite names in assets/media/atlas/game.atlas
    std::map<std::string, Json> m_mapNpcs;       // npcs/<id>.json
    std::map<std::string, std::vector<Json>> m_mapDialogueActions;   // dialogue id -> every action its choices run
    std::map<std::string, DataFile> m_mapDataFiles;                  // enemies.json, items.json, npcs/*.json (path -> file)
    std::map<std::string, std::string> m_mapDataOrig;                // as last read / written
    std::vector<std::pair<std::string, std::vector<std::string>>> m_vAnims;   // "anim/<file>.anim" -> its clip names
    int m_iStoryVer = 0;                       // bumped by every edit (the story check's cache)
    int m_iDataVer = 0;
    // The open floor's tile theme (its stage file's "theme"): spriteOf gives wall_<theme> etc. where they exist.
    std::string m_strTheme;                        // bumped when a type record or a text changes (dataDirty's cache)

    bool f_Load(const std::string& l_strTomsRoot, std::string& l_strErr);
    // The events, NPC files, dialogue actions and strings as they are on disk now (the event editor saved them).
    bool f_ReloadEvents(std::string& l_strErr);
    // Floors this editor has not changed: read again from disk (the event editor saved their events lists or stairs).
    int f_RefreshCleanFloors();
    std::string f_Abs(const std::string& l_strRel) const { return m_strRoot + "/" + l_strRel; }

    const Floor* f_Floor(const std::string& l_strId) const;
    int f_FloorIndex(const std::string& l_strId) const;
    StageDoc& f_Doc(const std::string& l_strFid);
    bool f_HasDoc(const std::string& l_strFid) const { return m_mapDocs.count(l_strFid) > 0; }
    void f_DropDocs() { m_mapDocs.clear(); }
    const Event* f_Event(const std::string& l_strId) const;
    Event& f_AddPendingEvent(const std::string& l_strId, const std::string& l_strKind);
    std::vector<std::string> f_Pools() const;   // in first-seen order

    // Text: a localised name object ({ "zh_TW", "en" }) or a text.json key
    std::string f_Localized(const Json& l_jNameOrKey) const;
    Json f_TextOf(const std::string& l_strKey) const;   // textEdits, then strings ({} when neither)
    std::string f_NameOf(const std::string& l_strSem) const;

    // ---- types: a palette entry of a monster / item / NPC is a record in enemies.json / items.json / npcs/<id>.json
    static const char* f_EnemiesPath() { return "assets/data/enemies.json"; }
    static const char* f_ItemsPath() { return "assets/data/items.json"; }
    // The palette's own categories (the editor's; the game never reads it): {"groups": ["Bosses", ...]} in order.
    static const char* f_PalettePath() { return "assets/data/editor_palette.json"; }
    static std::string f_NpcPath(const std::string& l_strId) { return "assets/data/npcs/" + l_strId + ".json"; }
    static std::string f_DialoguePath(const std::string& l_strId) { return "assets/data/dialogue/" + l_strId + ".json"; }
    Json* f_TypeRec(const std::string& l_strSem);
    // ---- the palette's own categories: the list (palettePath, kept even when empty) and each type's "group" field ----
    std::vector<std::string> f_PaletteGroups() const;
    void f_SetPaletteGroups(const std::vector<std::string>& l_vGroups);
    std::string f_GroupOf(const std::string& l_strSem) const;               // "" = its usual group
    bool f_SetGroup(const std::string& l_strSem, const std::string& l_strGroup);  // "" = back to its usual group; false: no record
    int f_RenameGroup(const std::string& l_strFrom, const std::string& l_strTo); // every type in it; returns how many
    int f_ClearGroup(const std::string& l_strGroup);                        // its types back to their usual groups
    const Json* f_TypeRec(const std::string& l_strSem) const { return const_cast<Project*>(this)->f_TypeRec(l_strSem); }
    Json f_TypeLook(const std::string& l_strSem) const;    // as Game::typeLook: the record's "sprite", then its "look"
    std::string f_SpriteOf(const std::string& l_strSem) const;   // the sprite a semantic draws with ("" = an event plate)
    std::vector<std::string> f_EnemyIds() const;
    std::vector<std::string> f_ItemIds() const;
    std::string f_ItemType(const std::string& l_strId) const;     // "consumable" when not set
    std::vector<std::string> f_NpcIds() const;               // built-ins, NPC files, and NPCs placed on any floor
    std::vector<std::string> f_TypeUses(const std::string& l_strSem);   // floors that place it (it cannot be deleted then)

    // ---- the cell rules (the HTML editor's semAt / charFor / putSem / placeCheck)
    std::string f_SemAt(const StageDoc& l_oD, int l_iX, int l_iY) const;   // "" = outside or a char with no legend entry
    std::map<std::string, int> f_PlacedEvents(const StageDoc& l_oD) const;   // event id -> cells on the grid (one legend look-up per char)
    static bool f_IsEntity(const std::string& l_strSem) { return !l_strSem.empty() && l_strSem != "wall" && l_strSem != "floor"; }
    char f_CharFor(StageDoc& l_oD, const std::string& l_strSem) const;
    // Paints one cell; an event the floor does not list is added to its spec (`listed` is set then).
    bool f_PutSem(StageDoc& l_oD, int l_iX, int l_iY, const std::string& l_strSem, bool* l_pListed = nullptr) const;
    Footprint f_FootprintOf(const StageDoc& l_oD, char l_cCh, const std::string& l_strSem) const;
    std::vector<Cell> f_FpCells(const StageDoc& l_oD, const std::string& l_strSem, int l_iX, int l_iY) const;
    PlaceCheck f_PlaceCheck(const StageDoc& l_oD, int l_iX, int l_iY, const std::string& l_strSem, const Cell* l_pIgnore = nullptr) const;
    static Json* f_ObjOf(StageDoc& l_oD, int l_iX, int l_iY);
    static const Json* f_ObjOf(const StageDoc& l_oD, int l_iX, int l_iY);
    static void f_DropObj(StageDoc& l_oD, int l_iX, int l_iY);
    Json f_LookAt(const StageDoc& l_oD, int l_iX, int l_iY, const std::string& l_strSem) const;
    std::optional<Cell> f_Find(const StageDoc& l_oD, const std::string& l_strSem) const;   // its first cell

    // ---- edits and undo (whole-floor snapshots, as the HTML editor)
    static std::string f_Snapshot(const StageDoc& l_oD);
    static void f_Restore(StageDoc& l_oD, const std::string& l_strSnap);
    void f_BeginEdit(StageDoc& l_oD) const { l_oD.m_optPending = f_Snapshot(l_oD); }
    bool f_CommitEdit(StageDoc& l_oD, const std::string& l_strLabel);   // false: nothing changed
    // Stable object ids (docs/gameplay/05_SAVE_SYSTEM.md): every object whose state the game saves (monster, item, key, event,
    // gate, door) gets objects."x,y".uid -- "o1", "o2" ... unique on its floor -- which moves with it, so a player's
    // save still knows it after a designer moves it. Run on every committed edit; a duplicate (a copied entry) gets
    // a new one.
    static bool f_SavesState(const std::string& l_strSem);
    void f_EnsureUids(StageDoc& l_oD) const;
    bool f_Undo(StageDoc& l_oD, std::string* l_pLabel = nullptr);
    bool f_Redo(StageDoc& l_oD, std::string* l_pLabel = nullptr);

    // ---- what Save writes
    Json f_StageOut(const StageDoc& l_oD) const;
    std::string f_StageText(const StageDoc& l_oD) const { return f_FmtJson(f_StageOut(l_oD), l_oD.m_pF->m_oStageEol); }
    std::string f_SpecText(const StageDoc& l_oD) const { return f_FmtJson(l_oD.m_jSpec, l_oD.m_pF->m_oSpecEol); }
    bool f_IsDirty(const std::string& l_strFid) const;
    bool f_AnyDirty() const;
    bool f_DataDirty() const;                 // a type record or a name changed (enemies / items / npcs, text.json)

private:
    std::map<std::string, std::unique_ptr<StageDoc>> m_mapDocs;
    std::map<std::string, size_t> m_mapEvIndex;
    mutable int m_iDataDirtyAt = -1;
    mutable bool m_bDataDirtyVal = false;
    void f_IndexEvents();
};

// The chars the game reads directly (game_input.cpp), not through the legend.
const std::map<std::string, char>& f_ReservedChars();
const std::map<std::string, char>& f_PreferredChars();

}  // namespace stage
