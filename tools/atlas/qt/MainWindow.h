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
    explicit MainWindow(QWidget* parent = nullptr);

    AtlasDocument* document() const { return m_doc; }
    CanvasPanel* canvasPanel() const { return m_panel; }
    bool openProject(const QString& path);   // with error reporting; adds to recent files
    // Saves to `path` (Save / Save As); logs what was written, reports a refusal.
    bool saveTo(const QString& path);
    // Add Images / dropped files and folders: copies the PNGs into the project as base art,
    // asking before replacing images that exist. Returns how many went in.
    int addImagePaths(const QStringList& paths);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    void createDocks();
    void readSettings();
    void writeSettings() const;

    bool maybeSave();
    bool save();
    bool saveAs();
    void addRecentFile(const QString& path);
    void rebuildRecentMenu();
    QString lastDir() const;
    void rememberDir(const QString& pathOrDir);

    // File
    void onNew();
    void onOpen();
    void onImportAtlas();
    void onAddReferenceFolder();
    void onAddImages();
    void onExtractImages();
    void onExport(bool allVariants);
    void onExportFinished(bool ok, const QString& summary);
    // Edit / Sprite
    void onDelete();
    void onSelectAll();
    void onNewChild();
    void onSliceGrid();
    void onToggleBake();
    void onTogglePin();
    void onReplaceImage(bool variantArt);
    void onRemoveVariantArt();
    void onSaveImageAs();
    void onImportReferences();
    void onEditModeToggled(bool on);
    // View / Help
    void setDarkTheme(bool dark);
    void onCommandLineHelp();
    void onAbout();

    void updateTitle();
    void updateActions();
    void updateStatus();
    void refreshVariants();

    AtlasDocument* m_doc;
    CanvasPanel* m_panel;
    SpriteTreeDock* m_spriteDock = nullptr;
    PropertiesDock* m_propsDock = nullptr;
    AnimationDock* m_animDock = nullptr;
    ProblemsDock* m_problemsDock = nullptr;

    QAction *m_newAct, *m_openAct, *m_saveAct, *m_saveAsAct, *m_importAct, *m_addFolderAct, *m_addImagesAct;
    QAction *m_exportAct, *m_exportAllAct, *m_extractAct, *m_quitAct;
    QAction *m_undoAct, *m_redoAct, *m_deleteAct, *m_renameAct, *m_selectAllAct, *m_findAct;
    QAction *m_newChildAct, *m_sliceAct, *m_bakeAct, *m_pinAct, *m_editModeAct;
    QAction *m_importRefsAct, *m_replaceImageAct, *m_saveImageAct, *m_replaceVariantAct, *m_removeVariantAct;
    QAction *m_fitAct, *m_zoomInAct, *m_zoomOutAct, *m_outlinesAct, *m_childrenAct, *m_darkAct, *m_lightAct;
    QAction *m_helpCliAct, *m_aboutAct;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QMenu* m_spriteMenu = nullptr;   // also the context menu of the tree and canvases

    QComboBox* m_variantCombo = nullptr;
    QToolButton* m_zoomButton = nullptr;
    QLabel *m_statusStore, *m_statusBuild, *m_statusPages, *m_statusOccupancy, *m_statusSprites, *m_statusProblems;
};
