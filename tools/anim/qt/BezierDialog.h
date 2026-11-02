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
    explicit BezierCurveEdit(QWidget* l_pParent = nullptr);
    void f_SetBezier(const float l_arrB[4]);
    const float* f_Bezier() const { return m_arrB; }
    QSize sizeHint() const override { return QSize(260, 260); }

signals:
    void f_Changed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF f_Box() const;
    QPointF f_ToWidget(double l_dX, double l_dY) const;
    QPointF f_FromWidget(const QPointF& l_oW) const;

    float m_arrB[4] = {0.25f, 0.1f, 0.25f, 1.0f};
    int m_iDrag = -1;   // 0 = P1, 1 = P2
    double m_dPhase = 0;
    QTimer* m_pTimer;
};

class BezierDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BezierDialog(const toms::anim::Ease& l_oStart, QWidget* l_pParent = nullptr);
    toms::anim::Ease f_Ease() const;

private:
    void f_SyncSpins();

    BezierCurveEdit* m_pCurve;
    QDoubleSpinBox* m_arrSpin[4];
    QComboBox* m_pPresets;
    bool m_bUpdating = false;
};

// Ease combo boxes (Properties, key list): every Tweeny ease by name, then a last item that
// opens the Bezier dialog (and names the current curve when the ease is a Bezier).
namespace EaseUi {
void f_Fill(QComboBox* l_pCombo);
void f_Show(QComboBox* l_pCombo, const toms::anim::Ease* l_pE);   // nullptr = no key (disabled)
// After the user picked `index`: the chosen ease (the dialog runs for the Bezier item). False =
// cancelled.
bool f_Picked(QComboBox* l_pCombo, int l_iIndex, const toms::anim::Ease& l_oCurrent, QWidget* l_pParent, toms::anim::Ease& l_oOut);
}  // namespace EaseUi
