// AppShell.cpp -- see AppShell.h.
#include "AppShell.h"

#include "EventEditor.h"
#include "ObjectWindow.h"
#include "Language.h"
#include "StageEditor.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>

namespace {
const int g_iLayoutVersion = 2;   // QMainWindow::saveState version: 1 was the tabs / separate-windows shell

// A floating editor's title bar: its name, □ maximise / restore, ⧉ dock back. Dragging it moves the window, and over
// the main window docks it (QDockWidget does that with any title bar widget that lets mouse presses through).
class FloatTitle : public QWidget {
public:
    FloatTitle(QDockWidget* l_pDock, AppShell* l_pShell) : QWidget(l_pDock) {
        auto* l_pH = new QHBoxLayout(this);
        l_pH->setContentsMargins(8, 2, 2, 2);
        m_pLabel = new QLabel(l_pDock->windowTitle(), this);
        m_pLabel->setStyleSheet(QStringLiteral("font-weight:600"));
        l_pH->addWidget(m_pLabel, 1);
        auto* l_pMax = new QToolButton(this);
        l_pMax->setText(QStringLiteral("□"));
        l_pMax->setToolTip(AppShell::tr("Maximise / restore"));
        l_pMax->setAutoRaise(true);
        connect(l_pMax, &QToolButton::clicked, l_pDock, [l_pDock] { if (l_pDock->isMaximized()) l_pDock->showNormal(); else l_pDock->showMaximized(); });
        auto* l_pBack = new QToolButton(this);
        l_pBack->setText(QStringLiteral("⧉"));
        l_pBack->setToolTip(AppShell::tr("Dock it back into the main window (or drag it there, or double-click this title)"));
        l_pBack->setAutoRaise(true);
        connect(l_pBack, &QToolButton::clicked, l_pDock, [l_pShell, l_pDock] { l_pShell->f_SetFloating(l_pDock, false); });
        l_pH->addWidget(l_pMax);
        l_pH->addWidget(l_pBack);
        setAutoFillBackground(true);
        setStyleSheet(QStringLiteral("FloatTitle, QWidget { background:#2c3036; }"));
        connect(l_pDock, &QDockWidget::windowTitleChanged, m_pLabel, &QLabel::setText);
    }
private:
    QLabel* m_pLabel;
};

// A docked editor that is not tabbed with the other one (split): a slim bar to drag it by, and ⧉ to float it.
class DockTitle : public QWidget {
public:
    DockTitle(QDockWidget* l_pDock, AppShell* l_pShell) : QWidget(l_pDock) {
        auto* l_pH = new QHBoxLayout(this);
        l_pH->setContentsMargins(8, 1, 2, 1);
        auto* l_pL = new QLabel(l_pDock->windowTitle(), this);
        l_pL->setStyleSheet(QStringLiteral("color:#8b929c"));
        l_pH->addWidget(l_pL, 1);
        auto* l_pFl = new QToolButton(this);
        l_pFl->setText(QStringLiteral("⧉"));
        l_pFl->setToolTip(AppShell::tr("Float it as a window of its own (or drag this bar out)"));
        l_pFl->setAutoRaise(true);
        connect(l_pFl, &QToolButton::clicked, l_pDock, [l_pShell, l_pDock] { l_pShell->f_SetFloating(l_pDock, true); });
        l_pH->addWidget(l_pFl);
        connect(l_pDock, &QDockWidget::windowTitleChanged, l_pL, &QLabel::setText);
    }
};
}  // namespace

// The two editors talk directly (no files in between): ↗ / ⇄ / place on the map / an NPC's topics; after either one
// saves, the other reads the files again (only what it has not changed itself).
AppShell::AppShell(StageEditor* l_pStage, EventEditor* l_pEvents, QObject* l_pParent) : QObject(l_pParent), m_pStage(l_pStage), m_pEvents(l_pEvents) {
    connect(m_pStage, &StageEditor::f_OpenEventEditor, this, [this](const QString& l_oId) {
        if (!l_oId.isEmpty()) m_pEvents->f_SelectEvent(l_oId);
        f_ShowEvents();
    });
    connect(m_pStage, &StageEditor::f_OpenNpcInEventEditor, this, [this](const QString& l_oId) {
        m_pEvents->f_ShowNpc(l_oId);
        f_ShowEvents();
    });
    connect(m_pEvents, &EventEditor::f_OpenStage, this, [this](const QString& l_oFloor, const QString& l_oEv) {
        if (!l_oFloor.isEmpty()) m_pStage->f_ShowEventOnFloor(l_oFloor.toStdString(), l_oEv.toStdString());
        f_ShowStage();
    });
    connect(m_pEvents, &EventEditor::f_PlaceOnStage, this, [this](const QString& l_oId, const QString& l_oKind, const QStringList& l_oFloors) {
        std::vector<std::string> l_vFl;
        for (const QString& l_oF : l_oFloors) l_vFl.push_back(l_oF.toStdString());
        m_pStage->f_EnterPlaceMode(l_oId.toStdString(), l_oKind.toStdString(), l_vFl);
        f_ShowStage();
    });
    connect(m_pEvents, &EventEditor::f_FilesWritten, m_pStage, [this] { m_pStage->f_ReloadEvents(); });
    connect(m_pStage, &StageEditor::f_FilesWritten, m_pEvents, [this] { m_pEvents->f_ReloadFromDisk(); });
    connect(m_pStage, &StageEditor::f_DirtyChanged, this, &AppShell::f_UpdateTitles);
    connect(m_pEvents, &EventEditor::f_DirtyChanged, this, &AppShell::f_UpdateTitles);

    m_oMain = new QMainWindow;
    m_oMain->setObjectName(QStringLiteral("MainWindow"));
    m_oMain->installEventFilter(this);
    // no centre: the two editors fill the window; their tabs go along the top, as documents
    m_oMain->setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);
    m_oMain->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    m_oMain->setDocumentMode(true);
    m_oStageDock = f_MakeDock(m_pStage, QStringLiteral("StageDock"), tr("Stage"));
    m_oEventDock = f_MakeDock(m_pEvents, QStringLiteral("EventDock"), tr("Events"));
    m_oMain->addDockWidget(Qt::TopDockWidgetArea, m_oStageDock);
    m_oMain->tabifyDockWidget(m_oStageDock, m_oEventDock);
    f_BuildMenus();
}

AppShell::~AppShell() {
    f_SaveLayout();
    delete m_oMain;   // with its docks and the two editors in them (taking them out first would recreate the map's window)
}

QDockWidget* AppShell::f_MakeDock(QWidget* l_pEditor, const QString& l_oObjectName, const QString& l_oTitle) {
    auto* l_pD = new QDockWidget(l_oTitle, m_oMain);
    l_pD->setObjectName(l_oObjectName);
    l_pD->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    l_pD->setAllowedAreas(Qt::AllDockWidgetAreas);
    l_pD->setWidget(l_pEditor);
    l_pEditor->show();
    connect(l_pD, &QDockWidget::topLevelChanged, this, [this, l_pD](bool) { QTimer::singleShot(0, this, [this, l_pD] { f_UpdateTitleBar(l_pD); f_UpdateTitles(); }); });
    connect(l_pD, &QDockWidget::dockLocationChanged, this, [this] {
        QTimer::singleShot(0, this, [this] { f_UpdateTitleBar(m_oStageDock); f_UpdateTitleBar(m_oEventDock); });
    });
    l_pD->installEventFilter(this);
    return l_pD;
}

// floating: the window's own bar (□, ⧉); docked as a tab: none (the tab is the handle, as in Visual Studio); docked
// beside the other editor: a slim bar to drag it by
void AppShell::f_UpdateTitleBar(QDockWidget* l_pD) {
    if (!l_pD || !m_oMain) return;
    QWidget* l_pOld = l_pD->titleBarWidget();
    QWidget* l_pNow = nullptr;
    if (l_pD->isFloating()) l_pNow = new FloatTitle(l_pD, this);
    else if (!m_oMain->tabifiedDockWidgets(l_pD).isEmpty()) l_pNow = new QWidget(l_pD);   // empty: hides the bar
    else l_pNow = new DockTitle(l_pD, this);
    l_pD->setTitleBarWidget(l_pNow);
    if (l_pOld) l_pOld->deleteLater();
}

void AppShell::f_SetFloating(QDockWidget* l_pD, bool l_bFloating) {
    if (!l_pD || l_pD->isFloating() == l_bFloating) return;
    if (l_bFloating) {
        const QRect l_oG = l_pD->geometry();
        l_pD->setFloating(true);
        const QPoint l_oAt = m_oMain->mapToGlobal(QPoint(60, 60));
        l_pD->setGeometry(QRect(l_oAt, QSize(std::max(900, l_oG.width()), std::max(640, l_oG.height()))));
    } else {
        if (l_pD->isMaximized()) l_pD->showNormal();
        l_pD->setFloating(false);
        QDockWidget* l_pOther = l_pD == m_oStageDock ? m_oEventDock.data() : m_oStageDock.data();
        if (l_pOther && !l_pOther->isFloating() && m_oMain->tabifiedDockWidgets(l_pD).isEmpty()) m_oMain->tabifyDockWidget(l_pOther, l_pD);
    }
    l_pD->show();
    l_pD->raise();
    f_UpdateTitleBar(l_pD);
    if (QDockWidget* l_pOther = l_pD == m_oStageDock ? m_oEventDock.data() : m_oStageDock.data()) f_UpdateTitleBar(l_pOther);
}

void AppShell::f_DockAsTabs() {
    for (QDockWidget* l_pD : {m_oStageDock.data(), m_oEventDock.data()}) {
        if (l_pD->isMaximized()) l_pD->showNormal();
        l_pD->setFloating(false);
        m_oMain->removeDockWidget(l_pD);
    }
    m_oMain->addDockWidget(Qt::TopDockWidgetArea, m_oStageDock);
    m_oMain->tabifyDockWidget(m_oStageDock, m_oEventDock);
    m_oStageDock->show();
    m_oEventDock->show();
    f_Raise(m_oStageDock);
    f_UpdateTitleBar(m_oStageDock);
    f_UpdateTitleBar(m_oEventDock);
}

void AppShell::f_BuildMenus() {
    QMenuBar* l_pBar = m_oMain->menuBar();
    QMenu* l_pFile = l_pBar->addMenu(tr("&File"));
    l_pFile->addAction(tr("Save the stage changes…"), m_pStage, [this] { m_pStage->f_Save(); });
    l_pFile->addAction(tr("Save the event changes…"), m_pEvents, [this] { m_pEvents->f_Save(); });
    l_pFile->addSeparator();
    QAction* l_pExit = l_pFile->addAction(tr("E&xit"), this, &AppShell::f_Quit);
    l_pExit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));

    QMenu* l_pView = l_pBar->addMenu(tr("&View"));
    l_pView->addAction(tr("Default layout"), this, &AppShell::f_DefaultLayout);
    l_pView->addSeparator();
    l_pView->addAction(tr("Dock both as tabs"), this, &AppShell::f_DockAsTabs);
    l_pView->addSeparator();
    // the stage editor's panels: tick one to show it, or put them all back where they start
    QMenu* l_pPanels = l_pView->addMenu(tr("Stage panels"));
    for (QDockWidget* l_pD : m_pStage->f_Panels()) l_pPanels->addAction(l_pD->toggleViewAction());
    l_pPanels->addSeparator();
    l_pPanels->addAction(tr("Reset stage layout"), m_pStage, [this] { f_ShowStage(); m_pStage->f_ResetLayout(); });
    l_pView->addSeparator();
    QAction* l_pFs = l_pView->addAction(tr("Float the stage editor"));
    QAction* l_pFe = l_pView->addAction(tr("Float the event editor"));
    for (auto [l_pA, l_pD] : {std::pair{l_pFs, m_oStageDock.data()}, std::pair{l_pFe, m_oEventDock.data()}}) {
        l_pA->setCheckable(true);
        connect(l_pA, &QAction::triggered, this, [this, l_pD](bool l_bOn) { f_SetFloating(l_pD, l_bOn); });
        connect(l_pView, &QMenu::aboutToShow, l_pA, [l_pA, l_pD] { l_pA->setChecked(l_pD->isFloating()); });
    }

    QMenu* l_pWin = l_pBar->addMenu(tr("&Window"));
    QAction* l_pS = l_pWin->addAction(tr("Stage editor"), this, &AppShell::f_ShowStage);
    l_pS->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    l_pS->setShortcutContext(Qt::ApplicationShortcut);
    QAction* l_pE = l_pWin->addAction(tr("Event editor"), this, &AppShell::f_ShowEvents);
    l_pE->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    l_pE->setShortcutContext(Qt::ApplicationShortcut);

    QMenu* l_pHelp = l_pBar->addMenu(tr("&Help"));
    l_pHelp->addAction(tr("About"), m_oMain, [this] {
        QMessageBox::about(m_oMain, tr("TOMS Stage & Event Editor"),
                           tr("The stage and event editors on the game's stack (Qt %1 + the game's bgfx renderer).\n"
                              "Docs: docs/gameplay/02_STAGE_EDITOR.md, docs/gameplay/event_editor/README.md").arg(QString::fromLatin1(qVersion())));
    });
    Language::f_AddToggle(l_pBar);
}

void AppShell::f_Start(bool l_bFloatEvents) {
    QSettings l_oS;
    m_oMain->resize(1500, 920);
    m_oMain->restoreGeometry(l_oS.value(QStringLiteral("shell/mainGeometry")).toByteArray());
    const bool l_bRestored = !l_bFloatEvents && m_oMain->restoreState(l_oS.value(QStringLiteral("shell/dockState")).toByteArray(), g_iLayoutVersion);
    if (!l_bRestored) f_DockAsTabs();
    m_oMain->show();
    if (l_bFloatEvents) f_SetFloating(m_oEventDock, true);
    f_UpdateTitleBar(m_oStageDock);
    f_UpdateTitleBar(m_oEventDock);
    f_UpdateTitles();
}

void AppShell::f_DefaultLayout() {
    if (!m_oMain) return;
    f_DockAsTabs();
    m_oMain->showNormal();
    const QRect l_oAvail = (m_oMain->screen() ? m_oMain->screen() : QApplication::primaryScreen())->availableGeometry();
    const QSize l_oSize(std::min(1500, l_oAvail.width() * 9 / 10), std::min(920, l_oAvail.height() * 9 / 10));   // as a first run
    m_oMain->resize(l_oSize);
    m_oMain->move(l_oAvail.center() - QPoint(l_oSize.width() / 2, l_oSize.height() / 2));
    m_pStage->f_ResetLayout();
    m_pEvents->f_ResetLayout();
    if (ObjectWindow* l_pW = ObjectWindow::f_Current()) {
        l_pW->resize(760, 640);
        l_pW->move(m_oMain->geometry().center() - QPoint(380, 320));
    }
    f_ShowStage();
}

void AppShell::f_SaveLayout() {
    if (!m_oMain) return;
    QSettings l_oS;
    l_oS.setValue(QStringLiteral("shell/mainGeometry"), m_oMain->saveGeometry());
    l_oS.setValue(QStringLiteral("shell/dockState"), m_oMain->saveState(g_iLayoutVersion));
}

bool AppShell::f_HasUnsaved() const { return m_pStage->f_HasUnsaved() || m_pEvents->f_HasUnsaved(); }

void AppShell::f_UpdateTitles() {
    if (!m_oMain) return;
    const QString l_oDirty = m_pStage->f_HasUnsaved() ? QStringLiteral(" ●") : QString();
    const QString l_oEvDirty = m_pEvents->f_HasUnsaved() ? QStringLiteral(" ●") : QString();
    const QString l_oStageTitle = m_pStage->windowTitle().isEmpty() ? tr("TOMS Stage Editor") : m_pStage->windowTitle();
    m_oMain->setWindowTitle(tr("TOMS Stage & Event Editor") + QStringLiteral(" — ") + l_oStageTitle);
    // a floating editor is a window: its title says what it is
    m_oStageDock->setWindowTitle((m_oStageDock->isFloating() ? l_oStageTitle : tr("Stage")) + (m_oStageDock->isFloating() ? QString() : l_oDirty));
    m_oEventDock->setWindowTitle((m_oEventDock->isFloating() ? tr("TOMS Event Editor") : tr("Events")) + l_oEvDirty);
}

// the editor in front: its tab selected, or its floating window raised (and restored when minimised)
void AppShell::f_Raise(QDockWidget* l_pD) {
    if (!l_pD) return;
    l_pD->show();
    l_pD->raise();
    if (l_pD->isFloating()) {
        if (l_pD->isMinimized()) l_pD->showNormal();
        l_pD->activateWindow();
    } else {
        m_oMain->raise();
        m_oMain->activateWindow();
    }
}

void AppShell::f_ShowStage() { f_Raise(m_oStageDock); }
void AppShell::f_ShowEvents() { f_Raise(m_oEventDock); }

bool AppShell::f_ConfirmQuit() {
    if (!f_HasUnsaved()) return true;
    QStringList l_oWhich;
    if (m_pStage->f_HasUnsaved()) l_oWhich << tr("the stage editor");
    if (m_pEvents->f_HasUnsaved()) l_oWhich << tr("the event editor");
    const auto l_eR = QMessageBox::question(m_oMain, tr("Unsaved changes"), tr("Unsaved changes in %1. Save them before closing?").arg(l_oWhich.join(QStringLiteral(" + "))),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (l_eR == QMessageBox::Cancel) return false;
    if (l_eR == QMessageBox::Save) {
        if (m_pStage->f_HasUnsaved() && !m_pStage->f_Save()) return false;
        if (m_pEvents->f_HasUnsaved() && !m_pEvents->f_Save()) return false;
        return !f_HasUnsaved();
    }
    return true;
}

void AppShell::f_Quit() {
    if (!f_ConfirmQuit()) return;
    m_bQuitting = true;
    f_SaveLayout();
    QApplication::quit();
}

bool AppShell::eventFilter(QObject* l_pO, QEvent* l_pE) {
    // a floating editor's window closed (Alt+F4): it docks back instead of disappearing
    if (l_pE->type() == QEvent::Close && (l_pO == m_oStageDock || l_pO == m_oEventDock)) {
        auto* l_pD = static_cast<QDockWidget*>(l_pO);
        if (l_pD->isFloating() && !m_bQuitting) { l_pE->ignore(); QTimer::singleShot(0, this, [this, l_pD] { f_SetFloating(l_pD, false); }); return true; }
        return QObject::eventFilter(l_pO, l_pE);
    }
    if (l_pO != m_oMain || l_pE->type() != QEvent::Close || m_bQuitting) return QObject::eventFilter(l_pO, l_pE);
    auto* l_pCe = static_cast<QCloseEvent*>(l_pE);
    if (!f_ConfirmQuit()) { l_pCe->ignore(); return true; }
    m_bQuitting = true;
    f_SaveLayout();
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
    return QObject::eventFilter(l_pO, l_pE);
}

QList<QWidget*> AppShell::f_Windows() const {
    QList<QWidget*> l_oOut;
    if (m_oMain && m_oMain->isVisible()) l_oOut << m_oMain.data();
    for (QDockWidget* l_pD : {m_oStageDock.data(), m_oEventDock.data()})
        if (l_pD && l_pD->isFloating() && l_pD->isVisible()) l_oOut << l_pD;
    return l_oOut;
}

QStringList AppShell::f_SelfTest() {
    QStringList l_oFail;
    // Default layout: a floating editor and a closed panel come back, docked
    f_SetFloating(m_oEventDock, true);
    if (!m_pStage->f_Panels().isEmpty()) m_pStage->f_Panels().front()->hide();
    f_DefaultLayout();
    QApplication::processEvents();
    if (m_oEventDock->isFloating() || (!m_pStage->f_Panels().isEmpty() && m_pStage->f_Panels().front()->isHidden()))
        l_oFail << QStringLiteral("default layout: everything docked and shown");
    f_DockAsTabs();
    QApplication::processEvents();
    if (m_oMain->tabifiedDockWidgets(m_oStageDock) != QList<QDockWidget*>{m_oEventDock}) l_oFail << QStringLiteral("docked: the editors are two tabs");
    if (m_pStage->window() != m_oMain || m_pEvents->window() != m_oMain) l_oFail << QStringLiteral("docked: both editors in the main window");
    f_ShowEvents();
    QApplication::processEvents();
    if (m_oEventDock->visibleRegion().isEmpty() && m_oMain->isVisible()) l_oFail << QStringLiteral("docked: showEvents brings the event tab to the front");
    // float the stage editor: a window of its own, with the editor inside; then dock it back as a tab
    f_SetFloating(m_oStageDock, true);
    QApplication::processEvents();
    if (!m_oStageDock->isFloating() || m_pStage->window() != m_oStageDock) l_oFail << QStringLiteral("floating: the stage editor is a window of its own");
    if (!dynamic_cast<FloatTitle*>(m_oStageDock->titleBarWidget())) l_oFail << QStringLiteral("floating: the window has its □ / ⧉ title bar");
    f_SetFloating(m_oStageDock, false);
    QApplication::processEvents();
    if (m_oStageDock->isFloating() || m_pStage->window() != m_oMain) l_oFail << QStringLiteral("docked again: the stage editor is back in the main window");
    if (m_oMain->tabifiedDockWidgets(m_oStageDock).isEmpty()) l_oFail << QStringLiteral("docked again: as a tab");
    // split: beside each other, each with a slim bar to drag it by
    m_oMain->removeDockWidget(m_oEventDock);   // as a drag beside it ends: out of the tab group, into another area
    m_oMain->addDockWidget(Qt::BottomDockWidgetArea, m_oEventDock);
    m_oEventDock->show();
    QApplication::processEvents();
    f_UpdateTitleBar(m_oStageDock);
    if (!dynamic_cast<DockTitle*>(m_oStageDock->titleBarWidget())) l_oFail << QStringLiteral("split: a docked editor beside the other has a bar");
    f_DockAsTabs();
    QApplication::processEvents();
    return l_oFail;
}
