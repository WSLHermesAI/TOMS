#include "ParticleWidgets.h"

#include "Theme.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using toms::fx::Curve;
using toms::fx::CurveKey;
using toms::fx::Ease;
using toms::fx::Range;

namespace {

QDoubleSpinBox* f_Spin(double l_dLo, double l_dHi, int l_iDecimals, double l_dStep, const QString& l_oSuffix, QWidget* l_pParent)
{
    auto* l_pS = new QDoubleSpinBox(l_pParent);
    l_pS->setRange(l_dLo, l_dHi);
    l_pS->setDecimals(l_iDecimals);
    l_pS->setSingleStep(l_dStep);
    l_pS->setSuffix(l_oSuffix);
    l_pS->setKeyboardTracking(false);
    l_pS->setMinimumWidth(70);
    return l_pS;
}

QPixmap f_Checker(int l_iCell)
{
    QPixmap l_oPm(l_iCell * 2, l_iCell * 2);
    l_oPm.fill(QColor(200, 200, 200));
    QPainter l_oP(&l_oPm);
    l_oP.fillRect(0, 0, l_iCell, l_iCell, QColor(150, 150, 150));
    l_oP.fillRect(l_iCell, l_iCell, l_iCell, l_iCell, QColor(150, 150, 150));
    return l_oPm;
}

}  // namespace

// ---- RangeEdit ---------------------------------------------------------------------------------

RangeEdit::RangeEdit(double l_dLo, double l_dHi, int l_iDecimals, double l_dStep, const QString& l_oSuffix, QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pMin(f_Spin(l_dLo, l_dHi, l_iDecimals, l_dStep, l_oSuffix, this))
    , m_pMax(f_Spin(l_dLo, l_dHi, l_iDecimals, l_dStep, l_oSuffix, this))
    , m_pLink(new QToolButton(this))
{
    m_pLink->setCheckable(true);
    m_pLink->setText(QStringLiteral("="));
    m_pLink->setToolTip(tr("Linked: one value (no randomness). Unlinked: each particle picks a value between min and max."));
    auto* l_pL = new QHBoxLayout(this);
    l_pL->setContentsMargins(0, 0, 0, 0);
    l_pL->setSpacing(3);
    l_pL->addWidget(m_pMin, 1);
    l_pL->addWidget(m_pLink);
    l_pL->addWidget(m_pMax, 1);
    // Linked: max follows min. Unlinked: a min above the max pushes the max up, and the other way.
    connect(m_pMin, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_bSetting && (m_pLink->isChecked() || m_pMin->value() > m_pMax->value())) {
            const QSignalBlocker l_oB(m_pMax);
            m_pMax->setValue(m_pMin->value());
        }
        f_EmitChanged();
    });
    connect(m_pMax, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_bSetting && m_pMax->value() < m_pMin->value()) {
            const QSignalBlocker l_oB(m_pMin);
            m_pMin->setValue(m_pMax->value());
        }
        f_EmitChanged();
    });
    connect(m_pLink, &QToolButton::toggled, this, [this](bool l_bOn) {
        m_pMax->setEnabled(!l_bOn);
        if (l_bOn && !m_bSetting) {
            const QSignalBlocker l_oB(m_pMax);
            m_pMax->setValue(m_pMin->value());
            f_EmitChanged();
        }
    });
}

void RangeEdit::f_SetValue(const Range& l_oR)
{
    m_bSetting = true;
    const QSignalBlocker l_oA(m_pMin), l_oB(m_pMax), l_oC(m_pLink);
    m_pMin->setValue(l_oR.m_fMin);
    m_pMax->setValue(l_oR.m_fMax);
    m_pLink->setChecked(l_oR.f_Fixed());
    m_pMax->setEnabled(!l_oR.f_Fixed());
    m_bSetting = false;
}

Range RangeEdit::f_Value() const
{
    const float l_fLo = float(m_pMin->value()), l_fHi = float(m_pLink->isChecked() ? m_pMin->value() : m_pMax->value());
    return Range(std::min(l_fLo, l_fHi), std::max(l_fLo, l_fHi));
}

void RangeEdit::f_EmitChanged()
{
    if (!m_bSetting) emit f_Changed(f_Value());
}

// ---- Vec2Edit ----------------------------------------------------------------------------------

Vec2Edit::Vec2Edit(double l_dLo, double l_dHi, int l_iDecimals, QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pX(f_Spin(l_dLo, l_dHi, l_iDecimals, 1, QString(), this))
    , m_pY(f_Spin(l_dLo, l_dHi, l_iDecimals, 1, QString(), this))
{
    m_pX->setPrefix(QStringLiteral("x "));
    m_pY->setPrefix(QStringLiteral("y "));
    auto* l_pL = new QHBoxLayout(this);
    l_pL->setContentsMargins(0, 0, 0, 0);
    l_pL->setSpacing(3);
    l_pL->addWidget(m_pX, 1);
    l_pL->addWidget(m_pY, 1);
    for (QDoubleSpinBox* l_pS : {m_pX, m_pY})
        connect(l_pS, &QDoubleSpinBox::valueChanged, this, [this] {
            if (!m_bSetting) emit f_Changed(f_Value());
        });
}

void Vec2Edit::f_SetValue(const glm::vec2& l_v2V)
{
    m_bSetting = true;
    m_pX->setValue(l_v2V.x);
    m_pY->setValue(l_v2V.y);
    m_bSetting = false;
}

glm::vec2 Vec2Edit::f_Value() const { return {float(m_pX->value()), float(m_pY->value())}; }

// ---- ColorButton -------------------------------------------------------------------------------

ColorButton::ColorButton(QWidget* l_pParent)
    : QToolButton(l_pParent)
{
    setMinimumSize(44, 22);
    setToolTip(tr("Click to pick the colour and its alpha"));
    connect(this, &QToolButton::clicked, this, [this] {
        const QColor l_oC = QColorDialog::getColor(f_ToQColor(m_v4Color), this, tr("Colour"), QColorDialog::ShowAlphaChannel);
        if (!l_oC.isValid()) return;
        f_SetColor(f_FromQColor(l_oC));
        emit f_Changed(m_v4Color);
    });
}

QColor ColorButton::f_ToQColor(const glm::vec4& l_v4C)
{
    auto l_fnB = [](float l_fV) { return std::clamp(int(std::lround(l_fV * 255)), 0, 255); };
    return QColor(l_fnB(l_v4C.r), l_fnB(l_v4C.g), l_fnB(l_v4C.b), l_fnB(l_v4C.a));
}

glm::vec4 ColorButton::f_FromQColor(const QColor& l_oC)
{
    auto l_fnR = [](int l_iV) { return std::round(l_iV / 255.0f * 1000.0f) / 1000.0f; };
    return {l_fnR(l_oC.red()), l_fnR(l_oC.green()), l_fnR(l_oC.blue()), l_fnR(l_oC.alpha())};
}

void ColorButton::f_SetColor(const glm::vec4& l_v4C)
{
    m_v4Color = l_v4C;
    update();
}

void ColorButton::paintEvent(QPaintEvent* l_pE)
{
    QToolButton::paintEvent(l_pE);
    QPainter l_oP(this);
    const QRect l_oR = rect().adjusted(4, 4, -4, -4);
    l_oP.fillRect(l_oR, QBrush(f_Checker(4)));
    l_oP.fillRect(l_oR, f_ToQColor(m_v4Color));
    l_oP.setPen(palette().color(QPalette::Mid));
    l_oP.drawRect(l_oR.adjusted(0, 0, -1, -1));
}

// ---- EaseCombo ---------------------------------------------------------------------------------

EaseCombo::EaseCombo(QWidget* l_pParent)
    : QComboBox(l_pParent)
{
    for (const std::string& l_strN : toms::anim::f_EaseNames()) addItem(QString::fromStdString(l_strN));
    setToolTip(tr("The curve from this key to the next"));
    connect(this, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_bSetting) emit f_EaseChanged(f_Ease());
    });
}

void EaseCombo::f_SetEase(const Ease& l_oE)
{
    m_bSetting = true;
    const int l_iBez = findText(QStringLiteral("bezier"));
    if (l_oE.m_iKind == Ease::m_iBezier) {
        m_oBezier = l_oE;
        if (l_iBez < 0) addItem(QStringLiteral("bezier"));
        setCurrentIndex(findText(QStringLiteral("bezier")));
    } else {
        if (l_iBez >= 0) removeItem(l_iBez);
        setCurrentIndex(findText(QString::fromStdString(toms::anim::f_EaseName(l_oE))));
    }
    m_bSetting = false;
}

Ease EaseCombo::f_Ease() const
{
    if (currentText() == QLatin1String("bezier")) return m_oBezier;
    Ease l_oE;
    toms::anim::f_ParseEase(currentText().toStdString(), l_oE);
    return l_oE;
}

// ---- CurveEdit ---------------------------------------------------------------------------------

CurveEdit::CurveEdit(QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pValue(f_Spin(-100, 100, 2, 0.1, QString(), this))
    , m_pEase(new EaseCombo(this))
    , m_pClear(new QToolButton(this))
{
    setMinimumHeight(130);
    setToolTip(tr("Drag a key to move it, double-click to add one, right-click to delete. x = the particle's life, 0..1."));
    m_pClear->setText(tr("Clear"));
    m_pValue->setPrefix(QStringLiteral("× "));
    auto* l_pRow = new QHBoxLayout;
    l_pRow->setContentsMargins(0, 0, 0, 0);
    l_pRow->addWidget(m_pValue);
    l_pRow->addWidget(m_pEase, 1);
    l_pRow->addWidget(m_pClear);
    auto* l_pL = new QVBoxLayout(this);
    l_pL->setContentsMargins(0, 0, 0, 0);
    l_pL->addStretch(1);
    l_pL->addLayout(l_pRow);
    connect(m_pValue, &QDoubleSpinBox::valueChanged, this, [this](double l_dV) {
        if (m_bSetting || m_iSel < 0) return;
        m_oCurve.m_vKeys[size_t(m_iSel)].m_oV = float(l_dV);
        f_Commit();
    });
    connect(m_pEase, &EaseCombo::f_EaseChanged, this, [this](const Ease& l_oE) {
        if (m_iSel < 0) return;
        m_oCurve.m_vKeys[size_t(m_iSel)].m_oEase = l_oE;
        f_Commit();
    });
    connect(m_pClear, &QToolButton::clicked, this, [this] {
        m_oCurve.m_vKeys.clear();
        m_iSel = -1;
        m_iGesture++;
        f_Commit();
    });
    f_SyncRow();
}

void CurveEdit::f_SetCurve(const Curve<float>& l_oC)
{
    m_oCurve = l_oC;
    if (m_iSel >= int(m_oCurve.m_vKeys.size())) m_iSel = int(m_oCurve.m_vKeys.size()) - 1;
    if (!m_bDragging) f_Range(m_fLo, m_fHi);
    f_SyncRow();
    update();
}

void CurveEdit::f_SyncRow()
{
    m_bSetting = true;
    const bool l_bHas = m_iSel >= 0 && m_iSel < int(m_oCurve.m_vKeys.size());
    m_pValue->setEnabled(l_bHas);
    m_pEase->setEnabled(l_bHas);
    m_pClear->setEnabled(!m_oCurve.m_vKeys.empty());
    if (l_bHas) {
        m_pValue->setValue(m_oCurve.m_vKeys[size_t(m_iSel)].m_oV);
        m_pEase->f_SetEase(m_oCurve.m_vKeys[size_t(m_iSel)].m_oEase);
    }
    m_bSetting = false;
}

QRectF CurveEdit::f_Graph() const { return QRectF(6, 4, width() - 12, height() - 40); }

void CurveEdit::f_Range(float& l_fLo, float& l_fHi) const
{
    l_fLo = 0;
    l_fHi = 2;
    for (const auto& l_oK : m_oCurve.m_vKeys) {
        l_fLo = std::min(l_fLo, l_oK.m_oV);
        l_fHi = std::max(l_fHi, l_oK.m_oV * 1.15f);
    }
}

QPointF CurveEdit::f_ToPx(float l_fT, float l_fV) const
{
    const QRectF l_oG = f_Graph();
    return QPointF(l_oG.left() + l_fT * l_oG.width(), l_oG.bottom() - (l_fV - m_fLo) / std::max(1e-6f, m_fHi - m_fLo) * l_oG.height());
}

void CurveEdit::f_FromPx(const QPointF& l_oP, float& l_fT, float& l_fV) const
{
    const QRectF l_oG = f_Graph();
    l_fT = float(std::clamp((l_oP.x() - l_oG.left()) / l_oG.width(), 0.0, 1.0));
    l_fV = float(m_fLo + (l_oG.bottom() - l_oP.y()) / l_oG.height() * (m_fHi - m_fLo));
    l_fT = std::round(l_fT * 100) / 100;
    l_fV = std::round(l_fV * 100) / 100;
}

int CurveEdit::f_KeyAt(const QPointF& l_oP) const
{
    for (int l_iI = 0; l_iI < int(m_oCurve.m_vKeys.size()); l_iI++)
        if (QLineF(f_ToPx(m_oCurve.m_vKeys[size_t(l_iI)].m_fT, m_oCurve.m_vKeys[size_t(l_iI)].m_oV), l_oP).length() <= 7) return l_iI;
    return -1;
}

void CurveEdit::f_SortKeepSelection()
{
    const float l_fSelT = m_iSel >= 0 ? m_oCurve.m_vKeys[size_t(m_iSel)].m_fT : -1;
    const float l_fSelV = m_iSel >= 0 ? m_oCurve.m_vKeys[size_t(m_iSel)].m_oV : 0;
    std::stable_sort(m_oCurve.m_vKeys.begin(), m_oCurve.m_vKeys.end(), [](const CurveKey<float>& l_oA, const CurveKey<float>& l_oB) { return l_oA.m_fT < l_oB.m_fT; });
    if (m_iSel >= 0)
        for (int l_iI = 0; l_iI < int(m_oCurve.m_vKeys.size()); l_iI++)
            if (m_oCurve.m_vKeys[size_t(l_iI)].m_fT == l_fSelT && m_oCurve.m_vKeys[size_t(l_iI)].m_oV == l_fSelV) m_iSel = l_iI;
}

void CurveEdit::f_Commit()
{
    f_SyncRow();
    update();
    emit f_Changed(m_oCurve);
}

void CurveEdit::f_AddKey(float l_fT, float l_fV)
{
    CurveKey<float> l_oK;
    l_oK.m_fT = std::clamp(l_fT, 0.0f, 1.0f);
    l_oK.m_oV = l_fV;
    m_oCurve.m_vKeys.push_back(l_oK);
    m_iSel = int(m_oCurve.m_vKeys.size()) - 1;
    f_SortKeepSelection();
    m_iGesture++;
    f_Commit();
}

void CurveEdit::paintEvent(QPaintEvent*)
{
    QPainter l_oP(this);
    l_oP.setRenderHint(QPainter::Antialiasing);
    const QRectF l_oG = f_Graph();
    l_oP.fillRect(l_oG, palette().color(QPalette::Base));
    l_oP.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
    for (float l_fV : {0.0f, 1.0f, 2.0f})
        if (l_fV >= m_fLo && l_fV <= m_fHi) l_oP.drawLine(f_ToPx(0, l_fV), f_ToPx(1, l_fV));
    l_oP.setPen(palette().color(QPalette::PlaceholderText));
    l_oP.drawText(QRectF(l_oG.left() + 3, f_ToPx(0, 1).y() - 14, 40, 14), Qt::AlignLeft, QStringLiteral("×1"));
    if (m_oCurve.f_Empty()) {
        l_oP.setPen(QPen(palette().color(QPalette::PlaceholderText), 1, Qt::DashLine));
        l_oP.drawLine(f_ToPx(0, 1), f_ToPx(1, 1));
        l_oP.drawText(l_oG, Qt::AlignCenter, tr("no curve (×1) — double-click to add a key"));
        return;
    }
    QPainterPath l_oPath;
    for (int l_iI = 0; l_iI <= 80; l_iI++) {
        const float l_fT = l_iI / 80.0f;
        const QPointF l_oPt = f_ToPx(l_fT, m_oCurve.f_At(l_fT, 1.0f));
        l_iI ? l_oPath.lineTo(l_oPt) : l_oPath.moveTo(l_oPt);
    }
    l_oP.setPen(QPen(Theme::f_Colors().m_oKey, 2));
    l_oP.drawPath(l_oPath);
    for (int l_iI = 0; l_iI < int(m_oCurve.m_vKeys.size()); l_iI++) {
        const auto& l_oK = m_oCurve.m_vKeys[size_t(l_iI)];
        l_oP.setPen(QPen(Qt::black, 1));
        l_oP.setBrush(l_iI == m_iSel ? Theme::f_Colors().m_oSelection : palette().color(QPalette::Text));
        l_oP.drawEllipse(f_ToPx(l_oK.m_fT, l_oK.m_oV), 4.5, 4.5);
    }
}

void CurveEdit::mousePressEvent(QMouseEvent* l_pE)
{
    const int l_iK = f_KeyAt(l_pE->position());
    if (l_pE->button() == Qt::RightButton) {
        if (l_iK >= 0) {
            m_oCurve.m_vKeys.erase(m_oCurve.m_vKeys.begin() + l_iK);
            m_iSel = -1;
            m_iGesture++;
            f_Commit();
        }
        return;
    }
    m_iSel = l_iK;
    m_bDragging = l_iK >= 0;
    m_iGesture++;
    f_SyncRow();
    update();
}

void CurveEdit::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (!m_bDragging || m_iSel < 0) return;
    float l_fT, l_fV;
    f_FromPx(l_pE->position(), l_fT, l_fV);
    m_oCurve.m_vKeys[size_t(m_iSel)].m_fT = l_fT;
    m_oCurve.m_vKeys[size_t(m_iSel)].m_oV = l_fV;
    f_SortKeepSelection();
    f_Commit();
}

void CurveEdit::mouseReleaseEvent(QMouseEvent*)
{
    if (m_bDragging) {
        m_bDragging = false;
        f_Range(m_fLo, m_fHi);
        update();
    }
}

void CurveEdit::mouseDoubleClickEvent(QMouseEvent* l_pE)
{
    if (f_KeyAt(l_pE->position()) >= 0 || !f_Graph().contains(l_pE->position())) return;
    float l_fT, l_fV;
    f_FromPx(l_pE->position(), l_fT, l_fV);
    f_AddKey(l_fT, l_fV);
}

// ---- GradientEdit ------------------------------------------------------------------------------

GradientEdit::GradientEdit(QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pColor(new ColorButton(this))
    , m_pEase(new EaseCombo(this))
    , m_pClear(new QToolButton(this))
{
    setMinimumHeight(78);
    setToolTip(tr("Colour × alpha over the particle's life. Drag a stop to move it, double-click it for its colour, "
                  "double-click the bar to add one, right-click to delete."));
    m_pClear->setText(tr("Clear"));
    auto* l_pRow = new QHBoxLayout;
    l_pRow->setContentsMargins(0, 0, 0, 0);
    l_pRow->addWidget(m_pColor);
    l_pRow->addWidget(m_pEase, 1);
    l_pRow->addWidget(m_pClear);
    auto* l_pL = new QVBoxLayout(this);
    l_pL->setContentsMargins(0, 0, 0, 0);
    l_pL->addStretch(1);
    l_pL->addLayout(l_pRow);
    connect(m_pColor, &ColorButton::f_Changed, this, [this](const glm::vec4& l_v4C) {
        if (m_iSel < 0) return;
        m_oCurve.m_vKeys[size_t(m_iSel)].m_oV = l_v4C;
        m_iGesture++;
        f_Commit();
    });
    connect(m_pEase, &EaseCombo::f_EaseChanged, this, [this](const Ease& l_oE) {
        if (m_iSel < 0) return;
        m_oCurve.m_vKeys[size_t(m_iSel)].m_oEase = l_oE;
        m_iGesture++;
        f_Commit();
    });
    connect(m_pClear, &QToolButton::clicked, this, [this] {
        m_oCurve.m_vKeys.clear();
        m_iSel = -1;
        m_iGesture++;
        f_Commit();
    });
    f_SyncRow();
}

void GradientEdit::f_SetCurve(const Curve<glm::vec4>& l_oC)
{
    m_oCurve = l_oC;
    if (m_iSel >= int(m_oCurve.m_vKeys.size())) m_iSel = int(m_oCurve.m_vKeys.size()) - 1;
    f_SyncRow();
    update();
}

void GradientEdit::f_SyncRow()
{
    m_bSetting = true;
    const bool l_bHas = m_iSel >= 0 && m_iSel < int(m_oCurve.m_vKeys.size());
    m_pColor->setEnabled(l_bHas);
    m_pEase->setEnabled(l_bHas);
    m_pClear->setEnabled(!m_oCurve.m_vKeys.empty());
    if (l_bHas) {
        m_pColor->f_SetColor(m_oCurve.m_vKeys[size_t(m_iSel)].m_oV);
        m_pEase->f_SetEase(m_oCurve.m_vKeys[size_t(m_iSel)].m_oEase);
    }
    m_bSetting = false;
}

QRectF GradientEdit::f_Bar() const { return QRectF(8, 4, width() - 16, 24); }

float GradientEdit::f_TAt(double l_dX) const
{
    const QRectF l_oB = f_Bar();
    return std::round(float(std::clamp((l_dX - l_oB.left()) / l_oB.width(), 0.0, 1.0)) * 100) / 100;
}

int GradientEdit::f_StopAt(const QPointF& l_oP) const
{
    const QRectF l_oB = f_Bar();
    for (int l_iI = 0; l_iI < int(m_oCurve.m_vKeys.size()); l_iI++) {
        const double l_dX = l_oB.left() + m_oCurve.m_vKeys[size_t(l_iI)].m_fT * l_oB.width();
        if (std::fabs(l_oP.x() - l_dX) <= 6 && l_oP.y() >= l_oB.top() - 2 && l_oP.y() <= l_oB.bottom() + 14) return l_iI;
    }
    return -1;
}

void GradientEdit::f_SortKeepSelection()
{
    const float l_fSelT = m_iSel >= 0 ? m_oCurve.m_vKeys[size_t(m_iSel)].m_fT : -1;
    std::stable_sort(m_oCurve.m_vKeys.begin(), m_oCurve.m_vKeys.end(), [](const auto& l_oA, const auto& l_oB) { return l_oA.m_fT < l_oB.m_fT; });
    if (m_iSel >= 0)
        for (int l_iI = 0; l_iI < int(m_oCurve.m_vKeys.size()); l_iI++)
            if (m_oCurve.m_vKeys[size_t(l_iI)].m_fT == l_fSelT) m_iSel = l_iI;
}

void GradientEdit::f_Commit()
{
    f_SyncRow();
    update();
    emit f_Changed(m_oCurve);
}

void GradientEdit::f_AddStop(float l_fT)
{
    CurveKey<glm::vec4> l_oK;
    l_oK.m_fT = std::clamp(l_fT, 0.0f, 1.0f);
    l_oK.m_oV = m_oCurve.f_At(l_oK.m_fT, glm::vec4(1.0f));
    m_oCurve.m_vKeys.push_back(l_oK);
    m_iSel = int(m_oCurve.m_vKeys.size()) - 1;
    f_SortKeepSelection();
    m_iGesture++;
    f_Commit();
}

void GradientEdit::paintEvent(QPaintEvent*)
{
    QPainter l_oP(this);
    const QRectF l_oB = f_Bar();
    l_oP.fillRect(l_oB, QBrush(f_Checker(5)));
    const int l_iN = int(l_oB.width());
    for (int l_iX = 0; l_iX < l_iN; l_iX++) {
        const float l_fT = l_iN > 1 ? l_iX / float(l_iN - 1) : 0;
        l_oP.fillRect(QRectF(l_oB.left() + l_iX, l_oB.top(), 1.5, l_oB.height()), ColorButton::f_ToQColor(m_oCurve.f_At(l_fT, glm::vec4(1.0f))));
    }
    l_oP.setPen(palette().color(QPalette::Mid));
    l_oP.setBrush(Qt::NoBrush);
    l_oP.drawRect(l_oB);
    if (m_oCurve.f_Empty()) {
        l_oP.setPen(Qt::black);
        l_oP.drawText(l_oB, Qt::AlignCenter, tr("white (no change) — double-click to add a stop"));
    }
    l_oP.setRenderHint(QPainter::Antialiasing);
    for (int l_iI = 0; l_iI < int(m_oCurve.m_vKeys.size()); l_iI++) {
        const auto& l_oK = m_oCurve.m_vKeys[size_t(l_iI)];
        const double l_dX = l_oB.left() + l_oK.m_fT * l_oB.width();
        QPolygonF l_oTri;
        l_oTri << QPointF(l_dX, l_oB.bottom() + 1) << QPointF(l_dX - 6, l_oB.bottom() + 12) << QPointF(l_dX + 6, l_oB.bottom() + 12);
        l_oP.setPen(QPen(l_iI == m_iSel ? Theme::f_Colors().m_oSelection : palette().color(QPalette::Text), l_iI == m_iSel ? 2 : 1));
        QColor l_oC = ColorButton::f_ToQColor(l_oK.m_oV);
        l_oC.setAlpha(255);
        l_oP.setBrush(l_oC);
        l_oP.drawPolygon(l_oTri);
    }
}

void GradientEdit::mousePressEvent(QMouseEvent* l_pE)
{
    const int l_iS = f_StopAt(l_pE->position());
    if (l_pE->button() == Qt::RightButton) {
        if (l_iS >= 0) {
            m_oCurve.m_vKeys.erase(m_oCurve.m_vKeys.begin() + l_iS);
            m_iSel = -1;
            m_iGesture++;
            f_Commit();
        }
        return;
    }
    m_iSel = l_iS;
    m_bDragging = l_iS >= 0;
    m_iGesture++;
    f_SyncRow();
    update();
}

void GradientEdit::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (!m_bDragging || m_iSel < 0) return;
    m_oCurve.m_vKeys[size_t(m_iSel)].m_fT = f_TAt(l_pE->position().x());
    f_SortKeepSelection();
    f_Commit();
}

void GradientEdit::mouseReleaseEvent(QMouseEvent*) { m_bDragging = false; }

void GradientEdit::mouseDoubleClickEvent(QMouseEvent* l_pE)
{
    const int l_iS = f_StopAt(l_pE->position());
    if (l_iS >= 0) {
        m_iSel = l_iS;
        f_SyncRow();
        m_pColor->click();
        return;
    }
    if (f_Bar().adjusted(0, 0, 0, 14).contains(l_pE->position())) f_AddStop(f_TAt(l_pE->position().x()));
}
