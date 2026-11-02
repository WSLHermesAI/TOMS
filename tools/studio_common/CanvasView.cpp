#include "CanvasView.h"

#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QSettings>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double g_dMinZoom = 1.0 / 32, g_dMaxZoom = 64.0;
constexpr int g_iCheckerCell = 8;   // screen pixels: the pattern does not scale with the zoom
const QString g_oCoordinatesKey = QStringLiteral("view/coordinates");

bool& f_CoordinatesFlag()
{
    static bool l_bOn = QSettings().value(g_oCoordinatesKey, true).toBool();
    return l_bOn;
}

QString f_CoordinateText(double l_dV, double l_dStep)
{
    if (std::fabs(l_dV) < l_dStep * 1e-6) return QStringLiteral("0");
    return QString::number(std::lround(l_dV));   // steps are whole content pixels
}
}

double CanvasView::f_CoordinateStep(double l_dZoom)
{
    // The smallest 1/2/5 x 10^n (at least one content pixel) that keeps the hints apart on screen.
    const double l_dWant = m_dCoordinateSpacing / std::max(l_dZoom, 1e-6);
    double l_dBase = 1;
    while (l_dBase * 10 <= l_dWant) l_dBase *= 10;
    for (double l_dM : {1.0, 2.0, 5.0, 10.0})
        if (l_dBase * l_dM >= l_dWant) return l_dBase * l_dM;
    return l_dBase * 10;
}

bool CanvasView::f_CoordinatesShown() { return f_CoordinatesFlag(); }

void CanvasView::f_SetCoordinatesShown(bool l_bOn)
{
    f_CoordinatesFlag() = l_bOn;
    QSettings().setValue(g_oCoordinatesKey, l_bOn);
    for (QWidget* l_pW : QApplication::allWidgets())
        if (auto* l_pView = qobject_cast<CanvasView*>(l_pW)) l_pView->update();
}

QAction* CanvasView::f_AddCoordinatesAction(QMenu* l_pMenu)
{
    QAction* l_pA = l_pMenu->addAction(tr("Show Coordinates"));
    l_pA->setCheckable(true);
    l_pA->setChecked(f_CoordinatesShown());
    l_pA->setToolTip(tr("X/Y values along the canvas edges; the step follows the zoom"));
    connect(l_pA, &QAction::toggled, l_pA, [](bool l_bOn) { f_SetCoordinatesShown(l_bOn); });
    return l_pA;
}

void CanvasView::f_PaintCoordinates(QPainter& l_oP)
{
    if (!f_CoordinatesShown()) return;
    const Theme::Colors& l_oTc = Theme::f_Colors();
    const double l_dStep = f_CoordinateStep(m_dZoom);
    const QPointF l_oC0 = f_ToContent(QPointF(0, 0)), l_oC1 = f_ToContent(QPointF(width(), height()));
    l_oP.save();
    l_oP.setRenderHint(QPainter::Antialiasing, false);
    QFont l_oF = l_oP.font();
    l_oF.setPointSizeF(std::max(7.0, l_oF.pointSizeF() - 1));
    l_oP.setFont(l_oF);
    const QFontMetrics l_oFm(l_oF);
    QColor l_oLine = l_oTc.m_oGrid;
    l_oLine.setAlpha(std::min(255, l_oLine.alpha() * 2));
    QColor l_oText = l_oTc.m_oLabel;
    l_oText.setAlpha(170);
    const double l_dBottom = height() - l_oFm.descent() - 3;   // x values sit on the bottom edge
    const double l_dLeftReserve = l_oFm.horizontalAdvance(QStringLiteral("-00000")) + 6;   // the y column's corner

    // Vertical lines, x values along the bottom.
    for (double l_dX = std::ceil(l_oC0.x() / l_dStep) * l_dStep; l_dX <= l_oC1.x(); l_dX += l_dStep) {
        const double l_dWx = std::round(f_ToWidget(QPointF(l_dX, 0)).x()) + 0.5;
        l_oP.setPen(QPen(l_oLine, 1));
        l_oP.drawLine(QPointF(l_dWx, 0), QPointF(l_dWx, height()));
        if (l_dWx < l_dLeftReserve) continue;   // the corner belongs to the y values
        l_oP.setPen(l_oText);
        l_oP.drawText(QPointF(l_dWx + 3, l_dBottom), f_CoordinateText(l_dX, l_dStep));
    }
    // Horizontal lines, y values along the left edge (above each line).
    for (double l_dY = std::ceil(l_oC0.y() / l_dStep) * l_dStep; l_dY <= l_oC1.y(); l_dY += l_dStep) {
        const double l_dWy = std::round(f_ToWidget(QPointF(0, l_dY)).y()) + 0.5;
        l_oP.setPen(QPen(l_oLine, 1));
        l_oP.drawLine(QPointF(0, l_dWy), QPointF(width(), l_dWy));
        if (l_dWy > height() - l_oFm.height() - 4) continue;   // the corner belongs to the x values
        if (l_dWy < l_oFm.height() + 30) continue;                // the top strip: the canvases' own labels (clip, time)
        l_oP.setPen(l_oText);
        l_oP.drawText(QPointF(4, l_dWy - 3), f_CoordinateText(l_dY, l_dStep));
    }

    // The cursor: its position next to it, and on the two edges (a highlighted mark among the values).
    if (m_bCursorIn && rect().contains(m_oCursor.toPoint())) {
        const QPointF l_oC = f_ToContent(m_oCursor);
        const int l_iDecimals = m_dZoom >= 8 ? 1 : 0;   // zoomed in far: tenths of a content pixel
        const QString l_oXs = QString::number(l_oC.x(), 'f', l_iDecimals), l_oYs = QString::number(l_oC.y(), 'f', l_iDecimals);
        const QColor l_oMark = l_oTc.m_oSelection;
        // the guide lines through the cursor, faint
        QColor l_oGuide = l_oMark;
        l_oGuide.setAlpha(90);
        l_oP.setPen(QPen(l_oGuide, 1, Qt::DashLine));
        const double l_dCx = std::round(m_oCursor.x()) + 0.5, l_dCy = std::round(m_oCursor.y()) + 0.5;
        l_oP.drawLine(QPointF(l_dCx, 0), QPointF(l_dCx, height()));
        l_oP.drawLine(QPointF(0, l_dCy), QPointF(width(), l_dCy));
        // x on the bottom edge, y on the left edge
        auto l_fnEdgeTag = [&](const QRectF& l_oR, const QString& l_oT) {
            l_oP.setPen(Qt::NoPen);
            l_oP.setBrush(l_oMark);
            l_oP.drawRoundedRect(l_oR, 2, 2);
            l_oP.setPen(Qt::white);
            l_oP.drawText(l_oR, Qt::AlignCenter, l_oT);
        };
        const double l_dTw = l_oFm.horizontalAdvance(l_oXs) + 8, l_dTh = l_oFm.height() + 2;
        l_fnEdgeTag(QRectF(std::clamp(l_dCx - l_dTw / 2, 0.0, width() - l_dTw), height() - l_dTh, l_dTw, l_dTh), l_oXs);
        const double l_dYw = l_oFm.horizontalAdvance(l_oYs) + 8;
        l_fnEdgeTag(QRectF(0, std::clamp(l_dCy - l_dTh / 2, 0.0, height() - l_dTh), l_dYw, l_dTh), l_oYs);
        // next to the cursor (kept inside the canvas)
        const QString l_oBoth = l_oXs + QStringLiteral(", ") + l_oYs;
        const QSizeF l_oBox(l_oFm.horizontalAdvance(l_oBoth) + 10, l_oFm.height() + 4);
        QPointF l_oAt = m_oCursor + QPointF(14, 14);
        if (l_oAt.x() + l_oBox.width() > width()) l_oAt.setX(m_oCursor.x() - 14 - l_oBox.width());
        if (l_oAt.y() + l_oBox.height() > height() - l_dTh) l_oAt.setY(m_oCursor.y() - 14 - l_oBox.height());
        f_DrawLabel(l_oP, l_oAt, l_oBoth);
    }
    l_oP.restore();
}

CanvasView::CanvasView(QWidget* l_pParent)
    : QWidget(l_pParent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 150);
}

void CanvasView::f_SetZoom(double l_dZoom) { f_ZoomAround(l_dZoom, QPointF(width() / 2.0, height() / 2.0)); }

void CanvasView::f_ZoomAround(double l_dZoom, const QPointF& l_oAnchor)
{
    l_dZoom = std::clamp(l_dZoom, g_dMinZoom, g_dMaxZoom);
    if (l_dZoom == m_dZoom) return;
    const QPointF l_oC = f_ToContent(l_oAnchor);
    m_dZoom = l_dZoom;
    m_oOffset = l_oAnchor - l_oC * m_dZoom;
    update();
    emit f_ZoomChanged(m_dZoom);
}

// Zoom steps by powers of two (and halves between), so pixel art lands on whole multiples.
void CanvasView::f_ZoomIn() { f_SetZoom(std::pow(2.0, std::floor(std::log2(m_dZoom) * 2 + 1.0001) / 2)); }

void CanvasView::f_ZoomOut() { f_SetZoom(std::pow(2.0, std::ceil(std::log2(m_dZoom) * 2 - 1.0001) / 2)); }

QTransform CanvasView::f_ViewTransform() const
{
    return QTransform(m_dZoom, 0, 0, m_dZoom, m_oOffset.x(), m_oOffset.y());
}

void CanvasView::f_FitToView()
{
    const QRectF l_oCr = f_ContentRect();
    if (l_oCr.isEmpty() || width() < 40 || height() < 40) return;   // stays pending until it can fit
    m_bFitPending = false;
    const double l_dMargin = 24;
    double l_dZ = std::min((width() - 2 * l_dMargin) / l_oCr.width(), (height() - 2 * l_dMargin) / l_oCr.height());
    // Prefer an integer zoom when magnifying (crisp pixels), any value when shrinking.
    if (l_dZ > 1) l_dZ = std::floor(l_dZ);
    m_dZoom = std::clamp(l_dZ, g_dMinZoom, g_dMaxZoom);
    m_oOffset = QPointF((width() - l_oCr.width() * m_dZoom) / 2 - l_oCr.left() * m_dZoom,
                       (height() - l_oCr.height() * m_dZoom) / 2 - l_oCr.top() * m_dZoom);
    m_oOffset = QPointF(std::round(m_oOffset.x()), std::round(m_oOffset.y()));
    update();
    emit f_ZoomChanged(m_dZoom);
}

void CanvasView::f_CenterOn(const QRectF& l_oArea)
{
    const QRectF l_oW = f_ToWidget(l_oArea);
    if (rect().contains(l_oW.toAlignedRect())) return;
    m_oOffset += QPointF(width() / 2.0, height() / 2.0) - l_oW.center();
    update();
}

QPoint CanvasView::f_PixelAt(const QPointF& l_oWidgetPos) const
{
    const QPointF l_oC = f_ToContent(l_oWidgetPos);
    return QPoint(int(std::floor(l_oC.x())), int(std::floor(l_oC.y())));
}

void CanvasView::f_PaintBackground(QPainter& l_oP)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    l_oP.fillRect(rect(), l_oTc.m_oCanvasBackground);
    const QRectF l_oCr = f_ContentRect();
    if (l_oCr.isEmpty()) return;
    QPixmap l_oChecker(g_iCheckerCell * 2, g_iCheckerCell * 2);
    l_oChecker.fill(l_oTc.m_oCheckerLight);
    {
        QPainter l_oCp(&l_oChecker);
        l_oCp.fillRect(0, 0, g_iCheckerCell, g_iCheckerCell, l_oTc.m_oCheckerDark);
        l_oCp.fillRect(g_iCheckerCell, g_iCheckerCell, g_iCheckerCell, g_iCheckerCell, l_oTc.m_oCheckerDark);
    }
    const QRectF l_oArea = f_ToWidget(l_oCr);
    l_oP.fillRect(l_oArea, QBrush(l_oChecker));
    l_oP.setPen(QPen(l_oTc.m_oPageBorder, 1));
    l_oP.setBrush(Qt::NoBrush);
    l_oP.drawRect(l_oArea.adjusted(-0.5, -0.5, 0.5, 0.5));
}

void CanvasView::f_DrawLabel(QPainter& l_oP, const QPointF& l_oAt, const QString& l_oText)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    const QFontMetrics l_oFm(l_oP.font());
    const QRectF l_oR(l_oAt, QSizeF(l_oFm.horizontalAdvance(l_oText) + 10, l_oFm.height() + 4));
    l_oP.setPen(Qt::NoPen);
    l_oP.setBrush(l_oTc.m_oLabelBackground);
    l_oP.drawRoundedRect(l_oR, 3, 3);
    l_oP.setPen(l_oTc.m_oLabel);
    l_oP.drawText(l_oR, Qt::AlignCenter, l_oText);
}

bool CanvasView::f_PanPress(QMouseEvent* l_pE)
{
    if (l_pE->button() == Qt::RightButton) {   // a pan once it moves; a plain click stays a context menu
        m_bRightDown = true;
        m_oRightStart = l_pE->position();
        m_bEatContextMenu = false;
        return true;
    }
    if (l_pE->button() == Qt::MiddleButton || (l_pE->button() == Qt::LeftButton && m_bSpaceDown)) {
        m_bPanning = true;
        m_oPanLast = l_pE->position();
        setCursor(Qt::ClosedHandCursor);
        return true;
    }
    return false;
}

bool CanvasView::f_PanMove(QMouseEvent* l_pE)
{
    if (m_bRightDown && !m_bPanning && (l_pE->buttons() & Qt::RightButton)) {
        if ((l_pE->position() - m_oRightStart).manhattanLength() < 4) return true;
        m_bPanning = true;
        m_bEatContextMenu = true;
        m_oPanLast = m_oRightStart;
        setCursor(Qt::ClosedHandCursor);
    }
    if (!m_bPanning) return false;
    m_oOffset += l_pE->position() - m_oPanLast;
    m_oPanLast = l_pE->position();
    update();
    return true;
}

bool CanvasView::f_PanRelease(QMouseEvent* l_pE)
{
    if (l_pE->button() == Qt::RightButton && m_bRightDown) {
        m_bRightDown = false;
        if (!m_bPanning) return false;   // a click: the context menu follows as usual
        m_bPanning = false;
        setCursor(m_bSpaceDown ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return true;
    }
    if (!m_bPanning || (l_pE->button() != Qt::MiddleButton && l_pE->button() != Qt::LeftButton)) return false;
    m_bPanning = false;
    setCursor(m_bSpaceDown ? Qt::OpenHandCursor : Qt::ArrowCursor);
    return true;
}

bool CanvasView::event(QEvent* l_pE)
{
    if (l_pE->type() == QEvent::MouseMove || l_pE->type() == QEvent::Leave) {   // the cursor readout (paintCoordinates)
        const bool l_bIn = l_pE->type() == QEvent::MouseMove;
        if (l_bIn) m_oCursor = static_cast<QMouseEvent*>(l_pE)->position();
        if (l_bIn != m_bCursorIn || l_bIn) {
            m_bCursorIn = l_bIn;
            if (f_CoordinatesShown()) update();
        }
    }
    if (l_pE->type() == QEvent::ContextMenu && m_bEatContextMenu) {
        m_bEatContextMenu = false;
        l_pE->accept();
        return true;
    }
    return QWidget::event(l_pE);
}

void CanvasView::wheelEvent(QWheelEvent* l_pE)
{
    const double l_dSteps = l_pE->angleDelta().y() / 120.0;
    if (l_dSteps == 0) return;
    f_ZoomAround(m_dZoom * std::pow(1.25, l_dSteps), l_pE->position());
    l_pE->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* l_pE)
{
    if (l_pE->key() == Qt::Key_Space && !l_pE->isAutoRepeat()) {
        m_bSpaceDown = true;
        if (!m_bPanning) setCursor(Qt::OpenHandCursor);
        return;
    }
    QWidget::keyPressEvent(l_pE);
}

void CanvasView::keyReleaseEvent(QKeyEvent* l_pE)
{
    if (l_pE->key() == Qt::Key_Space && !l_pE->isAutoRepeat()) {
        m_bSpaceDown = false;
        if (!m_bPanning) unsetCursor();
        return;
    }
    QWidget::keyReleaseEvent(l_pE);
}

void CanvasView::resizeEvent(QResizeEvent* l_pE)
{
    QWidget::resizeEvent(l_pE);
    if (m_bFitPending) f_FitToView();
}

void CanvasView::paintEvent(QPaintEvent*)
{
    if (m_bFitPending && !f_ContentRect().isEmpty()) f_FitToView();
    QPainter l_oP(this);
    f_PaintBackground(l_oP);
    f_PaintContent(l_oP);
    f_PaintCoordinates(l_oP);
}
