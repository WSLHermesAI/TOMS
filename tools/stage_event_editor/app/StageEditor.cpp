// StageEditor.cpp -- see StageEditor.h. The behaviour follows tools/web_editors/stage_editor.html.
#include "StageEditor.h"

#include "FlowLayout.h"
#include "Language.h"
#include "ObjectWindow.h"
#include "StageCanvas.h"
#include "StageDraw.h"
#include "StageText.h"
#include "stage_save.h"

#include <QActionGroup>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

using stage::Cell;
using stage::Json;
using StageText::q;

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// a palette entry: a click picks the brush, a double-click opens the type's details
class PaletteButton : public QToolButton {
public:
    std::function<void()> onDouble;
protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (onDouble) onDouble();
        else QToolButton::mouseDoubleClickEvent(e);
    }
};

QString esc(const QString& s) { return s.toHtmlEscaped(); }
QString esc(const std::string& s) { return q(s).toHtmlEscaped(); }

QString keyChips(const stage::Keys& k) {
    static const char* colors[3] = {"#e0c34c", "#6aa8ff", "#ff7b7b"};
    QString out;
    for (int c = 0; c < 3; c++)
        if (k.n[c]) out += QStringLiteral("<span style='background:%1;color:#111;font-weight:bold'>&nbsp;%2&nbsp;</span> ").arg(colors[c]).arg(k.n[c]);
    return out.isEmpty() ? QStringLiteral("<span style='color:#8b929c'>0</span>") : out;
}

QString sevIcon(stage::Sev s) { return s == stage::Sev::Error ? QStringLiteral("⛔") : s == stage::Sev::Warn ? QStringLiteral("⚠") : QStringLiteral("ⓘ"); }
QColor sevColor(stage::Sev s) { return s == stage::Sev::Error ? QColor("#ff5d5d") : s == stage::Sev::Warn ? QColor("#ff9f43") : QColor("#8b929c"); }

QLabel* rich(const QString& html, QWidget* parent = nullptr) {
    auto* l = new QLabel(html, parent);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setMinimumWidth(60);   // wraps to the panel's width instead of widening it
    return l;
}

QLabel* heading(const QString& text) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("color:#8b929c; font-size:11px; font-weight:600"));
    return l;
}

QIcon plateIcon() {
    QImage img(14, 14, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter g(&img);
    g.fillRect(QRect(0, 0, 14, 14), QColor("#dbb85c"));
    g.fillRect(QRect(3, 3, 8, 8), QColor("#475775"));
    return QIcon(QPixmap::fromImage(img));
}

}  // namespace

// ---- construction ----------------------------------------------------------------------------------------------------

StageEditor::StageEditor(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("StageEditor"));
    setWindowFlags(Qt::Widget);   // hosted in the app's tab or window
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    buildUi();
    m_checksTimer = new QTimer(this);
    m_checksTimer->setSingleShot(true);
    m_checksTimer->setInterval(30);
    connect(m_checksTimer, &QTimer::timeout, this, &StageEditor::renderProblems);
    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    connect(m_flashTimer, &QTimer::timeout, this, [this] { m_v.flash.clear(); m_canvas->update(); });
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(400);
    connect(m_reloadTimer, &QTimer::timeout, this, [this] {
        if (m_ignoreWatch) { m_ignoreWatch = false; watchFiles(); return; }
        reloadEvents();
    });
    connect(Language::Notifier::instance(), &Language::Notifier::changed, this, &StageEditor::retranslateDynamic);
}

StageEditor::~StageEditor() {
    if (!m_saveLayout) return;
    QSettings s;
    s.setValue(QStringLiteral("stage/docks"), saveState());
}

QList<QDockWidget*> StageEditor::panels() const {
    QList<QDockWidget*> out;
    for (const char* n : {"PaletteDock", "EventsDock", "InspectorDock", "ChecksDock"})
        if (auto* d = findChild<QDockWidget*>(QLatin1String(n))) out << d;
    return out;
}

void StageEditor::resetLayout() {
    const std::pair<const char*, Qt::DockWidgetArea> home[] = {{"PaletteDock", Qt::LeftDockWidgetArea}, {"EventsDock", Qt::LeftDockWidgetArea},
                                                                {"InspectorDock", Qt::RightDockWidgetArea}, {"ChecksDock", Qt::BottomDockWidgetArea}};
    for (const auto& [name, area] : home) {
        auto* d = findChild<QDockWidget*>(QLatin1String(name));
        if (!d) continue;
        d->setFloating(false);
        removeDockWidget(d);
        addDockWidget(area, d);
        d->show();
    }
    for (QToolBar* t : findChildren<QToolBar*>()) t->show();
    // their starting sizes: the palette column, the inspector, the checks below the map; palette above events
    const QList<QDockWidget*> p = panels();
    if (p.size() == 4) {
        resizeDocks({p[0], p[2]}, {260, 320}, Qt::Horizontal);
        resizeDocks({p[0], p[1]}, {460, 300}, Qt::Vertical);
        resizeDocks({p[3]}, {180}, Qt::Vertical);
    }
}

void StageEditor::buildUi() {
    m_canvas = new StageCanvas(this);
    auto* center = new QWidget(this);
    auto* cv = new QVBoxLayout(center);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    // place mode: opened from the event editor to put one event on its floors
    m_placeBar = new QWidget(center);
    m_placeBar->setStyleSheet(QStringLiteral("background:#1f3a2c; border-bottom:1px solid #2f6b4b"));
    auto* pb = new QHBoxLayout(m_placeBar);
    pb->setContentsMargins(8, 4, 8, 4);
    m_pmText = new QLabel(m_placeBar);
    pb->addWidget(m_pmText, 1);
    m_pmSuggest = new QToolButton(m_placeBar);
    m_pmAuto = new QToolButton(m_placeBar);
    m_pmNext = new QToolButton(m_placeBar);
    auto* pmClose = new QToolButton(m_placeBar);
    pmClose->setText(QStringLiteral("✕"));
    for (QToolButton* b : {m_pmSuggest, m_pmAuto, m_pmNext, pmClose}) pb->addWidget(b);
    m_pmSuggest->setText(tr("Suggest a cell"));
    m_pmAuto->setText(tr("Place it for me"));
    connect(m_pmSuggest, &QToolButton::clicked, this, [this] {
        stage::StageDoc* d = curDoc();
        if (!d || !m_v.placeMode) return;
        const auto c = stage::pickCell(m_p, *d, stage::safeCells(m_p, m_cur),
                                       stage::mulberry(stage::hashStr(m_v.placeMode->id + m_cur + std::to_string(QRandomGenerator::global()->generate()))));
        if (!c) { toast(tr("No safe free cell on this floor."), true); return; }
        m_v.sel = *c;
        flashCells({*c});
        renderInspector();
    });
    connect(m_pmAuto, &QToolButton::clicked, this, [this] {
        stage::StageDoc* d = curDoc();
        if (!d || !m_v.placeMode) return;
        const auto c = stage::pickCell(m_p, *d, stage::safeCells(m_p, m_cur), stage::mulberry(stage::hashStr(m_v.placeMode->id + m_cur)));
        if (!c) { toast(tr("No safe free cell on this floor."), true); return; }
        const std::string id = m_v.placeMode->id;
        edit(tr("Paint %1 (%2 cells)").arg(q(id)).arg(1), [&](stage::StageDoc& dd) { m_p.putSem(dd, c->x, c->y, "event:" + id); });
        m_v.sel = *c;
        renderInspector();
    });
    connect(m_pmNext, &QToolButton::clicked, this, [this] {
        if (!m_v.placeMode) return;
        const auto& fl = m_v.placeMode->floors;
        auto it = std::find(fl.begin(), fl.end(), m_cur);
        if (it != fl.end() && it + 1 != fl.end()) {
            const std::string id = m_v.placeMode->id;
            openFloor(*(it + 1));
            setBrush("event:" + id);
        } else {
            m_v.placeMode.reset();
            renderPlaceBar();
            m_canvas->update();
        }
    });
    connect(pmClose, &QToolButton::clicked, this, [this] { m_v.placeMode.reset(); renderPlaceBar(); m_canvas->update(); });
    m_placeBar->hide();
    cv->addWidget(m_placeBar);
    cv->addWidget(m_canvas, 1);
    setCentralWidget(center);
    buildToolbars();
    buildDocks();
    m_hover = new QLabel(this);
    m_status = new QLabel(this);
    statusBar()->addWidget(m_hover, 1);
    statusBar()->addPermanentWidget(m_status);
    connect(m_canvas, &CanvasView::zoomChanged, this, [this](double z) { m_zoomLbl->setText(QStringLiteral("%1%").arg(std::lround(z * 100))); });
    restoreState(QSettings().value(QStringLiteral("stage/docks")).toByteArray());
}

void StageEditor::buildToolbars() {
    auto* top = addToolBar(tr("Stage"));
    top->setObjectName(QStringLiteral("StageBar"));
    top->setMovable(false);
    m_toEventA = top->addAction(tr("⇄ Event editor"), this, [this] {
        stage::StageDoc* d = curDoc();
        std::string ev;
        if (d && m_v.sel) {
            const std::string sem = m_p.semAt(*d, m_v.sel->x, m_v.sel->y);
            if (startsWith(sem, "event:")) ev = sem.substr(6);
        }
        Q_EMIT openEventEditor(q(ev));
    });
    m_toEventA->setToolTip(tr("Show the event editor (with the selected event)"));
    top->addWidget(new QLabel(QStringLiteral(" ") + tr("Floor") + QStringLiteral(" ")));
    m_floorSel = new QComboBox(this);
    m_floorSel->setMinimumContentsLength(18);
    connect(m_floorSel, &QComboBox::activated, this, [this](int i) { openFloor(m_floorSel->itemData(i).toString().toStdString()); });
    top->addWidget(m_floorSel);
    QAction* prev = top->addAction(QStringLiteral("◀"), this, [this] { step(-1); });
    prev->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    prev->setToolTip(tr("Previous floor ([)"));
    QAction* next = top->addAction(QStringLiteral("▶"), this, [this] { step(1); });
    next->setShortcut(QKeySequence(Qt::Key_BracketRight));
    next->setToolTip(tr("Next floor (])"));
    top->addSeparator();
    m_undoA = top->addAction(tr("Undo"), this, &StageEditor::undo);
    m_undoA->setShortcut(QKeySequence::Undo);
    m_redoA = top->addAction(tr("Redo"), this, &StageEditor::redo);
    m_redoA->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence::Redo});
    top->addSeparator();
    m_saveA = top->addAction(tr("Save"), this, [this] { save(); });
    m_saveA->setShortcut(QKeySequence::Save);
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    top->addWidget(spacer);
    m_src = new QLabel(this);
    m_src->setStyleSheet(QStringLiteral("color:#4cd18a"));
    top->addWidget(m_src);

    addToolBarBreak();
    auto* tools = addToolBar(tr("Tools"));
    tools->setObjectName(QStringLiteral("ToolsBar"));
    tools->setMovable(false);
    auto* group = new QActionGroup(this);
    const struct { Tool t; const char* text; Qt::Key key; } defs[] = {
        {Paint, QT_TR_NOOP("🖌 Paint"), Qt::Key_B}, {Rect, QT_TR_NOOP("▭ Rectangle"), Qt::Key_R}, {Erase, QT_TR_NOOP("⌫ Erase"), Qt::Key_E},
        {Select, QT_TR_NOOP("↖ Select / move"), Qt::Key_V}, {Edit, QT_TR_NOOP("✎ Edit"), Qt::Key_O}};
    for (const auto& d : defs) {
        QAction* a = tools->addAction(tr(d.text));
        a->setCheckable(true);
        a->setShortcut(QKeySequence(d.key));
        a->setToolTip(QStringLiteral("%1 (%2)").arg(tr(d.text), QKeySequence(d.key).toString()));
        group->addAction(a);
        const Tool t = d.t;
        connect(a, &QAction::triggered, this, [this, t] { setTool(t); });
        m_toolA[t] = a;
    }
    tools->addSeparator();
    m_gridA = tools->addAction(tr("# Grid"));
    m_gridA->setCheckable(true);
    m_gridA->setChecked(true);
    m_gridA->setShortcut(QKeySequence(Qt::Key_G));
    connect(m_gridA, &QAction::toggled, this, [this](bool on) { m_v.showGrid = on; m_canvas->update(); });
    m_gameA = tools->addAction(tr("🎮 Game view"));
    m_gameA->setCheckable(true);
    m_gameA->setShortcut(QKeySequence(Qt::Key_P));
    m_gameA->setToolTip(tr("The map as the game draws it, without the editor's marks (P)"));
    connect(m_gameA, &QAction::toggled, this, [this](bool on) { m_v.gameView = on; m_canvas->update(); });
    QAction* zo = tools->addAction(QStringLiteral("−"), m_canvas, &CanvasView::zoomOut);
    zo->setShortcut(QKeySequence(Qt::Key_Minus));
    m_zoomLbl = new QLabel(QStringLiteral("100%"), this);
    m_zoomLbl->setMinimumWidth(44);
    m_zoomLbl->setAlignment(Qt::AlignCenter);
    tools->addWidget(m_zoomLbl);
    QAction* zi = tools->addAction(QStringLiteral("+"), m_canvas, &CanvasView::zoomIn);
    zi->setShortcuts({QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
    tools->addAction(tr("Fit"), m_canvas, &CanvasView::fitToView);
    tools->addSeparator();
    m_safeA = tools->addAction(tr("◇ Safe cells"));
    m_safeA->setCheckable(true);
    m_safeA->setToolTip(tr("Free cells the player reaches on the way up without opening optional doors"));
    connect(m_safeA, &QAction::toggled, this, [this](bool on) { m_v.showSafe = on; m_canvas->update(); });
    tools->addAction(tr("🎲 Auto-place events…"), this, &StageEditor::openAutoPlace);
    auto* spacer2 = new QWidget(this);
    spacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(spacer2);
    tools->addWidget(new QLabel(tr("Brush:") + QStringLiteral(" ")));
    m_brushIcon = new QLabel(this);
    m_brushIcon->setFixedSize(24, 24);
    tools->addWidget(m_brushIcon);
    m_brushName = new QLabel(this);
    m_brushName->setStyleSheet(QStringLiteral("font-weight:bold"));
    tools->addWidget(m_brushName);

    // keys without a button
    auto* clear = new QAction(this);
    clear->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    connect(clear, &QAction::triggered, this, [this] {
        if (!m_v.sel || !curDoc()) return;
        const Cell c = *m_v.sel;
        edit(tr("Erase %1 cell(s)").arg(1), [&](stage::StageDoc& d) { d.grid[(size_t)c.y][(size_t)c.x] = '.'; stage::Project::dropObj(d, c.x, c.y); });
    });
    auto* esc = new QAction(this);
    esc->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(esc, &QAction::triggered, this, [this] { m_v.sel.reset(); renderInspector(); m_canvas->update(); });
    m_canvas->addAction(clear);
    m_canvas->addAction(esc);
    for (QAction* a : {clear, esc}) a->setShortcutContext(Qt::WidgetShortcut);
}

void StageEditor::buildDocks() {
    auto* pal = new QDockWidget(tr("Palette"), this);
    pal->setObjectName(QStringLiteral("PaletteDock"));
    m_paletteArea = new QScrollArea(pal);
    m_paletteArea->setWidgetResizable(true);
    m_paletteArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pal->setWidget(m_paletteArea);
    addDockWidget(Qt::LeftDockWidgetArea, pal);

    auto* evd = new QDockWidget(tr("Events"), this);
    evd->setObjectName(QStringLiteral("EventsDock"));
    auto* evw = new QWidget(evd);
    auto* evl = new QVBoxLayout(evw);
    evl->setContentsMargins(4, 4, 4, 4);
    m_evSearch = new QLineEdit(evw);
    m_evSearch->setPlaceholderText(tr("Find event: id, kind or text"));
    m_evSearch->setClearButtonEnabled(true);
    connect(m_evSearch, &QLineEdit::textChanged, this, &StageEditor::renderEventPalette);
    m_evTree = new QTreeWidget(evw);
    m_evTree->setColumnCount(3);
    m_evTree->setHeaderLabels({tr("Event"), tr("Kind"), tr("Placed")});
    m_evTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_evTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_evTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_evTree->setTextElideMode(Qt::ElideMiddle);
    m_evTree->header()->setStretchLastSection(false);
    m_evTree->setRootIsDecorated(true);
    m_evTree->setUniformRowHeights(true);
    connect(m_evTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        const QString id = it->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) setBrush("event:" + id.toStdString());
    });
    evl->addWidget(m_evSearch);
    evl->addWidget(m_evTree, 1);
    evd->setWidget(evw);
    addDockWidget(Qt::LeftDockWidgetArea, evd);
    splitDockWidget(pal, evd, Qt::Vertical);

    auto* ins = new QDockWidget(tr("Inspector"), this);
    ins->setObjectName(QStringLiteral("InspectorDock"));
    m_inspArea = new QScrollArea(ins);
    m_inspArea->setWidgetResizable(true);
    m_inspArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // its texts wrap
    m_inspArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);      // a steady width: wrapping is not redone when it appears
    ins->setWidget(m_inspArea);
    addDockWidget(Qt::RightDockWidgetArea, ins);

    auto* chk = new QDockWidget(tr("Checks"), this);
    chk->setObjectName(QStringLiteral("ChecksDock"));
    m_bottomTabs = new QTabWidget(chk);
    m_bottomTabs->setDocumentMode(true);
    m_checks = new QTreeWidget(m_bottomTabs);
    m_checks->setColumnCount(2);
    m_checks->setHeaderHidden(true);
    m_checks->setRootIsDecorated(false);
    m_checks->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_checks->header()->setStretchLastSection(false);
    m_checks->setWordWrap(true);
    m_bottomTabs->addTab(m_checks, QString());
    auto* storyW = new QWidget(m_bottomTabs);
    auto* sl = new QVBoxLayout(storyW);
    sl->setContentsMargins(0, 0, 0, 0);
    m_storySum = new QLabel(storyW);
    m_storySum->setTextFormat(Qt::RichText);
    m_storySum->setWordWrap(true);
    m_storySum->setContentsMargins(6, 2, 6, 2);
    m_story = new QTableWidget(storyW);
    m_story->setColumnCount(7);
    m_story->verticalHeader()->hide();
    m_story->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_story->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_story->horizontalHeader()->setStretchLastSection(true);
    connect(m_story, &QTableWidget::cellClicked, this, [this](int row, int) {
        const QString fid = m_story->item(row, 0) ? m_story->item(row, 0)->data(Qt::UserRole).toString() : QString();
        if (fid.isEmpty()) return;
        openFloor(fid.toStdString());
        for (const stage::StoryRow& r : stage::storyCheck(m_p))
            if (r.fid == m_cur)
                for (const stage::Issue& i : r.issues)
                    if (!i.cells.empty()) { m_v.sel = i.cells[0]; flashCells({i.cells[0]}); renderInspector(); return; }
    });
    sl->addWidget(m_storySum);
    sl->addWidget(m_story, 1);
    m_bottomTabs->addTab(storyW, QString());
    chk->setWidget(m_bottomTabs);
    connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int i) { if (i == 1) renderStory(); });   // built when shown
    addDockWidget(Qt::BottomDockWidgetArea, chk);
    m_evTree->setIndentation(12);
    resizeDocks({pal, ins}, {330, 330}, Qt::Horizontal);
    resizeDocks({chk}, {170}, Qt::Vertical);
}

// ---- the project ---------------------------------------------------------------------------------------------------

bool StageEditor::openProject(const QString& tomsRoot, QString* error) {
    m_root = tomsRoot;
    m_p.lang = StageText::lang();
    std::string err;
    if (!m_p.load(tomsRoot.toStdString(), err)) {
        if (error) *error = q(err);
        return false;
    }
    m_bank.load(tomsRoot);
    m_src->setText(tr("project folder (live): %1").arg(QDir::toNativeSeparators(tomsRoot)));
    QString synced;
    std::string ierr;
    if (stage::syncFloorIndex(m_p.root, &ierr)) synced = tr("Event index updated (assets/data/events/floor_index.json).");
    watchFiles();
    m_cur.clear();
    openFloor(m_p.floor("F01") ? "F01" : m_p.floors.front().id);
    setTool(Paint);
    setBrush("wall");
    toast(tr("Loaded %1 floors from the project folder.").arg(m_p.floors.size()) + (synced.isEmpty() ? QString() : QStringLiteral("  ") + synced));
    return true;
}

void StageEditor::watchFiles() {
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        auto changed = [this](const QString&) { m_reloadTimer->start(); };
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    }
    // the events, NPC files, dialogues and strings the event editor writes (a replaced file drops out: re-added here)
    QStringList paths;
    const QString data = m_root + QStringLiteral("/assets/data/");
    for (const char* dir : {"events", "npcs", "dialogue"}) {
        const QDir d(data + QString::fromLatin1(dir));
        paths << d.absolutePath();
        for (const QString& f : d.entryList({QStringLiteral("*.json")}, QDir::Files)) paths << d.absoluteFilePath(f);
    }
    paths << data + QStringLiteral("text.json");
    paths.removeAll(data + QStringLiteral("events/floor_index.json"));   // written by both editors' Save: not a reason to reload
    const QStringList have = m_watcher->files() + m_watcher->directories();
    QStringList add;
    for (const QString& p : paths) if (!have.contains(p)) add << p;
    if (!add.isEmpty()) m_watcher->addPaths(add);
}

void StageEditor::reloadEvents() {
    std::string err;
    if (!m_p.reloadEvents(err)) { toast(q(err), true); return; }
    m_p.refreshCleanFloors();
    watchFiles();
    renderAll();
    toast(tr("Events reloaded from the project folder (the event editor saved)."));
}

bool StageEditor::hasUnsaved() const { return m_p.anyDirty() || m_p.dataDirty(); }   // both cached: cheap

// ---- floors and edits ------------------------------------------------------------------------------------------------

void StageEditor::openFloor(const std::string& id) {
    if (!m_p.floor(id)) return;
    const bool changed = id != m_cur;
    m_cur = id;
    m_v.sel.reset();
    m_v.flash.clear();
    m_v.hover.reset();
    m_v.moveFrom.reset();
    m_v.rectA.reset();
    m_v.rectB.reset();
    m_v.painting = false;
    syncTheme();
    renderAll();
    if (changed) m_canvas->floorChanged();
    updateTitle();
    Q_EMIT dirtyChanged();   // the shell's titles name the floor
}

void StageEditor::step(int delta) {
    const int n = (int)m_p.floors.size();
    if (!n) return;
    const int i = std::max(0, m_p.floorIndex(m_cur));
    openFloor(m_p.floors[(size_t)((i + delta + n) % n)].id);
}

bool StageEditor::edit(const QString& label, const std::function<void(stage::StageDoc&)>& fn) {
    stage::StageDoc* d = curDoc();
    if (!d) return false;
    m_p.beginEdit(*d);
    fn(*d);
    if (!m_p.commitEdit(*d, label.toStdString())) return false;
    afterEdit();
    return true;
}

// The open floor's tile theme, for drawing its walls, floor and stairs (an edit, an undo or another floor changes it).
void StageEditor::syncTheme() {
    const stage::StageDoc* d = curDoc();
    m_p.theme = d && d->st.contains("theme") && d->st["theme"].is_string() ? d->st["theme"].get<std::string>() : std::string();
}

void StageEditor::afterEdit() {
    syncTheme();
    renderFloorSel();
    renderInspector();
    m_checksTimer->start();   // the checks re-run the all-floors story check: just after the map shows the edit
    renderEventPalette();
    renderPlaceBar();
    m_canvas->update();
    m_undoA->setEnabled(curDoc() && !curDoc()->undo.empty());
    m_redoA->setEnabled(curDoc() && !curDoc()->redo.empty());
    updateTitle();
    Q_EMIT dirtyChanged();
}

void StageEditor::undo() {
    stage::StageDoc* d = curDoc();
    std::string label;
    if (d && m_p.undo(*d, &label)) { afterEdit(); toast(QStringLiteral("↶ ") + q(label)); }
}

void StageEditor::redo() {
    stage::StageDoc* d = curDoc();
    std::string label;
    if (d && m_p.redo(*d, &label)) { afterEdit(); toast(QStringLiteral("↷ ") + q(label)); }
}

void StageEditor::typesChanged() {
    m_p.dataVer++;   // a type record or a name changed (ObjectWindow): the unsaved check looks again
    renderPalette();
    renderInspector();
    updateBrushInfo();
    m_canvas->update();
    updateTitle();
    Q_EMIT dirtyChanged();
}

// ---- the canvas's mouse --------------------------------------------------------------------------------------------

namespace {
QString whyText(const stage::PlaceCheck& c) {
    if (c.why == stage::PlaceCheck::Edge) return StageEditor::tr("Can't place it here: it would stick out of the map.");
    if (c.why == stage::PlaceCheck::Wall) return StageEditor::tr("Can't place it here: wall at %1, %2.").arg(c.x).arg(c.y);
    return StageEditor::tr("Can't place it here: %1 is at %2, %3. Erase or move it first.").arg(q(c.what)).arg(c.x).arg(c.y);
}
}  // namespace

void StageEditor::canvasPress(const Cell& c) {
    stage::StageDoc* d = curDoc();
    if (!d) return;
    m_v.sel = c;
    m_v.flash.clear();
    const std::string sem = m_p.semAt(*d, c.x, c.y);
    if (m_v.tool == Select) {
        if (stage::Project::isEntity(sem)) m_v.moveFrom = c;
        renderInspector();
        m_canvas->update();
        return;
    }
    if (m_v.tool == Edit) {   // ✎: the object's own settings
        renderInspector();
        m_canvas->update();
        if (stage::Project::isEntity(sem)) ObjectWindow::openObject(this, c.x, c.y);
        return;
    }
    m_p.beginEdit(*d);
    m_v.paintCount = 0;
    if (m_v.tool == Rect) {
        m_v.rectA = m_v.rectB = c;
        m_canvas->update();
        return;
    }
    m_v.painting = true;
    paintAt(c);
}

void StageEditor::canvasHover(const std::optional<Cell>& c, bool leftDown) {
    m_v.hover = c;
    m_hover->setText(c ? hoverText(*c) : QString());
    if (c && leftDown && m_v.painting) paintAt(*c);
    if (c && m_v.rectA) m_v.rectB = *c;
    m_canvas->update();
}

void StageEditor::canvasRelease() {
    stage::StageDoc* d = curDoc();
    if (!d) return;
    if (m_v.painting) {
        m_v.painting = false;
        const QString label = m_v.tool == Erase ? tr("Erase %1 cell(s)").arg(m_v.paintCount)
                                                : tr("Paint %1 (%2 cells)").arg(q(m_p.nameOf(m_v.brush))).arg(m_v.paintCount);
        if (m_p.commitEdit(*d, label.toStdString())) afterEdit();
    }
    if (m_v.rectA) {
        const int x0 = std::min(m_v.rectA->x, m_v.rectB->x), y0 = std::min(m_v.rectA->y, m_v.rectB->y);
        const int x1 = std::max(m_v.rectA->x, m_v.rectB->x), y1 = std::max(m_v.rectA->y, m_v.rectB->y);
        m_v.rectA.reset();
        m_v.rectB.reset();
        int blocked = 0;   // objects only go on free floor; cells this rectangle already filled count as taken
        bool listed = false;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (!m_p.placeCheck(*d, x, y, m_v.brush).ok) { blocked++; continue; }
                if (m_p.putSem(*d, x, y, m_v.brush, &listed)) m_v.paintCount++;
            }
        if (m_p.commitEdit(*d, tr("Paint %1 (%2 cells)").arg(q(m_p.nameOf(m_v.brush))).arg(m_v.paintCount).toStdString())) afterEdit();
        if (blocked) toast(tr("%1 cell(s) skipped: a wall or another object is in the way.").arg(blocked), true);
        if (listed) toast(tr("%1 was added to this floor's events list (it can roll it now).").arg(q(m_v.brush.substr(6))));
        m_canvas->update();
    }
    if (m_v.moveFrom) {
        const Cell from = *m_v.moveFrom;
        m_v.moveFrom.reset();
        if (m_v.hover && *m_v.hover != from) {
            const Cell to = *m_v.hover;
            const std::string sem = m_p.semAt(*d, from.x, from.y);
            const stage::PlaceCheck chk = m_p.placeCheck(*d, to.x, to.y, sem, &from);
            if (!chk.ok) toast(whyText(chk), true);
            else {
                m_v.sel = to;   // before the edit: afterEdit shows it in the inspector (built once)
                edit(tr("Move %1").arg(q(m_p.nameOf(sem))), [&](stage::StageDoc& dd) {
                    const char ch = dd.grid[(size_t)from.y][(size_t)from.x];
                    dd.grid[(size_t)from.y][(size_t)from.x] = '.';
                    dd.grid[(size_t)to.y][(size_t)to.x] = ch;
                    if (const Json* o = stage::Project::objOf(dd, from.x, from.y)) {   // its settings move with it
                        const Json keep = *o;
                        stage::Project::dropObj(dd, from.x, from.y);
                        dd.st["objects"][std::to_string(to.x) + "," + std::to_string(to.y)] = keep;
                    }
                });
            }
        }
        m_canvas->update();
    }
}

void StageEditor::paintAt(const Cell& c) {
    stage::StageDoc* d = curDoc();
    if (!d) return;
    if (m_v.tool == Erase) {
        if (d->at(c.x, c.y) != '.') {
            d->grid[(size_t)c.y][(size_t)c.x] = '.';
            stage::Project::dropObj(*d, c.x, c.y);
            m_v.paintCount++;
        }
    } else {
        const stage::PlaceCheck chk = m_p.placeCheck(*d, c.x, c.y, m_v.brush);
        if (!chk.ok) { toast(whyText(chk), true); m_canvas->update(); return; }
        bool listed = false;
        if (m_p.putSem(*d, c.x, c.y, m_v.brush, &listed)) m_v.paintCount++;
        if (listed) toast(tr("%1 was added to this floor's events list (it can roll it now).").arg(q(m_v.brush.substr(6))));
    }
    if (m_v.brush == "player_start" && m_v.tool == Paint) m_v.painting = false;   // one start only
    if (!m_v.painting && m_v.tool == Paint && m_p.commitEdit(*d, tr("Paint %1 (%2 cells)").arg(q(m_p.nameOf(m_v.brush))).arg(m_v.paintCount).toStdString()))
        afterEdit();
    m_canvas->update();
}

void StageEditor::eyedropper(const Cell& c) {
    stage::StageDoc* d = curDoc();
    if (!d) return;
    const std::string sem = m_p.semAt(*d, c.x, c.y);
    if (!sem.empty()) setBrush(sem);
}

void StageEditor::placeHint(const stage::PlaceCheck& chk) {
    if (m_v.painting) return;
    if (!chk.ok) m_status->setText(QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(esc(whyText(chk))));
}

QString StageEditor::hoverText(const Cell& c) {
    stage::StageDoc* d = curDoc();
    if (!d) return QString();
    const std::string sem = m_p.semAt(*d, c.x, c.y);
    QString what = sem.empty() ? QStringLiteral("?") : q(m_p.nameOf(sem));
    if (startsWith(sem, "event:")) {
        const stage::Event* e = m_p.event(sem.substr(6));
        what += e ? QStringLiteral(" · %1 · %2").arg(q(e->kind), q(m_p.localized(Json(e->text)))) : QStringLiteral(" · ⛔ ") + tr("not defined in any pool");
    }
    if (startsWith(sem, "monster:"))
        if (const Json* m = m_p.typeRec(sem))
            what += QStringLiteral(" · %1 %2 %3 %4 %5 %6").arg(tr("HP")).arg(m->value("hp", 0)).arg(tr("ATK")).arg(m->value("atk", 0)).arg(tr("DEF")).arg(m->value("def", 0));
    return QStringLiteral("(%1, %2)  '%3'  %4").arg(c.x).arg(c.y).arg(QChar(d->at(c.x, c.y))).arg(what);
}

void StageEditor::flashCells(const std::vector<Cell>& cells) {
    m_v.flash = cells;
    if (!cells.empty()) m_canvas->showCell(cells[0]);
    m_canvas->update();
    m_flashTimer->start(1600);
}

void StageEditor::showEventOnFloor(const std::string& floor, const std::string& eventId) {
    openFloor(floor);
    if (eventId.empty()) return;
    if (const auto c = m_p.find(*curDoc(), "event:" + eventId)) {
        m_v.sel = *c;
        flashCells({*c});
        renderInspector();
    }
}

void StageEditor::selectCell(const Cell& c) {
    stage::StageDoc* d = curDoc();
    if (!d || !d->inside(c.x, c.y)) return;
    m_v.sel = c;
    renderInspector();
    m_canvas->showCell(c);
}

void StageEditor::enterPlaceMode(const std::string& id, const std::string& kind, const std::vector<std::string>& floors) {
    if (id.empty()) return;
    if (!m_p.event(id)) m_p.addPendingEvent(id, kind);
    PlaceMode pm;
    pm.id = id;
    for (const std::string& f : floors) if (m_p.floor(f)) pm.floors.push_back(f);
    if (pm.floors.empty()) pm.floors.push_back(m_cur);
    m_v.placeMode = pm;
    openFloor(pm.floors[0]);
    setTool(Paint);
    setBrush("event:" + id);
    renderPlaceBar();
}

void StageEditor::setTool(Tool t) {
    m_v.tool = t;
    updateToolButtons();
    m_canvas->update();
}

void StageEditor::setBrush(const std::string& sem) {
    m_v.brush = sem;
    if (m_v.tool == Erase || m_v.tool == Select) setTool(Paint);
    updateBrushInfo();
    renderPalette();
    renderEventPalette();
}

void StageEditor::toast(const QString& msg, bool bad) {
    m_status->setText(bad ? QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(esc(msg)) : esc(msg));
    statusBar()->showMessage(QString());
    static QTimer* t = nullptr;
    if (!t) {
        t = new QTimer(this);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, [this] { m_status->clear(); });
    }
    t->start(6000);
}

// ---- rendering the panels --------------------------------------------------------------------------------------------

void StageEditor::retranslateDynamic() {
    m_storyKey.clear();
    m_p.lang = StageText::lang();
    renderAll();
}

void StageEditor::renderAll() {
    if (m_cur.empty()) return;
    renderFloorSel();
    renderPalette();
    renderEventPalette();
    renderInspector();
    renderProblems();
    renderPlaceBar();
    updateBrushInfo();
    updateToolButtons();
    m_undoA->setEnabled(!curDoc()->undo.empty());
    m_redoA->setEnabled(!curDoc()->redo.empty());
    m_canvas->update();
}

void StageEditor::updateTitle() {
    const bool any = hasUnsaved();
    m_saveA->setText(tr("Save") + (any ? QStringLiteral(" •") : QString()));
    setWindowTitle(tr("TOMS Stage Editor") + QStringLiteral(" — ") + q(m_cur) + (m_p.isDirty(m_cur) ? QStringLiteral(" ●") : QString()));
}

void StageEditor::updateToolButtons() {
    for (int i = 0; i < 5; i++) if (m_toolA[i]) m_toolA[i]->setChecked(i == m_v.tool);
}

void StageEditor::updateBrushInfo() {
    m_brushIcon->setPixmap(QPixmap::fromImage(StageDraw::icon(m_p, m_bank, m_v.brush, 24)));
    QString name = q(m_p.nameOf(m_v.brush));
    if (startsWith(m_v.brush, "event:"))
        if (const stage::Event* e = m_p.event(m_v.brush.substr(6))) name += QStringLiteral(" · ") + q(e->kind);
    m_brushName->setText(name);
}

void StageEditor::renderFloorSel() {
    m_floorSel->blockSignals(true);
    m_floorSel->clear();
    for (const stage::Floor& f : m_p.floors) {
        const std::string act = f.spec.value("act", std::string());
        const bool hand = f.spec.contains("handAuthoredStage") && f.spec["handAuthoredStage"].is_string();
        m_floorSel->addItem((m_p.isDirty(f.id) ? QStringLiteral("● ") : QString()) + q(f.id) + QStringLiteral(" · ") + q(act)
                                + (hand ? QStringLiteral(" · ★") : QString()), q(f.id));
        if (f.id == m_cur) m_floorSel->setCurrentIndex(m_floorSel->count() - 1);
    }
    m_floorSel->blockSignals(false);
    updateTitle();
}

void StageEditor::renderPalette() {
    stage::StageDoc* d = curDoc();
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(2);
    std::vector<std::string> items, story;
    for (const std::string& k : m_p.itemIds()) {
        const std::string t = m_p.itemType(k);
        const stage::Json* rec = m_p.typeRec("item:" + k);
        const bool gateKey = t == "story" && rec && rec->contains("opens");
        if ((t == "consumable" && !startsWith(k, "key_")) || (t == "story" && !gateKey)) items.push_back(k);   // event items too
        if (gateKey) story.push_back(k);   // a key that opens its own gate
    }
    auto prefixed = [](const char* pre, const std::vector<std::string>& ids) {
        std::vector<std::string> out;
        for (const std::string& id : ids) out.push_back(pre + id);
        return out;
    };
    // Doors: the three coloured doors, then every door that takes one key of its own (a gate, "gate:<item id>"); Keys:
    // the three keys, then those own keys (story items, which can be put on the map too).
    auto join = [](std::vector<std::string> a, const std::vector<std::string>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
    const std::vector<std::pair<QString, std::vector<std::string>>> builtin = {
        {tr("Terrain"), {"wall", "floor"}}, {tr("Stairs"), {"stairs_up", "stairs_down"}}, {tr("Player"), {"player_start"}},
        {tr("Doors"), join({"door:yellow", "door:blue", "door:red"}, prefixed("gate:", story))},
        {tr("Keys"), join({"key:yellow", "key:blue", "key:red"}, prefixed("item:", story))},
        {tr("Monsters"), prefixed("monster:", m_p.enemyIds())}, {tr("NPCs"), prefixed("npc:", m_p.npcIds())},
        {tr("Items"), prefixed("item:", items)}};
    // Every group has a +. A door with its own key (a "story" item) is how a new kind of door is made: its gate is the
    // door only that key opens. Terrain, stairs and the player's start are the game's own: nothing to add there.
    const QString doorKey = tr("Add a new door and its key (an item only that door takes)");
    const QString builtIn = tr("Built into the game: walls, floor, stairs and the start have no data to copy");
    // the + of each group: the category made, what the new one is painted as (its place in this group), the tip
    struct Add { std::string cat, brush; QString tip; };
    const std::map<QString, Add> addable = {
        {tr("Monsters"), {"monster", "monster:", tr("Add a new monster type")}}, {tr("Items"), {"item", "item:", tr("Add a new item type")}},
        {tr("NPCs"), {"npc", "npc:", tr("Add a new NPC")}},
        {tr("Doors"), {"story", "gate:", doorKey}}, {tr("Keys"), {"story", "item:", doorKey}},
        {tr("Terrain"), {"", "", builtIn}}, {tr("Stairs"), {"", "", builtIn}}, {tr("Player"), {"", "", builtIn}}};
    // The palette's own categories: a type whose record names one ("group") shows there, not in its usual group.
    struct Group { QString title; std::vector<std::string> list; std::string custom; };
    std::vector<Group> groups;
    std::vector<std::string> customNames = m_p.paletteGroups();
    std::map<std::string, std::vector<std::string>> inCustom;
    for (const auto& [title, list] : builtin) {
        Group g{title, {}, std::string()};
        for (const std::string& sem : list) {
            const std::string grp = m_p.groupOf(sem);
            if (grp.empty()) { g.list.push_back(sem); continue; }
            inCustom[grp].push_back(sem);
            if (std::find(customNames.begin(), customNames.end(), grp) == customNames.end()) customNames.push_back(grp);
        }
        groups.push_back(std::move(g));
    }
    for (const std::string& n : customNames) groups.push_back({q(n), inCustom[n], n});
    // + New category, above the groups
    auto* newCat = new QPushButton(tr("+ New category"));
    newCat->setToolTip(tr("A category of your own: its + makes types that go into it; right-click a type to move it there"));
    connect(newCat, &QPushButton::clicked, this, &StageEditor::newCategoryDialog);
    v->addWidget(newCat);
    for (const auto& [title, list, custom] : groups) {
        const bool collapsed = m_collapsed.count((custom.empty() ? "" : "custom:") + title.toStdString()) > 0;
        auto* head = new QHBoxLayout();
        auto* h = new QToolButton;
        h->setText((collapsed ? QStringLiteral("▸ ") : QStringLiteral("▾ ")) + title);
        h->setAutoRaise(true);
        h->setStyleSheet(QStringLiteral("font-weight:bold; color:#8b929c"));
        const std::string key = (custom.empty() ? "" : "custom:") + title.toStdString();
        if (!custom.empty()) h->setStyleSheet(QStringLiteral("font-weight:bold; color:#c9a8ff"));   // one of yours
        connect(h, &QToolButton::clicked, this, [this, key] {
            if (m_collapsed.count(key)) m_collapsed.erase(key); else m_collapsed.insert(key);
            QTimer::singleShot(0, this, &StageEditor::renderPalette);
        });
        head->addWidget(h);
        auto add = addable.find(title);
        if (add != addable.end()) {
            auto* plus = new QToolButton;
            plus->setText(QStringLiteral("+"));
            plus->setToolTip(add->second.tip);
            const std::string cat = add->second.cat, brush = add->second.brush;
            plus->setEnabled(!cat.empty());
            connect(plus, &QToolButton::clicked, this, [this, cat, brush] { ObjectWindow::openNew(this, cat, brush); });
            head->addWidget(plus);
        }
        if (!custom.empty()) {   // one of yours: + (which kind), rename, delete
            auto* plus = new QToolButton;
            plus->setText(QStringLiteral("+"));
            plus->setToolTip(tr("Add a new type to this category"));
            plus->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(plus);
            const std::pair<QString, std::pair<std::string, std::string>> kinds[] = {
                {tr("Monster"), {"monster", "monster:"}}, {tr("Item"), {"item", "item:"}}, {tr("NPC"), {"npc", "npc:"}},
                {tr("Door and its key"), {"story", "gate:"}}};
            for (const auto& [label, ck] : kinds) {
                const std::string cat = ck.first, brush = ck.second;
                m->addAction(label, this, [this, cat, brush, custom] { ObjectWindow::openNew(this, cat, brush, custom); });
            }
            plus->setMenu(m);
            head->addWidget(plus);
            auto* ren = new QToolButton;
            ren->setText(QStringLiteral("✎"));
            ren->setToolTip(tr("Rename this category"));
            connect(ren, &QToolButton::clicked, this, [this, custom] { renameCategoryDialog(custom); });
            head->addWidget(ren);
            auto* del = new QToolButton;
            del->setText(QStringLiteral("✕"));
            del->setToolTip(tr("Delete this category (its types go back to their usual groups)"));
            connect(del, &QToolButton::clicked, this, [this, custom] { deleteCategoryDialog(custom); });
            head->addWidget(del);
        }
        head->addStretch(1);
        v->addLayout(head);
        if (collapsed) continue;
        auto* grid = new QWidget;
        auto* flow = new FlowLayout(grid, 4);
        for (const std::string& sem : list) {
            auto* b = new PaletteButton;
            QImage img = StageDraw::icon(m_p, m_bank, sem, 32);
            if (d) {   // the char this floor uses for it (or its usual one)
                std::string ch;
                for (auto& [c, val] : d->st["legend"].items()) if (val == sem) { ch = c; break; }
                if (ch.empty()) { auto pc = stage::preferredChars().find(sem); if (pc != stage::preferredChars().end()) ch = std::string(1, pc->second); }
                if (!ch.empty()) {
                    QPainter g(&img);
                    QFont f(QStringLiteral("Consolas"));
                    f.setPixelSize(10);
                    g.setFont(f);
                    g.setPen(Qt::black);
                    g.drawText(QRect(0, 0, 31, 33), Qt::AlignRight | Qt::AlignBottom, q(ch));
                    g.setPen(QColor("#d7dbe0"));
                    g.drawText(QRect(0, 0, 30, 32), Qt::AlignRight | Qt::AlignBottom, q(ch));
                }
            }
            b->setIcon(QIcon(QPixmap::fromImage(img)));
            b->setIconSize(QSize(32, 32));
            b->setFixedSize(40, 40);
            b->setCheckable(true);
            b->setChecked(sem == m_v.brush);
            QString tip = q(m_p.nameOf(sem)) + QStringLiteral(" · ") + q(sem);
            // the record a click shows (a gate's is its key item's; a key is the item key_<colour>)
            const std::string ts = startsWith(sem, "gate:") ? "item:" + sem.substr(5)
                                 : startsWith(sem, "key:") ? "item:key_" + sem.substr(4) : sem;
            const bool hasRecord = (startsWith(ts, "monster:") || startsWith(ts, "item:") || startsWith(ts, "npc:")) && m_p.typeRec(ts);
            if (hasRecord) tip += QStringLiteral(" · ") + tr("click: paint with it and edit its details");
            b->setToolTip(tip);
            if (hasRecord) {
                b->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(b, &QWidget::customContextMenuRequested, this, [this, b, sem] {
                    QMenu menu(b);
                    QMenu* to = menu.addMenu(tr("Move to category"));
                    const std::string now = m_p.groupOf(sem);
                    for (const std::string& n : m_p.paletteGroups()) {
                        QAction* a = to->addAction(q(n), this, [this, sem, n] { moveToCategory(sem, n); });
                        a->setCheckable(true);
                        a->setChecked(n == now);
                    }
                    if (m_p.paletteGroups().empty()) to->addAction(tr("(no categories yet: + New category)"))->setEnabled(false);
                    to->addSeparator();
                    QAction* usual = to->addAction(tr("Its usual group"), this, [this, sem] { moveToCategory(sem, std::string()); });
                    usual->setEnabled(!now.empty());
                    menu.addAction(tr("New category with it…"), this, [this, sem] {
                        bool ok = false;
                        const QString name = QInputDialog::getText(this, tr("New category"), tr("Name"), QLineEdit::Normal, QString(), &ok).trimmed();
                        if (ok && addCategory(name.toStdString())) moveToCategory(sem, name.toStdString());
                    });
                    menu.exec(QCursor::pos());
                });
            }
            connect(b, &QToolButton::clicked, this, [this, sem, ts, hasRecord] {
                setBrush(sem);
                if (hasRecord) ObjectWindow::openType(this, ts);   // the details window follows the palette
            });
            flow->addWidget(b);
        }
        v->addWidget(grid);
    }
    v->addStretch(1);
    const int scroll = m_paletteArea->verticalScrollBar() ? m_paletteArea->verticalScrollBar()->value() : 0;
    m_paletteArea->setWidget(w);
    QTimer::singleShot(0, this, [this, scroll] {
        m_paletteArea->verticalScrollBar()->setValue(scroll);
        if (!m_revealBrush) return;
        m_revealBrush = false;
        for (QToolButton* b : m_paletteArea->widget()->findChildren<QToolButton*>())
            if (b->isCheckable() && b->isChecked()) { m_paletteArea->ensureWidgetVisible(b, 8, 24); break; }
    });
}

void StageEditor::renderEventPalette() {
    stage::StageDoc* d = curDoc();
    if (!d) return;
    const QString query = m_evSearch->text().trimmed().toLower();
    std::map<std::string, int> placed = m_p.placedEvents(*d);
    auto match = [&](const std::string& id) {
        if (query.isEmpty()) return true;
        const stage::Event* e = m_p.event(id);
        QStringList hay{q(id)};
        if (e) {
            hay << q(e->kind) << q(e->pool);
            const Json t = m_p.textOf(e->text);
            for (const char* l : {"zh_TW", "en"}) if (t.contains(l) && t[l].is_string()) hay << q(t[l].get<std::string>());
        }
        for (const QString& h : hay) if (h.toLower().contains(query)) return true;
        return false;
    };
    const QIcon plate = plateIcon();
    auto row = [&](QTreeWidgetItem* parent, const std::string& id) {
        const stage::Event* e = m_p.event(id);
        auto* it = new QTreeWidgetItem(parent);
        it->setIcon(0, plate);
        it->setText(0, q(id));
        it->setData(0, Qt::UserRole, q(id));
        it->setText(1, e ? q(e->kind) : QStringLiteral("⛔"));
        if (e) it->setForeground(1, StageDraw::kindColor(m_p, e->kind));
        else it->setForeground(0, QColor("#ff9b9b"));
        if (placed[id]) { it->setText(2, QStringLiteral("✓ %1").arg(placed[id])); it->setForeground(2, QColor("#4cd18a")); }
        it->setToolTip(0, e ? q(m_p.localized(Json(e->text))) : tr("not defined in any pool"));
        if ("event:" + id == m_v.brush) m_evTree->setCurrentItem(it);
    };
    m_evTree->setUpdatesEnabled(false);
    m_evTree->clear();
    std::vector<std::string> mine;
    if (d->spec.contains("events") && d->spec["events"].is_array())
        for (const Json& e : d->spec["events"]) if (e.is_string()) mine.push_back(e.get<std::string>());
    auto* floorGroup = new QTreeWidgetItem(m_evTree, {tr("This floor can roll") + QStringLiteral(" (%1)").arg(mine.size())});
    for (const std::string& id : mine) if (match(id)) row(floorGroup, id);
    auto* all = new QTreeWidgetItem(m_evTree, {tr("All events")});
    for (const std::string& pool : m_p.pools()) {
        QTreeWidgetItem* pg = nullptr;
        for (const stage::Event& e : m_p.events) {
            if (e.pool != pool || !match(e.id)) continue;
            if (!pg) pg = new QTreeWidgetItem(all, {q(pool)});
            row(pg, e.id);
        }
    }
    for (QTreeWidgetItem* g : {floorGroup, all}) {
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFirstColumnSpanned(true);
    }
    floorGroup->setExpanded(true);
    all->setExpanded(!query.isEmpty() || true);
    for (int i = 0; i < all->childCount(); i++) { all->child(i)->setExpanded(!query.isEmpty()); all->child(i)->setFirstColumnSpanned(true); }
    m_evTree->setUpdatesEnabled(true);
}

QWidget* StageEditor::npcPanel(const std::string& npc) {
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    const Json* f = m_p.typeRec("npc:" + npc);
    v->addWidget(heading(tr("NPC") + QStringLiteral(" · ") + q(m_p.nameOf("npc:" + npc))));
    if (f) {
        const size_t lines = f->contains("chat") && (*f)["chat"].contains("lines") && (*f)["chat"]["lines"].is_array() ? (*f)["chat"]["lines"].size() : 0;
        v->addWidget(rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("%1 chat line(s) when no topic is offered").arg(lines))));
    } else {
        v->addWidget(rich(QStringLiteral("<span style='color:#ff9f43'>⚠ %1</span>").arg(tr("No NPC file yet: it plays dialogue/<id>.json").toHtmlEscaped())));
    }
    // its topics (talk events) in the game's menu order, marked by where they can be talked about
    std::vector<std::string> pools = m_p.pools();
    std::sort(pools.begin(), pools.end());
    std::vector<std::pair<size_t, const stage::Event*>> topics;
    for (size_t i = 0; i < m_p.events.size(); i++) {
        const stage::Event& e = m_p.events[i];
        const Json* tr2 = e.rec.contains("trigger") ? &e.rec["trigger"] : nullptr;
        if (tr2 && tr2->value("on", std::string()) == "talk" && tr2->value("npc", std::string()) == npc) topics.push_back({i, &e});
    }
    auto order = [](const stage::Event* e) { return e->rec.contains("topicOrder") && e->rec["topicOrder"].is_number() ? e->rec["topicOrder"].get<double>() : 1e9; };
    auto poolIdx = [&](const std::string& p2) { return std::find(pools.begin(), pools.end(), p2) - pools.begin(); };
    std::stable_sort(topics.begin(), topics.end(), [&](const auto& a, const auto& b) {
        if (order(a.second) != order(b.second)) return order(a.second) < order(b.second);
        if (poolIdx(a.second->pool) != poolIdx(b.second->pool)) return poolIdx(a.second->pool) < poolIdx(b.second->pool);
        return a.first < b.first;
    });
    std::function<std::string(const Json&)> onStageOf = [&](const Json& c) -> std::string {
        if (!c.is_object()) return std::string();
        if (c.value("type", std::string()) == "onStage") return c.value("stageId", std::string());
        if (c.contains("all") && c["all"].is_array())
            for (const Json& x : c["all"]) { const std::string s = onStageOf(x); if (!s.empty()) return s; }
        return std::string();
    };
    if (topics.empty()) v->addWidget(rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("No talk events."))));
    for (const auto& [i, e] : topics) {
        (void)i;
        const std::string where = e->rec.contains("requires") ? onStageOf(e->rec["requires"]) : std::string();
        const bool here = where.empty() || where == m_cur;
        QString info = where.empty() ? tr("on every floor it stands on") : where == m_cur ? tr("only on this floor") : tr("only on %1").arg(q(where));
        if (e->rec.contains("requires")) info += QStringLiteral(" · ") + tr("has conditions");
        if (e->rec.contains("afterDialogue")) info += QStringLiteral(" · ✓ ") + tr("after-dialogue");
        if (e->rec.contains("once") && e->rec["once"] == false)
            info += QStringLiteral(" · ↻") + (e->rec.contains("maxTimes") ? QStringLiteral(" ×%1").arg(q(stage::numText(e->rec["maxTimes"]))) : QString());
        QString title = q(m_p.localized(Json(e->title)));
        if (title.isEmpty() || title == q(e->title)) title = q(e->id);
        // one wrapping label per topic; ↗ is a link in it (a label beside a button would not wrap)
        auto* l = rich(QStringLiteral("%1 <b>%2</b> <code>%3</code> <a href='open' style='color:#4c9eff; text-decoration:none'>↗</a>"
                                      "<br><span style='color:#8b929c;font-size:11px'>%4</span>")
                           .arg(here ? QStringLiteral("💬") : QStringLiteral("·"), esc(title), esc(e->id), esc(info)));
        l->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);
        l->setToolTip(tr("Open in the event editor"));
        if (!here) l->setStyleSheet(QStringLiteral("color: rgba(215,219,224,128)"));
        const QString id = q(e->id);
        connect(l, &QLabel::linkActivated, this, [this, id] { Q_EMIT openEventEditor(id); });
        v->addWidget(l);
    }
    auto* edit = new QPushButton(tr("Edit in the event editor →"));
    connect(edit, &QPushButton::clicked, this, [this, npc] { Q_EMIT openNpcInEventEditor(q(npc)); });
    v->addWidget(edit, 0, Qt::AlignLeft);
    return box;
}

QWidget* StageEditor::floorPanel(stage::StageDoc& d) {
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(heading(tr("Floor")));
    const bool hand = d.spec.contains("handAuthoredStage") && d.spec["handAuthoredStage"].is_string();
    v->addWidget(rich(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2 · %3</span><br><span style='color:#8b929c;font-size:11px'>%4</span>")
                          .arg(esc(d.f->id), esc(d.spec.value("act", std::string())), esc(d.spec.value("role", std::string())),
                               hand ? esc(tr("Hand-made boss stage (%1).").arg(q(d.f->stagePath)))
                                    : esc(tr("Generated grid: running the generator again would replace hand edits.")))));
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);   // labels above: the panel stays narrow
    for (const char* l : {"zh_TW", "en"}) {
        const std::string lang = l;
        auto* e = new QLineEdit(d.st.contains("name") && d.st["name"].is_object() ? q(d.st["name"].value(lang, std::string())) : QString());
        connect(e, &QLineEdit::editingFinished, this, [this, e, lang] {
            const std::string val = e->text().toStdString();
            QTimer::singleShot(0, this, [this, val, lang] {
                edit(tr("Rename floor"), [&](stage::StageDoc& dd) {
                    if (!dd.st.contains("name") || !dd.st["name"].is_object()) dd.st["name"] = Json::object();
                    dd.st["name"][lang] = val;
                });
            });
        });
        form->addRow(tr("Name") + QStringLiteral(" · ") + q(lang), e);
    }
    {   // the floor's tile theme: its walls, floor and stairs (wall_<theme> ... in the atlas)
        auto* th = new QComboBox;
        th->addItem(tr("(default)"), QString());
        QStringList themes;
        for (const std::string& s : m_p.sprites)
            if (startsWith(s, "wall_")) themes << q(s.substr(5));
        themes.sort();
        for (const QString& t : themes) th->addItem(t, t);
        const QString cur = d.st.contains("theme") && d.st["theme"].is_string() ? q(d.st["theme"].get<std::string>()) : QString();
        th->setCurrentIndex(std::max(0, th->findData(cur)));
        connect(th, &QComboBox::currentIndexChanged, this, [this, th](int) {
            const std::string val = th->currentData().toString().toStdString();
            QTimer::singleShot(0, this, [this, val] {
                edit(tr("Floor theme → %1").arg(val.empty() ? tr("(default)") : q(val)), [&](stage::StageDoc& dd) {
                    if (val.empty()) dd.st.erase("theme");
                    else dd.st["theme"] = val;
                });
                m_p.theme = val;
                renderPalette();
                m_canvas->update();
            });
        });
        form->addRow(tr("Floor theme"), th);
    }
    for (const char* k : {"up", "down"}) {
        const std::string key = k;
        const Json* cn = d.st.contains("connect") ? &d.st["connect"] : nullptr;
        auto* e = new QLineEdit(cn && cn->contains(key) && (*cn)[key].is_string() ? q((*cn)[key].get<std::string>()) : QString());
        connect(e, &QLineEdit::editingFinished, this, [this, e, key] {
            const std::string val = e->text().trimmed().toStdString();
            QTimer::singleShot(0, this, [this, val, key] {
                edit(tr("connect.%1 → %2").arg(q(key), val.empty() ? tr("(none)") : q(val)), [&](stage::StageDoc& dd) {
                    if (!dd.st.contains("connect") || !dd.st["connect"].is_object()) dd.st["connect"] = Json::object();
                    dd.st["connect"][key] = val.empty() ? Json() : Json(val);
                });
            });
        });
        form->addRow(key == "up" ? tr("Stairs up to") : tr("Stairs down to"), e);
    }
    auto* size = new QHBoxLayout();
    auto* sw = new QSpinBox;
    auto* sh = new QSpinBox;
    sw->setRange(5, 60);
    sh->setRange(5, 60);
    sw->setValue(d.width());
    sh->setValue(d.height());
    auto* apply = new QPushButton(tr("Apply size"));
    connect(apply, &QPushButton::clicked, this, [this, sw, sh] {
        const int W = sw->value(), H = sh->value();
        edit(tr("Resize to %1 × %2").arg(W).arg(H), [&](stage::StageDoc& dd) {
            const std::vector<std::string> old = dd.grid;
            const int oh = (int)old.size(), ow = oh ? (int)old[0].size() : 0;
            dd.grid.assign((size_t)H, std::string((size_t)W, '.'));
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    dd.grid[(size_t)y][(size_t)x] = (y == 0 || x == 0 || y == H - 1 || x == W - 1) ? '#'
                        : (y < oh - 1 && x < ow - 1) ? old[(size_t)y][(size_t)x] : '.';
        });
        m_v.sel.reset();
        m_canvas->floorChanged();
        renderInspector();
    });
    size->addWidget(sw);
    size->addWidget(new QLabel(QStringLiteral("×")));
    size->addWidget(sh);
    size->addWidget(apply);
    size->addStretch(1);
    form->addRow(tr("Size (w × h)"), size);
    v->addLayout(form);
    // the events this floor can roll: green = placed; a click paints it
    const std::map<std::string, int> placed = m_p.placedEvents(d);
    std::vector<std::string> list;
    if (d.spec.contains("events") && d.spec["events"].is_array())
        for (const Json& e : d.spec["events"]) if (e.is_string()) list.push_back(e.get<std::string>());
    v->addWidget(heading(tr("Events this floor can roll (%1)").arg(list.size())));
    auto* chips = new QWidget;
    auto* flow = new FlowLayout(chips, 4);
    for (const std::string& id : list) {
        auto* c = new QToolButton;
        c->setText((placed.count(id) ? QStringLiteral("✓ ") : QString()) + q(id));
        c->setStyleSheet(!m_p.event(id) ? QStringLiteral("color:#ff9b9b") : placed.count(id) ? QStringLiteral("color:#4cd18a") : QString());
        connect(c, &QToolButton::clicked, this, [this, id] { setBrush("event:" + id); });
        flow->addWidget(c);
    }
    v->addWidget(chips);
    v->addWidget(rich(QStringLiteral("<span style='color:#8b929c;font-size:11px'>%1</span>").arg(tr("Green = placed on the grid. Click one to paint it."))));
    return box;
}

void StageEditor::renderInspector() {
    stage::StageDoc* d = curDoc();
    if (!d) return;
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSizeConstraint(QLayout::SetNoConstraint);   // the panel's width wins: long rows wrap or shrink
    if (m_v.sel && d->inside(m_v.sel->x, m_v.sel->y)) {
        const Cell c = *m_v.sel;
        const char ch = d->at(c.x, c.y);
        const std::string sem = m_p.semAt(*d, c.x, c.y);
        v->addWidget(heading(tr("Cell %1, %2").arg(c.x).arg(c.y)));
        v->addWidget(rich(QStringLiteral("<table><tr><td style='color:#8b929c'>%1&nbsp;</td><td>%2</td></tr><tr><td style='color:#8b929c'>%3&nbsp;</td><td><code>%4</code> → <code>%5</code></td></tr></table>")
                              .arg(tr("Object"), sem.empty() ? QStringLiteral("⛔ ?") : esc(m_p.nameOf(sem)), tr("Legend char"),
                                   QString(QChar(ch)).toHtmlEscaped(), sem.empty() ? QStringLiteral("?") : esc(sem))));
        if (startsWith(sem, "monster:")) {
            const Json* m = m_p.typeRec(sem);
            const stage::Footprint fp = m_p.footprintOf(*d, ch, sem);
            auto val = [&](const char* k) { return m && m->contains(k) ? q(stage::numText((*m)[k])) : QStringLiteral("?"); };
            v->addWidget(rich(QStringLiteral("%1 %2 · %3 %4 · %5 %6 · %7 %8 × %9").arg(tr("HP"), val("hp"), tr("ATK"), val("atk"), tr("DEF"), val("def"), tr("Size"))
                                  .arg(fp.w).arg(fp.h)));
        }
        if (startsWith(sem, "gate:")) {
            const std::string item = sem.substr(5);
            v->addWidget(rich(QStringLiteral("%1: %2").arg(tr("Opens with"),
                m_p.typeRec("item:" + item) ? esc(m_p.nameOf("item:" + item)) + QStringLiteral(" <code>%1</code>").arg(esc(item))
                                            : QStringLiteral("⛔ %1 · %2").arg(esc(item), tr("no such item")))));
        }
        if (startsWith(sem, "item:"))
            if (const Json* it = m_p.typeRec(sem))
                v->addWidget(rich(QStringLiteral("%1: <code>%2</code>").arg(tr("Effect"), esc(it->value("stats", it->value("effect", Json::object())).dump()))));
        if (startsWith(sem, "event:")) {
            const std::string id = sem.substr(6);
            const stage::Event* e = m_p.event(id);
            if (e && e->pending) v->addWidget(rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("From the event editor; not saved in a pool yet."))));
            if (e) {
                const Json t = m_p.textOf(e->text);
                v->addWidget(rich(QStringLiteral("%1: <span style='color:%2'>%3</span> · %4: %5").arg(tr("Kind"), StageDraw::kindColor(m_p, e->kind).name(), esc(e->kind), tr("Pool"), esc(e->pool))));
                for (const char* l : {"zh_TW", "en"}) {
                    auto* txt = rich(QStringLiteral("<div style='color:#8b929c;font-size:11px'>%1 · %2</div>%3").arg(tr("Text"), QString::fromLatin1(l),
                        t.contains(l) && t[l].is_string() ? esc(t[l].get<std::string>()).replace(QStringLiteral("\n"), QStringLiteral("<br>")) : QStringLiteral("—")));
                    txt->setStyleSheet(QStringLiteral("background:#0d0f12; border:1px solid #6b5a3a; border-radius:4px; padding:6px; color:#f3e6c8"));
                    v->addWidget(txt);
                }
            } else {
                v->addWidget(rich(QStringLiteral("<span style='color:#ff9f43'>⛔ %1</span>").arg(tr("not defined in any pool"))));
            }
            std::vector<std::string> rollers;
            for (const stage::Floor& f : m_p.floors) {
                const Json& sp = m_p.doc(f.id).spec;
                if (sp.contains("events") && sp["events"].is_array() && std::find(sp["events"].begin(), sp["events"].end(), Json(id)) != sp["events"].end())
                    rollers.push_back(f.id);
            }
            v->addWidget(heading(tr("Floors that can roll it (%1)").arg(rollers.size())));
            auto* chips = new QWidget;
            auto* flow = new FlowLayout(chips, 4);
            for (const std::string& f : rollers) {
                auto* b = new QToolButton;
                b->setText(q(f));
                connect(b, &QToolButton::clicked, this, [this, f] { QTimer::singleShot(0, this, [this, f] { openFloor(f); }); });
                flow->addWidget(b);
            }
            v->addWidget(chips);
            auto* repl = new QComboBox;
            for (const stage::Event& ev : m_p.events) repl->addItem(q(ev.id));
            repl->setCurrentText(q(id));
            connect(repl, &QComboBox::textActivated, this, [this, c](const QString& nid) {
                QTimer::singleShot(0, this, [this, c, nid] {
                    edit(tr("Paint %1 (%2 cells)").arg(nid).arg(1), [&](stage::StageDoc& dd) { m_p.putSem(dd, c.x, c.y, "event:" + nid.toStdString()); });
                });
            });
            auto* rr = new QHBoxLayout();
            rr->addWidget(new QLabel(tr("Replace with")));
            rr->addWidget(repl, 1);
            v->addLayout(rr);
            auto* open = new QPushButton(tr("Open in the event editor"));
            connect(open, &QPushButton::clicked, this, [this, id] { Q_EMIT openEventEditor(q(id)); });
            v->addWidget(open, 0, Qt::AlignLeft);
        }
        if (startsWith(sem, "npc:")) v->addWidget(npcPanel(sem.substr(4)));
        if (stage::Project::isEntity(sem) && sem != "player_start" && !startsWith(sem, "stairs")) {
            const Json* o = stage::Project::objOf(*d, c.x, c.y);
            QStringList parts;
            if (o && o->contains("look") && (*o)["look"].is_object() && !(*o)["look"].empty()) {
                QStringList keys;
                for (auto& [k, val] : (*o)["look"].items()) { (void)val; keys << q(k); }
                parts << tr("own look (%1)").arg(keys.join(QStringLiteral(", ")));
            }
            if (o && o->contains("events") && (*o)["events"].is_array() && !(*o)["events"].empty()) {
                QStringList ids;
                for (const Json& e : (*o)["events"]) ids << q(e.get<std::string>());
                parts << tr("linked: %1").arg(ids.join(QStringLiteral(", ")));
            }
            v->addWidget(heading(tr("This object")));
            v->addWidget(rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(esc(parts.isEmpty() ? tr("no settings of its own") : parts.join(QStringLiteral(" · "))))));
            auto* br = new QHBoxLayout();
            auto* oe = new QPushButton(QStringLiteral("✎ ") + tr("Edit object"));
            connect(oe, &QPushButton::clicked, this, [this, c] { ObjectWindow::openObject(this, c.x, c.y); });
            br->addWidget(oe);
            const std::string ts = startsWith(sem, "gate:") ? "item:" + sem.substr(5) : sem;
            if (startsWith(ts, "monster:") || startsWith(ts, "item:") || startsWith(ts, "npc:")) {
                auto* te = new QPushButton(tr("Type details"));
                connect(te, &QPushButton::clicked, this, [this, ts] { ObjectWindow::openType(this, ts); });
                br->addWidget(te);
            }
            br->addStretch(1);
            v->addLayout(br);
        }
        auto* br = new QHBoxLayout();
        auto* ub = new QPushButton(tr("Use as brush"));
        connect(ub, &QPushButton::clicked, this, [this, sem] { if (!sem.empty()) setBrush(sem); });
        auto* cc = new QPushButton(tr("Clear cell"));
        connect(cc, &QPushButton::clicked, this, [this, c] {
            QTimer::singleShot(0, this, [this, c] {
                edit(tr("Erase %1 cell(s)").arg(1), [&](stage::StageDoc& dd) { dd.grid[(size_t)c.y][(size_t)c.x] = '.'; stage::Project::dropObj(dd, c.x, c.y); });
            });
        });
        br->addWidget(ub);
        br->addWidget(cc);
        br->addStretch(1);
        v->addLayout(br);
        auto* line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QStringLiteral("color:#3a3f47"));
        v->addWidget(line);
    }
    v->addWidget(floorPanel(*d));
    v->addStretch(1);
    m_inspArea->setWidget(w);
}

void StageEditor::renderProblems() {
    stage::StageDoc* d = curDoc();
    if (!d) return;
    const std::vector<stage::StoryRow>& rows = stage::storyCheck(m_p);
    std::vector<stage::Issue> list = stage::problems(m_p, *d);
    for (const stage::StoryRow& r : rows) if (r.fid == m_cur) list.insert(list.end(), r.issues.begin(), r.issues.end());
    int nErr = 0, nWarn = 0;
    for (const stage::StoryRow& r : rows)
        for (const stage::Issue& i : r.issues) {
            if (i.sev == stage::Sev::Error) nErr++;
            else if (i.sev == stage::Sev::Warn) nWarn++;
        }
    int shown = 0;
    m_checks->clear();
    for (const stage::Issue& i : list) {
        if (i.sev != stage::Sev::Info) shown++;
        auto* it = new QTreeWidgetItem(m_checks, {sevIcon(i.sev) + QStringLiteral("  ") + StageText::issue(i)});
        it->setForeground(0, i.sev == stage::Sev::Info ? QColor("#8b929c") : QColor("#d7dbe0"));
        it->setToolTip(0, StageText::issue(i));
        const std::vector<Cell> cells = i.cells;
        const std::string brush = i.brush;
        it->setData(0, Qt::UserRole, QVariant::fromValue((qulonglong)(m_checks->topLevelItemCount() - 1)));
        if (!i.fixList.empty()) {
            auto* fix = new QPushButton(tr("Add to list"));
            const std::string id = i.fixList;
            connect(fix, &QPushButton::clicked, this, [this, id] {
                QTimer::singleShot(0, this, [this, id] {
                    edit(tr("Add %1 to the floor's events list").arg(q(id)), [&](stage::StageDoc& dd) {
                        if (!dd.spec.contains("events") || !dd.spec["events"].is_array()) dd.spec["events"] = Json::array();
                        dd.spec["events"].push_back(id);
                    });
                });
            });
            m_checks->setItemWidget(it, 1, fix);
        }
    }
    if (list.empty()) new QTreeWidgetItem(m_checks, {tr("No problems on this floor.")});
    disconnect(m_checks, &QTreeWidget::itemClicked, nullptr, nullptr);
    connect(m_checks, &QTreeWidget::itemClicked, this, [this, list](QTreeWidgetItem* it) {
        const int idx = m_checks->indexOfTopLevelItem(it);
        if (idx < 0 || idx >= (int)list.size()) return;
        const stage::Issue& i = list[(size_t)idx];
        if (!i.brush.empty()) setBrush(i.brush);
        if (!i.cells.empty()) m_v.sel = i.cells[0];
        flashCells(i.cells);
        renderInspector();
    });
    m_bottomTabs->setTabText(0, tr("Checks (%1)").arg(shown));
    m_bottomTabs->setTabText(1, tr("Story · all floors (%1)").arg(nErr + nWarn));
    renderStory();
}

void StageEditor::renderStory() {
    const std::vector<stage::StoryRow>& rows = stage::storyCheck(m_p);
    int nErr = 0, nWarn = 0;
    for (const stage::StoryRow& r : rows)
        for (const stage::Issue& i : r.issues) { if (i.sev == stage::Sev::Error) nErr++; else if (i.sev == stage::Sev::Warn) nWarn++; }
    QStringList changed;
    for (const stage::Floor& f : m_p.floors) if (m_p.isDirty(f.id)) changed << q(f.id);
    // the table (a row of rich labels per floor) is costly to build: only while its tab shows, and a floor switch only
    // moves the highlight
    if (m_bottomTabs->currentIndex() != 1) return;
    const QString key = QStringLiteral("%1/%2/").arg(m_p.storyVer).arg(m_p.dataVer) + changed.join(QLatin1Char(','));
    if (key == m_storyKey) {
        for (int i = 0; i < (int)rows.size(); i++)
            if (rows[(size_t)i].fid == m_cur) { m_story->selectRow(i); m_story->scrollToItem(m_story->item(i, 0)); }
        return;
    }
    m_storyKey = key;
    m_storySum->setText(tr("Story check of all %1 floors.").arg(rows.size()) + QStringLiteral(" ")
                        + (changed.isEmpty() ? QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("No unsaved changes."))
                                             : QStringLiteral("<b style='color:#e0b44c'>%1</b> %2").arg(tr("● %1 floor(s) changed, not saved yet:").arg(changed.size()), changed.join(QStringLiteral(" "))))
                        + QStringLiteral(" &nbsp; <b>%1</b>").arg(tr("%1 blocking · %2 warnings").arg(nErr).arg(nWarn)));
    m_story->setUpdatesEnabled(false);
    m_story->clearContents();
    m_story->setRowCount((int)rows.size());
    m_story->setHorizontalHeaderLabels({tr("Floor"), tr("Keys in"), tr("Found"), tr("Doors on the way up"), tr("Keys out"), tr("Events"), tr("Issues")});
    int curRow = -1;
    for (int i = 0; i < (int)rows.size(); i++) {
        const stage::StoryRow& r = rows[(size_t)i];
        const stage::StageDoc& d = m_p.doc(r.fid);
        auto* it = new QTableWidgetItem((m_p.isDirty(r.fid) ? QStringLiteral("● ") : QString()) + q(r.fid) + QStringLiteral(" ") + q(d.spec.value("act", std::string())));
        it->setData(Qt::UserRole, q(r.fid));
        m_story->setItem(i, 0, it);
        stage::Keys found;
        for (int k : r.ex.F.keyAt) if (k >= 0) found.n[k]++;
        const QString up = r.top ? QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("top floor"))
                         : r.ex.route >= 0 ? keyChips(r.ex.all[(size_t)r.ex.route].opened) : QStringLiteral("⛔");
        int placed = 0;
        for (const auto& [id, n] : m_p.placedEvents(d)) { (void)id; placed += n; }
        const size_t listed = d.spec.contains("events") && d.spec["events"].is_array() ? d.spec["events"].size() : 0;
        QStringList iss;
        for (const stage::Issue& is : r.issues)
            iss << QStringLiteral("<span style='color:%1'>%2</span> %3").arg(sevColor(is.sev).name(), sevIcon(is.sev), esc(StageText::issue(is)));
        const QString cells[6] = {keyChips(r.keysIn), keyChips(found), up, r.ex.carry >= 0 ? keyChips(r.ex.all[(size_t)r.ex.carry].left) : QStringLiteral("—"),
                                  QStringLiteral("%1/%2").arg(placed).arg(listed), iss.join(QStringLiteral("<br>"))};
        for (int c = 0; c < 6; c++) {
            auto* l = new QLabel(cells[c]);
            l->setTextFormat(Qt::RichText);
            l->setContentsMargins(4, 0, 4, 0);
            l->setAttribute(Qt::WA_TransparentForMouseEvents);
            if (c == 5) l->setWordWrap(true);
            m_story->setCellWidget(i, c + 1, l);
        }
        if (r.fid == m_cur) curRow = i;
    }
    m_story->resizeRowsToContents();
    m_story->resizeColumnsToContents();
    if (curRow >= 0) { m_story->selectRow(curRow); m_story->scrollToItem(m_story->item(curRow, 0)); }
    m_story->setUpdatesEnabled(true);
}

void StageEditor::renderPlaceBar() {
    if (!m_v.placeMode || m_cur.empty()) { m_placeBar->hide(); return; }
    m_placeBar->show();
    const std::string id = m_v.placeMode->id;
    const auto at = m_p.find(*curDoc(), "event:" + id);
    m_pmText->setText(at ? tr("✓ %1 is at %2, %3. Move it with Select, or place it on the next floor.").arg(q(id)).arg(at->x).arg(at->y)
                         : tr("📍 Place %1 on %2: click a cell (green = safe), or let the editor pick.").arg(q(id), q(m_cur)));
    m_pmSuggest->setVisible(!at);
    m_pmAuto->setVisible(!at);
    const auto& fl = m_v.placeMode->floors;
    auto it = std::find(fl.begin(), fl.end(), m_cur);
    const bool hasNext = it != fl.end() && it + 1 != fl.end();
    m_pmNext->setText(hasNext ? tr("Next: %1 ▶").arg(q(*(it + 1))) : tr("Done ✓"));
    m_pmNext->setVisible(at || hasNext);
}

// ---- auto-place, the windows, save ------------------------------------------------------------------------------------

void StageEditor::openAutoPlace() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Place events automatically"));
    auto* v = new QVBoxLayout(&dlg);
    auto* scopeThis = new QRadioButton(tr("This floor"));
    auto* scopeAll = new QRadioButton(tr("All floors (one click)"));
    scopeThis->setChecked(true);
    auto* g1 = new QButtonGroup(&dlg);
    g1->addButton(scopeThis);
    g1->addButton(scopeAll);
    auto* modeMissing = new QRadioButton(tr("Only events in the list that are not placed yet"));
    auto* modeAll = new QRadioButton(tr("Move every event to a new random cell"));
    modeMissing->setChecked(true);
    auto* g2 = new QButtonGroup(&dlg);
    g2->addButton(modeMissing);
    g2->addButton(modeAll);
    for (QWidget* w : {static_cast<QWidget*>(scopeThis), static_cast<QWidget*>(scopeAll)}) v->addWidget(w);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    v->addWidget(line);
    v->addWidget(modeMissing);
    v->addWidget(modeAll);
    auto* seedRow = new QHBoxLayout();
    seedRow->addWidget(new QLabel(tr("Seed")));
    auto* seed = new QSpinBox;
    seed->setRange(0, 99999);
    seed->setValue(1);
    auto* dice = new QToolButton;
    dice->setText(QStringLiteral("🎲"));
    connect(dice, &QToolButton::clicked, seed, [seed] { seed->setValue((int)QRandomGenerator::global()->bounded(99999)); });
    seedRow->addWidget(seed);
    seedRow->addWidget(dice);
    seedRow->addStretch(1);
    v->addLayout(seedRow);
    auto* note = new QLabel(tr("Events go only on cells the player reaches on the way up without opening optional doors, so a key can never be \"spent\" on an event. They are spread out, never next to stairs, doors or the start, and only events that fire on a tile are placed. The same seed gives the same result; everything stays editable and undoable."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8b929c"));
    v->addWidget(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    bb->addButton(tr("Place"), QDialogButtonBox::AcceptRole);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    dlg.resize(460, dlg.sizeHint().height());
    if (dlg.exec() != QDialog::Accepted) return;
    const std::vector<std::string> fids = scopeAll->isChecked() ? stage::chainOrder(m_p) : std::vector<std::string>{m_cur};
    const stage::AutoPlaceResult res = stage::autoPlace(m_p, fids, modeAll->isChecked() ? "all" : "missing", std::to_string(seed->value()),
                                                        [](int n) { return tr("Auto-place %1 event(s)").arg(n).toStdString(); });
    afterEdit();
    m_bottomTabs->setCurrentIndex(1);
    QStringList msg{tr("Placed %1 event(s) on %2 floor(s).").arg(res.placed).arg(res.floors)};
    if (!res.skipped.empty()) msg << tr("Skipped %1: see Story.").arg(res.skipped.size());
    for (size_t i = 0; i < res.skipped.size() && i < 3; i++) msg << StageText::issue(res.skipped[i]);
    toast(msg.join(QStringLiteral(" · ")), !res.skipped.empty());
}

bool StageEditor::save() {
    const std::vector<stage::ChangedFile> files = stage::changedFiles(m_p);
    if (files.empty()) { toast(tr("Nothing to save.")); return true; }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save changes"));
    dlg.resize(900, 620);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(rich(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>· %2</span>").arg(tr("Save changes"), tr("%1 file(s) change").arg(files.size()))));
    auto* view = new QTextBrowser;
    QString html;
    if (files.size() > 3) {   // more than three: only which files
        html = QStringLiteral("<p>%1</p><ul style='font-family:Consolas'>").arg(tr("%1 files changed (too many to show each change):").arg(files.size()));
        for (const stage::ChangedFile& f : files) html += QStringLiteral("<li>%1</li>").arg(esc(f.path));
        html += QStringLiteral("</ul>");
    } else {
        for (const stage::ChangedFile& f : files) {
            html += QStringLiteral("<p><b>%1</b>%2</p><pre style='font-family:Consolas; font-size:12px'>").arg(esc(f.path),
                        f.kind == stage::ChangedFile::Delete ? QStringLiteral(" — ") + tr("deleted") : QString());
            for (const auto& [c, line] : stage::lineDiff(f.before, f.text)) {
                const QString l = esc(line);
                if (c == '+') html += QStringLiteral("<span style='color:#7ee2a8; background:#173323'>+ %1</span>\n").arg(l);
                else if (c == '-') html += QStringLiteral("<span style='color:#ff9b9b; background:#3a1b1b'>- %1</span>\n").arg(l);
                else html += QStringLiteral("<span style='color:#8b929c'>  %1</span>\n").arg(l);
            }
            html += QStringLiteral("</pre>");
        }
    }
    view->setHtml(html);
    v->addWidget(view, 1);
    auto* note = new QLabel(tr("Writes into the project folder. Each file is backed up to Build/editor_backups first."));
    note->setStyleSheet(QStringLiteral("color:#8b929c"));
    v->addWidget(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* write = bb->addButton(tr("Write to project"), QDialogButtonBox::AcceptRole);
    write->setDefault(true);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return false;
    const std::string stamp = stage::timeStamp();
    std::vector<std::string> written;
    std::string err;
    m_ignoreWatch = true;   // this editor's own writes are not a reason to reload
    const bool ok = stage::writeFiles(m_p, stamp, written, err);
    QStringList paths;
    for (const std::string& p : written) paths << q(p);
    if (!ok) { toast(q(err), true); renderAll(); return false; }
    std::string ierr;
    const bool indexed = stage::syncFloorIndex(m_p.root, &ierr);
    m_p.storyVer++;
    renderAll();
    updateTitle();
    toast(tr("Saved %1 file(s) to the project folder. Backups: Build/editor_backups/%2/").arg(written.size()).arg(q(stamp))
          + (indexed ? QStringLiteral("  ") + tr("Event index updated (assets/data/events/floor_index.json).") : QString()));
    Q_EMIT filesWritten(paths);
    Q_EMIT dirtyChanged();
    return true;
}

void StageEditor::openObjectWindow(const Cell& c) { ObjectWindow::openObject(this, c.x, c.y); }
void StageEditor::openTypeWindow(const std::string& sem) { ObjectWindow::openType(this, sem); }
void StageEditor::openNewType(const std::string& cat) { ObjectWindow::openNew(this, cat); }

// ---- the palette's own categories ---------------------------------------------------------------------------------

bool StageEditor::addCategory(const std::string& name) {
    std::vector<std::string> g = m_p.paletteGroups();
    if (name.empty() || std::find(g.begin(), g.end(), name) != g.end()) return false;
    g.push_back(name);
    m_p.setPaletteGroups(g);
    typesChanged();
    return true;
}

bool StageEditor::renameCategory(const std::string& from, const std::string& to) {
    const std::vector<std::string> g = m_p.paletteGroups();
    if (to.empty() || from == to || std::find(g.begin(), g.end(), to) != g.end()) return false;
    m_collapsed.erase("custom:" + from);
    m_p.renameGroup(from, to);
    typesChanged();
    return true;
}

void StageEditor::deleteCategory(const std::string& name) {
    m_p.clearGroup(name);
    std::vector<std::string> g = m_p.paletteGroups();
    g.erase(std::remove(g.begin(), g.end(), name), g.end());
    m_p.setPaletteGroups(g);
    m_collapsed.erase("custom:" + name);
    typesChanged();
}

bool StageEditor::moveToCategory(const std::string& sem, const std::string& group) {
    if (!m_p.setGroup(sem, group)) return false;
    if (!group.empty()) {   // a name not in the list yet (a record's own) joins it
        std::vector<std::string> g = m_p.paletteGroups();
        if (std::find(g.begin(), g.end(), group) == g.end()) { g.push_back(group); m_p.setPaletteGroups(g); }
    }
    typesChanged();
    return true;
}

void StageEditor::newCategoryDialog() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New category"), tr("Name"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (!addCategory(name.toStdString())) toast(tr("%1 already exists.").arg(name), true);
    else toast(tr("Category %1 added: its + makes types for it, or right-click a type to move it there.").arg(name));
}

void StageEditor::renameCategoryDialog(const std::string& name) {
    bool ok = false;
    const QString to = QInputDialog::getText(this, tr("Rename category"), tr("Name"), QLineEdit::Normal, q(name), &ok).trimmed();
    if (!ok || to.isEmpty() || to == q(name)) return;
    if (!renameCategory(name, to.toStdString())) toast(tr("%1 already exists.").arg(to), true);
}

void StageEditor::deleteCategoryDialog(const std::string& name) {
    int n = 0;
    for (const std::string& id : m_p.enemyIds()) n += m_p.groupOf("monster:" + id) == name;
    for (const std::string& id : m_p.itemIds()) n += m_p.groupOf("item:" + id) == name;
    for (const std::string& id : m_p.npcIds()) n += m_p.groupOf("npc:" + id) == name;
    if (n > 0 && QMessageBox::question(this, tr("Delete category"),
                                       tr("Delete the category %1? Its %2 type(s) go back to their usual groups (nothing is deleted).").arg(q(name)).arg(n))
                     != QMessageBox::Yes)
        return;
    deleteCategory(name);
}

void StageEditor::revealBrush() {
    m_revealBrush = true;
    renderPalette();
}

// ---- --selftest ----------------------------------------------------------------------------------------------------

QStringList StageEditor::selfTest() {
    QStringList fail;
    auto check = [&](bool ok, const QString& what) { if (!ok) fail << what; };
    check(m_p.floors.size() == 70, QStringLiteral("70 floors loaded (%1)").arg(m_p.floors.size()));
    check(!hasUnsaved(), QStringLiteral("nothing to save after loading"));
    openFloor("F02");
    check(m_cur == "F02", QStringLiteral("F02 opened"));
    stage::StageDoc& d = *curDoc();
    // paint a wall on a free cell, then undo
    std::optional<Cell> free;
    for (int y = 1; y < d.height() - 1 && !free; y++)
        for (int x = 1; x < d.width() - 1; x++) if (d.at(x, y) == '.') { free = Cell{x, y}; break; }
    check(free.has_value(), QStringLiteral("F02 has a free cell"));
    if (free) {
        setTool(Paint);
        setBrush("wall");
        canvasPress(*free);
        canvasRelease();
        check(d.at(free->x, free->y) == '#', QStringLiteral("painting puts a wall"));
        check(m_p.isDirty("F02") && hasUnsaved(), QStringLiteral("F02 changed after painting"));
        undo();
        check(!m_p.isDirty("F02"), QStringLiteral("undo restores F02"));
        // an object cannot go on a wall
        setBrush("monster:slime");
        canvasPress(Cell{0, 0});
        canvasRelease();
        check(d.at(0, 0) == '#', QStringLiteral("a monster is not painted over a wall"));
    }
    // ✎ Edit on the villager: own sprite and a linked event
    if (const auto v = m_p.find(d, "npc:villager")) {
        setTool(Edit);
        canvasPress(*v);
        ObjectWindow* w = ObjectWindow::current();
        check(w != nullptr, QStringLiteral("✎ Edit opens the object window"));
        if (w) {
            w->setLookSprite("npc_king");
            w->linkEvent("ev_village_well");
            w->apply();
        }
        const Json* o = stage::Project::objOf(d, v->x, v->y);
        check(o && o->contains("look") && o->contains("events"), QStringLiteral("the object's own settings are set"));
        bool stageListed = false;
        for (const stage::ChangedFile& f : stage::changedFiles(m_p)) stageListed |= f.path == d.f->stagePath;
        check(stageListed, QStringLiteral("Save would write the stage file"));
        undo();
        check(!m_p.isDirty("F02"), QStringLiteral("undo removes the object settings"));
    } else {
        fail << QStringLiteral("F02 has no villager");
    }
    // a type's details
    ObjectWindow::openType(this, "monster:slime");
    check(ObjectWindow::current() != nullptr, QStringLiteral("the slime's details open"));
    if (ObjectWindow::current()) ObjectWindow::current()->close();
    // Reset stage layout brings back a closed panel and a floating one, docked
    if (panels().size() == 4) {
        panels()[0]->hide();
        panels()[1]->setFloating(true);
        resetLayout();
        check(!panels()[0]->isHidden() && !panels()[1]->isFloating() && dockWidgetArea(panels()[0]) == Qt::LeftDockWidgetArea,
              QStringLiteral("reset layout docks every panel back"));
    } else {
        fail << QStringLiteral("the stage editor has %1 panels, not 4").arg(panels().size());
    }
    // every palette group has a + (the game's own terrain, stairs and start: shown, disabled)
    auto paletteButtons = [this] { return m_paletteArea->widget()->findChildren<QToolButton*>(); };
    {
        int plus = 0, enabled = 0;
        for (QToolButton* b : paletteButtons())
            if (b->text() == QStringLiteral("+")) { plus++; enabled += b->isEnabled() ? 1 : 0; }
        check(plus == 8 && enabled == 5, QStringLiteral("every palette group has a + (%1, %2 enabled)").arg(plus).arg(enabled));
    }
    // a click on the palette paints with it and shows its details; the window follows the palette and does not block
    auto clickPalette = [&](const std::string& sem) {
        for (QToolButton* b : paletteButtons())
            if (b->toolTip().contains(QStringLiteral(" · ") + q(sem))) { b->click(); return true; }
        return false;
    };
    check(clickPalette("monster:slime") && m_v.brush == "monster:slime", QStringLiteral("a palette click picks the brush"));
    ObjectWindow* details = ObjectWindow::current();
    check(details && details->kind() == ObjectWindow::Type && details->sem() == "monster:slime" && !details->isModal(),
          QStringLiteral("and opens its details, without blocking the editor"));
    check(clickPalette("key:yellow") && ObjectWindow::current() == details && details && details->sem() == "item:key_yellow",
          QStringLiteral("the same window follows the next click (a key: its key item)"));
    check(clickPalette("wall") && ObjectWindow::current() == details && details && details->sem() == "item:key_yellow",
          QStringLiteral("a built-in (wall) leaves the window as it is"));
    // a key's tint (its item record key_blue) is what the palette and the map draw "key:blue" with
    if (Json* kb = m_p.typeRec("item:key_blue")) {
        const Json before = *kb;
        (*kb)["look"]["tint"] = Json::array({0.0, 0.0, 0.5, 1.0});
        check(m_p.typeRec("key:blue") == kb && m_p.typeLook("key:blue").contains("tint"), QStringLiteral("a map key uses its item's look"));
        check(!m_p.typeUses("item:key_blue").empty(), QStringLiteral("key:blue cells count as uses of the item key_blue"));
        *kb = before;
    }
    if (ObjectWindow::current()) ObjectWindow::current()->close();
    // the palette's own categories: add one, a type moved in shows there (not in its usual group), rename, delete
    {
        const std::string cat = "測試分類";
        auto groupHeads = [&] {
            QStringList out;
            for (QToolButton* b : paletteButtons()) if (b->text().startsWith(QStringLiteral("▾ ")) || b->text().startsWith(QStringLiteral("▸ "))) out << b->text().mid(2);
            return out;
        };
        check(addCategory(cat) && !addCategory(cat) && groupHeads().contains(q(cat)), QStringLiteral("a new category shows in the palette (once)"));
        check(moveToCategory("monster:slime", cat) && m_p.groupOf("monster:slime") == cat, QStringLiteral("a type moves into it"));
        int slimeButtons = 0;
        for (QToolButton* b : paletteButtons()) slimeButtons += b->toolTip().contains(QStringLiteral(" · monster:slime")) ? 1 : 0;
        check(slimeButtons == 1, QStringLiteral("it shows in the category only, not twice (%1)").arg(slimeButtons));
        check(renameCategory(cat, "Bosses") && m_p.groupOf("monster:slime") == "Bosses" && groupHeads().contains(QStringLiteral("Bosses")),
              QStringLiteral("rename carries its types"));
        deleteCategory("Bosses");
        check(m_p.groupOf("monster:slime").empty() && !groupHeads().contains(QStringLiteral("Bosses")), QStringLiteral("delete sends them back"));
        check(!hasUnsaved(), QStringLiteral("and nothing is left to save"));
    }
    // a new type's id may be Chinese (lowercase letters, CJK, digits, _); spaces and capitals are refused
    ObjectWindow::openNew(this, "monster");
    if (ObjectWindow* w = ObjectWindow::current()) { w->setNewId("Slime red", std::string()); w->apply(); }
    check(!m_p.typeRec("monster:Slime red"), QStringLiteral("an id with a space or a capital is refused"));
    if (ObjectWindow* w = ObjectWindow::current(); w && w->kind() == ObjectWindow::New) { w->setNewId("史萊姆_紅", std::string()); w->apply(); }
    const std::string zhId = "史萊姆_紅";
    check(m_p.typeRec("monster:" + zhId) != nullptr, QStringLiteral("a Traditional Chinese id is accepted"));
    if (ObjectWindow::current()) ObjectWindow::current()->close();
    m_p.dataFiles[stage::Project::enemiesPath()].obj.erase(zhId);   // and gone again (nothing to save)
    typesChanged();
    // a new door and its key: an item of type "story", whose gate shows in the palette
    ObjectWindow::openNew(this, "story");
    if (ObjectWindow* w = ObjectWindow::current()) {
        w->setNewId("selftest_key", std::string());
        w->apply();
    }
    const Json* keyRec = m_p.typeRec("item:selftest_key");
    check(keyRec && keyRec->value("type", std::string()) == "story", QStringLiteral("the new door's key is a story item"));
    check(m_v.brush == "gate:selftest_key", QStringLiteral("the new door is the brush"));
    check(ObjectWindow::current() && ObjectWindow::current()->sem() == "item:selftest_key", QStringLiteral("its details open"));
    bool gateShown = false;
    for (QToolButton* b : paletteButtons()) gateShown |= b->toolTip().contains(QStringLiteral("gate:selftest_key"));
    check(gateShown, QStringLiteral("its gate is in the palette"));
    if (ObjectWindow::current()) ObjectWindow::current()->close();
    m_p.dataFiles[stage::Project::itemsPath()].obj.erase("selftest_key");   // and gone again (nothing to save)
    m_p.textEdits.erase("item.selftest_key.name");
    m_p.textEdits.erase("item.selftest_key.desc");
    typesChanged();
    // auto-place, undone in one step
    const stage::AutoPlaceResult r = stage::autoPlace(m_p, {"F02"}, "all", "7", [](int n) { return std::to_string(n); });
    afterEdit();
    check(r.placed > 0, QStringLiteral("auto-place places F02's events"));
    undo();
    check(!m_p.isDirty("F02"), QStringLiteral("auto-place undoes in one step"));
    m_bottomTabs->setCurrentIndex(1);   // the story table is built when its tab shows
    check(m_story->rowCount() == 70, QStringLiteral("the story table has every floor (%1)").arg(m_story->rowCount()));
    {   // the placed-event count (one legend look-up per char) matches a cell-by-cell count
        const stage::StageDoc& d2 = m_p.doc("F02");
        std::map<std::string, int> slow;
        for (int y = 0; y < d2.height(); y++)
            for (int x = 0; x < d2.width(); x++)
                if (const std::string s2 = m_p.semAt(d2, x, y); startsWith(s2, "event:")) slow[s2.substr(6)]++;
        check(m_p.placedEvents(d2) == slow, QStringLiteral("placed events counted the fast way match"));
    }
    check(!hasUnsaved(), QStringLiteral("nothing unsaved after the undos"));
    check(m_checks->topLevelItemCount() > 0, QStringLiteral("the checks list is filled"));
    setTool(Paint);
    setBrush("wall");
    return fail;
}
