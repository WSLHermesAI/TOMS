#include "BezierDialog.h"

#include "Theme.h"
#include "anim_keys.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
// y may overshoot (back-like curves), so the box shows -1..2.
constexpr double g_dYMin = -1.0, g_dYMax = 2.0;

struct Preset {
    const char* m_pszName;
    float m_arrB[4];
};
const Preset g_arrPresets[] = {
    {"ease", {0.25f, 0.1f, 0.25f, 1.0f}},       {"ease-in", {0.42f, 0.0f, 1.0f, 1.0f}},
    {"ease-out", {0.0f, 0.0f, 0.58f, 1.0f}},    {"ease-in-out", {0.42f, 0.0f, 0.58f, 1.0f}},
    {"overshoot", {0.34f, 1.56f, 0.64f, 1.0f}}, {"anticipate", {0.36f, -0.4f, 0.6f, 1.0f}},
};
}  // namespace

BezierCurveEdit::BezierCurveEdit(QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pTimer(new QTimer(this))
{
    setMinimumSize(220, 220);
    setMouseTracking(true);
    connect(m_pTimer, &QTimer::timeout, this, [this] {
        m_dPhase = std::fmod(m_dPhase + 0.016, 1.6);   // 1 s run + 0.6 s pause
        update();
    });
    m_pTimer->start(16);
}

void BezierCurveEdit::f_SetBezier(const float l_arrB[4])
{
    std::copy(l_arrB, l_arrB + 4, m_arrB);
    update();
}

QRectF BezierCurveEdit::f_Box() const
{
    const double l_dS = std::min(width(), height()) - 24;
    return QRectF((width() - l_dS) / 2, (height() - l_dS) / 2, l_dS, l_dS);
}

QPointF BezierCurveEdit::f_ToWidget(double l_dX, double l_dY) const
{
    const QRectF l_oB = f_Box();
    return QPointF(l_oB.left() + l_dX * l_oB.width(), l_oB.bottom() - (l_dY - g_dYMin) / (g_dYMax - g_dYMin) * l_oB.height());
}

QPointF BezierCurveEdit::f_FromWidget(const QPointF& l_oW) const
{
    const QRectF l_oB = f_Box();
    return QPointF((l_oW.x() - l_oB.left()) / l_oB.width(), g_dYMin + (l_oB.bottom() - l_oW.y()) / l_oB.height() * (g_dYMax - g_dYMin));
}

void BezierCurveEdit::paintEvent(QPaintEvent*)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    QPainter l_oP(this);
    l_oP.setRenderHint(QPainter::Antialiasing);
    l_oP.fillRect(rect(), palette().color(QPalette::Base));
    // The unit square (0..1 in x and y) and the overshoot area around it.
    l_oP.setPen(QPen(l_oTc.m_oGrid, 1));
    for (int l_iI = 0; l_iI <= 4; l_iI++) l_oP.drawLine(f_ToWidget(l_iI / 4.0, g_dYMin), f_ToWidget(l_iI / 4.0, g_dYMax));
    for (double l_dY = g_dYMin; l_dY <= g_dYMax + 1e-9; l_dY += 0.5) l_oP.drawLine(f_ToWidget(0, l_dY), f_ToWidget(1, l_dY));
    l_oP.setPen(QPen(l_oTc.m_oGridAxis, 1));
    l_oP.drawRect(QRectF(f_ToWidget(0, 1), f_ToWidget(1, 0)));
    const QPointF l_oP0 = f_ToWidget(0, 0), l_oP3 = f_ToWidget(1, 1), l_oH1 = f_ToWidget(m_arrB[0], m_arrB[1]), l_oH2 = f_ToWidget(m_arrB[2], m_arrB[3]);
    l_oP.setPen(QPen(l_oTc.m_oPageBorder, 1.5));
    l_oP.drawLine(l_oP0, l_oH1);
    l_oP.drawLine(l_oP3, l_oH2);
    // The curve as the runtime evaluates it.
    toms::anim::Ease l_oE;
    l_oE.m_iKind = toms::anim::Ease::m_iBezier;
    std::copy(m_arrB, m_arrB + 4, l_oE.m_arrBezier);
    QPainterPath l_oPath(l_oP0);
    for (int l_iI = 1; l_iI <= 100; l_iI++) l_oPath.lineTo(f_ToWidget(l_iI / 100.0, l_oE.f_Apply(float(l_iI / 100.0))));
    l_oP.setPen(QPen(l_oTc.m_oSelection, 2.5));
    l_oP.setBrush(Qt::NoBrush);
    l_oP.drawPath(l_oPath);
    for (int l_iI = 0; l_iI < 2; l_iI++) {
        l_oP.setPen(QPen(Qt::white, 1.5));
        l_oP.setBrush(l_iI == 0 ? l_oTc.m_oGizmoX : l_oTc.m_oGizmoY);
        l_oP.drawEllipse(l_iI == 0 ? l_oH1 : l_oH2, 6, 6);
    }
    // Timing preview: a dot moving with the eased value along the top edge.
    const double l_dT = std::min(1.0, m_dPhase);
    const double l_dV = l_oE.f_Apply(float(l_dT));
    l_oP.setPen(Qt::NoPen);
    l_oP.setBrush(l_oTc.m_oKey);
    l_oP.drawEllipse(QPointF(f_Box().left() + l_dV * f_Box().width(), f_Box().top() - 6), 5, 5);
    l_oP.setBrush(l_oTc.m_oKey.darker(150));
    l_oP.drawEllipse(f_ToWidget(l_dT, l_dV), 3.5, 3.5);
}

void BezierCurveEdit::mousePressEvent(QMouseEvent* l_pE)
{
    const QPointF l_arrH[2] = {f_ToWidget(m_arrB[0], m_arrB[1]), f_ToWidget(m_arrB[2], m_arrB[3])};
    m_iDrag = -1;
    for (int l_iI = 0; l_iI < 2; l_iI++)
        if (std::hypot(l_pE->position().x() - l_arrH[l_iI].x(), l_pE->position().y() - l_arrH[l_iI].y()) < 10) m_iDrag = l_iI;
}

void BezierCurveEdit::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (m_iDrag < 0) return;
    const QPointF l_oC = f_FromWidget(l_pE->position());
    m_arrB[m_iDrag * 2] = float(std::clamp(l_oC.x(), 0.0, 1.0));
    m_arrB[m_iDrag * 2 + 1] = float(std::clamp(l_oC.y(), g_dYMin, g_dYMax));
    update();
    emit f_Changed();
}

void BezierCurveEdit::mouseReleaseEvent(QMouseEvent*) { m_iDrag = -1; }

BezierDialog::BezierDialog(const toms::anim::Ease& l_oStart, QWidget* l_pParent)
    : QDialog(l_pParent)
    , m_pCurve(new BezierCurveEdit(this))
{
    setWindowTitle(tr("Bezier Ease"));
    if (l_oStart.m_iKind == toms::anim::Ease::m_iBezier) m_pCurve->f_SetBezier(l_oStart.m_arrBezier);
    m_pPresets = new QComboBox(this);
    m_pPresets->addItem(tr("Presets…"));
    for (const Preset& l_oPr : g_arrPresets) m_pPresets->addItem(QString::fromLatin1(l_oPr.m_pszName));
    connect(m_pPresets, &QComboBox::activated, this, [this](int l_iI) {
        if (l_iI <= 0) return;
        m_pCurve->f_SetBezier(g_arrPresets[l_iI - 1].m_arrB);
        f_SyncSpins();
        m_pPresets->setCurrentIndex(0);
    });
    auto* l_pSpins = new QHBoxLayout();
    const char* l_arrLabels[4] = {"x1", "y1", "x2", "y2"};
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        m_arrSpin[l_iI] = new QDoubleSpinBox(this);
        m_arrSpin[l_iI]->setDecimals(3);
        m_arrSpin[l_iI]->setSingleStep(0.05);
        m_arrSpin[l_iI]->setRange(l_iI % 2 == 0 ? 0.0 : g_dYMin, l_iI % 2 == 0 ? 1.0 : g_dYMax);
        auto* l_pLab = new QLabel(QString::fromLatin1(l_arrLabels[l_iI]), this);
        l_pLab->setForegroundRole(QPalette::PlaceholderText);
        l_pSpins->addWidget(l_pLab);
        l_pSpins->addWidget(m_arrSpin[l_iI], 1);
        connect(m_arrSpin[l_iI], &QDoubleSpinBox::valueChanged, this, [this] {
            if (m_bUpdating) return;
            float l_arrB[4];
            for (int l_iK = 0; l_iK < 4; l_iK++) l_arrB[l_iK] = float(m_arrSpin[l_iK]->value());
            m_pCurve->f_SetBezier(l_arrB);
        });
    }
    connect(m_pCurve, &BezierCurveEdit::f_Changed, this, &BezierDialog::f_SyncSpins);
    auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(l_pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(l_pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* l_pL = new QVBoxLayout(this);
    l_pL->addWidget(m_pPresets);
    l_pL->addWidget(m_pCurve, 1);
    l_pL->addLayout(l_pSpins);
    auto* l_pHint = new QLabel(tr("Like CSS cubic-bezier(x1, y1, x2, y2). Drag the handles; y may overshoot."), this);
    l_pHint->setForegroundRole(QPalette::PlaceholderText);
    l_pHint->setWordWrap(true);
    l_pL->addWidget(l_pHint);
    l_pL->addWidget(l_pButtons);
    f_SyncSpins();
}

void BezierDialog::f_SyncSpins()
{
    m_bUpdating = true;
    for (int l_iI = 0; l_iI < 4; l_iI++) m_arrSpin[l_iI]->setValue(m_pCurve->f_Bezier()[l_iI]);
    m_bUpdating = false;
}

toms::anim::Ease BezierDialog::f_Ease() const
{
    toms::anim::Ease l_oE;
    l_oE.m_iKind = toms::anim::Ease::m_iBezier;
    std::copy(m_pCurve->f_Bezier(), m_pCurve->f_Bezier() + 4, l_oE.m_arrBezier);
    return l_oE;
}

namespace EaseUi {

void f_Fill(QComboBox* l_pCombo)
{
    l_pCombo->clear();
    for (const std::string& l_strN : toms::anim::f_EaseNames()) l_pCombo->addItem(QString::fromStdString(l_strN));
    l_pCombo->addItem(QObject::tr("Bezier…"));
    l_pCombo->setMaxVisibleItems(20);
}

void f_Show(QComboBox* l_pCombo, const toms::anim::Ease* l_pE)
{
    const QSignalBlocker l_oBlock(l_pCombo);
    const int l_iBezierItem = l_pCombo->count() - 1;
    l_pCombo->setEnabled(l_pE != nullptr);
    if (!l_pE) {
        l_pCombo->setCurrentIndex(1);   // linear, greyed out
        l_pCombo->setItemText(l_iBezierItem, QObject::tr("Bezier…"));
        return;
    }
    if (l_pE->m_iKind == toms::anim::Ease::m_iBezier) {
        l_pCombo->setItemText(l_iBezierItem, QString::fromStdString(animed::f_FormatEase(*l_pE)));
        l_pCombo->setCurrentIndex(l_iBezierItem);
    } else {
        l_pCombo->setItemText(l_iBezierItem, QObject::tr("Bezier…"));
        l_pCombo->setCurrentIndex(std::clamp(l_pE->m_iKind, 0, l_iBezierItem - 1));
    }
}

bool f_Picked(QComboBox* l_pCombo, int l_iIndex, const toms::anim::Ease& l_oCurrent, QWidget* l_pParent, toms::anim::Ease& l_oOut)
{
    if (l_iIndex < 0) return false;
    if (l_iIndex == l_pCombo->count() - 1) {
        BezierDialog l_oDlg(l_oCurrent, l_pParent);
        if (l_oDlg.exec() != QDialog::Accepted) {
            f_Show(l_pCombo, &l_oCurrent);
            return false;
        }
        l_oOut = l_oDlg.f_Ease();
        return true;
    }
    l_oOut = toms::anim::Ease();
    l_oOut.m_iKind = l_iIndex;
    return true;
}

}  // namespace EaseUi
