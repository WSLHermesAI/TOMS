// StageEditor.h -- the stage editor, native (docs/19_STAGE_EDITOR.md): the HTML editor's layout and rules, on the
// game's renderer.
//
//   toolbar     ⇄ Event editor, floor (◀ ▶, ● = unsaved), undo / redo, Save
//   tools       🖌 Paint (B), ▭ Rectangle (R), ⌫ Erase (E), ↖ Select / move (V), ✎ Edit (O); # Grid (G),
//               🎮 Game view (P), zoom; ◇ Safe cells; 🎲 Auto-place events…; the brush
//   docks       Palette (types: + adds one, double-click opens its details), Events (this floor's list, every pool),
//               Inspector (the cell, its object, an NPC's topics, the floor), Checks / Story (all floors)
//   map         StageCanvas
//
// The rules are stage_core's (Project, the checks, Save); this class is the window around them. Files are read from and
// written to the TOMS folder directly (each written file is backed up to Build/editor_backups first).
#pragma once

#include "SpriteBank.h"
#include "stage_checks.h"
#include "stage_project.h"

#include <QLabel>
#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

class FlowLayout;
class QDockWidget;
class QAction;
class QComboBox;
class QFileSystemWatcher;
class QLineEdit;
class QScrollArea;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;
class StageCanvas;

class StageEditor : public QMainWindow {
    Q_OBJECT
public:
    enum Tool { Paint, Rect, Erase, Select, Edit };
    struct PlaceMode {
        std::string id;
        std::vector<std::string> floors;
    };
    struct View {
        Tool tool = Paint;
        std::string brush = "wall";
        bool showGrid = true, gameView = false, showSafe = false;
        std::optional<stage::Cell> sel, hover, moveFrom, rectA, rectB;
        std::vector<stage::Cell> flash;
        bool painting = false;
        int paintCount = 0;
        std::optional<PlaceMode> placeMode;
    };

    explicit StageEditor(QWidget* parent = nullptr);
    ~StageEditor() override;

    bool openProject(const QString& tomsRoot, QString* error);
    QString root() const { return m_root; }
    stage::Project& project() { return m_p; }
    SpriteBank& bank() { return m_bank; }
    stage::StageDoc* curDoc() { return m_cur.empty() ? nullptr : &m_p.doc(m_cur); }
    const std::string& curId() const { return m_cur; }
    const View& view() const { return m_v; }
    StageCanvas* canvas() const { return m_canvas; }

    // the canvas
    void canvasPress(const stage::Cell& c);
    void canvasHover(const std::optional<stage::Cell>& c, bool leftDown);
    void canvasRelease();
    void eyedropper(const stage::Cell& c);
    void placeHint(const stage::PlaceCheck& chk);

    // the shell / the event editor
    void openFloor(const std::string& id);
    void showEventOnFloor(const std::string& floor, const std::string& eventId);   // select and flash its cell
    void selectCell(const stage::Cell& c);
    void enterPlaceMode(const std::string& id, const std::string& kind, const std::vector<std::string>& floors);
    bool hasUnsaved() const;
    // Every panel docked where it starts (Palette and Events left, Inspector right, Checks below) and shown -- also one
    // that was closed or left floating off screen. View > Reset stage layout.
    void resetLayout();
    QList<QDockWidget*> panels() const;   // for View > Stage panels
    // The layout is not saved on exit (screenshots and tests leave the user's layout alone).
    void setSaveLayout(bool save) { m_saveLayout = save; }
    bool save();                      // the Save dialog; true when written (or nothing to save)
    void reloadEvents();              // the events / NPC files changed on disk

    // used by ObjectWindow
    bool edit(const QString& label, const std::function<void(stage::StageDoc&)>& fn);
    void typesChanged();              // a type record changed: palette, map, inspector
    // The palette's own categories (Project::paletteGroups): add / rename / delete one, move a type into one ("" = back
    // to its usual group). False when the name is empty or taken. The ...Dialog ones ask first (the palette's buttons).
    bool addCategory(const std::string& name);
    bool renameCategory(const std::string& from, const std::string& to);
    void deleteCategory(const std::string& name);
    bool moveToCategory(const std::string& sem, const std::string& group);
    void newCategoryDialog();
    void renameCategoryDialog(const std::string& name);
    void deleteCategoryDialog(const std::string& name);
    void setBrush(const std::string& sem);
    void revealBrush();   // scrolls the palette to the brush's button (a type just made)
    void setTool(Tool t);
    void toast(const QString& msg, bool bad = false);

    // --selftest: paints, undoes, opens the windows, checks the save list; returns the failures (empty = passed)
    QStringList selfTest();

signals:
    void openEventEditor(const QString& eventId);   // "" = only show it
    void openNpcInEventEditor(const QString& npcId);
    void filesWritten(const QStringList& paths);
    void dirtyChanged();

private:
    void buildUi();
    void buildToolbars();
    void buildDocks();
    void retranslateDynamic();        // the language changed: every list and label built with names
    void afterEdit();
    void undo();
    void redo();
    void step(int delta);
    void paintAt(const stage::Cell& c);
    void renderAll();
    void renderFloorSel();
    void renderPalette();
    void renderEventPalette();
    void renderInspector();
    void renderProblems();
    void renderStory();
    void renderPlaceBar();
    void updateBrushInfo();
    void updateToolButtons();
    void updateTitle();
    void flashCells(const std::vector<stage::Cell>& cells);
    void openAutoPlace();
    void openObjectWindow(const stage::Cell& c);
    void openTypeWindow(const std::string& sem);
    void openNewType(const std::string& cat);
    void watchFiles();
    QWidget* floorPanel(stage::StageDoc& d);
    QWidget* npcPanel(const std::string& npc);
    QString hoverText(const stage::Cell& c);

    QString m_root;
    stage::Project m_p;
    SpriteBank m_bank;
    std::string m_cur;
    View m_v;

    StageCanvas* m_canvas = nullptr;
    QComboBox* m_floorSel = nullptr;
    QAction *m_undoA = nullptr, *m_redoA = nullptr, *m_saveA = nullptr, *m_toEventA = nullptr;
    QAction *m_gridA = nullptr, *m_gameA = nullptr, *m_safeA = nullptr;
    QAction* m_toolA[5] = {};
    QLabel *m_zoomLbl = nullptr, *m_brushIcon = nullptr, *m_brushName = nullptr, *m_src = nullptr;
    QLabel *m_hover = nullptr, *m_status = nullptr;
    QWidget* m_placeBar = nullptr;
    QLabel* m_pmText = nullptr;
    QToolButton *m_pmSuggest = nullptr, *m_pmAuto = nullptr, *m_pmNext = nullptr;
    QScrollArea* m_paletteArea = nullptr;
    QLineEdit* m_evSearch = nullptr;
    QTreeWidget* m_evTree = nullptr;
    QScrollArea* m_inspArea = nullptr;
    QTabWidget* m_bottomTabs = nullptr;
    QTreeWidget* m_checks = nullptr;
    QTableWidget* m_story = nullptr;
    QLabel* m_storySum = nullptr;
    QString m_storyKey;   // what the story table shows (check version, type data, unsaved floors): rebuilt only when it changes
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_reloadTimer = nullptr;
    QTimer* m_flashTimer = nullptr;
    QTimer* m_checksTimer = nullptr;   // afterEdit's deferred renderProblems
    std::set<std::string> m_collapsed;
    bool m_ignoreWatch = false;
    bool m_saveLayout = true;
    bool m_revealBrush = false;
};
