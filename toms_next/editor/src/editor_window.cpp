// editor_window.cpp -- see editor_window.h.
#include "editor_window.h"

#include "bgfx_viewport.h"
#include "mainwindow.h"   // the legacy stage editor (TOMS/editor/src/mainwindow.h)

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
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

    auto* legacy = new MainWindow();          // an existing QMainWindow, hosted as a plain widget
    legacy->setWindowFlags(Qt::Widget);
    tabs_->addTab(legacy, tr("Stage editor"));
    setCentralWidget(tabs_);

    buildStageDock();
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
        QMessageBox::warning(this, title, text + tr("\n\nSee toms_next/docs/05_TROUBLESHOOTING.md"));
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
    file->addSeparator();
    file->addAction(tr("E&xit"), this, &QWidget::close);
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About"), this, [this] {
        QMessageBox::about(this, tr("TOMS Editor"),
            tr("Qt %1 + bgfx (%2)\n\nThe Play tab runs the same game code and renderer as toms_game.exe.\n"
               "Docs: toms_next/docs/").arg(QString::fromLatin1(qVersion()), viewport_->rendererName()));
    });
}
