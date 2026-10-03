#pragma once

#include <QTransform>
#include <QWidget>

// Zoom/pan base for the editors' canvases (tools/studio_common: shared by the atlas and anim
// editors). Content is in content pixels -- by default an image with (0,0) top-left, or any
// rectangle a subclass returns from contentRect() (the anim viewport centres its origin); the view
// maps it to the widget with a uniform zoom and an offset. Wheel zooms around the cursor; the middle
// button, or Space + left button, pans. Subclasses paint in widget coordinates using
// toWidget()/toContent() so outlines stay one screen pixel wide at every zoom.
class CanvasView : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget* parent = nullptr);

    double zoom() const { return m_zoom; }
    void setZoom(double zoom);   // around the widget centre

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

    // Fills the widget, then the checkerboard where the content is.
    void paintBackground(QPainter& p);
    // Pan handling for subclasses: call first from the mouse handlers; true = consumed.
    bool panPress(QMouseEvent* e);
    bool panMove(QMouseEvent* e);
    bool panRelease(QMouseEvent* e);
    bool isPanning() const { return m_panning; }
    // Draws a small text box (sizes, coordinates) at a widget position.
    static void drawLabel(QPainter& p, const QPointF& at, const QString& text);

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
};
