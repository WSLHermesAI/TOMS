#pragma once

#include "particle_fx.h"

#include <QComboBox>
#include <QToolButton>
#include <QWidget>

class QDoubleSpinBox;

// Small editors the Inspector is built from. Each emits its value when the user changes it (never
// when set from code), plus a gesture number that changes per mouse press / focus, so a whole
// drag or a held spin arrow can become one undo step.

// A start value: min and max. Linked (the chain button) = one value, no randomness.
class RangeEdit : public QWidget
{
    Q_OBJECT
public:
    RangeEdit(double l_dLo, double l_dHi, int l_iDecimals, double l_dStep, const QString& l_oSuffix, QWidget* l_pParent = nullptr);
    void f_SetValue(const toms::fx::Range& l_oR);
    toms::fx::Range f_Value() const;
    QDoubleSpinBox* f_MinSpin() const { return m_pMin; }
    QDoubleSpinBox* f_MaxSpin() const { return m_pMax; }
signals:
    void f_Changed(const toms::fx::Range& l_oR);
private:
    void f_EmitChanged();
    QDoubleSpinBox *m_pMin, *m_pMax;
    QToolButton* m_pLink;
    bool m_bSetting = false;
};

class Vec2Edit : public QWidget
{
    Q_OBJECT
public:
    Vec2Edit(double l_dLo, double l_dHi, int l_iDecimals, QWidget* l_pParent = nullptr);
    void f_SetValue(const glm::vec2& l_v2V);
    glm::vec2 f_Value() const;
signals:
    void f_Changed(const glm::vec2& l_v2V);
private:
    QDoubleSpinBox *m_pX, *m_pY;
    bool m_bSetting = false;
};

// A colour swatch (over a checkerboard, so alpha shows); click = colour dialog with alpha.
class ColorButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ColorButton(QWidget* l_pParent = nullptr);
    void f_SetColor(const glm::vec4& l_v4C);
    glm::vec4 f_Color() const { return m_v4Color; }
    static QColor f_ToQColor(const glm::vec4& l_v4C);
    static glm::vec4 f_FromQColor(const QColor& l_oC);
signals:
    void f_Changed(const glm::vec4& l_v4C);
protected:
    void paintEvent(QPaintEvent* l_pE) override;
private:
    glm::vec4 m_v4Color{1, 1, 1, 1};
};

// The .anim ease names (linear, stepped, quadraticIn, ...); a Bezier shows as "bezier" and is kept.
class EaseCombo : public QComboBox
{
    Q_OBJECT
public:
    explicit EaseCombo(QWidget* l_pParent = nullptr);
    void f_SetEase(const toms::fx::Ease& l_oE);
    toms::fx::Ease f_Ease() const;
signals:
    void f_EaseChanged(const toms::fx::Ease& l_oE);
private:
    toms::fx::Ease m_oBezier;
    bool m_bSetting = false;
};

// A curve over the particle's life (x = 0..1) of a multiplier (size, speed, spin). Drag a key to
// move it; double-click adds one; right-click deletes. The selected key's value and ease are
// edited below the graph. No keys = x1 (dashed).
class CurveEdit : public QWidget
{
    Q_OBJECT
public:
    explicit CurveEdit(QWidget* l_pParent = nullptr);
    void f_SetCurve(const toms::fx::Curve<float>& l_oC);
    const toms::fx::Curve<float>& f_Curve() const { return m_oCurve; }
    int f_Gesture() const { return m_iGesture; }
    int f_Selected() const { return m_iSel; }
    // For the selftest: a key added at (t, v) as a double-click would.
    void f_AddKey(float l_fT, float l_fV);
signals:
    void f_Changed(const toms::fx::Curve<float>& l_oC);
protected:
    void paintEvent(QPaintEvent* l_pE) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void mouseDoubleClickEvent(QMouseEvent* l_pE) override;
private:
    QRectF f_Graph() const;
    void f_Range(float& l_fLo, float& l_fHi) const;
    QPointF f_ToPx(float l_fT, float l_fV) const;
    void f_FromPx(const QPointF& l_oP, float& l_fT, float& l_fV) const;
    int f_KeyAt(const QPointF& l_oP) const;
    void f_SortKeepSelection();
    void f_Commit();
    void f_SyncRow();

    toms::fx::Curve<float> m_oCurve;
    int m_iSel = -1;
    bool m_bDragging = false;
    int m_iGesture = 0;
    float m_fLo = 0, m_fHi = 2;    // the value range shown, fixed during a drag
    QDoubleSpinBox* m_pValue;
    EaseCombo* m_pEase;
    QToolButton* m_pClear;
    bool m_bSetting = false;
};

// Colour (and alpha) over the particle's life, as stops on a bar. Click a stop to select it, drag
// to move it, double-click it to pick its colour; double-click the bar to add one; right-click
// deletes. The colour multiplies the emitter's start colour. No stops = white (no change).
class GradientEdit : public QWidget
{
    Q_OBJECT
public:
    explicit GradientEdit(QWidget* l_pParent = nullptr);
    void f_SetCurve(const toms::fx::Curve<glm::vec4>& l_oC);
    const toms::fx::Curve<glm::vec4>& f_Curve() const { return m_oCurve; }
    int f_Gesture() const { return m_iGesture; }
    void f_AddStop(float l_fT);   // for the selftest: as a double-click on the bar
signals:
    void f_Changed(const toms::fx::Curve<glm::vec4>& l_oC);
protected:
    void paintEvent(QPaintEvent* l_pE) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void mouseDoubleClickEvent(QMouseEvent* l_pE) override;
private:
    QRectF f_Bar() const;
    float f_TAt(double l_dX) const;
    int f_StopAt(const QPointF& l_oP) const;
    void f_SortKeepSelection();
    void f_Commit();
    void f_SyncRow();

    toms::fx::Curve<glm::vec4> m_oCurve;
    int m_iSel = -1;
    bool m_bDragging = false;
    int m_iGesture = 0;
    ColorButton* m_pColor;
    EaseCombo* m_pEase;
    QToolButton* m_pClear;
    bool m_bSetting = false;
};
