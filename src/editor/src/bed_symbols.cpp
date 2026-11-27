// bed_symbols.cpp -- see bed_symbols.h.
#include "bed_symbols.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bed {

namespace {

constexpr double g_dAreaW = 560.0, g_dAreaH = 340.0, g_dCircleR = 34.0;   // the play's area and a circle (battle.json plays)
constexpr double g_dStepMs = 300.0;                                       // a new point's time after the last (circleCountMs)
constexpr int g_iTickMs = 16;

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_Ss(const QString& l_oS) { return l_oS.toStdString(); }
double f_Round3(double l_dV) { return std::round(l_dV * 1000.0) / 1000.0; }

ojson f_PointsJson(const std::vector<SymPoint>& l_vP) {
    ojson l_jA = ojson::array();
    for (const SymPoint& l_oP : l_vP) l_jA.push_back(ojson::array({f_Round3(l_oP.m_dX), f_Round3(l_oP.m_dY)}));
    return l_jA;
}
ojson f_TimesJson(const std::vector<double>& l_vT) {
    ojson l_jA = ojson::array();
    for (double l_dT : l_vT) l_jA.push_back((int)std::lround(l_dT));
    return l_jA;
}
std::vector<SymPoint> f_PointsFrom(const ojson& l_jA) {
    std::vector<SymPoint> l_vP;
    if (l_jA.is_array())
        for (const auto& l_jP : l_jA)
            if (l_jP.is_array() && l_jP.size() == 2 && l_jP[0].is_number() && l_jP[1].is_number())
                l_vP.push_back({l_jP[0].get<double>(), l_jP[1].get<double>()});
    return l_vP;
}
std::vector<double> f_TimesFrom(const ojson& l_jA, size_t l_nCount) {
    std::vector<double> l_vT;
    if (l_jA.is_array()) for (const auto& l_jT : l_jA) if (l_jT.is_number()) l_vT.push_back(l_jT.get<double>());
    if (l_vT.size() != l_nCount) l_vT = f_TimesByIndex(l_nCount, g_dStepMs * (double)(l_nCount ? l_nCount - 1 : 0));
    return l_vT;
}

}  // namespace

// ---- the canvas: the play's area, the circles, the curve ----

class SymbolCanvas : public QWidget {
public:
    explicit SymbolCanvas(SymbolEditor* l_pEd) : m_pEd(l_pEd) { setMinimumSize(420, 280); setMouseTracking(true); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent*) override { m_iDrag = -1; }

private:
    QRectF f_Area() const;    // the play's area on the widget (560 x 340, kept in shape)
    QRectF f_Inner() const;   // where a circle's middle may be (inside the area's edge by its radius)
    double f_Scale() const { return f_Area().width() / g_dAreaW; }
    QPointF f_ToWidget(const SymPoint& l_oP) const {
        const QRectF l_oR = f_Inner();
        return {l_oR.left() + l_oP.m_dX * l_oR.width(), l_oR.top() + l_oP.m_dY * l_oR.height()};
    }
    SymPoint f_FromWidget(const QPointF& l_oP) const {
        const QRectF l_oR = f_Inner();
        return {std::clamp((l_oP.x() - l_oR.left()) / std::max(1.0, l_oR.width()), 0.0, 1.0),
                std::clamp((l_oP.y() - l_oR.top()) / std::max(1.0, l_oR.height()), 0.0, 1.0)};
    }
    int f_Nearest(const QPointF& l_oP, double l_dMaxPx) const;

    SymbolEditor* m_pEd;
    int m_iDrag = -1;
};

QRectF SymbolCanvas::f_Area() const {
    const double l_dW = width() - 24.0, l_dH = height() - 24.0;
    const double l_dS = std::min(l_dW / g_dAreaW, l_dH / g_dAreaH);
    return QRectF((width() - g_dAreaW * l_dS) * 0.5, (height() - g_dAreaH * l_dS) * 0.5, g_dAreaW * l_dS, g_dAreaH * l_dS);
}

QRectF SymbolCanvas::f_Inner() const {
    const double l_dR = g_dCircleR * f_Scale();
    return f_Area().adjusted(l_dR, l_dR, -l_dR, -l_dR);
}

int SymbolCanvas::f_Nearest(const QPointF& l_oP, double l_dMaxPx) const {
    const std::vector<SymPoint>& l_vP = m_pEd->f_Points();
    int l_iBest = -1;
    double l_dBest = l_dMaxPx;
    for (size_t l_nI = 0; l_nI < l_vP.size(); l_nI++) {
        const QPointF l_oW = f_ToWidget(l_vP[l_nI]);
        const double l_dD = std::hypot(l_oW.x() - l_oP.x(), l_oW.y() - l_oP.y());
        if (l_dD <= l_dBest) { l_dBest = l_dD; l_iBest = (int)l_nI; }
    }
    return l_iBest;
}

void SymbolCanvas::paintEvent(QPaintEvent*) {
    QPainter l_oP(this);
    l_oP.setRenderHint(QPainter::Antialiasing);
    l_oP.fillRect(rect(), QColor(24, 26, 34));
    const QRectF l_oArea = f_Area(), l_oIn = f_Inner();
    l_oP.fillRect(l_oArea, QColor(36, 40, 54));
    l_oP.setPen(QPen(QColor(60, 66, 86), 1));
    for (int l_iK = 1; l_iK < 10; l_iK++) {   // a 10 % grid of the circles' room
        const double l_dX = l_oIn.left() + l_oIn.width() * l_iK / 10.0, l_dY = l_oIn.top() + l_oIn.height() * l_iK / 10.0;
        l_oP.drawLine(QPointF(l_dX, l_oIn.top()), QPointF(l_dX, l_oIn.bottom()));
        l_oP.drawLine(QPointF(l_oIn.left(), l_dY), QPointF(l_oIn.right(), l_dY));
    }
    l_oP.setPen(QPen(QColor(120, 130, 160), 1));
    l_oP.drawRect(l_oArea);
    l_oP.setPen(QPen(QColor(90, 100, 130), 1, Qt::DashLine));
    l_oP.drawRect(l_oIn);

    const bool l_bCurveMode = m_pEd->f_Mode() == SymbolEditor::Mode::Curve;
    const double l_dPlay = m_pEd->f_PlayMs();
    // the curve: its control polygon, its LOD line, its points with their times
    const std::vector<SymPoint>& l_vC = m_pEd->f_Curve();
    if (!l_vC.empty()) {
        l_oP.setPen(QPen(QColor(90, 90, 110), 1, Qt::DotLine));
        for (size_t l_nI = 1; l_nI < l_vC.size(); l_nI++) l_oP.drawLine(f_ToWidget(l_vC[l_nI - 1]), f_ToWidget(l_vC[l_nI]));
        const std::vector<SymPoint> l_vL = f_CurveLod(l_vC, m_pEd->f_Lod());
        QPainterPath l_oPath(f_ToWidget(l_vL[0]));
        for (size_t l_nI = 1; l_nI < l_vL.size(); l_nI++) l_oPath.lineTo(f_ToWidget(l_vL[l_nI]));
        l_oP.setPen(QPen(l_bCurveMode ? QColor(255, 210, 90) : QColor(150, 130, 80), 2));
        l_oP.drawPath(l_oPath);
        for (size_t l_nI = 0; l_nI < l_vC.size(); l_nI++) {
            const QPointF l_oW = f_ToWidget(l_vC[l_nI]);
            const bool l_bSel = l_bCurveMode && (int)l_nI == m_pEd->f_Selected();
            l_oP.setPen(QPen(l_bSel ? Qt::white : QColor(255, 210, 90), l_bSel ? 2 : 1));
            l_oP.setBrush(l_bCurveMode ? QColor(120, 90, 20) : Qt::NoBrush);
            l_oP.drawRect(QRectF(l_oW.x() - 5, l_oW.y() - 5, 10, 10));
            if (l_bCurveMode) {
                l_oP.setPen(QColor(255, 220, 140));
                l_oP.drawText(l_oW + QPointF(8, -6), QStringLiteral("%1 · %2ms").arg(l_nI + 1).arg(std::lround(m_pEd->f_CurveMs()[l_nI])));
            }
        }
        if (l_dPlay >= 0.0 && m_pEd->f_CurveMs().size() == l_vC.size()) {   // the dot running along it
            const SymPoint l_oAt = f_CurveAt(l_vL, f_CurveLodTimes(m_pEd->f_CurveMs(), m_pEd->f_Lod()), l_dPlay);
            l_oP.setPen(Qt::NoPen);
            l_oP.setBrush(QColor(255, 240, 160));
            l_oP.drawEllipse(f_ToWidget(l_oAt), 6, 6);
        }
    }
    // the circles: their numbers, their times; lit as they are shown while playing
    const std::vector<SymPoint>& l_vO = m_pEd->f_Circles();
    const std::vector<double>& l_vMs = m_pEd->f_CircleMs();
    const double l_dR = g_dCircleR * f_Scale();
    QFont l_oFont = font();
    for (size_t l_nI = 0; l_nI < l_vO.size(); l_nI++) {
        const QPointF l_oW = f_ToWidget(l_vO[l_nI]);
        const bool l_bLit = l_dPlay < 0.0 || (l_nI < l_vMs.size() && l_vMs[l_nI] <= l_dPlay);
        const bool l_bSel = !l_bCurveMode && (int)l_nI == m_pEd->f_Selected();
        l_oP.setPen(QPen(l_bSel ? Qt::white : QColor(200, 190, 255), l_bSel ? 3 : 2));
        l_oP.setBrush(l_bLit ? QColor(140, 110, 255, l_bCurveMode ? 80 : 200) : QColor(60, 50, 100, 60));
        l_oP.drawEllipse(l_oW, l_dR, l_dR);
        if (!l_bLit) continue;
        l_oFont.setPointSizeF(std::max(8.0, l_dR * 0.55));
        l_oFont.setBold(true);
        l_oP.setFont(l_oFont);
        l_oP.setPen(Qt::white);
        l_oP.drawText(QRectF(l_oW.x() - l_dR, l_oW.y() - l_dR, 2 * l_dR, 2 * l_dR), Qt::AlignCenter, QString::number(l_nI + 1));
        l_oFont.setPointSizeF(8);
        l_oFont.setBold(false);
        l_oP.setFont(l_oFont);
        l_oP.setPen(QColor(200, 200, 220));
        if (l_nI < l_vMs.size())
            l_oP.drawText(QRectF(l_oW.x() - 40, l_oW.y() + l_dR, 80, 14), Qt::AlignCenter, QStringLiteral("%1 ms").arg(std::lround(l_vMs[l_nI])));
    }
    l_oP.setPen(QColor(170, 170, 190));
    l_oP.drawText(QRectF(8, 4, width() - 16, 18), Qt::AlignLeft,
                  l_bCurveMode ? tr("Curve: click to add a point, drag to move, right-click to delete, Ctrl+click to insert after the selected one")
                               : tr("Circles: click to add, drag to move, right-click to delete, Ctrl+click to insert after the selected one"));
    if (l_dPlay >= 0.0) l_oP.drawText(QRectF(8, height() - 22, 200, 18), Qt::AlignLeft, QStringLiteral("▶ %1 ms").arg(std::lround(l_dPlay)));
}

void SymbolCanvas::mousePressEvent(QMouseEvent* l_pE) {
    std::vector<SymPoint>& l_vP = m_pEd->f_Points();
    std::vector<double>& l_vT = m_pEd->f_Times();
    const int l_iHit = f_Nearest(l_pE->position(), std::max(10.0, (m_pEd->f_Mode() == SymbolEditor::Mode::Circles ? g_dCircleR : 6.0) * f_Scale()));
    if (l_pE->button() == Qt::RightButton) {
        if (l_iHit >= 0) {
            l_vP.erase(l_vP.begin() + l_iHit);
            l_vT.erase(l_vT.begin() + l_iHit);
            m_pEd->f_SetSelected(-1);
            m_pEd->f_Edited();
        }
        return;
    }
    if (l_pE->button() != Qt::LeftButton) return;
    if (l_iHit >= 0 && !(l_pE->modifiers() & Qt::ControlModifier)) {   // pick it up
        m_iDrag = l_iHit;
        m_pEd->f_SetSelected(l_iHit);
        update();
        return;
    }
    const SymPoint l_oNew = f_FromWidget(l_pE->position());
    const int l_iSel = m_pEd->f_Selected();
    if ((l_pE->modifiers() & Qt::ControlModifier) && l_iSel >= 0 && l_iSel < (int)l_vP.size()) {   // in after the selected one
        const double l_dT = l_iSel + 1 < (int)l_vT.size() ? (l_vT[(size_t)l_iSel] + l_vT[(size_t)l_iSel + 1]) * 0.5 : l_vT[(size_t)l_iSel] + g_dStepMs;
        l_vP.insert(l_vP.begin() + l_iSel + 1, l_oNew);
        l_vT.insert(l_vT.begin() + l_iSel + 1, l_dT);
        m_pEd->f_SetSelected(l_iSel + 1);
    } else {
        const double l_dT = m_pEd->f_NextTime();
        l_vP.push_back(l_oNew);
        l_vT.push_back(l_dT);
        m_pEd->f_SetSelected((int)l_vP.size() - 1);
    }
    m_iDrag = m_pEd->f_Selected();
    m_pEd->f_Edited();
}

void SymbolCanvas::mouseMoveEvent(QMouseEvent* l_pE) {
    std::vector<SymPoint>& l_vP = m_pEd->f_Points();
    if (m_iDrag < 0 || m_iDrag >= (int)l_vP.size() || !(l_pE->buttons() & Qt::LeftButton)) return;
    l_vP[(size_t)m_iDrag] = f_FromWidget(l_pE->position());
    m_pEd->f_Edited();
}

// ---- the editor ----

SymbolEditor::SymbolEditor(const QString& l_oDataDir, QWidget* l_pParent) : QWidget(l_pParent) {
    m_oPath = QDir(l_oDataDir).filePath(QStringLiteral("circle_symbols.json"));
    m_pTimer = new QTimer(this);
    m_pTimer->setInterval(g_iTickMs);
    connect(m_pTimer, &QTimer::timeout, this, [this] {
        double l_dEnd = 0.0;
        for (double l_dT : m_vCircleMs) l_dEnd = std::max(l_dEnd, l_dT);
        for (double l_dT : m_vCurveMs) l_dEnd = std::max(l_dEnd, l_dT);
        m_dPlayMs += g_iTickMs;
        if (m_dPlayMs > l_dEnd + 600.0) {
            if (m_pLoop->isChecked()) m_dPlayMs = 0.0;
            else { m_dPlayMs = -1.0; m_pTimer->stop(); m_pPlay->setChecked(false); }
        }
        m_pCanvas->update();
    });
    f_BuildUi();
    f_Load();
}

std::vector<SymPoint>& SymbolEditor::f_Points() { return m_eMode == Mode::Curve ? m_vCurve : m_vCircles; }
std::vector<double>& SymbolEditor::f_Times() { return m_eMode == Mode::Curve ? m_vCurveMs : m_vCircleMs; }
int SymbolEditor::f_Lod() const { return m_pLod ? m_pLod->value() : 3; }
int SymbolEditor::f_SymbolCount() const { return m_jFile.contains("symbols") ? (int)m_jFile["symbols"].size() : 0; }

double SymbolEditor::f_NextTime() const {
    const std::vector<double>& l_vT = m_eMode == Mode::Curve ? m_vCurveMs : m_vCircleMs;
    return l_vT.empty() ? 0.0 : l_vT.back() + g_dStepMs;
}

void SymbolEditor::f_SetSelected(int l_iIndex) {
    m_iSelected = l_iIndex;
    if (m_pTable && l_iIndex >= 0 && l_iIndex < m_pTable->rowCount()) m_pTable->selectRow(l_iIndex);
    m_pCanvas->update();
}

void SymbolEditor::f_Say(const QString& l_oText) {
    if (m_fnSay) m_fnSay(l_oText);
}

void SymbolEditor::f_BuildUi() {
    auto* l_pV = new QVBoxLayout(this);
    // the symbols
    auto* l_pTop = new QHBoxLayout();
    m_pList = new QListWidget();
    m_pList->setMaximumWidth(150);
    auto* l_pSide = new QVBoxLayout();
    l_pSide->addWidget(new QLabel(tr("<b>Symbols</b>")));
    l_pSide->addWidget(m_pList, 1);
    auto* l_pNew = new QPushButton(tr("New"));
    auto* l_pDup = new QPushButton(tr("Duplicate"));
    auto* l_pDel = new QPushButton(tr("Delete"));
    m_pId = new QLineEdit();
    m_pId->setToolTip(tr("Its name (Enter renames it; the level rows follow)."));
    l_pSide->addWidget(m_pId);
    l_pSide->addWidget(l_pNew);
    l_pSide->addWidget(l_pDup);
    l_pSide->addWidget(l_pDel);
    l_pTop->addLayout(l_pSide);

    auto* l_pMid = new QVBoxLayout();
    auto* l_pBar = new QHBoxLayout();
    m_pMode = new QComboBox();
    m_pMode->addItem(tr("edit: circles"));
    m_pMode->addItem(tr("edit: curve"));
    m_pPlay = new QPushButton(tr("▶ Play"));
    m_pPlay->setCheckable(true);
    m_pPlay->setToolTip(tr("The circles light at their times; a dot runs along the curve."));
    m_pLoop = new QCheckBox(tr("loop"));
    l_pBar->addWidget(m_pMode);
    l_pBar->addWidget(m_pPlay);
    l_pBar->addWidget(m_pLoop);
    l_pBar->addStretch(1);
    l_pMid->addLayout(l_pBar);
    m_pCanvas = new SymbolCanvas(this);
    l_pMid->addWidget(m_pCanvas, 1);
    m_pInfo = new QLabel();
    m_pInfo->setWordWrap(true);
    l_pMid->addWidget(m_pInfo);
    l_pTop->addLayout(l_pMid, 1);
    l_pV->addLayout(l_pTop, 3);

    // the points' table and the tools
    auto* l_pBottom = new QHBoxLayout();
    m_pTable = new QTableWidget(0, 3);
    m_pTable->setHorizontalHeaderLabels({tr("x (0..1)"), tr("y (0..1)"), tr("time ms")});
    m_pTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setToolTip(tr("The points in order (the circles' tap order, or the curve's): each place and when it comes."));
    l_pBottom->addWidget(m_pTable, 2);

    auto* l_pTools = new QVBoxLayout();
    auto* l_pTimeBox = new QGroupBox(tr("Times"));
    auto* l_pTf = new QFormLayout(l_pTimeBox);
    m_pTotalMs = new QSpinBox();
    m_pTotalMs->setRange(0, 20000);
    m_pTotalMs->setSingleStep(100);
    m_pTotalMs->setValue(1500);
    m_pTotalMs->setSuffix(QStringLiteral(" ms"));
    auto* l_pByDist = new QPushButton(tr("By distance (even speed)"));
    auto* l_pByIndex = new QPushButton(tr("By index (even steps)"));
    l_pTf->addRow(tr("first to last"), m_pTotalMs);
    l_pTf->addRow(l_pByDist);
    l_pTf->addRow(l_pByIndex);
    l_pTools->addWidget(l_pTimeBox);

    auto* l_pCurveBox = new QGroupBox(tr("Curve"));
    auto* l_pCf = new QFormLayout(l_pCurveBox);
    m_pLod = new QSpinBox();
    m_pLod->setRange(1, 6);
    m_pLod->setValue(3);
    m_pLod->setToolTip(tr("Smoothing: each step cuts every corner (as the FM79979 curve's LOD). 1: straight lines."));
    m_pSampleN = new QSpinBox();
    m_pSampleN->setRange(2, 12);
    m_pSampleN->setValue(6);
    auto* l_pSample = new QPushButton(tr("Circles from the curve"));
    l_pSample->setToolTip(tr("N circles evenly along the curve, each with the curve's time where it sits (replaces the circles)."));
    l_pCf->addRow(tr("LOD"), m_pLod);
    l_pCf->addRow(tr("circles"), m_pSampleN);
    l_pCf->addRow(l_pSample);
    l_pTools->addWidget(l_pCurveBox);

    auto* l_pShapeBox = new QGroupBox(tr("Order and flips"));
    auto* l_pSv = new QVBoxLayout(l_pShapeBox);
    auto* l_pReverse = new QPushButton(tr("Reverse the order"));
    l_pReverse->setToolTip(tr("The points run the other way; the times stay (the same rhythm)."));
    auto* l_pMirror = new QPushButton(tr("Mirror left-right"));
    auto* l_pClear = new QPushButton(tr("Clear"));
    m_pRotate = new QCheckBox(tr("rotate: may start at any point"));
    m_pReverse = new QCheckBox(tr("reverse: may run backwards"));
    m_pMirror = new QCheckBox(tr("mirror: may flip left-right"));
    for (QWidget* l_pW : std::initializer_list<QWidget*>{l_pReverse, l_pMirror, l_pClear, m_pRotate, m_pReverse, m_pMirror}) l_pSv->addWidget(l_pW);
    l_pTools->addWidget(l_pShapeBox);
    l_pBottom->addLayout(l_pTools, 1);
    l_pV->addLayout(l_pBottom, 2);

    // the staff's level rows
    l_pV->addWidget(new QLabel(tr("<b>The staff by level</b> (a row sets what it has; a level takes the rows up to it): "
                                  "chance % a symbol at all, symbols with weights (\"ring5:3, star5:2, A5:1\")")));
    m_pRows = new QTableWidget(0, 3);
    m_pRows->setHorizontalHeaderLabels({tr("lv"), tr("chance %"), tr("symbols")});
    m_pRows->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_pRows->setMaximumHeight(170);
    l_pV->addWidget(m_pRows, 1);
    auto* l_pRowBar = new QHBoxLayout();
    auto* l_pAddRow = new QPushButton(tr("+ Row"));
    auto* l_pDelRow = new QPushButton(tr("− Row"));
    auto* l_pSave = new QPushButton(tr("Save circle_symbols.json"));
    l_pSave->setStyleSheet(QStringLiteral("font-weight: bold;"));
    auto* l_pTry = new QPushButton(tr("Try in the battle"));
    l_pTry->setToolTip(tr("The test battle with the staff, every attack showing this symbol (its circles and times; not saved)."));
    auto* l_pStop = new QPushButton(tr("Stop trying"));
    for (QWidget* l_pW : std::initializer_list<QWidget*>{l_pAddRow, l_pDelRow}) l_pRowBar->addWidget(l_pW);
    l_pRowBar->addStretch(1);
    for (QWidget* l_pW : std::initializer_list<QWidget*>{l_pTry, l_pStop, l_pSave}) l_pRowBar->addWidget(l_pW);
    l_pV->addLayout(l_pRowBar);

    // ---- wiring ----
    connect(m_pList, &QListWidget::currentTextChanged, this, [this](const QString& l_oId) { if (!m_bFilling && !l_oId.isEmpty()) f_SelectSymbol(l_oId); });
    connect(m_pMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int l_iI) {
        m_eMode = l_iI == 1 ? Mode::Curve : Mode::Circles;
        m_iSelected = -1;
        f_FillTable();
        m_pCanvas->update();
    });
    connect(m_pPlay, &QPushButton::toggled, this, [this](bool l_bOn) {
        m_dPlayMs = l_bOn ? 0.0 : -1.0;
        if (l_bOn) m_pTimer->start(); else m_pTimer->stop();
        m_pCanvas->update();
    });
    connect(m_pLod, qOverload<int>(&QSpinBox::valueChanged), this, [this] { f_Commit(); m_pCanvas->update(); });
    for (QCheckBox* l_pC : {m_pRotate, m_pReverse, m_pMirror}) connect(l_pC, &QCheckBox::toggled, this, [this] { if (!m_bFilling) f_Commit(); });
    connect(m_pTable, &QTableWidget::cellChanged, this, [this](int l_iRow, int l_iCol) {
        if (m_bFilling) return;
        std::vector<SymPoint>& l_vP = f_Points();
        std::vector<double>& l_vT = f_Times();
        if (l_iRow < 0 || l_iRow >= (int)l_vP.size()) return;
        bool l_bOk = false;
        const double l_dV = m_pTable->item(l_iRow, l_iCol)->text().toDouble(&l_bOk);
        if (!l_bOk) { f_FillTable(); return; }
        if (l_iCol == 0) l_vP[(size_t)l_iRow].m_dX = std::clamp(l_dV, 0.0, 1.0);
        else if (l_iCol == 1) l_vP[(size_t)l_iRow].m_dY = std::clamp(l_dV, 0.0, 1.0);
        else l_vT[(size_t)l_iRow] = std::max(0.0, l_dV);
        f_Edited();
    });
    connect(m_pTable, &QTableWidget::currentCellChanged, this, [this](int l_iRow) { if (!m_bFilling) { m_iSelected = l_iRow; m_pCanvas->update(); } });
    connect(l_pByDist, &QPushButton::clicked, this, [this] {
        f_Times() = f_TimesByDistance(f_Points(), (double)m_pTotalMs->value());
        f_Edited();
    });
    connect(l_pByIndex, &QPushButton::clicked, this, [this] {
        f_Times() = f_TimesByIndex(f_Points().size(), (double)m_pTotalMs->value());
        f_Edited();
    });
    connect(l_pSample, &QPushButton::clicked, this, [this] {
        if (m_vCurve.size() < 2) { f_Say(tr("Draw a curve first (edit: curve, at least two points).")); return; }
        std::vector<SymPoint> l_vP;
        std::vector<double> l_vT;
        f_CurveSample(f_CurveLod(m_vCurve, f_Lod()), f_CurveLodTimes(m_vCurveMs, f_Lod()), m_pSampleN->value(), l_vP, l_vT);
        m_vCircles = l_vP;
        m_vCircleMs = l_vT;
        f_Commit();
        f_FillTable();
        m_pCanvas->update();
        f_Say(tr("%1 circles along the curve, with its times").arg(m_vCircles.size()));
    });
    connect(l_pReverse, &QPushButton::clicked, this, [this] { std::reverse(f_Points().begin(), f_Points().end()); f_Edited(); });
    connect(l_pMirror, &QPushButton::clicked, this, [this] { for (SymPoint& l_oP : f_Points()) l_oP.m_dX = 1.0 - l_oP.m_dX; f_Edited(); });
    connect(l_pClear, &QPushButton::clicked, this, [this] { f_Points().clear(); f_Times().clear(); m_iSelected = -1; f_Edited(); });
    connect(l_pNew, &QPushButton::clicked, this, [this] {
        f_Commit();
        int l_iN = 1;
        while (m_jFile["symbols"].contains("symbol" + std::to_string(l_iN))) l_iN++;
        const std::string l_strId = "symbol" + std::to_string(l_iN);
        m_jFile["symbols"][l_strId] = ojson::object({{"rotate", false}, {"reverse", true}, {"mirror", false}, {"points", ojson::array()}});
        f_FillList();
        f_SelectSymbol(f_Qs(l_strId));
    });
    connect(l_pDup, &QPushButton::clicked, this, [this] {
        if (m_oCurrent.isEmpty()) return;
        f_Commit();
        std::string l_strId = f_Ss(m_oCurrent) + "_copy";
        while (m_jFile["symbols"].contains(l_strId)) l_strId += "_";
        m_jFile["symbols"][l_strId] = m_jFile["symbols"][f_Ss(m_oCurrent)];
        f_FillList();
        f_SelectSymbol(f_Qs(l_strId));
    });
    connect(l_pDel, &QPushButton::clicked, this, [this] {
        if (m_oCurrent.isEmpty()) return;
        m_jFile["symbols"].erase(f_Ss(m_oCurrent));
        m_oCurrent.clear();
        f_FillList();
        if (m_pList->count()) f_SelectSymbol(m_pList->item(0)->text());
    });
    connect(m_pId, &QLineEdit::editingFinished, this, [this] {
        const std::string l_strNew = f_Ss(m_pId->text().trimmed()), l_strOld = f_Ss(m_oCurrent);
        if (l_strNew.empty() || l_strNew == l_strOld || m_oCurrent.isEmpty()) return;
        if (m_jFile["symbols"].contains(l_strNew)) { f_Say(tr("There is already a symbol \"%1\".").arg(f_Qs(l_strNew))); m_pId->setText(m_oCurrent); return; }
        f_Commit();
        f_ReadRows();
        ojson l_jNew = ojson::object();   // the same place in the list, a new name
        for (auto l_oIt = m_jFile["symbols"].begin(); l_oIt != m_jFile["symbols"].end(); ++l_oIt) l_jNew[l_oIt.key() == l_strOld ? l_strNew : l_oIt.key()] = l_oIt.value();
        m_jFile["symbols"] = l_jNew;
        for (auto& l_jRows : m_jFile["weapons"])   // the level rows follow the name
            for (auto& l_jRow : l_jRows)
                if (l_jRow.contains("symbols") && l_jRow["symbols"].contains(l_strOld)) {
                    ojson l_jW = ojson::object();
                    for (auto l_oIt = l_jRow["symbols"].begin(); l_oIt != l_jRow["symbols"].end(); ++l_oIt) l_jW[l_oIt.key() == l_strOld ? l_strNew : l_oIt.key()] = l_oIt.value();
                    l_jRow["symbols"] = l_jW;
                }
        m_oCurrent = f_Qs(l_strNew);
        f_FillList();
        f_FillRows();
    });
    connect(l_pAddRow, &QPushButton::clicked, this, [this] {
        const int l_iR = m_pRows->rowCount();
        m_pRows->insertRow(l_iR);
        m_pRows->setItem(l_iR, 0, new QTableWidgetItem(QString::number(l_iR ? m_pRows->item(l_iR - 1, 0)->text().toInt() + 2 : 1)));
        for (int l_iC = 1; l_iC < 3; l_iC++) m_pRows->setItem(l_iR, l_iC, new QTableWidgetItem());
    });
    connect(l_pDelRow, &QPushButton::clicked, this, [this] { if (m_pRows->currentRow() >= 0) m_pRows->removeRow(m_pRows->currentRow()); });
    connect(l_pSave, &QPushButton::clicked, this, [this] { f_Save(); });
    connect(l_pTry, &QPushButton::clicked, this, [this] {
        if (m_vCircles.empty()) { f_Say(tr("This symbol has no circles yet.")); return; }
        std::vector<int> l_vMs;
        for (double l_dT : m_vCircleMs) l_vMs.push_back((int)std::lround(l_dT));
        if (m_fnTry) m_fnTry(m_vCircles, l_vMs);
    });
    connect(l_pStop, &QPushButton::clicked, this, [this] { if (m_fnTry) m_fnTry({}, {}); });
}

bool SymbolEditor::f_SelectForTest(const QString& l_oId) {
    if (!m_jFile["symbols"].contains(f_Ss(l_oId))) return false;
    f_SelectSymbol(l_oId);
    return true;
}

bool SymbolEditor::f_Load() {
    QFile l_oF(m_oPath);
    m_jFile = ojson::object();
    if (l_oF.open(QIODevice::ReadOnly)) {
        try { m_jFile = ojson::parse(l_oF.readAll().toStdString()); } catch (...) { m_jFile = ojson::object(); }
    }
    if (!m_jFile.is_object()) m_jFile = ojson::object();
    if (!m_jFile.contains("symbols") || !m_jFile["symbols"].is_object()) m_jFile["symbols"] = ojson::object();
    if (!m_jFile.contains("weapons") || !m_jFile["weapons"].is_object()) m_jFile["weapons"] = ojson::object();
    if (!m_jFile["weapons"].contains("staff")) m_jFile["weapons"]["staff"] = ojson::array();
    f_FillList();
    f_FillRows();
    if (m_pList->count()) f_SelectSymbol(m_pList->item(0)->text());
    return f_SymbolCount() > 0;
}

void SymbolEditor::f_FillList() {
    m_bFilling = true;
    m_pList->clear();
    for (auto l_oIt = m_jFile["symbols"].begin(); l_oIt != m_jFile["symbols"].end(); ++l_oIt)
        if (!l_oIt.key().empty() && l_oIt.key()[0] != '_') m_pList->addItem(f_Qs(l_oIt.key()));
    const auto l_oFound = m_pList->findItems(m_oCurrent, Qt::MatchExactly);
    if (!l_oFound.isEmpty()) m_pList->setCurrentItem(l_oFound.front());
    m_bFilling = false;
}

void SymbolEditor::f_SelectSymbol(const QString& l_oId) {
    if (!m_oCurrent.isEmpty() && m_oCurrent != l_oId) f_Commit();
    m_oCurrent = l_oId;
    const ojson l_jS = m_jFile["symbols"].value(f_Ss(l_oId), ojson::object());
    m_vCircles = f_PointsFrom(l_jS.value("points", ojson::array()));
    m_vCircleMs = f_TimesFrom(l_jS.value("times", ojson::array()), m_vCircles.size());
    const ojson l_jC = l_jS.value("curve", ojson::object());
    m_vCurve = f_PointsFrom(l_jC.value("points", ojson::array()));
    m_vCurveMs = f_TimesFrom(l_jC.value("times", ojson::array()), m_vCurve.size());
    m_bFilling = true;
    m_pLod->setValue(l_jC.value("lod", 3));
    m_pRotate->setChecked(l_jS.value("rotate", false));
    m_pReverse->setChecked(l_jS.value("reverse", false));
    m_pMirror->setChecked(l_jS.value("mirror", false));
    m_pId->setText(l_oId);
    const auto l_oFound = m_pList->findItems(l_oId, Qt::MatchExactly);
    if (!l_oFound.isEmpty()) m_pList->setCurrentItem(l_oFound.front());
    m_bFilling = false;
    m_iSelected = -1;
    f_FillTable();
    m_pCanvas->update();
}

void SymbolEditor::f_Commit() {
    if (m_oCurrent.isEmpty() || m_bFilling) return;
    ojson& l_jS = m_jFile["symbols"][f_Ss(m_oCurrent)];
    if (!l_jS.is_object()) l_jS = ojson::object();
    l_jS["rotate"] = m_pRotate->isChecked();
    l_jS["reverse"] = m_pReverse->isChecked();
    l_jS["mirror"] = m_pMirror->isChecked();
    l_jS["points"] = f_PointsJson(m_vCircles);
    l_jS["times"] = f_TimesJson(m_vCircleMs);
    if (m_vCurve.empty()) l_jS.erase("curve");
    else l_jS["curve"] = ojson::object({{"lod", f_Lod()}, {"points", f_PointsJson(m_vCurve)}, {"times", f_TimesJson(m_vCurveMs)}});
    // the game lights the circles in their tap order: a time before the one ahead of it is a mistake worth seeing
    bool l_bInOrder = true;
    for (size_t l_nI = 1; l_nI < m_vCircleMs.size(); l_nI++) l_bInOrder = l_bInOrder && m_vCircleMs[l_nI] >= m_vCircleMs[l_nI - 1];
    double l_dLast = m_vCircleMs.empty() ? 0.0 : m_vCircleMs.back();
    m_pInfo->setText(tr("%1 circles, the last lit at %2 ms; the curve: %3 points, LOD %4.%5")
                         .arg(m_vCircles.size()).arg(std::lround(l_dLast)).arg(m_vCurve.size()).arg(f_Lod())
                         .arg(l_bInOrder ? QString() : tr("  ⚠ the circles' times are not in their order: a later one lights before an earlier one.")));
}

void SymbolEditor::f_Edited() {
    f_Commit();
    f_FillTable();
    m_pCanvas->update();
}

void SymbolEditor::f_FillTable() {
    m_bFilling = true;
    const std::vector<SymPoint>& l_vP = f_Points();
    const std::vector<double>& l_vT = f_Times();
    m_pTable->setRowCount((int)l_vP.size());
    for (size_t l_nI = 0; l_nI < l_vP.size(); l_nI++) {
        m_pTable->setVerticalHeaderItem((int)l_nI, new QTableWidgetItem(QString::number(l_nI + 1)));
        m_pTable->setItem((int)l_nI, 0, new QTableWidgetItem(QString::number(f_Round3(l_vP[l_nI].m_dX))));
        m_pTable->setItem((int)l_nI, 1, new QTableWidgetItem(QString::number(f_Round3(l_vP[l_nI].m_dY))));
        m_pTable->setItem((int)l_nI, 2, new QTableWidgetItem(QString::number(l_nI < l_vT.size() ? std::lround(l_vT[l_nI]) : 0)));
    }
    if (m_iSelected >= 0 && m_iSelected < m_pTable->rowCount()) m_pTable->selectRow(m_iSelected);
    m_bFilling = false;
}

void SymbolEditor::f_FillRows() {
    const ojson l_jRows = m_jFile["weapons"].value("staff", ojson::array());
    m_pRows->setRowCount(0);
    for (const auto& l_jR : l_jRows) {
        if (!l_jR.is_object() || !l_jR.contains("lv")) continue;
        const int l_iR = m_pRows->rowCount();
        m_pRows->insertRow(l_iR);
        m_pRows->setItem(l_iR, 0, new QTableWidgetItem(QString::number(l_jR.value("lv", 1))));
        m_pRows->setItem(l_iR, 1, new QTableWidgetItem(l_jR.contains("chance") ? QString::number(l_jR["chance"].get<double>()) : QString()));
        QString l_oSyms;
        const ojson l_jSyms = l_jR.value("symbols", ojson::object());
        for (auto l_oIt = l_jSyms.begin(); l_oIt != l_jSyms.end(); ++l_oIt)
            l_oSyms += (l_oSyms.isEmpty() ? QString() : QStringLiteral(", ")) + f_Qs(l_oIt.key()) + QStringLiteral(":") + QString::number(l_oIt.value().get<double>());
        m_pRows->setItem(l_iR, 2, new QTableWidgetItem(l_oSyms));
    }
}

void SymbolEditor::f_ReadRows() {
    ojson l_jRows = ojson::array();
    for (const auto& l_jOld : m_jFile["weapons"].value("staff", ojson::array()))   // the comment rows stay first
        if (l_jOld.is_object() && !l_jOld.contains("lv")) l_jRows.push_back(l_jOld);
    for (int l_iR = 0; l_iR < m_pRows->rowCount(); l_iR++) {
        auto l_fnText = [&](int l_iC) { return m_pRows->item(l_iR, l_iC) ? m_pRows->item(l_iR, l_iC)->text().trimmed() : QString(); };
        bool l_bOk = false;
        const int l_iLv = l_fnText(0).toInt(&l_bOk);
        if (!l_bOk) continue;
        ojson l_jR = ojson::object({{"lv", std::max(1, l_iLv)}});
        const double l_dChance = l_fnText(1).toDouble(&l_bOk);
        if (l_bOk) l_jR["chance"] = l_dChance;
        ojson l_jW = ojson::object();
        for (const QString& l_oPart : l_fnText(2).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QStringList l_oKv = l_oPart.split(QLatin1Char(':'));
            const double l_dW = l_oKv.size() > 1 ? l_oKv[1].trimmed().toDouble() : 1.0;
            if (!l_oKv[0].trimmed().isEmpty()) l_jW[f_Ss(l_oKv[0].trimmed())] = l_dW;
        }
        if (!l_jW.empty()) l_jR["symbols"] = l_jW;
        l_jRows.push_back(l_jR);
    }
    m_jFile["weapons"]["staff"] = l_jRows;
}

bool SymbolEditor::f_Save() {
    f_Commit();
    f_ReadRows();
    QFile l_oF(m_oPath);
    if (!l_oF.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f_Say(tr("Could not write %1").arg(m_oPath)); return false; }
    l_oF.write(QByteArray::fromStdString(f_WriteSymbolsFile(m_jFile)));
    l_oF.close();
    if (m_fnSaved) m_fnSaved(m_jFile);
    f_Say(tr("Saved data/circle_symbols.json (the running game uses it now)"));
    return true;
}

}  // namespace bed
