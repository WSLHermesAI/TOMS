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
    explicit CanvasView(QWidget* l_pParent = nullptr);

    double f_Zoom() const { return m_dZoom; }
    QPointF f_ContentToWidget(const QPointF& l_oC) const { return f_ToWidget(l_oC); }   // (tests: did the view pan?)
    void f_SetZoom(double l_dZoom);   // around the widget centre

    // Coordinate hints, as in Cocos Creator's scene view: faint lines every "step" content pixels,
    // with the x values along the bottom edge and the y values along the left edge. The step follows
    // the zoom (1, 2, 5 x 10^n content pixels, at least kCoordinateSpacing screen pixels apart): every
    // 500 pixels zoomed out, every 10 or every pixel zoomed in. One switch for every canvas of the
    // editor (View > Show Coordinates, remembered in the editor's settings). With it on, the cursor's position is
    // shown too: its x / y next to it, and marked on the two edges among the values.
    static constexpr double m_dCoordinateSpacing = 90;   // screen pixels between two hints, at least
    static double f_CoordinateStep(double l_dZoom);          // content pixels between two hints
    static bool f_CoordinatesShown();
    static void f_SetCoordinatesShown(bool l_bOn);           // repaints every canvas
    static QAction* f_AddCoordinatesAction(QMenu* l_pMenu);  // the View menu's checkable toggle

public slots:
    void f_ZoomIn();
    void f_ZoomOut();
    void f_FitToView();
    void f_CenterOn(const QRectF& l_oArea);   // scrolls (no zoom change) when it is out of view

signals:
    void f_ZoomChanged(double l_dZoom);

protected:
    virtual QSize f_ContentSize() const = 0;
    // The content area (fit, checkerboard): (0,0)..contentSize() unless a subclass says otherwise.
    virtual QRectF f_ContentRect() const { return QRectF(QPointF(0, 0), QSizeF(f_ContentSize())); }
    // Content -> widget, for painting content with QPainter::setTransform.
    QTransform f_ViewTransform() const;

    QPointF f_ToContent(const QPointF& l_oW) const { return (l_oW - m_oOffset) / m_dZoom; }
    QPointF f_ToWidget(const QPointF& l_oC) const { return l_oC * m_dZoom + m_oOffset; }
    QRectF f_ToWidget(const QRectF& l_oC) const { return QRectF(f_ToWidget(l_oC.topLeft()), l_oC.size() * m_dZoom); }
    QPoint f_PixelAt(const QPointF& l_oWidgetPos) const;   // content pixel under a widget point (floor)
    // Fit the next time the content is shown (after loading a new image or entering a mode).
    void f_RequestFit() { m_bFitPending = true; update(); }
    // For subclasses that paint without QPainter (a bgfx viewport): runs a pending fit, as paintEvent does.
    void f_PreparePaint() { if (m_bFitPending && !f_ContentRect().isEmpty()) f_FitToView(); }

    // Fills the widget, then the checkerboard where the content is.
    void f_PaintBackground(QPainter& l_oP);
    // Pan handling for subclasses: call first from the mouse handlers; true = consumed.
    bool f_PanPress(QMouseEvent* l_pE);
    bool f_PanMove(QMouseEvent* l_pE);
    bool f_PanRelease(QMouseEvent* l_pE);
    bool f_IsPanning() const { return m_bPanning; }
    // The coordinate hints over everything else (paintEvent and the game renderer's overlay call it).
    void f_PaintCoordinates(QPainter& l_oP);
    // Draws a small text box (sizes, coordinates) at a widget position.
    static void f_DrawLabel(QPainter& l_oP, const QPointF& l_oAt, const QString& l_oText);

    bool event(QEvent* l_pE) override;   // drops the context menu that ends a right-button pan
    void wheelEvent(QWheelEvent* l_pE) override;
    void keyPressEvent(QKeyEvent* l_pE) override;
    void keyReleaseEvent(QKeyEvent* l_pE) override;
    void resizeEvent(QResizeEvent* l_pE) override;
    void paintEvent(QPaintEvent* l_pE) override;   // runs a pending fit, then calls paintContent()
    virtual void f_PaintContent(QPainter& l_oP) = 0;

private:
    void f_ZoomAround(double l_dZoom, const QPointF& l_oAnchor);

    double m_dZoom = 1.0;
    QPointF m_oOffset{20, 20};
    QPointF m_oCursor;                // the mouse over the canvas (widget pixels), for the coordinate readout
    bool m_bCursorIn = false;
    bool m_bFitPending = true;
    bool m_bSpaceDown = false;
    bool m_bPanning = false;
    QPointF m_oPanLast;
    bool m_bRightDown = false;        // right button held: becomes a pan once it moves a few pixels
    QPointF m_oRightStart;
    bool m_bEatContextMenu = false;   // the right button panned: its release is not a click
};
