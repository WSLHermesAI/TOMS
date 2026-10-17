// EventEditor.cpp -- see EventEditor.h. The window, the events tree, History, the Problems tab and Save; the inspector
// is EventForms.cpp, the other tabs EventTabs.cpp and EventNpcs.cpp. Behaviour as tools/web_editors/event_editor.html.
#include "EventEditor.h"

#include "EventWidgets.h"
#include "FlowLayout.h"
#include "Language.h"
#include "stage_save.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using stage::Json;
using namespace EvUi;

namespace {
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
const char* kMissing = "__missing";

QIcon dotIcon(const QColor& c) {
    QPixmap px(10, 10);
    px.fill(Qt::transparent);
    QPainter g(&px);
    g.setRenderHint(QPainter::Antialiasing);
    g.setBrush(c);
    g.setPen(Qt::NoPen);
    g.drawEllipse(1, 1, 8, 8);
    return QIcon(px);
}

QString sevIcon(int sev) { return sev == 0 ? QStringLiteral("⛔") : sev == 1 ? QStringLiteral("⚠") : QStringLiteral("ⓘ"); }

// a line diff of two whole files: the common start and end are cut first, so a large file (text.json) stays cheap
std::vector<std::pair<char, std::string>> fileDiff(const std::string& a, const std::string& b) {
    auto split = [](const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) { if (c == '\r') continue; if (c == '\n') { out.push_back(cur); cur.clear(); } else cur += c; }
        out.push_back(cur);
        return out;
    };
    const std::vector<std::string> A = split(a), B = split(b);
    size_t pre = 0;
    while (pre < A.size() && pre < B.size() && A[pre] == B[pre]) pre++;
    size_t suf = 0;
    while (suf < A.size() - pre && suf < B.size() - pre && A[A.size() - 1 - suf] == B[B.size() - 1 - suf]) suf++;
    std::string ma, mb;
    for (size_t i = pre; i < A.size() - suf; i++) ma += A[i] + "\n";
    for (size_t i = pre; i < B.size() - suf; i++) mb += B[i] + "\n";
    std::vector<std::pair<char, std::string>> out;
    for (size_t i = pre >= 2 ? pre - 2 : 0; i < pre; i++) out.push_back({' ', A[i]});
    if (pre > 2) out.insert(out.begin(), {' ', "…"});
    if (ma.size() + mb.size() < 400000) {
        for (auto& x : stage::lineDiff(ma, mb)) if (!(x.first == ' ' && x.second.empty())) out.push_back(x);
    } else {
        out.push_back({' ', "(" + std::to_string(A.size() - suf - pre) + " → " + std::to_string(B.size() - suf - pre) + " lines)"});
    }
    for (size_t i = A.size() - suf; i < A.size() && i < A.size() - suf + 2; i++) out.push_back({' ', A[i]});
    if (suf > 2) out.push_back({' ', "…"});
    return out;
}
}  // namespace

EventEditor::EventEditor(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("EventEditor"));
    setWindowFlags(Qt::Widget);
    auto* tb = addToolBar(tr("Events"));
    tb->setObjectName(QStringLiteral("EventBar"));
    tb->setMovable(false);
    QAction* toStage = tb->addAction(tr("⇄ Stage editor"), this, [this] {
        const Json* e = m_p.ev(m_p.selected);
        std::string floor;
        if (e) { auto fl = m_p.floorsOf(m_p.selected); if (!fl.empty()) floor = (*fl[0])["id"].get<std::string>(); }
        else if (m_p.flo(m_p.selected)) floor = m_p.selected;
        Q_EMIT openStage(q(floor), e ? q(m_p.selected) : QString());
    });
    toStage->setToolTip(tr("Show the stage editor (on the selected event's first floor)"));
    tb->addSeparator();
    m_undoA = tb->addAction(tr("Undo"), this, &EventEditor::undo);
    m_undoA->setShortcut(QKeySequence::Undo);
    m_undoA->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_saveA = tb->addAction(tr("Save"), this, [this] { save(); });
    m_saveA->setShortcut(QKeySequence::Save);
    m_saveA->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_dirty = new QLabel(this);
    m_dirty->setStyleSheet(QStringLiteral("color:#ff9f43; font-weight:bold"));
    tb->addWidget(m_dirty);
    auto* sp = new QWidget(this);
    sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(sp);
    m_src = new QLabel(this);
    m_src->setStyleSheet(QStringLiteral("color:#4cd18a"));
    tb->addWidget(m_src);

    // left: the events tree
    auto* dock = new QDockWidget(tr("Events"), this);
    dock->setObjectName(QStringLiteral("EventsTreeDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* lw = new QWidget(dock);
    auto* lv = new QVBoxLayout(lw);
    lv->setContentsMargins(4, 4, 4, 4);
    auto* newBtn = new QPushButton(tr("+ New event"));
    connect(newBtn, &QPushButton::clicked, this, [this] { createEvent(std::string()); });
    lv->addWidget(newBtn);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Find: id, text, kind or floor (Ctrl+F)"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& s) { m_q = s.trimmed(); renderTree(); });
    auto* find = new QAction(this);
    find->setShortcut(QKeySequence::Find);
    find->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QAction::triggered, this, [this] { m_search->setFocus(); m_search->selectAll(); });
    addAction(find);
    lv->addWidget(m_search);
    auto* fr = new QHBoxLayout;
    m_kindF = new QComboBox;
    connect(m_kindF, &QComboBox::activated, this, [this](int i) { m_kind = m_kindF->itemData(i).toString(); renderTree(); });
    m_probF = new QCheckBox(tr("only problems"));
    connect(m_probF, &QCheckBox::toggled, this, [this](bool on) { m_probOnly = on; renderTree(); });
    fr->addWidget(m_kindF, 1);
    fr->addWidget(m_probF);
    lv->addLayout(fr);
    m_count = new QLabel;
    m_count->setTextFormat(Qt::RichText);
    connect(m_count, &QLabel::linkActivated, this, [this] { m_search->clear(); m_kind.clear(); m_probF->setChecked(false); renderTree(); });
    lv->addWidget(m_count);
    auto* tr2 = new QHBoxLayout;
    auto* ex = new QToolButton;
    ex->setText(tr("▾ Expand all"));
    connect(ex, &QToolButton::clicked, this, [this] { m_collapsed.clear(); renderTree(); });
    auto* co = new QToolButton;
    co->setText(tr("▸ Collapse all"));
    connect(co, &QToolButton::clicked, this, [this] { for (const std::string& p : m_p.pools()) m_collapsed.insert(p); m_collapsed.insert(kMissing); renderTree(); });
    tr2->addWidget(ex);
    tr2->addWidget(co);
    tr2->addStretch(1);
    lv->addLayout(tr2);
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setIndentation(12);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        const QString id = it->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) select(id.toStdString());
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) { m_collapsed.erase(it->data(0, Qt::UserRole + 1).toString().toStdString()); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) { m_collapsed.insert(it->data(0, Qt::UserRole + 1).toString().toStdString()); });
    lv->addWidget(m_tree, 1);
    dock->setWidget(lw);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // right: history
    auto* hd = new QDockWidget(tr("History"), this);
    hd->setObjectName(QStringLiteral("HistoryDock"));
    hd->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_hist = new QListWidget(hd);
    hd->setWidget(m_hist);
    addDockWidget(Qt::RightDockWidgetArea, hd);

    // centre: the inspector above the tabs
    m_split = new QSplitter(Qt::Vertical, this);
    m_form = new QScrollArea;
    m_form->setWidgetResizable(true);
    m_form->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_form->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto* bottom = new QWidget;
    auto* bv = new QVBoxLayout(bottom);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(0);
    auto* tabRow = new QHBoxLayout;
    m_tabs = new QTabBar;
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->setTabsClosable(true);
    connect(m_tabs, &QTabBar::tabBarClicked, this, [this](int i) { if (i >= 0) QTimer::singleShot(0, this, [this, i] { setTab(m_tabs->tabData(i).toString()); }); });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int i) {
        const QString t = m_tabs->tabData(i).toString();
        if (t.startsWith(QStringLiteral("val:"))) QTimer::singleShot(0, this, [this, t] { closeValue(t.mid(4).toStdString()); });
    });
    tabRow->addWidget(m_tabs, 1);
    m_info = new QLabel;
    m_info->setStyleSheet(QStringLiteral("color:#8b929c; padding:0 8px"));
    tabRow->addWidget(m_info);
    bv->addLayout(tabRow);
    m_pane = new QScrollArea;
    m_pane->setWidgetResizable(true);
    bv->addWidget(m_pane, 1);
    m_split->addWidget(m_form);
    m_split->addWidget(bottom);
    m_split->setStretchFactor(0, 3);
    m_split->setStretchFactor(1, 2);
    setCentralWidget(m_split);
    resizeDocks({dock, hd}, {290, 210}, Qt::Horizontal);

    m_status = new QLabel(this);
    m_probs = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_probs);
    connect(Language::Notifier::instance(), &Language::Notifier::changed, this, &EventEditor::scheduleRender);
}

bool EventEditor::openProject(const QString& root, QString* error) {
    m_root = root;
    std::string err;
    if (!m_p.load(root.toStdString(), err)) { if (error) *error = q(err); return false; }
    m_src->setText(tr("project folder (live): %1").arg(QDir::toNativeSeparators(root)));
    m_hist->clear();
    m_hist->addItem(tr("Loaded assets/data"));
    m_collapsed = {kMissing};
    renderAll();
    toast(tr("Loaded %1 events and %2 floors from the project folder.").arg(m_p.S["events"].size()).arg(m_p.S["floors"].size()));
    return true;
}

void EventEditor::reloadFromDisk() {
    if (m_p.dirty()) { toast(tr("The stage editor saved; this editor has unsaved changes, so it keeps them (its Save merges onto the files)."));  return; }
    const std::string sel = m_p.selected;
    std::string err;
    if (!m_p.load(m_root.toStdString(), err)) { toast(q(err), true); return; }
    m_p.selected = sel;
    scheduleRender();
}

// ---- edits ---------------------------------------------------------------------------------------------------------

void EventEditor::edit(const QString& label, const std::function<void()>& fn) {
    m_p.edit(label.toStdString(), fn);
    m_hist->addItem(label);
    m_hist->scrollToBottom();
    scheduleRender();
    Q_EMIT dirtyChanged();
}

void EventEditor::undo() {
    std::string label;
    if (!m_p.undo(&label)) return;
    if (m_hist->count() > 1) delete m_hist->takeItem(m_hist->count() - 1);
    toast(QStringLiteral("↶ ") + q(label));
    scheduleRender();
    Q_EMIT dirtyChanged();
}

void EventEditor::scheduleRender() {
    if (m_renderQueued) return;
    m_renderQueued = true;
    QTimer::singleShot(0, this, [this] { m_renderQueued = false; renderAll(); });
}

void EventEditor::toast(const QString& msg, bool bad) {
    m_status->setText(bad ? QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(esc(msg)) : esc(msg));
}

QString EventEditor::lang() const { return Language::current() == Language::Lang::Chinese ? QStringLiteral("zh_TW") : QStringLiteral("en"); }

QString EventEditor::L(const Json& o) const {
    if (o.is_string()) return q(o.get<std::string>());
    if (!o.is_object()) return QString();
    for (const std::string l : {lang().toStdString(), std::string("en"), std::string("zh_TW")})
        if (o.contains(l) && o[l].is_string() && !o[l].get<std::string>().empty()) return q(o[l].get<std::string>());
    return QString();
}

void EventEditor::select(const std::string& id, bool toTab) {
    m_p.selected = id;
    const Json* e = m_p.ev(id);
    if (e) m_collapsed.erase((*e)["pool"].get<std::string>());
    else if (m_p.isMissing(id)) m_collapsed.erase(kMissing);
    if (toTab && m_p.flo(id)) m_tab = QStringLiteral("floors");
    scheduleRender();
}

void EventEditor::selectEvent(const QString& id) {
    if (m_p.ev(id.toStdString())) select(id.toStdString());
}

void EventEditor::showNpc(const QString& id) {
    m_npcSel = id.toStdString();
    setTab(QStringLiteral("npcs"));
}

void EventEditor::setTab(const QString& tab) {
    m_tab = tab;
    m_paneScroll = 0;
    scheduleRender();
}

void EventEditor::openValue(const std::string& ref) {
    if (!m_openVals.contains(q(ref))) m_openVals << q(ref);
    setTab(QStringLiteral("val:") + q(ref));
}

void EventEditor::closeValue(const std::string& ref) {
    m_openVals.removeAll(q(ref));
    if (m_tab == QStringLiteral("val:") + q(ref)) m_tab = QStringLiteral("vars");
    scheduleRender();
}

// ---- rendering -----------------------------------------------------------------------------------------------------

void EventEditor::renderAll() {
    if (m_p.S.is_null()) return;
    // the kind filter's list
    {
        m_kindF->blockSignals(true);
        m_kindF->clear();
        m_kindF->addItem(tr("(all kinds)"), QString());
        std::vector<std::string> ks = m_p.kindsList();
        for (const Json& e : m_p.S["events"]) {
            const std::string k = e["kind"].get<std::string>();
            if (!k.empty() && std::find(ks.begin(), ks.end(), k) == ks.end()) ks.push_back(k);
        }
        for (const std::string& k : ks) {
            const QString n = q(m_p.kindName(k, lang().toStdString()));
            m_kindF->addItem(dotIcon(QColor(q(m_p.kindColor(k)))), q(k) + (n.isEmpty() ? QString() : QStringLiteral(" — ") + n), q(k));
        }
        m_kindF->setCurrentIndex(std::max(0, m_kindF->findData(m_kind)));
        m_kindF->blockSignals(false);
    }
    renderTree();
    renderForm();
    renderTabs();
    renderPane();
    const auto all = m_p.problems();
    int errs = 0, warns = 0;
    for (const auto& p : all) { const int s = stage::ruleSev(p.rule); if (s == 0) errs++; else if (s == 1) warns++; }
    m_probs->setText(tr("⛔ %1 errors · ⚠ %2 warnings").arg(errs).arg(warns) + (m_p.dirty() ? QStringLiteral(" · ") + tr("unsaved changes") : QString()));
    m_dirty->setText(m_p.dirty() ? QStringLiteral(" •") : QString());
    m_undoA->setEnabled(!m_p.undoStack.empty());
}

void EventEditor::renderTree() {
    const auto all = m_p.problems();
    std::set<std::string> probIds;
    for (const auto& p : all) if (stage::ruleSev(p.rule) != 2) probIds.insert(p.id);
    const QString ql = m_q.toLower();
    const bool filtering = !m_q.isEmpty() || !m_kind.isEmpty() || m_probOnly;
    // hits: id -> "" (by id / kind / pool / floor) or the matching text
    std::map<std::string, QString> hits;
    for (const Json& e : m_p.S["events"]) {
        const std::string id = e["id"].get<std::string>();
        if (!m_kind.isEmpty() && q(e["kind"].get<std::string>()) != m_kind) continue;
        if (m_probOnly && !probIds.count(id)) continue;
        if (ql.isEmpty()) { hits[id] = QString(); continue; }
        QStringList hay{q(id), q(e["kind"].get<std::string>()), q(e["pool"].get<std::string>()), q(e["text"].get<std::string>())};
        for (const Json* f : m_p.floorsOf(id)) hay << q((*f)["id"].get<std::string>());
        bool hit = false;
        for (const QString& h : hay) if (h.toLower().contains(ql)) { hit = true; break; }
        if (hit) { hits[id] = QString(); continue; }
        const Json t = m_p.text(e["text"].get<std::string>());
        for (auto& [k, v] : t.items())
            if (!startsWith(k, "_") && v.is_string() && q(v.get<std::string>()).toLower().contains(ql)) { hits[id] = q(v.get<std::string>()).simplified(); break; }
    }
    std::vector<stage::EvProblem> missing;
    for (const auto& p : all)
        if (p.rule == "missing_event" && m_kind.isEmpty() && (ql.isEmpty() || q(p.id).toLower().contains(ql) || (!p.whereArgs.empty() && q(p.whereArgs[0]).toLower().contains(ql))))
            missing.push_back(p);
    const QString fk = m_q + QLatin1Char('|') + m_kind + QLatin1Char('|') + (m_probOnly ? QStringLiteral("1") : QString());
    if (fk != m_lastFilt) {   // a new filter opens the groups that have matches; after that, folding is the user's
        m_lastFilt = fk;
        if (filtering) {
            for (const Json& e : m_p.S["events"]) if (hits.count(e["id"].get<std::string>())) m_collapsed.erase(e["pool"].get<std::string>());
            if (!missing.empty() && missing.size() <= 40) m_collapsed.erase(kMissing);
        }
    }
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    QTreeWidgetItem* selItem = nullptr;
    int shown = 0;
    for (const std::string& pool : m_p.pools()) {
        std::vector<const Json*> evs;
        for (const Json& e : m_p.S["events"]) if (e["pool"] == pool && hits.count(e["id"].get<std::string>())) evs.push_back(&e);
        if (filtering && evs.empty()) continue;
        auto* g = new QTreeWidgetItem(m_tree);
        g->setText(0, q(pool) + QStringLiteral("  (%1)").arg(evs.size()) + (m_p.isNewPool(pool) ? QStringLiteral("  ") + tr("NEW") : QString()));
        g->setData(0, Qt::UserRole + 1, q(pool));
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        auto* plus = new QToolButton;
        plus->setText(QStringLiteral("+"));
        plus->setAutoRaise(true);
        plus->setToolTip(tr("New event in %1").arg(q(pool)));
        connect(plus, &QToolButton::clicked, this, [this, pool] { createEvent(pool); });
        for (const Json* e : evs) {
            const std::string id = (*e)["id"].get<std::string>();
            auto* it = new QTreeWidgetItem(g);
            const bool orphan = m_p.floorsOf(id).empty() && (*e)["trigger"].value("on", std::string("step")) == "step";
            QString label = q(id);
            if (m_p.isNewEvent(id)) label += QStringLiteral("  ") + tr("NEW");
            if (orphan) label += QStringLiteral("  ⚠");
            it->setText(0, label);
            it->setIcon(0, dotIcon(QColor(q(m_p.kindColor((*e)["kind"].get<std::string>())))));
            it->setData(0, Qt::UserRole, q(id));
            it->setToolTip(0, q(id));
            const QString hit = hits[id];
            if (!hit.isEmpty()) { it->setText(1, tr("text") + QStringLiteral(": ") + hit.left(40)); it->setForeground(1, QColor("#8b929c")); }
            if (id == m_p.selected) selItem = it;
            shown++;
        }
        m_tree->setItemWidget(g, 1, plus);
        g->setExpanded(!m_collapsed.count(pool));
    }
    if (!missing.empty()) {
        auto* g = new QTreeWidgetItem(m_tree);
        g->setText(0, QStringLiteral("⛔ ") + tr("No pool (%1)").arg(missing.size()));
        g->setForeground(0, QColor("#ff9b9b"));
        g->setData(0, Qt::UserRole + 1, QString::fromLatin1(kMissing));
        for (const auto& p : missing) {
            auto* it = new QTreeWidgetItem(g);
            it->setText(0, q(p.id));
            it->setForeground(0, QColor("#ff9b9b"));
            it->setData(0, Qt::UserRole, q(p.id));
            if (p.id == m_p.selected) selItem = it;
        }
        g->setExpanded(!m_collapsed.count(kMissing));
    }
    if (!m_tree->topLevelItemCount()) new QTreeWidgetItem(m_tree, {tr("No event matches.")});
    if (selItem) m_tree->setCurrentItem(selItem);
    m_tree->setUpdatesEnabled(true);
    m_tree->verticalScrollBar()->setValue(scroll);
    int total = (int)m_p.S["events"].size();
    for (const auto& p : all) if (p.rule == "missing_event") total++;
    m_count->setText(filtering ? tr("%1 of %2 events match").arg(shown + (int)missing.size()).arg(total) + QStringLiteral(" <a href='clear'>%1</a>").arg(tr("clear")) : QString());
}

void EventEditor::renderForm() {
    const int scroll = m_form->verticalScrollBar()->value();
    QWidget* w = nullptr;
    if (Json* f = m_p.flo(m_p.selected)) w = floorForm(*f);
    else if (m_p.isMissing(m_p.selected)) w = missingForm(m_p.selected);
    else if (Json* e = m_p.ev(m_p.selected)) w = eventForm(*e);
    else {
        w = new QWidget;
        auto* v = new QVBoxLayout(w);
        v->addWidget(rich(tr("Select an event in the tree or a row in Problems, or click <b>+ New event</b>.")));
        v->addStretch(1);
    }
    m_form->setWidget(w);
    QTimer::singleShot(0, this, [this, scroll] { m_form->verticalScrollBar()->setValue(scroll); });
}

void EventEditor::renderTabs() {
    const auto all = m_p.problems();
    int n = 0;
    for (const auto& p : all) if (stage::ruleSev(p.rule) != 2) n++;
    struct T { QString key, label; };
    std::vector<T> tabs = {{QStringLiteral("problems"), tr("Problems (%1)").arg(n)}, {QStringLiteral("floors"), tr("Floor map")},
                           {QStringLiteral("vars"), tr("Variables")}, {QStringLiteral("kinds"), tr("Kinds")},
                           {QStringLiteral("npcs"), tr("NPCs")}, {QStringLiteral("dlg"), tr("Dialogues")}};
    for (const QString& r : m_openVals) {
        const QString k = r.section(QLatin1Char(':'), 0, 0), name = r.section(QLatin1Char(':'), 1);
        tabs.push_back({QStringLiteral("val:") + r, (k == QStringLiteral("var") ? QStringLiteral("◆ ") : k == QStringLiteral("counter") ? QStringLiteral("▲ ") : QStringLiteral("⚑ ")) + name});
    }
    if (m_tab.startsWith(QStringLiteral("val:")) && !m_openVals.contains(m_tab.mid(4))) m_tab = QStringLiteral("vars");
    m_tabs->blockSignals(true);
    while (m_tabs->count()) m_tabs->removeTab(0);
    for (const T& t : tabs) {
        const int i = m_tabs->addTab(t.label);
        m_tabs->setTabData(i, t.key);
        if (!t.key.startsWith(QStringLiteral("val:"))) {   // only value tabs close
            m_tabs->setTabButton(i, QTabBar::RightSide, nullptr);
            m_tabs->setTabButton(i, QTabBar::LeftSide, nullptr);
        }
        if (t.key == m_tab) m_tabs->setCurrentIndex(i);
    }
    m_tabs->blockSignals(false);
}

void EventEditor::renderPane() {
    const int scroll = m_pane->verticalScrollBar()->value();
    m_info->clear();
    QWidget* w = nullptr;
    if (m_tab == QStringLiteral("floors")) w = floorMapPane();
    else if (m_tab == QStringLiteral("vars")) w = varsPane();
    else if (m_tab == QStringLiteral("kinds")) w = kindsPane();
    else if (m_tab == QStringLiteral("npcs")) w = npcsPane();
    else if (m_tab == QStringLiteral("dlg")) w = dialoguesPane();
    else if (m_tab.startsWith(QStringLiteral("val:"))) w = valuePane(m_tab.mid(4).toStdString());
    else w = problemsPane();
    m_pane->setWidget(w);
    QTimer::singleShot(0, this, [this, scroll] { m_pane->verticalScrollBar()->setValue(scroll); });
}

// ---- problems ------------------------------------------------------------------------------------------------------

QString EventEditor::ruleName(const std::string& r) const {
    static const std::map<std::string, const char*> names = {
        {"missing_event", QT_TR_NOOP("Missing event id")}, {"missing_text", QT_TR_NOOP("Missing text key")}, {"orphan", QT_TR_NOOP("Orphan event")},
        {"no_kind", QT_TR_NOOP("No kind")}, {"placeholder", QT_TR_NOOP("Placeholder text")}, {"untranslated", QT_TR_NOOP("Untranslated")},
        {"missing_title", QT_TR_NOOP("No title / description")}, {"unknown_kind", QT_TR_NOOP("Kind not in the list")},
        {"missing_dialogue", QT_TR_NOOP("Missing dialogue file")}, {"bad_dialogue", QT_TR_NOOP("Dialogue: missing node")},
        {"unknown_ref", QT_TR_NOOP("Unknown reference")}, {"bad_next", QT_TR_NOOP("Link to missing event")}, {"never_fires", QT_TR_NOOP("Never fires")},
        {"never_active", QT_TR_NOOP("Never active")}, {"logic_cycle", QT_TR_NOOP("Event loop")}, {"unused_var", QT_TR_NOOP("Unused variable")},
        {"missing_floor", QT_TR_NOOP("Missing floor")}, {"bad_unlock", QT_TR_NOOP("Unknown unlock event")}, {"unreachable", QT_TR_NOOP("Unreachable floor")}};
    auto it = names.find(r);
    return it == names.end() ? q(r) : tr(it->second);
}

QString EventEditor::problemText(const stage::EvProblem& p) const {
    const auto a = [&](size_t i) { return i < p.whereArgs.size() ? q(p.whereArgs[i]) : QString(); };
    const std::string& c = p.whereCode;
    if (c.empty()) return a(0);
    if (c == "wNoTitle") return tr("no %1").arg(a(0));
    if (c == "wUnknownKind") return tr("kind %1 is not in kinds.json").arg(a(0));
    if (c == "wUnknown") {
        const QString k = a(0) == QStringLiteral("var") ? tr("Event variables") : a(0) == QStringLiteral("counter") ? tr("Counters")
                        : a(0) == QStringLiteral("flag") ? tr("Flags") : a(0) == QStringLiteral("event") ? tr("Events done") : a(0);
        return tr("%1 %2 does not exist").arg(k, a(1));
    }
    if (c == "wBadNext") return tr("next → %1").arg(a(0));
    if (c == "wNoIncoming") return tr("fires only from a link, but no event links to it");
    if (c == "wNpcNowhere") return tr("talks to %1, who is not placed on any floor").arg(a(0));
    if (c == "wNeverActive") return tr("needs %1 ≥ %2, but no event changes %1").arg(a(0), a(1));
    if (c == "wUnusedVar") return tr("no event reads or writes it");
    if (c == "stairWhere") return QStringLiteral("%1 %2 %3").arg(a(0), a(1) == QStringLiteral("down") ? QStringLiteral("↓") : QStringLiteral("↑"), a(2));
    if (c == "badUnlockWhere") return tr("%1 ↑ %2 waits for %3").arg(a(0), a(1), a(2));
    if (c == "unreachWhere") return tr("no stair path from F01");
    if (c == "wDlgStart") return tr("start node %1 does not exist").arg(a(0));
    if (c == "wDlgNext") return tr("%1: next %2 does not exist").arg(a(0), a(1));
    return a(0);
}

QWidget* EventEditor::problemsPane() {
    const auto all = m_p.problems();
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    // rule chips: tick to show
    auto* chips = new QWidget;
    auto* flow = new FlowLayout(chips, 4);
    for (const auto& [rule, sev] : stage::evRules()) {
        int n = 0;
        for (const auto& p : all) if (p.rule == rule) n++;
        auto* c = chip(sevIcon(sev) + QStringLiteral(" ") + ruleName(rule) + QStringLiteral("  %1").arg(n), m_ruleOff.count(rule) ? QColor() : QColor("#4c9eff"));
        c->setCheckable(true);
        c->setChecked(!m_ruleOff.count(rule));
        const std::string r = rule;
        connect(c, &QToolButton::clicked, this, [this, r] { if (m_ruleOff.count(r)) m_ruleOff.erase(r); else m_ruleOff.insert(r); scheduleRender(); });
        flow->addWidget(c);
    }
    v->addWidget(chips);
    std::map<std::string, int> byAct;
    for (const auto& p : all) if (p.rule == "missing_event") byAct[p.act]++;
    if (!byAct.empty()) {
        auto* row = new QWidget;
        auto* rf = new FlowLayout(row, 4);
        rf->addWidget(muted(tr("Missing ids by chapter:")));
        for (const auto& [act, n] : byAct) {
            auto* b = new QPushButton(tr("%1: create %2").arg(q(act)).arg(n));
            const std::string a = act;
            connect(b, &QPushButton::clicked, this, [this, a] { createChapter(a); });
            rf->addWidget(b);
        }
        v->addWidget(row);
    }
    std::vector<stage::EvProblem> list;
    for (const auto& p : all) if (!m_ruleOff.count(p.rule)) list.push_back(p);
    auto ruleIdx = [](const std::string& r) { int i = 0; for (const auto& [k, s] : stage::evRules()) { (void)s; if (k == r) return i; i++; } return i; };
    std::stable_sort(list.begin(), list.end(), [&](const auto& a, const auto& b) { return ruleIdx(a.rule) != ruleIdx(b.rule) ? ruleIdx(a.rule) < ruleIdx(b.rule) : a.id < b.id; });
    const size_t shown = std::min<size_t>(list.size(), 150);
    auto* table = new QTableWidget((int)shown + (list.size() > shown ? 1 : 0), 5);
    table->setHorizontalHeaderLabels({QString(), tr("Id"), tr("Problem"), tr("Where"), QString()});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    for (size_t i = 0; i < shown; i++) {
        const auto& p = list[i];
        const int sev = stage::ruleSev(p.rule);
        auto* s = new QTableWidgetItem(sevIcon(sev));
        s->setForeground(sev == 0 ? QColor("#ff5d5d") : sev == 1 ? QColor("#ff9f43") : QColor("#8b929c"));
        table->setItem((int)i, 0, s);
        auto* idItem = new QTableWidgetItem(q(p.id));
        QFont bf = idItem->font();
        bf.setBold(true);
        idItem->setFont(bf);
        table->setItem((int)i, 1, idItem);
        table->setItem((int)i, 2, new QTableWidgetItem(ruleName(p.rule)));
        table->setItem((int)i, 3, new QTableWidgetItem(problemText(p)));
        // fix buttons
        std::vector<std::pair<QString, QString>> fixes;
        const std::string& r = p.rule;
        if (r == "missing_event") fixes = {{"create", tr("Create event")}, {"remove", tr("Remove from floors")}};
        else if (r == "orphan") fixes = {{"connect", tr("Add floor…")}, {"delete", tr("Delete")}};
        else if (r == "no_kind") fixes = {{"open", tr("Choose kind")}};
        else if (r == "placeholder") fixes = {{"open", tr("Write text")}};
        else if (r == "missing_text") fixes = {{"mktext", tr("Create text")}};
        else if (r == "missing_floor") fixes = {{"mkfloor", tr("Create floor")}, {"floor", tr("Open floor")}};
        else if (r == "bad_unlock" || r == "unreachable") fixes = {{"floor", tr("Open floor")}};
        else if (r == "unused_var") fixes = {{"vars", tr("Open variables")}};
        else if (r == "unknown_kind") fixes = {{"open", tr("Choose kind")}, {"kinds", tr("Open Kinds")}};
        else if (r == "bad_dialogue") fixes = {{"dlg", tr("Open")}};
        else fixes = {{"open", tr("Open")}};
        auto* cell = new QWidget;
        auto* ch = new QHBoxLayout(cell);
        ch->setContentsMargins(2, 0, 2, 0);
        for (const auto& [k, label] : fixes) {
            auto* b = new QToolButton;
            b->setText(label);
            const stage::EvProblem pp = p;
            const QString kk = k;
            connect(b, &QToolButton::clicked, this, [this, pp, kk] { runFix(pp, kk); });
            ch->addWidget(b);
        }
        table->setCellWidget((int)i, 4, cell);
    }
    if (list.size() > shown) table->setItem((int)shown, 1, new QTableWidgetItem(tr("… and %1 more").arg(list.size() - shown)));
    if (list.empty()) { table->setRowCount(1); table->setItem(0, 1, new QTableWidgetItem(tr("No problems. 🎉"))); }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(table, &QTableWidget::cellClicked, this, [this, list, shown](int row, int) {
        if (row < 0 || (size_t)row >= shown) return;
        const auto& p = list[(size_t)row];
        if (p.rule == "unused_var") setTab(QStringLiteral("vars"));
        else if (p.rule == "bad_dialogue") { m_dlgSel = p.id; setTab(QStringLiteral("dlg")); }
        else select(p.id, true);
    });
    table->setMinimumHeight(260);
    v->addWidget(table, 1);
    int errs = 0, warns = 0;
    for (const auto& p : all) { const int s = stage::ruleSev(p.rule); if (s == 0) errs++; else if (s == 1) warns++; }
    m_info->setText(tr("%1 errors · %2 warnings").arg(errs).arg(warns));
    return w;
}

void EventEditor::runFix(const stage::EvProblem& p, const QString& k) {
    if (k == QStringLiteral("vars")) { setTab(QStringLiteral("vars")); return; }
    if (k == QStringLiteral("kinds")) { setTab(QStringLiteral("kinds")); return; }
    if (k == QStringLiteral("dlg")) { m_dlgSel = p.id; setTab(QStringLiteral("dlg")); return; }
    if (k == QStringLiteral("mkfloor")) { createFloor(p.to, p.id); return; }
    if (k == QStringLiteral("floor")) { m_tab = QStringLiteral("floors"); select(p.id); return; }
    if (k == QStringLiteral("create")) {
        const auto fl = m_p.floorsOf(p.id);
        if (fl.empty()) return;
        const std::string id = p.id, act = (*fl[0])["act"].get<std::string>();
        edit(tr("Create %1").arg(q(id)), [this, id, act] { m_p.makeRecord(id, act); m_p.selected = id; });
        return;
    }
    if (k == QStringLiteral("remove")) {
        const std::string id = p.id;
        edit(tr("Remove %1 from floors").arg(q(id)), [this, id] {
            for (Json& f : m_p.S["floors"]) {
                Json& evs = f["events"];
                evs.erase(std::remove(evs.begin(), evs.end(), Json(id)), evs.end());
            }
            m_p.selected.clear();
        });
        return;
    }
    if (k == QStringLiteral("delete")) {
        const std::string id = p.id;
        edit(tr("Delete %1").arg(q(id)), [this, id] {
            Json& evs = m_p.S["events"];
            for (size_t i = 0; i < evs.size(); i++) if (evs[i]["id"] == id) { evs.erase(i); break; }
            m_p.selected.clear();
        });
        return;
    }
    if (k == QStringLiteral("mktext")) {
        const Json* e = m_p.ev(p.id);
        if (!e) return;
        const std::string key = (*e)["text"].get<std::string>();
        edit(tr("Create text %1").arg(q(key)), [this, key] { Json t = Json::object(); for (const char* l : stage::kLangs) t[l] = ""; m_p.S["texts"][key] = t; });
        return;
    }
    select(p.id);
    if (k == QStringLiteral("connect")) QTimer::singleShot(0, this, [this, id = p.id] { addFloorsDialog(id); });
}

// ---- save ----------------------------------------------------------------------------------------------------------

bool EventEditor::save() {
    const std::map<std::string, std::string> comments = {
        {"vars", "Event variables: read and written by event logic (docs/event_editor/06_EVENT_LOGIC.md)."},
        {"kinds", "Event kinds: names and editor colours (edited in the event editor's Kinds tab)."}};
    const auto files = m_p.liveWrites(comments);
    if (files.empty()) { toast(tr("Nothing to save")); if (m_p.dirty()) { m_p.base = m_p.S; scheduleRender(); } return true; }
    int warn = 0;
    for (const auto& p : m_p.problems()) if (stage::ruleSev(p.rule) != 2) warn++;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save changes"));
    dlg.resize(940, 680);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(rich(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2</span><br>%3").arg(tr("Save changes"),
        tr("· a backup of each file is copied first, then these files are written"),
        tr("%1 file(s) change.").arg(files.size()) + (warn ? QStringLiteral(" ") + tr("%1 problem(s) remain; saving is still allowed.").arg(warn) : QString()))));
    auto* view = new QTextBrowser;
    QString html;
    for (const auto& f : files) {
        html += QStringLiteral("<p><b>%1</b>%2</p><pre style='font-family:Consolas; font-size:12px'>").arg(esc(q(f.path)), f.existed ? QString() : QStringLiteral("  ") + tr("(new file)"));
        for (const auto& [c, line] : fileDiff(f.before, f.text)) {
            const QString l = esc(q(line));
            if (c == '+') html += QStringLiteral("<span style='color:#7ee2a8; background:#173323'>+ %1</span>\n").arg(l);
            else if (c == '-') html += QStringLiteral("<span style='color:#ff9b9b; background:#3a1b1b'>- %1</span>\n").arg(l);
            else html += QStringLiteral("<span style='color:#8b929c'>  %1</span>\n").arg(l);
        }
        html += QStringLiteral("</pre>");
    }
    view->setHtml(html);
    v->addWidget(view, 1);
    v->addWidget(muted(tr("Writes into assets/data. Each file is backed up to Build/editor_backups first.")));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* write = bb->addButton(tr("Write to project (%1 file(s))").arg(files.size()), QDialogButtonBox::AcceptRole);
    write->setDefault(true);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return false;
    const std::string stamp = stage::timeStamp();
    std::string err;
    if (!m_p.writeAll(files, stamp, err)) { toast(tr("Could not write: %1").arg(q(err)), true); return false; }
    QStringList paths;
    for (const auto& f : files) paths << q(f.path);
    std::string ierr;
    const bool indexed = stage::syncFloorIndex(m_p.root, &ierr);
    m_hist->addItem(tr("Wrote %1 file(s) to assets/data").arg(files.size()));
    m_hist->item(m_hist->count() - 1)->setForeground(QColor("#4cd18a"));
    toast(tr("Wrote %1 file(s) to assets/data. Backups: Build/editor_backups/%2/").arg(files.size()).arg(q(stamp))
          + (indexed ? QStringLiteral("  ") + tr("Event index updated (assets/data/events/floor_index.json).") : QString()));
    scheduleRender();
    Q_EMIT filesWritten(paths);
    Q_EMIT dirtyChanged();
    return true;
}

// ---- --selftest ----------------------------------------------------------------------------------------------------

QStringList EventEditor::selfTest() {
    QStringList fail;
    auto check = [&](bool ok, const QString& what) { if (!ok) fail << QStringLiteral("events: ") + what; };
    check(!m_p.S["events"].empty(), QStringLiteral("events loaded"));
    check(!m_p.dirty(), QStringLiteral("nothing unsaved after loading"));
    check(m_p.liveWrites({}).empty(), QStringLiteral("nothing to write after loading"));
    // an event's text, then undo
    Json* e = m_p.ev("ev_village_well");
    check(e != nullptr, QStringLiteral("ev_village_well exists"));
    if (e) {
        select("ev_village_well");
        renderAll();
        const std::string key = (*e)["text"].get<std::string>();
        edit(QStringLiteral("selftest text"), [this, key] { m_p.S["texts"][key]["en"] = "selftest"; });
        renderAll();
        const auto files = m_p.liveWrites({});
        check(files.size() == 1 && files[0].path == "assets/data/text.json", QStringLiteral("a text edit writes only text.json (%1 files)").arg(files.size()));
        undo();
        check(!m_p.dirty(), QStringLiteral("undo restores the text"));
        // a condition row and an action
        edit(QStringLiteral("selftest logic"), [this] {
            Json& ev = *m_p.ev("ev_village_well");
            ev["req"].push_back(stage::rowToLeaf("event:ev_village_ledger", "set", Json()));
            ev["actions"].push_back(stage::makeAction("addCounter", "insight", 1));
        });
        renderAll();
        bool poolOnly = true;
        for (const auto& f : m_p.liveWrites({})) poolOnly &= startsWith(f.path, "assets/data/events/pool_");
        check(poolOnly, QStringLiteral("a logic edit writes only its pool"));
        undo();
    }
    // each tab renders
    for (const QString& t : {QStringLiteral("problems"), QStringLiteral("floors"), QStringLiteral("vars"), QStringLiteral("kinds"), QStringLiteral("npcs"), QStringLiteral("dlg")}) {
        m_tab = t;
        renderAll();
        check(m_pane->widget() != nullptr, QStringLiteral("tab %1 renders").arg(t));
    }
    // the talk preview on the villager
    showNpc(QStringLiteral("villager"));
    renderAll();
    m_pv.reset(m_p, "villager");
    m_pv.talk(m_p, "villager", "en");
    check(!m_pv.log.empty(), QStringLiteral("the talk preview talks to the villager"));
    // a value tab
    if (!m_p.valueRefs().empty()) { openValue(m_p.valueRefs()[0]); renderAll(); check(m_pane->widget() != nullptr, QStringLiteral("a value tab renders")); }
    // the floor inspector and a missing id
    select("F02");
    renderAll();
    check(m_form->widget() != nullptr, QStringLiteral("the floor inspector renders"));
    check(!m_p.dirty(), QStringLiteral("nothing unsaved at the end"));
    m_tab = QStringLiteral("problems");
    m_openVals.clear();
    renderAll();
    return fail;
}

void EventEditor::resetLayout() {
    const std::pair<const char*, Qt::DockWidgetArea> home[] = {{"EventsTreeDock", Qt::LeftDockWidgetArea}, {"HistoryDock", Qt::RightDockWidgetArea}};
    QList<QDockWidget*> docks;
    for (const auto& [name, area] : home) {
        auto* d = findChild<QDockWidget*>(QLatin1String(name));
        if (!d) continue;
        d->setFloating(false);
        removeDockWidget(d);
        addDockWidget(area, d);
        d->show();
        docks << d;
    }
    if (docks.size() == 2) resizeDocks(docks, {300, 280}, Qt::Horizontal);
    if (m_split) {   // the form over the lists, 3 : 2
        const int h = std::max(500, m_split->height());
        m_split->setSizes({h * 3 / 5, h * 2 / 5});
    }
}
