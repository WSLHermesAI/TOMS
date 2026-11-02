#pragma once

#include <QMainWindow>

class AnimationDock;
class AtlasDocument;
class CanvasPanel;
class ProblemsDock;
class PropertiesDock;
class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QToolButton;
class SpriteTreeDock;

// The editor window: menus, toolbar, status bar and the docks around the canvas. Owns the
// document; every panel talks to the document, not to each other.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* l_pParent = nullptr);

    AtlasDocument* f_Document() const { return m_pDoc; }
    CanvasPanel* f_CanvasPanel() const { return m_pPanel; }
    bool f_OpenProject(const QString& l_oPath);   // with error reporting; adds to recent files
    // Saves to `path` (Save / Save As); logs what was written, reports a refusal.
    bool f_SaveTo(const QString& l_oPath);
    // Add Images / dropped files and folders: copies the PNGs into the project as base art,
    // asking before replacing images that exist. Returns how many went in.
    int f_AddImagePaths(const QStringList& l_oPaths);

protected:
    void closeEvent(QCloseEvent* l_pE) override;
    void dragEnterEvent(QDragEnterEvent* l_pE) override;
    void dropEvent(QDropEvent* l_pE) override;

private:
    void f_CreateActions();
    void f_CreateMenus();
    void f_CreateToolBar();
    void f_CreateStatusBar();
    void f_CreateDocks();
    void f_ReadSettings();
    void f_WriteSettings() const;

    bool f_MaybeSave();
    bool f_Save();
    bool f_SaveAs();
    void f_AddRecentFile(const QString& l_oPath);
    void f_RebuildRecentMenu();
    QString f_LastDir() const;
    void f_RememberDir(const QString& l_oPathOrDir);

    // File
    void f_OnNew();
    void f_OnOpen();
    void f_OnImportAtlas();
    void f_OnAddReferenceFolder();
    void f_OnAddImages();
    void f_OnExtractImages();
    void f_OnExport(bool l_bAllVariants);
    void f_OnExportFinished(bool l_bOk, const QString& l_oSummary);
    // Edit / Sprite
    void f_OnDelete();
    void f_OnSelectAll();
    void f_OnNewChild();
    void f_OnSliceGrid();
    void f_OnToggleBake();
    void f_OnTogglePin();
    void f_OnReplaceImage(bool l_bVariantArt);
    void f_OnRemoveVariantArt();
    void f_OnSaveImageAs();
    void f_OnImportReferences();
    void f_OnEditModeToggled(bool l_bOn);
    // View / Help
    void f_SetDarkTheme(bool l_bDark);
    void f_OnCommandLineHelp();
    void f_OnAbout();

    void f_UpdateTitle();
    void f_UpdateActions();
    void f_UpdateStatus();
    void f_RefreshVariants();

    AtlasDocument* m_pDoc;
    class AutoBackup* m_pBackup = nullptr;
    QDockWidget* m_pHistoryDock = nullptr;
    CanvasPanel* m_pPanel;
    SpriteTreeDock* m_pSpriteDock = nullptr;
    PropertiesDock* m_pPropsDock = nullptr;
    AnimationDock* m_pAnimDock = nullptr;
    ProblemsDock* m_pProblemsDock = nullptr;

    QAction *m_pNewAct, *m_pOpenAct, *m_pSaveAct, *m_pSaveAsAct, *m_pImportAct, *m_pAddFolderAct, *m_pAddImagesAct;
    QAction *m_pExportAct, *m_pExportAllAct, *m_pExtractAct, *m_pQuitAct;
    QAction *m_pUndoAct, *m_pRedoAct, *m_pDeleteAct, *m_pRenameAct, *m_pSelectAllAct, *m_pFindAct;
    QAction *m_pNewChildAct, *m_pSliceAct, *m_pBakeAct, *m_pPinAct, *m_pEditModeAct;
    QAction *m_pImportRefsAct, *m_pReplaceImageAct, *m_pSaveImageAct, *m_pReplaceVariantAct, *m_pRemoveVariantAct;
    QAction *m_pFitAct, *m_pZoomInAct, *m_pZoomOutAct, *m_pOutlinesAct, *m_pChildrenAct, *m_pDarkAct, *m_pLightAct;
    QAction *m_pHelpCliAct, *m_pAboutAct;
    QMenu* m_pRecentMenu = nullptr;
    QMenu* m_pViewMenu = nullptr;
    QMenu* m_pSpriteMenu = nullptr;   // also the context menu of the tree and canvases

    QComboBox* m_pVariantCombo = nullptr;
    QToolButton* m_pZoomButton = nullptr;
    QLabel *m_pStatusStore, *m_pStatusBuild, *m_pStatusPages, *m_pStatusOccupancy, *m_pStatusSprites, *m_pStatusProblems;
};
