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
    RangeEdit(double lo, double hi, int decimals, double step, const QString& suffix, QWidget* parent = nullptr);
    void setValue(const toms::fx::Range& r);
    toms::fx::Range value() const;
    QDoubleSpinBox* minSpin() const { return m_min; }
    QDoubleSpinBox* maxSpin() const { return m_max; }
signals:
    void changed(const toms::fx::Range& r);
private:
    void emitChanged();
    QDoubleSpinBox *m_min, *m_max;
    QToolButton* m_link;
    bool m_setting = false;
};

class Vec2Edit : public QWidget
{
    Q_OBJECT
public:
    Vec2Edit(double lo, double hi, int decimals, QWidget* parent = nullptr);
    void setValue(const glm::vec2& v);
    glm::vec2 value() const;
signals:
    void changed(const glm::vec2& v);
private:
    QDoubleSpinBox *m_x, *m_y;
    bool m_setting = false;
};

// A colour swatch (over a checkerboard, so alpha shows); click = colour dialog with alpha.
class ColorButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    void setColor(const glm::vec4& c);
    glm::vec4 color() const { return m_color; }
    static QColor toQColor(const glm::vec4& c);
    static glm::vec4 fromQColor(const QColor& c);
signals:
    void changed(const glm::vec4& c);
protected:
    void paintEvent(QPaintEvent* e) override;
private:
    glm::vec4 m_color{1, 1, 1, 1};
};

// The .anim ease names (linear, stepped, quadraticIn, ...); a Bezier shows as "bezier" and is kept.
class EaseCombo : public QComboBox
{
    Q_OBJECT
public:
    explicit EaseCombo(QWidget* parent = nullptr);
    void setEase(const toms::fx::Ease& e);
    toms::fx::Ease ease() const;
signals:
    void easeChanged(const toms::fx::Ease& e);
private:
    toms::fx::Ease m_bezier;
    bool m_setting = false;
};

// A curve over the particle's life (x = 0..1) of a multiplier (size, speed, spin). Drag a key to
// move it; double-click adds one; right-click deletes. The selected key's value and ease are
// edited below the graph. No keys = x1 (dashed).
class CurveEdit : public QWidget
{
    Q_OBJECT
public:
    explicit CurveEdit(QWidget* parent = nullptr);
    void setCurve(const toms::fx::Curve<float>& c);
    const toms::fx::Curve<float>& curve() const { return m_curve; }
    int gesture() const { return m_gesture; }
    int selected() const { return m_sel; }
    // For the selftest: a key added at (t, v) as a double-click would.
    void addKey(float t, float v);
signals:
    void changed(const toms::fx::Curve<float>& c);
protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
private:
    QRectF graph() const;
    void range(float& lo, float& hi) const;
    QPointF toPx(float t, float v) const;
    void fromPx(const QPointF& p, float& t, float& v) const;
    int keyAt(const QPointF& p) const;
    void sortKeepSelection();
    void commit();
    void syncRow();

    toms::fx::Curve<float> m_curve;
    int m_sel = -1;
    bool m_dragging = false;
    int m_gesture = 0;
    float m_lo = 0, m_hi = 2;    // the value range shown, fixed during a drag
    QDoubleSpinBox* m_value;
    EaseCombo* m_ease;
    QToolButton* m_clear;
    bool m_setting = false;
};

// Colour (and alpha) over the particle's life, as stops on a bar. Click a stop to select it, drag
// to move it, double-click it to pick its colour; double-click the bar to add one; right-click
// deletes. The colour multiplies the emitter's start colour. No stops = white (no change).
class GradientEdit : public QWidget
{
    Q_OBJECT
public:
    explicit GradientEdit(QWidget* parent = nullptr);
    void setCurve(const toms::fx::Curve<glm::vec4>& c);
    const toms::fx::Curve<glm::vec4>& curve() const { return m_curve; }
    int gesture() const { return m_gesture; }
    void addStop(float t);   // for the selftest: as a double-click on the bar
signals:
    void changed(const toms::fx::Curve<glm::vec4>& c);
protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
private:
    QRectF bar() const;
    float tAt(double x) const;
    int stopAt(const QPointF& p) const;
    void sortKeepSelection();
    void commit();
    void syncRow();

    toms::fx::Curve<glm::vec4> m_curve;
    int m_sel = -1;
    bool m_dragging = false;
    int m_gesture = 0;
    ColorButton* m_color;
    EaseCombo* m_ease;
    QToolButton* m_clear;
    bool m_setting = false;
};
