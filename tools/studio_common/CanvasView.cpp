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
constexpr double kMinZoom = 1.0 / 32, kMaxZoom = 64.0;
constexpr int kCheckerCell = 8;   // screen pixels: the pattern does not scale with the zoom
const QString kCoordinatesKey = QStringLiteral("view/coordinates");

bool& coordinatesFlag()
{
    static bool on = QSettings().value(kCoordinatesKey, true).toBool();
    return on;
}

QString coordinateText(double v, double step)
{
    if (std::fabs(v) < step * 1e-6) return QStringLiteral("0");
    return QString::number(std::lround(v));   // steps are whole content pixels
}
}

double CanvasView::coordinateStep(double zoom)
{
    // The smallest 1/2/5 x 10^n (at least one content pixel) that keeps the hints apart on screen.
    const double want = kCoordinateSpacing / std::max(zoom, 1e-6);
    double base = 1;
    while (base * 10 <= want) base *= 10;
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (base * m >= want) return base * m;
    return base * 10;
}

bool CanvasView::coordinatesShown() { return coordinatesFlag(); }

void CanvasView::setCoordinatesShown(bool on)
{
    coordinatesFlag() = on;
    QSettings().setValue(kCoordinatesKey, on);
    for (QWidget* w : QApplication::allWidgets())
        if (auto* view = qobject_cast<CanvasView*>(w)) view->update();
}

QAction* CanvasView::addCoordinatesAction(QMenu* menu)
{
    QAction* a = menu->addAction(tr("Show Coordinates"));
    a->setCheckable(true);
    a->setChecked(coordinatesShown());
    a->setToolTip(tr("X/Y values along the canvas edges; the step follows the zoom"));
    connect(a, &QAction::toggled, a, [](bool on) { setCoordinatesShown(on); });
    return a;
}

void CanvasView::paintCoordinates(QPainter& p)
{
    if (!coordinatesShown()) return;
    const Theme::Colors& tc = Theme::colors();
    const double step = coordinateStep(m_zoom);
    const QPointF c0 = toContent(QPointF(0, 0)), c1 = toContent(QPointF(width(), height()));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QFont f = p.font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    p.setFont(f);
    const QFontMetrics fm(f);
    QColor line = tc.grid;
    line.setAlpha(std::min(255, line.alpha() * 2));
    QColor text = tc.label;
    text.setAlpha(170);
    const double bottom = height() - fm.descent() - 3;   // x values sit on the bottom edge
    const double leftReserve = fm.horizontalAdvance(QStringLiteral("-00000")) + 6;   // the y column's corner

    // Vertical lines, x values along the bottom.
    for (double x = std::ceil(c0.x() / step) * step; x <= c1.x(); x += step) {
        const double wx = std::round(toWidget(QPointF(x, 0)).x()) + 0.5;
        p.setPen(QPen(line, 1));
        p.drawLine(QPointF(wx, 0), QPointF(wx, height()));
        if (wx < leftReserve) continue;   // the corner belongs to the y values
        p.setPen(text);
        p.drawText(QPointF(wx + 3, bottom), coordinateText(x, step));
    }
    // Horizontal lines, y values along the left edge (above each line).
    for (double y = std::ceil(c0.y() / step) * step; y <= c1.y(); y += step) {
        const double wy = std::round(toWidget(QPointF(0, y)).y()) + 0.5;
        p.setPen(QPen(line, 1));
        p.drawLine(QPointF(0, wy), QPointF(width(), wy));
        if (wy > height() - fm.height() - 4) continue;   // the corner belongs to the x values
        p.setPen(text);
        p.drawText(QPointF(4, wy - 3), coordinateText(y, step));
    }
    p.restore();
}

CanvasView::CanvasView(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 150);
}

void CanvasView::setZoom(double zoom) { zoomAround(zoom, QPointF(width() / 2.0, height() / 2.0)); }

void CanvasView::zoomAround(double zoom, const QPointF& anchor)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom == m_zoom) return;
    const QPointF c = toContent(anchor);
    m_zoom = zoom;
    m_offset = anchor - c * m_zoom;
    update();
    emit zoomChanged(m_zoom);
}

// Zoom steps by powers of two (and halves between), so pixel art lands on whole multiples.
void CanvasView::zoomIn() { setZoom(std::pow(2.0, std::floor(std::log2(m_zoom) * 2 + 1.0001) / 2)); }

void CanvasView::zoomOut() { setZoom(std::pow(2.0, std::ceil(std::log2(m_zoom) * 2 - 1.0001) / 2)); }

QTransform CanvasView::viewTransform() const
{
    return QTransform(m_zoom, 0, 0, m_zoom, m_offset.x(), m_offset.y());
}

void CanvasView::fitToView()
{
    const QRectF cr = contentRect();
    if (cr.isEmpty() || width() < 40 || height() < 40) return;   // stays pending until it can fit
    m_fitPending = false;
    const double margin = 24;
    double z = std::min((width() - 2 * margin) / cr.width(), (height() - 2 * margin) / cr.height());
    // Prefer an integer zoom when magnifying (crisp pixels), any value when shrinking.
    if (z > 1) z = std::floor(z);
    m_zoom = std::clamp(z, kMinZoom, kMaxZoom);
    m_offset = QPointF((width() - cr.width() * m_zoom) / 2 - cr.left() * m_zoom,
                       (height() - cr.height() * m_zoom) / 2 - cr.top() * m_zoom);
    m_offset = QPointF(std::round(m_offset.x()), std::round(m_offset.y()));
    update();
    emit zoomChanged(m_zoom);
}

void CanvasView::centerOn(const QRectF& area)
{
    const QRectF w = toWidget(area);
    if (rect().contains(w.toAlignedRect())) return;
    m_offset += QPointF(width() / 2.0, height() / 2.0) - w.center();
    update();
}

QPoint CanvasView::pixelAt(const QPointF& widgetPos) const
{
    const QPointF c = toContent(widgetPos);
    return QPoint(int(std::floor(c.x())), int(std::floor(c.y())));
}

void CanvasView::paintBackground(QPainter& p)
{
    const Theme::Colors& tc = Theme::colors();
    p.fillRect(rect(), tc.canvasBackground);
    const QRectF cr = contentRect();
    if (cr.isEmpty()) return;
    QPixmap checker(kCheckerCell * 2, kCheckerCell * 2);
    checker.fill(tc.checkerLight);
    {
        QPainter cp(&checker);
        cp.fillRect(0, 0, kCheckerCell, kCheckerCell, tc.checkerDark);
        cp.fillRect(kCheckerCell, kCheckerCell, kCheckerCell, kCheckerCell, tc.checkerDark);
    }
    const QRectF area = toWidget(cr);
    p.fillRect(area, QBrush(checker));
    p.setPen(QPen(tc.pageBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(area.adjusted(-0.5, -0.5, 0.5, 0.5));
}

void CanvasView::drawLabel(QPainter& p, const QPointF& at, const QString& text)
{
    const Theme::Colors& tc = Theme::colors();
    const QFontMetrics fm(p.font());
    const QRectF r(at, QSizeF(fm.horizontalAdvance(text) + 10, fm.height() + 4));
    p.setPen(Qt::NoPen);
    p.setBrush(tc.labelBackground);
    p.drawRoundedRect(r, 3, 3);
    p.setPen(tc.label);
    p.drawText(r, Qt::AlignCenter, text);
}

bool CanvasView::panPress(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {   // a pan once it moves; a plain click stays a context menu
        m_rightDown = true;
        m_rightStart = e->position();
        m_eatContextMenu = false;
        return true;
    }
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_spaceDown)) {
        m_panning = true;
        m_panLast = e->position();
        setCursor(Qt::ClosedHandCursor);
        return true;
    }
    return false;
}

bool CanvasView::panMove(QMouseEvent* e)
{
    if (m_rightDown && !m_panning && (e->buttons() & Qt::RightButton)) {
        if ((e->position() - m_rightStart).manhattanLength() < 4) return true;
        m_panning = true;
        m_eatContextMenu = true;
        m_panLast = m_rightStart;
        setCursor(Qt::ClosedHandCursor);
    }
    if (!m_panning) return false;
    m_offset += e->position() - m_panLast;
    m_panLast = e->position();
    update();
    return true;
}

bool CanvasView::panRelease(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton && m_rightDown) {
        m_rightDown = false;
        if (!m_panning) return false;   // a click: the context menu follows as usual
        m_panning = false;
        setCursor(m_spaceDown ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return true;
    }
    if (!m_panning || (e->button() != Qt::MiddleButton && e->button() != Qt::LeftButton)) return false;
    m_panning = false;
    setCursor(m_spaceDown ? Qt::OpenHandCursor : Qt::ArrowCursor);
    return true;
}

bool CanvasView::event(QEvent* e)
{
    if (e->type() == QEvent::ContextMenu && m_eatContextMenu) {
        m_eatContextMenu = false;
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void CanvasView::wheelEvent(QWheelEvent* e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) return;
    zoomAround(m_zoom * std::pow(1.25, steps), e->position());
    e->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = true;
        if (!m_panning) setCursor(Qt::OpenHandCursor);
        return;
    }
    QWidget::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = false;
        if (!m_panning) unsetCursor();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void CanvasView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_fitPending) fitToView();
}

void CanvasView::paintEvent(QPaintEvent*)
{
    if (m_fitPending && !contentRect().isEmpty()) fitToView();
    QPainter p(this);
    paintBackground(p);
    paintContent(p);
    paintCoordinates(p);
}
