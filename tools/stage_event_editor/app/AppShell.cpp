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
const int kLayoutVersion = 2;   // QMainWindow::saveState version: 1 was the tabs / separate-windows shell

// A floating editor's title bar: its name, □ maximise / restore, ⧉ dock back. Dragging it moves the window, and over
// the main window docks it (QDockWidget does that with any title bar widget that lets mouse presses through).
class FloatTitle : public QWidget {
public:
    FloatTitle(QDockWidget* dock, AppShell* shell) : QWidget(dock) {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(8, 2, 2, 2);
        m_label = new QLabel(dock->windowTitle(), this);
        m_label->setStyleSheet(QStringLiteral("font-weight:600"));
        h->addWidget(m_label, 1);
        auto* max = new QToolButton(this);
        max->setText(QStringLiteral("□"));
        max->setToolTip(AppShell::tr("Maximise / restore"));
        max->setAutoRaise(true);
        connect(max, &QToolButton::clicked, dock, [dock] { if (dock->isMaximized()) dock->showNormal(); else dock->showMaximized(); });
        auto* back = new QToolButton(this);
        back->setText(QStringLiteral("⧉"));
        back->setToolTip(AppShell::tr("Dock it back into the main window (or drag it there, or double-click this title)"));
        back->setAutoRaise(true);
        connect(back, &QToolButton::clicked, dock, [shell, dock] { shell->setFloating(dock, false); });
        h->addWidget(max);
        h->addWidget(back);
        setAutoFillBackground(true);
        setStyleSheet(QStringLiteral("FloatTitle, QWidget { background:#2c3036; }"));
        connect(dock, &QDockWidget::windowTitleChanged, m_label, &QLabel::setText);
    }
private:
    QLabel* m_label;
};

// A docked editor that is not tabbed with the other one (split): a slim bar to drag it by, and ⧉ to float it.
class DockTitle : public QWidget {
public:
    DockTitle(QDockWidget* dock, AppShell* shell) : QWidget(dock) {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(8, 1, 2, 1);
        auto* l = new QLabel(dock->windowTitle(), this);
        l->setStyleSheet(QStringLiteral("color:#8b929c"));
        h->addWidget(l, 1);
        auto* fl = new QToolButton(this);
        fl->setText(QStringLiteral("⧉"));
        fl->setToolTip(AppShell::tr("Float it as a window of its own (or drag this bar out)"));
        fl->setAutoRaise(true);
        connect(fl, &QToolButton::clicked, dock, [shell, dock] { shell->setFloating(dock, true); });
        h->addWidget(fl);
        connect(dock, &QDockWidget::windowTitleChanged, l, &QLabel::setText);
    }
};
}  // namespace

// The two editors talk directly (no files in between): ↗ / ⇄ / place on the map / an NPC's topics; after either one
// saves, the other reads the files again (only what it has not changed itself).
AppShell::AppShell(StageEditor* stage, EventEditor* events, QObject* parent) : QObject(parent), m_stage(stage), m_events(events) {
    connect(m_stage, &StageEditor::openEventEditor, this, [this](const QString& id) {
        if (!id.isEmpty()) m_events->selectEvent(id);
        showEvents();
    });
    connect(m_stage, &StageEditor::openNpcInEventEditor, this, [this](const QString& id) {
        m_events->showNpc(id);
        showEvents();
    });
    connect(m_events, &EventEditor::openStage, this, [this](const QString& floor, const QString& ev) {
        if (!floor.isEmpty()) m_stage->showEventOnFloor(floor.toStdString(), ev.toStdString());
        showStage();
    });
    connect(m_events, &EventEditor::placeOnStage, this, [this](const QString& id, const QString& kind, const QStringList& floors) {
        std::vector<std::string> fl;
        for (const QString& f : floors) fl.push_back(f.toStdString());
        m_stage->enterPlaceMode(id.toStdString(), kind.toStdString(), fl);
        showStage();
    });
    connect(m_events, &EventEditor::filesWritten, m_stage, [this] { m_stage->reloadEvents(); });
    connect(m_stage, &StageEditor::filesWritten, m_events, [this] { m_events->reloadFromDisk(); });
    connect(m_stage, &StageEditor::dirtyChanged, this, &AppShell::updateTitles);
    connect(m_events, &EventEditor::dirtyChanged, this, &AppShell::updateTitles);

    m_main = new QMainWindow;
    m_main->setObjectName(QStringLiteral("MainWindow"));
    m_main->installEventFilter(this);
    // no centre: the two editors fill the window; their tabs go along the top, as documents
    m_main->setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);
    m_main->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    m_main->setDocumentMode(true);
    m_stageDock = makeDock(m_stage, QStringLiteral("StageDock"), tr("Stage"));
    m_eventDock = makeDock(m_events, QStringLiteral("EventDock"), tr("Events"));
    m_main->addDockWidget(Qt::TopDockWidgetArea, m_stageDock);
    m_main->tabifyDockWidget(m_stageDock, m_eventDock);
    buildMenus();
}

AppShell::~AppShell() {
    saveLayout();
    delete m_main;   // with its docks and the two editors in them (taking them out first would recreate the map's window)
}

QDockWidget* AppShell::makeDock(QWidget* editor, const QString& objectName, const QString& title) {
    auto* d = new QDockWidget(title, m_main);
    d->setObjectName(objectName);
    d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    d->setAllowedAreas(Qt::AllDockWidgetAreas);
    d->setWidget(editor);
    editor->show();
    connect(d, &QDockWidget::topLevelChanged, this, [this, d](bool) { QTimer::singleShot(0, this, [this, d] { updateTitleBar(d); updateTitles(); }); });
    connect(d, &QDockWidget::dockLocationChanged, this, [this] {
        QTimer::singleShot(0, this, [this] { updateTitleBar(m_stageDock); updateTitleBar(m_eventDock); });
    });
    d->installEventFilter(this);
    return d;
}

// floating: the window's own bar (□, ⧉); docked as a tab: none (the tab is the handle, as in Visual Studio); docked
// beside the other editor: a slim bar to drag it by
void AppShell::updateTitleBar(QDockWidget* d) {
    if (!d || !m_main) return;
    QWidget* old = d->titleBarWidget();
    QWidget* now = nullptr;
    if (d->isFloating()) now = new FloatTitle(d, this);
    else if (!m_main->tabifiedDockWidgets(d).isEmpty()) now = new QWidget(d);   // empty: hides the bar
    else now = new DockTitle(d, this);
    d->setTitleBarWidget(now);
    if (old) old->deleteLater();
}

void AppShell::setFloating(QDockWidget* d, bool floating) {
    if (!d || d->isFloating() == floating) return;
    if (floating) {
        const QRect g = d->geometry();
        d->setFloating(true);
        const QPoint at = m_main->mapToGlobal(QPoint(60, 60));
        d->setGeometry(QRect(at, QSize(std::max(900, g.width()), std::max(640, g.height()))));
    } else {
        if (d->isMaximized()) d->showNormal();
        d->setFloating(false);
        QDockWidget* other = d == m_stageDock ? m_eventDock.data() : m_stageDock.data();
        if (other && !other->isFloating() && m_main->tabifiedDockWidgets(d).isEmpty()) m_main->tabifyDockWidget(other, d);
    }
    d->show();
    d->raise();
    updateTitleBar(d);
    if (QDockWidget* other = d == m_stageDock ? m_eventDock.data() : m_stageDock.data()) updateTitleBar(other);
}

void AppShell::dockAsTabs() {
    for (QDockWidget* d : {m_stageDock.data(), m_eventDock.data()}) {
        if (d->isMaximized()) d->showNormal();
        d->setFloating(false);
        m_main->removeDockWidget(d);
    }
    m_main->addDockWidget(Qt::TopDockWidgetArea, m_stageDock);
    m_main->tabifyDockWidget(m_stageDock, m_eventDock);
    m_stageDock->show();
    m_eventDock->show();
    raise(m_stageDock);
    updateTitleBar(m_stageDock);
    updateTitleBar(m_eventDock);
}

void AppShell::buildMenus() {
    QMenuBar* bar = m_main->menuBar();
    QMenu* file = bar->addMenu(tr("&File"));
    file->addAction(tr("Save the stage changes…"), m_stage, [this] { m_stage->save(); });
    file->addAction(tr("Save the event changes…"), m_events, [this] { m_events->save(); });
    file->addSeparator();
    QAction* exit = file->addAction(tr("E&xit"), this, &AppShell::quit);
    exit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));

    QMenu* view = bar->addMenu(tr("&View"));
    view->addAction(tr("Default layout"), this, &AppShell::defaultLayout);
    view->addSeparator();
    view->addAction(tr("Dock both as tabs"), this, &AppShell::dockAsTabs);
    view->addSeparator();
    // the stage editor's panels: tick one to show it, or put them all back where they start
    QMenu* panels = view->addMenu(tr("Stage panels"));
    for (QDockWidget* d : m_stage->panels()) panels->addAction(d->toggleViewAction());
    panels->addSeparator();
    panels->addAction(tr("Reset stage layout"), m_stage, [this] { showStage(); m_stage->resetLayout(); });
    view->addSeparator();
    QAction* fs = view->addAction(tr("Float the stage editor"));
    QAction* fe = view->addAction(tr("Float the event editor"));
    for (auto [a, d] : {std::pair{fs, m_stageDock.data()}, std::pair{fe, m_eventDock.data()}}) {
        a->setCheckable(true);
        connect(a, &QAction::triggered, this, [this, d](bool on) { setFloating(d, on); });
        connect(view, &QMenu::aboutToShow, a, [a, d] { a->setChecked(d->isFloating()); });
    }

    QMenu* win = bar->addMenu(tr("&Window"));
    QAction* s = win->addAction(tr("Stage editor"), this, &AppShell::showStage);
    s->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    s->setShortcutContext(Qt::ApplicationShortcut);
    QAction* e = win->addAction(tr("Event editor"), this, &AppShell::showEvents);
    e->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    e->setShortcutContext(Qt::ApplicationShortcut);

    QMenu* help = bar->addMenu(tr("&Help"));
    help->addAction(tr("About"), m_main, [this] {
        QMessageBox::about(m_main, tr("TOMS Stage & Event Editor"),
                           tr("The stage and event editors on the game's stack (Qt %1 + the game's bgfx renderer).\n"
                              "Docs: docs/19_STAGE_EDITOR.md, docs/event_editor/README.md").arg(QString::fromLatin1(qVersion())));
    });
    Language::addToggle(bar);
}

void AppShell::start(bool floatEvents) {
    QSettings s;
    m_main->resize(1500, 920);
    m_main->restoreGeometry(s.value(QStringLiteral("shell/mainGeometry")).toByteArray());
    const bool restored = !floatEvents && m_main->restoreState(s.value(QStringLiteral("shell/dockState")).toByteArray(), kLayoutVersion);
    if (!restored) dockAsTabs();
    m_main->show();
    if (floatEvents) setFloating(m_eventDock, true);
    updateTitleBar(m_stageDock);
    updateTitleBar(m_eventDock);
    updateTitles();
}

void AppShell::defaultLayout() {
    if (!m_main) return;
    dockAsTabs();
    m_main->showNormal();
    const QRect avail = (m_main->screen() ? m_main->screen() : QApplication::primaryScreen())->availableGeometry();
    const QSize size(std::min(1500, avail.width() * 9 / 10), std::min(920, avail.height() * 9 / 10));   // as a first run
    m_main->resize(size);
    m_main->move(avail.center() - QPoint(size.width() / 2, size.height() / 2));
    m_stage->resetLayout();
    m_events->resetLayout();
    if (ObjectWindow* w = ObjectWindow::current()) {
        w->resize(760, 640);
        w->move(m_main->geometry().center() - QPoint(380, 320));
    }
    showStage();
}

void AppShell::saveLayout() {
    if (!m_main) return;
    QSettings s;
    s.setValue(QStringLiteral("shell/mainGeometry"), m_main->saveGeometry());
    s.setValue(QStringLiteral("shell/dockState"), m_main->saveState(kLayoutVersion));
}

bool AppShell::hasUnsaved() const { return m_stage->hasUnsaved() || m_events->hasUnsaved(); }

void AppShell::updateTitles() {
    if (!m_main) return;
    const QString dirty = m_stage->hasUnsaved() ? QStringLiteral(" ●") : QString();
    const QString evDirty = m_events->hasUnsaved() ? QStringLiteral(" ●") : QString();
    const QString stageTitle = m_stage->windowTitle().isEmpty() ? tr("TOMS Stage Editor") : m_stage->windowTitle();
    m_main->setWindowTitle(tr("TOMS Stage & Event Editor") + QStringLiteral(" — ") + stageTitle);
    // a floating editor is a window: its title says what it is
    m_stageDock->setWindowTitle((m_stageDock->isFloating() ? stageTitle : tr("Stage")) + (m_stageDock->isFloating() ? QString() : dirty));
    m_eventDock->setWindowTitle((m_eventDock->isFloating() ? tr("TOMS Event Editor") : tr("Events")) + evDirty);
}

// the editor in front: its tab selected, or its floating window raised (and restored when minimised)
void AppShell::raise(QDockWidget* d) {
    if (!d) return;
    d->show();
    d->raise();
    if (d->isFloating()) {
        if (d->isMinimized()) d->showNormal();
        d->activateWindow();
    } else {
        m_main->raise();
        m_main->activateWindow();
    }
}

void AppShell::showStage() { raise(m_stageDock); }
void AppShell::showEvents() { raise(m_eventDock); }

bool AppShell::confirmQuit() {
    if (!hasUnsaved()) return true;
    QStringList which;
    if (m_stage->hasUnsaved()) which << tr("the stage editor");
    if (m_events->hasUnsaved()) which << tr("the event editor");
    const auto r = QMessageBox::question(m_main, tr("Unsaved changes"), tr("Unsaved changes in %1. Save them before closing?").arg(which.join(QStringLiteral(" + "))),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) {
        if (m_stage->hasUnsaved() && !m_stage->save()) return false;
        if (m_events->hasUnsaved() && !m_events->save()) return false;
        return !hasUnsaved();
    }
    return true;
}

void AppShell::quit() {
    if (!confirmQuit()) return;
    m_quitting = true;
    saveLayout();
    QApplication::quit();
}

bool AppShell::eventFilter(QObject* o, QEvent* e) {
    // a floating editor's window closed (Alt+F4): it docks back instead of disappearing
    if (e->type() == QEvent::Close && (o == m_stageDock || o == m_eventDock)) {
        auto* d = static_cast<QDockWidget*>(o);
        if (d->isFloating() && !m_quitting) { e->ignore(); QTimer::singleShot(0, this, [this, d] { setFloating(d, false); }); return true; }
        return QObject::eventFilter(o, e);
    }
    if (o != m_main || e->type() != QEvent::Close || m_quitting) return QObject::eventFilter(o, e);
    auto* ce = static_cast<QCloseEvent*>(e);
    if (!confirmQuit()) { ce->ignore(); return true; }
    m_quitting = true;
    saveLayout();
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
    return QObject::eventFilter(o, e);
}

QList<QWidget*> AppShell::windows() const {
    QList<QWidget*> out;
    if (m_main && m_main->isVisible()) out << m_main.data();
    for (QDockWidget* d : {m_stageDock.data(), m_eventDock.data()})
        if (d && d->isFloating() && d->isVisible()) out << d;
    return out;
}

QStringList AppShell::selfTest() {
    QStringList fail;
    // Default layout: a floating editor and a closed panel come back, docked
    setFloating(m_eventDock, true);
    if (!m_stage->panels().isEmpty()) m_stage->panels().front()->hide();
    defaultLayout();
    QApplication::processEvents();
    if (m_eventDock->isFloating() || (!m_stage->panels().isEmpty() && m_stage->panels().front()->isHidden()))
        fail << QStringLiteral("default layout: everything docked and shown");
    dockAsTabs();
    QApplication::processEvents();
    if (m_main->tabifiedDockWidgets(m_stageDock) != QList<QDockWidget*>{m_eventDock}) fail << QStringLiteral("docked: the editors are two tabs");
    if (m_stage->window() != m_main || m_events->window() != m_main) fail << QStringLiteral("docked: both editors in the main window");
    showEvents();
    QApplication::processEvents();
    if (m_eventDock->visibleRegion().isEmpty() && m_main->isVisible()) fail << QStringLiteral("docked: showEvents brings the event tab to the front");
    // float the stage editor: a window of its own, with the editor inside; then dock it back as a tab
    setFloating(m_stageDock, true);
    QApplication::processEvents();
    if (!m_stageDock->isFloating() || m_stage->window() != m_stageDock) fail << QStringLiteral("floating: the stage editor is a window of its own");
    if (!dynamic_cast<FloatTitle*>(m_stageDock->titleBarWidget())) fail << QStringLiteral("floating: the window has its □ / ⧉ title bar");
    setFloating(m_stageDock, false);
    QApplication::processEvents();
    if (m_stageDock->isFloating() || m_stage->window() != m_main) fail << QStringLiteral("docked again: the stage editor is back in the main window");
    if (m_main->tabifiedDockWidgets(m_stageDock).isEmpty()) fail << QStringLiteral("docked again: as a tab");
    // split: beside each other, each with a slim bar to drag it by
    m_main->removeDockWidget(m_eventDock);   // as a drag beside it ends: out of the tab group, into another area
    m_main->addDockWidget(Qt::BottomDockWidgetArea, m_eventDock);
    m_eventDock->show();
    QApplication::processEvents();
    updateTitleBar(m_stageDock);
    if (!dynamic_cast<DockTitle*>(m_stageDock->titleBarWidget())) fail << QStringLiteral("split: a docked editor beside the other has a bar");
    dockAsTabs();
    QApplication::processEvents();
    return fail;
}
