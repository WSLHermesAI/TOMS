// editor_window.cpp -- see editor_window.h.
#include "editor_window.h"

#include "bgfx_viewport.h"
#include "mainwindow.h"   // the old stage editor (src/editor/stage/mainwindow.h)

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QTreeWidget>
#include <QUrl>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QApplication>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

EditorWindow::EditorWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("TOMS Editor (Qt6 + bgfx)"));
    resize(1500, 900);

    tabs_ = new QTabWidget(this);
    viewport_ = new BgfxViewport(tabs_);
    tabs_->addTab(viewport_, tr("Play (bgfx)"));

    auto* stageEditor = new MainWindow();          // an existing QMainWindow, hosted as a plain widget
    stageEditor->setWindowFlags(Qt::Widget);
    tabs_->addTab(stageEditor, tr("Stage editor"));
    setCentralWidget(tabs_);

    buildStageDock();
    buildScenarioDock();
    buildSessionDock();
    buildMenus();

    connect(viewport_, &BgfxViewport::statusMessage, this, [this](const QString& t) {
        statusBar()->showMessage(t, 8000);
        rendererLabel_->setText(tr("Renderer: %1").arg(viewport_->rendererName()));
    });
    connect(viewport_, &BgfxViewport::frameInfo, this, [this](double fps, int quads, int calls) {
        statsLabel_->setText(tr("%1 fps · %2 quads · %3 draw calls").arg(fps, 0, 'f', 0).arg(quads).arg(calls));
    });
    connect(viewport_, &BgfxViewport::startFailed, this, [this](const QString& title, const QString& text) {
        QMessageBox::warning(this, title, text + tr("\n\nSee docs/project/04_TROUBLESHOOTING.md"));
    });
    statusBar()->showMessage(tr("Click the game view to give it the keyboard. F1 = debug overlay."));
}

void EditorWindow::runSmokeTest(int frames, const QString& png) {
    viewport_->runSmokeTest(frames, png);
    connect(viewport_, &BgfxViewport::smokeTestDone, this, [this, png] {
        // The whole editor window as the user sees it (Qt widgets + the bgfx view).
        QString windowPng = png;
        windowPng.replace(QStringLiteral(".png"), QStringLiteral("_window.png"));
        // Grab the composed desktop area (window id 0), not the HWND: GDI cannot read a DXGI
        // flip-model swap chain, so grabbing the window itself shows the bgfx view as blank.
        // The Qt widgets as Qt draws them (docks, lists, tabs), whatever covers the window on screen; the bgfx view
        // is blank in it -- the viewport's own PNG above has the game.
        QString widgetsPng = png;
        widgetsPng.replace(QStringLiteral(".png"), QStringLiteral("_widgets.png"));
        grab().save(widgetsPng);
        if (QScreen* s = screen()) {
            const QRect g = frameGeometry();
            s->grabWindow(0, g.x() - s->geometry().x(), g.y() - s->geometry().y(), g.width(), g.height()).save(windowPng);
        }
        QApplication::quit();
    });
}

void EditorWindow::fillStageList() {
    stageList_->clear();
    const QDir stages(QDir(viewport_->assetDir()).filePath(QStringLiteral("../data/stages")));
    for (const QFileInfo& f : stages.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        stageList_->addItem(f.completeBaseName());
}

void EditorWindow::buildStageDock() {
    auto* dock = new QDockWidget(tr("Stages"), this);
    dock->setObjectName(QStringLiteral("StagesDock"));
    stageList_ = new QListWidget(dock);
    fillStageList();
    connect(stageList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        tabs_->setCurrentWidget(viewport_);
        viewport_->loadStage(it->text());
    });
    dock->setWidget(stageList_);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

// tests/scenarios/ of the repository: the assets folder is <repo>/assets/media.
QString EditorWindow::scenarioRoot() const {
    return QDir::cleanPath(QDir(viewport_->assetDir()).filePath(QStringLiteral("../../tests/scenarios")));
}

// One row per scenario folder (a folder holding scenario.json): its name, its scene, and its _comment as the tooltip.
// "_shared" and other folders starting with "_" hold shared files, not scenarios; "_cloud" (downloaded ones, docs/gameplay/09)
// is listed as a group of its own when it is there.
void EditorWindow::fillScenarioList() {
    scenarioList_->clear();
    auto addRoot = [this](const QString& dir, const QString& title) {
        const QDir root(dir);
        if (!root.exists()) return;
        auto* group = new QTreeWidgetItem(scenarioList_, {title});
        group->setToolTip(0, QDir::toNativeSeparators(root.absolutePath()));
        group->setFirstColumnSpanned(true);
        for (const QFileInfo& f : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString file = QDir(f.absoluteFilePath()).filePath(QStringLiteral("scenario.json"));
            if (f.fileName().startsWith(QLatin1Char('_')) || !QFileInfo::exists(file)) continue;
            QFile in(file);
            QJsonObject j;
            if (in.open(QIODevice::ReadOnly)) j = QJsonDocument::fromJson(in.readAll()).object();
            auto* it = new QTreeWidgetItem(group, {f.fileName(), j.value(QStringLiteral("scene")).toString(QStringLiteral("stage"))});
            it->setData(0, Qt::UserRole, f.absoluteFilePath());
            QString tip = j.value(QStringLiteral("_comment")).toString();
            if (j.isEmpty()) tip = tr("scenario.json cannot be read");
            it->setToolTip(0, tip.isEmpty() ? QDir::toNativeSeparators(f.absoluteFilePath()) : tip);
            it->setToolTip(1, it->toolTip(0));
        }
        group->setExpanded(true);
        if (group->childCount() == 0) delete group;
    };
    addRoot(scenarioRoot(), tr("tests/scenarios"));
    addRoot(QDir(scenarioRoot()).filePath(QStringLiteral("_cloud")), tr("downloaded (_cloud)"));
    if (!extraScenarioDir_.isEmpty()) addRoot(extraScenarioDir_, QFileInfo(extraScenarioDir_).fileName());
    if (scenarioList_->topLevelItemCount() == 0) {
        auto* none = new QTreeWidgetItem(scenarioList_, {tr("No scenarios in %1").arg(QDir::toNativeSeparators(scenarioRoot()))});
        none->setFirstColumnSpanned(true);
        none->setFlags(Qt::NoItemFlags);
    }
}

void EditorWindow::playScenario(const QString& path) {
    tabs_->setCurrentWidget(viewport_);
    QString err;
    if (!viewport_->startScenario(path, err))
        QMessageBox::warning(this, tr("The scenario could not start"), err + tr("\n\nSee docs/gameplay/08_TEST_SCENARIOS.md"));
    else
        statusBar()->showMessage(tr("Scenario %1: playing. Restart game (Session) leaves it.").arg(QFileInfo(path).fileName()), 8000);
}

void EditorWindow::buildScenarioDock() {
    auto* dock = new QDockWidget(tr("Scenarios"), this);
    dock->setObjectName(QStringLiteral("ScenariosDock"));
    scenarioList_ = new QTreeWidget(dock);
    scenarioList_->setColumnCount(2);
    scenarioList_->setHeaderLabels({tr("Scenario (double-click to play)"), tr("Scene")});
    scenarioList_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    scenarioList_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    scenarioList_->header()->setStretchLastSection(false);
    scenarioList_->setRootIsDecorated(true);
    scenarioList_->setContextMenuPolicy(Qt::CustomContextMenu);
    fillScenarioList();
    connect(scenarioList_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        const QString path = it->data(0, Qt::UserRole).toString();
        if (!path.isEmpty()) playScenario(path);
    });
    connect(scenarioList_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* it = scenarioList_->itemAt(pos);
        const QString path = it ? it->data(0, Qt::UserRole).toString() : QString();
        QMenu m(this);
        if (!path.isEmpty()) {
            m.addAction(tr("Play"), this, [this, path] { playScenario(path); });
            m.addAction(tr("Open its folder"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
            m.addAction(tr("Edit scenario.json"), this, [path] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(path).filePath(QStringLiteral("scenario.json"))));
            });
            m.addSeparator();
        }
        m.addAction(tr("Reload the list"), this, [this] { fillScenarioList(); });
        m.exec(scenarioList_->viewport()->mapToGlobal(pos));
    });
    dock->setWidget(scenarioList_);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    // under the Stages list, as a second tab of the same dock area
    if (auto* stages = findChild<QDockWidget*>(QStringLiteral("StagesDock"))) tabifyDockWidget(stages, dock);
}

void EditorWindow::buildSessionDock() {
    auto* dock = new QDockWidget(tr("Session"), this);
    dock->setObjectName(QStringLiteral("SessionDock"));
    auto* panel = new QWidget(dock);
    auto* lay = new QVBoxLayout(panel);
    rendererLabel_ = new QLabel(tr("Renderer: (not started)"), panel);
    statsLabel_ = new QLabel(QStringLiteral("-"), panel);
    auto* assets = new QLabel(tr("Assets: %1").arg(QDir::toNativeSeparators(viewport_->assetDir())), panel);
    assets->setWordWrap(true);
    auto* restart = new QPushButton(tr("Restart game"), panel);
    auto* overlay = new QCheckBox(tr("Debug overlay (F1)"), panel);
    auto* stats = new QCheckBox(tr("bgfx stats"), panel);
    lay->addWidget(rendererLabel_);
    lay->addWidget(statsLabel_);
    lay->addWidget(assets);
    lay->addWidget(restart);
    lay->addWidget(overlay);
    lay->addWidget(stats);
    lay->addStretch(1);
    connect(restart, &QPushButton::clicked, this, [this] {
        tabs_->setCurrentWidget(viewport_);
        viewport_->restartGame();
        viewport_->setFocus();
    });
    connect(overlay, &QCheckBox::toggled, viewport_, &BgfxViewport::setDebugOverlay);
    connect(stats, &QCheckBox::toggled, viewport_, &BgfxViewport::setShowStats);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void EditorWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Reload stage list"), this, [this] { fillStageList(); });
    file->addAction(tr("Reload scenario list"), this, [this] { fillScenarioList(); });
    file->addAction(tr("Open scenario folder..."), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("A folder of scenario folders, or one scenario"), scenarioRoot());
        if (dir.isEmpty()) return;
        if (QFileInfo::exists(QDir(dir).filePath(QStringLiteral("scenario.json")))) { playScenario(dir); return; }   // one scenario
        extraScenarioDir_ = dir;
        fillScenarioList();
    });
    file->addSeparator();
    file->addAction(tr("E&xit"), this, &QWidget::close);
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About"), this, [this] {
        QMessageBox::about(this, tr("TOMS Editor"),
            tr("Qt %1 + bgfx (%2)\n\nThe Play tab runs the same game code and renderer as toms_game.exe.\n"
               "Docs: docs/").arg(QString::fromLatin1(qVersion()), viewport_->rendererName()));
    });
}
