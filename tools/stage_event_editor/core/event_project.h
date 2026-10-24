// event_project.h -- the event editor's data, without Qt (docs/gameplay/event_editor/README.md).
//
// The same model as tools/web_editors/event_editor.html, read straight from the TOMS folder (its readLiveData):
// every pool's events, the floors' event lists and stairs, the texts they use, event variables / counters / flags,
// the kinds, the NPC files and the dialogue files. The working state is one JSON value (S) as in the HTML editor, so an
// edit is "snapshot, change, compare": undo restores the snapshot, and dirty() compares S with what is on disk (base).
// Save writes only the files that changed, each built from the file as it is on disk now (the stage editor may have
// written a floor's events list meanwhile), in the HTML editor's exact formats.
#pragma once

#include "stage_project.h"   // Json, Eol, readText, writeText

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace stage {

extern const char* const kLangs[6];   // zh_TW, en, zh_CN, ja, ko, es: the event text languages

// One problem of the validator (the HTML editor's RULES): where is a message code + arguments ("" code = args[0] as is)
struct EvProblem {
    std::string rule, id, act, to;
    std::string whereCode;
    std::vector<std::string> whereArgs;
};
const std::vector<std::pair<std::string, int>>& evRules();   // rule -> severity (0 error, 1 warn, 2 info), in display order
int ruleSev(const std::string& rule);

// A condition row (condition.cpp leaf): subject "var:x" / "counter:x" / "flag:x" / "event:x" / "talks:npc" / "stat:s" /
// "floor:F05" / "item:id" / "stage:id" / "choice:eventId", an operator, a value.
struct CondRow {
    std::string subj, op;
    Json val;
};
struct CondRows {
    std::string mode = "all";   // "all" | "any"
    std::vector<Json> rows;     // leaves
    bool raw = false;           // nested logic: shown as JSON
};
CondRow leafToRow(const Json& leaf);
Json rowToLeaf(const std::string& subj, const std::string& op, const Json& val);
CondRows condToRows(const Json& c);
Json rowsToCond(const std::string& mode, const std::vector<Json>& rows);
std::vector<std::pair<std::string, std::string>> opsFor(const std::string& kind);   // (op, label key)
Json makeAction(const std::string& type, const std::string& target, const Json& val);
std::string actTarget(const Json& a);
Json actValue(const Json& a);
std::string leafRef(const Json& l);   // "var:x" ... or ""
std::string actRef(const Json& a);
bool nameOk(const std::string& s, bool file = false);
bool idOk(const std::string& s);       // letters, digits, _ and - (NPC, dialogue and node ids)

class EventProject {
public:
    std::string root;
    Json S, base;                      // the working state and what is on disk
    // read-only extras
    std::map<std::string, std::vector<std::string>> npcPlaces;   // npc -> floors whose grid places it
    std::vector<std::string> items, stats, sprites;
    std::map<std::string, Json> origTexts;                       // (not used for saving: texts compare with base)

    struct Step { Json s; std::string sel, label; };
    std::vector<Step> undoStack;
    std::string selected;              // an event id, a missing id or a floor id

    bool load(const std::string& tomsRoot, std::string& err);
    bool dirty() const { return S != base; }

    // edits: one undo step each
    void edit(const std::string& label, const std::function<void()>& fn);
    bool undo(std::string* label = nullptr);

    // lookups
    Json* ev(const std::string& id);
    const Json* ev(const std::string& id) const { return const_cast<EventProject*>(this)->ev(id); }
    Json* flo(const std::string& id);
    const Json* flo(const std::string& id) const { return const_cast<EventProject*>(this)->flo(id); }
    std::vector<const Json*> floorsOf(const std::string& evId) const;
    bool isMissing(const std::string& id) const;
    bool isNewEvent(const std::string& id) const;
    bool isNewPool(const std::string& pool) const;
    bool isNewFloor(const std::string& id) const;
    std::set<std::string> reachable() const;
    Json text(const std::string& key) const;   // {} when absent
    bool isPlaceholder(const std::string& key) const;
    Json reqJson(const Json& e) const;
    bool hasLogic(const Json& e) const;
    std::vector<std::string> pools() const;    // sorted
    std::vector<std::string> kindsList() const;
    std::string kindName(const std::string& k, const std::string& lang) const;
    std::string kindColor(const std::string& k) const;
    std::vector<std::string> dialogueList() const;
    std::vector<Json> dialogueActs(const std::string& id) const;
    std::vector<const Json*> changers(const std::string& ref) const;
    std::vector<const Json*> readers(const std::string& ref) const;
    std::vector<const Json*> readersOnly(const std::string& ref) const;
    std::vector<const Json*> watchers(const std::string& ref) const;
    std::vector<std::string> linkedValues(const std::string& ref) const;
    bool refExists(const std::string& ref) const;
    std::vector<std::string> valueRefs() const;   // var:x, counter:x, flag:x
    Json* recOf(const std::string& ref);
    std::vector<std::string> npcIds() const;
    std::vector<Json*> topicsOf(const std::string& npc);
    bool woken(const std::string& id) const;
    std::string talkFloorOf(const Json& e, bool* conditional = nullptr) const;   // "" = every floor
    void setEventReq(Json& e, const Json& c);
    void setTalkFloor(Json& e, const std::string& floor);
    std::string guessKind(const std::string& id) const;
    std::vector<std::string> choiceIdsOf(const std::string& evId) const;
    std::vector<EvProblem> problems() const;

    // event actions (call inside edit())
    std::string newEventId();
    Json blankEvent(const std::string& id, const std::string& pool) const;
    void makeRecord(const std::string& id, const std::string& act);   // a missing id becomes an event
    void renameEvent(Json& e, const std::string& nu);
    std::string idError(const std::string& id, const Json* self) const;   // "" ok, "format", "dup:<pool>"

    // save
    struct FileWrite { std::string path, before, text; bool existed = false; };
    std::vector<FileWrite> liveWrites(const std::map<std::string, std::string>& comments) const;
    // backs up and writes; base = S. `written` gets the paths.
    bool writeAll(const std::vector<FileWrite>& files, const std::string& stamp, std::string& err);
};

// The talk preview (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): the game's rules (talk_system.cpp, condition.cpp) on a
// pretend run.
struct TalkPreview {
    std::string npc, stage;
    std::set<std::string> finished, started, cleared;
    std::map<std::string, double> vars, counters, stats;
    std::map<std::string, int> counts;
    struct TalkState { int index = 0, count = 0, last = -1; };
    std::map<std::string, TalkState> talks;
    struct MenuItem { std::string id; bool finished = false; };
    struct Entry { int n = 0; std::string kind, dialogue, text; std::vector<MenuItem> menu; };   // kind: chat | topic | menu
    std::vector<Entry> log;   // newest first

    void reset(EventProject& p, const std::string& npc);
    bool eval(const Json& c) const;
    std::vector<MenuItem> menu(EventProject& p, const std::string& npc);
    int chat(const Json& npcFile);                 // nextChatLine: the line index, -1 = none
    std::string firstText(const EventProject& p, const std::string& dlg, const std::string& lang, bool* missing = nullptr) const;
    void talk(EventProject& p, const std::string& npc, const std::string& lang);
    void finish(EventProject& p, const std::string& id);
    void tick(EventProject& p, const std::string& id, bool on);   // a "finished events" box
};

}  // namespace stage
