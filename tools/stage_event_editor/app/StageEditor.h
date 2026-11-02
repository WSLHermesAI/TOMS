// StageEditor.h -- the stage editor, native (docs/gameplay/02_STAGE_EDITOR.md): the HTML editor's layout and rules, on the
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
        std::string m_strId;
        std::vector<std::string> m_vFloors;
    };
    struct View {
        Tool m_eTool = Paint;
        std::string m_strBrush = "wall";
        bool m_bShowGrid = true, m_bGameView = false, m_bShowSafe = false;
        std::optional<stage::Cell> m_optSel, m_optHover, m_optMoveFrom, m_optRectA, m_optRectB;
        std::vector<stage::Cell> m_vFlash;
        bool m_bPainting = false;
        int m_iPaintCount = 0;
        std::optional<PlaceMode> m_optPlaceMode;
    };

    explicit StageEditor(QWidget* l_pParent = nullptr);
    ~StageEditor() override;

    bool f_OpenProject(const QString& l_oTomsRoot, QString* l_pError);
    QString f_Root() const { return m_oRoot; }
    stage::Project& f_Project() { return m_oP; }
    SpriteBank& f_Bank() { return m_oBank; }
    stage::StageDoc* f_CurDoc() { return m_strCur.empty() ? nullptr : &m_oP.f_Doc(m_strCur); }
    const std::string& f_CurId() const { return m_strCur; }
    const View& f_View() const { return m_oV; }
    StageCanvas* f_Canvas() const { return m_pCanvas; }

    // the canvas
    void f_CanvasPress(const stage::Cell& l_oC);
    void f_CanvasHover(const std::optional<stage::Cell>& l_optC, bool l_bLeftDown);
    void f_CanvasRelease();
    void f_Eyedropper(const stage::Cell& l_oC);
    void f_PlaceHint(const stage::PlaceCheck& l_oChk);

    // the shell / the event editor
    void f_OpenFloor(const std::string& l_strId);
    void f_ShowEventOnFloor(const std::string& l_strFloor, const std::string& l_strEventId);   // select and flash its cell
    void f_SelectCell(const stage::Cell& l_oC);
    void f_EnterPlaceMode(const std::string& l_strId, const std::string& l_strKind, const std::vector<std::string>& l_vFloors);
    bool f_HasUnsaved() const;
    // Every panel docked where it starts (Palette and Events left, Inspector right, Checks below) and shown -- also one
    // that was closed or left floating off screen. View > Reset stage layout.
    void f_ResetLayout();
    QList<QDockWidget*> f_Panels() const;   // for View > Stage panels
    // The layout is not saved on exit (screenshots and tests leave the user's layout alone).
    void f_SetSaveLayout(bool l_bSave) { m_bSaveLayout = l_bSave; }
    bool f_Save();                      // the Save dialog; true when written (or nothing to save)
    void f_ReloadEvents();              // the events / NPC files changed on disk

    // used by ObjectWindow
    bool f_Edit(const QString& l_oLabel, const std::function<void(stage::StageDoc&)>& l_fnFn);
    void f_TypesChanged();              // a type record changed: palette, map, inspector
    // The palette's own categories (Project::paletteGroups): add / rename / delete one, move a type into one ("" = back
    // to its usual group). False when the name is empty or taken. The ...Dialog ones ask first (the palette's buttons).
    bool f_AddCategory(const std::string& l_strName);
    bool f_RenameCategory(const std::string& l_strFrom, const std::string& l_strTo);
    void f_DeleteCategory(const std::string& l_strName);
    bool f_MoveToCategory(const std::string& l_strSem, const std::string& l_strGroup);
    void f_NewCategoryDialog();
    void f_RenameCategoryDialog(const std::string& l_strName);
    void f_DeleteCategoryDialog(const std::string& l_strName);
    void f_SetBrush(const std::string& l_strSem);
    void f_RevealBrush();
    void f_SyncTheme();     // Project::theme from the open floor's stage file   // scrolls the palette to the brush's button (a type just made)
    void f_SetTool(Tool l_eT);
    void f_Toast(const QString& l_oMsg, bool l_bBad = false);

    // --selftest: paints, undoes, opens the windows, checks the save list; returns the failures (empty = passed)
    QStringList f_SelfTest();

signals:
    void f_OpenEventEditor(const QString& l_oEventId);   // "" = only show it
    void f_OpenNpcInEventEditor(const QString& l_oNpcId);
    void f_FilesWritten(const QStringList& l_oPaths);
    void f_DirtyChanged();

private:
    void f_BuildUi();
    void f_BuildToolbars();
    void f_BuildDocks();
    void f_RetranslateDynamic();        // the language changed: every list and label built with names
    void f_AfterEdit();
    void f_Undo();
    void f_Redo();
    void f_Step(int l_iDelta);
    void f_PaintAt(const stage::Cell& l_oC);
    void f_RenderAll();
    void f_RenderFloorSel();
    void f_RenderPalette();
    void f_RenderEventPalette();
    void f_RenderInspector();
    void f_RenderProblems();
    void f_RenderStory();
    void f_RenderPlaceBar();
    void f_UpdateBrushInfo();
    void f_UpdateToolButtons();
    void f_UpdateTitle();
    void f_FlashCells(const std::vector<stage::Cell>& l_vCells);
    void f_OpenAutoPlace();
    void f_OpenObjectWindow(const stage::Cell& l_oC);
    void f_OpenTypeWindow(const std::string& l_strSem);
    void f_OpenNewType(const std::string& l_strCat);
    void f_WatchFiles();
    QWidget* f_FloorPanel(stage::StageDoc& l_oD);
    QWidget* f_NpcPanel(const std::string& l_strNpc);
    QString f_HoverText(const stage::Cell& l_oC);

    QString m_oRoot;
    stage::Project m_oP;
    SpriteBank m_oBank;
    std::string m_strCur;
    View m_oV;

    StageCanvas* m_pCanvas = nullptr;
    QComboBox* m_pFloorSel = nullptr;
    QAction *m_pUndoA = nullptr, *m_pRedoA = nullptr, *m_pSaveA = nullptr, *m_pToEventA = nullptr;
    QAction *m_pGridA = nullptr, *m_pGameA = nullptr, *m_pSafeA = nullptr;
    QAction* m_arrToolA[5] = {};
    QLabel *m_pZoomLbl = nullptr, *m_pBrushIcon = nullptr, *m_pBrushName = nullptr, *m_pSrc = nullptr;
    QLabel *m_pHover = nullptr, *m_pStatus = nullptr;
    QWidget* m_pPlaceBar = nullptr;
    QLabel* m_pPmText = nullptr;
    QToolButton *m_pPmSuggest = nullptr, *m_pPmAuto = nullptr, *m_pPmNext = nullptr;
    QScrollArea* m_pPaletteArea = nullptr;
    QLineEdit* m_pEvSearch = nullptr;
    QTreeWidget* m_pEvTree = nullptr;
    QScrollArea* m_pInspArea = nullptr;
    QTabWidget* m_pBottomTabs = nullptr;
    QTreeWidget* m_pChecks = nullptr;
    QTableWidget* m_pStory = nullptr;
    QLabel* m_pStorySum = nullptr;
    QString m_oStoryKey;   // what the story table shows (check version, type data, unsaved floors): rebuilt only when it changes
    QFileSystemWatcher* m_pWatcher = nullptr;
    QTimer* m_pReloadTimer = nullptr;
    QTimer* m_pFlashTimer = nullptr;
    QTimer* m_pChecksTimer = nullptr;   // afterEdit's deferred renderProblems
    std::set<std::string> m_setCollapsed;
    bool m_bIgnoreWatch = false;
    bool m_bSaveLayout = true;
    bool m_bRevealBrush = false;
};
