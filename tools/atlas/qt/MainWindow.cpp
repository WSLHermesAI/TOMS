#include "MainWindow.h"

#include "AnimationDock.h"
#include "AtlasCanvas.h"
#include "AutoBackup.h"
#include "AtlasDocument.h"
#include "CanvasPanel.h"
#include "GridSliceDialog.h"
#include "Icons.h"
#include "ProblemsDock.h"
#include "PropertiesDock.h"
#include "ReferencesDialog.h"
#include "SpriteEditCanvas.h"
#include "SpriteTreeDock.h"
#include "Theme.h"
#include "atlas_export.h"
#include "atlas_store.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace {

constexpr int g_iMaxRecent = 10;
constexpr int g_iStateVersion = 1;   // bump when the dock layout changes incompatibly

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_U8(const QString& l_oS) { return l_oS.toStdString(); }

QSettings f_Settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("AtlasEditor")); }

QAction* f_MakeAction(QObject* l_pParent, const QString& l_oText, const QKeySequence& l_oKey = QKeySequence(),
                    Icons::Id l_eIcon = Icons::Id(-1))
{
    auto* l_pA = new QAction(l_oText, l_pParent);
    if (!l_oKey.isEmpty()) l_pA->setShortcut(l_oKey);
    if (int(l_eIcon) >= 0) l_pA->setIcon(Icons::f_Icon(l_eIcon));
    return l_pA;
}

}  // namespace

MainWindow::MainWindow(QWidget* l_pParent)
    : QMainWindow(l_pParent)
    , m_pDoc(new AtlasDocument(this))
    , m_pPanel(new CanvasPanel(m_pDoc, this))
{
    setObjectName(QStringLiteral("AtlasEditorMainWindow"));
    setCentralWidget(m_pPanel);
    setDockNestingEnabled(true);
    setAcceptDrops(true);

    f_CreateActions();
    f_CreateDocks();
    f_CreateMenus();
    f_CreateToolBar();
    f_CreateStatusBar();

    connect(m_pDoc, &AtlasDocument::f_DirtyChanged, this, &MainWindow::f_UpdateTitle);
    connect(m_pDoc, &AtlasDocument::f_FilePathChanged, this, &MainWindow::f_UpdateTitle);
    connect(m_pDoc, &AtlasDocument::f_ProjectChanged, this, &MainWindow::f_UpdateTitle);
    connect(m_pDoc, &AtlasDocument::f_ProjectChanged, this, &MainWindow::f_UpdateStatus);
    connect(m_pDoc, &AtlasDocument::f_VariantChanged, this, &MainWindow::f_UpdateActions);
    connect(m_pDoc, &AtlasDocument::f_SelectionChanged, this, &MainWindow::f_UpdateActions);
    connect(m_pDoc, &AtlasDocument::f_ProjectChanged, this, &MainWindow::f_UpdateActions);
    connect(m_pDoc, &AtlasDocument::f_ProjectChanged, this, &MainWindow::f_RefreshVariants);
    connect(m_pDoc, &AtlasDocument::f_VariantChanged, this, &MainWindow::f_RefreshVariants);
    connect(m_pDoc, &AtlasDocument::f_BuildStarted, this, &MainWindow::f_UpdateStatus);
    connect(m_pDoc, &AtlasDocument::f_BuildFinished, this, &MainWindow::f_UpdateStatus);
    connect(m_pDoc, &AtlasDocument::f_BuildFinished, this, &MainWindow::f_UpdateActions);
    connect(m_pDoc, &AtlasDocument::f_ExportFinished, this, &MainWindow::f_OnExportFinished);
    connect(m_pDoc, &AtlasDocument::f_Message, this, [this](const QString& l_oM) { statusBar()->showMessage(l_oM, 5000); });
    connect(m_pPanel, &CanvasPanel::f_EditModeChanged, this, [this](bool l_bOn) {
        m_pEditModeAct->setChecked(l_bOn);
        f_UpdateStatus();
    });
    connect(m_pPanel->f_AtlasCanvas(), &AtlasCanvas::f_PageChanged, this, &MainWindow::f_UpdateStatus);
    connect(m_pPanel, &CanvasPanel::f_ZoomChanged, this, [this](double l_dZ) { m_pZoomButton->setText(QStringLiteral("%1%").arg(qRound(l_dZ * 100))); });
    auto l_fnShowSpriteMenu = [this](const QPoint& l_oAt) { m_pSpriteMenu->exec(l_oAt); };
    connect(m_pPanel->f_AtlasCanvas(), &AtlasCanvas::f_ContextMenuRequested, this, l_fnShowSpriteMenu);
    connect(m_pPanel->f_EditCanvas(), &SpriteEditCanvas::f_ContextMenuRequested, this, l_fnShowSpriteMenu);
    connect(m_pSpriteDock, &SpriteTreeDock::f_ContextMenuRequested, this, l_fnShowSpriteMenu);
    connect(m_pSpriteDock, &SpriteTreeDock::f_EditSpriteRequested, m_pPanel, &CanvasPanel::f_EnterEditMode);
    connect(m_pPropsDock, &PropertiesDock::f_ImportReferencesRequested, this, &MainWindow::f_OnImportReferences);

    f_ReadSettings();
    f_UpdateTitle();
    f_UpdateActions();
    f_UpdateStatus();
    f_RefreshVariants();
}

// ---- construction -------------------------------------------------------------------------------

void MainWindow::f_CreateActions()
{
    using Id = Icons::Id;
    m_pNewAct = f_MakeAction(this, tr("&New"), QKeySequence::New, Id::New);
    m_pOpenAct = f_MakeAction(this, tr("&Open…"), QKeySequence::Open, Id::Open);
    m_pSaveAct = f_MakeAction(this, tr("&Save"), QKeySequence::Save, Id::Save);
    m_pSaveAsAct = f_MakeAction(this, tr("Save &As…"), QKeySequence::SaveAs);
    m_pImportAct = f_MakeAction(this, tr("&Import Atlas…"), QKeySequence(tr("Ctrl+I")), Id::Import);
    m_pImportAct->setToolTip(tr("Import a .pi / .atlas / TexturePacker .json / Cocos .plist atlas as a new project"));
    m_pSaveAct->setToolTip(tr("Save the project and its packed atlas (the images live there) (Ctrl+S)"));
    m_pAddFolderAct = f_MakeAction(this, tr("Add &Reference Folder…"), QKeySequence(), Id::Folder);
    m_pAddFolderAct->setToolTip(tr("A folder to import new or changed art from (Sprite > Import From References)"));
    m_pAddImagesAct = f_MakeAction(this, tr("Add &Images…"), QKeySequence(tr("Ctrl+Shift+I")), Id::Image);
    m_pAddImagesAct->setToolTip(tr("Copy PNG files into the project"));
    m_pExtractAct = f_MakeAction(this, tr("E&xtract Images…"));
    m_pExtractAct->setToolTip(tr("Write every sprite as its own PNG (the way back to separate files)"));
    m_pExportAct = f_MakeAction(this, tr("&Export Copy To Folder…"), QKeySequence(tr("Ctrl+B")), Id::Export);
    m_pExportAct->setToolTip(tr("Write the atlas files (shown art) to a folder of your choice; the project is not touched (Ctrl+B)"));
    m_pExportAllAct = f_MakeAction(this, tr("Export All &Variants To Folder…"), QKeySequence(tr("Ctrl+Shift+B")));
    m_pExportAllAct->setToolTip(tr("Base art into the folder, each variant into <folder>/<variant id>"));
    m_pQuitAct = f_MakeAction(this, tr("&Quit"), QKeySequence::Quit);
    connect(m_pNewAct, &QAction::triggered, this, &MainWindow::f_OnNew);
    connect(m_pOpenAct, &QAction::triggered, this, &MainWindow::f_OnOpen);
    connect(m_pSaveAct, &QAction::triggered, this, &MainWindow::f_Save);
    connect(m_pSaveAsAct, &QAction::triggered, this, &MainWindow::f_SaveAs);
    connect(m_pImportAct, &QAction::triggered, this, &MainWindow::f_OnImportAtlas);
    connect(m_pAddFolderAct, &QAction::triggered, this, &MainWindow::f_OnAddReferenceFolder);
    connect(m_pAddImagesAct, &QAction::triggered, this, &MainWindow::f_OnAddImages);
    connect(m_pExtractAct, &QAction::triggered, this, &MainWindow::f_OnExtractImages);
    connect(m_pExportAct, &QAction::triggered, this, [this] { f_OnExport(false); });
    connect(m_pExportAllAct, &QAction::triggered, this, [this] { f_OnExport(true); });
    connect(m_pQuitAct, &QAction::triggered, this, &QWidget::close);

    m_pUndoAct = m_pDoc->f_UndoStack()->createUndoAction(this, tr("&Undo"));
    m_pUndoAct->setShortcut(QKeySequence::Undo);
    m_pRedoAct = m_pDoc->f_UndoStack()->createRedoAction(this, tr("&Redo"));
    m_pRedoAct->setShortcut(QKeySequence::Redo);
    m_pDeleteAct = f_MakeAction(this, tr("&Delete"), QKeySequence::Delete);
    m_pRenameAct = f_MakeAction(this, tr("Re&name"), QKeySequence(Qt::Key_F2));
    m_pSelectAllAct = f_MakeAction(this, tr("Select &All"), QKeySequence::SelectAll);
    m_pFindAct = f_MakeAction(this, tr("&Find Sprite"), QKeySequence::Find);
    connect(m_pDeleteAct, &QAction::triggered, this, &MainWindow::f_OnDelete);
    connect(m_pSelectAllAct, &QAction::triggered, this, &MainWindow::f_OnSelectAll);

    m_pNewChildAct = f_MakeAction(this, tr("New &Child Sprite…"), QKeySequence(tr("Ctrl+Shift+N")), Id::Child);
    m_pSliceAct = f_MakeAction(this, tr("&Slice into Grid…"), QKeySequence(tr("Ctrl+G")), Id::Grid);
    m_pBakeAct = f_MakeAction(this, tr("&Bake / Unbake"), QKeySequence(), Id::Bake);
    m_pBakeAct->setToolTip(tr("A baked child gets its own copy of the pixels in the atlas"));
    m_pPinAct = f_MakeAction(this, tr("&Pin / Unpin"), QKeySequence(tr("Ctrl+P")), Id::Pin);
    m_pImportRefsAct = f_MakeAction(this, tr("&Import From References…"), QKeySequence(tr("Ctrl+R")), Id::Import);
    m_pImportRefsAct->setToolTip(tr("New or changed art in the reference folders (the variant's folder when a variant is shown)"));
    m_pReplaceImageAct = f_MakeAction(this, tr("Re&place Image…"));
    m_pSaveImageAct = f_MakeAction(this, tr("Save Image &As…"));
    m_pReplaceVariantAct = f_MakeAction(this, tr("Replace &Variant Art…"));
    m_pRemoveVariantAct = f_MakeAction(this, tr("Remove Variant &Art"));
    m_pRemoveVariantAct->setToolTip(tr("The variant shows the base art again for the selected sprites"));
    m_pEditModeAct = f_MakeAction(this, tr("&Edit Sprite"), QKeySequence(tr("E")), Id::EditMode);
    m_pEditModeAct->setCheckable(true);
    m_pEditModeAct->setToolTip(tr("Sprite edit mode: child rects, pivot, 9-slice (E, or double-click a sprite)"));
    connect(m_pNewChildAct, &QAction::triggered, this, &MainWindow::f_OnNewChild);
    connect(m_pSliceAct, &QAction::triggered, this, &MainWindow::f_OnSliceGrid);
    connect(m_pBakeAct, &QAction::triggered, this, &MainWindow::f_OnToggleBake);
    connect(m_pPinAct, &QAction::triggered, this, &MainWindow::f_OnTogglePin);
    connect(m_pImportRefsAct, &QAction::triggered, this, &MainWindow::f_OnImportReferences);
    connect(m_pReplaceImageAct, &QAction::triggered, this, [this] { f_OnReplaceImage(false); });
    connect(m_pReplaceVariantAct, &QAction::triggered, this, [this] { f_OnReplaceImage(true); });
    connect(m_pRemoveVariantAct, &QAction::triggered, this, &MainWindow::f_OnRemoveVariantArt);
    connect(m_pSaveImageAct, &QAction::triggered, this, &MainWindow::f_OnSaveImageAs);
    connect(m_pEditModeAct, &QAction::triggered, this, &MainWindow::f_OnEditModeToggled);

    m_pFitAct = f_MakeAction(this, tr("&Fit"), QKeySequence(tr("F")), Id::Fit);
    m_pZoomInAct = f_MakeAction(this, tr("Zoom &In"), QKeySequence::ZoomIn, Id::ZoomIn);
    m_pZoomOutAct = f_MakeAction(this, tr("Zoom &Out"), QKeySequence::ZoomOut, Id::ZoomOut);
    connect(m_pFitAct, &QAction::triggered, this, [this] { m_pPanel->f_CurrentView()->f_FitToView(); });
    connect(m_pZoomInAct, &QAction::triggered, this, [this] { m_pPanel->f_CurrentView()->f_ZoomIn(); });
    connect(m_pZoomOutAct, &QAction::triggered, this, [this] { m_pPanel->f_CurrentView()->f_ZoomOut(); });
    m_pOutlinesAct = f_MakeAction(this, tr("Show &Outlines"), QKeySequence(tr("O")));
    m_pOutlinesAct->setCheckable(true);
    m_pOutlinesAct->setChecked(true);
    m_pChildrenAct = f_MakeAction(this, tr("Show &Child Sprites"));
    m_pChildrenAct->setCheckable(true);
    m_pChildrenAct->setChecked(true);
    connect(m_pOutlinesAct, &QAction::toggled, m_pPanel->f_AtlasCanvas(), &AtlasCanvas::f_SetShowOutlines);
    connect(m_pChildrenAct, &QAction::toggled, m_pPanel->f_AtlasCanvas(), &AtlasCanvas::f_SetShowChildren);
    m_pDarkAct = f_MakeAction(this, tr("&Dark Theme"));
    m_pLightAct = f_MakeAction(this, tr("&Light Theme"));
    auto* l_pThemes = new QActionGroup(this);
    for (QAction* l_pA : {m_pDarkAct, m_pLightAct}) {
        l_pA->setCheckable(true);
        l_pThemes->addAction(l_pA);
    }
    connect(m_pDarkAct, &QAction::triggered, this, [this] { f_SetDarkTheme(true); });
    connect(m_pLightAct, &QAction::triggered, this, [this] { f_SetDarkTheme(false); });

    m_pHelpCliAct = f_MakeAction(this, tr("&Command Line Help"));
    m_pAboutAct = f_MakeAction(this, tr("&About Atlas Editor"));
    connect(m_pHelpCliAct, &QAction::triggered, this, &MainWindow::f_OnCommandLineHelp);
    connect(m_pAboutAct, &QAction::triggered, this, &MainWindow::f_OnAbout);
}

void MainWindow::f_CreateDocks()
{
    m_pSpriteDock = new SpriteTreeDock(m_pDoc, this);
    m_pPropsDock = new PropertiesDock(m_pDoc, this);
    m_pAnimDock = new AnimationDock(m_pDoc, this);
    m_pProblemsDock = new ProblemsDock(m_pDoc, this);
    m_pHistoryDock = f_CreateHistoryDock(m_pDoc->f_UndoStack(), this);
    m_pBackup = new AutoBackup(m_pDoc->f_UndoStack(), QStringLiteral(".atlasproj"), [this] { return m_pDoc->f_FilePath(); },
                              [this](const QString& l_oP, QString* l_pE) { return m_pDoc->f_WriteBackup(l_oP, l_pE); }, this);
    connect(m_pBackup, &AutoBackup::f_Message, this, [this](const QString& l_oT) { statusBar()->showMessage(l_oT, 5000); });
    addDockWidget(Qt::LeftDockWidgetArea, m_pSpriteDock);
    addDockWidget(Qt::RightDockWidgetArea, m_pPropsDock);
    tabifyDockWidget(m_pPropsDock, m_pHistoryDock);
    m_pPropsDock->raise();
    addDockWidget(Qt::BottomDockWidgetArea, m_pProblemsDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_pAnimDock);
    tabifyDockWidget(m_pProblemsDock, m_pAnimDock);
    m_pProblemsDock->raise();
    resizeDocks({m_pSpriteDock, m_pPropsDock}, {270, 310}, Qt::Horizontal);
    resizeDocks({m_pProblemsDock}, {190}, Qt::Vertical);
    connect(m_pRenameAct, &QAction::triggered, m_pSpriteDock, &SpriteTreeDock::f_BeginRename);
    connect(m_pFindAct, &QAction::triggered, this, [this] {
        m_pSpriteDock->show();
        m_pSpriteDock->raise();
        m_pSpriteDock->f_FocusFilter();
    });
}

void MainWindow::f_CreateMenus()
{
    QMenu* l_pFile = menuBar()->addMenu(tr("&File"));
    l_pFile->addAction(m_pNewAct);
    l_pFile->addAction(m_pOpenAct);
    m_pRecentMenu = l_pFile->addMenu(tr("Open &Recent"));
    connect(m_pRecentMenu, &QMenu::aboutToShow, this, &MainWindow::f_RebuildRecentMenu);
    l_pFile->addAction(m_pSaveAct);
    l_pFile->addAction(m_pSaveAsAct);
    l_pFile->addSeparator();
    l_pFile->addAction(m_pImportAct);
    l_pFile->addAction(m_pAddImagesAct);
    l_pFile->addAction(m_pAddFolderAct);
    l_pFile->addSeparator();
    l_pFile->addAction(m_pExportAct);
    l_pFile->addAction(m_pExportAllAct);
    l_pFile->addAction(m_pExtractAct);
    l_pFile->addSeparator();
    m_pBackup->f_AddMenu(l_pFile, this);
    l_pFile->addSeparator();
    l_pFile->addAction(m_pQuitAct);

    QMenu* l_pEdit = menuBar()->addMenu(tr("&Edit"));
    l_pEdit->addAction(m_pUndoAct);
    l_pEdit->addAction(m_pRedoAct);
    l_pEdit->addSeparator();
    l_pEdit->addAction(m_pDeleteAct);
    l_pEdit->addAction(m_pRenameAct);
    l_pEdit->addAction(m_pSelectAllAct);
    l_pEdit->addAction(m_pFindAct);

    m_pSpriteMenu = menuBar()->addMenu(tr("&Sprite"));
    m_pSpriteMenu->addAction(m_pEditModeAct);
    m_pSpriteMenu->addSeparator();
    m_pSpriteMenu->addAction(m_pNewChildAct);
    m_pSpriteMenu->addAction(m_pSliceAct);
    m_pSpriteMenu->addAction(m_pBakeAct);
    m_pSpriteMenu->addAction(m_pPinAct);
    m_pSpriteMenu->addSeparator();
    m_pSpriteMenu->addAction(m_pReplaceImageAct);
    m_pSpriteMenu->addAction(m_pSaveImageAct);
    m_pSpriteMenu->addAction(m_pReplaceVariantAct);
    m_pSpriteMenu->addAction(m_pRemoveVariantAct);
    m_pSpriteMenu->addAction(m_pImportRefsAct);
    m_pSpriteMenu->addSeparator();
    m_pSpriteMenu->addAction(m_pRenameAct);
    m_pSpriteMenu->addAction(m_pDeleteAct);

    m_pViewMenu = menuBar()->addMenu(tr("&View"));
    {   // the viewport's renderer: the game's (bgfx, the same pixels as toms_game) or QPainter
        QAction* l_pA = m_pViewMenu->addAction(tr("Preview with the Game Renderer (after a restart)"));
        l_pA->setCheckable(true);
        l_pA->setChecked(f_Settings().value(QStringLiteral("preview/gameRenderer"), true).toBool());
        l_pA->setToolTip(tr("Draw the pages and sprites with toms_game's renderer (bgfx): exactly the game's pixels. Off: QPainter."));
        connect(l_pA, &QAction::toggled, this, [this](bool l_bOn) {
            f_Settings().setValue(QStringLiteral("preview/gameRenderer"), l_bOn);
            statusBar()->showMessage(tr("The renderer changes the next time the editor starts."), 8000);
        });
        m_pViewMenu->addSeparator();
    }
    m_pViewMenu->addAction(m_pFitAct);
    m_pViewMenu->addAction(m_pZoomInAct);
    m_pViewMenu->addAction(m_pZoomOutAct);
    CanvasView::f_AddCoordinatesAction(m_pViewMenu);
    m_pViewMenu->addSeparator();
    m_pViewMenu->addAction(m_pOutlinesAct);
    m_pViewMenu->addAction(m_pChildrenAct);
    m_pViewMenu->addSeparator();
    m_pViewMenu->addAction(m_pDarkAct);
    m_pViewMenu->addAction(m_pLightAct);
    m_pViewMenu->addSeparator();
    for (QDockWidget* l_pD : std::initializer_list<QDockWidget*>{m_pSpriteDock, m_pPropsDock, m_pAnimDock, m_pProblemsDock, m_pHistoryDock})
        m_pViewMenu->addAction(l_pD->toggleViewAction());

    QMenu* l_pHelp = menuBar()->addMenu(tr("&Help"));
    l_pHelp->addAction(m_pHelpCliAct);
    l_pHelp->addAction(m_pAboutAct);
}

void MainWindow::f_CreateToolBar()
{
    QToolBar* l_pTb = addToolBar(tr("Main"));
    l_pTb->setObjectName(QStringLiteral("MainToolBar"));
    l_pTb->setIconSize(QSize(20, 20));
    l_pTb->setMovable(false);
    l_pTb->addAction(m_pNewAct);
    l_pTb->addAction(m_pOpenAct);
    l_pTb->addAction(m_pSaveAct);
    l_pTb->addSeparator();
    l_pTb->addAction(m_pImportAct);
    l_pTb->addAction(m_pAddImagesAct);
    l_pTb->addAction(m_pExportAct);
    l_pTb->addSeparator();
    auto* l_pVariantLabel = new QLabel(tr(" Variant "), l_pTb);
    l_pVariantLabel->setForegroundRole(QPalette::PlaceholderText);
    l_pTb->addWidget(l_pVariantLabel);
    m_pVariantCombo = new QComboBox(l_pTb);
    m_pVariantCombo->setMinimumContentsLength(10);
    m_pVariantCombo->setToolTip(tr("Which art the canvas shows (and Import From References / Export Copy work on)"));
    l_pTb->addWidget(m_pVariantCombo);
    connect(m_pVariantCombo, &QComboBox::activated, this, [this] { m_pDoc->f_SetVariant(m_pVariantCombo->currentData().toString()); });
    l_pTb->addSeparator();
    l_pTb->addAction(m_pEditModeAct);
    l_pTb->addAction(m_pNewChildAct);
    l_pTb->addAction(m_pSliceAct);
    l_pTb->addSeparator();
    l_pTb->addAction(m_pZoomOutAct);
    m_pZoomButton = new QToolButton(l_pTb);
    m_pZoomButton->setText(QStringLiteral("100%"));
    m_pZoomButton->setToolTip(tr("Zoom"));
    m_pZoomButton->setPopupMode(QToolButton::InstantPopup);
    m_pZoomButton->setMinimumWidth(56);
    auto* l_pZoomMenu = new QMenu(m_pZoomButton);
    for (int l_iPct : {25, 50, 100, 200, 400, 800, 1600})
        l_pZoomMenu->addAction(QStringLiteral("%1%").arg(l_iPct), this, [this, l_iPct] { m_pPanel->f_CurrentView()->f_SetZoom(l_iPct / 100.0); });
    l_pZoomMenu->addSeparator();
    l_pZoomMenu->addAction(m_pFitAct);
    m_pZoomButton->setMenu(l_pZoomMenu);
    l_pTb->addWidget(m_pZoomButton);
    l_pTb->addAction(m_pZoomInAct);
    l_pTb->addAction(m_pFitAct);
}

void MainWindow::f_CreateStatusBar()
{
    auto l_fnLabel = [this](const QString& l_oTip) {
        auto* l_pL = new QLabel(this);
        l_pL->setToolTip(l_oTip);
        l_pL->setContentsMargins(6, 0, 6, 0);
        statusBar()->addPermanentWidget(l_pL);
        return l_pL;
    };
    m_pStatusStore = l_fnLabel(tr("The images live in the project's packed atlas"));
    m_pStatusBuild = l_fnLabel(tr("Last build"));
    m_pStatusPages = l_fnLabel(tr("Pages and the size of the shown page"));
    m_pStatusOccupancy = l_fnLabel(tr("Share of the page area covered by sprite pixels"));
    m_pStatusSprites = l_fnLabel(tr("Sprites in the atlas"));
    m_pStatusProblems = l_fnLabel(tr("Warnings and errors (see Problems)"));
}

// ---- settings -----------------------------------------------------------------------------------

void MainWindow::f_ReadSettings()
{
    QSettings l_oS = f_Settings();
    const bool l_bDark = l_oS.value(QStringLiteral("theme"), QStringLiteral("dark")).toString() != QLatin1String("light");
    (l_bDark ? m_pDarkAct : m_pLightAct)->setChecked(true);
    f_SetDarkTheme(l_bDark);
    m_pOutlinesAct->setChecked(l_oS.value(QStringLiteral("view/outlines"), true).toBool());
    m_pChildrenAct->setChecked(l_oS.value(QStringLiteral("view/children"), true).toBool());
    if (!restoreGeometry(l_oS.value(QStringLiteral("geometry")).toByteArray())) resize(1400, 860);
    restoreState(l_oS.value(QStringLiteral("windowState")).toByteArray(), g_iStateVersion);
}

void MainWindow::f_WriteSettings() const
{
    QSettings l_oS = f_Settings();
    l_oS.setValue(QStringLiteral("geometry"), saveGeometry());
    l_oS.setValue(QStringLiteral("windowState"), saveState(g_iStateVersion));
    l_oS.setValue(QStringLiteral("theme"), Theme::f_Mode() == Theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    l_oS.setValue(QStringLiteral("view/outlines"), m_pOutlinesAct->isChecked());
    l_oS.setValue(QStringLiteral("view/children"), m_pChildrenAct->isChecked());
}

QString MainWindow::f_LastDir() const
{
    if (!m_pDoc->f_FilePath().isEmpty()) return QFileInfo(m_pDoc->f_FilePath()).absolutePath();
    return f_Settings().value(QStringLiteral("lastDir"), QDir::homePath()).toString();
}

void MainWindow::f_RememberDir(const QString& l_oPathOrDir)
{
    const QFileInfo l_oFi(l_oPathOrDir);
    f_Settings().setValue(QStringLiteral("lastDir"), l_oFi.isDir() ? l_oFi.absoluteFilePath() : l_oFi.absolutePath());
}

void MainWindow::f_AddRecentFile(const QString& l_oPath)
{
    QSettings l_oS = f_Settings();
    QStringList l_oRecent = l_oS.value(QStringLiteral("recentFiles")).toStringList();
    const QString l_oAbs = QFileInfo(l_oPath).absoluteFilePath();
    l_oRecent.removeAll(l_oAbs);
    l_oRecent.prepend(l_oAbs);
    while (l_oRecent.size() > g_iMaxRecent) l_oRecent.removeLast();
    l_oS.setValue(QStringLiteral("recentFiles"), l_oRecent);
    f_RememberDir(l_oAbs);
}

void MainWindow::f_RebuildRecentMenu()
{
    m_pRecentMenu->clear();
    const QStringList l_oRecent = f_Settings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& l_oPath : l_oRecent) {
        QAction* l_pA = m_pRecentMenu->addAction(QDir::toNativeSeparators(l_oPath));
        l_pA->setEnabled(QFileInfo::exists(l_oPath));
        connect(l_pA, &QAction::triggered, this, [this, l_oPath] {
            if (f_MaybeSave()) f_OpenProject(l_oPath);
        });
    }
    if (l_oRecent.isEmpty()) m_pRecentMenu->addAction(tr("(none)"))->setEnabled(false);
    else {
        m_pRecentMenu->addSeparator();
        m_pRecentMenu->addAction(tr("Clear List"), this, [] { f_Settings().remove(QStringLiteral("recentFiles")); });
    }
}

// ---- files --------------------------------------------------------------------------------------

bool MainWindow::f_OpenProject(const QString& l_oPath)
{
    QString l_oErr;
    if (!m_pDoc->f_Open(l_oPath, &l_oErr)) {
        QMessageBox::warning(this, tr("Open Project"), tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        return false;
    }
    f_AddRecentFile(l_oPath);
    return true;
}

bool MainWindow::f_MaybeSave()
{
    if (!m_pDoc->f_IsDirty()) return true;
    const auto l_eR = QMessageBox::warning(this, tr("Unsaved Changes"),
                                        tr("%1 has unsaved changes. Save them?").arg(m_pDoc->f_DisplayName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (l_eR == QMessageBox::Save) return f_Save();
    return l_eR == QMessageBox::Discard;
}

bool MainWindow::f_Save()
{
    if (m_pDoc->f_FilePath().isEmpty()) return f_SaveAs();
    return f_SaveTo(m_pDoc->f_FilePath());
}

bool MainWindow::f_SaveAs()
{
    const QString l_oStart = m_pDoc->f_FilePath().isEmpty() ? QDir(f_LastDir()).filePath(f_Qs(m_pDoc->f_Project().m_strName) + QStringLiteral(".atlasproj"))
                                                      : m_pDoc->f_FilePath();
    QString l_oPath = QFileDialog::getSaveFileName(this, tr("Save Project As"), l_oStart, tr("Atlas projects (*.atlasproj)"));
    if (l_oPath.isEmpty()) return false;
    if (QFileInfo(l_oPath).suffix().isEmpty()) l_oPath += QStringLiteral(".atlasproj");
    return f_SaveTo(l_oPath);
}

bool MainWindow::f_SaveTo(const QString& l_oPath)
{
    statusBar()->showMessage(tr("Saving…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    SaveReport l_oReport;
    QString l_oErr;
    const bool l_bOk = m_pDoc->f_Save(l_oPath, &l_oReport, &l_oErr);
    QApplication::restoreOverrideCursor();
    for (const atlas::Diagnostic& l_oD : l_oReport.m_vDiagnostics) m_pProblemsDock->f_AppendLog(f_Qs(atlas::f_FormatDiagnostic(l_oD)));
    if (!l_bOk) {
        statusBar()->showMessage(tr("Save failed"), 5000);
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n\n%2\n\nNothing was written.")
                                                   .arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        return false;
    }
    for (const QString& l_oF : l_oReport.m_oWritten) m_pProblemsDock->f_AppendLog(tr("  wrote %1").arg(l_oF));
    if (l_oReport.m_oWritten.size() <= 1) m_pProblemsDock->f_AppendLog(tr("  (the packed atlas and output files were up to date)"));
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(l_oPath)), 5000);
    f_AddRecentFile(l_oPath);
    return true;
}

void MainWindow::f_OnNew()
{
    if (f_MaybeSave()) m_pDoc->f_NewProject();
}

void MainWindow::f_OnOpen()
{
    if (!f_MaybeSave()) return;
    const QString l_oPath = QFileDialog::getOpenFileName(this, tr("Open Project"), f_LastDir(), tr("Atlas projects (*.atlasproj);;All files (*)"));
    if (!l_oPath.isEmpty()) f_OpenProject(l_oPath);
}

void MainWindow::f_OnImportAtlas()
{
    if (!f_MaybeSave()) return;
    const QString l_oAtlasFile = QFileDialog::getOpenFileName(this, tr("Import Atlas"), f_LastDir(),
                                                           tr("Atlas files (*.pi *.atlas *.json *.plist);;All files (*)"));
    if (l_oAtlasFile.isEmpty()) return;
    const QFileInfo l_oFi(l_oAtlasFile);
    QString l_oProjPath = QFileDialog::getSaveFileName(this, tr("New Project for the Imported Atlas"),
                                                    l_oFi.absoluteDir().filePath(l_oFi.completeBaseName() + QStringLiteral(".atlasproj")),
                                                    tr("Atlas projects (*.atlasproj)"));
    if (l_oProjPath.isEmpty()) return;
    if (QFileInfo(l_oProjPath).suffix().isEmpty()) l_oProjPath += QStringLiteral(".atlasproj");

    QApplication::setOverrideCursor(Qt::WaitCursor);
    atlas::BuildResult l_oIn;
    atlas::Project l_oP;
    std::string l_strErr;
    // Every sprite is cut out of the atlas into memory: an embedded project, nothing written yet.
    const bool l_bOk = atlas::f_ImportAtlasFile(f_U8(l_oAtlasFile), l_oIn, &l_strErr) &&
                    atlas::f_ImportToProject(l_oIn, f_U8(QDir::fromNativeSeparators(l_oProjPath)), std::string(), l_oP, &l_strErr);
    QApplication::restoreOverrideCursor();
    if (!l_bOk) {
        QMessageBox::warning(this, tr("Import Atlas"), tr("Could not import %1:\n%2").arg(QDir::toNativeSeparators(l_oAtlasFile), f_Qs(l_strErr)));
        return;
    }
    // Keep writing the format it came in, like `atlaspack import`.
    const QString l_oExt = l_oFi.suffix().toLower();
    l_oP.m_oOutput.m_vFormats = {l_oExt == QLatin1String("pi") ? "pi" : l_oExt == QLatin1String("json") ? "tp-json" : "atlas"};
    l_oP.m_oOutput.m_strDir = ".";
    m_pDoc->f_Adopt(l_oP);
    f_RememberDir(l_oProjPath);
    m_pProblemsDock->f_AppendLog(tr("Imported %1: %2 sprite(s), %3 image(s). Save writes %4 and the packed atlas %5.")
                                  .arg(QDir::toNativeSeparators(l_oAtlasFile)).arg(l_oIn.m_vRegions.size()).arg(l_oP.m_mapImages.size())
                                  .arg(QDir::toNativeSeparators(l_oProjPath), QDir::toNativeSeparators(m_pDoc->f_StoreFile())));
    if (QFileInfo(m_pDoc->f_StoreFile()).absoluteFilePath().compare(l_oFi.absoluteFilePath(), Qt::CaseInsensitive) == 0)
        m_pProblemsDock->f_AppendLog(tr("Note: saving rewrites %1 with the re-packed atlas.").arg(QDir::toNativeSeparators(l_oAtlasFile)));
}

void MainWindow::f_OnAddReferenceFolder()
{
    const QString l_oDir = QFileDialog::getExistingDirectory(this, tr("Add Reference Folder"), f_LastDir());
    if (l_oDir.isEmpty()) return;
    f_RememberDir(l_oDir);
    m_pDoc->f_AddReference(l_oDir);
}

void MainWindow::f_OnAddImages()
{
    const QStringList l_oFiles = QFileDialog::getOpenFileNames(this, tr("Add Images"), f_LastDir(), tr("PNG images (*.png)"));
    if (l_oFiles.isEmpty()) return;
    f_RememberDir(l_oFiles.first());
    f_AddImagePaths(l_oFiles);
}

int MainWindow::f_AddImagePaths(const QStringList& l_oPaths)
{
    const QList<ImageFile> l_oFound = AtlasDocument::f_CollectPngs(l_oPaths);
    if (l_oFound.isEmpty()) {
        statusBar()->showMessage(tr("No PNG files there"), 4000);
        return 0;
    }
    // Names already in the project: ask once each, or once for all of them.
    int l_iConflicts = 0;
    for (const ImageFile& l_oF : l_oFound) l_iConflicts += m_pDoc->f_IsImage(l_oF.m_oName);
    QList<ImageFile> l_oTake;
    int l_iDecided = -1;   // -1 ask, 0 skip all, 1 replace all
    int l_iSeen = 0;
    for (const ImageFile& l_oF : l_oFound) {
        if (!m_pDoc->f_IsImage(l_oF.m_oName)) {
            l_oTake << l_oF;
            continue;
        }
        l_iSeen++;
        int l_iChoice = l_iDecided;
        if (l_iChoice < 0) {
            QMessageBox l_oAsk(QMessageBox::Question, tr("Add Images"),
                            tr("The project already has an image called '%1'.\nReplace it with %2?")
                                .arg(l_oF.m_oName, QDir::toNativeSeparators(l_oF.m_oPath)),
                            QMessageBox::NoButton, this);
            QPushButton* l_pReplace = l_oAsk.addButton(tr("Replace"), QMessageBox::AcceptRole);
            QPushButton* l_pSkip = l_oAsk.addButton(tr("Skip"), QMessageBox::RejectRole);
            QPushButton* l_pCancel = l_oAsk.addButton(QMessageBox::Cancel);
            l_oAsk.setDefaultButton(l_pSkip);
            QCheckBox* l_pAll = nullptr;
            if (l_iConflicts - l_iSeen > 0) {
                l_pAll = new QCheckBox(tr("Do the same for the other %n existing image(s)", nullptr, l_iConflicts - l_iSeen), &l_oAsk);
                l_oAsk.setCheckBox(l_pAll);
            }
            l_oAsk.exec();
            if (l_oAsk.clickedButton() == l_pCancel) return 0;
            l_iChoice = l_oAsk.clickedButton() == l_pReplace ? 1 : 0;
            if (l_pAll && l_pAll->isChecked()) l_iDecided = l_iChoice;
        }
        if (l_iChoice == 1) l_oTake << l_oF;
    }
    if (l_oTake.isEmpty()) {
        statusBar()->showMessage(tr("Nothing added"), 4000);
        return 0;
    }
    QStringList l_oErrors;
    const int l_iN = m_pDoc->f_SetImages(l_oTake, QString(), tr("Add %n image(s)", nullptr, int(l_oTake.size())), &l_oErrors);
    for (const QString& l_oE : l_oErrors) m_pProblemsDock->f_AppendLog(l_oE);
    m_pProblemsDock->f_AppendLog(tr("Added %n image(s)", nullptr, l_iN) + (l_oFound.size() > l_oTake.size() ? tr(" (%1 skipped)").arg(l_oFound.size() - l_oTake.size()) : QString()));
    if (!l_oErrors.isEmpty())
        QMessageBox::warning(this, tr("Add Images"), tr("%n file(s) could not be added:", nullptr, int(l_oErrors.size())) + QStringLiteral("\n") +
                                                         l_oErrors.mid(0, 10).join(QLatin1Char('\n')));
    return l_iN;
}

void MainWindow::f_OnExtractImages()
{
    const QString l_oDir = QFileDialog::getExistingDirectory(this, tr("Extract Images To"), f_LastDir());
    if (l_oDir.isEmpty()) return;
    QMessageBox l_oAsk(QMessageBox::Question, tr("Extract Images"),
                    tr("Write every sprite%1 as its own PNG to\n%2 ?\n\nFiles of the same name are overwritten.")
                        .arg(m_pDoc->f_Variant().isEmpty() ? QString() : tr(" (%1 art)").arg(m_pDoc->f_Variant()), QDir::toNativeSeparators(l_oDir)),
                    QMessageBox::Cancel, this);
    QPushButton* l_pGo = l_oAsk.addButton(tr("Extract"), QMessageBox::AcceptRole);
    auto* l_pChildren = new QCheckBox(tr("Include child sprites"), &l_oAsk);
    l_oAsk.setCheckBox(l_pChildren);
    l_oAsk.setDefaultButton(l_pGo);
    l_oAsk.exec();
    if (l_oAsk.clickedButton() != l_pGo) return;
    f_RememberDir(l_oDir);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int l_iCount = 0;
    QString l_oErr;
    const bool l_bOk = m_pDoc->f_ExtractImages(l_oDir, l_pChildren->isChecked(), &l_iCount, &l_oErr);
    QApplication::restoreOverrideCursor();
    if (!l_bOk) {
        QMessageBox::warning(this, tr("Extract Images"), l_oErr);
        return;
    }
    const QString l_oMsg = tr("Extracted %n image(s) to %1", nullptr, l_iCount).arg(QDir::toNativeSeparators(l_oDir));
    m_pProblemsDock->f_AppendLog(l_oMsg);
    statusBar()->showMessage(l_oMsg, 5000);
}

void MainWindow::f_OnExport(bool l_bAllVariants)
{
    if (m_pDoc->f_IsExporting()) return;
    QSettings l_oS = f_Settings();
    const QString l_oStart = l_oS.value(QStringLiteral("exportDir"), f_LastDir()).toString();
    const QString l_oDir = QFileDialog::getExistingDirectory(this, l_bAllVariants ? tr("Export All Variants To Folder") : tr("Export Copy To Folder"), l_oStart);
    if (l_oDir.isEmpty()) return;
    l_oS.setValue(QStringLiteral("exportDir"), l_oDir);
    statusBar()->showMessage(tr("Exporting…"));
    m_pDoc->f_ExportCopy(l_oDir, l_bAllVariants);
}

void MainWindow::f_OnExportFinished(bool l_bOk, const QString& l_oSummary)
{
    for (const QString& l_oLine : l_oSummary.split(QLatin1Char('\n'))) m_pProblemsDock->f_AppendLog(l_oLine);
    statusBar()->showMessage(l_bOk ? tr("Exported") : tr("Export failed"), 5000);
    if (!l_bOk) QMessageBox::warning(this, tr("Export"), l_oSummary + QStringLiteral("\n\n") + tr("See Problems for the errors."));
}

// ---- edit / sprite ------------------------------------------------------------------------------

void MainWindow::f_OnDelete()
{
    const QStringList l_oSel = m_pDoc->f_Selection();
    if (l_oSel.isEmpty()) return;
    // Deleting a sprite takes its image, its variant art and its child sprites along.
    const QStringList l_oAll = m_pDoc->f_DeletionClosure(l_oSel);
    QStringList l_oExtra;
    for (const QString& l_oN : l_oAll)
        if (!l_oSel.contains(l_oN)) l_oExtra << l_oN;
    auto l_fnList = [](const QStringList& l_oNames) {
        return l_oNames.mid(0, 12).join(QStringLiteral(", ")) + (l_oNames.size() > 12 ? QStringLiteral(", …") : QString());
    };
    QString l_oText = tr("Delete %n sprite(s)?", nullptr, int(l_oSel.size())) + QStringLiteral("\n") + l_fnList(l_oSel);
    if (!l_oExtra.isEmpty()) l_oText += QStringLiteral("\n\n") + tr("Also deletes %n child sprite(s):", nullptr, int(l_oExtra.size())) + QStringLiteral("\n") + l_fnList(l_oExtra);
    l_oText += QStringLiteral("\n\n") + tr("Images and variant art go with them (Undo brings them back).");
    if (QMessageBox::question(this, tr("Delete"), l_oText) != QMessageBox::Yes) return;
    m_pDoc->f_DeleteSprites(l_oSel);
}

void MainWindow::f_OnSelectAll()
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    if (!l_spSnap) return;
    QStringList l_oAll;
    for (const SpriteInfo& l_oS : l_spSnap->m_vSprites) l_oAll << l_oS.m_oName;
    m_pDoc->f_SetSelection(l_oAll);
}

void MainWindow::f_OnNewChild()
{
    const QString l_oParent = m_pDoc->f_CurrentSprite();
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const atlas::Region* l_pR = l_spSnap ? l_spSnap->f_Region(l_oParent) : nullptr;
    if (!l_pR) {
        statusBar()->showMessage(tr("Select a sprite that is in the atlas first"), 4000);
        return;
    }
    bool l_bOk = false;
    const QString l_oName = QInputDialog::getText(this, tr("New Child Sprite"),
                                               tr("Name (the rect starts as the whole of %1; adjust it in edit mode):").arg(l_oParent),
                                               QLineEdit::Normal, m_pDoc->f_UniqueName(l_oParent + QStringLiteral("_0")), &l_bOk).trimmed();
    if (!l_bOk || l_oName.isEmpty()) return;
    if (m_pDoc->f_SpriteExists(l_oName)) {
        QMessageBox::warning(this, tr("New Child Sprite"), tr("There already is a sprite called '%1'.").arg(l_oName));
        return;
    }
    m_pPanel->f_EnterEditMode(l_oParent);
    m_pDoc->f_AddChildren(l_oParent, {{l_oName, atlas::IRect{0, 0, l_pR->m_iOrigW, l_pR->m_iOrigH}}}, tr("New child %1").arg(l_oName));
}

void MainWindow::f_OnSliceGrid()
{
    const QString l_oParent = m_pDoc->f_CurrentSprite();
    const QImage l_oImg = m_pDoc->f_SpriteImage(l_oParent);
    if (l_oImg.isNull()) {
        statusBar()->showMessage(tr("Select a sprite that is in the atlas first"), 4000);
        return;
    }
    GridSliceDialog l_oDlg(l_oImg, l_oParent, [this](const QString& l_oN) { return m_pDoc->f_SpriteExists(l_oN); }, this);
    if (l_oDlg.exec() != QDialog::Accepted) return;
    const auto l_vCells = l_oDlg.f_Cells();
    m_pDoc->f_AddChildren(l_oParent, l_vCells, tr("Slice %1 into %n sprite(s)", nullptr, int(l_vCells.size())).arg(l_oParent));
}

void MainWindow::f_OnToggleBake()
{
    QStringList l_oChildren;
    bool l_bAllBaked = true;
    for (const QString& l_oN : m_pDoc->f_Selection()) {
        const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(l_oN);
        if (!l_oD.f_IsChild()) continue;
        l_oChildren << l_oN;
        l_bAllBaked = l_bAllBaked && l_oD.m_bBake;
    }
    m_pDoc->f_EditSprites(l_oChildren, l_bAllBaked ? tr("Unbake") : tr("Bake"), [&](atlas::SpriteDef& l_oD) { l_oD.m_bBake = !l_bAllBaked; });
}

void MainWindow::f_OnTogglePin()
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    QStringList l_oNames;
    bool l_bAllPinned = true;
    for (const QString& l_oN : m_pDoc->f_Selection()) {
        const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(l_oN);
        if (l_oD.f_IsChild() && !l_oD.m_bBake) continue;   // a plain child lives inside its parent
        l_oNames << l_oN;
        l_bAllPinned = l_bAllPinned && l_oD.m_bPinned;
    }
    m_pDoc->f_EditSprites(l_oNames, l_bAllPinned ? tr("Unpin") : tr("Pin"), [&](atlas::SpriteDef& l_oD) {
        if (!l_bAllPinned && !l_oD.m_bPinned && l_spSnap)
            if (const atlas::Region* l_pR = l_spSnap->f_Region(f_Qs(l_oD.m_strName)); l_pR && l_pR->m_strAliasOf.empty()) {
                l_oD.m_iPinPage = l_pR->m_iPage;   // pinned where it is now
                l_oD.m_iPinX = l_pR->m_oFrame.m_iX;
                l_oD.m_iPinY = l_pR->m_oFrame.m_iY;
            }
        l_oD.m_bPinned = !l_bAllPinned;
    });
}

void MainWindow::f_OnReplaceImage(bool l_bVariantArt)
{
    const QString l_oName = m_pDoc->f_CurrentSprite();
    const QString l_oVariant = l_bVariantArt ? m_pDoc->f_Variant() : QString();
    if (l_oName.isEmpty() || !m_pDoc->f_IsImage(l_oName) || (l_bVariantArt && l_oVariant.isEmpty())) return;
    const QString l_oFile = QFileDialog::getOpenFileName(this, l_bVariantArt ? tr("%1 Art for %2").arg(l_oVariant, l_oName) : tr("Replace %1").arg(l_oName),
                                                      f_LastDir(), tr("PNG images (*.png)"));
    if (l_oFile.isEmpty()) return;
    f_RememberDir(l_oFile);
    QStringList l_oErrors;
    const QString l_oText = l_bVariantArt ? tr("Replace %1 art of %2").arg(l_oVariant, l_oName) : tr("Replace %1").arg(l_oName);
    if (!m_pDoc->f_SetImages({{l_oName, QDir::fromNativeSeparators(l_oFile)}}, l_oVariant, l_oText, &l_oErrors))
        QMessageBox::warning(this, l_oText, l_oErrors.join(QLatin1Char('\n')));
}

void MainWindow::f_OnRemoveVariantArt()
{
    QStringList l_oNames;
    for (const QString& l_oN : m_pDoc->f_Selection())
        if (m_pDoc->f_HasVariantArt(l_oN, m_pDoc->f_Variant())) l_oNames << l_oN;
    m_pDoc->f_RemoveVariantArt(l_oNames, m_pDoc->f_Variant());
}

void MainWindow::f_OnSaveImageAs()
{
    const QString l_oName = m_pDoc->f_CurrentSprite();
    if (l_oName.isEmpty()) return;
    const QString l_oStart = QDir(f_LastDir()).filePath(l_oName.section(QLatin1Char('/'), -1) + QStringLiteral(".png"));
    QString l_oFile = QFileDialog::getSaveFileName(this, tr("Save %1 As").arg(l_oName), l_oStart, tr("PNG images (*.png)"));
    if (l_oFile.isEmpty()) return;
    if (QFileInfo(l_oFile).suffix().isEmpty()) l_oFile += QStringLiteral(".png");
    f_RememberDir(l_oFile);
    QString l_oErr;
    if (!m_pDoc->f_SaveSpriteImage(l_oName, l_oFile, &l_oErr)) {
        QMessageBox::warning(this, tr("Save Image"), l_oErr);
        return;
    }
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(l_oFile)), 5000);
}

void MainWindow::f_OnImportReferences()
{
    ReferencesDialog l_oDlg(m_pDoc, this);
    if (l_oDlg.exec() != QDialog::Accepted) return;
    const QList<ImageFile> l_oFiles = l_oDlg.f_Chosen();
    if (l_oFiles.isEmpty()) return;
    QStringList l_oErrors;
    const QString l_oText = l_oDlg.f_Variant().isEmpty() ? tr("Import %n image(s) from references", nullptr, int(l_oFiles.size()))
                                                  : tr("Import %n %1 image(s)", nullptr, int(l_oFiles.size())).arg(l_oDlg.f_Variant());
    const int l_iN = m_pDoc->f_SetImages(l_oFiles, l_oDlg.f_Variant(), l_oText, &l_oErrors);
    for (const QString& l_oE : l_oErrors) m_pProblemsDock->f_AppendLog(l_oE);
    m_pProblemsDock->f_AppendLog(tr("Imported %n image(s) from the reference folders", nullptr, l_iN) +
                              (l_oDlg.f_Variant().isEmpty() ? QString() : tr(" as %1 art").arg(l_oDlg.f_Variant())));
    if (!l_oErrors.isEmpty())
        QMessageBox::warning(this, tr("Import From References"), l_oErrors.mid(0, 10).join(QLatin1Char('\n')));
}

void MainWindow::f_OnEditModeToggled(bool l_bOn)
{
    if (!l_bOn) {
        m_pPanel->f_ExitEditMode();
        return;
    }
    const QString l_oTarget = m_pDoc->f_CurrentSprite();
    if (l_oTarget.isEmpty() || !m_pDoc->f_Snapshot() || !m_pDoc->f_Snapshot()->f_Region(l_oTarget)) {
        m_pEditModeAct->setChecked(false);
        statusBar()->showMessage(tr("Select a sprite to edit (or double-click one)"), 4000);
        return;
    }
    m_pPanel->f_EnterEditMode(l_oTarget);
}

// ---- view / help --------------------------------------------------------------------------------

void MainWindow::f_SetDarkTheme(bool l_bDark)
{
    Theme::f_Apply(l_bDark ? Theme::Mode::Dark : Theme::Mode::Light);
    (l_bDark ? m_pDarkAct : m_pLightAct)->setChecked(true);
    // Item views cache their thumbnails' backgrounds via the palette; a repaint picks it up.
    for (QWidget* l_pW : QApplication::allWidgets()) l_pW->update();
}

void MainWindow::f_OnCommandLineHelp()
{
    // The usage text lives in the command line code: ask our own headless mode for it, so this
    // dialog can never drift from what `atlaspack --help` prints.
    auto* l_pProc = new QProcess(this);
    connect(l_pProc, &QProcess::finished, this, [this, l_pProc] {
        QString l_oText = QString::fromUtf8(l_pProc->readAllStandardOutput());
        l_pProc->deleteLater();
        QDialog l_oDlg(this);
        l_oDlg.setWindowTitle(tr("Command Line"));
        auto* l_pView = new QPlainTextEdit(&l_oDlg);
        l_pView->setReadOnly(true);
        l_pView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        l_pView->setLineWrapMode(QPlainTextEdit::NoWrap);
        l_pView->setPlainText(tr("atlas_editor --headless <command> ...   runs a command without opening a window\n"
                              "atlas_editor <project.atlasproj>       opens the project\n\n")
                           + l_oText.replace(QLatin1String("atlaspack "), QLatin1String("atlas_editor --headless ")));
        auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Close, &l_oDlg);
        connect(l_pButtons, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
        auto* l_pL = new QVBoxLayout(&l_oDlg);
        l_pL->addWidget(l_pView);
        l_pL->addWidget(l_pButtons);
        l_oDlg.resize(900, 360);
        l_oDlg.exec();
    });
    l_pProc->start(QCoreApplication::applicationFilePath(), {QStringLiteral("--headless"), QStringLiteral("--help")});
}

void MainWindow::f_OnAbout()
{
    QMessageBox::about(this, tr("About Atlas Editor"),
                       tr("<h3>TOMS Atlas Editor</h3>"
                          "<p>Packs sprites into texture atlases. The <i>.atlasproj</i> project and the packed atlas Save "
                          "writes next to it hold everything, images included; folders of PNGs are only references to "
                          "import new art from.</p>"
                          "<p>Formats: %1</p><p>Built with Qt %2.</p>")
                           .arg(f_Qs([] {
                               std::string l_strAll;
                               for (const std::string& l_strF : atlas::f_ExportFormats()) l_strAll += (l_strAll.empty() ? "" : ", ") + l_strF;
                               return l_strAll;
                           }()))
                           .arg(QString::fromLatin1(qVersion())));
}

// ---- state --------------------------------------------------------------------------------------

void MainWindow::f_UpdateTitle()
{
    setWindowTitle(QStringLiteral("%1 — %2[*] — Atlas Editor").arg(f_Qs(m_pDoc->f_Project().m_strName), m_pDoc->f_DisplayName()));
    setWindowModified(m_pDoc->f_IsDirty());
}

void MainWindow::f_UpdateActions()
{
    const QStringList l_oSel = m_pDoc->f_Selection();
    const bool l_bAny = !l_oSel.isEmpty();
    const QString l_oCur = m_pDoc->f_CurrentSprite();
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const bool l_bInAtlas = l_spSnap && l_spSnap->f_Region(l_oCur);
    const bool l_bImage = l_oSel.size() == 1 && m_pDoc->f_IsImage(l_oCur);
    const bool l_bVariant = !m_pDoc->f_Variant().isEmpty();
    bool l_bAnyChild = false, l_bAnyVariantArt = false;
    for (const QString& l_oN : l_oSel) {
        l_bAnyChild = l_bAnyChild || m_pDoc->f_SpriteDef(l_oN).f_IsChild();
        l_bAnyVariantArt = l_bAnyVariantArt || m_pDoc->f_HasVariantArt(l_oN, m_pDoc->f_Variant());
    }
    const bool l_bBusy = m_pDoc->f_IsSaving();
    m_pSaveAct->setEnabled(!l_bBusy);
    m_pSaveAsAct->setEnabled(!l_bBusy);
    m_pDeleteAct->setEnabled(l_bAny);
    m_pRenameAct->setEnabled(l_oSel.size() == 1);
    m_pNewChildAct->setEnabled(l_bInAtlas);
    m_pSliceAct->setEnabled(l_bInAtlas);
    m_pBakeAct->setEnabled(l_bAnyChild);
    m_pPinAct->setEnabled(l_bAny);
    m_pReplaceImageAct->setEnabled(l_bImage);
    m_pSaveImageAct->setEnabled(l_oSel.size() == 1 && (l_bInAtlas || l_bImage));
    m_pReplaceVariantAct->setVisible(l_bVariant);
    m_pRemoveVariantAct->setVisible(l_bVariant);
    m_pReplaceVariantAct->setEnabled(l_bImage);
    m_pRemoveVariantAct->setEnabled(l_bAnyVariantArt);
    m_pReplaceVariantAct->setText(l_bVariant ? tr("Replace %1 Art…").arg(m_pDoc->f_Variant()) : tr("Replace &Variant Art…"));
    m_pRemoveVariantAct->setText(l_bVariant ? tr("Remove %1 Art").arg(m_pDoc->f_Variant()) : tr("Remove Variant &Art"));
    m_pExtractAct->setEnabled(!m_pDoc->f_Project().m_mapImages.empty());
    m_pEditModeAct->setEnabled(l_bInAtlas || m_pPanel->f_IsEditMode());
    m_pExportAllAct->setEnabled(!m_pDoc->f_Project().m_vVariants.empty());
}

void MainWindow::f_UpdateStatus()
{
    const atlas::Project& l_oP = m_pDoc->f_Project();
    QString l_oStore = tr("embedded · %n image(s)", nullptr, int(l_oP.m_mapImages.size()));
    if (const atlas::Variant* l_pV = m_pDoc->f_CurrentVariant()) l_oStore += tr(" · %1: %2 own").arg(f_Qs(l_pV->m_strId)).arg(l_pV->m_mapImages.size());
    m_pStatusStore->setText(l_oStore);
    m_pStatusStore->setToolTip(tr("The images live in the packed atlas Save writes: %1").arg(QDir::toNativeSeparators(m_pDoc->f_StoreFile())));
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    if (m_pDoc->f_IsBuilding()) m_pStatusBuild->setText(tr("Building…"));
    else if (l_spSnap) m_pStatusBuild->setText(tr("Built in %1 ms").arg(l_spSnap->m_i64ElapsedMs));
    if (!l_spSnap) {
        for (QLabel* l_pL : {m_pStatusPages, m_pStatusOccupancy, m_pStatusSprites, m_pStatusProblems}) l_pL->clear();
        return;
    }
    const atlas::BuildResult& l_oB = l_spSnap->m_oResult;
    const int l_iPage = m_pPanel->f_AtlasCanvas()->f_Page();
    QString l_oPages = tr("%n page(s)", nullptr, int(l_oB.m_vPages.size()));
    if (l_iPage < int(l_oB.m_vPages.size())) l_oPages += QStringLiteral("  ·  %1 × %2").arg(l_oB.m_vPages[size_t(l_iPage)].m_oImage.m_iW).arg(l_oB.m_vPages[size_t(l_iPage)].m_oImage.m_iH);
    m_pStatusPages->setText(l_oPages);
    // Occupancy: pixels that sprites own (aliases and plain children share someone else's).
    double l_dUsed = 0, l_dTotal = 0;
    for (const atlas::Page& l_oPg : l_oB.m_vPages) l_dTotal += double(l_oPg.m_oImage.m_iW) * l_oPg.m_oImage.m_iH;
    for (const atlas::Region& l_oR : l_oB.m_vRegions)
        if (l_oR.m_strAliasOf.empty() && (!l_oR.m_bChild || l_oR.m_bBaked)) l_dUsed += double(l_oR.m_oFrame.m_iW) * l_oR.m_oFrame.m_iH;
    m_pStatusOccupancy->setText(l_dTotal > 0 ? tr("%1% used").arg(l_dUsed * 100 / l_dTotal, 0, 'f', 1) : QString());
    m_pStatusSprites->setText(tr("%n sprite(s)", nullptr, int(l_oB.m_vRegions.size())));
    const int l_iE = l_oB.f_ErrorCount(), l_iW = l_oB.f_WarningCount();
    m_pStatusProblems->setText(l_iE || l_iW ? tr("%1 error(s), %2 warning(s)").arg(l_iE).arg(l_iW) : tr("no problems"));
    m_pStatusProblems->setStyleSheet(l_iE ? QStringLiteral("color: #f04a5a;") : l_iW ? QStringLiteral("color: #e0a82e;") : QString());
}

void MainWindow::f_RefreshVariants()
{
    const QSignalBlocker l_oBlock(m_pVariantCombo);
    m_pVariantCombo->clear();
    m_pVariantCombo->addItem(tr("Base"), QString());
    for (const atlas::Variant& l_oV : m_pDoc->f_Project().m_vVariants) m_pVariantCombo->addItem(f_Qs(l_oV.m_strId), f_Qs(l_oV.m_strId));
    m_pVariantCombo->setCurrentIndex(std::max(0, m_pVariantCombo->findData(m_pDoc->f_Variant())));
    m_pVariantCombo->setEnabled(m_pVariantCombo->count() > 1);
}

// ---- events -------------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* l_pE)
{
    if (!f_MaybeSave()) {
        l_pE->ignore();
        return;
    }
    f_WriteSettings();
    l_pE->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* l_pE)
{
    if (!l_pE->mimeData()->hasUrls()) return;
    for (const QUrl& l_oU : l_pE->mimeData()->urls()) {
        const QFileInfo l_oFi(l_oU.toLocalFile());
        const QString l_oExt = l_oFi.suffix().toLower();
        if (l_oFi.isDir() || l_oExt == QLatin1String("png") || l_oExt == QLatin1String("atlasproj")) {
            l_pE->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* l_pE)
{
    QStringList l_oPaths;
    for (const QUrl& l_oU : l_pE->mimeData()->urls()) {
        const QFileInfo l_oFi(l_oU.toLocalFile());
        const QString l_oExt = l_oFi.suffix().toLower();
        if (l_oExt == QLatin1String("atlasproj")) {
            l_pE->acceptProposedAction();
            if (f_MaybeSave()) f_OpenProject(l_oFi.absoluteFilePath());
            return;   // a project replaces everything else dropped with it
        }
        if (l_oFi.isDir() || l_oExt == QLatin1String("png")) l_oPaths << l_oFi.absoluteFilePath();
    }
    l_pE->acceptProposedAction();
    // PNGs are named by their file name, PNGs in a dropped folder by their path below it.
    if (!l_oPaths.isEmpty()) f_AddImagePaths(l_oPaths);
}
