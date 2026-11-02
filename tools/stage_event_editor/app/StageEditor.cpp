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
using StageText::f_Q;

namespace {

bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }

// a palette entry: a click picks the brush, a double-click opens the type's details
class PaletteButton : public QToolButton {
public:
    std::function<void()> m_fnOnDouble;
protected:
    void mouseDoubleClickEvent(QMouseEvent* l_pE) override {
        if (m_fnOnDouble) m_fnOnDouble();
        else QToolButton::mouseDoubleClickEvent(l_pE);
    }
};

QString f_Esc(const QString& l_oS) { return l_oS.toHtmlEscaped(); }
QString f_Esc(const std::string& l_strS) { return f_Q(l_strS).toHtmlEscaped(); }

QString f_KeyChips(const stage::Keys& l_oK) {
    static const char* l_arrColors[3] = {"#e0c34c", "#6aa8ff", "#ff7b7b"};
    QString l_oOut;
    for (int l_iC = 0; l_iC < 3; l_iC++)
        if (l_oK.m_arrN[l_iC]) l_oOut += QStringLiteral("<span style='background:%1;color:#111;font-weight:bold'>&nbsp;%2&nbsp;</span> ").arg(l_arrColors[l_iC]).arg(l_oK.m_arrN[l_iC]);
    return l_oOut.isEmpty() ? QStringLiteral("<span style='color:#8b929c'>0</span>") : l_oOut;
}

QString f_SevIcon(stage::Sev l_eS) { return l_eS == stage::Sev::Error ? QStringLiteral("⛔") : l_eS == stage::Sev::Warn ? QStringLiteral("⚠") : QStringLiteral("ⓘ"); }
QColor f_SevColor(stage::Sev l_eS) { return l_eS == stage::Sev::Error ? QColor("#ff5d5d") : l_eS == stage::Sev::Warn ? QColor("#ff9f43") : QColor("#8b929c"); }

QLabel* f_Rich(const QString& l_oHtml, QWidget* l_pParent = nullptr) {
    auto* l_pL = new QLabel(l_oHtml, l_pParent);
    l_pL->setTextFormat(Qt::RichText);
    l_pL->setWordWrap(true);
    l_pL->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l_pL->setMinimumWidth(60);   // wraps to the panel's width instead of widening it
    return l_pL;
}

QLabel* f_Heading(const QString& l_oText) {
    auto* l_pL = new QLabel(l_oText);
    l_pL->setStyleSheet(QStringLiteral("color:#8b929c; font-size:11px; font-weight:600"));
    return l_pL;
}

QIcon f_PlateIcon() {
    QImage l_oImg(14, 14, QImage::Format_ARGB32_Premultiplied);
    l_oImg.fill(Qt::transparent);
    QPainter l_oG(&l_oImg);
    l_oG.fillRect(QRect(0, 0, 14, 14), QColor("#dbb85c"));
    l_oG.fillRect(QRect(3, 3, 8, 8), QColor("#475775"));
    return QIcon(QPixmap::fromImage(l_oImg));
}

}  // namespace

// ---- construction ----------------------------------------------------------------------------------------------------

StageEditor::StageEditor(QWidget* l_pParent) : QMainWindow(l_pParent) {
    setObjectName(QStringLiteral("StageEditor"));
    setWindowFlags(Qt::Widget);   // hosted in the app's tab or window
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    f_BuildUi();
    m_pChecksTimer = new QTimer(this);
    m_pChecksTimer->setSingleShot(true);
    m_pChecksTimer->setInterval(30);
    connect(m_pChecksTimer, &QTimer::timeout, this, &StageEditor::f_RenderProblems);
    m_pFlashTimer = new QTimer(this);
    m_pFlashTimer->setSingleShot(true);
    connect(m_pFlashTimer, &QTimer::timeout, this, [this] { m_oV.m_vFlash.clear(); m_pCanvas->update(); });
    m_pReloadTimer = new QTimer(this);
    m_pReloadTimer->setSingleShot(true);
    m_pReloadTimer->setInterval(400);
    connect(m_pReloadTimer, &QTimer::timeout, this, [this] {
        if (m_bIgnoreWatch) { m_bIgnoreWatch = false; f_WatchFiles(); return; }
        f_ReloadEvents();
    });
    connect(Language::Notifier::f_Instance(), &Language::Notifier::f_Changed, this, &StageEditor::f_RetranslateDynamic);
}

StageEditor::~StageEditor() {
    if (!m_bSaveLayout) return;
    QSettings l_oS;
    l_oS.setValue(QStringLiteral("stage/docks"), saveState());
}

QList<QDockWidget*> StageEditor::f_Panels() const {
    QList<QDockWidget*> l_oOut;
    for (const char* l_pszN : {"PaletteDock", "EventsDock", "InspectorDock", "ChecksDock"})
        if (auto* l_pD = findChild<QDockWidget*>(QLatin1String(l_pszN))) l_oOut << l_pD;
    return l_oOut;
}

void StageEditor::f_ResetLayout() {
    const std::pair<const char*, Qt::DockWidgetArea> l_arrHome[] = {{"PaletteDock", Qt::LeftDockWidgetArea}, {"EventsDock", Qt::LeftDockWidgetArea},
                                                                {"InspectorDock", Qt::RightDockWidgetArea}, {"ChecksDock", Qt::BottomDockWidgetArea}};
    for (const auto& [l_pszName, l_eArea] : l_arrHome) {
        auto* l_pD = findChild<QDockWidget*>(QLatin1String(l_pszName));
        if (!l_pD) continue;
        l_pD->setFloating(false);
        removeDockWidget(l_pD);
        addDockWidget(l_eArea, l_pD);
        l_pD->show();
    }
    for (QToolBar* l_pT : findChildren<QToolBar*>()) l_pT->show();
    // their starting sizes: the palette column, the inspector, the checks below the map; palette above events
    const QList<QDockWidget*> l_oP = f_Panels();
    if (l_oP.size() == 4) {
        resizeDocks({l_oP[0], l_oP[2]}, {260, 320}, Qt::Horizontal);
        resizeDocks({l_oP[0], l_oP[1]}, {460, 300}, Qt::Vertical);
        resizeDocks({l_oP[3]}, {180}, Qt::Vertical);
    }
}

void StageEditor::f_BuildUi() {
    m_pCanvas = new StageCanvas(this);
    auto* l_pCenter = new QWidget(this);
    auto* l_pCv = new QVBoxLayout(l_pCenter);
    l_pCv->setContentsMargins(0, 0, 0, 0);
    l_pCv->setSpacing(0);
    // place mode: opened from the event editor to put one event on its floors
    m_pPlaceBar = new QWidget(l_pCenter);
    m_pPlaceBar->setStyleSheet(QStringLiteral("background:#1f3a2c; border-bottom:1px solid #2f6b4b"));
    auto* l_pPb = new QHBoxLayout(m_pPlaceBar);
    l_pPb->setContentsMargins(8, 4, 8, 4);
    m_pPmText = new QLabel(m_pPlaceBar);
    l_pPb->addWidget(m_pPmText, 1);
    m_pPmSuggest = new QToolButton(m_pPlaceBar);
    m_pPmAuto = new QToolButton(m_pPlaceBar);
    m_pPmNext = new QToolButton(m_pPlaceBar);
    auto* l_pPmClose = new QToolButton(m_pPlaceBar);
    l_pPmClose->setText(QStringLiteral("✕"));
    for (QToolButton* l_pB : {m_pPmSuggest, m_pPmAuto, m_pPmNext, l_pPmClose}) l_pPb->addWidget(l_pB);
    m_pPmSuggest->setText(tr("Suggest a cell"));
    m_pPmAuto->setText(tr("Place it for me"));
    connect(m_pPmSuggest, &QToolButton::clicked, this, [this] {
        stage::StageDoc* l_pD = f_CurDoc();
        if (!l_pD || !m_oV.m_optPlaceMode) return;
        const auto l_optC = stage::f_PickCell(m_oP, *l_pD, stage::f_SafeCells(m_oP, m_strCur),
                                       stage::f_Mulberry(stage::f_HashStr(m_oV.m_optPlaceMode->m_strId + m_strCur + std::to_string(QRandomGenerator::global()->generate()))));
        if (!l_optC) { f_Toast(tr("No safe free cell on this floor."), true); return; }
        m_oV.m_optSel = *l_optC;
        f_FlashCells({*l_optC});
        f_RenderInspector();
    });
    connect(m_pPmAuto, &QToolButton::clicked, this, [this] {
        stage::StageDoc* l_pD = f_CurDoc();
        if (!l_pD || !m_oV.m_optPlaceMode) return;
        const auto l_optC = stage::f_PickCell(m_oP, *l_pD, stage::f_SafeCells(m_oP, m_strCur), stage::f_Mulberry(stage::f_HashStr(m_oV.m_optPlaceMode->m_strId + m_strCur)));
        if (!l_optC) { f_Toast(tr("No safe free cell on this floor."), true); return; }
        const std::string l_strId = m_oV.m_optPlaceMode->m_strId;
        f_Edit(tr("Paint %1 (%2 cells)").arg(f_Q(l_strId)).arg(1), [&](stage::StageDoc& l_oDd) { m_oP.f_PutSem(l_oDd, l_optC->m_iX, l_optC->m_iY, "event:" + l_strId); });
        m_oV.m_optSel = *l_optC;
        f_RenderInspector();
    });
    connect(m_pPmNext, &QToolButton::clicked, this, [this] {
        if (!m_oV.m_optPlaceMode) return;
        const auto& l_vFl = m_oV.m_optPlaceMode->m_vFloors;
        auto l_itIt = std::find(l_vFl.begin(), l_vFl.end(), m_strCur);
        if (l_itIt != l_vFl.end() && l_itIt + 1 != l_vFl.end()) {
            const std::string l_strId = m_oV.m_optPlaceMode->m_strId;
            f_OpenFloor(*(l_itIt + 1));
            f_SetBrush("event:" + l_strId);
        } else {
            m_oV.m_optPlaceMode.reset();
            f_RenderPlaceBar();
            m_pCanvas->update();
        }
    });
    connect(l_pPmClose, &QToolButton::clicked, this, [this] { m_oV.m_optPlaceMode.reset(); f_RenderPlaceBar(); m_pCanvas->update(); });
    m_pPlaceBar->hide();
    l_pCv->addWidget(m_pPlaceBar);
    l_pCv->addWidget(m_pCanvas, 1);
    setCentralWidget(l_pCenter);
    f_BuildToolbars();
    f_BuildDocks();
    m_pHover = new QLabel(this);
    m_pStatus = new QLabel(this);
    statusBar()->addWidget(m_pHover, 1);
    statusBar()->addPermanentWidget(m_pStatus);
    connect(m_pCanvas, &CanvasView::f_ZoomChanged, this, [this](double l_dZ) { m_pZoomLbl->setText(QStringLiteral("%1%").arg(std::lround(l_dZ * 100))); });
    restoreState(QSettings().value(QStringLiteral("stage/docks")).toByteArray());
}

void StageEditor::f_BuildToolbars() {
    auto* l_pTop = addToolBar(tr("Stage"));
    l_pTop->setObjectName(QStringLiteral("StageBar"));
    l_pTop->setMovable(false);
    m_pToEventA = l_pTop->addAction(tr("⇄ Event editor"), this, [this] {
        stage::StageDoc* l_pD = f_CurDoc();
        std::string l_strEv;
        if (l_pD && m_oV.m_optSel) {
            const std::string l_strSem = m_oP.f_SemAt(*l_pD, m_oV.m_optSel->m_iX, m_oV.m_optSel->m_iY);
            if (f_StartsWith(l_strSem, "event:")) l_strEv = l_strSem.substr(6);
        }
        Q_EMIT f_OpenEventEditor(f_Q(l_strEv));
    });
    m_pToEventA->setToolTip(tr("Show the event editor (with the selected event)"));
    l_pTop->addWidget(new QLabel(QStringLiteral(" ") + tr("Floor") + QStringLiteral(" ")));
    m_pFloorSel = new QComboBox(this);
    m_pFloorSel->setMinimumContentsLength(18);
    connect(m_pFloorSel, &QComboBox::activated, this, [this](int l_iI) { f_OpenFloor(m_pFloorSel->itemData(l_iI).toString().toStdString()); });
    l_pTop->addWidget(m_pFloorSel);
    QAction* l_pPrev = l_pTop->addAction(QStringLiteral("◀"), this, [this] { f_Step(-1); });
    l_pPrev->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    l_pPrev->setToolTip(tr("Previous floor ([)"));
    QAction* l_pNext = l_pTop->addAction(QStringLiteral("▶"), this, [this] { f_Step(1); });
    l_pNext->setShortcut(QKeySequence(Qt::Key_BracketRight));
    l_pNext->setToolTip(tr("Next floor (])"));
    l_pTop->addSeparator();
    m_pUndoA = l_pTop->addAction(tr("Undo"), this, &StageEditor::f_Undo);
    m_pUndoA->setShortcut(QKeySequence::Undo);
    m_pRedoA = l_pTop->addAction(tr("Redo"), this, &StageEditor::f_Redo);
    m_pRedoA->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence::Redo});
    l_pTop->addSeparator();
    m_pSaveA = l_pTop->addAction(tr("Save"), this, [this] { f_Save(); });
    m_pSaveA->setShortcut(QKeySequence::Save);
    auto* l_pSpacer = new QWidget(this);
    l_pSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    l_pTop->addWidget(l_pSpacer);
    m_pSrc = new QLabel(this);
    m_pSrc->setStyleSheet(QStringLiteral("color:#4cd18a"));
    l_pTop->addWidget(m_pSrc);

    addToolBarBreak();
    auto* l_pTools = addToolBar(tr("Tools"));
    l_pTools->setObjectName(QStringLiteral("ToolsBar"));
    l_pTools->setMovable(false);
    auto* l_pGroup = new QActionGroup(this);
    const struct { Tool m_eT; const char* m_pszText; Qt::Key m_eKey; } l_arrDefs[] = {
        {Paint, QT_TR_NOOP("🖌 Paint"), Qt::Key_B}, {Rect, QT_TR_NOOP("▭ Rectangle"), Qt::Key_R}, {Erase, QT_TR_NOOP("⌫ Erase"), Qt::Key_E},
        {Select, QT_TR_NOOP("↖ Select / move"), Qt::Key_V}, {Edit, QT_TR_NOOP("✎ Edit"), Qt::Key_O}};
    for (const auto& l_oD : l_arrDefs) {
        QAction* l_pA = l_pTools->addAction(tr(l_oD.m_pszText));
        l_pA->setCheckable(true);
        l_pA->setShortcut(QKeySequence(l_oD.m_eKey));
        l_pA->setToolTip(QStringLiteral("%1 (%2)").arg(tr(l_oD.m_pszText), QKeySequence(l_oD.m_eKey).toString()));
        l_pGroup->addAction(l_pA);
        const Tool l_eT = l_oD.m_eT;
        connect(l_pA, &QAction::triggered, this, [this, l_eT] { f_SetTool(l_eT); });
        m_arrToolA[l_eT] = l_pA;
    }
    l_pTools->addSeparator();
    m_pGridA = l_pTools->addAction(tr("# Grid"));
    m_pGridA->setCheckable(true);
    m_pGridA->setChecked(true);
    m_pGridA->setShortcut(QKeySequence(Qt::Key_G));
    connect(m_pGridA, &QAction::toggled, this, [this](bool l_bOn) { m_oV.m_bShowGrid = l_bOn; m_pCanvas->update(); });
    m_pGameA = l_pTools->addAction(tr("🎮 Game view"));
    m_pGameA->setCheckable(true);
    m_pGameA->setShortcut(QKeySequence(Qt::Key_P));
    m_pGameA->setToolTip(tr("The map as the game draws it, without the editor's marks (P)"));
    connect(m_pGameA, &QAction::toggled, this, [this](bool l_bOn) { m_oV.m_bGameView = l_bOn; m_pCanvas->update(); });
    QAction* l_pZo = l_pTools->addAction(QStringLiteral("−"), m_pCanvas, &CanvasView::f_ZoomOut);
    l_pZo->setShortcut(QKeySequence(Qt::Key_Minus));
    m_pZoomLbl = new QLabel(QStringLiteral("100%"), this);
    m_pZoomLbl->setMinimumWidth(44);
    m_pZoomLbl->setAlignment(Qt::AlignCenter);
    l_pTools->addWidget(m_pZoomLbl);
    QAction* l_pZi = l_pTools->addAction(QStringLiteral("+"), m_pCanvas, &CanvasView::f_ZoomIn);
    l_pZi->setShortcuts({QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
    l_pTools->addAction(tr("Fit"), m_pCanvas, &CanvasView::f_FitToView);
    l_pTools->addSeparator();
    m_pSafeA = l_pTools->addAction(tr("◇ Safe cells"));
    m_pSafeA->setCheckable(true);
    m_pSafeA->setToolTip(tr("Free cells the player reaches on the way up without opening optional doors"));
    connect(m_pSafeA, &QAction::toggled, this, [this](bool l_bOn) { m_oV.m_bShowSafe = l_bOn; m_pCanvas->update(); });
    l_pTools->addAction(tr("🎲 Auto-place events…"), this, &StageEditor::f_OpenAutoPlace);
    auto* l_pSpacer2 = new QWidget(this);
    l_pSpacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    l_pTools->addWidget(l_pSpacer2);
    l_pTools->addWidget(new QLabel(tr("Brush:") + QStringLiteral(" ")));
    m_pBrushIcon = new QLabel(this);
    m_pBrushIcon->setFixedSize(24, 24);
    l_pTools->addWidget(m_pBrushIcon);
    m_pBrushName = new QLabel(this);
    m_pBrushName->setStyleSheet(QStringLiteral("font-weight:bold"));
    l_pTools->addWidget(m_pBrushName);

    // keys without a button
    auto* l_pClear = new QAction(this);
    l_pClear->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    connect(l_pClear, &QAction::triggered, this, [this] {
        if (!m_oV.m_optSel || !f_CurDoc()) return;
        const Cell l_oC = *m_oV.m_optSel;
        f_Edit(tr("Erase %1 cell(s)").arg(1), [&](stage::StageDoc& l_oD) { l_oD.m_vGrid[(size_t)l_oC.m_iY][(size_t)l_oC.m_iX] = '.'; stage::Project::f_DropObj(l_oD, l_oC.m_iX, l_oC.m_iY); });
    });
    auto* l_pEsc = new QAction(this);
    l_pEsc->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(l_pEsc, &QAction::triggered, this, [this] { m_oV.m_optSel.reset(); f_RenderInspector(); m_pCanvas->update(); });
    m_pCanvas->addAction(l_pClear);
    m_pCanvas->addAction(l_pEsc);
    for (QAction* l_pA : {l_pClear, l_pEsc}) l_pA->setShortcutContext(Qt::WidgetShortcut);
}

void StageEditor::f_BuildDocks() {
    auto* l_pPal = new QDockWidget(tr("Palette"), this);
    l_pPal->setObjectName(QStringLiteral("PaletteDock"));
    m_pPaletteArea = new QScrollArea(l_pPal);
    m_pPaletteArea->setWidgetResizable(true);
    m_pPaletteArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l_pPal->setWidget(m_pPaletteArea);
    addDockWidget(Qt::LeftDockWidgetArea, l_pPal);

    auto* l_pEvd = new QDockWidget(tr("Events"), this);
    l_pEvd->setObjectName(QStringLiteral("EventsDock"));
    auto* l_pEvw = new QWidget(l_pEvd);
    auto* l_pEvl = new QVBoxLayout(l_pEvw);
    l_pEvl->setContentsMargins(4, 4, 4, 4);
    m_pEvSearch = new QLineEdit(l_pEvw);
    m_pEvSearch->setPlaceholderText(tr("Find event: id, kind or text"));
    m_pEvSearch->setClearButtonEnabled(true);
    connect(m_pEvSearch, &QLineEdit::textChanged, this, &StageEditor::f_RenderEventPalette);
    m_pEvTree = new QTreeWidget(l_pEvw);
    m_pEvTree->setColumnCount(3);
    m_pEvTree->setHeaderLabels({tr("Event"), tr("Kind"), tr("Placed")});
    m_pEvTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pEvTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pEvTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_pEvTree->setTextElideMode(Qt::ElideMiddle);
    m_pEvTree->header()->setStretchLastSection(false);
    m_pEvTree->setRootIsDecorated(true);
    m_pEvTree->setUniformRowHeights(true);
    connect(m_pEvTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* l_pIt) {
        const QString l_oId = l_pIt->data(0, Qt::UserRole).toString();
        if (!l_oId.isEmpty()) f_SetBrush("event:" + l_oId.toStdString());
    });
    l_pEvl->addWidget(m_pEvSearch);
    l_pEvl->addWidget(m_pEvTree, 1);
    l_pEvd->setWidget(l_pEvw);
    addDockWidget(Qt::LeftDockWidgetArea, l_pEvd);
    splitDockWidget(l_pPal, l_pEvd, Qt::Vertical);

    auto* l_pIns = new QDockWidget(tr("Inspector"), this);
    l_pIns->setObjectName(QStringLiteral("InspectorDock"));
    m_pInspArea = new QScrollArea(l_pIns);
    m_pInspArea->setWidgetResizable(true);
    m_pInspArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // its texts wrap
    m_pInspArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);      // a steady width: wrapping is not redone when it appears
    l_pIns->setWidget(m_pInspArea);
    addDockWidget(Qt::RightDockWidgetArea, l_pIns);

    auto* l_pChk = new QDockWidget(tr("Checks"), this);
    l_pChk->setObjectName(QStringLiteral("ChecksDock"));
    m_pBottomTabs = new QTabWidget(l_pChk);
    m_pBottomTabs->setDocumentMode(true);
    m_pChecks = new QTreeWidget(m_pBottomTabs);
    m_pChecks->setColumnCount(2);
    m_pChecks->setHeaderHidden(true);
    m_pChecks->setRootIsDecorated(false);
    m_pChecks->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pChecks->header()->setStretchLastSection(false);
    m_pChecks->setWordWrap(true);
    m_pBottomTabs->addTab(m_pChecks, QString());
    auto* l_pStoryW = new QWidget(m_pBottomTabs);
    auto* l_pSl = new QVBoxLayout(l_pStoryW);
    l_pSl->setContentsMargins(0, 0, 0, 0);
    m_pStorySum = new QLabel(l_pStoryW);
    m_pStorySum->setTextFormat(Qt::RichText);
    m_pStorySum->setWordWrap(true);
    m_pStorySum->setContentsMargins(6, 2, 6, 2);
    m_pStory = new QTableWidget(l_pStoryW);
    m_pStory->setColumnCount(7);
    m_pStory->verticalHeader()->hide();
    m_pStory->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pStory->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pStory->horizontalHeader()->setStretchLastSection(true);
    connect(m_pStory, &QTableWidget::cellClicked, this, [this](int l_iRow, int) {
        const QString l_oFid = m_pStory->item(l_iRow, 0) ? m_pStory->item(l_iRow, 0)->data(Qt::UserRole).toString() : QString();
        if (l_oFid.isEmpty()) return;
        f_OpenFloor(l_oFid.toStdString());
        for (const stage::StoryRow& l_oR : stage::f_StoryCheck(m_oP))
            if (l_oR.m_strFid == m_strCur)
                for (const stage::Issue& l_oI : l_oR.m_vIssues)
                    if (!l_oI.m_vCells.empty()) { m_oV.m_optSel = l_oI.m_vCells[0]; f_FlashCells({l_oI.m_vCells[0]}); f_RenderInspector(); return; }
    });
    l_pSl->addWidget(m_pStorySum);
    l_pSl->addWidget(m_pStory, 1);
    m_pBottomTabs->addTab(l_pStoryW, QString());
    l_pChk->setWidget(m_pBottomTabs);
    connect(m_pBottomTabs, &QTabWidget::currentChanged, this, [this](int l_iI) { if (l_iI == 1) f_RenderStory(); });   // built when shown
    addDockWidget(Qt::BottomDockWidgetArea, l_pChk);
    m_pEvTree->setIndentation(12);
    resizeDocks({l_pPal, l_pIns}, {330, 330}, Qt::Horizontal);
    resizeDocks({l_pChk}, {170}, Qt::Vertical);
}

// ---- the project ---------------------------------------------------------------------------------------------------

bool StageEditor::f_OpenProject(const QString& l_oTomsRoot, QString* l_pError) {
    m_oRoot = l_oTomsRoot;
    m_oP.m_strLang = StageText::f_Lang();
    std::string l_strErr;
    if (!m_oP.f_Load(l_oTomsRoot.toStdString(), l_strErr)) {
        if (l_pError) *l_pError = f_Q(l_strErr);
        return false;
    }
    m_oBank.f_Load(l_oTomsRoot);
    m_pSrc->setText(tr("project folder (live): %1").arg(QDir::toNativeSeparators(l_oTomsRoot)));
    QString l_oSynced;
    std::string l_strIerr;
    if (stage::f_SyncFloorIndex(m_oP.m_strRoot, &l_strIerr)) l_oSynced = tr("Event index updated (assets/data/events/floor_index.json).");
    f_WatchFiles();
    m_strCur.clear();
    f_OpenFloor(m_oP.f_Floor("F01") ? "F01" : m_oP.m_vFloors.front().m_strId);
    f_SetTool(Paint);
    f_SetBrush("wall");
    f_Toast(tr("Loaded %1 floors from the project folder.").arg(m_oP.m_vFloors.size()) + (l_oSynced.isEmpty() ? QString() : QStringLiteral("  ") + l_oSynced));
    return true;
}

void StageEditor::f_WatchFiles() {
    if (!m_pWatcher) {
        m_pWatcher = new QFileSystemWatcher(this);
        auto l_fnChanged = [this](const QString&) { m_pReloadTimer->start(); };
        connect(m_pWatcher, &QFileSystemWatcher::directoryChanged, this, l_fnChanged);
        connect(m_pWatcher, &QFileSystemWatcher::fileChanged, this, l_fnChanged);
    }
    // the events, NPC files, dialogues and strings the event editor writes (a replaced file drops out: re-added here)
    QStringList l_oPaths;
    const QString l_oData = m_oRoot + QStringLiteral("/assets/data/");
    for (const char* l_pszDir : {"events", "npcs", "dialogue"}) {
        const QDir l_oD(l_oData + QString::fromLatin1(l_pszDir));
        l_oPaths << l_oD.absolutePath();
        for (const QString& l_oF : l_oD.entryList({QStringLiteral("*.json")}, QDir::Files)) l_oPaths << l_oD.absoluteFilePath(l_oF);
    }
    l_oPaths << l_oData + QStringLiteral("text.json");
    l_oPaths.removeAll(l_oData + QStringLiteral("events/floor_index.json"));   // written by both editors' Save: not a reason to reload
    const QStringList l_oHave = m_pWatcher->files() + m_pWatcher->directories();
    QStringList l_oAdd;
    for (const QString& l_oP : l_oPaths) if (!l_oHave.contains(l_oP)) l_oAdd << l_oP;
    if (!l_oAdd.isEmpty()) m_pWatcher->addPaths(l_oAdd);
}

void StageEditor::f_ReloadEvents() {
    std::string l_strErr;
    if (!m_oP.f_ReloadEvents(l_strErr)) { f_Toast(f_Q(l_strErr), true); return; }
    m_oP.f_RefreshCleanFloors();
    f_WatchFiles();
    f_RenderAll();
    f_Toast(tr("Events reloaded from the project folder (the event editor saved)."));
}

bool StageEditor::f_HasUnsaved() const { return m_oP.f_AnyDirty() || m_oP.f_DataDirty(); }   // both cached: cheap

// ---- floors and edits ------------------------------------------------------------------------------------------------

void StageEditor::f_OpenFloor(const std::string& l_strId) {
    if (!m_oP.f_Floor(l_strId)) return;
    const bool l_bChanged = l_strId != m_strCur;
    m_strCur = l_strId;
    m_oV.m_optSel.reset();
    m_oV.m_vFlash.clear();
    m_oV.m_optHover.reset();
    m_oV.m_optMoveFrom.reset();
    m_oV.m_optRectA.reset();
    m_oV.m_optRectB.reset();
    m_oV.m_bPainting = false;
    f_SyncTheme();
    f_RenderAll();
    if (l_bChanged) m_pCanvas->f_FloorChanged();
    f_UpdateTitle();
    Q_EMIT f_DirtyChanged();   // the shell's titles name the floor
}

void StageEditor::f_Step(int l_iDelta) {
    const int l_iN = (int)m_oP.m_vFloors.size();
    if (!l_iN) return;
    const int l_iI = std::max(0, m_oP.f_FloorIndex(m_strCur));
    f_OpenFloor(m_oP.m_vFloors[(size_t)((l_iI + l_iDelta + l_iN) % l_iN)].m_strId);
}

bool StageEditor::f_Edit(const QString& l_oLabel, const std::function<void(stage::StageDoc&)>& l_fnFn) {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return false;
    m_oP.f_BeginEdit(*l_pD);
    l_fnFn(*l_pD);
    if (!m_oP.f_CommitEdit(*l_pD, l_oLabel.toStdString())) return false;
    f_AfterEdit();
    return true;
}

// The open floor's tile theme, for drawing its walls, floor and stairs (an edit, an undo or another floor changes it).
void StageEditor::f_SyncTheme() {
    const stage::StageDoc* l_pD = f_CurDoc();
    m_oP.m_strTheme = l_pD && l_pD->m_jSt.contains("theme") && l_pD->m_jSt["theme"].is_string() ? l_pD->m_jSt["theme"].get<std::string>() : std::string();
}

void StageEditor::f_AfterEdit() {
    f_SyncTheme();
    f_RenderFloorSel();
    f_RenderInspector();
    m_pChecksTimer->start();   // the checks re-run the all-floors story check: just after the map shows the edit
    f_RenderEventPalette();
    f_RenderPlaceBar();
    m_pCanvas->update();
    m_pUndoA->setEnabled(f_CurDoc() && !f_CurDoc()->m_vUndo.empty());
    m_pRedoA->setEnabled(f_CurDoc() && !f_CurDoc()->m_vRedo.empty());
    f_UpdateTitle();
    Q_EMIT f_DirtyChanged();
}

void StageEditor::f_Undo() {
    stage::StageDoc* l_pD = f_CurDoc();
    std::string l_strLabel;
    if (l_pD && m_oP.f_Undo(*l_pD, &l_strLabel)) { f_AfterEdit(); f_Toast(QStringLiteral("↶ ") + f_Q(l_strLabel)); }
}

void StageEditor::f_Redo() {
    stage::StageDoc* l_pD = f_CurDoc();
    std::string l_strLabel;
    if (l_pD && m_oP.f_Redo(*l_pD, &l_strLabel)) { f_AfterEdit(); f_Toast(QStringLiteral("↷ ") + f_Q(l_strLabel)); }
}

void StageEditor::f_TypesChanged() {
    m_oP.m_iDataVer++;   // a type record or a name changed (ObjectWindow): the unsaved check looks again
    f_RenderPalette();
    f_RenderInspector();
    f_UpdateBrushInfo();
    m_pCanvas->update();
    f_UpdateTitle();
    Q_EMIT f_DirtyChanged();
}

// ---- the canvas's mouse --------------------------------------------------------------------------------------------

namespace {
QString f_WhyText(const stage::PlaceCheck& l_oC) {
    if (l_oC.m_eWhy == stage::PlaceCheck::Edge) return StageEditor::tr("Can't place it here: it would stick out of the map.");
    if (l_oC.m_eWhy == stage::PlaceCheck::Wall) return StageEditor::tr("Can't place it here: wall at %1, %2.").arg(l_oC.m_iX).arg(l_oC.m_iY);
    return StageEditor::tr("Can't place it here: %1 is at %2, %3. Erase or move it first.").arg(f_Q(l_oC.m_strWhat)).arg(l_oC.m_iX).arg(l_oC.m_iY);
}
}  // namespace

void StageEditor::f_CanvasPress(const Cell& l_oC) {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return;
    m_oV.m_optSel = l_oC;
    m_oV.m_vFlash.clear();
    const std::string l_strSem = m_oP.f_SemAt(*l_pD, l_oC.m_iX, l_oC.m_iY);
    if (m_oV.m_eTool == Select) {
        if (stage::Project::f_IsEntity(l_strSem)) m_oV.m_optMoveFrom = l_oC;
        f_RenderInspector();
        m_pCanvas->update();
        return;
    }
    if (m_oV.m_eTool == Edit) {   // ✎: the object's own settings
        f_RenderInspector();
        m_pCanvas->update();
        if (stage::Project::f_IsEntity(l_strSem)) ObjectWindow::f_OpenObject(this, l_oC.m_iX, l_oC.m_iY);
        return;
    }
    m_oP.f_BeginEdit(*l_pD);
    m_oV.m_iPaintCount = 0;
    if (m_oV.m_eTool == Rect) {
        m_oV.m_optRectA = m_oV.m_optRectB = l_oC;
        m_pCanvas->update();
        return;
    }
    m_oV.m_bPainting = true;
    f_PaintAt(l_oC);
}

void StageEditor::f_CanvasHover(const std::optional<Cell>& l_optC, bool l_bLeftDown) {
    m_oV.m_optHover = l_optC;
    m_pHover->setText(l_optC ? f_HoverText(*l_optC) : QString());
    if (l_optC && l_bLeftDown && m_oV.m_bPainting) f_PaintAt(*l_optC);
    if (l_optC && m_oV.m_optRectA) m_oV.m_optRectB = *l_optC;
    m_pCanvas->update();
}

void StageEditor::f_CanvasRelease() {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return;
    if (m_oV.m_bPainting) {
        m_oV.m_bPainting = false;
        const QString l_oLabel = m_oV.m_eTool == Erase ? tr("Erase %1 cell(s)").arg(m_oV.m_iPaintCount)
                                                : tr("Paint %1 (%2 cells)").arg(f_Q(m_oP.f_NameOf(m_oV.m_strBrush))).arg(m_oV.m_iPaintCount);
        if (m_oP.f_CommitEdit(*l_pD, l_oLabel.toStdString())) f_AfterEdit();
    }
    if (m_oV.m_optRectA) {
        const int l_iX0 = std::min(m_oV.m_optRectA->m_iX, m_oV.m_optRectB->m_iX), l_iY0 = std::min(m_oV.m_optRectA->m_iY, m_oV.m_optRectB->m_iY);
        const int l_iX1 = std::max(m_oV.m_optRectA->m_iX, m_oV.m_optRectB->m_iX), l_iY1 = std::max(m_oV.m_optRectA->m_iY, m_oV.m_optRectB->m_iY);
        m_oV.m_optRectA.reset();
        m_oV.m_optRectB.reset();
        int l_iBlocked = 0;   // objects only go on free floor; cells this rectangle already filled count as taken
        bool l_bListed = false;
        for (int l_iY = l_iY0; l_iY <= l_iY1; l_iY++)
            for (int l_iX = l_iX0; l_iX <= l_iX1; l_iX++) {
                if (!m_oP.f_PlaceCheck(*l_pD, l_iX, l_iY, m_oV.m_strBrush).m_bOk) { l_iBlocked++; continue; }
                if (m_oP.f_PutSem(*l_pD, l_iX, l_iY, m_oV.m_strBrush, &l_bListed)) m_oV.m_iPaintCount++;
            }
        if (m_oP.f_CommitEdit(*l_pD, tr("Paint %1 (%2 cells)").arg(f_Q(m_oP.f_NameOf(m_oV.m_strBrush))).arg(m_oV.m_iPaintCount).toStdString())) f_AfterEdit();
        if (l_iBlocked) f_Toast(tr("%1 cell(s) skipped: a wall or another object is in the way.").arg(l_iBlocked), true);
        if (l_bListed) f_Toast(tr("%1 was added to this floor's events list (it can roll it now).").arg(f_Q(m_oV.m_strBrush.substr(6))));
        m_pCanvas->update();
    }
    if (m_oV.m_optMoveFrom) {
        const Cell l_oFrom = *m_oV.m_optMoveFrom;
        m_oV.m_optMoveFrom.reset();
        if (m_oV.m_optHover && *m_oV.m_optHover != l_oFrom) {
            const Cell l_oTo = *m_oV.m_optHover;
            const std::string l_strSem = m_oP.f_SemAt(*l_pD, l_oFrom.m_iX, l_oFrom.m_iY);
            const stage::PlaceCheck l_oChk = m_oP.f_PlaceCheck(*l_pD, l_oTo.m_iX, l_oTo.m_iY, l_strSem, &l_oFrom);
            if (!l_oChk.m_bOk) f_Toast(f_WhyText(l_oChk), true);
            else {
                m_oV.m_optSel = l_oTo;   // before the edit: afterEdit shows it in the inspector (built once)
                f_Edit(tr("Move %1").arg(f_Q(m_oP.f_NameOf(l_strSem))), [&](stage::StageDoc& l_oDd) {
                    const char l_cCh = l_oDd.m_vGrid[(size_t)l_oFrom.m_iY][(size_t)l_oFrom.m_iX];
                    l_oDd.m_vGrid[(size_t)l_oFrom.m_iY][(size_t)l_oFrom.m_iX] = '.';
                    l_oDd.m_vGrid[(size_t)l_oTo.m_iY][(size_t)l_oTo.m_iX] = l_cCh;
                    if (const Json* l_pO = stage::Project::f_ObjOf(l_oDd, l_oFrom.m_iX, l_oFrom.m_iY)) {   // its settings move with it
                        const Json l_jKeep = *l_pO;
                        stage::Project::f_DropObj(l_oDd, l_oFrom.m_iX, l_oFrom.m_iY);
                        l_oDd.m_jSt["objects"][std::to_string(l_oTo.m_iX) + "," + std::to_string(l_oTo.m_iY)] = l_jKeep;
                    }
                });
            }
        }
        m_pCanvas->update();
    }
}

void StageEditor::f_PaintAt(const Cell& l_oC) {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return;
    if (m_oV.m_eTool == Erase) {
        if (l_pD->f_At(l_oC.m_iX, l_oC.m_iY) != '.') {
            l_pD->m_vGrid[(size_t)l_oC.m_iY][(size_t)l_oC.m_iX] = '.';
            stage::Project::f_DropObj(*l_pD, l_oC.m_iX, l_oC.m_iY);
            m_oV.m_iPaintCount++;
        }
    } else {
        const stage::PlaceCheck l_oChk = m_oP.f_PlaceCheck(*l_pD, l_oC.m_iX, l_oC.m_iY, m_oV.m_strBrush);
        if (!l_oChk.m_bOk) { f_Toast(f_WhyText(l_oChk), true); m_pCanvas->update(); return; }
        bool l_bListed = false;
        if (m_oP.f_PutSem(*l_pD, l_oC.m_iX, l_oC.m_iY, m_oV.m_strBrush, &l_bListed)) m_oV.m_iPaintCount++;
        if (l_bListed) f_Toast(tr("%1 was added to this floor's events list (it can roll it now).").arg(f_Q(m_oV.m_strBrush.substr(6))));
    }
    if (m_oV.m_strBrush == "player_start" && m_oV.m_eTool == Paint) m_oV.m_bPainting = false;   // one start only
    if (!m_oV.m_bPainting && m_oV.m_eTool == Paint && m_oP.f_CommitEdit(*l_pD, tr("Paint %1 (%2 cells)").arg(f_Q(m_oP.f_NameOf(m_oV.m_strBrush))).arg(m_oV.m_iPaintCount).toStdString()))
        f_AfterEdit();
    m_pCanvas->update();
}

void StageEditor::f_Eyedropper(const Cell& l_oC) {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return;
    const std::string l_strSem = m_oP.f_SemAt(*l_pD, l_oC.m_iX, l_oC.m_iY);
    if (!l_strSem.empty()) f_SetBrush(l_strSem);
}

void StageEditor::f_PlaceHint(const stage::PlaceCheck& l_oChk) {
    if (m_oV.m_bPainting) return;
    if (!l_oChk.m_bOk) m_pStatus->setText(QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(f_Esc(f_WhyText(l_oChk))));
}

QString StageEditor::f_HoverText(const Cell& l_oC) {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return QString();
    const std::string l_strSem = m_oP.f_SemAt(*l_pD, l_oC.m_iX, l_oC.m_iY);
    QString l_oWhat = l_strSem.empty() ? QStringLiteral("?") : f_Q(m_oP.f_NameOf(l_strSem));
    if (f_StartsWith(l_strSem, "event:")) {
        const stage::Event* l_pE = m_oP.f_Event(l_strSem.substr(6));
        l_oWhat += l_pE ? QStringLiteral(" · %1 · %2").arg(f_Q(l_pE->m_strKind), f_Q(m_oP.f_Localized(Json(l_pE->m_strText)))) : QStringLiteral(" · ⛔ ") + tr("not defined in any pool");
    }
    if (f_StartsWith(l_strSem, "monster:"))
        if (const Json* l_pM = m_oP.f_TypeRec(l_strSem))
            l_oWhat += QStringLiteral(" · %1 %2 %3 %4 %5 %6").arg(tr("HP")).arg(l_pM->value("hp", 0)).arg(tr("ATK")).arg(l_pM->value("atk", 0)).arg(tr("DEF")).arg(l_pM->value("def", 0));
    return QStringLiteral("(%1, %2)  '%3'  %4").arg(l_oC.m_iX).arg(l_oC.m_iY).arg(QChar(l_pD->f_At(l_oC.m_iX, l_oC.m_iY))).arg(l_oWhat);
}

void StageEditor::f_FlashCells(const std::vector<Cell>& l_vCells) {
    m_oV.m_vFlash = l_vCells;
    if (!l_vCells.empty()) m_pCanvas->f_ShowCell(l_vCells[0]);
    m_pCanvas->update();
    m_pFlashTimer->start(1600);
}

void StageEditor::f_ShowEventOnFloor(const std::string& l_strFloor, const std::string& l_strEventId) {
    f_OpenFloor(l_strFloor);
    if (l_strEventId.empty()) return;
    if (const auto l_optC = m_oP.f_Find(*f_CurDoc(), "event:" + l_strEventId)) {
        m_oV.m_optSel = *l_optC;
        f_FlashCells({*l_optC});
        f_RenderInspector();
    }
}

void StageEditor::f_SelectCell(const Cell& l_oC) {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD || !l_pD->f_Inside(l_oC.m_iX, l_oC.m_iY)) return;
    m_oV.m_optSel = l_oC;
    f_RenderInspector();
    m_pCanvas->f_ShowCell(l_oC);
}

void StageEditor::f_EnterPlaceMode(const std::string& l_strId, const std::string& l_strKind, const std::vector<std::string>& l_vFloors) {
    if (l_strId.empty()) return;
    if (!m_oP.f_Event(l_strId)) m_oP.f_AddPendingEvent(l_strId, l_strKind);
    PlaceMode l_oPm;
    l_oPm.m_strId = l_strId;
    for (const std::string& l_strF : l_vFloors) if (m_oP.f_Floor(l_strF)) l_oPm.m_vFloors.push_back(l_strF);
    if (l_oPm.m_vFloors.empty()) l_oPm.m_vFloors.push_back(m_strCur);
    m_oV.m_optPlaceMode = l_oPm;
    f_OpenFloor(l_oPm.m_vFloors[0]);
    f_SetTool(Paint);
    f_SetBrush("event:" + l_strId);
    f_RenderPlaceBar();
}

void StageEditor::f_SetTool(Tool l_eT) {
    m_oV.m_eTool = l_eT;
    f_UpdateToolButtons();
    m_pCanvas->update();
}

void StageEditor::f_SetBrush(const std::string& l_strSem) {
    m_oV.m_strBrush = l_strSem;
    if (m_oV.m_eTool == Erase || m_oV.m_eTool == Select) f_SetTool(Paint);
    f_UpdateBrushInfo();
    f_RenderPalette();
    f_RenderEventPalette();
}

void StageEditor::f_Toast(const QString& l_oMsg, bool l_bBad) {
    m_pStatus->setText(l_bBad ? QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(f_Esc(l_oMsg)) : f_Esc(l_oMsg));
    statusBar()->showMessage(QString());
    static QTimer* l_pT = nullptr;
    if (!l_pT) {
        l_pT = new QTimer(this);
        l_pT->setSingleShot(true);
        connect(l_pT, &QTimer::timeout, this, [this] { m_pStatus->clear(); });
    }
    l_pT->start(6000);
}

// ---- rendering the panels --------------------------------------------------------------------------------------------

void StageEditor::f_RetranslateDynamic() {
    m_oStoryKey.clear();
    m_oP.m_strLang = StageText::f_Lang();
    f_RenderAll();
}

void StageEditor::f_RenderAll() {
    if (m_strCur.empty()) return;
    f_RenderFloorSel();
    f_RenderPalette();
    f_RenderEventPalette();
    f_RenderInspector();
    f_RenderProblems();
    f_RenderPlaceBar();
    f_UpdateBrushInfo();
    f_UpdateToolButtons();
    m_pUndoA->setEnabled(!f_CurDoc()->m_vUndo.empty());
    m_pRedoA->setEnabled(!f_CurDoc()->m_vRedo.empty());
    m_pCanvas->update();
}

void StageEditor::f_UpdateTitle() {
    const bool l_bAny = f_HasUnsaved();
    m_pSaveA->setText(tr("Save") + (l_bAny ? QStringLiteral(" •") : QString()));
    setWindowTitle(tr("TOMS Stage Editor") + QStringLiteral(" — ") + f_Q(m_strCur) + (m_oP.f_IsDirty(m_strCur) ? QStringLiteral(" ●") : QString()));
}

void StageEditor::f_UpdateToolButtons() {
    for (int l_iI = 0; l_iI < 5; l_iI++) if (m_arrToolA[l_iI]) m_arrToolA[l_iI]->setChecked(l_iI == m_oV.m_eTool);
}

void StageEditor::f_UpdateBrushInfo() {
    m_pBrushIcon->setPixmap(QPixmap::fromImage(StageDraw::f_Icon(m_oP, m_oBank, m_oV.m_strBrush, 24)));
    QString l_oName = f_Q(m_oP.f_NameOf(m_oV.m_strBrush));
    if (f_StartsWith(m_oV.m_strBrush, "event:"))
        if (const stage::Event* l_pE = m_oP.f_Event(m_oV.m_strBrush.substr(6))) l_oName += QStringLiteral(" · ") + f_Q(l_pE->m_strKind);
    m_pBrushName->setText(l_oName);
}

void StageEditor::f_RenderFloorSel() {
    m_pFloorSel->blockSignals(true);
    m_pFloorSel->clear();
    for (const stage::Floor& l_oF : m_oP.m_vFloors) {
        const std::string l_strAct = l_oF.m_jSpec.value("act", std::string());
        const bool l_bHand = l_oF.m_jSpec.contains("handAuthoredStage") && l_oF.m_jSpec["handAuthoredStage"].is_string();
        m_pFloorSel->addItem((m_oP.f_IsDirty(l_oF.m_strId) ? QStringLiteral("● ") : QString()) + f_Q(l_oF.m_strId) + QStringLiteral(" · ") + f_Q(l_strAct)
                                + (l_bHand ? QStringLiteral(" · ★") : QString()), f_Q(l_oF.m_strId));
        if (l_oF.m_strId == m_strCur) m_pFloorSel->setCurrentIndex(m_pFloorSel->count() - 1);
    }
    m_pFloorSel->blockSignals(false);
    f_UpdateTitle();
}

void StageEditor::f_RenderPalette() {
    stage::StageDoc* l_pD = f_CurDoc();
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->setContentsMargins(6, 6, 6, 6);
    l_pV->setSpacing(2);
    std::vector<std::string> l_vItems, l_vStory;
    for (const std::string& l_strK : m_oP.f_ItemIds()) {
        const std::string l_strT = m_oP.f_ItemType(l_strK);
        const stage::Json* l_pRec = m_oP.f_TypeRec("item:" + l_strK);
        const bool l_bGateKey = l_strT == "story" && l_pRec && l_pRec->contains("opens");
        if ((l_strT == "consumable" && !f_StartsWith(l_strK, "key_")) || (l_strT == "story" && !l_bGateKey)) l_vItems.push_back(l_strK);   // event items too
        if (l_bGateKey) l_vStory.push_back(l_strK);   // a key that opens its own gate
    }
    auto l_fnPrefixed = [](const char* l_pszPre, const std::vector<std::string>& l_vIds) {
        std::vector<std::string> l_vOut;
        for (const std::string& l_strId : l_vIds) l_vOut.push_back(l_pszPre + l_strId);
        return l_vOut;
    };
    // Doors: the three coloured doors, then every door that takes one key of its own (a gate, "gate:<item id>"); Keys:
    // the three keys, then those own keys (story items, which can be put on the map too).
    auto l_fnJoin = [](std::vector<std::string> l_vA, const std::vector<std::string>& l_vB) { l_vA.insert(l_vA.end(), l_vB.begin(), l_vB.end()); return l_vA; };
    const std::vector<std::pair<QString, std::vector<std::string>>> l_vBuiltin = {
        {tr("Terrain"), {"wall", "floor"}}, {tr("Stairs"), {"stairs_up", "stairs_down"}}, {tr("Player"), {"player_start"}},
        {tr("Doors"), l_fnJoin({"door:yellow", "door:blue", "door:red"}, l_fnPrefixed("gate:", l_vStory))},
        {tr("Keys"), l_fnJoin({"key:yellow", "key:blue", "key:red"}, l_fnPrefixed("item:", l_vStory))},
        {tr("Monsters"), l_fnPrefixed("monster:", m_oP.f_EnemyIds())}, {tr("NPCs"), l_fnPrefixed("npc:", m_oP.f_NpcIds())},
        {tr("Items"), l_fnPrefixed("item:", l_vItems)}};
    // Every group has a +. A door with its own key (a "story" item) is how a new kind of door is made: its gate is the
    // door only that key opens. Terrain, stairs and the player's start are the game's own: nothing to add there.
    const QString l_oDoorKey = tr("Add a new door and its key (an item only that door takes)");
    const QString l_oBuiltIn = tr("Built into the game: walls, floor, stairs and the start have no data to copy");
    // the + of each group: the category made, what the new one is painted as (its place in this group), the tip
    struct Add { std::string m_strCat, m_strBrush; QString m_oTip; };
    const std::map<QString, Add> l_mapAddable = {
        {tr("Monsters"), {"monster", "monster:", tr("Add a new monster type")}}, {tr("Items"), {"item", "item:", tr("Add a new item type")}},
        {tr("NPCs"), {"npc", "npc:", tr("Add a new NPC")}},
        {tr("Doors"), {"story", "gate:", l_oDoorKey}}, {tr("Keys"), {"story", "item:", l_oDoorKey}},
        {tr("Terrain"), {"", "", l_oBuiltIn}}, {tr("Stairs"), {"", "", l_oBuiltIn}}, {tr("Player"), {"", "", l_oBuiltIn}}};
    // The palette's own categories: a type whose record names one ("group") shows there, not in its usual group.
    struct Group { QString m_oTitle; std::vector<std::string> m_vList; std::string m_strCustom; };
    std::vector<Group> l_vGroups;
    std::vector<std::string> l_vCustomNames = m_oP.f_PaletteGroups();
    std::map<std::string, std::vector<std::string>> l_mapInCustom;
    for (const auto& [l_oTitle, l_vList] : l_vBuiltin) {
        Group l_oG{l_oTitle, {}, std::string()};
        for (const std::string& l_strSem : l_vList) {
            const std::string l_strGrp = m_oP.f_GroupOf(l_strSem);
            if (l_strGrp.empty()) { l_oG.m_vList.push_back(l_strSem); continue; }
            l_mapInCustom[l_strGrp].push_back(l_strSem);
            if (std::find(l_vCustomNames.begin(), l_vCustomNames.end(), l_strGrp) == l_vCustomNames.end()) l_vCustomNames.push_back(l_strGrp);
        }
        l_vGroups.push_back(std::move(l_oG));
    }
    for (const std::string& l_strN : l_vCustomNames) l_vGroups.push_back({f_Q(l_strN), l_mapInCustom[l_strN], l_strN});
    // + New category, above the groups
    auto* l_pNewCat = new QPushButton(tr("+ New category"));
    l_pNewCat->setToolTip(tr("A category of your own: its + makes types that go into it; right-click a type to move it there"));
    connect(l_pNewCat, &QPushButton::clicked, this, &StageEditor::f_NewCategoryDialog);
    l_pV->addWidget(l_pNewCat);
    for (const auto& [l_oTitle, l_vList, l_strCustom] : l_vGroups) {
        const bool l_bCollapsed = m_setCollapsed.count((l_strCustom.empty() ? "" : "custom:") + l_oTitle.toStdString()) > 0;
        auto* l_pHead = new QHBoxLayout();
        auto* l_pH = new QToolButton;
        l_pH->setText((l_bCollapsed ? QStringLiteral("▸ ") : QStringLiteral("▾ ")) + l_oTitle);
        l_pH->setAutoRaise(true);
        l_pH->setStyleSheet(QStringLiteral("font-weight:bold; color:#8b929c"));
        const std::string l_strKey = (l_strCustom.empty() ? "" : "custom:") + l_oTitle.toStdString();
        if (!l_strCustom.empty()) l_pH->setStyleSheet(QStringLiteral("font-weight:bold; color:#c9a8ff"));   // one of yours
        connect(l_pH, &QToolButton::clicked, this, [this, l_strKey] {
            if (m_setCollapsed.count(l_strKey)) m_setCollapsed.erase(l_strKey); else m_setCollapsed.insert(l_strKey);
            QTimer::singleShot(0, this, &StageEditor::f_RenderPalette);
        });
        l_pHead->addWidget(l_pH);
        auto l_itAdd = l_mapAddable.find(l_oTitle);
        if (l_itAdd != l_mapAddable.end()) {
            auto* l_pPlus = new QToolButton;
            l_pPlus->setText(QStringLiteral("+"));
            l_pPlus->setToolTip(l_itAdd->second.m_oTip);
            const std::string l_strCat = l_itAdd->second.m_strCat, l_strBrush = l_itAdd->second.m_strBrush;
            l_pPlus->setEnabled(!l_strCat.empty());
            connect(l_pPlus, &QToolButton::clicked, this, [this, l_strCat, l_strBrush] { ObjectWindow::f_OpenNew(this, l_strCat, l_strBrush); });
            l_pHead->addWidget(l_pPlus);
        }
        if (!l_strCustom.empty()) {   // one of yours: + (which kind), rename, delete
            auto* l_pPlus = new QToolButton;
            l_pPlus->setText(QStringLiteral("+"));
            l_pPlus->setToolTip(tr("Add a new type to this category"));
            l_pPlus->setPopupMode(QToolButton::InstantPopup);
            auto* l_pM = new QMenu(l_pPlus);
            const std::pair<QString, std::pair<std::string, std::string>> l_arrKinds[] = {
                {tr("Monster"), {"monster", "monster:"}}, {tr("Item"), {"item", "item:"}}, {tr("NPC"), {"npc", "npc:"}},
                {tr("Door and its key"), {"story", "gate:"}}};
            for (const auto& [l_oLabel, l_pairCk] : l_arrKinds) {
                const std::string l_strCat = l_pairCk.first, l_strBrush = l_pairCk.second;
                l_pM->addAction(l_oLabel, this, [this, l_strCat, l_strBrush, l_strCustom] { ObjectWindow::f_OpenNew(this, l_strCat, l_strBrush, l_strCustom); });
            }
            l_pPlus->setMenu(l_pM);
            l_pHead->addWidget(l_pPlus);
            auto* l_pRen = new QToolButton;
            l_pRen->setText(QStringLiteral("✎"));
            l_pRen->setToolTip(tr("Rename this category"));
            connect(l_pRen, &QToolButton::clicked, this, [this, l_strCustom] { f_RenameCategoryDialog(l_strCustom); });
            l_pHead->addWidget(l_pRen);
            auto* l_pDel = new QToolButton;
            l_pDel->setText(QStringLiteral("✕"));
            l_pDel->setToolTip(tr("Delete this category (its types go back to their usual groups)"));
            connect(l_pDel, &QToolButton::clicked, this, [this, l_strCustom] { f_DeleteCategoryDialog(l_strCustom); });
            l_pHead->addWidget(l_pDel);
        }
        l_pHead->addStretch(1);
        l_pV->addLayout(l_pHead);
        if (l_bCollapsed) continue;
        auto* l_pGrid = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pGrid, 4);
        for (const std::string& l_strSem : l_vList) {
            auto* l_pB = new PaletteButton;
            QImage l_oImg = StageDraw::f_Icon(m_oP, m_oBank, l_strSem, 32);
            if (l_pD) {   // the char this floor uses for it (or its usual one)
                std::string l_strCh;
                for (auto& [l_strC, l_jVal] : l_pD->m_jSt["legend"].items()) if (l_jVal == l_strSem) { l_strCh = l_strC; break; }
                if (l_strCh.empty()) { auto l_itPc = stage::f_PreferredChars().find(l_strSem); if (l_itPc != stage::f_PreferredChars().end()) l_strCh = std::string(1, l_itPc->second); }
                if (!l_strCh.empty()) {
                    QPainter l_oG(&l_oImg);
                    QFont l_oF(QStringLiteral("Consolas"));
                    l_oF.setPixelSize(10);
                    l_oG.setFont(l_oF);
                    l_oG.setPen(Qt::black);
                    l_oG.drawText(QRect(0, 0, 31, 33), Qt::AlignRight | Qt::AlignBottom, f_Q(l_strCh));
                    l_oG.setPen(QColor("#d7dbe0"));
                    l_oG.drawText(QRect(0, 0, 30, 32), Qt::AlignRight | Qt::AlignBottom, f_Q(l_strCh));
                }
            }
            l_pB->setIcon(QIcon(QPixmap::fromImage(l_oImg)));
            l_pB->setIconSize(QSize(32, 32));
            l_pB->setFixedSize(40, 40);
            l_pB->setCheckable(true);
            l_pB->setChecked(l_strSem == m_oV.m_strBrush);
            QString l_oTip = f_Q(m_oP.f_NameOf(l_strSem)) + QStringLiteral(" · ") + f_Q(l_strSem);
            // the record a click shows (a gate's is its key item's; a key is the item key_<colour>)
            const std::string l_strTs = f_StartsWith(l_strSem, "gate:") ? "item:" + l_strSem.substr(5)
                                 : f_StartsWith(l_strSem, "key:") ? "item:key_" + l_strSem.substr(4) : l_strSem;
            const bool l_bHasRecord = (f_StartsWith(l_strTs, "monster:") || f_StartsWith(l_strTs, "item:") || f_StartsWith(l_strTs, "npc:")) && m_oP.f_TypeRec(l_strTs);
            if (l_bHasRecord) l_oTip += QStringLiteral(" · ") + tr("click: paint with it and edit its details");
            l_pB->setToolTip(l_oTip);
            if (l_bHasRecord) {
                l_pB->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(l_pB, &QWidget::customContextMenuRequested, this, [this, l_pB, l_strSem] {
                    QMenu l_oMenu(l_pB);
                    QMenu* l_pTo = l_oMenu.addMenu(tr("Move to category"));
                    const std::string l_strNow = m_oP.f_GroupOf(l_strSem);
                    for (const std::string& l_strN : m_oP.f_PaletteGroups()) {
                        QAction* l_pA = l_pTo->addAction(f_Q(l_strN), this, [this, l_strSem, l_strN] { f_MoveToCategory(l_strSem, l_strN); });
                        l_pA->setCheckable(true);
                        l_pA->setChecked(l_strN == l_strNow);
                    }
                    if (m_oP.f_PaletteGroups().empty()) l_pTo->addAction(tr("(no categories yet: + New category)"))->setEnabled(false);
                    l_pTo->addSeparator();
                    QAction* l_pUsual = l_pTo->addAction(tr("Its usual group"), this, [this, l_strSem] { f_MoveToCategory(l_strSem, std::string()); });
                    l_pUsual->setEnabled(!l_strNow.empty());
                    l_oMenu.addAction(tr("New category with it…"), this, [this, l_strSem] {
                        bool l_bOk = false;
                        const QString l_oName = QInputDialog::getText(this, tr("New category"), tr("Name"), QLineEdit::Normal, QString(), &l_bOk).trimmed();
                        if (l_bOk && f_AddCategory(l_oName.toStdString())) f_MoveToCategory(l_strSem, l_oName.toStdString());
                    });
                    l_oMenu.exec(QCursor::pos());
                });
            }
            connect(l_pB, &QToolButton::clicked, this, [this, l_strSem, l_strTs, l_bHasRecord] {
                f_SetBrush(l_strSem);
                if (l_bHasRecord) ObjectWindow::f_OpenType(this, l_strTs);   // the details window follows the palette
            });
            l_pFlow->addWidget(l_pB);
        }
        l_pV->addWidget(l_pGrid);
    }
    l_pV->addStretch(1);
    const int l_iScroll = m_pPaletteArea->verticalScrollBar() ? m_pPaletteArea->verticalScrollBar()->value() : 0;
    m_pPaletteArea->setWidget(l_pW);
    QTimer::singleShot(0, this, [this, l_iScroll] {
        m_pPaletteArea->verticalScrollBar()->setValue(l_iScroll);
        if (!m_bRevealBrush) return;
        m_bRevealBrush = false;
        for (QToolButton* l_pB : m_pPaletteArea->widget()->findChildren<QToolButton*>())
            if (l_pB->isCheckable() && l_pB->isChecked()) { m_pPaletteArea->ensureWidgetVisible(l_pB, 8, 24); break; }
    });
}

void StageEditor::f_RenderEventPalette() {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return;
    const QString l_oQuery = m_pEvSearch->text().trimmed().toLower();
    std::map<std::string, int> l_mapPlaced = m_oP.f_PlacedEvents(*l_pD);
    auto l_fnMatch = [&](const std::string& l_strId) {
        if (l_oQuery.isEmpty()) return true;
        const stage::Event* l_pE = m_oP.f_Event(l_strId);
        QStringList l_oHay{f_Q(l_strId)};
        if (l_pE) {
            l_oHay << f_Q(l_pE->m_strKind) << f_Q(l_pE->m_strPool);
            const Json l_jT = m_oP.f_TextOf(l_pE->m_strText);
            for (const char* l_pszL : {"zh_TW", "en"}) if (l_jT.contains(l_pszL) && l_jT[l_pszL].is_string()) l_oHay << f_Q(l_jT[l_pszL].get<std::string>());
        }
        for (const QString& l_oH : l_oHay) if (l_oH.toLower().contains(l_oQuery)) return true;
        return false;
    };
    const QIcon l_oPlate = f_PlateIcon();
    auto l_fnRow = [&](QTreeWidgetItem* l_pParent, const std::string& l_strId) {
        const stage::Event* l_pE = m_oP.f_Event(l_strId);
        auto* l_pIt = new QTreeWidgetItem(l_pParent);
        l_pIt->setIcon(0, l_oPlate);
        l_pIt->setText(0, f_Q(l_strId));
        l_pIt->setData(0, Qt::UserRole, f_Q(l_strId));
        l_pIt->setText(1, l_pE ? f_Q(l_pE->m_strKind) : QStringLiteral("⛔"));
        if (l_pE) l_pIt->setForeground(1, StageDraw::f_KindColor(m_oP, l_pE->m_strKind));
        else l_pIt->setForeground(0, QColor("#ff9b9b"));
        if (l_mapPlaced[l_strId]) { l_pIt->setText(2, QStringLiteral("✓ %1").arg(l_mapPlaced[l_strId])); l_pIt->setForeground(2, QColor("#4cd18a")); }
        l_pIt->setToolTip(0, l_pE ? f_Q(m_oP.f_Localized(Json(l_pE->m_strText))) : tr("not defined in any pool"));
        if ("event:" + l_strId == m_oV.m_strBrush) m_pEvTree->setCurrentItem(l_pIt);
    };
    m_pEvTree->setUpdatesEnabled(false);
    m_pEvTree->clear();
    std::vector<std::string> l_vMine;
    if (l_pD->m_jSpec.contains("events") && l_pD->m_jSpec["events"].is_array())
        for (const Json& l_jE : l_pD->m_jSpec["events"]) if (l_jE.is_string()) l_vMine.push_back(l_jE.get<std::string>());
    auto* l_pFloorGroup = new QTreeWidgetItem(m_pEvTree, {tr("This floor can roll") + QStringLiteral(" (%1)").arg(l_vMine.size())});
    for (const std::string& l_strId : l_vMine) if (l_fnMatch(l_strId)) l_fnRow(l_pFloorGroup, l_strId);
    auto* l_pAll = new QTreeWidgetItem(m_pEvTree, {tr("All events")});
    for (const std::string& l_strPool : m_oP.f_Pools()) {
        QTreeWidgetItem* l_pPg = nullptr;
        for (const stage::Event& l_oE : m_oP.m_vEvents) {
            if (l_oE.m_strPool != l_strPool || !l_fnMatch(l_oE.m_strId)) continue;
            if (!l_pPg) l_pPg = new QTreeWidgetItem(l_pAll, {f_Q(l_strPool)});
            l_fnRow(l_pPg, l_oE.m_strId);
        }
    }
    for (QTreeWidgetItem* l_pG : {l_pFloorGroup, l_pAll}) {
        QFont l_oF = l_pG->font(0);
        l_oF.setBold(true);
        l_pG->setFont(0, l_oF);
        l_pG->setFirstColumnSpanned(true);
    }
    l_pFloorGroup->setExpanded(true);
    l_pAll->setExpanded(!l_oQuery.isEmpty() || true);
    for (int l_iI = 0; l_iI < l_pAll->childCount(); l_iI++) { l_pAll->child(l_iI)->setExpanded(!l_oQuery.isEmpty()); l_pAll->child(l_iI)->setFirstColumnSpanned(true); }
    m_pEvTree->setUpdatesEnabled(true);
}

QWidget* StageEditor::f_NpcPanel(const std::string& l_strNpc) {
    auto* l_pBox = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pBox);
    l_pV->setContentsMargins(0, 0, 0, 0);
    const Json* l_pF = m_oP.f_TypeRec("npc:" + l_strNpc);
    l_pV->addWidget(f_Heading(tr("NPC") + QStringLiteral(" · ") + f_Q(m_oP.f_NameOf("npc:" + l_strNpc))));
    if (l_pF) {
        const size_t l_nLines = l_pF->contains("chat") && (*l_pF)["chat"].contains("lines") && (*l_pF)["chat"]["lines"].is_array() ? (*l_pF)["chat"]["lines"].size() : 0;
        l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("%1 chat line(s) when no topic is offered").arg(l_nLines))));
    } else {
        l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#ff9f43'>⚠ %1</span>").arg(tr("No NPC file yet: it plays dialogue/<id>.json").toHtmlEscaped())));
    }
    // its topics (talk events) in the game's menu order, marked by where they can be talked about
    std::vector<std::string> l_vPools = m_oP.f_Pools();
    std::sort(l_vPools.begin(), l_vPools.end());
    std::vector<std::pair<size_t, const stage::Event*>> l_vTopics;
    for (size_t l_nI = 0; l_nI < m_oP.m_vEvents.size(); l_nI++) {
        const stage::Event& l_oE = m_oP.m_vEvents[l_nI];
        const Json* l_pTr2 = l_oE.m_jRec.contains("trigger") ? &l_oE.m_jRec["trigger"] : nullptr;
        if (l_pTr2 && l_pTr2->value("on", std::string()) == "talk" && l_pTr2->value("npc", std::string()) == l_strNpc) l_vTopics.push_back({l_nI, &l_oE});
    }
    auto l_fnOrder = [](const stage::Event* l_pE) { return l_pE->m_jRec.contains("topicOrder") && l_pE->m_jRec["topicOrder"].is_number() ? l_pE->m_jRec["topicOrder"].get<double>() : 1e9; };
    auto l_fnPoolIdx = [&](const std::string& l_strP2) { return std::find(l_vPools.begin(), l_vPools.end(), l_strP2) - l_vPools.begin(); };
    std::stable_sort(l_vTopics.begin(), l_vTopics.end(), [&](const auto& l_oA, const auto& l_oB) {
        if (l_fnOrder(l_oA.second) != l_fnOrder(l_oB.second)) return l_fnOrder(l_oA.second) < l_fnOrder(l_oB.second);
        if (l_fnPoolIdx(l_oA.second->m_strPool) != l_fnPoolIdx(l_oB.second->m_strPool)) return l_fnPoolIdx(l_oA.second->m_strPool) < l_fnPoolIdx(l_oB.second->m_strPool);
        return l_oA.first < l_oB.first;
    });
    std::function<std::string(const Json&)> l_fnOnStageOf = [&](const Json& l_jC) -> std::string {
        if (!l_jC.is_object()) return std::string();
        if (l_jC.value("type", std::string()) == "onStage") return l_jC.value("stageId", std::string());
        if (l_jC.contains("all") && l_jC["all"].is_array())
            for (const Json& l_jX : l_jC["all"]) { const std::string l_strS = l_fnOnStageOf(l_jX); if (!l_strS.empty()) return l_strS; }
        return std::string();
    };
    if (l_vTopics.empty()) l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("No talk events."))));
    for (const auto& [l_u64I, l_pE] : l_vTopics) {
        (void)l_u64I;
        const std::string l_strWhere = l_pE->m_jRec.contains("requires") ? l_fnOnStageOf(l_pE->m_jRec["requires"]) : std::string();
        const bool l_bHere = l_strWhere.empty() || l_strWhere == m_strCur;
        QString l_oInfo = l_strWhere.empty() ? tr("on every floor it stands on") : l_strWhere == m_strCur ? tr("only on this floor") : tr("only on %1").arg(f_Q(l_strWhere));
        if (l_pE->m_jRec.contains("requires")) l_oInfo += QStringLiteral(" · ") + tr("has conditions");
        if (l_pE->m_jRec.contains("afterDialogue")) l_oInfo += QStringLiteral(" · ✓ ") + tr("after-dialogue");
        if (l_pE->m_jRec.contains("once") && l_pE->m_jRec["once"] == false)
            l_oInfo += QStringLiteral(" · ↻") + (l_pE->m_jRec.contains("maxTimes") ? QStringLiteral(" ×%1").arg(f_Q(stage::f_NumText(l_pE->m_jRec["maxTimes"]))) : QString());
        QString l_oTitle = f_Q(m_oP.f_Localized(Json(l_pE->m_strTitle)));
        if (l_oTitle.isEmpty() || l_oTitle == f_Q(l_pE->m_strTitle)) l_oTitle = f_Q(l_pE->m_strId);
        // one wrapping label per topic; ↗ is a link in it (a label beside a button would not wrap)
        auto* l_pL = f_Rich(QStringLiteral("%1 <b>%2</b> <code>%3</code> <a href='open' style='color:#4c9eff; text-decoration:none'>↗</a>"
                                      "<br><span style='color:#8b929c;font-size:11px'>%4</span>")
                           .arg(l_bHere ? QStringLiteral("💬") : QStringLiteral("·"), f_Esc(l_oTitle), f_Esc(l_pE->m_strId), f_Esc(l_oInfo)));
        l_pL->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);
        l_pL->setToolTip(tr("Open in the event editor"));
        if (!l_bHere) l_pL->setStyleSheet(QStringLiteral("color: rgba(215,219,224,128)"));
        const QString l_oId = f_Q(l_pE->m_strId);
        connect(l_pL, &QLabel::linkActivated, this, [this, l_oId] { Q_EMIT f_OpenEventEditor(l_oId); });
        l_pV->addWidget(l_pL);
    }
    auto* l_pEdit = new QPushButton(tr("Edit in the event editor →"));
    connect(l_pEdit, &QPushButton::clicked, this, [this, l_strNpc] { Q_EMIT f_OpenNpcInEventEditor(f_Q(l_strNpc)); });
    l_pV->addWidget(l_pEdit, 0, Qt::AlignLeft);
    return l_pBox;
}

QWidget* StageEditor::f_FloorPanel(stage::StageDoc& l_oD) {
    auto* l_pBox = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pBox);
    l_pV->setContentsMargins(0, 0, 0, 0);
    l_pV->addWidget(f_Heading(tr("Floor")));
    const bool l_bHand = l_oD.m_jSpec.contains("handAuthoredStage") && l_oD.m_jSpec["handAuthoredStage"].is_string();
    l_pV->addWidget(f_Rich(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2 · %3</span><br><span style='color:#8b929c;font-size:11px'>%4</span>")
                          .arg(f_Esc(l_oD.m_pF->m_strId), f_Esc(l_oD.m_jSpec.value("act", std::string())), f_Esc(l_oD.m_jSpec.value("role", std::string())),
                               l_bHand ? f_Esc(tr("Hand-made boss stage (%1).").arg(f_Q(l_oD.m_pF->m_strStagePath)))
                                    : f_Esc(tr("Generated grid: running the generator again would replace hand edits.")))));
    auto* l_pForm = new QFormLayout;
    l_pForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    l_pForm->setRowWrapPolicy(QFormLayout::WrapAllRows);   // labels above: the panel stays narrow
    for (const char* l_pszL : {"zh_TW", "en"}) {
        const std::string l_strLang = l_pszL;
        auto* l_pE = new QLineEdit(l_oD.m_jSt.contains("name") && l_oD.m_jSt["name"].is_object() ? f_Q(l_oD.m_jSt["name"].value(l_strLang, std::string())) : QString());
        connect(l_pE, &QLineEdit::editingFinished, this, [this, l_pE, l_strLang] {
            const std::string l_strVal = l_pE->text().toStdString();
            QTimer::singleShot(0, this, [this, l_strVal, l_strLang] {
                f_Edit(tr("Rename floor"), [&](stage::StageDoc& l_oDd) {
                    if (!l_oDd.m_jSt.contains("name") || !l_oDd.m_jSt["name"].is_object()) l_oDd.m_jSt["name"] = Json::object();
                    l_oDd.m_jSt["name"][l_strLang] = l_strVal;
                });
            });
        });
        l_pForm->addRow(tr("Name") + QStringLiteral(" · ") + f_Q(l_strLang), l_pE);
    }
    {   // the floor's tile theme: its walls, floor and stairs (wall_<theme> ... in the atlas)
        auto* l_pTh = new QComboBox;
        l_pTh->addItem(tr("(default)"), QString());
        QStringList l_oThemes;
        for (const std::string& l_strS : m_oP.m_vSprites)
            if (f_StartsWith(l_strS, "wall_")) l_oThemes << f_Q(l_strS.substr(5));
        l_oThemes.sort();
        for (const QString& l_oT : l_oThemes) l_pTh->addItem(l_oT, l_oT);
        const QString l_oCur = l_oD.m_jSt.contains("theme") && l_oD.m_jSt["theme"].is_string() ? f_Q(l_oD.m_jSt["theme"].get<std::string>()) : QString();
        l_pTh->setCurrentIndex(std::max(0, l_pTh->findData(l_oCur)));
        connect(l_pTh, &QComboBox::currentIndexChanged, this, [this, l_pTh](int) {
            const std::string l_strVal = l_pTh->currentData().toString().toStdString();
            QTimer::singleShot(0, this, [this, l_strVal] {
                f_Edit(tr("Floor theme → %1").arg(l_strVal.empty() ? tr("(default)") : f_Q(l_strVal)), [&](stage::StageDoc& l_oDd) {
                    if (l_strVal.empty()) l_oDd.m_jSt.erase("theme");
                    else l_oDd.m_jSt["theme"] = l_strVal;
                });
                m_oP.m_strTheme = l_strVal;
                f_RenderPalette();
                m_pCanvas->update();
            });
        });
        l_pForm->addRow(tr("Floor theme"), l_pTh);
    }
    for (const char* l_pszK : {"up", "down"}) {
        const std::string l_strKey = l_pszK;
        const Json* l_pCn = l_oD.m_jSt.contains("connect") ? &l_oD.m_jSt["connect"] : nullptr;
        auto* l_pE = new QLineEdit(l_pCn && l_pCn->contains(l_strKey) && (*l_pCn)[l_strKey].is_string() ? f_Q((*l_pCn)[l_strKey].get<std::string>()) : QString());
        connect(l_pE, &QLineEdit::editingFinished, this, [this, l_pE, l_strKey] {
            const std::string l_strVal = l_pE->text().trimmed().toStdString();
            QTimer::singleShot(0, this, [this, l_strVal, l_strKey] {
                f_Edit(tr("connect.%1 → %2").arg(f_Q(l_strKey), l_strVal.empty() ? tr("(none)") : f_Q(l_strVal)), [&](stage::StageDoc& l_oDd) {
                    if (!l_oDd.m_jSt.contains("connect") || !l_oDd.m_jSt["connect"].is_object()) l_oDd.m_jSt["connect"] = Json::object();
                    l_oDd.m_jSt["connect"][l_strKey] = l_strVal.empty() ? Json() : Json(l_strVal);
                });
            });
        });
        l_pForm->addRow(l_strKey == "up" ? tr("Stairs up to") : tr("Stairs down to"), l_pE);
    }
    auto* l_pSize = new QHBoxLayout();
    auto* l_pSw = new QSpinBox;
    auto* l_pSh = new QSpinBox;
    l_pSw->setRange(5, 60);
    l_pSh->setRange(5, 60);
    l_pSw->setValue(l_oD.f_Width());
    l_pSh->setValue(l_oD.f_Height());
    auto* l_pApply = new QPushButton(tr("Apply size"));
    connect(l_pApply, &QPushButton::clicked, this, [this, l_pSw, l_pSh] {
        const int l_iW = l_pSw->value(), l_iH = l_pSh->value();
        f_Edit(tr("Resize to %1 × %2").arg(l_iW).arg(l_iH), [&](stage::StageDoc& l_oDd) {
            const std::vector<std::string> l_vOld = l_oDd.m_vGrid;
            const int l_iOh = (int)l_vOld.size(), l_iOw = l_iOh ? (int)l_vOld[0].size() : 0;
            l_oDd.m_vGrid.assign((size_t)l_iH, std::string((size_t)l_iW, '.'));
            for (int l_iY = 0; l_iY < l_iH; l_iY++)
                for (int l_iX = 0; l_iX < l_iW; l_iX++)
                    l_oDd.m_vGrid[(size_t)l_iY][(size_t)l_iX] = (l_iY == 0 || l_iX == 0 || l_iY == l_iH - 1 || l_iX == l_iW - 1) ? '#'
                        : (l_iY < l_iOh - 1 && l_iX < l_iOw - 1) ? l_vOld[(size_t)l_iY][(size_t)l_iX] : '.';
        });
        m_oV.m_optSel.reset();
        m_pCanvas->f_FloorChanged();
        f_RenderInspector();
    });
    l_pSize->addWidget(l_pSw);
    l_pSize->addWidget(new QLabel(QStringLiteral("×")));
    l_pSize->addWidget(l_pSh);
    l_pSize->addWidget(l_pApply);
    l_pSize->addStretch(1);
    l_pForm->addRow(tr("Size (w × h)"), l_pSize);
    l_pV->addLayout(l_pForm);
    // the events this floor can roll: green = placed; a click paints it
    const std::map<std::string, int> l_mapPlaced = m_oP.f_PlacedEvents(l_oD);
    std::vector<std::string> l_vList;
    if (l_oD.m_jSpec.contains("events") && l_oD.m_jSpec["events"].is_array())
        for (const Json& l_jE : l_oD.m_jSpec["events"]) if (l_jE.is_string()) l_vList.push_back(l_jE.get<std::string>());
    l_pV->addWidget(f_Heading(tr("Events this floor can roll (%1)").arg(l_vList.size())));
    auto* l_pChips = new QWidget;
    auto* l_pFlow = new FlowLayout(l_pChips, 4);
    for (const std::string& l_strId : l_vList) {
        auto* l_pC = new QToolButton;
        l_pC->setText((l_mapPlaced.count(l_strId) ? QStringLiteral("✓ ") : QString()) + f_Q(l_strId));
        l_pC->setStyleSheet(!m_oP.f_Event(l_strId) ? QStringLiteral("color:#ff9b9b") : l_mapPlaced.count(l_strId) ? QStringLiteral("color:#4cd18a") : QString());
        connect(l_pC, &QToolButton::clicked, this, [this, l_strId] { f_SetBrush("event:" + l_strId); });
        l_pFlow->addWidget(l_pC);
    }
    l_pV->addWidget(l_pChips);
    l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#8b929c;font-size:11px'>%1</span>").arg(tr("Green = placed on the grid. Click one to paint it."))));
    return l_pBox;
}

void StageEditor::f_RenderInspector() {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return;
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->setContentsMargins(8, 8, 8, 8);
    l_pV->setSizeConstraint(QLayout::SetNoConstraint);   // the panel's width wins: long rows wrap or shrink
    if (m_oV.m_optSel && l_pD->f_Inside(m_oV.m_optSel->m_iX, m_oV.m_optSel->m_iY)) {
        const Cell l_oC = *m_oV.m_optSel;
        const char l_cCh = l_pD->f_At(l_oC.m_iX, l_oC.m_iY);
        const std::string l_strSem = m_oP.f_SemAt(*l_pD, l_oC.m_iX, l_oC.m_iY);
        l_pV->addWidget(f_Heading(tr("Cell %1, %2").arg(l_oC.m_iX).arg(l_oC.m_iY)));
        l_pV->addWidget(f_Rich(QStringLiteral("<table><tr><td style='color:#8b929c'>%1&nbsp;</td><td>%2</td></tr><tr><td style='color:#8b929c'>%3&nbsp;</td><td><code>%4</code> → <code>%5</code></td></tr></table>")
                              .arg(tr("Object"), l_strSem.empty() ? QStringLiteral("⛔ ?") : f_Esc(m_oP.f_NameOf(l_strSem)), tr("Legend char"),
                                   QString(QChar(l_cCh)).toHtmlEscaped(), l_strSem.empty() ? QStringLiteral("?") : f_Esc(l_strSem))));
        if (f_StartsWith(l_strSem, "monster:")) {
            const Json* l_pM = m_oP.f_TypeRec(l_strSem);
            const stage::Footprint l_oFp = m_oP.f_FootprintOf(*l_pD, l_cCh, l_strSem);
            auto l_fnVal = [&](const char* l_pszK) { return l_pM && l_pM->contains(l_pszK) ? f_Q(stage::f_NumText((*l_pM)[l_pszK])) : QStringLiteral("?"); };
            l_pV->addWidget(f_Rich(QStringLiteral("%1 %2 · %3 %4 · %5 %6 · %7 %8 × %9").arg(tr("HP"), l_fnVal("hp"), tr("ATK"), l_fnVal("atk"), tr("DEF"), l_fnVal("def"), tr("Size"))
                                  .arg(l_oFp.m_iW).arg(l_oFp.m_iH)));
        }
        if (f_StartsWith(l_strSem, "gate:")) {
            const std::string l_strItem = l_strSem.substr(5);
            l_pV->addWidget(f_Rich(QStringLiteral("%1: %2").arg(tr("Opens with"),
                m_oP.f_TypeRec("item:" + l_strItem) ? f_Esc(m_oP.f_NameOf("item:" + l_strItem)) + QStringLiteral(" <code>%1</code>").arg(f_Esc(l_strItem))
                                            : QStringLiteral("⛔ %1 · %2").arg(f_Esc(l_strItem), tr("no such item")))));
        }
        if (f_StartsWith(l_strSem, "item:"))
            if (const Json* l_pIt = m_oP.f_TypeRec(l_strSem))
                l_pV->addWidget(f_Rich(QStringLiteral("%1: <code>%2</code>").arg(tr("Effect"), f_Esc(l_pIt->value("stats", l_pIt->value("effect", Json::object())).dump()))));
        if (f_StartsWith(l_strSem, "event:")) {
            const std::string l_strId = l_strSem.substr(6);
            const stage::Event* l_pE = m_oP.f_Event(l_strId);
            if (l_pE && l_pE->m_bPending) l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("From the event editor; not saved in a pool yet."))));
            if (l_pE) {
                const Json l_jT = m_oP.f_TextOf(l_pE->m_strText);
                l_pV->addWidget(f_Rich(QStringLiteral("%1: <span style='color:%2'>%3</span> · %4: %5").arg(tr("Kind"), StageDraw::f_KindColor(m_oP, l_pE->m_strKind).name(), f_Esc(l_pE->m_strKind), tr("Pool"), f_Esc(l_pE->m_strPool))));
                for (const char* l_pszL : {"zh_TW", "en"}) {
                    auto* l_pTxt = f_Rich(QStringLiteral("<div style='color:#8b929c;font-size:11px'>%1 · %2</div>%3").arg(tr("Text"), QString::fromLatin1(l_pszL),
                        l_jT.contains(l_pszL) && l_jT[l_pszL].is_string() ? f_Esc(l_jT[l_pszL].get<std::string>()).replace(QStringLiteral("\n"), QStringLiteral("<br>")) : QStringLiteral("—")));
                    l_pTxt->setStyleSheet(QStringLiteral("background:#0d0f12; border:1px solid #6b5a3a; border-radius:4px; padding:6px; color:#f3e6c8"));
                    l_pV->addWidget(l_pTxt);
                }
            } else {
                l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#ff9f43'>⛔ %1</span>").arg(tr("not defined in any pool"))));
            }
            std::vector<std::string> l_vRollers;
            for (const stage::Floor& l_oF : m_oP.m_vFloors) {
                const Json& l_jSp = m_oP.f_Doc(l_oF.m_strId).m_jSpec;
                if (l_jSp.contains("events") && l_jSp["events"].is_array() && std::find(l_jSp["events"].begin(), l_jSp["events"].end(), Json(l_strId)) != l_jSp["events"].end())
                    l_vRollers.push_back(l_oF.m_strId);
            }
            l_pV->addWidget(f_Heading(tr("Floors that can roll it (%1)").arg(l_vRollers.size())));
            auto* l_pChips = new QWidget;
            auto* l_pFlow = new FlowLayout(l_pChips, 4);
            for (const std::string& l_strF : l_vRollers) {
                auto* l_pB = new QToolButton;
                l_pB->setText(f_Q(l_strF));
                connect(l_pB, &QToolButton::clicked, this, [this, l_strF] { QTimer::singleShot(0, this, [this, l_strF] { f_OpenFloor(l_strF); }); });
                l_pFlow->addWidget(l_pB);
            }
            l_pV->addWidget(l_pChips);
            auto* l_pRepl = new QComboBox;
            for (const stage::Event& l_oEv : m_oP.m_vEvents) l_pRepl->addItem(f_Q(l_oEv.m_strId));
            l_pRepl->setCurrentText(f_Q(l_strId));
            connect(l_pRepl, &QComboBox::textActivated, this, [this, l_oC](const QString& l_oNid) {
                QTimer::singleShot(0, this, [this, l_oC, l_oNid] {
                    f_Edit(tr("Paint %1 (%2 cells)").arg(l_oNid).arg(1), [&](stage::StageDoc& l_oDd) { m_oP.f_PutSem(l_oDd, l_oC.m_iX, l_oC.m_iY, "event:" + l_oNid.toStdString()); });
                });
            });
            auto* l_pRr = new QHBoxLayout();
            l_pRr->addWidget(new QLabel(tr("Replace with")));
            l_pRr->addWidget(l_pRepl, 1);
            l_pV->addLayout(l_pRr);
            auto* l_pOpen = new QPushButton(tr("Open in the event editor"));
            connect(l_pOpen, &QPushButton::clicked, this, [this, l_strId] { Q_EMIT f_OpenEventEditor(f_Q(l_strId)); });
            l_pV->addWidget(l_pOpen, 0, Qt::AlignLeft);
        }
        if (f_StartsWith(l_strSem, "npc:")) l_pV->addWidget(f_NpcPanel(l_strSem.substr(4)));
        if (stage::Project::f_IsEntity(l_strSem) && l_strSem != "player_start" && !f_StartsWith(l_strSem, "stairs")) {
            const Json* l_pO = stage::Project::f_ObjOf(*l_pD, l_oC.m_iX, l_oC.m_iY);
            QStringList l_oParts;
            if (l_pO && l_pO->contains("look") && (*l_pO)["look"].is_object() && !(*l_pO)["look"].empty()) {
                QStringList l_oKeys;
                for (auto& [l_strK, l_jVal] : (*l_pO)["look"].items()) { (void)l_jVal; l_oKeys << f_Q(l_strK); }
                l_oParts << tr("own look (%1)").arg(l_oKeys.join(QStringLiteral(", ")));
            }
            if (l_pO && l_pO->contains("events") && (*l_pO)["events"].is_array() && !(*l_pO)["events"].empty()) {
                QStringList l_oIds;
                for (const Json& l_jE : (*l_pO)["events"]) l_oIds << f_Q(l_jE.get<std::string>());
                l_oParts << tr("linked: %1").arg(l_oIds.join(QStringLiteral(", ")));
            }
            l_pV->addWidget(f_Heading(tr("This object")));
            l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(f_Esc(l_oParts.isEmpty() ? tr("no settings of its own") : l_oParts.join(QStringLiteral(" · "))))));
            auto* l_pBr = new QHBoxLayout();
            auto* l_pOe = new QPushButton(QStringLiteral("✎ ") + tr("Edit object"));
            connect(l_pOe, &QPushButton::clicked, this, [this, l_oC] { ObjectWindow::f_OpenObject(this, l_oC.m_iX, l_oC.m_iY); });
            l_pBr->addWidget(l_pOe);
            const std::string l_strTs = f_StartsWith(l_strSem, "gate:") ? "item:" + l_strSem.substr(5) : l_strSem;
            if (f_StartsWith(l_strTs, "monster:") || f_StartsWith(l_strTs, "item:") || f_StartsWith(l_strTs, "npc:")) {
                auto* l_pTe = new QPushButton(tr("Type details"));
                connect(l_pTe, &QPushButton::clicked, this, [this, l_strTs] { ObjectWindow::f_OpenType(this, l_strTs); });
                l_pBr->addWidget(l_pTe);
            }
            l_pBr->addStretch(1);
            l_pV->addLayout(l_pBr);
        }
        auto* l_pBr = new QHBoxLayout();
        auto* l_pUb = new QPushButton(tr("Use as brush"));
        connect(l_pUb, &QPushButton::clicked, this, [this, l_strSem] { if (!l_strSem.empty()) f_SetBrush(l_strSem); });
        auto* l_pCc = new QPushButton(tr("Clear cell"));
        connect(l_pCc, &QPushButton::clicked, this, [this, l_oC] {
            QTimer::singleShot(0, this, [this, l_oC] {
                f_Edit(tr("Erase %1 cell(s)").arg(1), [&](stage::StageDoc& l_oDd) { l_oDd.m_vGrid[(size_t)l_oC.m_iY][(size_t)l_oC.m_iX] = '.'; stage::Project::f_DropObj(l_oDd, l_oC.m_iX, l_oC.m_iY); });
            });
        });
        l_pBr->addWidget(l_pUb);
        l_pBr->addWidget(l_pCc);
        l_pBr->addStretch(1);
        l_pV->addLayout(l_pBr);
        auto* l_pLine = new QFrame;
        l_pLine->setFrameShape(QFrame::HLine);
        l_pLine->setStyleSheet(QStringLiteral("color:#3a3f47"));
        l_pV->addWidget(l_pLine);
    }
    l_pV->addWidget(f_FloorPanel(*l_pD));
    l_pV->addStretch(1);
    m_pInspArea->setWidget(l_pW);
}

void StageEditor::f_RenderProblems() {
    stage::StageDoc* l_pD = f_CurDoc();
    if (!l_pD) return;
    const std::vector<stage::StoryRow>& l_vRows = stage::f_StoryCheck(m_oP);
    std::vector<stage::Issue> l_vList = stage::f_Problems(m_oP, *l_pD);
    for (const stage::StoryRow& l_oR : l_vRows) if (l_oR.m_strFid == m_strCur) l_vList.insert(l_vList.end(), l_oR.m_vIssues.begin(), l_oR.m_vIssues.end());
    int l_iNErr = 0, l_iNWarn = 0;
    for (const stage::StoryRow& l_oR : l_vRows)
        for (const stage::Issue& l_oI : l_oR.m_vIssues) {
            if (l_oI.m_eSev == stage::Sev::Error) l_iNErr++;
            else if (l_oI.m_eSev == stage::Sev::Warn) l_iNWarn++;
        }
    int l_iShown = 0;
    m_pChecks->clear();
    for (const stage::Issue& l_oI : l_vList) {
        if (l_oI.m_eSev != stage::Sev::Info) l_iShown++;
        auto* l_pIt = new QTreeWidgetItem(m_pChecks, {f_SevIcon(l_oI.m_eSev) + QStringLiteral("  ") + StageText::f_Issue(l_oI)});
        l_pIt->setForeground(0, l_oI.m_eSev == stage::Sev::Info ? QColor("#8b929c") : QColor("#d7dbe0"));
        l_pIt->setToolTip(0, StageText::f_Issue(l_oI));
        const std::vector<Cell> l_vCells = l_oI.m_vCells;
        const std::string l_strBrush = l_oI.m_strBrush;
        l_pIt->setData(0, Qt::UserRole, QVariant::fromValue((qulonglong)(m_pChecks->topLevelItemCount() - 1)));
        if (!l_oI.m_strFixList.empty()) {
            auto* l_pFix = new QPushButton(tr("Add to list"));
            const std::string l_strId = l_oI.m_strFixList;
            connect(l_pFix, &QPushButton::clicked, this, [this, l_strId] {
                QTimer::singleShot(0, this, [this, l_strId] {
                    f_Edit(tr("Add %1 to the floor's events list").arg(f_Q(l_strId)), [&](stage::StageDoc& l_oDd) {
                        if (!l_oDd.m_jSpec.contains("events") || !l_oDd.m_jSpec["events"].is_array()) l_oDd.m_jSpec["events"] = Json::array();
                        l_oDd.m_jSpec["events"].push_back(l_strId);
                    });
                });
            });
            m_pChecks->setItemWidget(l_pIt, 1, l_pFix);
        }
    }
    if (l_vList.empty()) new QTreeWidgetItem(m_pChecks, {tr("No problems on this floor.")});
    disconnect(m_pChecks, &QTreeWidget::itemClicked, nullptr, nullptr);
    connect(m_pChecks, &QTreeWidget::itemClicked, this, [this, l_vList](QTreeWidgetItem* l_pIt) {
        const int l_iIdx = m_pChecks->indexOfTopLevelItem(l_pIt);
        if (l_iIdx < 0 || l_iIdx >= (int)l_vList.size()) return;
        const stage::Issue& l_oI = l_vList[(size_t)l_iIdx];
        if (!l_oI.m_strBrush.empty()) f_SetBrush(l_oI.m_strBrush);
        if (!l_oI.m_vCells.empty()) m_oV.m_optSel = l_oI.m_vCells[0];
        f_FlashCells(l_oI.m_vCells);
        f_RenderInspector();
    });
    m_pBottomTabs->setTabText(0, tr("Checks (%1)").arg(l_iShown));
    m_pBottomTabs->setTabText(1, tr("Story · all floors (%1)").arg(l_iNErr + l_iNWarn));
    f_RenderStory();
}

void StageEditor::f_RenderStory() {
    const std::vector<stage::StoryRow>& l_vRows = stage::f_StoryCheck(m_oP);
    int l_iNErr = 0, l_iNWarn = 0;
    for (const stage::StoryRow& l_oR : l_vRows)
        for (const stage::Issue& l_oI : l_oR.m_vIssues) { if (l_oI.m_eSev == stage::Sev::Error) l_iNErr++; else if (l_oI.m_eSev == stage::Sev::Warn) l_iNWarn++; }
    QStringList l_oChanged;
    for (const stage::Floor& l_oF : m_oP.m_vFloors) if (m_oP.f_IsDirty(l_oF.m_strId)) l_oChanged << f_Q(l_oF.m_strId);
    // the table (a row of rich labels per floor) is costly to build: only while its tab shows, and a floor switch only
    // moves the highlight
    if (m_pBottomTabs->currentIndex() != 1) return;
    const QString l_oKey = QStringLiteral("%1/%2/").arg(m_oP.m_iStoryVer).arg(m_oP.m_iDataVer) + l_oChanged.join(QLatin1Char(','));
    if (l_oKey == m_oStoryKey) {
        for (int l_iI = 0; l_iI < (int)l_vRows.size(); l_iI++)
            if (l_vRows[(size_t)l_iI].m_strFid == m_strCur) { m_pStory->selectRow(l_iI); m_pStory->scrollToItem(m_pStory->item(l_iI, 0)); }
        return;
    }
    m_oStoryKey = l_oKey;
    m_pStorySum->setText(tr("Story check of all %1 floors.").arg(l_vRows.size()) + QStringLiteral(" ")
                        + (l_oChanged.isEmpty() ? QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("No unsaved changes."))
                                             : QStringLiteral("<b style='color:#e0b44c'>%1</b> %2").arg(tr("● %1 floor(s) changed, not saved yet:").arg(l_oChanged.size()), l_oChanged.join(QStringLiteral(" "))))
                        + QStringLiteral(" &nbsp; <b>%1</b>").arg(tr("%1 blocking · %2 warnings").arg(l_iNErr).arg(l_iNWarn)));
    m_pStory->setUpdatesEnabled(false);
    m_pStory->clearContents();
    m_pStory->setRowCount((int)l_vRows.size());
    m_pStory->setHorizontalHeaderLabels({tr("Floor"), tr("Keys in"), tr("Found"), tr("Doors on the way up"), tr("Keys out"), tr("Events"), tr("Issues")});
    int l_iCurRow = -1;
    for (int l_iI = 0; l_iI < (int)l_vRows.size(); l_iI++) {
        const stage::StoryRow& l_oR = l_vRows[(size_t)l_iI];
        const stage::StageDoc& l_oD = m_oP.f_Doc(l_oR.m_strFid);
        auto* l_pIt = new QTableWidgetItem((m_oP.f_IsDirty(l_oR.m_strFid) ? QStringLiteral("● ") : QString()) + f_Q(l_oR.m_strFid) + QStringLiteral(" ") + f_Q(l_oD.m_jSpec.value("act", std::string())));
        l_pIt->setData(Qt::UserRole, f_Q(l_oR.m_strFid));
        m_pStory->setItem(l_iI, 0, l_pIt);
        stage::Keys l_oFound;
        for (int l_iK : l_oR.m_oEx.m_oF.m_vKeyAt) if (l_iK >= 0) l_oFound.m_arrN[l_iK]++;
        const QString l_oUp = l_oR.m_bTop ? QStringLiteral("<span style='color:#8b929c'>%1</span>").arg(tr("top floor"))
                         : l_oR.m_oEx.m_iRoute >= 0 ? f_KeyChips(l_oR.m_oEx.m_vAll[(size_t)l_oR.m_oEx.m_iRoute].m_oOpened) : QStringLiteral("⛔");
        int l_iPlaced = 0;
        for (const auto& [l_strId, l_iN] : m_oP.f_PlacedEvents(l_oD)) { (void)l_strId; l_iPlaced += l_iN; }
        const size_t l_nListed = l_oD.m_jSpec.contains("events") && l_oD.m_jSpec["events"].is_array() ? l_oD.m_jSpec["events"].size() : 0;
        QStringList l_oIss;
        for (const stage::Issue& l_oIs : l_oR.m_vIssues)
            l_oIss << QStringLiteral("<span style='color:%1'>%2</span> %3").arg(f_SevColor(l_oIs.m_eSev).name(), f_SevIcon(l_oIs.m_eSev), f_Esc(StageText::f_Issue(l_oIs)));
        const QString l_arrCells[6] = {f_KeyChips(l_oR.m_oKeysIn), f_KeyChips(l_oFound), l_oUp, l_oR.m_oEx.m_iCarry >= 0 ? f_KeyChips(l_oR.m_oEx.m_vAll[(size_t)l_oR.m_oEx.m_iCarry].m_oLeft) : QStringLiteral("—"),
                                  QStringLiteral("%1/%2").arg(l_iPlaced).arg(l_nListed), l_oIss.join(QStringLiteral("<br>"))};
        for (int l_iC = 0; l_iC < 6; l_iC++) {
            auto* l_pL = new QLabel(l_arrCells[l_iC]);
            l_pL->setTextFormat(Qt::RichText);
            l_pL->setContentsMargins(4, 0, 4, 0);
            l_pL->setAttribute(Qt::WA_TransparentForMouseEvents);
            if (l_iC == 5) l_pL->setWordWrap(true);
            m_pStory->setCellWidget(l_iI, l_iC + 1, l_pL);
        }
        if (l_oR.m_strFid == m_strCur) l_iCurRow = l_iI;
    }
    m_pStory->resizeRowsToContents();
    m_pStory->resizeColumnsToContents();
    if (l_iCurRow >= 0) { m_pStory->selectRow(l_iCurRow); m_pStory->scrollToItem(m_pStory->item(l_iCurRow, 0)); }
    m_pStory->setUpdatesEnabled(true);
}

void StageEditor::f_RenderPlaceBar() {
    if (!m_oV.m_optPlaceMode || m_strCur.empty()) { m_pPlaceBar->hide(); return; }
    m_pPlaceBar->show();
    const std::string l_strId = m_oV.m_optPlaceMode->m_strId;
    const auto l_optAt = m_oP.f_Find(*f_CurDoc(), "event:" + l_strId);
    m_pPmText->setText(l_optAt ? tr("✓ %1 is at %2, %3. Move it with Select, or place it on the next floor.").arg(f_Q(l_strId)).arg(l_optAt->m_iX).arg(l_optAt->m_iY)
                         : tr("📍 Place %1 on %2: click a cell (green = safe), or let the editor pick.").arg(f_Q(l_strId), f_Q(m_strCur)));
    m_pPmSuggest->setVisible(!l_optAt);
    m_pPmAuto->setVisible(!l_optAt);
    const auto& l_vFl = m_oV.m_optPlaceMode->m_vFloors;
    auto l_itIt = std::find(l_vFl.begin(), l_vFl.end(), m_strCur);
    const bool l_bHasNext = l_itIt != l_vFl.end() && l_itIt + 1 != l_vFl.end();
    m_pPmNext->setText(l_bHasNext ? tr("Next: %1 ▶").arg(f_Q(*(l_itIt + 1))) : tr("Done ✓"));
    m_pPmNext->setVisible(l_optAt || l_bHasNext);
}

// ---- auto-place, the windows, save ------------------------------------------------------------------------------------

void StageEditor::f_OpenAutoPlace() {
    QDialog l_oDlg(this);
    l_oDlg.setWindowTitle(tr("Place events automatically"));
    auto* l_pV = new QVBoxLayout(&l_oDlg);
    auto* l_pScopeThis = new QRadioButton(tr("This floor"));
    auto* l_pScopeAll = new QRadioButton(tr("All floors (one click)"));
    l_pScopeThis->setChecked(true);
    auto* l_pG1 = new QButtonGroup(&l_oDlg);
    l_pG1->addButton(l_pScopeThis);
    l_pG1->addButton(l_pScopeAll);
    auto* l_pModeMissing = new QRadioButton(tr("Only events in the list that are not placed yet"));
    auto* l_pModeAll = new QRadioButton(tr("Move every event to a new random cell"));
    l_pModeMissing->setChecked(true);
    auto* l_pG2 = new QButtonGroup(&l_oDlg);
    l_pG2->addButton(l_pModeMissing);
    l_pG2->addButton(l_pModeAll);
    for (QWidget* l_pW : {static_cast<QWidget*>(l_pScopeThis), static_cast<QWidget*>(l_pScopeAll)}) l_pV->addWidget(l_pW);
    auto* l_pLine = new QFrame;
    l_pLine->setFrameShape(QFrame::HLine);
    l_pV->addWidget(l_pLine);
    l_pV->addWidget(l_pModeMissing);
    l_pV->addWidget(l_pModeAll);
    auto* l_pSeedRow = new QHBoxLayout();
    l_pSeedRow->addWidget(new QLabel(tr("Seed")));
    auto* l_pSeed = new QSpinBox;
    l_pSeed->setRange(0, 99999);
    l_pSeed->setValue(1);
    auto* l_pDice = new QToolButton;
    l_pDice->setText(QStringLiteral("🎲"));
    connect(l_pDice, &QToolButton::clicked, l_pSeed, [l_pSeed] { l_pSeed->setValue((int)QRandomGenerator::global()->bounded(99999)); });
    l_pSeedRow->addWidget(l_pSeed);
    l_pSeedRow->addWidget(l_pDice);
    l_pSeedRow->addStretch(1);
    l_pV->addLayout(l_pSeedRow);
    auto* l_pNote = new QLabel(tr("Events go only on cells the player reaches on the way up without opening optional doors, so a key can never be \"spent\" on an event. They are spread out, never next to stairs, doors or the start, and only events that fire on a tile are placed. The same seed gives the same result; everything stays editable and undoable."));
    l_pNote->setWordWrap(true);
    l_pNote->setStyleSheet(QStringLiteral("color:#8b929c"));
    l_pV->addWidget(l_pNote);
    auto* l_pBb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    l_pBb->addButton(tr("Place"), QDialogButtonBox::AcceptRole);
    connect(l_pBb, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pBb, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_pV->addWidget(l_pBb);
    l_oDlg.resize(460, l_oDlg.sizeHint().height());
    if (l_oDlg.exec() != QDialog::Accepted) return;
    const std::vector<std::string> l_vFids = l_pScopeAll->isChecked() ? stage::f_ChainOrder(m_oP) : std::vector<std::string>{m_strCur};
    const stage::AutoPlaceResult l_oRes = stage::f_AutoPlace(m_oP, l_vFids, l_pModeAll->isChecked() ? "all" : "missing", std::to_string(l_pSeed->value()),
                                                        [](int l_iN) { return tr("Auto-place %1 event(s)").arg(l_iN).toStdString(); });
    f_AfterEdit();
    m_pBottomTabs->setCurrentIndex(1);
    QStringList l_oMsg{tr("Placed %1 event(s) on %2 floor(s).").arg(l_oRes.m_iPlaced).arg(l_oRes.m_iFloors)};
    if (!l_oRes.m_vSkipped.empty()) l_oMsg << tr("Skipped %1: see Story.").arg(l_oRes.m_vSkipped.size());
    for (size_t l_nI = 0; l_nI < l_oRes.m_vSkipped.size() && l_nI < 3; l_nI++) l_oMsg << StageText::f_Issue(l_oRes.m_vSkipped[l_nI]);
    f_Toast(l_oMsg.join(QStringLiteral(" · ")), !l_oRes.m_vSkipped.empty());
}

bool StageEditor::f_Save() {
    const std::vector<stage::ChangedFile> l_vFiles = stage::f_ChangedFiles(m_oP);
    if (l_vFiles.empty()) { f_Toast(tr("Nothing to save.")); return true; }
    QDialog l_oDlg(this);
    l_oDlg.setWindowTitle(tr("Save changes"));
    l_oDlg.resize(900, 620);
    auto* l_pV = new QVBoxLayout(&l_oDlg);
    l_pV->addWidget(f_Rich(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>· %2</span>").arg(tr("Save changes"), tr("%1 file(s) change").arg(l_vFiles.size()))));
    auto* l_pView = new QTextBrowser;
    QString l_oHtml;
    if (l_vFiles.size() > 3) {   // more than three: only which files
        l_oHtml = QStringLiteral("<p>%1</p><ul style='font-family:Consolas'>").arg(tr("%1 files changed (too many to show each change):").arg(l_vFiles.size()));
        for (const stage::ChangedFile& l_oF : l_vFiles) l_oHtml += QStringLiteral("<li>%1</li>").arg(f_Esc(l_oF.m_strPath));
        l_oHtml += QStringLiteral("</ul>");
    } else {
        for (const stage::ChangedFile& l_oF : l_vFiles) {
            l_oHtml += QStringLiteral("<p><b>%1</b>%2</p><pre style='font-family:Consolas; font-size:12px'>").arg(f_Esc(l_oF.m_strPath),
                        l_oF.m_eKind == stage::ChangedFile::Delete ? QStringLiteral(" — ") + tr("deleted") : QString());
            for (const auto& [l_cC, l_strLine] : stage::f_LineDiff(l_oF.m_strBefore, l_oF.m_strText)) {
                const QString l_oL = f_Esc(l_strLine);
                if (l_cC == '+') l_oHtml += QStringLiteral("<span style='color:#7ee2a8; background:#173323'>+ %1</span>\n").arg(l_oL);
                else if (l_cC == '-') l_oHtml += QStringLiteral("<span style='color:#ff9b9b; background:#3a1b1b'>- %1</span>\n").arg(l_oL);
                else l_oHtml += QStringLiteral("<span style='color:#8b929c'>  %1</span>\n").arg(l_oL);
            }
            l_oHtml += QStringLiteral("</pre>");
        }
    }
    l_pView->setHtml(l_oHtml);
    l_pV->addWidget(l_pView, 1);
    auto* l_pNote = new QLabel(tr("Writes into the project folder. Each file is backed up to Build/editor_backups first."));
    l_pNote->setStyleSheet(QStringLiteral("color:#8b929c"));
    l_pV->addWidget(l_pNote);
    auto* l_pBb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* l_pWrite = l_pBb->addButton(tr("Write to project"), QDialogButtonBox::AcceptRole);
    l_pWrite->setDefault(true);
    connect(l_pBb, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pBb, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_pV->addWidget(l_pBb);
    if (l_oDlg.exec() != QDialog::Accepted) return false;
    const std::string l_strStamp = stage::f_TimeStamp();
    std::vector<std::string> l_vWritten;
    std::string l_strErr;
    m_bIgnoreWatch = true;   // this editor's own writes are not a reason to reload
    const bool l_bOk = stage::f_WriteFiles(m_oP, l_strStamp, l_vWritten, l_strErr);
    QStringList l_oPaths;
    for (const std::string& l_strP : l_vWritten) l_oPaths << f_Q(l_strP);
    if (!l_bOk) { f_Toast(f_Q(l_strErr), true); f_RenderAll(); return false; }
    std::string l_strIerr;
    const bool l_bIndexed = stage::f_SyncFloorIndex(m_oP.m_strRoot, &l_strIerr);
    m_oP.m_iStoryVer++;
    f_RenderAll();
    f_UpdateTitle();
    f_Toast(tr("Saved %1 file(s) to the project folder. Backups: Build/editor_backups/%2/").arg(l_vWritten.size()).arg(f_Q(l_strStamp))
          + (l_bIndexed ? QStringLiteral("  ") + tr("Event index updated (assets/data/events/floor_index.json).") : QString()));
    Q_EMIT f_FilesWritten(l_oPaths);
    Q_EMIT f_DirtyChanged();
    return true;
}

void StageEditor::f_OpenObjectWindow(const Cell& l_oC) { ObjectWindow::f_OpenObject(this, l_oC.m_iX, l_oC.m_iY); }
void StageEditor::f_OpenTypeWindow(const std::string& l_strSem) { ObjectWindow::f_OpenType(this, l_strSem); }
void StageEditor::f_OpenNewType(const std::string& l_strCat) { ObjectWindow::f_OpenNew(this, l_strCat); }

// ---- the palette's own categories ---------------------------------------------------------------------------------

bool StageEditor::f_AddCategory(const std::string& l_strName) {
    std::vector<std::string> l_vG = m_oP.f_PaletteGroups();
    if (l_strName.empty() || std::find(l_vG.begin(), l_vG.end(), l_strName) != l_vG.end()) return false;
    l_vG.push_back(l_strName);
    m_oP.f_SetPaletteGroups(l_vG);
    f_TypesChanged();
    return true;
}

bool StageEditor::f_RenameCategory(const std::string& l_strFrom, const std::string& l_strTo) {
    const std::vector<std::string> l_vG = m_oP.f_PaletteGroups();
    if (l_strTo.empty() || l_strFrom == l_strTo || std::find(l_vG.begin(), l_vG.end(), l_strTo) != l_vG.end()) return false;
    m_setCollapsed.erase("custom:" + l_strFrom);
    m_oP.f_RenameGroup(l_strFrom, l_strTo);
    f_TypesChanged();
    return true;
}

void StageEditor::f_DeleteCategory(const std::string& l_strName) {
    m_oP.f_ClearGroup(l_strName);
    std::vector<std::string> l_vG = m_oP.f_PaletteGroups();
    l_vG.erase(std::remove(l_vG.begin(), l_vG.end(), l_strName), l_vG.end());
    m_oP.f_SetPaletteGroups(l_vG);
    m_setCollapsed.erase("custom:" + l_strName);
    f_TypesChanged();
}

bool StageEditor::f_MoveToCategory(const std::string& l_strSem, const std::string& l_strGroup) {
    if (!m_oP.f_SetGroup(l_strSem, l_strGroup)) return false;
    if (!l_strGroup.empty()) {   // a name not in the list yet (a record's own) joins it
        std::vector<std::string> l_vG = m_oP.f_PaletteGroups();
        if (std::find(l_vG.begin(), l_vG.end(), l_strGroup) == l_vG.end()) { l_vG.push_back(l_strGroup); m_oP.f_SetPaletteGroups(l_vG); }
    }
    f_TypesChanged();
    return true;
}

void StageEditor::f_NewCategoryDialog() {
    bool l_bOk = false;
    const QString l_oName = QInputDialog::getText(this, tr("New category"), tr("Name"), QLineEdit::Normal, QString(), &l_bOk).trimmed();
    if (!l_bOk || l_oName.isEmpty()) return;
    if (!f_AddCategory(l_oName.toStdString())) f_Toast(tr("%1 already exists.").arg(l_oName), true);
    else f_Toast(tr("Category %1 added: its + makes types for it, or right-click a type to move it there.").arg(l_oName));
}

void StageEditor::f_RenameCategoryDialog(const std::string& l_strName) {
    bool l_bOk = false;
    const QString l_oTo = QInputDialog::getText(this, tr("Rename category"), tr("Name"), QLineEdit::Normal, f_Q(l_strName), &l_bOk).trimmed();
    if (!l_bOk || l_oTo.isEmpty() || l_oTo == f_Q(l_strName)) return;
    if (!f_RenameCategory(l_strName, l_oTo.toStdString())) f_Toast(tr("%1 already exists.").arg(l_oTo), true);
}

void StageEditor::f_DeleteCategoryDialog(const std::string& l_strName) {
    int l_iN = 0;
    for (const std::string& l_strId : m_oP.f_EnemyIds()) l_iN += m_oP.f_GroupOf("monster:" + l_strId) == l_strName;
    for (const std::string& l_strId : m_oP.f_ItemIds()) l_iN += m_oP.f_GroupOf("item:" + l_strId) == l_strName;
    for (const std::string& l_strId : m_oP.f_NpcIds()) l_iN += m_oP.f_GroupOf("npc:" + l_strId) == l_strName;
    if (l_iN > 0 && QMessageBox::question(this, tr("Delete category"),
                                       tr("Delete the category %1? Its %2 type(s) go back to their usual groups (nothing is deleted).").arg(f_Q(l_strName)).arg(l_iN))
                     != QMessageBox::Yes)
        return;
    f_DeleteCategory(l_strName);
}

void StageEditor::f_RevealBrush() {
    m_bRevealBrush = true;
    f_RenderPalette();
}

// ---- --selftest ----------------------------------------------------------------------------------------------------

QStringList StageEditor::f_SelfTest() {
    QStringList l_oFail;
    auto l_fnCheck = [&](bool l_bOk, const QString& l_oWhat) { if (!l_bOk) l_oFail << l_oWhat; };
    l_fnCheck(m_oP.m_vFloors.size() == 70, QStringLiteral("70 floors loaded (%1)").arg(m_oP.m_vFloors.size()));
    l_fnCheck(!f_HasUnsaved(), QStringLiteral("nothing to save after loading"));
    f_OpenFloor("F02");
    l_fnCheck(m_strCur == "F02", QStringLiteral("F02 opened"));
    stage::StageDoc& l_oD = *f_CurDoc();
    // paint a wall on a free cell, then undo
    std::optional<Cell> l_optFree;
    for (int l_iY = 1; l_iY < l_oD.f_Height() - 1 && !l_optFree; l_iY++)
        for (int l_iX = 1; l_iX < l_oD.f_Width() - 1; l_iX++) if (l_oD.f_At(l_iX, l_iY) == '.') { l_optFree = Cell{l_iX, l_iY}; break; }
    l_fnCheck(l_optFree.has_value(), QStringLiteral("F02 has a free cell"));
    if (l_optFree) {
        f_SetTool(Paint);
        f_SetBrush("wall");
        f_CanvasPress(*l_optFree);
        f_CanvasRelease();
        l_fnCheck(l_oD.f_At(l_optFree->m_iX, l_optFree->m_iY) == '#', QStringLiteral("painting puts a wall"));
        l_fnCheck(m_oP.f_IsDirty("F02") && f_HasUnsaved(), QStringLiteral("F02 changed after painting"));
        f_Undo();
        l_fnCheck(!m_oP.f_IsDirty("F02"), QStringLiteral("undo restores F02"));
        // an object cannot go on a wall
        f_SetBrush("monster:slime");
        f_CanvasPress(Cell{0, 0});
        f_CanvasRelease();
        l_fnCheck(l_oD.f_At(0, 0) == '#', QStringLiteral("a monster is not painted over a wall"));
    }
    // ✎ Edit on the villager: own sprite and a linked event
    if (const auto l_optV = m_oP.f_Find(l_oD, "npc:villager")) {
        f_SetTool(Edit);
        f_CanvasPress(*l_optV);
        ObjectWindow* l_pW = ObjectWindow::f_Current();
        l_fnCheck(l_pW != nullptr, QStringLiteral("✎ Edit opens the object window"));
        if (l_pW) {
            l_pW->f_SetLookSprite("npc_king");
            l_pW->f_LinkEvent("ev_village_well");
            l_pW->f_Apply();
        }
        const Json* l_pO = stage::Project::f_ObjOf(l_oD, l_optV->m_iX, l_optV->m_iY);
        l_fnCheck(l_pO && l_pO->contains("look") && l_pO->contains("events"), QStringLiteral("the object's own settings are set"));
        bool l_bStageListed = false;
        for (const stage::ChangedFile& l_oF : stage::f_ChangedFiles(m_oP)) l_bStageListed |= l_oF.m_strPath == l_oD.m_pF->m_strStagePath;
        l_fnCheck(l_bStageListed, QStringLiteral("Save would write the stage file"));
        f_Undo();
        l_fnCheck(!m_oP.f_IsDirty("F02"), QStringLiteral("undo removes the object settings"));
    } else {
        l_oFail << QStringLiteral("F02 has no villager");
    }
    // a type's details
    ObjectWindow::f_OpenType(this, "monster:slime");
    l_fnCheck(ObjectWindow::f_Current() != nullptr, QStringLiteral("the slime's details open"));
    if (ObjectWindow::f_Current()) ObjectWindow::f_Current()->close();
    // Reset stage layout brings back a closed panel and a floating one, docked
    if (f_Panels().size() == 4) {
        f_Panels()[0]->hide();
        f_Panels()[1]->setFloating(true);
        f_ResetLayout();
        l_fnCheck(!f_Panels()[0]->isHidden() && !f_Panels()[1]->isFloating() && dockWidgetArea(f_Panels()[0]) == Qt::LeftDockWidgetArea,
              QStringLiteral("reset layout docks every panel back"));
    } else {
        l_oFail << QStringLiteral("the stage editor has %1 panels, not 4").arg(f_Panels().size());
    }
    // every palette group has a + (the game's own terrain, stairs and start: shown, disabled)
    auto l_fnPaletteButtons = [this] { return m_pPaletteArea->widget()->findChildren<QToolButton*>(); };
    {
        int l_iPlus = 0, l_iEnabled = 0;
        for (QToolButton* l_pB : l_fnPaletteButtons())
            if (l_pB->text() == QStringLiteral("+")) { l_iPlus++; l_iEnabled += l_pB->isEnabled() ? 1 : 0; }
        l_fnCheck(l_iPlus == 8 && l_iEnabled == 5, QStringLiteral("every palette group has a + (%1, %2 enabled)").arg(l_iPlus).arg(l_iEnabled));
    }
    // a click on the palette paints with it and shows its details; the window follows the palette and does not block
    auto l_fnClickPalette = [&](const std::string& l_strSem) {
        for (QToolButton* l_pB : l_fnPaletteButtons())
            if (l_pB->toolTip().contains(QStringLiteral(" · ") + f_Q(l_strSem))) { l_pB->click(); return true; }
        return false;
    };
    l_fnCheck(l_fnClickPalette("monster:slime") && m_oV.m_strBrush == "monster:slime", QStringLiteral("a palette click picks the brush"));
    ObjectWindow* l_pDetails = ObjectWindow::f_Current();
    l_fnCheck(l_pDetails && l_pDetails->f_Kind() == ObjectWindow::Type && l_pDetails->f_Sem() == "monster:slime" && !l_pDetails->isModal(),
          QStringLiteral("and opens its details, without blocking the editor"));
    l_fnCheck(l_fnClickPalette("key:yellow") && ObjectWindow::f_Current() == l_pDetails && l_pDetails && l_pDetails->f_Sem() == "item:key_yellow",
          QStringLiteral("the same window follows the next click (a key: its key item)"));
    l_fnCheck(l_fnClickPalette("wall") && ObjectWindow::f_Current() == l_pDetails && l_pDetails && l_pDetails->f_Sem() == "item:key_yellow",
          QStringLiteral("a built-in (wall) leaves the window as it is"));
    // a key's tint (its item record key_blue) is what the palette and the map draw "key:blue" with
    if (Json* l_pKb = m_oP.f_TypeRec("item:key_blue")) {
        const Json l_jBefore = *l_pKb;
        (*l_pKb)["look"]["tint"] = Json::array({0.0, 0.0, 0.5, 1.0});
        l_fnCheck(m_oP.f_TypeRec("key:blue") == l_pKb && m_oP.f_TypeLook("key:blue").contains("tint"), QStringLiteral("a map key uses its item's look"));
        l_fnCheck(!m_oP.f_TypeUses("item:key_blue").empty(), QStringLiteral("key:blue cells count as uses of the item key_blue"));
        *l_pKb = l_jBefore;
    }
    if (ObjectWindow::f_Current()) ObjectWindow::f_Current()->close();
    // the palette's own categories: add one, a type moved in shows there (not in its usual group), rename, delete
    {
        const std::string l_strCat = "測試分類";
        auto l_fnGroupHeads = [&] {
            QStringList l_oOut;
            for (QToolButton* l_pB : l_fnPaletteButtons()) if (l_pB->text().startsWith(QStringLiteral("▾ ")) || l_pB->text().startsWith(QStringLiteral("▸ "))) l_oOut << l_pB->text().mid(2);
            return l_oOut;
        };
        l_fnCheck(f_AddCategory(l_strCat) && !f_AddCategory(l_strCat) && l_fnGroupHeads().contains(f_Q(l_strCat)), QStringLiteral("a new category shows in the palette (once)"));
        l_fnCheck(f_MoveToCategory("monster:slime", l_strCat) && m_oP.f_GroupOf("monster:slime") == l_strCat, QStringLiteral("a type moves into it"));
        int l_iSlimeButtons = 0;
        for (QToolButton* l_pB : l_fnPaletteButtons()) l_iSlimeButtons += l_pB->toolTip().contains(QStringLiteral(" · monster:slime")) ? 1 : 0;
        l_fnCheck(l_iSlimeButtons == 1, QStringLiteral("it shows in the category only, not twice (%1)").arg(l_iSlimeButtons));
        l_fnCheck(f_RenameCategory(l_strCat, "Bosses") && m_oP.f_GroupOf("monster:slime") == "Bosses" && l_fnGroupHeads().contains(QStringLiteral("Bosses")),
              QStringLiteral("rename carries its types"));
        f_DeleteCategory("Bosses");
        l_fnCheck(m_oP.f_GroupOf("monster:slime").empty() && !l_fnGroupHeads().contains(QStringLiteral("Bosses")), QStringLiteral("delete sends them back"));
        l_fnCheck(!f_HasUnsaved(), QStringLiteral("and nothing is left to save"));
    }
    // a new type's id may be Chinese (lowercase letters, CJK, digits, _); spaces and capitals are refused
    ObjectWindow::f_OpenNew(this, "monster");
    if (ObjectWindow* l_pW = ObjectWindow::f_Current()) { l_pW->f_SetNewId("Slime red", std::string()); l_pW->f_Apply(); }
    l_fnCheck(!m_oP.f_TypeRec("monster:Slime red"), QStringLiteral("an id with a space or a capital is refused"));
    if (ObjectWindow* l_pW = ObjectWindow::f_Current(); l_pW && l_pW->f_Kind() == ObjectWindow::New) { l_pW->f_SetNewId("史萊姆_紅", std::string()); l_pW->f_Apply(); }
    const std::string l_strZhId = "史萊姆_紅";
    l_fnCheck(m_oP.f_TypeRec("monster:" + l_strZhId) != nullptr, QStringLiteral("a Traditional Chinese id is accepted"));
    if (ObjectWindow::f_Current()) ObjectWindow::f_Current()->close();
    m_oP.m_mapDataFiles[stage::Project::f_EnemiesPath()].m_jObj.erase(l_strZhId);   // and gone again (nothing to save)
    f_TypesChanged();
    // a new door and its key: an item of type "story", whose gate shows in the palette
    ObjectWindow::f_OpenNew(this, "story");
    if (ObjectWindow* l_pW = ObjectWindow::f_Current()) {
        l_pW->f_SetNewId("selftest_key", std::string());
        l_pW->f_Apply();
    }
    const Json* l_pKeyRec = m_oP.f_TypeRec("item:selftest_key");
    l_fnCheck(l_pKeyRec && l_pKeyRec->value("type", std::string()) == "story", QStringLiteral("the new door's key is a story item"));
    l_fnCheck(m_oV.m_strBrush == "gate:selftest_key", QStringLiteral("the new door is the brush"));
    l_fnCheck(ObjectWindow::f_Current() && ObjectWindow::f_Current()->f_Sem() == "item:selftest_key", QStringLiteral("its details open"));
    bool l_bGateShown = false;
    for (QToolButton* l_pB : l_fnPaletteButtons()) l_bGateShown |= l_pB->toolTip().contains(QStringLiteral("gate:selftest_key"));
    l_fnCheck(l_bGateShown, QStringLiteral("its gate is in the palette"));
    if (ObjectWindow::f_Current()) ObjectWindow::f_Current()->close();
    m_oP.m_mapDataFiles[stage::Project::f_ItemsPath()].m_jObj.erase("selftest_key");   // and gone again (nothing to save)
    m_oP.m_mapTextEdits.erase("item.selftest_key.name");
    m_oP.m_mapTextEdits.erase("item.selftest_key.desc");
    f_TypesChanged();
    // auto-place, undone in one step
    const stage::AutoPlaceResult l_oR = stage::f_AutoPlace(m_oP, {"F02"}, "all", "7", [](int l_iN) { return std::to_string(l_iN); });
    f_AfterEdit();
    l_fnCheck(l_oR.m_iPlaced > 0, QStringLiteral("auto-place places F02's events"));
    f_Undo();
    l_fnCheck(!m_oP.f_IsDirty("F02"), QStringLiteral("auto-place undoes in one step"));
    m_pBottomTabs->setCurrentIndex(1);   // the story table is built when its tab shows
    l_fnCheck(m_pStory->rowCount() == 70, QStringLiteral("the story table has every floor (%1)").arg(m_pStory->rowCount()));
    {   // the placed-event count (one legend look-up per char) matches a cell-by-cell count
        const stage::StageDoc& l_oD2 = m_oP.f_Doc("F02");
        std::map<std::string, int> l_mapSlow;
        for (int l_iY = 0; l_iY < l_oD2.f_Height(); l_iY++)
            for (int l_iX = 0; l_iX < l_oD2.f_Width(); l_iX++)
                if (const std::string l_strS2 = m_oP.f_SemAt(l_oD2, l_iX, l_iY); f_StartsWith(l_strS2, "event:")) l_mapSlow[l_strS2.substr(6)]++;
        l_fnCheck(m_oP.f_PlacedEvents(l_oD2) == l_mapSlow, QStringLiteral("placed events counted the fast way match"));
    }
    l_fnCheck(!f_HasUnsaved(), QStringLiteral("nothing unsaved after the undos"));
    l_fnCheck(m_pChecks->topLevelItemCount() > 0, QStringLiteral("the checks list is filled"));
    f_SetTool(Paint);
    f_SetBrush("wall");
    return l_oFail;
}
