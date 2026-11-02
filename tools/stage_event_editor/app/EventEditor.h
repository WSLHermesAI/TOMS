// EventEditor.h -- the event editor, native (docs/gameplay/event_editor/README.md): the HTML editor's model and rules
// (stage_core's EventProject), in a Qt window on the live files.
//
//   left        Events: every pool's events and the ids floors name but no pool defines; find, kind, only problems
//   centre      the inspector (an event, a floor, a missing id) above the tabs: Problems, Floor map, Variables, Logic
//               graph (EventGraph.h), Kinds, NPCs (with the talk preview), Dialogues, and one closable tab per opened value
//   right       History (each edit is one undo step)
//
// Every edit is one snapshot of the whole state (EventProject::edit), so Undo restores everything it touched. The
// widgets are rebuilt after each edit, on the next turn of the event loop (scheduleRender), never inside the signal of
// the widget that made it.
#pragma once

#include "EventGraph.h"
#include "event_project.h"

#include <QMainWindow>

#include <functional>
#include <set>
#include <string>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QSplitter;
class QTabBar;
class QTreeWidget;
class QVBoxLayout;
class QWidget;

class EventEditor : public QMainWindow {
    Q_OBJECT
public:
    explicit EventEditor(QWidget* l_pParent = nullptr);

    bool f_OpenProject(const QString& l_oTomsRoot, QString* l_pError);
    stage::EventProject& f_Project() { return m_oP; }
    bool f_HasUnsaved() const { return m_oP.f_Dirty(); }
    bool f_Save();                           // the Save dialog; true when written or nothing to save
    void f_ReloadFromDisk();                 // the stage editor saved: reread (only when nothing is unsaved here)
    void f_SelectEvent(const QString& l_oId);   // from the stage editor (↗)
    void f_ShowNpc(const QString& l_oId);       // from the stage editor's NPC panel
    void f_ShowTab(const QString& l_oTab) { f_SetTab(l_oTab); }   // problems | floors | vars | logic | kinds | npcs | dlg
    // Its panels (the events tree left, History right) docked where they start, shown, at their starting sizes.
    void f_ResetLayout();
    QStringList f_SelfTest();

signals:
    void f_OpenStage(const QString& l_oFloor, const QString& l_oEventId);   // ⇄: the event's first floor
    void f_PlaceOnStage(const QString& l_oId, const QString& l_oKind, const QStringList& l_oFloors);   // + Add floor… → place it
    void f_FilesWritten(const QStringList& l_oPaths);
    void f_DirtyChanged();

private:
    // edits and rendering
    void f_Edit(const QString& l_oLabel, const std::function<void()>& l_fnFn);
    void f_Undo();
    void f_ScheduleRender();
    void f_RenderAll();
    void f_RenderTree();
    void f_RenderForm();
    void f_RenderTabs();
    void f_RenderPane();
    void f_RenderHistory();
    void f_Select(const std::string& l_strId, bool l_bToTab = false);
    void f_SetTab(const QString& l_oTab);
    void f_Toast(const QString& l_oMsg, bool l_bBad = false);

    // inspector (EventForms.cpp)
    QWidget* f_EventForm(nlohmann::ordered_json& l_jE);
    QWidget* f_FloorForm(nlohmann::ordered_json& l_jF);
    QWidget* f_MissingForm(const std::string& l_strId);
    QWidget* f_LogicBox(nlohmann::ordered_json& l_jE);
    void f_AddFloorsDialog(const std::string& l_strEvId);
    void f_CreateEvent(const std::string& l_strPool);
    void f_CreateFloor(const std::string& l_strId, const std::string& l_strFromId);
    void f_CreateChapter(const std::string& l_strAct);
public:
    // shared by the forms and the tabs: a condition editor and an action list
    // get(): the condition now; set(c): store it (inside an edit)
    QWidget* f_CondBox(const QString& l_oKey, const std::function<nlohmann::ordered_json()>& l_fnGet,
                     const std::function<void(const nlohmann::ordered_json&)>& l_fnSet, const QString& l_oLabel, const QString& l_oNone = QString());
    // get(): the actions now (read only, for building the rows); list(): the live array inside the state, created when
    // missing (only called inside an edit)
    QWidget* f_ActsBox(const QString& l_oKey, const std::function<nlohmann::ordered_json()>& l_fnGet,
                     const std::function<nlohmann::ordered_json*()>& l_fnList, const QString& l_oLabel);
    QComboBox* f_SubjectCombo(const std::string& l_strCur);
    QStringList f_ActionTargets(const std::string& l_strType) const;
    QString f_CatOf(const std::string& l_strRef);   // a value's category (its own, or the automatic group)
    QString f_Lang() const;
    QString f_L(const nlohmann::ordered_json& l_jNameOrText) const;   // a {zh_TW, en} object in the editors' language
private:

    // bottom tabs (EventEditor.cpp, EventTabs.cpp, EventNpcs.cpp)
    QWidget* f_ProblemsPane();
    QWidget* f_FloorMapPane();
    QWidget* f_VarsPane();
    QWidget* f_LogicPane();
    QWidget* f_ValuePane(const std::string& l_strRef);
    QWidget* f_KindsPane();
    QWidget* f_NpcsPane();
    QWidget* f_PreviewBox(const std::string& l_strNpc);
    QWidget* f_DialoguesPane();
    void f_OpenValue(const std::string& l_strRef);
    void f_CloseValue(const std::string& l_strRef);
    QString f_ProblemText(const stage::EvProblem& l_oP) const;
    QString f_RuleName(const std::string& l_strRule) const;
    void f_RunFix(const stage::EvProblem& l_oP, const QString& l_oFix);

    QString m_oRoot;
    stage::EventProject m_oP;
    QString m_oTab = QStringLiteral("problems");
    QStringList m_oOpenVals;
    bool m_bRenderQueued = false;

    // the tree's filter and folding
    QString m_oQ, m_oKind;
    bool m_bProbOnly = false;
    std::set<std::string> m_setCollapsed;
    QString m_oLastFilt;
    // tabs' state
    std::set<std::string> m_setRuleOff{"untranslated"};
    std::set<std::string> m_setCondJson;           // condition editors switched to JSON
    QString m_oVq, m_oVcat;                       // Variables: filter text, category
    std::set<std::string> m_setVtypes{"var", "counter", "flag"};
    std::set<std::string> m_setVarSel;
    std::map<std::string, std::set<std::string>> m_mapKindSel;
    std::string m_strNpcSel, m_strDlgSel;
    stage::TalkPreview m_oPv;
    LogicGraphState m_oGraph;                    // the logic graph's pan, zoom, positions, selection
    LogicGraph* m_pGraphView = nullptr;          // the logic graph while its tab is shown
    int m_iPaneScroll = 0;

    // widgets
    QAction *m_pUndoA = nullptr, *m_pSaveA = nullptr;
    QLabel *m_pSrc = nullptr, *m_pDirty = nullptr, *m_pStatus = nullptr, *m_pProbs = nullptr, *m_pInfo = nullptr;
    QLineEdit* m_pSearch = nullptr;
    QComboBox* m_pKindF = nullptr;
    QCheckBox* m_pProbF = nullptr;
    QLabel* m_pCount = nullptr;
    QTreeWidget* m_pTree = nullptr;
    QScrollArea* m_pForm = nullptr;
    QScrollArea* m_pPane = nullptr;
    QTabBar* m_pTabs = nullptr;
    QListWidget* m_pHist = nullptr;
    QSplitter* m_pSplit = nullptr;
};
