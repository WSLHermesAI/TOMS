#include "AnimEditor.h"

#include "AnimAtlasesPanel.h"
#include "AnimClipsDock.h"
#include "AutoBackup.h"
#include "AnimDocument.h"
#include "AnimKeyListDock.h"
#include "AnimNodesDock.h"
#include "AnimProblemsDock.h"
#include "AnimPropertiesDock.h"
#include "AnimSpriteDrop.h"
#include "AnimSpritesDock.h"
#include "AnimTransport.h"
#include "AnimViewport.h"
#include "Icons.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>

using toms::anim::Node;

namespace {

constexpr int kMaxRecent = 10;

QAction* makeAction(QObject* parent, const QString& text, const QKeySequence& key = QKeySequence(), int icon = -1)
{
    auto* a = new QAction(text, parent);
    if (!key.isEmpty()) a->setShortcut(key);
    if (icon >= 0) a->setIcon(Icons::icon(Icons::Id(icon)));
    return a;
}

bool selftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }

}  // namespace

QSettings AnimEditor::settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("AnimEditor")); }

AnimEditor::AnimEditor(QMainWindow* host)
    : QObject(host)
    , m_host(host)
    , m_doc(new AnimDocument(this))
    , m_viewport(new AnimViewport(m_doc, GameCanvasView::gameRendererWanted(QStringLiteral("AnimEditor")), host))
{
    m_transport = new AnimTransport(m_doc, host);
    createActions();
    createDocks();
    createMenus();
    createToolBars();
    m_keys->setSpriteSource([this] { return m_sprites->selectedSprites(); });

    connect(m_doc, &AnimDocument::dirtyChanged, this, &AnimEditor::titleChanged);
    connect(m_doc, &AnimDocument::filePathChanged, this, &AnimEditor::titleChanged);
    connect(m_doc, &AnimDocument::selectionChanged, this, &AnimEditor::updateActions);
    connect(m_doc, &AnimDocument::fileChanged, this, &AnimEditor::updateActions);
    connect(m_doc, &AnimDocument::atlasChanged, this, &AnimEditor::updateActions);
    connect(m_doc, &AnimDocument::message, this, [this](const QString& m) { m_host->statusBar()->showMessage(m, 6000); });
    connect(m_transport, &AnimTransport::eventsFired, this, [this](const QStringList& names, float t) {
        const QString text = tr("event %1 at %2 s").arg(names.join(QStringLiteral(", "))).arg(t, 0, 'f', 3);
        m_host->statusBar()->showMessage(text, 3000);
        m_problems->appendLog(text);
    });
    connect(m_viewport, &AnimViewport::toolChanged, this, [this](AnimViewport::Tool t) {
        (t == AnimViewport::Tool::Move ? m_moveAct : t == AnimViewport::Tool::Rotate ? m_rotateAct : m_scaleAct)->setChecked(true);
    });
    connect(m_doc, &AnimDocument::autoKeyChanged, m_autoKeyAct, &QAction::setChecked);
    updateActions();
}

QString AnimEditor::title() const { return m_doc->displayName() + QStringLiteral("[*]"); }

QList<QDockWidget*> AnimEditor::docks() const
{
    return {m_clips, m_nodes, m_sprites, m_props, m_history, m_keys, m_events, m_timeline, m_problems};
}

// ---- construction -------------------------------------------------------------------------------

void AnimEditor::createActions()
{
    using Id = Icons::Id;
    m_newAct = makeAction(this, tr("&New"), QKeySequence::New, int(Id::New));
    m_openAct = makeAction(this, tr("&Open…"), QKeySequence::Open, int(Id::Open));
    m_saveAct = makeAction(this, tr("&Save"), QKeySequence::Save, int(Id::Save));
    m_saveAsAct = makeAction(this, tr("Save &As…"), QKeySequence::SaveAs);
    m_addAtlasAct = makeAction(this, tr("Add &Atlas…"), QKeySequence(), int(Id::Grid));
    m_addAtlasAct->setToolTip(tr("Add an .atlas the sprites come from (Properties > Atlases; stored relative to the .anim)"));
    m_reloadAtlasAct = makeAction(this, tr("&Reload Atlases"), QKeySequence(tr("Ctrl+Shift+R")));
    m_quitAct = makeAction(this, tr("&Quit"), QKeySequence::Quit);
    connect(m_newAct, &QAction::triggered, this, [this] {
        if (maybeSave()) m_doc->newFile();
    });
    connect(m_openAct, &QAction::triggered, this, [this] {
        if (!maybeSave()) return;
        const QString path = QFileDialog::getOpenFileName(m_host, tr("Open Animation"), lastDir(), tr("Animations (*.anim);;All files (*)"));
        if (!path.isEmpty()) openFile(path);
    });
    connect(m_saveAct, &QAction::triggered, this, &AnimEditor::save);
    connect(m_saveAsAct, &QAction::triggered, this, [this] { saveAs(); });
    connect(m_addAtlasAct, &QAction::triggered, this, &AnimEditor::onAddAtlas);
    connect(m_reloadAtlasAct, &QAction::triggered, m_doc, &AnimDocument::reloadAtlases);
    connect(m_quitAct, &QAction::triggered, m_host, &QWidget::close);

    m_undoAct = m_doc->undoStack()->createUndoAction(this, tr("&Undo"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_undoAct->setIcon(Icons::icon(Id::Back));
    m_redoAct = m_doc->undoStack()->createRedoAction(this, tr("&Redo"));
    m_redoAct->setShortcut(QKeySequence::Redo);
    m_redoAct->setIcon(Icons::icon(Id::Forward));

    m_addChildAct = makeAction(this, tr("Add &Child Node"), QKeySequence(tr("Ctrl+Shift+N")), int(Id::Node));
    m_addChildAct->setToolTip(tr("A group node under the selected node"));
    m_addSiblingAct = makeAction(this, tr("Add &Sibling Node"), QKeySequence(), int(Id::Add));
    m_addSpriteAct = makeAction(this, tr("Add S&prite Node"), QKeySequence(tr("Ctrl+Shift+A")), int(Id::SpriteNode));
    m_addSpriteAct->setToolTip(tr("A node with the sprite picked in the Sprites dock, under the selected node"));
    m_dupAct = makeAction(this, tr("&Duplicate Node"), QKeySequence(tr("Ctrl+D")), int(Id::Duplicate));
    m_deleteAct = makeAction(this, tr("D&elete Node"), QKeySequence::Delete, int(Id::Remove));
    m_renameAct = makeAction(this, tr("Re&name Node"), QKeySequence(Qt::Key_F2), int(Id::Rename));
    m_upAct = makeAction(this, tr("Move &Up"), QKeySequence(tr("Ctrl+Up")), int(Id::Up));
    m_downAct = makeAction(this, tr("Move Do&wn"), QKeySequence(tr("Ctrl+Down")), int(Id::Down));
    connect(m_addChildAct, &QAction::triggered, this, [this] {
        Node n;
        n.name = "node";
        m_doc->addNode(m_doc->selectedPath(), n, tr("Add node"));
    });
    connect(m_addSiblingAct, &QAction::triggered, this, [this] {
        NodePath p = m_doc->selectedPath();
        if (!p.empty()) p.pop_back();
        Node n;
        n.name = "node";
        m_doc->addNode(p, n, tr("Add node"));
    });
    connect(m_addSpriteAct, &QAction::triggered, this, &AnimEditor::addSpriteNode);
    connect(m_dupAct, &QAction::triggered, this, [this] { m_doc->duplicateNode(m_doc->selectedPath()); });
    connect(m_deleteAct, &QAction::triggered, this, [this] { m_doc->deleteNode(m_doc->selectedPath()); });
    connect(m_upAct, &QAction::triggered, this, [this] { moveSelected(-1); });
    connect(m_downAct, &QAction::triggered, this, [this] { moveSelected(1); });

    m_moveAct = makeAction(this, tr("&Move Tool"), QKeySequence(Qt::Key_W), int(Id::Move));
    m_rotateAct = makeAction(this, tr("&Rotate Tool"), QKeySequence(Qt::Key_E), int(Id::Rotate));
    m_scaleAct = makeAction(this, tr("&Scale Tool"), QKeySequence(Qt::Key_R), int(Id::Scale));
    auto* tools = new QActionGroup(this);
    for (QAction* a : {m_moveAct, m_rotateAct, m_scaleAct}) {
        a->setCheckable(true);
        tools->addAction(a);
    }
    m_moveAct->setChecked(true);
    m_moveAct->setToolTip(tr("Move (W): drag the arrows, the centre box or a sprite; Ctrl snaps to pixels"));
    m_rotateAct->setToolTip(tr("Rotate (E): drag the ring; Shift snaps to 15°"));
    m_scaleAct->setToolTip(tr("Scale (R): drag the axis handles (Shift keeps proportions) or the centre box"));
    connect(m_moveAct, &QAction::triggered, this, [this] { m_viewport->setTool(AnimViewport::Tool::Move); });
    connect(m_rotateAct, &QAction::triggered, this, [this] { m_viewport->setTool(AnimViewport::Tool::Rotate); });
    connect(m_scaleAct, &QAction::triggered, this, [this] { m_viewport->setTool(AnimViewport::Tool::Scale); });
    m_autoKeyAct = makeAction(this, tr("&Auto-Key"), QKeySequence(Qt::Key_N), int(Id::AutoKey));
    m_autoKeyAct->setCheckable(true);
    m_autoKeyAct->setChecked(m_doc->autoKey());
    m_autoKeyAct->setToolTip(tr("Auto-key (N): edits write a key at the playhead; off, they change the rest values"));
    connect(m_autoKeyAct, &QAction::toggled, m_doc, &AnimDocument::setAutoKey);
    m_snapAct = makeAction(this, tr("Pixel S&nap"), QKeySequence(), int(Id::Snap));
    m_snapAct->setCheckable(true);
    m_snapAct->setToolTip(tr("Snap moves to whole pixels, rotation to degrees, scale to 0.05"));
    connect(m_snapAct, &QAction::toggled, m_viewport, &AnimViewport::setSnap);
    m_gridAct = makeAction(this, tr("Show &Grid"), QKeySequence(tr("G")));
    m_gridAct->setCheckable(true);
    m_gridAct->setChecked(true);
    connect(m_gridAct, &QAction::toggled, m_viewport, &AnimViewport::setShowGrid);
    m_insertKeyAct = makeAction(this, tr("&Insert Key at Playhead"), QKeySequence(Qt::Key_K), int(Id::KeyOn));
    m_qualifyAct = makeAction(this, tr("&Qualify Sprite References"));
    m_qualifyAct->setToolTip(tr("Every bare sprite name in the file (every clip) -> id:name with the atlas the lookup picks now"));
    m_qualifyAct->setStatusTip(m_qualifyAct->toolTip());
    connect(m_qualifyAct, &QAction::triggered, m_doc, &AnimDocument::qualifySpriteReferences);

    m_fitAct = makeAction(this, tr("&Fit"), QKeySequence(Qt::Key_F), int(Id::Fit));
    m_zoomInAct = makeAction(this, tr("Zoom &In"), QKeySequence::ZoomIn, int(Id::ZoomIn));
    m_zoomOutAct = makeAction(this, tr("Zoom &Out"), QKeySequence::ZoomOut, int(Id::ZoomOut));
    m_frameAct = makeAction(this, tr("Frame &Clip"), QKeySequence(tr("Shift+F")));
    m_frameAct->setToolTip(tr("Fit every pose of the clip"));
    connect(m_fitAct, &QAction::triggered, m_viewport, &CanvasView::fitToView);
    connect(m_zoomInAct, &QAction::triggered, m_viewport, &CanvasView::zoomIn);
    connect(m_zoomOutAct, &QAction::triggered, m_viewport, &CanvasView::zoomOut);
    connect(m_frameAct, &QAction::triggered, m_viewport, &AnimViewport::frameClip);

    m_helpCliAct = makeAction(this, tr("&Command Line"));
    m_aboutAct = makeAction(this, tr("&About Anim Editor"));
    connect(m_helpCliAct, &QAction::triggered, this, [this] {
        QMessageBox::information(m_host, tr("Command Line"),
                                 tr("anim_editor [file.anim]\n"
                                    "anim_editor --headless check <file.anim> [--atlas [<id>=]<file.atlas>]...\n"
                                    "    parse, check sprite names against the atlases and the keys; exit 0 = ok, 2 = errors\n"
                                    "    (--atlas, repeatable, replaces the file's atlas list; id = the file name unless given)\n"
                                    "anim_editor --selftest <file.anim> <outdir>\n"
                                    "    automated check (run with -platform offscreen)"));
    });
    connect(m_aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(m_host, tr("Anim Editor"),
                           tr("<b>Anim Editor</b><p>Node animations (.anim) on the packed sprite atlas for TOMS. "
                              "The preview uses the game's own evaluate() and appendQuads().</p><p>docs/15_ANIMATION.md</p>"));
    });
}

void AnimEditor::createDocks()
{
    m_clips = new AnimClipsDock(m_doc, m_host);
    m_nodes = new AnimNodesDock(m_doc, m_host);
    m_sprites = new AnimSpritesDock(m_doc, m_host);
    m_props = new AnimPropertiesDock(m_doc, m_host);
    m_keys = new AnimKeyListDock(m_doc, m_host);
    m_events = new AnimEventsDock(m_doc, m_host);
    m_problems = new AnimProblemsDock(m_doc, m_host);
    m_history = createHistoryDock(m_doc->undoStack(), m_host);
    m_backup = new AutoBackup(m_doc->undoStack(), QStringLiteral(".anim"), [this] { return m_doc->filePath(); },
                              [this](const QString& p, QString* e) { return m_doc->writeBackup(p, e); }, this);
    connect(m_backup, &AutoBackup::message, this, [this](const QString& t) { m_host->statusBar()->showMessage(t, 5000); });
    m_timeline = new QDockWidget(tr("Timeline"), m_host);
    m_timeline->setObjectName(QStringLiteral("AnimTimelineDock"));
    auto* placeholder = new QLabel(tr("The multi-track timeline comes in Phase 4.\n"
                                      "Until then: the scrubber in the transport bar, and the Keys list.\n\n"
                                      "Drop sprites here (or on the scrubber / the Keys list) to insert them\n"
                                      "as sprite keys, one after another, on the selected node."),
                                   m_timeline);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setForegroundRole(QPalette::PlaceholderText);
    m_timeline->setWidget(placeholder);

    // Several sprites dragged onto a time area: a sprite sequence (MPDI style).
    const auto onDrop = [this](const QStringList& refs, float t) { insertSpriteKeys(refs, t); };
    connect(new AnimSpriteDrop(placeholder, [this](const QPoint&) { return m_doc->time(); }),
            &AnimSpriteDrop::dropped, this, onDrop);
    AnimScrubber* scrubber = m_transport->scrubber();
    connect(new AnimSpriteDrop(scrubber, [scrubber](const QPoint& p) { return scrubber->timeAt(p.x()); }),
            &AnimSpriteDrop::dropped, this, onDrop);
    QTableWidget* keyTable = m_keys->table();
    connect(new AnimSpriteDrop(keyTable->viewport(), [this, keyTable](const QPoint& p) {
                const int row = keyTable->rowAt(p.y());
                const std::vector<float> times = m_keys->rowTimes();
                return row >= 0 && row < int(times.size()) ? times[size_t(row)] : m_doc->time();
            }),
            &AnimSpriteDrop::dropped, this, onDrop);

    QToolBar* nt = m_nodes->toolBar();
    for (QAction* a : {m_addChildAct, m_addSpriteAct, m_dupAct, m_deleteAct, m_upAct, m_downAct}) nt->addAction(a);
    connect(m_nodes, &AnimNodesDock::addSpriteNodeRequested, this, &AnimEditor::addSpriteNode);
    connect(m_props->atlasesPanel(), &AnimAtlasesPanel::addRequested, this, &AnimEditor::onAddAtlas);
    connect(m_renameAct, &QAction::triggered, m_nodes, &AnimNodesDock::beginRename);
    connect(m_insertKeyAct, &QAction::triggered, m_keys, &AnimKeyListDock::insertKey);
    // Delete / F2 mean "this node" only where nodes have the focus; the key list has its own Delete.
    for (QAction* a : {m_deleteAct, m_renameAct}) {
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_nodes->addAction(a);
        m_viewport->addAction(a);
    }
    auto* delKeys = new QAction(tr("Delete Keys"), m_keys);
    delKeys->setShortcut(QKeySequence::Delete);
    delKeys->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(delKeys, &QAction::triggered, m_keys, [this] { m_keys->deleteRows(false); });
    m_keys->addAction(delKeys);
}

void AnimEditor::createMenus()
{
    QMenu* file = new QMenu(tr("&File"), m_host);
    file->addAction(m_newAct);
    file->addAction(m_openAct);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &AnimEditor::rebuildRecentMenu);
    file->addAction(m_saveAct);
    file->addAction(m_saveAsAct);
    file->addSeparator();
    file->addAction(m_addAtlasAct);
    file->addAction(m_reloadAtlasAct);
    file->addSeparator();
    m_backup->addMenu(file, m_host);
    file->addSeparator();
    file->addAction(m_quitAct);

    QMenu* edit = new QMenu(tr("&Edit"), m_host);
    edit->addAction(m_undoAct);
    edit->addAction(m_redoAct);
    edit->addSeparator();
    edit->addAction(m_moveAct);
    edit->addAction(m_rotateAct);
    edit->addAction(m_scaleAct);
    edit->addSeparator();
    edit->addAction(m_autoKeyAct);
    edit->addAction(m_snapAct);

    QMenu* node = new QMenu(tr("&Node"), m_host);
    for (QAction* a : {m_addChildAct, m_addSiblingAct, m_addSpriteAct, m_dupAct}) node->addAction(a);
    node->addSeparator();
    node->addAction(m_renameAct);
    node->addAction(m_upAct);
    node->addAction(m_downAct);
    node->addSeparator();
    node->addAction(m_deleteAct);

    QMenu* key = new QMenu(tr("&Key"), m_host);
    key->addAction(m_insertKeyAct);
    key->addAction(tr("Delete Selected Rows"), m_keys, [this] { m_keys->deleteRows(false); });
    key->addAction(tr("Delete Current Column's Keys"), m_keys, [this] { m_keys->deleteRows(true); });
    key->addSeparator();
    key->addAction(tr("Set Time…"), m_keys, &AnimKeyListDock::setTimeDialog);
    key->addAction(tr("Even Spacing"), m_keys, &AnimKeyListDock::evenSpacing);
    key->addAction(tr("Rescale…"), m_keys, &AnimKeyListDock::rescaleDialog);
    key->addAction(tr("Ramp…"), m_keys, &AnimKeyListDock::rampDialog);
    key->addAction(tr("Fade In"), m_keys, &AnimKeyListDock::fadeIn);
    key->addAction(tr("Fade Out"), m_keys, &AnimKeyListDock::fadeOut);
    key->addAction(tr("Apply Sprite Sequence"), m_keys, &AnimKeyListDock::spriteSequence);
    key->addAction(tr("Set Ease…"), m_keys, &AnimKeyListDock::setEaseDialog);
    key->addSeparator();
    key->addAction(m_qualifyAct);

    QMenu* play = new QMenu(tr("&Playback"), m_host);
    play->addAction(m_transport->playAction());
    play->addAction(m_transport->stopAction());
    play->addAction(m_transport->loopAction());
    play->addSeparator();
    play->addAction(tr("Previous Key"), QKeySequence(Qt::Key_Comma), m_transport, [this] { m_transport->stepKey(-1); });
    play->addAction(tr("Next Key"), QKeySequence(Qt::Key_Period), m_transport, [this] { m_transport->stepKey(1); });

    m_viewMenu = new QMenu(tr("&View"), m_host);
    {   // the viewport's renderer: the game's (bgfx, the same pixels as toms_game) or QPainter
        QAction* a = m_viewMenu->addAction(tr("Preview with the Game Renderer (after a restart)"));
        a->setCheckable(true);
        a->setChecked(settings().value(QStringLiteral("preview/gameRenderer"), true).toBool());
        a->setToolTip(tr("Draw the viewport with toms_game's renderer (bgfx): exactly the game's pixels. Off: QPainter."));
        connect(a, &QAction::toggled, this, [this](bool on) {
            settings().setValue(QStringLiteral("preview/gameRenderer"), on);
            m_host->statusBar()->showMessage(tr("The renderer changes the next time the editor starts."), 8000);
        });
        m_viewMenu->addSeparator();
    }
    m_viewMenu->addAction(m_fitAct);
    m_viewMenu->addAction(m_frameAct);
    m_viewMenu->addAction(m_zoomInAct);
    m_viewMenu->addAction(m_zoomOutAct);
    m_viewMenu->addAction(m_gridAct);
    CanvasView::addCoordinatesAction(m_viewMenu);
    m_viewMenu->addSeparator();
    for (QDockWidget* d : docks()) m_viewMenu->addAction(d->toggleViewAction());
    m_viewMenu->addSeparator();

    QMenu* help = new QMenu(tr("&Help"), m_host);
    help->addAction(m_helpCliAct);
    help->addAction(m_aboutAct);
    m_menus = {file, edit, node, key, play, m_viewMenu, help};
}

void AnimEditor::createToolBars()
{
    m_mainBar = new QToolBar(tr("Main"), m_host);
    m_mainBar->setObjectName(QStringLiteral("AnimMainToolBar"));
    m_mainBar->setIconSize(QSize(20, 20));
    m_mainBar->setMovable(false);
    m_mainBar->addAction(m_newAct);
    m_mainBar->addAction(m_openAct);
    m_mainBar->addAction(m_saveAct);
    m_mainBar->addSeparator();
    m_mainBar->addAction(m_undoAct);
    m_mainBar->addAction(m_redoAct);
    m_mainBar->addSeparator();
    m_mainBar->addAction(m_moveAct);
    m_mainBar->addAction(m_rotateAct);
    m_mainBar->addAction(m_scaleAct);
    m_mainBar->addSeparator();
    m_mainBar->addAction(m_autoKeyAct);
    if (auto* b = m_mainBar->widgetForAction(m_autoKeyAct)) b->setObjectName(QStringLiteral("AutoKeyButton"));
    m_mainBar->addAction(m_snapAct);
    m_mainBar->addAction(m_insertKeyAct);
    m_mainBar->addSeparator();
    m_mainBar->addAction(m_zoomOutAct);
    m_mainBar->addAction(m_zoomInAct);
    m_mainBar->addAction(m_fitAct);
    m_mainBar->addSeparator();
    m_mainBar->addAction(m_addAtlasAct);
}

void AnimEditor::install()
{
    m_host->setCentralWidget(m_viewport);
    m_host->setDockNestingEnabled(true);
    for (QMenu* m : m_menus) m_host->menuBar()->addMenu(m);
    m_host->addToolBar(Qt::TopToolBarArea, m_mainBar);
    m_host->addToolBarBreak(Qt::TopToolBarArea);
    m_host->addToolBar(Qt::TopToolBarArea, m_transport->toolBar());

    m_host->addDockWidget(Qt::LeftDockWidgetArea, m_clips);
    m_host->splitDockWidget(m_clips, m_nodes, Qt::Vertical);
    m_host->splitDockWidget(m_nodes, m_sprites, Qt::Vertical);
    m_host->addDockWidget(Qt::RightDockWidgetArea, m_props);
    m_host->tabifyDockWidget(m_props, m_history);
    m_props->raise();
    m_host->addDockWidget(Qt::BottomDockWidgetArea, m_keys);
    m_host->tabifyDockWidget(m_keys, m_events);
    m_host->tabifyDockWidget(m_events, m_timeline);
    m_host->tabifyDockWidget(m_timeline, m_problems);
    m_keys->raise();
    m_host->resizeDocks({m_clips, m_props}, {260, 300}, Qt::Horizontal);
    m_host->resizeDocks({m_clips, m_nodes, m_sprites}, {150, 260, 260}, Qt::Vertical);
    m_host->resizeDocks({m_keys}, {230}, Qt::Vertical);
    emit titleChanged();
}

// ---- settings / recent files --------------------------------------------------------------------

void AnimEditor::readSettings()
{
    QSettings s = settings();
    m_doc->setAutoKey(s.value(QStringLiteral("autoKey"), true).toBool());
    m_autoKeyAct->setChecked(m_doc->autoKey());
    m_snapAct->setChecked(s.value(QStringLiteral("snap"), false).toBool());
    m_gridAct->setChecked(s.value(QStringLiteral("grid"), true).toBool());
    m_transport->loopAction()->setChecked(s.value(QStringLiteral("loopPreview"), false).toBool());
}

void AnimEditor::writeSettings() const
{
    QSettings s = settings();
    s.setValue(QStringLiteral("autoKey"), m_doc->autoKey());
    s.setValue(QStringLiteral("snap"), m_snapAct->isChecked());
    s.setValue(QStringLiteral("grid"), m_gridAct->isChecked());
    s.setValue(QStringLiteral("loopPreview"), m_transport->loopAction()->isChecked());
}

QString AnimEditor::lastDir() const
{
    if (!m_doc->filePath().isEmpty()) return QFileInfo(m_doc->filePath()).absolutePath();
    return settings().value(QStringLiteral("lastDir"), QDir::homePath()).toString();
}

void AnimEditor::addRecentFile(const QString& path)
{
    if (selftestRunning()) return;   // the selftest's scratch files do not belong in the list
    QSettings s = settings();
    QStringList recent = s.value(QStringLiteral("recentFiles")).toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    recent.removeAll(abs);
    recent.prepend(abs);
    while (recent.size() > kMaxRecent) recent.removeLast();
    s.setValue(QStringLiteral("recentFiles"), recent);
    s.setValue(QStringLiteral("lastDir"), QFileInfo(abs).absolutePath());
}

void AnimEditor::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = settings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& path : recent) {
        QAction* a = m_recentMenu->addAction(QDir::toNativeSeparators(path));
        a->setEnabled(QFileInfo::exists(path));
        connect(a, &QAction::triggered, this, [this, path] {
            if (maybeSave()) openFile(path);
        });
    }
    if (recent.isEmpty()) m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
    else {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(tr("Clear List"), this, [] { settings().remove(QStringLiteral("recentFiles")); });
    }
}

// ---- files --------------------------------------------------------------------------------------

bool AnimEditor::openFile(const QString& path, bool askForAtlas)
{
    QString err;
    if (!m_doc->open(path, &err)) {
        m_problems->appendLog(tr("Could not open %1: %2").arg(QDir::toNativeSeparators(path), err));
        if (!selftestRunning())
            QMessageBox::warning(m_host, tr("Open Animation"), tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    QStringList atlases, failed;
    for (int i = 0; i < m_doc->atlasCount(); i++) {
        atlases << QDir::toNativeSeparators(m_doc->atlasAt(i).path);
        if (!m_doc->atlasAt(i).ok) failed << m_doc->atlasAt(i).error;
    }
    m_problems->appendLog(tr("Opened %1 (%2 clip(s), atlases: %3)")
                              .arg(QDir::toNativeSeparators(path))
                              .arg(m_doc->file().clips.size())
                              .arg(atlases.isEmpty() ? tr("none") : atlases.join(QStringLiteral(", "))));
    addRecentFile(path);
    if (askForAtlas && !selftestRunning()) {
        if (m_doc->atlasCount() == 0) {
            if (QMessageBox::question(m_host, tr("Atlases"), tr("The file names no atlas.\n\nAdd the .atlas its sprites come from now?")) ==
                QMessageBox::Yes)
                onAddAtlas();
        } else if (!failed.isEmpty()) {
            QMessageBox::warning(m_host, tr("Atlases"),
                                 tr("%n atlas(es) could not be loaded:\n\n%1\n\nFix the list under Properties > Atlases "
                                    "(Reload, or Remove and Add).", nullptr, int(failed.size()))
                                     .arg(failed.join(QStringLiteral("\n"))));
        }
    }
    return true;
}

bool AnimEditor::maybeSave()
{
    if (!m_doc->isDirty()) return true;
    const auto r = QMessageBox::warning(m_host, tr("Unsaved Changes"), tr("%1 has unsaved changes. Save them?").arg(m_doc->displayName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

bool AnimEditor::save()
{
    if (m_doc->filePath().isEmpty()) return saveAs();
    return saveTo(m_doc->filePath());
}

bool AnimEditor::saveAs(const QString& startDir)
{
    QString start = m_doc->filePath().isEmpty() ? QDir(lastDir()).filePath(QStringLiteral("untitled.anim")) : m_doc->filePath();
    if (!startDir.isEmpty()) start = QDir(startDir).filePath(QFileInfo(start).fileName());
    QString path = QFileDialog::getSaveFileName(m_host, tr("Save Animation As"), start, tr("Animations (*.anim)"));
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".anim");
    return saveTo(path);
}

bool AnimEditor::saveTo(const QString& path)
{
    QString err;
    if (!m_doc->save(path, &err)) {
        m_problems->appendLog(tr("Save failed: %1").arg(err));
        if (!selftestRunning()) QMessageBox::warning(m_host, tr("Save"), tr("Could not save %1:\n\n%2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    addRecentFile(path);
    return true;
}

void AnimEditor::onAddAtlas()
{
    const QString start = m_doc->atlasCount() ? m_doc->atlasAt(m_doc->atlasCount() - 1).path : lastDir();
    const QStringList paths =
        QFileDialog::getOpenFileNames(m_host, tr("Add Atlas"), start, tr("Sprite atlases (*.atlas);;All files (*)"));
    for (const QString& path : paths) {
        const QString abs = QFileInfo(path).absoluteFilePath();
        bool have = false;
        for (int a = 0; a < m_doc->atlasCount() && !have; a++)
            have = QFileInfo(m_doc->atlasAt(a).path).absoluteFilePath().compare(abs, Qt::CaseInsensitive) == 0;
        if (have) {
            m_problems->appendLog(tr("Add atlas %1: already added").arg(QDir::toNativeSeparators(path)));
            continue;
        }
        // The first one may ask to save an untitled file; cancelling that stops the rest too.
        if (!addAtlasFile(path) && m_doc->filePath().isEmpty()) break;
    }
}

bool AnimEditor::insertSpriteKeys(const QStringList& refs, float time)
{
    if (refs.isEmpty()) return false;
    const toms::anim::Clip* clip = m_doc->clip();
    const Node* node = m_doc->selectedNode();
    if (!clip || !node) {
        m_doc->message(tr("Select a node first: dropped sprites become sprite keys of the selected node."));
        return false;
    }
    QSettings s = settings();
    double gap = selftestRunning() ? 0.1 : s.value(QStringLiteral("spriteKeyGap"), 0.1).toDouble();
    double start = std::max(0.0f, time);
    bool extend = true;
    if (!selftestRunning()) {
        QDialog dlg(m_host);
        dlg.setWindowTitle(tr("Insert Sprite Keys"));
        auto* intro = new QLabel(tr("Insert %n sprite key(s) on node '%1'?", nullptr, int(refs.size()))
                                     .arg(QString::fromStdString(node->name)), &dlg);
        auto* startSpin = new QDoubleSpinBox(&dlg);
        startSpin->setRange(0, 100000);
        startSpin->setDecimals(3);
        startSpin->setSingleStep(0.1);
        startSpin->setSuffix(tr(" s"));
        startSpin->setValue(start);
        auto* gapSpin = new QDoubleSpinBox(&dlg);
        gapSpin->setRange(0.001, 1000);
        gapSpin->setDecimals(3);
        gapSpin->setSingleStep(0.01);
        gapSpin->setSuffix(tr(" s"));
        gapSpin->setValue(gap);
        auto* summary = new QLabel(&dlg);
        auto* extendBox = new QCheckBox(&dlg);
        extendBox->setChecked(true);
        const auto update = [&] {
            const double first = startSpin->value(), step = gapSpin->value();
            const double last = first + step * double(refs.size() - 1);
            int replaced = 0;
            for (float k : animed::keyTimes(*node, animed::Channel::Sprite))
                for (int i = 0; i < refs.size(); i++)
                    if (std::fabs(k - (first + step * i)) < 1e-4) replaced++;
            QString text = tr("Keys at %1 ... %2 s (%3 per second).")
                               .arg(first, 0, 'f', 3)
                               .arg(last, 0, 'f', 3)
                               .arg(1.0 / step, 0, 'g', 4);
            if (replaced)
                text += QStringLiteral("\n") + tr("%n existing sprite key(s) at those times will be replaced.", nullptr, replaced);
            summary->setText(text);
            extendBox->setVisible(clip->length > 0 && last > clip->length + 1e-4);
            extendBox->setText(tr("Extend the clip length from %1 to %2 s").arg(clip->length, 0, 'f', 3).arg(last, 0, 'f', 3));
        };
        connect(startSpin, &QDoubleSpinBox::valueChanged, &dlg, update);
        connect(gapSpin, &QDoubleSpinBox::valueChanged, &dlg, update);
        update();
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto* form = new QFormLayout;
        form->addRow(tr("Start time"), startSpin);
        form->addRow(tr("Time gap per key"), gapSpin);
        auto* l = new QVBoxLayout(&dlg);
        l->addWidget(intro);
        l->addLayout(form);
        l->addWidget(summary);
        l->addWidget(extendBox);
        l->addWidget(buttons);
        gapSpin->setFocus();
        gapSpin->selectAll();
        if (dlg.exec() != QDialog::Accepted) return false;
        start = startSpin->value();
        gap = gapSpin->value();
        extend = extendBox->isChecked();
        s.setValue(QStringLiteral("spriteKeyGap"), gap);
    }
    if (!m_doc->insertSpriteSequence(float(start), float(gap), refs, extend)) return false;
    m_doc->setTime(float(start));
    return true;
}

bool AnimEditor::addAtlasFile(const QString& path)
{
    if (m_doc->filePath().isEmpty()) {
        // Atlas paths are stored relative to the .anim: it needs a place first.
        if (selftestRunning()) {
            m_problems->appendLog(tr("Add atlas: save the animation first (atlas paths are stored relative to the .anim file)"));
            return false;
        }
        if (QMessageBox::information(m_host, tr("Add Atlas"),
                                     tr("Save the animation first: atlas paths are stored relative to the .anim file."),
                                     QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) != QMessageBox::Ok ||
            !saveAs(QFileInfo(path).absolutePath()))
            return false;
    }
    // The id its sprites are stored with: the file name, unless another atlas has that already
    // (a style's game.atlas next to the original): then ask, suggesting the style folder's name.
    const QString defaultId = QString::fromStdString(toms::anim::defaultAtlasId(QFileInfo(path).fileName().toStdString()));
    QString id = m_doc->suggestAtlasId(path);
    if (id != defaultId) {
        if (selftestRunning()) {
            m_problems->appendLog(tr("Add atlas %1: the id '%2' is taken, using '%3'").arg(QDir::toNativeSeparators(path), defaultId, id));
        } else {
            for (;;) {
                bool ok = false;
                id = QInputDialog::getText(m_host, tr("Add Atlas"),
                                           tr("Another atlas has the id '%1' already. Sprites from %2 are stored as <id>:name, "
                                              "so it needs its own id:").arg(defaultId, QDir::toNativeSeparators(path)),
                                           QLineEdit::Normal, id, &ok).trimmed();
                if (!ok) return false;
                QString why;
                if (m_doc->atlasIdUsable(id, -1, &why)) break;
                QMessageBox::warning(m_host, tr("Add Atlas"), why);
            }
        }
    }
    QString err;
    if (!m_doc->addAtlas(path, id, &err)) {
        m_problems->appendLog(tr("Add atlas %1: %2").arg(QDir::toNativeSeparators(path), err));
        if (!selftestRunning())
            QMessageBox::warning(m_host, tr("Add Atlas"), tr("Could not add %1:\n\n%2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    return true;
}

// ---- node helpers -------------------------------------------------------------------------------

void AnimEditor::addSpriteNode()
{
    QString sprite = m_sprites->currentSprite();   // "id:name"
    if (sprite.isEmpty()) {
        if (m_doc->spriteChoices().isEmpty()) {
            m_doc->message(tr("No sprites: add an atlas under Properties > Atlases first."));
            return;
        }
        bool ok = false;
        const QString picked = QInputDialog::getItem(m_host, tr("Add Sprite Node"), tr("Sprite"), m_doc->spriteChoices(), 0, true, &ok);
        sprite = QString::fromStdString(m_doc->spriteFromText(picked));
        if (!ok || sprite.isEmpty()) return;
    }
    m_doc->addSpriteNode(m_doc->selectedNode() ? m_doc->selectedPath() : NodePath(), sprite, glm::vec2(0, 0));
}

void AnimEditor::moveSelected(int delta)
{
    const NodePath p = m_doc->selectedPath();
    if (p.empty()) return;
    const NodePath parent(p.begin(), p.end() - 1);
    // moveNode takes the index before the node is taken out: one further when moving down.
    const int index = delta < 0 ? p.back() - 1 : p.back() + 2;
    if (index < 0) return;
    m_doc->moveNode(p, parent, index);
}

void AnimEditor::updateActions()
{
    const bool node = m_doc->selectedNode() != nullptr;
    const bool child = node && !m_doc->selectedPath().empty();
    for (QAction* a : {m_addChildAct, m_addSiblingAct, m_addSpriteAct, m_renameAct, m_insertKeyAct}) a->setEnabled(node);
    m_qualifyAct->setEnabled(m_doc->bareSpriteReferenceCount() > 0);
    for (QAction* a : {m_dupAct, m_deleteAct, m_upAct, m_downAct}) a->setEnabled(child);
}
