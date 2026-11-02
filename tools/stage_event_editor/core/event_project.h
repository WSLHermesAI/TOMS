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

extern const char* const g_arrLangs[6];   // zh_TW, en, zh_CN, ja, ko, es: the event text languages

// One problem of the validator (the HTML editor's RULES): where is a message code + arguments ("" code = args[0] as is)
struct EvProblem {
    std::string m_strRule, m_strId, m_strAct, m_strTo;
    std::string m_strWhereCode;
    std::vector<std::string> m_vWhereArgs;
};
const std::vector<std::pair<std::string, int>>& f_EvRules();   // rule -> severity (0 error, 1 warn, 2 info), in display order
int f_RuleSev(const std::string& l_strRule);

// A condition row (condition.cpp leaf): subject "var:x" / "counter:x" / "flag:x" / "event:x" / "talks:npc" / "stat:s" /
// "floor:F05" / "item:id" / "stage:id" / "choice:eventId", an operator, a value.
struct CondRow {
    std::string m_strSubj, m_strOp;
    Json m_jVal;
};
struct CondRows {
    std::string m_strMode = "all";   // "all" | "any"
    std::vector<Json> m_vRows;     // leaves
    bool m_bRaw = false;           // nested logic: shown as JSON
};
CondRow f_LeafToRow(const Json& l_jLeaf);
Json f_RowToLeaf(const std::string& l_strSubj, const std::string& l_strOp, const Json& l_jVal);
CondRows f_CondToRows(const Json& l_jC);
Json f_RowsToCond(const std::string& l_strMode, const std::vector<Json>& l_vRows);
std::vector<std::pair<std::string, std::string>> f_OpsFor(const std::string& l_strKind);   // (op, label key)
Json f_MakeAction(const std::string& l_strType, const std::string& l_strTarget, const Json& l_jVal);
std::string f_ActTarget(const Json& l_jA);
Json f_ActValue(const Json& l_jA);
std::string f_LeafRef(const Json& l_jL);   // "var:x" ... or ""
std::string f_ActRef(const Json& l_jA);
bool f_NameOk(const std::string& l_strS, bool l_bFile = false);
bool f_IdOk(const std::string& l_strS);       // letters, digits, _ and - (NPC, dialogue and node ids)

class EventProject {
public:
    std::string m_strRoot;
    Json m_jS, m_jBase;                      // the working state and what is on disk
    // read-only extras
    std::map<std::string, std::vector<std::string>> m_mapNpcPlaces;   // npc -> floors whose grid places it
    std::vector<std::string> m_vItems, m_vStats, m_vSprites;
    std::map<std::string, Json> m_mapOrigTexts;                       // (not used for saving: texts compare with base)

    struct Step { Json m_jS; std::string m_strSel, m_strLabel; };
    std::vector<Step> m_vUndoStack;
    std::string m_strSelected;              // an event id, a missing id or a floor id

    bool f_Load(const std::string& l_strTomsRoot, std::string& l_strErr);
    bool f_Dirty() const { return m_jS != m_jBase; }

    // edits: one undo step each
    void f_Edit(const std::string& l_strLabel, const std::function<void()>& l_fnFn);
    bool f_Undo(std::string* l_pLabel = nullptr);

    // lookups
    Json* f_Ev(const std::string& l_strId);
    const Json* f_Ev(const std::string& l_strId) const { return const_cast<EventProject*>(this)->f_Ev(l_strId); }
    Json* f_Flo(const std::string& l_strId);
    const Json* f_Flo(const std::string& l_strId) const { return const_cast<EventProject*>(this)->f_Flo(l_strId); }
    std::vector<const Json*> f_FloorsOf(const std::string& l_strEvId) const;
    bool f_IsMissing(const std::string& l_strId) const;
    bool f_IsNewEvent(const std::string& l_strId) const;
    bool f_IsNewPool(const std::string& l_strPool) const;
    bool f_IsNewFloor(const std::string& l_strId) const;
    std::set<std::string> f_Reachable() const;
    Json f_Text(const std::string& l_strKey) const;   // {} when absent
    bool f_IsPlaceholder(const std::string& l_strKey) const;
    Json f_ReqJson(const Json& l_jE) const;
    bool f_HasLogic(const Json& l_jE) const;
    std::vector<std::string> f_Pools() const;    // sorted
    std::vector<std::string> f_KindsList() const;
    std::string f_KindName(const std::string& l_strK, const std::string& l_strLang) const;
    std::string f_KindColor(const std::string& l_strK) const;
    std::vector<std::string> f_DialogueList() const;
    std::vector<Json> f_DialogueActs(const std::string& l_strId) const;
    std::vector<const Json*> f_Changers(const std::string& l_strRef) const;
    std::vector<const Json*> f_Readers(const std::string& l_strRef) const;
    std::vector<const Json*> f_ReadersOnly(const std::string& l_strRef) const;
    std::vector<const Json*> f_Watchers(const std::string& l_strRef) const;
    std::vector<std::string> f_LinkedValues(const std::string& l_strRef) const;
    bool f_RefExists(const std::string& l_strRef) const;
    std::vector<std::string> f_ValueRefs() const;   // var:x, counter:x, flag:x
    Json* f_RecOf(const std::string& l_strRef);
    std::vector<std::string> f_NpcIds() const;
    std::vector<Json*> f_TopicsOf(const std::string& l_strNpc);
    bool f_Woken(const std::string& l_strId) const;
    std::string f_TalkFloorOf(const Json& l_jE, bool* l_pConditional = nullptr) const;   // "" = every floor
    void f_SetEventReq(Json& l_jE, const Json& l_jC);
    void f_SetTalkFloor(Json& l_jE, const std::string& l_strFloor);
    std::string f_GuessKind(const std::string& l_strId) const;
    std::vector<std::string> f_ChoiceIdsOf(const std::string& l_strEvId) const;
    std::vector<EvProblem> f_Problems() const;

    // event actions (call inside edit())
    std::string f_NewEventId();
    Json f_BlankEvent(const std::string& l_strId, const std::string& l_strPool) const;
    void f_MakeRecord(const std::string& l_strId, const std::string& l_strAct);   // a missing id becomes an event
    void f_RenameEvent(Json& l_jE, const std::string& l_strNu);
    std::string f_IdError(const std::string& l_strId, const Json* l_pSelf) const;   // "" ok, "format", "dup:<pool>"

    // save
    struct FileWrite { std::string m_strPath, m_strBefore, m_strText; bool m_bExisted = false; };
    std::vector<FileWrite> f_LiveWrites(const std::map<std::string, std::string>& l_mapComments) const;
    // backs up and writes; base = S. `written` gets the paths.
    bool f_WriteAll(const std::vector<FileWrite>& l_vFiles, const std::string& l_strStamp, std::string& l_strErr);
};

// The talk preview (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): the game's rules (talk_system.cpp, condition.cpp) on a
// pretend run.
struct TalkPreview {
    std::string m_strNpc, m_strStage;
    std::set<std::string> m_setFinished, m_setStarted, m_setCleared;
    std::map<std::string, double> m_mapVars, m_mapCounters, m_mapStats;
    std::map<std::string, int> m_mapCounts;
    struct TalkState { int m_iIndex = 0, m_iCount = 0, m_iLast = -1; };
    std::map<std::string, TalkState> m_mapTalks;
    struct MenuItem { std::string m_strId; bool m_bFinished = false; };
    struct Entry { int m_iN = 0; std::string m_strKind, m_strDialogue, m_strText; std::vector<MenuItem> m_vMenu; };   // kind: chat | topic | menu
    std::vector<Entry> m_vLog;   // newest first

    void f_Reset(EventProject& l_oP, const std::string& l_strNpc);
    bool f_Eval(const Json& l_jC) const;
    std::vector<MenuItem> f_Menu(EventProject& l_oP, const std::string& l_strNpc);
    int f_Chat(const Json& l_jNpcFile);                 // nextChatLine: the line index, -1 = none
    std::string f_FirstText(const EventProject& l_oP, const std::string& l_strDlg, const std::string& l_strLang, bool* l_pMissing = nullptr) const;
    void f_Talk(EventProject& l_oP, const std::string& l_strNpc, const std::string& l_strLang);
    void f_Finish(EventProject& l_oP, const std::string& l_strId);
    void f_Tick(EventProject& l_oP, const std::string& l_strId, bool l_bOn);   // a "finished events" box
};

}  // namespace stage
