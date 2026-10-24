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
    explicit EventEditor(QWidget* parent = nullptr);

    bool openProject(const QString& tomsRoot, QString* error);
    stage::EventProject& project() { return m_p; }
    bool hasUnsaved() const { return m_p.dirty(); }
    bool save();                           // the Save dialog; true when written or nothing to save
    void reloadFromDisk();                 // the stage editor saved: reread (only when nothing is unsaved here)
    void selectEvent(const QString& id);   // from the stage editor (↗)
    void showNpc(const QString& id);       // from the stage editor's NPC panel
    void showTab(const QString& tab) { setTab(tab); }   // problems | floors | vars | logic | kinds | npcs | dlg
    // Its panels (the events tree left, History right) docked where they start, shown, at their starting sizes.
    void resetLayout();
    QStringList selfTest();

signals:
    void openStage(const QString& floor, const QString& eventId);   // ⇄: the event's first floor
    void placeOnStage(const QString& id, const QString& kind, const QStringList& floors);   // + Add floor… → place it
    void filesWritten(const QStringList& paths);
    void dirtyChanged();

private:
    // edits and rendering
    void edit(const QString& label, const std::function<void()>& fn);
    void undo();
    void scheduleRender();
    void renderAll();
    void renderTree();
    void renderForm();
    void renderTabs();
    void renderPane();
    void renderHistory();
    void select(const std::string& id, bool toTab = false);
    void setTab(const QString& tab);
    void toast(const QString& msg, bool bad = false);

    // inspector (EventForms.cpp)
    QWidget* eventForm(nlohmann::ordered_json& e);
    QWidget* floorForm(nlohmann::ordered_json& f);
    QWidget* missingForm(const std::string& id);
    QWidget* logicBox(nlohmann::ordered_json& e);
    void addFloorsDialog(const std::string& evId);
    void createEvent(const std::string& pool);
    void createFloor(const std::string& id, const std::string& fromId);
    void createChapter(const std::string& act);
public:
    // shared by the forms and the tabs: a condition editor and an action list
    // get(): the condition now; set(c): store it (inside an edit)
    QWidget* condBox(const QString& key, const std::function<nlohmann::ordered_json()>& get,
                     const std::function<void(const nlohmann::ordered_json&)>& set, const QString& label, const QString& none = QString());
    // get(): the actions now (read only, for building the rows); list(): the live array inside the state, created when
    // missing (only called inside an edit)
    QWidget* actsBox(const QString& key, const std::function<nlohmann::ordered_json()>& get,
                     const std::function<nlohmann::ordered_json*()>& list, const QString& label);
    QComboBox* subjectCombo(const std::string& cur);
    QStringList actionTargets(const std::string& type) const;
    QString catOf(const std::string& ref);   // a value's category (its own, or the automatic group)
    QString lang() const;
    QString L(const nlohmann::ordered_json& nameOrText) const;   // a {zh_TW, en} object in the editors' language
private:

    // bottom tabs (EventEditor.cpp, EventTabs.cpp, EventNpcs.cpp)
    QWidget* problemsPane();
    QWidget* floorMapPane();
    QWidget* varsPane();
    QWidget* logicPane();
    QWidget* valuePane(const std::string& ref);
    QWidget* kindsPane();
    QWidget* npcsPane();
    QWidget* previewBox(const std::string& npc);
    QWidget* dialoguesPane();
    void openValue(const std::string& ref);
    void closeValue(const std::string& ref);
    QString problemText(const stage::EvProblem& p) const;
    QString ruleName(const std::string& rule) const;
    void runFix(const stage::EvProblem& p, const QString& fix);

    QString m_root;
    stage::EventProject m_p;
    QString m_tab = QStringLiteral("problems");
    QStringList m_openVals;
    bool m_renderQueued = false;

    // the tree's filter and folding
    QString m_q, m_kind;
    bool m_probOnly = false;
    std::set<std::string> m_collapsed;
    QString m_lastFilt;
    // tabs' state
    std::set<std::string> m_ruleOff{"untranslated"};
    std::set<std::string> m_condJson;           // condition editors switched to JSON
    QString m_vq, m_vcat;                       // Variables: filter text, category
    std::set<std::string> m_vtypes{"var", "counter", "flag"};
    std::set<std::string> m_varSel;
    std::map<std::string, std::set<std::string>> m_kindSel;
    std::string m_npcSel, m_dlgSel;
    stage::TalkPreview m_pv;
    LogicGraphState m_graph;                    // the logic graph's pan, zoom, positions, selection
    LogicGraph* m_graphView = nullptr;          // the logic graph while its tab is shown
    int m_paneScroll = 0;

    // widgets
    QAction *m_undoA = nullptr, *m_saveA = nullptr;
    QLabel *m_src = nullptr, *m_dirty = nullptr, *m_status = nullptr, *m_probs = nullptr, *m_info = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_kindF = nullptr;
    QCheckBox* m_probF = nullptr;
    QLabel* m_count = nullptr;
    QTreeWidget* m_tree = nullptr;
    QScrollArea* m_form = nullptr;
    QScrollArea* m_pane = nullptr;
    QTabBar* m_tabs = nullptr;
    QListWidget* m_hist = nullptr;
    QSplitter* m_split = nullptr;
};
