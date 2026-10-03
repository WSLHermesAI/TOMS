#pragma once

#include "CanvasView.h"

#include <QImage>
#include <QRect>
#include <QVector>

class AtlasDocument;

// Sprite edit mode: one sprite alone at its original size, with its child sprites.
//   - drag on empty pixels: draw a new child rect (asks for the name)
//   - click a child: select it; drag it to move, drag one of its 8 handles to resize
//   - drag the pivot marker; drag the 9-slice guides (when the sprite has 9-slice borders)
//   - arrow keys nudge the selected child (Shift: 10 px); double-click a child to edit inside it;
//     Esc goes back up to the parent, and from an image sprite back to the atlas
// Every coordinate snaps to whole pixels (the pivot to half pixels) and is committed on release
// as one undo step.
class SpriteEditCanvas : public CanvasView
{
    Q_OBJECT

public:
    explicit SpriteEditCanvas(AtlasDocument* doc, QWidget* parent = nullptr);

    QString target() const { return m_target; }
    void setTarget(const QString& name);

signals:
    void targetChanged(const QString& name);
    void exitRequested();
    void contextMenuRequested(const QPoint& globalPos);

protected:
    QSize contentSize() const override { return m_image.size(); }
    void paintContent(QPainter& p) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    struct Child {
        QString name;
        QRect rect;   // in the target's pixels
    };

    void reload();
    QVector<Child> children() const;
    // The sprite whose pivot and 9-slice are shown: the current child, or the target itself.
    QString focus() const;
    QRect focusRect() const;
    QPointF pivotPoint(const QString& name, const QRect& r) const;   // content coordinates
    int childAt(const QVector<Child>& kids, const QPointF& c) const;
    int handleAt(const QRect& r, const QPointF& widgetPos) const;     // 0..7 clockwise from top-left, -1
    int splitAt(const QPointF& widgetPos) const;                       // 0 left, 1 right, 2 top, 3 bottom, -1
    QPointF handlePos(const QRect& r, int handle) const;              // widget coordinates
    QRect clampToImage(QRect r) const;
    void updateCursor(const QPointF& widgetPos);
    void commitDrag();
    void nudge(int dx, int dy);

    enum class Drag { None, Create, Move, Resize, Pivot, Split };

    AtlasDocument* m_doc;
    QString m_target;
    QImage m_image;

    Drag m_drag = Drag::None;
    QString m_dragName;
    QPointF m_pressContent;
    QRect m_startRect, m_liveRect;
    int m_handle = -1;
    int m_splitSide = -1;
    int m_liveSplit[4] = {0, 0, 0, 0};
    QPointF m_livePivot;   // 0..1 of the focus rect
};
