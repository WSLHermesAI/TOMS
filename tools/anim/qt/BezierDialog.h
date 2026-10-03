#pragma once

#include "anim_clip.h"

#include <QDialog>
#include <QWidget>

class QDoubleSpinBox;
class QComboBox;
class QTimer;

// The curve of a cubic-bezier ease, drawn with Ease::apply (the runtime's own evaluation), with
// the two control points as draggable handles and a dot running along it as a timing preview.
class BezierCurveEdit : public QWidget
{
    Q_OBJECT
public:
    explicit BezierCurveEdit(QWidget* parent = nullptr);
    void setBezier(const float b[4]);
    const float* bezier() const { return m_b; }
    QSize sizeHint() const override { return QSize(260, 260); }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF box() const;
    QPointF toWidget(double x, double y) const;
    QPointF fromWidget(const QPointF& w) const;

    float m_b[4] = {0.25f, 0.1f, 0.25f, 1.0f};
    int m_drag = -1;   // 0 = P1, 1 = P2
    double m_phase = 0;
    QTimer* m_timer;
};

class BezierDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BezierDialog(const toms::anim::Ease& start, QWidget* parent = nullptr);
    toms::anim::Ease ease() const;

private:
    void syncSpins();

    BezierCurveEdit* m_curve;
    QDoubleSpinBox* m_spin[4];
    QComboBox* m_presets;
    bool m_updating = false;
};

// Ease combo boxes (Properties, key list): every Tweeny ease by name, then a last item that
// opens the Bezier dialog (and names the current curve when the ease is a Bezier).
namespace EaseUi {
void fill(QComboBox* combo);
void show(QComboBox* combo, const toms::anim::Ease* e);   // nullptr = no key (disabled)
// After the user picked `index`: the chosen ease (the dialog runs for the Bezier item). False =
// cancelled.
bool picked(QComboBox* combo, int index, const toms::anim::Ease& current, QWidget* parent, toms::anim::Ease& out);
}  // namespace EaseUi
