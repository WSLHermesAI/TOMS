// AppShell.cpp -- see AppShell.h.
#include "AppShell.h"

#include "EventEditor.h"
#include "Language.h"
#include "StageEditor.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QTabWidget>

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
}

AppShell::~AppShell() {
    saveGeometry();
    detach();
}

void AppShell::start(int forceMode) {
    const int saved = QSettings().value(QStringLiteral("shell/mode"), int(Tabs)).toInt();
    m_mode = Mode(forceMode >= 0 ? forceMode : saved);
    setMode(m_mode);
}

QMainWindow* AppShell::makeWindow(const QString& objectName) {
    auto* w = new QMainWindow;
    w->setObjectName(objectName);
    w->setAttribute(Qt::WA_DeleteOnClose, false);
    w->installEventFilter(this);
    buildMenus(w);
    return w;
}

void AppShell::buildMenus(QMainWindow* w) {
    QMenu* file = w->menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Save the stage changes…"), m_stage, [this] { m_stage->save(); });
    file->addAction(tr("Save the event changes…"), m_events, [this] { m_events->save(); });
    file->addSeparator();
    QAction* exit = file->addAction(tr("E&xit"), this, &AppShell::quit);
    exit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));

    QMenu* view = w->menuBar()->addMenu(tr("&View"));
    auto* group = new QActionGroup(w);
    QAction* tabs = view->addAction(tr("Tabs (one window)"));
    QAction* wins = view->addAction(tr("Separate windows"));
    for (QAction* a : {tabs, wins}) { a->setCheckable(true); group->addAction(a); }
    tabs->setChecked(m_mode == Tabs);
    wins->setChecked(m_mode == Windows);
    connect(tabs, &QAction::triggered, this, [this] { QMetaObject::invokeMethod(this, [this] { setMode(Tabs); }, Qt::QueuedConnection); });
    connect(wins, &QAction::triggered, this, [this] { QMetaObject::invokeMethod(this, [this] { setMode(Windows); }, Qt::QueuedConnection); });

    QMenu* win = w->menuBar()->addMenu(tr("&Window"));
    QAction* s = win->addAction(tr("Stage editor"), this, &AppShell::showStage);
    s->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    QAction* e = win->addAction(tr("Event editor"), this, &AppShell::showEvents);
    e->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));

    QMenu* help = w->menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About"), w, [w] {
        QMessageBox::about(w, tr("TOMS Stage & Event Editor"),
                           tr("The stage and event editors on the game's stack (Qt %1 + the game's bgfx renderer).\n"
                              "Docs: docs/19_STAGE_EDITOR.md, docs/event_editor/README.md").arg(QString::fromLatin1(qVersion())));
    });
    Language::addToggle(w->menuBar());
}

// Takes both editors out of their windows (they are moved, never recreated) and closes those windows.
void AppShell::detach() {
    if (m_tabs) {
        while (m_tabs->count()) m_tabs->removeTab(0);
    }
    if (m_stageWin && m_stageWin->centralWidget() == m_stage) m_stageWin->takeCentralWidget();
    if (m_eventWin && m_eventWin->centralWidget() == m_events) m_eventWin->takeCentralWidget();
    m_stage->hide();
    m_events->hide();
    m_stage->setParent(nullptr);
    m_events->setParent(nullptr);
    for (QPointer<QMainWindow>* w : {&m_main, &m_stageWin, &m_eventWin})
        if (*w) { (*w)->removeEventFilter(this); (*w)->hide(); (*w)->deleteLater(); *w = nullptr; }
    m_tabs = nullptr;
}

void AppShell::saveGeometry() {
    QSettings s;
    s.setValue(QStringLiteral("shell/mode"), int(m_mode));
    if (m_main) {
        s.setValue(QStringLiteral("shell/tabsGeometry"), m_main->saveGeometry());
        if (m_tabs) s.setValue(QStringLiteral("shell/tab"), m_tabs->currentIndex());
    }
    if (m_stageWin) s.setValue(QStringLiteral("shell/stageGeometry"), m_stageWin->saveGeometry());
    if (m_eventWin) s.setValue(QStringLiteral("shell/eventGeometry"), m_eventWin->saveGeometry());
}

void AppShell::setMode(Mode m) {
    const bool had = m_main || m_stageWin || m_eventWin;
    if (had) saveGeometry();
    detach();
    m_mode = m;
    QSettings s;
    s.setValue(QStringLiteral("shell/mode"), int(m));
    if (m == Tabs) {
        m_main = makeWindow(QStringLiteral("TabsWindow"));
        m_tabs = new QTabWidget(m_main);
        m_tabs->setDocumentMode(true);
        m_tabs->addTab(m_stage, tr("Stage"));
        m_tabs->addTab(m_events, tr("Events"));
        m_main->setCentralWidget(m_tabs);
        m_main->resize(1500, 920);
        m_main->restoreGeometry(s.value(QStringLiteral("shell/tabsGeometry")).toByteArray());
        m_tabs->setCurrentIndex(std::clamp(s.value(QStringLiteral("shell/tab"), 0).toInt(), 0, 1));
        m_tabs->currentWidget()->show();   // the tab widget shows / hides its pages
        m_main->show();
    } else {
        m_stageWin = makeWindow(QStringLiteral("StageWindow"));
        m_stageWin->setCentralWidget(m_stage);
        m_stageWin->resize(1400, 900);
        m_stageWin->restoreGeometry(s.value(QStringLiteral("shell/stageGeometry")).toByteArray());
        m_eventWin = makeWindow(QStringLiteral("EventWindow"));
        m_eventWin->setCentralWidget(m_events);
        m_eventWin->resize(1100, 800);
        if (!m_eventWin->restoreGeometry(s.value(QStringLiteral("shell/eventGeometry")).toByteArray()))
            m_eventWin->move(m_stageWin->pos() + QPoint(60, 60));
        m_stage->show();
        m_events->show();
        m_stageWin->show();
        m_eventWin->show();
    }
    updateTitles();
}

bool AppShell::hasUnsaved() const { return m_stage->hasUnsaved() || m_events->hasUnsaved(); }

void AppShell::updateTitles() {
    const QString dirty = m_stage->hasUnsaved() ? QStringLiteral(" ●") : QString();
    const QString evDirty = m_events->hasUnsaved() ? QStringLiteral(" ●") : QString();
    const QString stageTitle = m_stage->windowTitle().isEmpty() ? tr("TOMS Stage Editor") : m_stage->windowTitle();
    if (m_main) {
        m_main->setWindowTitle(tr("TOMS Stage & Event Editor") + QStringLiteral(" — ") + stageTitle);
        if (m_tabs) { m_tabs->setTabText(0, tr("Stage") + dirty); m_tabs->setTabText(1, tr("Events") + evDirty); }
    }
    if (m_stageWin) m_stageWin->setWindowTitle(stageTitle);
    if (m_eventWin) m_eventWin->setWindowTitle(tr("TOMS Event Editor") + evDirty);
}

void AppShell::showStage() {
    if (m_tabs) { m_tabs->setCurrentWidget(m_stage); m_main->raise(); m_main->activateWindow(); return; }
    if (!m_stageWin) return;
    if (m_stageWin->isMinimized()) m_stageWin->showNormal(); else m_stageWin->show();
    m_stageWin->raise();
    m_stageWin->activateWindow();
}

void AppShell::showEvents() {
    if (m_tabs) { m_tabs->setCurrentWidget(m_events); m_main->raise(); m_main->activateWindow(); return; }
    if (!m_eventWin) return;
    if (m_eventWin->isMinimized()) m_eventWin->showNormal(); else m_eventWin->show();
    m_eventWin->raise();
    m_eventWin->activateWindow();
}

bool AppShell::confirmQuit() {
    if (!hasUnsaved()) return true;
    QStringList which;
    if (m_stage->hasUnsaved()) which << tr("the stage editor");
    if (m_events->hasUnsaved()) which << tr("the event editor");
    const auto r = QMessageBox::question(m_stage->window(), tr("Unsaved changes"), tr("Unsaved changes in %1. Save them before closing?").arg(which.join(QStringLiteral(" + "))),
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
    saveGeometry();
    QApplication::quit();
}

bool AppShell::eventFilter(QObject* o, QEvent* e) {
    if (e->type() != QEvent::Close || m_quitting) return QObject::eventFilter(o, e);
    auto* ce = static_cast<QCloseEvent*>(e);
    // separate windows: closing one hides it while the other is open
    QMainWindow* other = o == m_stageWin ? m_eventWin.data() : o == m_eventWin ? m_stageWin.data() : nullptr;
    if (other && other->isVisible()) return QObject::eventFilter(o, e);
    if (!confirmQuit()) { ce->ignore(); return true; }
    m_quitting = true;
    saveGeometry();
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
    return QObject::eventFilter(o, e);
}

QList<QWidget*> AppShell::windows() const {
    QList<QWidget*> out;
    for (const QPointer<QMainWindow>& w : {m_main, m_stageWin, m_eventWin})
        if (w && w->isVisible()) out << w.data();
    return out;
}

QStringList AppShell::selfTest() {
    QStringList fail;
    setMode(Windows);
    if (!m_stageWin || m_stage->window() != m_stageWin || !m_eventWin || m_events->window() != m_eventWin)
        fail << QStringLiteral("separate windows: each editor in its own window");
    showEvents();
    showStage();
    setMode(Tabs);
    if (!m_main || m_stage->window() != m_main || m_events->window() != m_main || !m_tabs || m_tabs->count() != 2)
        fail << QStringLiteral("tabs: both editors in one window");
    showEvents();
    if (m_tabs && m_tabs->currentWidget() != m_events) fail << QStringLiteral("tabs: showEvents switches the tab");
    showStage();
    if (m_tabs && m_tabs->currentWidget() != m_stage) fail << QStringLiteral("tabs: showStage switches back");
    return fail;
}
