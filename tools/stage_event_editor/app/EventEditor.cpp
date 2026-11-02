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
bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
const char* g_pszMissing = "__missing";

QIcon f_DotIcon(const QColor& l_oC) {
    QPixmap l_oPx(10, 10);
    l_oPx.fill(Qt::transparent);
    QPainter l_oG(&l_oPx);
    l_oG.setRenderHint(QPainter::Antialiasing);
    l_oG.setBrush(l_oC);
    l_oG.setPen(Qt::NoPen);
    l_oG.drawEllipse(1, 1, 8, 8);
    return QIcon(l_oPx);
}

QString f_SevIcon(int l_iSev) { return l_iSev == 0 ? QStringLiteral("⛔") : l_iSev == 1 ? QStringLiteral("⚠") : QStringLiteral("ⓘ"); }

// a line diff of two whole files: the common start and end are cut first, so a large file (text.json) stays cheap
std::vector<std::pair<char, std::string>> f_FileDiff(const std::string& l_strA, const std::string& l_strB) {
    auto l_fnSplit = [](const std::string& l_strS) {
        std::vector<std::string> l_vOut;
        std::string l_strCur;
        for (char l_cC : l_strS) { if (l_cC == '\r') continue; if (l_cC == '\n') { l_vOut.push_back(l_strCur); l_strCur.clear(); } else l_strCur += l_cC; }
        l_vOut.push_back(l_strCur);
        return l_vOut;
    };
    const std::vector<std::string> l_vA = l_fnSplit(l_strA), l_vB = l_fnSplit(l_strB);
    size_t l_nPre = 0;
    while (l_nPre < l_vA.size() && l_nPre < l_vB.size() && l_vA[l_nPre] == l_vB[l_nPre]) l_nPre++;
    size_t l_nSuf = 0;
    while (l_nSuf < l_vA.size() - l_nPre && l_nSuf < l_vB.size() - l_nPre && l_vA[l_vA.size() - 1 - l_nSuf] == l_vB[l_vB.size() - 1 - l_nSuf]) l_nSuf++;
    std::string l_strMa, l_strMb;
    for (size_t l_nI = l_nPre; l_nI < l_vA.size() - l_nSuf; l_nI++) l_strMa += l_vA[l_nI] + "\n";
    for (size_t l_nI = l_nPre; l_nI < l_vB.size() - l_nSuf; l_nI++) l_strMb += l_vB[l_nI] + "\n";
    std::vector<std::pair<char, std::string>> l_vOut;
    for (size_t l_nI = l_nPre >= 2 ? l_nPre - 2 : 0; l_nI < l_nPre; l_nI++) l_vOut.push_back({' ', l_vA[l_nI]});
    if (l_nPre > 2) l_vOut.insert(l_vOut.begin(), {' ', "…"});
    if (l_strMa.size() + l_strMb.size() < 400000) {
        for (auto& l_pairX : stage::f_LineDiff(l_strMa, l_strMb)) if (!(l_pairX.first == ' ' && l_pairX.second.empty())) l_vOut.push_back(l_pairX);
    } else {
        l_vOut.push_back({' ', "(" + std::to_string(l_vA.size() - l_nSuf - l_nPre) + " → " + std::to_string(l_vB.size() - l_nSuf - l_nPre) + " lines)"});
    }
    for (size_t l_nI = l_vA.size() - l_nSuf; l_nI < l_vA.size() && l_nI < l_vA.size() - l_nSuf + 2; l_nI++) l_vOut.push_back({' ', l_vA[l_nI]});
    if (l_nSuf > 2) l_vOut.push_back({' ', "…"});
    return l_vOut;
}
}  // namespace

EventEditor::EventEditor(QWidget* l_pParent) : QMainWindow(l_pParent) {
    setObjectName(QStringLiteral("EventEditor"));
    setWindowFlags(Qt::Widget);
    auto* l_pTb = addToolBar(tr("Events"));
    l_pTb->setObjectName(QStringLiteral("EventBar"));
    l_pTb->setMovable(false);
    QAction* l_pToStage = l_pTb->addAction(tr("⇄ Stage editor"), this, [this] {
        const Json* l_pE = m_oP.f_Ev(m_oP.m_strSelected);
        std::string l_strFloor;
        if (l_pE) { auto l_vFl = m_oP.f_FloorsOf(m_oP.m_strSelected); if (!l_vFl.empty()) l_strFloor = (*l_vFl[0])["id"].get<std::string>(); }
        else if (m_oP.f_Flo(m_oP.m_strSelected)) l_strFloor = m_oP.m_strSelected;
        Q_EMIT f_OpenStage(f_Q(l_strFloor), l_pE ? f_Q(m_oP.m_strSelected) : QString());
    });
    l_pToStage->setToolTip(tr("Show the stage editor (on the selected event's first floor)"));
    l_pTb->addSeparator();
    m_pUndoA = l_pTb->addAction(tr("Undo"), this, &EventEditor::f_Undo);
    m_pUndoA->setShortcut(QKeySequence::Undo);
    m_pUndoA->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_pSaveA = l_pTb->addAction(tr("Save"), this, [this] { f_Save(); });
    m_pSaveA->setShortcut(QKeySequence::Save);
    m_pSaveA->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_pDirty = new QLabel(this);
    m_pDirty->setStyleSheet(QStringLiteral("color:#ff9f43; font-weight:bold"));
    l_pTb->addWidget(m_pDirty);
    auto* l_pSp = new QWidget(this);
    l_pSp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    l_pTb->addWidget(l_pSp);
    m_pSrc = new QLabel(this);
    m_pSrc->setStyleSheet(QStringLiteral("color:#4cd18a"));
    l_pTb->addWidget(m_pSrc);

    // left: the events tree
    auto* l_pDock = new QDockWidget(tr("Events"), this);
    l_pDock->setObjectName(QStringLiteral("EventsTreeDock"));
    l_pDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* l_pLw = new QWidget(l_pDock);
    auto* l_pLv = new QVBoxLayout(l_pLw);
    l_pLv->setContentsMargins(4, 4, 4, 4);
    auto* l_pNewBtn = new QPushButton(tr("+ New event"));
    connect(l_pNewBtn, &QPushButton::clicked, this, [this] { f_CreateEvent(std::string()); });
    l_pLv->addWidget(l_pNewBtn);
    m_pSearch = new QLineEdit;
    m_pSearch->setPlaceholderText(tr("Find: id, text, kind or floor (Ctrl+F)"));
    m_pSearch->setClearButtonEnabled(true);
    connect(m_pSearch, &QLineEdit::textChanged, this, [this](const QString& l_oS) { m_oQ = l_oS.trimmed(); f_RenderTree(); });
    auto* l_pFind = new QAction(this);
    l_pFind->setShortcut(QKeySequence::Find);
    l_pFind->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(l_pFind, &QAction::triggered, this, [this] { m_pSearch->setFocus(); m_pSearch->selectAll(); });
    addAction(l_pFind);
    l_pLv->addWidget(m_pSearch);
    auto* l_pFr = new QHBoxLayout;
    m_pKindF = new QComboBox;
    connect(m_pKindF, &QComboBox::activated, this, [this](int l_iI) { m_oKind = m_pKindF->itemData(l_iI).toString(); f_RenderTree(); });
    m_pProbF = new QCheckBox(tr("only problems"));
    connect(m_pProbF, &QCheckBox::toggled, this, [this](bool l_bOn) { m_bProbOnly = l_bOn; f_RenderTree(); });
    l_pFr->addWidget(m_pKindF, 1);
    l_pFr->addWidget(m_pProbF);
    l_pLv->addLayout(l_pFr);
    m_pCount = new QLabel;
    m_pCount->setTextFormat(Qt::RichText);
    connect(m_pCount, &QLabel::linkActivated, this, [this] { m_pSearch->clear(); m_oKind.clear(); m_pProbF->setChecked(false); f_RenderTree(); });
    l_pLv->addWidget(m_pCount);
    auto* l_pTr2 = new QHBoxLayout;
    auto* l_pEx = new QToolButton;
    l_pEx->setText(tr("▾ Expand all"));
    connect(l_pEx, &QToolButton::clicked, this, [this] { m_setCollapsed.clear(); f_RenderTree(); });
    auto* l_pCo = new QToolButton;
    l_pCo->setText(tr("▸ Collapse all"));
    connect(l_pCo, &QToolButton::clicked, this, [this] { for (const std::string& l_strP : m_oP.f_Pools()) m_setCollapsed.insert(l_strP); m_setCollapsed.insert(g_pszMissing); f_RenderTree(); });
    l_pTr2->addWidget(l_pEx);
    l_pTr2->addWidget(l_pCo);
    l_pTr2->addStretch(1);
    l_pLv->addLayout(l_pTr2);
    m_pTree = new QTreeWidget;
    m_pTree->setColumnCount(2);
    m_pTree->setHeaderHidden(true);
    m_pTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pTree->header()->setStretchLastSection(false);
    m_pTree->setIndentation(12);
    connect(m_pTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* l_pIt) {
        const QString l_oId = l_pIt->data(0, Qt::UserRole).toString();
        if (!l_oId.isEmpty()) f_Select(l_oId.toStdString());
    });
    connect(m_pTree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* l_pIt) { m_setCollapsed.erase(l_pIt->data(0, Qt::UserRole + 1).toString().toStdString()); });
    connect(m_pTree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* l_pIt) { m_setCollapsed.insert(l_pIt->data(0, Qt::UserRole + 1).toString().toStdString()); });
    l_pLv->addWidget(m_pTree, 1);
    l_pDock->setWidget(l_pLw);
    addDockWidget(Qt::LeftDockWidgetArea, l_pDock);

    // right: history
    auto* l_pHd = new QDockWidget(tr("History"), this);
    l_pHd->setObjectName(QStringLiteral("HistoryDock"));
    l_pHd->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_pHist = new QListWidget(l_pHd);
    l_pHd->setWidget(m_pHist);
    addDockWidget(Qt::RightDockWidgetArea, l_pHd);

    // centre: the inspector above the tabs
    m_pSplit = new QSplitter(Qt::Vertical, this);
    m_pForm = new QScrollArea;
    m_pForm->setWidgetResizable(true);
    m_pForm->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pForm->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto* l_pBottom = new QWidget;
    auto* l_pBv = new QVBoxLayout(l_pBottom);
    l_pBv->setContentsMargins(0, 0, 0, 0);
    l_pBv->setSpacing(0);
    auto* l_pTabRow = new QHBoxLayout;
    m_pTabs = new QTabBar;
    m_pTabs->setDocumentMode(true);
    m_pTabs->setExpanding(false);
    m_pTabs->setTabsClosable(true);
    connect(m_pTabs, &QTabBar::tabBarClicked, this, [this](int l_iI) { if (l_iI >= 0) QTimer::singleShot(0, this, [this, l_iI] { f_SetTab(m_pTabs->tabData(l_iI).toString()); }); });
    connect(m_pTabs, &QTabBar::tabCloseRequested, this, [this](int l_iI) {
        const QString l_oT = m_pTabs->tabData(l_iI).toString();
        if (l_oT.startsWith(QStringLiteral("val:"))) QTimer::singleShot(0, this, [this, l_oT] { f_CloseValue(l_oT.mid(4).toStdString()); });
    });
    l_pTabRow->addWidget(m_pTabs, 1);
    m_pInfo = new QLabel;
    m_pInfo->setStyleSheet(QStringLiteral("color:#8b929c; padding:0 8px"));
    l_pTabRow->addWidget(m_pInfo);
    l_pBv->addLayout(l_pTabRow);
    m_pPane = new QScrollArea;
    m_pPane->setWidgetResizable(true);
    l_pBv->addWidget(m_pPane, 1);
    m_pSplit->addWidget(m_pForm);
    m_pSplit->addWidget(l_pBottom);
    m_pSplit->setStretchFactor(0, 3);
    m_pSplit->setStretchFactor(1, 2);
    setCentralWidget(m_pSplit);
    resizeDocks({l_pDock, l_pHd}, {290, 210}, Qt::Horizontal);

    m_pStatus = new QLabel(this);
    m_pProbs = new QLabel(this);
    statusBar()->addWidget(m_pStatus, 1);
    statusBar()->addPermanentWidget(m_pProbs);
    connect(Language::Notifier::f_Instance(), &Language::Notifier::f_Changed, this, &EventEditor::f_ScheduleRender);
}

bool EventEditor::f_OpenProject(const QString& l_oRoot, QString* l_pError) {
    m_oRoot = l_oRoot;
    std::string l_strErr;
    if (!m_oP.f_Load(l_oRoot.toStdString(), l_strErr)) { if (l_pError) *l_pError = f_Q(l_strErr); return false; }
    m_pSrc->setText(tr("project folder (live): %1").arg(QDir::toNativeSeparators(l_oRoot)));
    m_pHist->clear();
    m_pHist->addItem(tr("Loaded assets/data"));
    m_setCollapsed = {g_pszMissing};
    f_RenderAll();
    f_Toast(tr("Loaded %1 events and %2 floors from the project folder.").arg(m_oP.m_jS["events"].size()).arg(m_oP.m_jS["floors"].size()));
    return true;
}

void EventEditor::f_ReloadFromDisk() {
    if (m_oP.f_Dirty()) { f_Toast(tr("The stage editor saved; this editor has unsaved changes, so it keeps them (its Save merges onto the files)."));  return; }
    const std::string l_strSel = m_oP.m_strSelected;
    std::string l_strErr;
    if (!m_oP.f_Load(m_oRoot.toStdString(), l_strErr)) { f_Toast(f_Q(l_strErr), true); return; }
    m_oP.m_strSelected = l_strSel;
    f_ScheduleRender();
}

// ---- edits ---------------------------------------------------------------------------------------------------------

void EventEditor::f_Edit(const QString& l_oLabel, const std::function<void()>& l_fnFn) {
    m_oP.f_Edit(l_oLabel.toStdString(), l_fnFn);
    m_pHist->addItem(l_oLabel);
    m_pHist->scrollToBottom();
    f_ScheduleRender();
    Q_EMIT f_DirtyChanged();
}

void EventEditor::f_Undo() {
    std::string l_strLabel;
    if (!m_oP.f_Undo(&l_strLabel)) return;
    if (m_pHist->count() > 1) delete m_pHist->takeItem(m_pHist->count() - 1);
    f_Toast(QStringLiteral("↶ ") + f_Q(l_strLabel));
    f_ScheduleRender();
    Q_EMIT f_DirtyChanged();
}

void EventEditor::f_ScheduleRender() {
    if (m_bRenderQueued) return;
    m_bRenderQueued = true;
    QTimer::singleShot(0, this, [this] { m_bRenderQueued = false; f_RenderAll(); });
}

void EventEditor::f_Toast(const QString& l_oMsg, bool l_bBad) {
    m_pStatus->setText(l_bBad ? QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(f_Esc(l_oMsg)) : f_Esc(l_oMsg));
}

QString EventEditor::f_Lang() const { return Language::f_Current() == Language::Lang::Chinese ? QStringLiteral("zh_TW") : QStringLiteral("en"); }

QString EventEditor::f_L(const Json& l_jO) const {
    if (l_jO.is_string()) return f_Q(l_jO.get<std::string>());
    if (!l_jO.is_object()) return QString();
    for (const std::string l_strL : {f_Lang().toStdString(), std::string("en"), std::string("zh_TW")})
        if (l_jO.contains(l_strL) && l_jO[l_strL].is_string() && !l_jO[l_strL].get<std::string>().empty()) return f_Q(l_jO[l_strL].get<std::string>());
    return QString();
}

void EventEditor::f_Select(const std::string& l_strId, bool l_bToTab) {
    m_oP.m_strSelected = l_strId;
    const Json* l_pE = m_oP.f_Ev(l_strId);
    if (l_pE) m_setCollapsed.erase((*l_pE)["pool"].get<std::string>());
    else if (m_oP.f_IsMissing(l_strId)) m_setCollapsed.erase(g_pszMissing);
    if (l_bToTab && m_oP.f_Flo(l_strId)) m_oTab = QStringLiteral("floors");
    f_ScheduleRender();
}

void EventEditor::f_SelectEvent(const QString& l_oId) {
    if (m_oP.f_Ev(l_oId.toStdString())) f_Select(l_oId.toStdString());
}

void EventEditor::f_ShowNpc(const QString& l_oId) {
    m_strNpcSel = l_oId.toStdString();
    f_SetTab(QStringLiteral("npcs"));
}

void EventEditor::f_SetTab(const QString& l_oTab) {
    m_oTab = l_oTab;
    m_iPaneScroll = 0;
    f_ScheduleRender();
}

// ---- logic graph ---------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_LogicPane() {
    auto* l_pG = new LogicGraph(m_oP, m_oGraph, [this](const QString& l_oLabel, std::function<void()> l_fnFn) { f_Edit(l_oLabel, l_fnFn); },
                             [this](const Json& l_jO) { return f_L(l_jO); });
    l_pG->m_fnOnSelectEvent = [this](const std::string& l_strId) { f_Select(l_strId); };
    l_pG->m_fnOnOpenValue = [this](const std::string& l_strRef) { f_OpenValue(l_strRef); };
    m_pGraphView = l_pG;
    m_pInfo->setText(tr("%1 events · %2 values · white = fires an event · purple = event variable · teal = counter · red = flag · "
                       "gold = event done").arg(l_pG->f_EventNodes()).arg(l_pG->f_ValueNodes()));
    return l_pG;
}

void EventEditor::f_OpenValue(const std::string& l_strRef) {
    if (!m_oOpenVals.contains(f_Q(l_strRef))) m_oOpenVals << f_Q(l_strRef);
    f_SetTab(QStringLiteral("val:") + f_Q(l_strRef));
}

void EventEditor::f_CloseValue(const std::string& l_strRef) {
    m_oOpenVals.removeAll(f_Q(l_strRef));
    if (m_oTab == QStringLiteral("val:") + f_Q(l_strRef)) m_oTab = QStringLiteral("vars");
    f_ScheduleRender();
}

// ---- rendering -----------------------------------------------------------------------------------------------------

void EventEditor::f_RenderAll() {
    if (m_oP.m_jS.is_null()) return;
    // the kind filter's list
    {
        m_pKindF->blockSignals(true);
        m_pKindF->clear();
        m_pKindF->addItem(tr("(all kinds)"), QString());
        std::vector<std::string> l_vKs = m_oP.f_KindsList();
        for (const Json& l_jE : m_oP.m_jS["events"]) {
            const std::string l_strK = l_jE["kind"].get<std::string>();
            if (!l_strK.empty() && std::find(l_vKs.begin(), l_vKs.end(), l_strK) == l_vKs.end()) l_vKs.push_back(l_strK);
        }
        for (const std::string& l_strK : l_vKs) {
            const QString l_oN = f_Q(m_oP.f_KindName(l_strK, f_Lang().toStdString()));
            m_pKindF->addItem(f_DotIcon(QColor(f_Q(m_oP.f_KindColor(l_strK)))), f_Q(l_strK) + (l_oN.isEmpty() ? QString() : QStringLiteral(" — ") + l_oN), f_Q(l_strK));
        }
        m_pKindF->setCurrentIndex(std::max(0, m_pKindF->findData(m_oKind)));
        m_pKindF->blockSignals(false);
    }
    f_RenderTree();
    f_RenderForm();
    f_RenderTabs();
    f_RenderPane();
    const auto l_vAll = m_oP.f_Problems();
    int l_iErrs = 0, l_iWarns = 0;
    for (const auto& l_oP : l_vAll) { const int l_iS = stage::f_RuleSev(l_oP.m_strRule); if (l_iS == 0) l_iErrs++; else if (l_iS == 1) l_iWarns++; }
    m_pProbs->setText(tr("⛔ %1 errors · ⚠ %2 warnings").arg(l_iErrs).arg(l_iWarns) + (m_oP.f_Dirty() ? QStringLiteral(" · ") + tr("unsaved changes") : QString()));
    m_pDirty->setText(m_oP.f_Dirty() ? QStringLiteral(" •") : QString());
    m_pUndoA->setEnabled(!m_oP.m_vUndoStack.empty());
}

void EventEditor::f_RenderTree() {
    const auto l_vAll = m_oP.f_Problems();
    std::set<std::string> l_setProbIds;
    for (const auto& l_oP : l_vAll) if (stage::f_RuleSev(l_oP.m_strRule) != 2) l_setProbIds.insert(l_oP.m_strId);
    const QString l_oQl = m_oQ.toLower();
    const bool l_bFiltering = !m_oQ.isEmpty() || !m_oKind.isEmpty() || m_bProbOnly;
    // hits: id -> "" (by id / kind / pool / floor) or the matching text
    std::map<std::string, QString> l_mapHits;
    for (const Json& l_jE : m_oP.m_jS["events"]) {
        const std::string l_strId = l_jE["id"].get<std::string>();
        if (!m_oKind.isEmpty() && f_Q(l_jE["kind"].get<std::string>()) != m_oKind) continue;
        if (m_bProbOnly && !l_setProbIds.count(l_strId)) continue;
        if (l_oQl.isEmpty()) { l_mapHits[l_strId] = QString(); continue; }
        QStringList l_oHay{f_Q(l_strId), f_Q(l_jE["kind"].get<std::string>()), f_Q(l_jE["pool"].get<std::string>()), f_Q(l_jE["text"].get<std::string>())};
        for (const Json* l_pF : m_oP.f_FloorsOf(l_strId)) l_oHay << f_Q((*l_pF)["id"].get<std::string>());
        bool l_bHit = false;
        for (const QString& l_oH : l_oHay) if (l_oH.toLower().contains(l_oQl)) { l_bHit = true; break; }
        if (l_bHit) { l_mapHits[l_strId] = QString(); continue; }
        const Json l_jT = m_oP.f_Text(l_jE["text"].get<std::string>());
        for (auto& [l_strK, l_jV] : l_jT.items())
            if (!f_StartsWith(l_strK, "_") && l_jV.is_string() && f_Q(l_jV.get<std::string>()).toLower().contains(l_oQl)) { l_mapHits[l_strId] = f_Q(l_jV.get<std::string>()).simplified(); break; }
    }
    std::vector<stage::EvProblem> l_vMissing;
    for (const auto& l_oP : l_vAll)
        if (l_oP.m_strRule == "missing_event" && m_oKind.isEmpty() && (l_oQl.isEmpty() || f_Q(l_oP.m_strId).toLower().contains(l_oQl) || (!l_oP.m_vWhereArgs.empty() && f_Q(l_oP.m_vWhereArgs[0]).toLower().contains(l_oQl))))
            l_vMissing.push_back(l_oP);
    const QString l_oFk = m_oQ + QLatin1Char('|') + m_oKind + QLatin1Char('|') + (m_bProbOnly ? QStringLiteral("1") : QString());
    if (l_oFk != m_oLastFilt) {   // a new filter opens the groups that have matches; after that, folding is the user's
        m_oLastFilt = l_oFk;
        if (l_bFiltering) {
            for (const Json& l_jE : m_oP.m_jS["events"]) if (l_mapHits.count(l_jE["id"].get<std::string>())) m_setCollapsed.erase(l_jE["pool"].get<std::string>());
            if (!l_vMissing.empty() && l_vMissing.size() <= 40) m_setCollapsed.erase(g_pszMissing);
        }
    }
    const int l_iScroll = m_pTree->verticalScrollBar()->value();
    m_pTree->setUpdatesEnabled(false);
    m_pTree->clear();
    QTreeWidgetItem* l_pSelItem = nullptr;
    int l_iShown = 0;
    for (const std::string& l_strPool : m_oP.f_Pools()) {
        std::vector<const Json*> l_vEvs;
        for (const Json& l_jE : m_oP.m_jS["events"]) if (l_jE["pool"] == l_strPool && l_mapHits.count(l_jE["id"].get<std::string>())) l_vEvs.push_back(&l_jE);
        if (l_bFiltering && l_vEvs.empty()) continue;
        auto* l_pG = new QTreeWidgetItem(m_pTree);
        l_pG->setText(0, f_Q(l_strPool) + QStringLiteral("  (%1)").arg(l_vEvs.size()) + (m_oP.f_IsNewPool(l_strPool) ? QStringLiteral("  ") + tr("NEW") : QString()));
        l_pG->setData(0, Qt::UserRole + 1, f_Q(l_strPool));
        QFont l_oF = l_pG->font(0);
        l_oF.setBold(true);
        l_pG->setFont(0, l_oF);
        auto* l_pPlus = new QToolButton;
        l_pPlus->setText(QStringLiteral("+"));
        l_pPlus->setAutoRaise(true);
        l_pPlus->setToolTip(tr("New event in %1").arg(f_Q(l_strPool)));
        connect(l_pPlus, &QToolButton::clicked, this, [this, l_strPool] { f_CreateEvent(l_strPool); });
        for (const Json* l_pE : l_vEvs) {
            const std::string l_strId = (*l_pE)["id"].get<std::string>();
            auto* l_pIt = new QTreeWidgetItem(l_pG);
            const bool l_bOrphan = m_oP.f_FloorsOf(l_strId).empty() && (*l_pE)["trigger"].value("on", std::string("step")) == "step";
            QString l_oLabel = f_Q(l_strId);
            if (m_oP.f_IsNewEvent(l_strId)) l_oLabel += QStringLiteral("  ") + tr("NEW");
            if (l_bOrphan) l_oLabel += QStringLiteral("  ⚠");
            l_pIt->setText(0, l_oLabel);
            l_pIt->setIcon(0, f_DotIcon(QColor(f_Q(m_oP.f_KindColor((*l_pE)["kind"].get<std::string>())))));
            l_pIt->setData(0, Qt::UserRole, f_Q(l_strId));
            l_pIt->setToolTip(0, f_Q(l_strId));
            const QString l_oHit = l_mapHits[l_strId];
            if (!l_oHit.isEmpty()) { l_pIt->setText(1, tr("text") + QStringLiteral(": ") + l_oHit.left(40)); l_pIt->setForeground(1, QColor("#8b929c")); }
            if (l_strId == m_oP.m_strSelected) l_pSelItem = l_pIt;
            l_iShown++;
        }
        m_pTree->setItemWidget(l_pG, 1, l_pPlus);
        l_pG->setExpanded(!m_setCollapsed.count(l_strPool));
    }
    if (!l_vMissing.empty()) {
        auto* l_pG = new QTreeWidgetItem(m_pTree);
        l_pG->setText(0, QStringLiteral("⛔ ") + tr("No pool (%1)").arg(l_vMissing.size()));
        l_pG->setForeground(0, QColor("#ff9b9b"));
        l_pG->setData(0, Qt::UserRole + 1, QString::fromLatin1(g_pszMissing));
        for (const auto& l_oP : l_vMissing) {
            auto* l_pIt = new QTreeWidgetItem(l_pG);
            l_pIt->setText(0, f_Q(l_oP.m_strId));
            l_pIt->setForeground(0, QColor("#ff9b9b"));
            l_pIt->setData(0, Qt::UserRole, f_Q(l_oP.m_strId));
            if (l_oP.m_strId == m_oP.m_strSelected) l_pSelItem = l_pIt;
        }
        l_pG->setExpanded(!m_setCollapsed.count(g_pszMissing));
    }
    if (!m_pTree->topLevelItemCount()) new QTreeWidgetItem(m_pTree, {tr("No event matches.")});
    if (l_pSelItem) m_pTree->setCurrentItem(l_pSelItem);
    m_pTree->setUpdatesEnabled(true);
    m_pTree->verticalScrollBar()->setValue(l_iScroll);
    int l_iTotal = (int)m_oP.m_jS["events"].size();
    for (const auto& l_oP : l_vAll) if (l_oP.m_strRule == "missing_event") l_iTotal++;
    m_pCount->setText(l_bFiltering ? tr("%1 of %2 events match").arg(l_iShown + (int)l_vMissing.size()).arg(l_iTotal) + QStringLiteral(" <a href='clear'>%1</a>").arg(tr("clear")) : QString());
}

void EventEditor::f_RenderForm() {
    const int l_iScroll = m_pForm->verticalScrollBar()->value();
    QWidget* l_pW = nullptr;
    if (Json* l_pF = m_oP.f_Flo(m_oP.m_strSelected)) l_pW = f_FloorForm(*l_pF);
    else if (m_oP.f_IsMissing(m_oP.m_strSelected)) l_pW = f_MissingForm(m_oP.m_strSelected);
    else if (Json* l_pE = m_oP.f_Ev(m_oP.m_strSelected)) l_pW = f_EventForm(*l_pE);
    else {
        l_pW = new QWidget;
        auto* l_pV = new QVBoxLayout(l_pW);
        l_pV->addWidget(f_Rich(tr("Select an event in the tree or a row in Problems, or click <b>+ New event</b>.")));
        l_pV->addStretch(1);
    }
    m_pForm->setWidget(l_pW);
    QTimer::singleShot(0, this, [this, l_iScroll] { m_pForm->verticalScrollBar()->setValue(l_iScroll); });
}

void EventEditor::f_RenderTabs() {
    const auto l_vAll = m_oP.f_Problems();
    int l_iN = 0;
    for (const auto& l_oP : l_vAll) if (stage::f_RuleSev(l_oP.m_strRule) != 2) l_iN++;
    struct T { QString m_oKey, m_oLabel; };
    std::vector<T> l_vTabs = {{QStringLiteral("problems"), tr("Problems (%1)").arg(l_iN)}, {QStringLiteral("floors"), tr("Floor map")},
                           {QStringLiteral("vars"), tr("Variables")}, {QStringLiteral("logic"), tr("Logic graph")},
                           {QStringLiteral("kinds"), tr("Kinds")},
                           {QStringLiteral("npcs"), tr("NPCs")}, {QStringLiteral("dlg"), tr("Dialogues")}};
    for (const QString& l_oR : m_oOpenVals) {
        const QString l_oK = l_oR.section(QLatin1Char(':'), 0, 0), l_oName = l_oR.section(QLatin1Char(':'), 1);
        l_vTabs.push_back({QStringLiteral("val:") + l_oR, (l_oK == QStringLiteral("var") ? QStringLiteral("◆ ") : l_oK == QStringLiteral("counter") ? QStringLiteral("▲ ") : QStringLiteral("⚑ ")) + l_oName});
    }
    if (m_oTab.startsWith(QStringLiteral("val:")) && !m_oOpenVals.contains(m_oTab.mid(4))) m_oTab = QStringLiteral("vars");
    m_pTabs->blockSignals(true);
    while (m_pTabs->count()) m_pTabs->removeTab(0);
    for (const T& l_oT : l_vTabs) {
        const int l_iI = m_pTabs->addTab(l_oT.m_oLabel);
        m_pTabs->setTabData(l_iI, l_oT.m_oKey);
        if (!l_oT.m_oKey.startsWith(QStringLiteral("val:"))) {   // only value tabs close
            m_pTabs->setTabButton(l_iI, QTabBar::RightSide, nullptr);
            m_pTabs->setTabButton(l_iI, QTabBar::LeftSide, nullptr);
        }
        if (l_oT.m_oKey == m_oTab) m_pTabs->setCurrentIndex(l_iI);
    }
    m_pTabs->blockSignals(false);
}

void EventEditor::f_RenderPane() {
    const int l_iScroll = m_pPane->verticalScrollBar()->value();
    m_pInfo->clear();
    QWidget* l_pW = nullptr;
    m_pGraphView = nullptr;
    if (m_oTab == QStringLiteral("floors")) l_pW = f_FloorMapPane();
    else if (m_oTab == QStringLiteral("logic")) l_pW = f_LogicPane();
    else if (m_oTab == QStringLiteral("vars")) l_pW = f_VarsPane();
    else if (m_oTab == QStringLiteral("kinds")) l_pW = f_KindsPane();
    else if (m_oTab == QStringLiteral("npcs")) l_pW = f_NpcsPane();
    else if (m_oTab == QStringLiteral("dlg")) l_pW = f_DialoguesPane();
    else if (m_oTab.startsWith(QStringLiteral("val:"))) l_pW = f_ValuePane(m_oTab.mid(4).toStdString());
    else l_pW = f_ProblemsPane();
    m_pPane->setWidget(l_pW);
    QTimer::singleShot(0, this, [this, l_iScroll] { m_pPane->verticalScrollBar()->setValue(l_iScroll); });
}

// ---- problems ------------------------------------------------------------------------------------------------------

QString EventEditor::f_RuleName(const std::string& l_strR) const {
    static const std::map<std::string, const char*> l_mapNames = {
        {"missing_event", QT_TR_NOOP("Missing event id")}, {"missing_text", QT_TR_NOOP("Missing text key")}, {"orphan", QT_TR_NOOP("Orphan event")},
        {"no_kind", QT_TR_NOOP("No kind")}, {"placeholder", QT_TR_NOOP("Placeholder text")}, {"untranslated", QT_TR_NOOP("Untranslated")},
        {"missing_title", QT_TR_NOOP("No title / description")}, {"unknown_kind", QT_TR_NOOP("Kind not in the list")},
        {"missing_dialogue", QT_TR_NOOP("Missing dialogue file")}, {"bad_dialogue", QT_TR_NOOP("Dialogue: missing node")},
        {"unknown_ref", QT_TR_NOOP("Unknown reference")}, {"bad_next", QT_TR_NOOP("Link to missing event")}, {"never_fires", QT_TR_NOOP("Never fires")},
        {"never_active", QT_TR_NOOP("Never active")}, {"logic_cycle", QT_TR_NOOP("Event loop")}, {"unused_var", QT_TR_NOOP("Unused variable")},
        {"missing_floor", QT_TR_NOOP("Missing floor")}, {"bad_unlock", QT_TR_NOOP("Unknown unlock event")}, {"unreachable", QT_TR_NOOP("Unreachable floor")}};
    auto l_itIt = l_mapNames.find(l_strR);
    return l_itIt == l_mapNames.end() ? f_Q(l_strR) : tr(l_itIt->second);
}

QString EventEditor::f_ProblemText(const stage::EvProblem& l_oP) const {
    const auto l_fnA = [&](size_t l_nI) { return l_nI < l_oP.m_vWhereArgs.size() ? f_Q(l_oP.m_vWhereArgs[l_nI]) : QString(); };
    const std::string& l_strC = l_oP.m_strWhereCode;
    if (l_strC.empty()) return l_fnA(0);
    if (l_strC == "wNoTitle") return tr("no %1").arg(l_fnA(0));
    if (l_strC == "wUnknownKind") return tr("kind %1 is not in kinds.json").arg(l_fnA(0));
    if (l_strC == "wUnknown") {
        const QString l_oK = l_fnA(0) == QStringLiteral("var") ? tr("Event variables") : l_fnA(0) == QStringLiteral("counter") ? tr("Counters")
                        : l_fnA(0) == QStringLiteral("flag") ? tr("Flags") : l_fnA(0) == QStringLiteral("event") ? tr("Events done") : l_fnA(0);
        return tr("%1 %2 does not exist").arg(l_oK, l_fnA(1));
    }
    if (l_strC == "wBadNext") return tr("next → %1").arg(l_fnA(0));
    if (l_strC == "wNoIncoming") return tr("fires only from a link, but no event links to it");
    if (l_strC == "wNpcNowhere") return tr("talks to %1, who is not placed on any floor").arg(l_fnA(0));
    if (l_strC == "wNeverActive") return tr("needs %1 ≥ %2, but no event changes %1").arg(l_fnA(0), l_fnA(1));
    if (l_strC == "wUnusedVar") return tr("no event reads or writes it");
    if (l_strC == "stairWhere") return QStringLiteral("%1 %2 %3").arg(l_fnA(0), l_fnA(1) == QStringLiteral("down") ? QStringLiteral("↓") : QStringLiteral("↑"), l_fnA(2));
    if (l_strC == "badUnlockWhere") return tr("%1 ↑ %2 waits for %3").arg(l_fnA(0), l_fnA(1), l_fnA(2));
    if (l_strC == "unreachWhere") return tr("no stair path from F01");
    if (l_strC == "wDlgStart") return tr("start node %1 does not exist").arg(l_fnA(0));
    if (l_strC == "wDlgNext") return tr("%1: next %2 does not exist").arg(l_fnA(0), l_fnA(1));
    return l_fnA(0);
}

QWidget* EventEditor::f_ProblemsPane() {
    const auto l_vAll = m_oP.f_Problems();
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    // rule chips: tick to show
    auto* l_pChips = new QWidget;
    auto* l_pFlow = new FlowLayout(l_pChips, 4);
    for (const auto& [l_strRule, l_iSev] : stage::f_EvRules()) {
        int l_iN = 0;
        for (const auto& l_oP : l_vAll) if (l_oP.m_strRule == l_strRule) l_iN++;
        auto* l_pC = f_Chip(f_SevIcon(l_iSev) + QStringLiteral(" ") + f_RuleName(l_strRule) + QStringLiteral("  %1").arg(l_iN), m_setRuleOff.count(l_strRule) ? QColor() : QColor("#4c9eff"));
        l_pC->setCheckable(true);
        l_pC->setChecked(!m_setRuleOff.count(l_strRule));
        const std::string l_strR = l_strRule;
        connect(l_pC, &QToolButton::clicked, this, [this, l_strR] { if (m_setRuleOff.count(l_strR)) m_setRuleOff.erase(l_strR); else m_setRuleOff.insert(l_strR); f_ScheduleRender(); });
        l_pFlow->addWidget(l_pC);
    }
    l_pV->addWidget(l_pChips);
    std::map<std::string, int> l_mapByAct;
    for (const auto& l_oP : l_vAll) if (l_oP.m_strRule == "missing_event") l_mapByAct[l_oP.m_strAct]++;
    if (!l_mapByAct.empty()) {
        auto* l_pRow = new QWidget;
        auto* l_pRf = new FlowLayout(l_pRow, 4);
        l_pRf->addWidget(f_Muted(tr("Missing ids by chapter:")));
        for (const auto& [l_strAct, l_iN] : l_mapByAct) {
            auto* l_pB = new QPushButton(tr("%1: create %2").arg(f_Q(l_strAct)).arg(l_iN));
            const std::string l_strA = l_strAct;
            connect(l_pB, &QPushButton::clicked, this, [this, l_strA] { f_CreateChapter(l_strA); });
            l_pRf->addWidget(l_pB);
        }
        l_pV->addWidget(l_pRow);
    }
    std::vector<stage::EvProblem> l_vList;
    for (const auto& l_oP : l_vAll) if (!m_setRuleOff.count(l_oP.m_strRule)) l_vList.push_back(l_oP);
    auto l_fnRuleIdx = [](const std::string& l_strR) { int l_iI = 0; for (const auto& [l_strK, l_iS] : stage::f_EvRules()) { (void)l_iS; if (l_strK == l_strR) return l_iI; l_iI++; } return l_iI; };
    std::stable_sort(l_vList.begin(), l_vList.end(), [&](const auto& l_oA, const auto& l_oB) { return l_fnRuleIdx(l_oA.m_strRule) != l_fnRuleIdx(l_oB.m_strRule) ? l_fnRuleIdx(l_oA.m_strRule) < l_fnRuleIdx(l_oB.m_strRule) : l_oA.m_strId < l_oB.m_strId; });
    const size_t l_nShown = std::min<size_t>(l_vList.size(), 150);
    auto* l_pTable = new QTableWidget((int)l_nShown + (l_vList.size() > l_nShown ? 1 : 0), 5);
    l_pTable->setHorizontalHeaderLabels({QString(), tr("Id"), tr("Problem"), tr("Where"), QString()});
    l_pTable->verticalHeader()->hide();
    l_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    l_pTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    for (size_t l_nI = 0; l_nI < l_nShown; l_nI++) {
        const auto& l_oP = l_vList[l_nI];
        const int l_iSev = stage::f_RuleSev(l_oP.m_strRule);
        auto* l_pS = new QTableWidgetItem(f_SevIcon(l_iSev));
        l_pS->setForeground(l_iSev == 0 ? QColor("#ff5d5d") : l_iSev == 1 ? QColor("#ff9f43") : QColor("#8b929c"));
        l_pTable->setItem((int)l_nI, 0, l_pS);
        auto* l_pIdItem = new QTableWidgetItem(f_Q(l_oP.m_strId));
        QFont l_oBf = l_pIdItem->font();
        l_oBf.setBold(true);
        l_pIdItem->setFont(l_oBf);
        l_pTable->setItem((int)l_nI, 1, l_pIdItem);
        l_pTable->setItem((int)l_nI, 2, new QTableWidgetItem(f_RuleName(l_oP.m_strRule)));
        l_pTable->setItem((int)l_nI, 3, new QTableWidgetItem(f_ProblemText(l_oP)));
        // fix buttons
        std::vector<std::pair<QString, QString>> l_vFixes;
        const std::string& l_strR = l_oP.m_strRule;
        if (l_strR == "missing_event") l_vFixes = {{"create", tr("Create event")}, {"remove", tr("Remove from floors")}};
        else if (l_strR == "orphan") l_vFixes = {{"connect", tr("Add floor…")}, {"delete", tr("Delete")}};
        else if (l_strR == "no_kind") l_vFixes = {{"open", tr("Choose kind")}};
        else if (l_strR == "placeholder") l_vFixes = {{"open", tr("Write text")}};
        else if (l_strR == "missing_text") l_vFixes = {{"mktext", tr("Create text")}};
        else if (l_strR == "missing_floor") l_vFixes = {{"mkfloor", tr("Create floor")}, {"floor", tr("Open floor")}};
        else if (l_strR == "bad_unlock" || l_strR == "unreachable") l_vFixes = {{"floor", tr("Open floor")}};
        else if (l_strR == "unused_var") l_vFixes = {{"vars", tr("Open variables")}};
        else if (l_strR == "unknown_kind") l_vFixes = {{"open", tr("Choose kind")}, {"kinds", tr("Open Kinds")}};
        else if (l_strR == "bad_dialogue") l_vFixes = {{"dlg", tr("Open")}};
        else l_vFixes = {{"open", tr("Open")}};
        auto* l_pCell = new QWidget;
        auto* l_pCh = new QHBoxLayout(l_pCell);
        l_pCh->setContentsMargins(2, 0, 2, 0);
        for (const auto& [l_oK, l_oLabel] : l_vFixes) {
            auto* l_pB = new QToolButton;
            l_pB->setText(l_oLabel);
            const stage::EvProblem l_oPp = l_oP;
            const QString l_oKk = l_oK;
            connect(l_pB, &QToolButton::clicked, this, [this, l_oPp, l_oKk] { f_RunFix(l_oPp, l_oKk); });
            l_pCh->addWidget(l_pB);
        }
        l_pTable->setCellWidget((int)l_nI, 4, l_pCell);
    }
    if (l_vList.size() > l_nShown) l_pTable->setItem((int)l_nShown, 1, new QTableWidgetItem(tr("… and %1 more").arg(l_vList.size() - l_nShown)));
    if (l_vList.empty()) { l_pTable->setRowCount(1); l_pTable->setItem(0, 1, new QTableWidgetItem(tr("No problems. 🎉"))); }
    l_pTable->resizeColumnsToContents();
    l_pTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(l_pTable, &QTableWidget::cellClicked, this, [this, l_vList, l_nShown](int l_iRow, int) {
        if (l_iRow < 0 || (size_t)l_iRow >= l_nShown) return;
        const auto& l_oP = l_vList[(size_t)l_iRow];
        if (l_oP.m_strRule == "unused_var") f_SetTab(QStringLiteral("vars"));
        else if (l_oP.m_strRule == "bad_dialogue") { m_strDlgSel = l_oP.m_strId; f_SetTab(QStringLiteral("dlg")); }
        else f_Select(l_oP.m_strId, true);
    });
    l_pTable->setMinimumHeight(260);
    l_pV->addWidget(l_pTable, 1);
    int l_iErrs = 0, l_iWarns = 0;
    for (const auto& l_oP : l_vAll) { const int l_iS = stage::f_RuleSev(l_oP.m_strRule); if (l_iS == 0) l_iErrs++; else if (l_iS == 1) l_iWarns++; }
    m_pInfo->setText(tr("%1 errors · %2 warnings").arg(l_iErrs).arg(l_iWarns));
    return l_pW;
}

void EventEditor::f_RunFix(const stage::EvProblem& l_oP, const QString& l_oK) {
    if (l_oK == QStringLiteral("vars")) { f_SetTab(QStringLiteral("vars")); return; }
    if (l_oK == QStringLiteral("kinds")) { f_SetTab(QStringLiteral("kinds")); return; }
    if (l_oK == QStringLiteral("dlg")) { m_strDlgSel = l_oP.m_strId; f_SetTab(QStringLiteral("dlg")); return; }
    if (l_oK == QStringLiteral("mkfloor")) { f_CreateFloor(l_oP.m_strTo, l_oP.m_strId); return; }
    if (l_oK == QStringLiteral("floor")) { m_oTab = QStringLiteral("floors"); f_Select(l_oP.m_strId); return; }
    if (l_oK == QStringLiteral("create")) {
        const auto l_vFl = m_oP.f_FloorsOf(l_oP.m_strId);
        if (l_vFl.empty()) return;
        const std::string l_strId = l_oP.m_strId, l_strAct = (*l_vFl[0])["act"].get<std::string>();
        f_Edit(tr("Create %1").arg(f_Q(l_strId)), [this, l_strId, l_strAct] { m_oP.f_MakeRecord(l_strId, l_strAct); m_oP.m_strSelected = l_strId; });
        return;
    }
    if (l_oK == QStringLiteral("remove")) {
        const std::string l_strId = l_oP.m_strId;
        f_Edit(tr("Remove %1 from floors").arg(f_Q(l_strId)), [this, l_strId] {
            for (Json& l_jF : m_oP.m_jS["floors"]) {
                Json& l_jEvs = l_jF["events"];
                l_jEvs.erase(std::remove(l_jEvs.begin(), l_jEvs.end(), Json(l_strId)), l_jEvs.end());
            }
            m_oP.m_strSelected.clear();
        });
        return;
    }
    if (l_oK == QStringLiteral("delete")) {
        const std::string l_strId = l_oP.m_strId;
        f_Edit(tr("Delete %1").arg(f_Q(l_strId)), [this, l_strId] {
            Json& l_jEvs = m_oP.m_jS["events"];
            for (size_t l_nI = 0; l_nI < l_jEvs.size(); l_nI++) if (l_jEvs[l_nI]["id"] == l_strId) { l_jEvs.erase(l_nI); break; }
            m_oP.m_strSelected.clear();
        });
        return;
    }
    if (l_oK == QStringLiteral("mktext")) {
        const Json* l_pE = m_oP.f_Ev(l_oP.m_strId);
        if (!l_pE) return;
        const std::string l_strKey = (*l_pE)["text"].get<std::string>();
        f_Edit(tr("Create text %1").arg(f_Q(l_strKey)), [this, l_strKey] { Json l_jT = Json::object(); for (const char* l_pszL : stage::g_arrLangs) l_jT[l_pszL] = ""; m_oP.m_jS["texts"][l_strKey] = l_jT; });
        return;
    }
    f_Select(l_oP.m_strId);
    if (l_oK == QStringLiteral("connect")) QTimer::singleShot(0, this, [this, id = l_oP.m_strId] { f_AddFloorsDialog(id); });
}

// ---- save ----------------------------------------------------------------------------------------------------------

bool EventEditor::f_Save() {
    const std::map<std::string, std::string> l_mapComments = {
        {"vars", "Event variables: read and written by event logic (docs/gameplay/event_editor/06_EVENT_LOGIC.md)."},
        {"kinds", "Event kinds: names and editor colours (edited in the event editor's Kinds tab)."}};
    const auto l_vFiles = m_oP.f_LiveWrites(l_mapComments);
    if (l_vFiles.empty()) { f_Toast(tr("Nothing to save")); if (m_oP.f_Dirty()) { m_oP.m_jBase = m_oP.m_jS; f_ScheduleRender(); } return true; }
    int l_iWarn = 0;
    for (const auto& l_oP : m_oP.f_Problems()) if (stage::f_RuleSev(l_oP.m_strRule) != 2) l_iWarn++;
    QDialog l_oDlg(this);
    l_oDlg.setWindowTitle(tr("Save changes"));
    l_oDlg.resize(940, 680);
    auto* l_pV = new QVBoxLayout(&l_oDlg);
    l_pV->addWidget(f_Rich(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2</span><br>%3").arg(tr("Save changes"),
        tr("· a backup of each file is copied first, then these files are written"),
        tr("%1 file(s) change.").arg(l_vFiles.size()) + (l_iWarn ? QStringLiteral(" ") + tr("%1 problem(s) remain; saving is still allowed.").arg(l_iWarn) : QString()))));
    auto* l_pView = new QTextBrowser;
    QString l_oHtml;
    for (const auto& l_oF : l_vFiles) {
        l_oHtml += QStringLiteral("<p><b>%1</b>%2</p><pre style='font-family:Consolas; font-size:12px'>").arg(f_Esc(f_Q(l_oF.m_strPath)), l_oF.m_bExisted ? QString() : QStringLiteral("  ") + tr("(new file)"));
        for (const auto& [l_cC, l_strLine] : f_FileDiff(l_oF.m_strBefore, l_oF.m_strText)) {
            const QString l_oL = f_Esc(f_Q(l_strLine));
            if (l_cC == '+') l_oHtml += QStringLiteral("<span style='color:#7ee2a8; background:#173323'>+ %1</span>\n").arg(l_oL);
            else if (l_cC == '-') l_oHtml += QStringLiteral("<span style='color:#ff9b9b; background:#3a1b1b'>- %1</span>\n").arg(l_oL);
            else l_oHtml += QStringLiteral("<span style='color:#8b929c'>  %1</span>\n").arg(l_oL);
        }
        l_oHtml += QStringLiteral("</pre>");
    }
    l_pView->setHtml(l_oHtml);
    l_pV->addWidget(l_pView, 1);
    l_pV->addWidget(f_Muted(tr("Writes into assets/data. Each file is backed up to Build/editor_backups first.")));
    auto* l_pBb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* l_pWrite = l_pBb->addButton(tr("Write to project (%1 file(s))").arg(l_vFiles.size()), QDialogButtonBox::AcceptRole);
    l_pWrite->setDefault(true);
    connect(l_pBb, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pBb, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_pV->addWidget(l_pBb);
    if (l_oDlg.exec() != QDialog::Accepted) return false;
    const std::string l_strStamp = stage::f_TimeStamp();
    std::string l_strErr;
    if (!m_oP.f_WriteAll(l_vFiles, l_strStamp, l_strErr)) { f_Toast(tr("Could not write: %1").arg(f_Q(l_strErr)), true); return false; }
    QStringList l_oPaths;
    for (const auto& l_oF : l_vFiles) l_oPaths << f_Q(l_oF.m_strPath);
    std::string l_strIerr;
    const bool l_bIndexed = stage::f_SyncFloorIndex(m_oP.m_strRoot, &l_strIerr);
    m_pHist->addItem(tr("Wrote %1 file(s) to assets/data").arg(l_vFiles.size()));
    m_pHist->item(m_pHist->count() - 1)->setForeground(QColor("#4cd18a"));
    f_Toast(tr("Wrote %1 file(s) to assets/data. Backups: Build/editor_backups/%2/").arg(l_vFiles.size()).arg(f_Q(l_strStamp))
          + (l_bIndexed ? QStringLiteral("  ") + tr("Event index updated (assets/data/events/floor_index.json).") : QString()));
    f_ScheduleRender();
    Q_EMIT f_FilesWritten(l_oPaths);
    Q_EMIT f_DirtyChanged();
    return true;
}

// ---- --selftest ----------------------------------------------------------------------------------------------------

QStringList EventEditor::f_SelfTest() {
    QStringList l_oFail;
    auto l_fnCheck = [&](bool l_bOk, const QString& l_oWhat) { if (!l_bOk) l_oFail << QStringLiteral("events: ") + l_oWhat; };
    l_fnCheck(!m_oP.m_jS["events"].empty(), QStringLiteral("events loaded"));
    l_fnCheck(!m_oP.f_Dirty(), QStringLiteral("nothing unsaved after loading"));
    l_fnCheck(m_oP.f_LiveWrites({}).empty(), QStringLiteral("nothing to write after loading"));
    // an event's text, then undo
    Json* l_pE = m_oP.f_Ev("ev_village_well");
    l_fnCheck(l_pE != nullptr, QStringLiteral("ev_village_well exists"));
    if (l_pE) {
        f_Select("ev_village_well");
        f_RenderAll();
        const std::string l_strKey = (*l_pE)["text"].get<std::string>();
        f_Edit(QStringLiteral("selftest text"), [this, l_strKey] { m_oP.m_jS["texts"][l_strKey]["en"] = "selftest"; });
        f_RenderAll();
        const auto l_vFiles = m_oP.f_LiveWrites({});
        l_fnCheck(l_vFiles.size() == 1 && l_vFiles[0].m_strPath == "assets/data/text.json", QStringLiteral("a text edit writes only text.json (%1 files)").arg(l_vFiles.size()));
        f_Undo();
        l_fnCheck(!m_oP.f_Dirty(), QStringLiteral("undo restores the text"));
        // a condition row and an action
        f_Edit(QStringLiteral("selftest logic"), [this] {
            Json& l_jEv = *m_oP.f_Ev("ev_village_well");
            l_jEv["req"].push_back(stage::f_RowToLeaf("event:ev_village_ledger", "set", Json()));
            l_jEv["actions"].push_back(stage::f_MakeAction("addCounter", "insight", 1));
        });
        f_RenderAll();
        bool l_bPoolOnly = true;
        for (const auto& l_oF : m_oP.f_LiveWrites({})) l_bPoolOnly &= f_StartsWith(l_oF.m_strPath, "assets/data/events/pool_");
        l_fnCheck(l_bPoolOnly, QStringLiteral("a logic edit writes only its pool"));
        f_Undo();
    }
    // each tab renders
    for (const QString& l_oT : {QStringLiteral("problems"), QStringLiteral("floors"), QStringLiteral("vars"), QStringLiteral("logic"), QStringLiteral("kinds"), QStringLiteral("npcs"), QStringLiteral("dlg")}) {
        m_oTab = l_oT;
        f_RenderAll();
        l_fnCheck(m_pPane->widget() != nullptr, QStringLiteral("tab %1 renders").arg(l_oT));
    }
    // the logic graph: the elder's events are nodes with links; connecting two pins is an edit of the event
    m_oTab = QStringLiteral("logic");
    f_RenderAll();
    l_fnCheck(m_pGraphView != nullptr && m_pGraphView->f_EventNodes() > 0 && !m_pGraphView->f_Links().empty(),
          QStringLiteral("the logic graph shows events and links"));
    if (m_pGraphView) {
        // two event nodes the first does not link to yet: Then -> Fire adds the link, undo removes it
        std::vector<std::string> l_vEvs;
        for (const std::string& l_strId : m_pGraphView->f_NodeIds()) if (f_StartsWith(l_strId, "ev:") && m_oP.f_Ev(l_strId.substr(3))) l_vEvs.push_back(l_strId.substr(3));
        std::string l_strA, l_strB;
        for (const std::string& l_strX : l_vEvs)
            for (const std::string& l_strY : l_vEvs) {
                if (!l_strA.empty() || l_strX == l_strY) continue;
                bool l_bAlready = false;
                for (const Json& l_jN : (*m_oP.f_Ev(l_strX))["next"]) l_bAlready |= l_jN == l_strY;
                if (!l_bAlready) { l_strA = l_strX; l_strB = l_strY; }
            }
        l_fnCheck(!l_strA.empty(), QStringLiteral("the graph has two event nodes to link"));
        if (!l_strA.empty()) {
            const size_t l_nBefore = (*m_oP.f_Ev(l_strA))["next"].size();
            const bool l_bLinked = m_pGraphView->f_ConnectPins("ev:" + l_strA + "|then", "ev:" + l_strB + "|fire");
            l_fnCheck(l_bLinked && (*m_oP.f_Ev(l_strA))["next"].size() == l_nBefore + 1, QStringLiteral("Then → Fire adds a link"));
            l_fnCheck(!m_pGraphView->f_ConnectPins("ev:" + l_strA + "|fire", "ev:" + l_strB + "|fire"), QStringLiteral("two inputs do not connect"));
            if (l_bLinked) f_Undo();
            l_fnCheck(!m_oP.f_Dirty(), QStringLiteral("undo removes the graph's link"));
        }
    }
    // the talk preview on the villager
    f_ShowNpc(QStringLiteral("villager"));
    f_RenderAll();
    m_oPv.f_Reset(m_oP, "villager");
    m_oPv.f_Talk(m_oP, "villager", "en");
    l_fnCheck(!m_oPv.m_vLog.empty(), QStringLiteral("the talk preview talks to the villager"));
    // a value tab
    if (!m_oP.f_ValueRefs().empty()) { f_OpenValue(m_oP.f_ValueRefs()[0]); f_RenderAll(); l_fnCheck(m_pPane->widget() != nullptr, QStringLiteral("a value tab renders")); }
    // the floor inspector and a missing id
    f_Select("F02");
    f_RenderAll();
    l_fnCheck(m_pForm->widget() != nullptr, QStringLiteral("the floor inspector renders"));
    l_fnCheck(!m_oP.f_Dirty(), QStringLiteral("nothing unsaved at the end"));
    m_oTab = QStringLiteral("problems");
    m_oOpenVals.clear();
    f_RenderAll();
    return l_oFail;
}

void EventEditor::f_ResetLayout() {
    const std::pair<const char*, Qt::DockWidgetArea> l_arrHome[] = {{"EventsTreeDock", Qt::LeftDockWidgetArea}, {"HistoryDock", Qt::RightDockWidgetArea}};
    QList<QDockWidget*> l_oDocks;
    for (const auto& [l_pszName, l_eArea] : l_arrHome) {
        auto* l_pD = findChild<QDockWidget*>(QLatin1String(l_pszName));
        if (!l_pD) continue;
        l_pD->setFloating(false);
        removeDockWidget(l_pD);
        addDockWidget(l_eArea, l_pD);
        l_pD->show();
        l_oDocks << l_pD;
    }
    if (l_oDocks.size() == 2) resizeDocks(l_oDocks, {300, 280}, Qt::Horizontal);
    if (m_pSplit) {   // the form over the lists, 3 : 2
        const int l_iH = std::max(500, m_pSplit->height());
        m_pSplit->setSizes({l_iH * 3 / 5, l_iH * 2 / 5});
    }
}
