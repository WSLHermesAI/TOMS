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

constexpr int kMaxRecent = 10;
constexpr int kStateVersion = 1;   // bump when the dock layout changes incompatibly

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string u8(const QString& s) { return s.toStdString(); }

QSettings settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("AtlasEditor")); }

QAction* makeAction(QObject* parent, const QString& text, const QKeySequence& key = QKeySequence(),
                    Icons::Id icon = Icons::Id(-1))
{
    auto* a = new QAction(text, parent);
    if (!key.isEmpty()) a->setShortcut(key);
    if (int(icon) >= 0) a->setIcon(Icons::icon(icon));
    return a;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_doc(new AtlasDocument(this))
    , m_panel(new CanvasPanel(m_doc, this))
{
    setObjectName(QStringLiteral("AtlasEditorMainWindow"));
    setCentralWidget(m_panel);
    setDockNestingEnabled(true);
    setAcceptDrops(true);

    createActions();
    createDocks();
    createMenus();
    createToolBar();
    createStatusBar();

    connect(m_doc, &AtlasDocument::dirtyChanged, this, &MainWindow::updateTitle);
    connect(m_doc, &AtlasDocument::filePathChanged, this, &MainWindow::updateTitle);
    connect(m_doc, &AtlasDocument::projectChanged, this, &MainWindow::updateTitle);
    connect(m_doc, &AtlasDocument::projectChanged, this, &MainWindow::updateStatus);
    connect(m_doc, &AtlasDocument::variantChanged, this, &MainWindow::updateActions);
    connect(m_doc, &AtlasDocument::selectionChanged, this, &MainWindow::updateActions);
    connect(m_doc, &AtlasDocument::projectChanged, this, &MainWindow::updateActions);
    connect(m_doc, &AtlasDocument::projectChanged, this, &MainWindow::refreshVariants);
    connect(m_doc, &AtlasDocument::variantChanged, this, &MainWindow::refreshVariants);
    connect(m_doc, &AtlasDocument::buildStarted, this, &MainWindow::updateStatus);
    connect(m_doc, &AtlasDocument::buildFinished, this, &MainWindow::updateStatus);
    connect(m_doc, &AtlasDocument::buildFinished, this, &MainWindow::updateActions);
    connect(m_doc, &AtlasDocument::exportFinished, this, &MainWindow::onExportFinished);
    connect(m_doc, &AtlasDocument::message, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    connect(m_panel, &CanvasPanel::editModeChanged, this, [this](bool on) {
        m_editModeAct->setChecked(on);
        updateStatus();
    });
    connect(m_panel->atlasCanvas(), &AtlasCanvas::pageChanged, this, &MainWindow::updateStatus);
    connect(m_panel, &CanvasPanel::zoomChanged, this, [this](double z) { m_zoomButton->setText(QStringLiteral("%1%").arg(qRound(z * 100))); });
    auto showSpriteMenu = [this](const QPoint& at) { m_spriteMenu->exec(at); };
    connect(m_panel->atlasCanvas(), &AtlasCanvas::contextMenuRequested, this, showSpriteMenu);
    connect(m_panel->editCanvas(), &SpriteEditCanvas::contextMenuRequested, this, showSpriteMenu);
    connect(m_spriteDock, &SpriteTreeDock::contextMenuRequested, this, showSpriteMenu);
    connect(m_spriteDock, &SpriteTreeDock::editSpriteRequested, m_panel, &CanvasPanel::enterEditMode);
    connect(m_propsDock, &PropertiesDock::importReferencesRequested, this, &MainWindow::onImportReferences);

    readSettings();
    updateTitle();
    updateActions();
    updateStatus();
    refreshVariants();
}

// ---- construction -------------------------------------------------------------------------------

void MainWindow::createActions()
{
    using Id = Icons::Id;
    m_newAct = makeAction(this, tr("&New"), QKeySequence::New, Id::New);
    m_openAct = makeAction(this, tr("&Open…"), QKeySequence::Open, Id::Open);
    m_saveAct = makeAction(this, tr("&Save"), QKeySequence::Save, Id::Save);
    m_saveAsAct = makeAction(this, tr("Save &As…"), QKeySequence::SaveAs);
    m_importAct = makeAction(this, tr("&Import Atlas…"), QKeySequence(tr("Ctrl+I")), Id::Import);
    m_importAct->setToolTip(tr("Import a .pi / .atlas / TexturePacker .json atlas as a new project"));
    m_saveAct->setToolTip(tr("Save the project and its packed atlas (the images live there) (Ctrl+S)"));
    m_addFolderAct = makeAction(this, tr("Add &Reference Folder…"), QKeySequence(), Id::Folder);
    m_addFolderAct->setToolTip(tr("A folder to import new or changed art from (Sprite > Import From References)"));
    m_addImagesAct = makeAction(this, tr("Add &Images…"), QKeySequence(tr("Ctrl+Shift+I")), Id::Image);
    m_addImagesAct->setToolTip(tr("Copy PNG files into the project"));
    m_extractAct = makeAction(this, tr("E&xtract Images…"));
    m_extractAct->setToolTip(tr("Write every sprite as its own PNG (the way back to separate files)"));
    m_exportAct = makeAction(this, tr("&Export Copy To Folder…"), QKeySequence(tr("Ctrl+B")), Id::Export);
    m_exportAct->setToolTip(tr("Write the atlas files (shown art) to a folder of your choice; the project is not touched (Ctrl+B)"));
    m_exportAllAct = makeAction(this, tr("Export All &Variants To Folder…"), QKeySequence(tr("Ctrl+Shift+B")));
    m_exportAllAct->setToolTip(tr("Base art into the folder, each variant into <folder>/<variant id>"));
    m_quitAct = makeAction(this, tr("&Quit"), QKeySequence::Quit);
    connect(m_newAct, &QAction::triggered, this, &MainWindow::onNew);
    connect(m_openAct, &QAction::triggered, this, &MainWindow::onOpen);
    connect(m_saveAct, &QAction::triggered, this, &MainWindow::save);
    connect(m_saveAsAct, &QAction::triggered, this, &MainWindow::saveAs);
    connect(m_importAct, &QAction::triggered, this, &MainWindow::onImportAtlas);
    connect(m_addFolderAct, &QAction::triggered, this, &MainWindow::onAddReferenceFolder);
    connect(m_addImagesAct, &QAction::triggered, this, &MainWindow::onAddImages);
    connect(m_extractAct, &QAction::triggered, this, &MainWindow::onExtractImages);
    connect(m_exportAct, &QAction::triggered, this, [this] { onExport(false); });
    connect(m_exportAllAct, &QAction::triggered, this, [this] { onExport(true); });
    connect(m_quitAct, &QAction::triggered, this, &QWidget::close);

    m_undoAct = m_doc->undoStack()->createUndoAction(this, tr("&Undo"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_redoAct = m_doc->undoStack()->createRedoAction(this, tr("&Redo"));
    m_redoAct->setShortcut(QKeySequence::Redo);
    m_deleteAct = makeAction(this, tr("&Delete"), QKeySequence::Delete);
    m_renameAct = makeAction(this, tr("Re&name"), QKeySequence(Qt::Key_F2));
    m_selectAllAct = makeAction(this, tr("Select &All"), QKeySequence::SelectAll);
    m_findAct = makeAction(this, tr("&Find Sprite"), QKeySequence::Find);
    connect(m_deleteAct, &QAction::triggered, this, &MainWindow::onDelete);
    connect(m_selectAllAct, &QAction::triggered, this, &MainWindow::onSelectAll);

    m_newChildAct = makeAction(this, tr("New &Child Sprite…"), QKeySequence(tr("Ctrl+Shift+N")), Id::Child);
    m_sliceAct = makeAction(this, tr("&Slice into Grid…"), QKeySequence(tr("Ctrl+G")), Id::Grid);
    m_bakeAct = makeAction(this, tr("&Bake / Unbake"), QKeySequence(), Id::Bake);
    m_bakeAct->setToolTip(tr("A baked child gets its own copy of the pixels in the atlas"));
    m_pinAct = makeAction(this, tr("&Pin / Unpin"), QKeySequence(tr("Ctrl+P")), Id::Pin);
    m_importRefsAct = makeAction(this, tr("&Import From References…"), QKeySequence(tr("Ctrl+R")), Id::Import);
    m_importRefsAct->setToolTip(tr("New or changed art in the reference folders (the variant's folder when a variant is shown)"));
    m_replaceImageAct = makeAction(this, tr("Re&place Image…"));
    m_saveImageAct = makeAction(this, tr("Save Image &As…"));
    m_replaceVariantAct = makeAction(this, tr("Replace &Variant Art…"));
    m_removeVariantAct = makeAction(this, tr("Remove Variant &Art"));
    m_removeVariantAct->setToolTip(tr("The variant shows the base art again for the selected sprites"));
    m_editModeAct = makeAction(this, tr("&Edit Sprite"), QKeySequence(tr("E")), Id::EditMode);
    m_editModeAct->setCheckable(true);
    m_editModeAct->setToolTip(tr("Sprite edit mode: child rects, pivot, 9-slice (E, or double-click a sprite)"));
    connect(m_newChildAct, &QAction::triggered, this, &MainWindow::onNewChild);
    connect(m_sliceAct, &QAction::triggered, this, &MainWindow::onSliceGrid);
    connect(m_bakeAct, &QAction::triggered, this, &MainWindow::onToggleBake);
    connect(m_pinAct, &QAction::triggered, this, &MainWindow::onTogglePin);
    connect(m_importRefsAct, &QAction::triggered, this, &MainWindow::onImportReferences);
    connect(m_replaceImageAct, &QAction::triggered, this, [this] { onReplaceImage(false); });
    connect(m_replaceVariantAct, &QAction::triggered, this, [this] { onReplaceImage(true); });
    connect(m_removeVariantAct, &QAction::triggered, this, &MainWindow::onRemoveVariantArt);
    connect(m_saveImageAct, &QAction::triggered, this, &MainWindow::onSaveImageAs);
    connect(m_editModeAct, &QAction::triggered, this, &MainWindow::onEditModeToggled);

    m_fitAct = makeAction(this, tr("&Fit"), QKeySequence(tr("F")), Id::Fit);
    m_zoomInAct = makeAction(this, tr("Zoom &In"), QKeySequence::ZoomIn, Id::ZoomIn);
    m_zoomOutAct = makeAction(this, tr("Zoom &Out"), QKeySequence::ZoomOut, Id::ZoomOut);
    connect(m_fitAct, &QAction::triggered, this, [this] { m_panel->currentView()->fitToView(); });
    connect(m_zoomInAct, &QAction::triggered, this, [this] { m_panel->currentView()->zoomIn(); });
    connect(m_zoomOutAct, &QAction::triggered, this, [this] { m_panel->currentView()->zoomOut(); });
    m_outlinesAct = makeAction(this, tr("Show &Outlines"), QKeySequence(tr("O")));
    m_outlinesAct->setCheckable(true);
    m_outlinesAct->setChecked(true);
    m_childrenAct = makeAction(this, tr("Show &Child Sprites"));
    m_childrenAct->setCheckable(true);
    m_childrenAct->setChecked(true);
    connect(m_outlinesAct, &QAction::toggled, m_panel->atlasCanvas(), &AtlasCanvas::setShowOutlines);
    connect(m_childrenAct, &QAction::toggled, m_panel->atlasCanvas(), &AtlasCanvas::setShowChildren);
    m_darkAct = makeAction(this, tr("&Dark Theme"));
    m_lightAct = makeAction(this, tr("&Light Theme"));
    auto* themes = new QActionGroup(this);
    for (QAction* a : {m_darkAct, m_lightAct}) {
        a->setCheckable(true);
        themes->addAction(a);
    }
    connect(m_darkAct, &QAction::triggered, this, [this] { setDarkTheme(true); });
    connect(m_lightAct, &QAction::triggered, this, [this] { setDarkTheme(false); });

    m_helpCliAct = makeAction(this, tr("&Command Line Help"));
    m_aboutAct = makeAction(this, tr("&About Atlas Editor"));
    connect(m_helpCliAct, &QAction::triggered, this, &MainWindow::onCommandLineHelp);
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::createDocks()
{
    m_spriteDock = new SpriteTreeDock(m_doc, this);
    m_propsDock = new PropertiesDock(m_doc, this);
    m_animDock = new AnimationDock(m_doc, this);
    m_problemsDock = new ProblemsDock(m_doc, this);
    m_historyDock = createHistoryDock(m_doc->undoStack(), this);
    m_backup = new AutoBackup(m_doc->undoStack(), QStringLiteral(".atlasproj"), [this] { return m_doc->filePath(); },
                              [this](const QString& p, QString* e) { return m_doc->writeBackup(p, e); }, this);
    connect(m_backup, &AutoBackup::message, this, [this](const QString& t) { statusBar()->showMessage(t, 5000); });
    addDockWidget(Qt::LeftDockWidgetArea, m_spriteDock);
    addDockWidget(Qt::RightDockWidgetArea, m_propsDock);
    tabifyDockWidget(m_propsDock, m_historyDock);
    m_propsDock->raise();
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_animDock);
    tabifyDockWidget(m_problemsDock, m_animDock);
    m_problemsDock->raise();
    resizeDocks({m_spriteDock, m_propsDock}, {270, 310}, Qt::Horizontal);
    resizeDocks({m_problemsDock}, {190}, Qt::Vertical);
    connect(m_renameAct, &QAction::triggered, m_spriteDock, &SpriteTreeDock::beginRename);
    connect(m_findAct, &QAction::triggered, this, [this] {
        m_spriteDock->show();
        m_spriteDock->raise();
        m_spriteDock->focusFilter();
    });
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_newAct);
    file->addAction(m_openAct);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    file->addAction(m_saveAct);
    file->addAction(m_saveAsAct);
    file->addSeparator();
    file->addAction(m_importAct);
    file->addAction(m_addImagesAct);
    file->addAction(m_addFolderAct);
    file->addSeparator();
    file->addAction(m_exportAct);
    file->addAction(m_exportAllAct);
    file->addAction(m_extractAct);
    file->addSeparator();
    m_backup->addMenu(file, this);
    file->addSeparator();
    file->addAction(m_quitAct);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_undoAct);
    edit->addAction(m_redoAct);
    edit->addSeparator();
    edit->addAction(m_deleteAct);
    edit->addAction(m_renameAct);
    edit->addAction(m_selectAllAct);
    edit->addAction(m_findAct);

    m_spriteMenu = menuBar()->addMenu(tr("&Sprite"));
    m_spriteMenu->addAction(m_editModeAct);
    m_spriteMenu->addSeparator();
    m_spriteMenu->addAction(m_newChildAct);
    m_spriteMenu->addAction(m_sliceAct);
    m_spriteMenu->addAction(m_bakeAct);
    m_spriteMenu->addAction(m_pinAct);
    m_spriteMenu->addSeparator();
    m_spriteMenu->addAction(m_replaceImageAct);
    m_spriteMenu->addAction(m_saveImageAct);
    m_spriteMenu->addAction(m_replaceVariantAct);
    m_spriteMenu->addAction(m_removeVariantAct);
    m_spriteMenu->addAction(m_importRefsAct);
    m_spriteMenu->addSeparator();
    m_spriteMenu->addAction(m_renameAct);
    m_spriteMenu->addAction(m_deleteAct);

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    {   // the viewport's renderer: the game's (bgfx, the same pixels as toms_game) or QPainter
        QAction* a = m_viewMenu->addAction(tr("Preview with the Game Renderer (after a restart)"));
        a->setCheckable(true);
        a->setChecked(settings().value(QStringLiteral("preview/gameRenderer"), true).toBool());
        a->setToolTip(tr("Draw the pages and sprites with toms_game's renderer (bgfx): exactly the game's pixels. Off: QPainter."));
        connect(a, &QAction::toggled, this, [this](bool on) {
            settings().setValue(QStringLiteral("preview/gameRenderer"), on);
            statusBar()->showMessage(tr("The renderer changes the next time the editor starts."), 8000);
        });
        m_viewMenu->addSeparator();
    }
    m_viewMenu->addAction(m_fitAct);
    m_viewMenu->addAction(m_zoomInAct);
    m_viewMenu->addAction(m_zoomOutAct);
    CanvasView::addCoordinatesAction(m_viewMenu);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_outlinesAct);
    m_viewMenu->addAction(m_childrenAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_darkAct);
    m_viewMenu->addAction(m_lightAct);
    m_viewMenu->addSeparator();
    for (QDockWidget* d : std::initializer_list<QDockWidget*>{m_spriteDock, m_propsDock, m_animDock, m_problemsDock, m_historyDock})
        m_viewMenu->addAction(d->toggleViewAction());

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_helpCliAct);
    help->addAction(m_aboutAct);
}

void MainWindow::createToolBar()
{
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("MainToolBar"));
    tb->setIconSize(QSize(20, 20));
    tb->setMovable(false);
    tb->addAction(m_newAct);
    tb->addAction(m_openAct);
    tb->addAction(m_saveAct);
    tb->addSeparator();
    tb->addAction(m_importAct);
    tb->addAction(m_addImagesAct);
    tb->addAction(m_exportAct);
    tb->addSeparator();
    auto* variantLabel = new QLabel(tr(" Variant "), tb);
    variantLabel->setForegroundRole(QPalette::PlaceholderText);
    tb->addWidget(variantLabel);
    m_variantCombo = new QComboBox(tb);
    m_variantCombo->setMinimumContentsLength(10);
    m_variantCombo->setToolTip(tr("Which art the canvas shows (and Import From References / Export Copy work on)"));
    tb->addWidget(m_variantCombo);
    connect(m_variantCombo, &QComboBox::activated, this, [this] { m_doc->setVariant(m_variantCombo->currentData().toString()); });
    tb->addSeparator();
    tb->addAction(m_editModeAct);
    tb->addAction(m_newChildAct);
    tb->addAction(m_sliceAct);
    tb->addSeparator();
    tb->addAction(m_zoomOutAct);
    m_zoomButton = new QToolButton(tb);
    m_zoomButton->setText(QStringLiteral("100%"));
    m_zoomButton->setToolTip(tr("Zoom"));
    m_zoomButton->setPopupMode(QToolButton::InstantPopup);
    m_zoomButton->setMinimumWidth(56);
    auto* zoomMenu = new QMenu(m_zoomButton);
    for (int pct : {25, 50, 100, 200, 400, 800, 1600})
        zoomMenu->addAction(QStringLiteral("%1%").arg(pct), this, [this, pct] { m_panel->currentView()->setZoom(pct / 100.0); });
    zoomMenu->addSeparator();
    zoomMenu->addAction(m_fitAct);
    m_zoomButton->setMenu(zoomMenu);
    tb->addWidget(m_zoomButton);
    tb->addAction(m_zoomInAct);
    tb->addAction(m_fitAct);
}

void MainWindow::createStatusBar()
{
    auto label = [this](const QString& tip) {
        auto* l = new QLabel(this);
        l->setToolTip(tip);
        l->setContentsMargins(6, 0, 6, 0);
        statusBar()->addPermanentWidget(l);
        return l;
    };
    m_statusStore = label(tr("The images live in the project's packed atlas"));
    m_statusBuild = label(tr("Last build"));
    m_statusPages = label(tr("Pages and the size of the shown page"));
    m_statusOccupancy = label(tr("Share of the page area covered by sprite pixels"));
    m_statusSprites = label(tr("Sprites in the atlas"));
    m_statusProblems = label(tr("Warnings and errors (see Problems)"));
}

// ---- settings -----------------------------------------------------------------------------------

void MainWindow::readSettings()
{
    QSettings s = settings();
    const bool dark = s.value(QStringLiteral("theme"), QStringLiteral("dark")).toString() != QLatin1String("light");
    (dark ? m_darkAct : m_lightAct)->setChecked(true);
    setDarkTheme(dark);
    m_outlinesAct->setChecked(s.value(QStringLiteral("view/outlines"), true).toBool());
    m_childrenAct->setChecked(s.value(QStringLiteral("view/children"), true).toBool());
    if (!restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray())) resize(1400, 860);
    restoreState(s.value(QStringLiteral("windowState")).toByteArray(), kStateVersion);
}

void MainWindow::writeSettings() const
{
    QSettings s = settings();
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("windowState"), saveState(kStateVersion));
    s.setValue(QStringLiteral("theme"), Theme::mode() == Theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    s.setValue(QStringLiteral("view/outlines"), m_outlinesAct->isChecked());
    s.setValue(QStringLiteral("view/children"), m_childrenAct->isChecked());
}

QString MainWindow::lastDir() const
{
    if (!m_doc->filePath().isEmpty()) return QFileInfo(m_doc->filePath()).absolutePath();
    return settings().value(QStringLiteral("lastDir"), QDir::homePath()).toString();
}

void MainWindow::rememberDir(const QString& pathOrDir)
{
    const QFileInfo fi(pathOrDir);
    settings().setValue(QStringLiteral("lastDir"), fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
}

void MainWindow::addRecentFile(const QString& path)
{
    QSettings s = settings();
    QStringList recent = s.value(QStringLiteral("recentFiles")).toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    recent.removeAll(abs);
    recent.prepend(abs);
    while (recent.size() > kMaxRecent) recent.removeLast();
    s.setValue(QStringLiteral("recentFiles"), recent);
    rememberDir(abs);
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = settings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& path : recent) {
        QAction* a = m_recentMenu->addAction(QDir::toNativeSeparators(path));
        a->setEnabled(QFileInfo::exists(path));
        connect(a, &QAction::triggered, this, [this, path] {
            if (maybeSave()) openProject(path);
        });
    }
    if (recent.isEmpty()) m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
    else {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(tr("Clear List"), this, [] { settings().remove(QStringLiteral("recentFiles")); });
    }
}

// ---- files --------------------------------------------------------------------------------------

bool MainWindow::openProject(const QString& path)
{
    QString err;
    if (!m_doc->open(path, &err)) {
        QMessageBox::warning(this, tr("Open Project"), tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    addRecentFile(path);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_doc->isDirty()) return true;
    const auto r = QMessageBox::warning(this, tr("Unsaved Changes"),
                                        tr("%1 has unsaved changes. Save them?").arg(m_doc->displayName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

bool MainWindow::save()
{
    if (m_doc->filePath().isEmpty()) return saveAs();
    return saveTo(m_doc->filePath());
}

bool MainWindow::saveAs()
{
    const QString start = m_doc->filePath().isEmpty() ? QDir(lastDir()).filePath(qs(m_doc->project().name) + QStringLiteral(".atlasproj"))
                                                      : m_doc->filePath();
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"), start, tr("Atlas projects (*.atlasproj)"));
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".atlasproj");
    return saveTo(path);
}

bool MainWindow::saveTo(const QString& path)
{
    statusBar()->showMessage(tr("Saving…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    SaveReport report;
    QString err;
    const bool ok = m_doc->save(path, &report, &err);
    QApplication::restoreOverrideCursor();
    for (const atlas::Diagnostic& d : report.diagnostics) m_problemsDock->appendLog(qs(atlas::formatDiagnostic(d)));
    if (!ok) {
        statusBar()->showMessage(tr("Save failed"), 5000);
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n\n%2\n\nNothing was written.")
                                                   .arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    for (const QString& f : report.written) m_problemsDock->appendLog(tr("  wrote %1").arg(f));
    if (report.written.size() <= 1) m_problemsDock->appendLog(tr("  (the packed atlas and output files were up to date)"));
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 5000);
    addRecentFile(path);
    return true;
}

void MainWindow::onNew()
{
    if (maybeSave()) m_doc->newProject();
}

void MainWindow::onOpen()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), lastDir(), tr("Atlas projects (*.atlasproj);;All files (*)"));
    if (!path.isEmpty()) openProject(path);
}

void MainWindow::onImportAtlas()
{
    if (!maybeSave()) return;
    const QString atlasFile = QFileDialog::getOpenFileName(this, tr("Import Atlas"), lastDir(),
                                                           tr("Atlas files (*.pi *.atlas *.json);;All files (*)"));
    if (atlasFile.isEmpty()) return;
    const QFileInfo fi(atlasFile);
    QString projPath = QFileDialog::getSaveFileName(this, tr("New Project for the Imported Atlas"),
                                                    fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".atlasproj")),
                                                    tr("Atlas projects (*.atlasproj)"));
    if (projPath.isEmpty()) return;
    if (QFileInfo(projPath).suffix().isEmpty()) projPath += QStringLiteral(".atlasproj");

    QApplication::setOverrideCursor(Qt::WaitCursor);
    atlas::BuildResult in;
    atlas::Project p;
    std::string err;
    // Every sprite is cut out of the atlas into memory: an embedded project, nothing written yet.
    const bool ok = atlas::importAtlasFile(u8(atlasFile), in, &err) &&
                    atlas::importToProject(in, u8(QDir::fromNativeSeparators(projPath)), std::string(), p, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Import Atlas"), tr("Could not import %1:\n%2").arg(QDir::toNativeSeparators(atlasFile), qs(err)));
        return;
    }
    // Keep writing the format it came in, like `atlaspack import`.
    const QString ext = fi.suffix().toLower();
    p.output.formats = {ext == QLatin1String("pi") ? "pi" : ext == QLatin1String("json") ? "tp-json" : "atlas"};
    p.output.dir = ".";
    m_doc->adopt(p);
    rememberDir(projPath);
    m_problemsDock->appendLog(tr("Imported %1: %2 sprite(s), %3 image(s). Save writes %4 and the packed atlas %5.")
                                  .arg(QDir::toNativeSeparators(atlasFile)).arg(in.regions.size()).arg(p.images.size())
                                  .arg(QDir::toNativeSeparators(projPath), QDir::toNativeSeparators(m_doc->storeFile())));
    if (QFileInfo(m_doc->storeFile()).absoluteFilePath().compare(fi.absoluteFilePath(), Qt::CaseInsensitive) == 0)
        m_problemsDock->appendLog(tr("Note: saving rewrites %1 with the re-packed atlas.").arg(QDir::toNativeSeparators(atlasFile)));
}

void MainWindow::onAddReferenceFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Reference Folder"), lastDir());
    if (dir.isEmpty()) return;
    rememberDir(dir);
    m_doc->addReference(dir);
}

void MainWindow::onAddImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Images"), lastDir(), tr("PNG images (*.png)"));
    if (files.isEmpty()) return;
    rememberDir(files.first());
    addImagePaths(files);
}

int MainWindow::addImagePaths(const QStringList& paths)
{
    const QList<ImageFile> found = AtlasDocument::collectPngs(paths);
    if (found.isEmpty()) {
        statusBar()->showMessage(tr("No PNG files there"), 4000);
        return 0;
    }
    // Names already in the project: ask once each, or once for all of them.
    int conflicts = 0;
    for (const ImageFile& f : found) conflicts += m_doc->isImage(f.name);
    QList<ImageFile> take;
    int decided = -1;   // -1 ask, 0 skip all, 1 replace all
    int seen = 0;
    for (const ImageFile& f : found) {
        if (!m_doc->isImage(f.name)) {
            take << f;
            continue;
        }
        seen++;
        int choice = decided;
        if (choice < 0) {
            QMessageBox ask(QMessageBox::Question, tr("Add Images"),
                            tr("The project already has an image called '%1'.\nReplace it with %2?")
                                .arg(f.name, QDir::toNativeSeparators(f.path)),
                            QMessageBox::NoButton, this);
            QPushButton* replace = ask.addButton(tr("Replace"), QMessageBox::AcceptRole);
            QPushButton* skip = ask.addButton(tr("Skip"), QMessageBox::RejectRole);
            QPushButton* cancel = ask.addButton(QMessageBox::Cancel);
            ask.setDefaultButton(skip);
            QCheckBox* all = nullptr;
            if (conflicts - seen > 0) {
                all = new QCheckBox(tr("Do the same for the other %n existing image(s)", nullptr, conflicts - seen), &ask);
                ask.setCheckBox(all);
            }
            ask.exec();
            if (ask.clickedButton() == cancel) return 0;
            choice = ask.clickedButton() == replace ? 1 : 0;
            if (all && all->isChecked()) decided = choice;
        }
        if (choice == 1) take << f;
    }
    if (take.isEmpty()) {
        statusBar()->showMessage(tr("Nothing added"), 4000);
        return 0;
    }
    QStringList errors;
    const int n = m_doc->setImages(take, QString(), tr("Add %n image(s)", nullptr, int(take.size())), &errors);
    for (const QString& e : errors) m_problemsDock->appendLog(e);
    m_problemsDock->appendLog(tr("Added %n image(s)", nullptr, n) + (found.size() > take.size() ? tr(" (%1 skipped)").arg(found.size() - take.size()) : QString()));
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Add Images"), tr("%n file(s) could not be added:", nullptr, int(errors.size())) + QStringLiteral("\n") +
                                                         errors.mid(0, 10).join(QLatin1Char('\n')));
    return n;
}

void MainWindow::onExtractImages()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Images To"), lastDir());
    if (dir.isEmpty()) return;
    QMessageBox ask(QMessageBox::Question, tr("Extract Images"),
                    tr("Write every sprite%1 as its own PNG to\n%2 ?\n\nFiles of the same name are overwritten.")
                        .arg(m_doc->variant().isEmpty() ? QString() : tr(" (%1 art)").arg(m_doc->variant()), QDir::toNativeSeparators(dir)),
                    QMessageBox::Cancel, this);
    QPushButton* go = ask.addButton(tr("Extract"), QMessageBox::AcceptRole);
    auto* children = new QCheckBox(tr("Include child sprites"), &ask);
    ask.setCheckBox(children);
    ask.setDefaultButton(go);
    ask.exec();
    if (ask.clickedButton() != go) return;
    rememberDir(dir);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int count = 0;
    QString err;
    const bool ok = m_doc->extractImages(dir, children->isChecked(), &count, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Extract Images"), err);
        return;
    }
    const QString msg = tr("Extracted %n image(s) to %1", nullptr, count).arg(QDir::toNativeSeparators(dir));
    m_problemsDock->appendLog(msg);
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::onExport(bool allVariants)
{
    if (m_doc->isExporting()) return;
    QSettings s = settings();
    const QString start = s.value(QStringLiteral("exportDir"), lastDir()).toString();
    const QString dir = QFileDialog::getExistingDirectory(this, allVariants ? tr("Export All Variants To Folder") : tr("Export Copy To Folder"), start);
    if (dir.isEmpty()) return;
    s.setValue(QStringLiteral("exportDir"), dir);
    statusBar()->showMessage(tr("Exporting…"));
    m_doc->exportCopy(dir, allVariants);
}

void MainWindow::onExportFinished(bool ok, const QString& summary)
{
    for (const QString& line : summary.split(QLatin1Char('\n'))) m_problemsDock->appendLog(line);
    statusBar()->showMessage(ok ? tr("Exported") : tr("Export failed"), 5000);
    if (!ok) QMessageBox::warning(this, tr("Export"), summary + QStringLiteral("\n\n") + tr("See Problems for the errors."));
}

// ---- edit / sprite ------------------------------------------------------------------------------

void MainWindow::onDelete()
{
    const QStringList sel = m_doc->selection();
    if (sel.isEmpty()) return;
    // Deleting a sprite takes its image, its variant art and its child sprites along.
    const QStringList all = m_doc->deletionClosure(sel);
    QStringList extra;
    for (const QString& n : all)
        if (!sel.contains(n)) extra << n;
    auto list = [](const QStringList& names) {
        return names.mid(0, 12).join(QStringLiteral(", ")) + (names.size() > 12 ? QStringLiteral(", …") : QString());
    };
    QString text = tr("Delete %n sprite(s)?", nullptr, int(sel.size())) + QStringLiteral("\n") + list(sel);
    if (!extra.isEmpty()) text += QStringLiteral("\n\n") + tr("Also deletes %n child sprite(s):", nullptr, int(extra.size())) + QStringLiteral("\n") + list(extra);
    text += QStringLiteral("\n\n") + tr("Images and variant art go with them (Undo brings them back).");
    if (QMessageBox::question(this, tr("Delete"), text) != QMessageBox::Yes) return;
    m_doc->deleteSprites(sel);
}

void MainWindow::onSelectAll()
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    if (!snap) return;
    QStringList all;
    for (const SpriteInfo& s : snap->sprites) all << s.name;
    m_doc->setSelection(all);
}

void MainWindow::onNewChild()
{
    const QString parent = m_doc->currentSprite();
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const atlas::Region* r = snap ? snap->region(parent) : nullptr;
    if (!r) {
        statusBar()->showMessage(tr("Select a sprite that is in the atlas first"), 4000);
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Child Sprite"),
                                               tr("Name (the rect starts as the whole of %1; adjust it in edit mode):").arg(parent),
                                               QLineEdit::Normal, m_doc->uniqueName(parent + QStringLiteral("_0")), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (m_doc->spriteExists(name)) {
        QMessageBox::warning(this, tr("New Child Sprite"), tr("There already is a sprite called '%1'.").arg(name));
        return;
    }
    m_panel->enterEditMode(parent);
    m_doc->addChildren(parent, {{name, atlas::IRect{0, 0, r->origW, r->origH}}}, tr("New child %1").arg(name));
}

void MainWindow::onSliceGrid()
{
    const QString parent = m_doc->currentSprite();
    const QImage img = m_doc->spriteImage(parent);
    if (img.isNull()) {
        statusBar()->showMessage(tr("Select a sprite that is in the atlas first"), 4000);
        return;
    }
    GridSliceDialog dlg(img, parent, [this](const QString& n) { return m_doc->spriteExists(n); }, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto cells = dlg.cells();
    m_doc->addChildren(parent, cells, tr("Slice %1 into %n sprite(s)", nullptr, int(cells.size())).arg(parent));
}

void MainWindow::onToggleBake()
{
    QStringList children;
    bool allBaked = true;
    for (const QString& n : m_doc->selection()) {
        const atlas::SpriteDef d = m_doc->spriteDef(n);
        if (!d.isChild()) continue;
        children << n;
        allBaked = allBaked && d.bake;
    }
    m_doc->editSprites(children, allBaked ? tr("Unbake") : tr("Bake"), [&](atlas::SpriteDef& d) { d.bake = !allBaked; });
}

void MainWindow::onTogglePin()
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    QStringList names;
    bool allPinned = true;
    for (const QString& n : m_doc->selection()) {
        const atlas::SpriteDef d = m_doc->spriteDef(n);
        if (d.isChild() && !d.bake) continue;   // a plain child lives inside its parent
        names << n;
        allPinned = allPinned && d.pinned;
    }
    m_doc->editSprites(names, allPinned ? tr("Unpin") : tr("Pin"), [&](atlas::SpriteDef& d) {
        if (!allPinned && !d.pinned && snap)
            if (const atlas::Region* r = snap->region(qs(d.name)); r && r->aliasOf.empty()) {
                d.pinPage = r->page;   // pinned where it is now
                d.pinX = r->frame.x;
                d.pinY = r->frame.y;
            }
        d.pinned = !allPinned;
    });
}

void MainWindow::onReplaceImage(bool variantArt)
{
    const QString name = m_doc->currentSprite();
    const QString variant = variantArt ? m_doc->variant() : QString();
    if (name.isEmpty() || !m_doc->isImage(name) || (variantArt && variant.isEmpty())) return;
    const QString file = QFileDialog::getOpenFileName(this, variantArt ? tr("%1 Art for %2").arg(variant, name) : tr("Replace %1").arg(name),
                                                      lastDir(), tr("PNG images (*.png)"));
    if (file.isEmpty()) return;
    rememberDir(file);
    QStringList errors;
    const QString text = variantArt ? tr("Replace %1 art of %2").arg(variant, name) : tr("Replace %1").arg(name);
    if (!m_doc->setImages({{name, QDir::fromNativeSeparators(file)}}, variant, text, &errors))
        QMessageBox::warning(this, text, errors.join(QLatin1Char('\n')));
}

void MainWindow::onRemoveVariantArt()
{
    QStringList names;
    for (const QString& n : m_doc->selection())
        if (m_doc->hasVariantArt(n, m_doc->variant())) names << n;
    m_doc->removeVariantArt(names, m_doc->variant());
}

void MainWindow::onSaveImageAs()
{
    const QString name = m_doc->currentSprite();
    if (name.isEmpty()) return;
    const QString start = QDir(lastDir()).filePath(name.section(QLatin1Char('/'), -1) + QStringLiteral(".png"));
    QString file = QFileDialog::getSaveFileName(this, tr("Save %1 As").arg(name), start, tr("PNG images (*.png)"));
    if (file.isEmpty()) return;
    if (QFileInfo(file).suffix().isEmpty()) file += QStringLiteral(".png");
    rememberDir(file);
    QString err;
    if (!m_doc->saveSpriteImage(name, file, &err)) {
        QMessageBox::warning(this, tr("Save Image"), err);
        return;
    }
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(file)), 5000);
}

void MainWindow::onImportReferences()
{
    ReferencesDialog dlg(m_doc, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QList<ImageFile> files = dlg.chosen();
    if (files.isEmpty()) return;
    QStringList errors;
    const QString text = dlg.variant().isEmpty() ? tr("Import %n image(s) from references", nullptr, int(files.size()))
                                                  : tr("Import %n %1 image(s)", nullptr, int(files.size())).arg(dlg.variant());
    const int n = m_doc->setImages(files, dlg.variant(), text, &errors);
    for (const QString& e : errors) m_problemsDock->appendLog(e);
    m_problemsDock->appendLog(tr("Imported %n image(s) from the reference folders", nullptr, n) +
                              (dlg.variant().isEmpty() ? QString() : tr(" as %1 art").arg(dlg.variant())));
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Import From References"), errors.mid(0, 10).join(QLatin1Char('\n')));
}

void MainWindow::onEditModeToggled(bool on)
{
    if (!on) {
        m_panel->exitEditMode();
        return;
    }
    const QString target = m_doc->currentSprite();
    if (target.isEmpty() || !m_doc->snapshot() || !m_doc->snapshot()->region(target)) {
        m_editModeAct->setChecked(false);
        statusBar()->showMessage(tr("Select a sprite to edit (or double-click one)"), 4000);
        return;
    }
    m_panel->enterEditMode(target);
}

// ---- view / help --------------------------------------------------------------------------------

void MainWindow::setDarkTheme(bool dark)
{
    Theme::apply(dark ? Theme::Mode::Dark : Theme::Mode::Light);
    (dark ? m_darkAct : m_lightAct)->setChecked(true);
    // Item views cache their thumbnails' backgrounds via the palette; a repaint picks it up.
    for (QWidget* w : QApplication::allWidgets()) w->update();
}

void MainWindow::onCommandLineHelp()
{
    // The usage text lives in the command line code: ask our own headless mode for it, so this
    // dialog can never drift from what `atlaspack --help` prints.
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc] {
        QString text = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Command Line"));
        auto* view = new QPlainTextEdit(&dlg);
        view->setReadOnly(true);
        view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        view->setLineWrapMode(QPlainTextEdit::NoWrap);
        view->setPlainText(tr("atlas_editor --headless <command> ...   runs a command without opening a window\n"
                              "atlas_editor <project.atlasproj>       opens the project\n\n")
                           + text.replace(QLatin1String("atlaspack "), QLatin1String("atlas_editor --headless ")));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto* l = new QVBoxLayout(&dlg);
        l->addWidget(view);
        l->addWidget(buttons);
        dlg.resize(900, 360);
        dlg.exec();
    });
    proc->start(QCoreApplication::applicationFilePath(), {QStringLiteral("--headless"), QStringLiteral("--help")});
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Atlas Editor"),
                       tr("<h3>TOMS Atlas Editor</h3>"
                          "<p>Packs sprites into texture atlases. The <i>.atlasproj</i> project and the packed atlas Save "
                          "writes next to it hold everything, images included; folders of PNGs are only references to "
                          "import new art from.</p>"
                          "<p>Formats: %1</p><p>Built with Qt %2.</p>")
                           .arg(qs([] {
                               std::string all;
                               for (const std::string& f : atlas::exportFormats()) all += (all.empty() ? "" : ", ") + f;
                               return all;
                           }()))
                           .arg(QString::fromLatin1(qVersion())));
}

// ---- state --------------------------------------------------------------------------------------

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1 — %2[*] — Atlas Editor").arg(qs(m_doc->project().name), m_doc->displayName()));
    setWindowModified(m_doc->isDirty());
}

void MainWindow::updateActions()
{
    const QStringList sel = m_doc->selection();
    const bool any = !sel.isEmpty();
    const QString cur = m_doc->currentSprite();
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const bool inAtlas = snap && snap->region(cur);
    const bool image = sel.size() == 1 && m_doc->isImage(cur);
    const bool variant = !m_doc->variant().isEmpty();
    bool anyChild = false, anyVariantArt = false;
    for (const QString& n : sel) {
        anyChild = anyChild || m_doc->spriteDef(n).isChild();
        anyVariantArt = anyVariantArt || m_doc->hasVariantArt(n, m_doc->variant());
    }
    const bool busy = m_doc->isSaving();
    m_saveAct->setEnabled(!busy);
    m_saveAsAct->setEnabled(!busy);
    m_deleteAct->setEnabled(any);
    m_renameAct->setEnabled(sel.size() == 1);
    m_newChildAct->setEnabled(inAtlas);
    m_sliceAct->setEnabled(inAtlas);
    m_bakeAct->setEnabled(anyChild);
    m_pinAct->setEnabled(any);
    m_replaceImageAct->setEnabled(image);
    m_saveImageAct->setEnabled(sel.size() == 1 && (inAtlas || image));
    m_replaceVariantAct->setVisible(variant);
    m_removeVariantAct->setVisible(variant);
    m_replaceVariantAct->setEnabled(image);
    m_removeVariantAct->setEnabled(anyVariantArt);
    m_replaceVariantAct->setText(variant ? tr("Replace %1 Art…").arg(m_doc->variant()) : tr("Replace &Variant Art…"));
    m_removeVariantAct->setText(variant ? tr("Remove %1 Art").arg(m_doc->variant()) : tr("Remove Variant &Art"));
    m_extractAct->setEnabled(!m_doc->project().images.empty());
    m_editModeAct->setEnabled(inAtlas || m_panel->isEditMode());
    m_exportAllAct->setEnabled(!m_doc->project().variants.empty());
}

void MainWindow::updateStatus()
{
    const atlas::Project& p = m_doc->project();
    QString store = tr("embedded · %n image(s)", nullptr, int(p.images.size()));
    if (const atlas::Variant* v = m_doc->currentVariant()) store += tr(" · %1: %2 own").arg(qs(v->id)).arg(v->images.size());
    m_statusStore->setText(store);
    m_statusStore->setToolTip(tr("The images live in the packed atlas Save writes: %1").arg(QDir::toNativeSeparators(m_doc->storeFile())));
    const BuildSnapshotPtr snap = m_doc->snapshot();
    if (m_doc->isBuilding()) m_statusBuild->setText(tr("Building…"));
    else if (snap) m_statusBuild->setText(tr("Built in %1 ms").arg(snap->elapsedMs));
    if (!snap) {
        for (QLabel* l : {m_statusPages, m_statusOccupancy, m_statusSprites, m_statusProblems}) l->clear();
        return;
    }
    const atlas::BuildResult& b = snap->result;
    const int page = m_panel->atlasCanvas()->page();
    QString pages = tr("%n page(s)", nullptr, int(b.pages.size()));
    if (page < int(b.pages.size())) pages += QStringLiteral("  ·  %1 × %2").arg(b.pages[size_t(page)].image.w).arg(b.pages[size_t(page)].image.h);
    m_statusPages->setText(pages);
    // Occupancy: pixels that sprites own (aliases and plain children share someone else's).
    double used = 0, total = 0;
    for (const atlas::Page& pg : b.pages) total += double(pg.image.w) * pg.image.h;
    for (const atlas::Region& r : b.regions)
        if (r.aliasOf.empty() && (!r.child || r.baked)) used += double(r.frame.w) * r.frame.h;
    m_statusOccupancy->setText(total > 0 ? tr("%1% used").arg(used * 100 / total, 0, 'f', 1) : QString());
    m_statusSprites->setText(tr("%n sprite(s)", nullptr, int(b.regions.size())));
    const int e = b.errorCount(), w = b.warningCount();
    m_statusProblems->setText(e || w ? tr("%1 error(s), %2 warning(s)").arg(e).arg(w) : tr("no problems"));
    m_statusProblems->setStyleSheet(e ? QStringLiteral("color: #f04a5a;") : w ? QStringLiteral("color: #e0a82e;") : QString());
}

void MainWindow::refreshVariants()
{
    const QSignalBlocker block(m_variantCombo);
    m_variantCombo->clear();
    m_variantCombo->addItem(tr("Base"), QString());
    for (const atlas::Variant& v : m_doc->project().variants) m_variantCombo->addItem(qs(v.id), qs(v.id));
    m_variantCombo->setCurrentIndex(std::max(0, m_variantCombo->findData(m_doc->variant())));
    m_variantCombo->setEnabled(m_variantCombo->count() > 1);
}

// ---- events -------------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    writeSettings();
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls()) {
        const QFileInfo fi(u.toLocalFile());
        const QString ext = fi.suffix().toLower();
        if (fi.isDir() || ext == QLatin1String("png") || ext == QLatin1String("atlasproj")) {
            e->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) {
        const QFileInfo fi(u.toLocalFile());
        const QString ext = fi.suffix().toLower();
        if (ext == QLatin1String("atlasproj")) {
            e->acceptProposedAction();
            if (maybeSave()) openProject(fi.absoluteFilePath());
            return;   // a project replaces everything else dropped with it
        }
        if (fi.isDir() || ext == QLatin1String("png")) paths << fi.absoluteFilePath();
    }
    e->acceptProposedAction();
    // PNGs are named by their file name, PNGs in a dropped folder by their path below it.
    if (!paths.isEmpty()) addImagePaths(paths);
}
