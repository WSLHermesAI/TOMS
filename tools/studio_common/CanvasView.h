#pragma once

#include <QTransform>
#include <QVector>
#include <QWidget>

// Zoom/pan base for the editors' canvases (tools/studio_common: shared by the atlas and anim
// editors). Content is in content pixels -- by default an image with (0,0) top-left, or any
// rectangle a subclass returns from contentRect() (the anim viewport centres its origin); the view
// maps it to the widget with a uniform zoom and an offset. Wheel zooms around the cursor; dragging
// with the right button (a right click without moving still opens the context menu), the middle
// button, or Space + left button, pans. Subclasses paint in widget coordinates using
// toWidget()/toContent() so outlines stay one screen pixel wide at every zoom.
class QAction;
class QMenu;

class CanvasView : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget* parent = nullptr);

    double zoom() const { return m_zoom; }
    QPointF contentToWidget(const QPointF& c) const { return toWidget(c); }   // (tests: did the view pan?)
    void setZoom(double zoom);   // around the widget centre

    // Coordinate hints, as in Cocos Creator's scene view: faint lines every "step" content pixels,
    // with the x values along the bottom edge and the y values along the left edge. The step follows
    // the zoom (1, 2, 5 x 10^n content pixels, at least kCoordinateSpacing screen pixels apart): every
    // 500 pixels zoomed out, every 10 or every pixel zoomed in. One switch for every canvas of the
    // editor (View > Show Coordinates, remembered in the editor's settings).
    static constexpr double kCoordinateSpacing = 90;   // screen pixels between two hints, at least
    static double coordinateStep(double zoom);          // content pixels between two hints
    static bool coordinatesShown();
    static void setCoordinatesShown(bool on);           // repaints every canvas
    static QAction* addCoordinatesAction(QMenu* menu);  // the View menu's checkable toggle

public slots:
    void zoomIn();
    void zoomOut();
    void fitToView();
    void centerOn(const QRectF& area);   // scrolls (no zoom change) when it is out of view

signals:
    void zoomChanged(double zoom);

protected:
    virtual QSize contentSize() const = 0;
    // The content area (fit, checkerboard): (0,0)..contentSize() unless a subclass says otherwise.
    virtual QRectF contentRect() const { return QRectF(QPointF(0, 0), QSizeF(contentSize())); }
    // Content -> widget, for painting content with QPainter::setTransform.
    QTransform viewTransform() const;

    QPointF toContent(const QPointF& w) const { return (w - m_offset) / m_zoom; }
    QPointF toWidget(const QPointF& c) const { return c * m_zoom + m_offset; }
    QRectF toWidget(const QRectF& c) const { return QRectF(toWidget(c.topLeft()), c.size() * m_zoom); }
    QPoint pixelAt(const QPointF& widgetPos) const;   // content pixel under a widget point (floor)
    // Fit the next time the content is shown (after loading a new image or entering a mode).
    void requestFit() { m_fitPending = true; update(); }
    // For subclasses that paint without QPainter (a bgfx viewport): runs a pending fit, as paintEvent does.
    void preparePaint() { if (m_fitPending && !contentRect().isEmpty()) fitToView(); }

    // Fills the widget, then the checkerboard where the content is.
    void paintBackground(QPainter& p);
    // Pan handling for subclasses: call first from the mouse handlers; true = consumed.
    bool panPress(QMouseEvent* e);
    bool panMove(QMouseEvent* e);
    bool panRelease(QMouseEvent* e);
    bool isPanning() const { return m_panning; }
    // The coordinate hints over everything else (paintEvent and the game renderer's overlay call it).
    void paintCoordinates(QPainter& p);
    // Draws a small text box (sizes, coordinates) at a widget position.
    static void drawLabel(QPainter& p, const QPointF& at, const QString& text);

    bool event(QEvent* e) override;   // drops the context menu that ends a right-button pan
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent* e) override;   // runs a pending fit, then calls paintContent()
    virtual void paintContent(QPainter& p) = 0;

private:
    void zoomAround(double zoom, const QPointF& anchor);

    double m_zoom = 1.0;
    QPointF m_offset{20, 20};
    bool m_fitPending = true;
    bool m_spaceDown = false;
    bool m_panning = false;
    QPointF m_panLast;
    bool m_rightDown = false;        // right button held: becomes a pan once it moves a few pixels
    QPointF m_rightStart;
    bool m_eatContextMenu = false;   // the right button panned: its release is not a click
};
