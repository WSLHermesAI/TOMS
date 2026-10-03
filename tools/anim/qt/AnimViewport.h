#pragma once

#include "AnimDocument.h"
#include "CanvasView.h"
#include "render_iface.h"

#include <QTransform>

#include <vector>

// The clip at the playhead, drawn with QPainter from the same evaluate() + appendQuads() the game
// uses (anim_player.h), so it shows exactly what the game draws. Content coordinates are clip
// space (pixels, y down, origin = where the game places the clip). Click selects the topmost
// node under the cursor; the selected node gets a Move (W) / Rotate (E) / Scale (R) gizmo whose
// drags go through AnimDocument::setChannel (auto-key on: a key at the playhead; off: rest value).
// Sprites dragged in from the Sprites dock become nodes under the selected node.
class AnimViewport : public CanvasView
{
    Q_OBJECT

public:
    enum class Tool { Move, Rotate, Scale };

    // One sprite quad as drawn, and where it came from.
    struct DrawItem {
        Quad quad;
        NodePath path;
        const SpriteImageCache* images = nullptr;   // the atlas the quad's texture is a page of
        int page = 0;
        QRect src;          // the pixels on the page (from the quad's uv)
    };

    explicit AnimViewport(AnimDocument* doc, QWidget* parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool t);
    void setSnap(bool on) { m_snap = on; }
    bool snap() const { return m_snap; }
    void setShowGrid(bool on);

    // What the viewport draws at time t of the current clip, in clip space, in draw order (one
    // appendQuads() call per pose, so every quad knows its node). Public for the selftest.
    void buildFrame(float t, std::vector<toms::anim::NodePose>& poses, std::vector<DrawItem>& items,
                    std::vector<std::string>* missing) const;
    // The QPainter transform that maps the sprite's pixels (0,0)..(w,h) onto the quad's corners.
    static QTransform quadTransform(const Quad& q, const QSizeF& srcSize);
    QStringList missingSprites() const { return m_missing; }
    QPointF widgetPos(const QPointF& content) const { return toWidget(content); }

public slots:
    void frameClip();   // fit the stage (all poses of the clip) into the view

signals:
    void toolChanged(AnimViewport::Tool t);

protected:
    QSize contentSize() const override;
    QRectF contentRect() const override { return m_stage; }
    void paintContent(QPainter& p) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    enum class Drag { None, MoveFree, MoveX, MoveY, Rotate, ScaleX, ScaleY, ScaleUniform };
    struct Gizmo {
        bool valid = false;
        QPointF origin;              // widget
        QPointF parentX, parentY;    // unit directions of the parent's axes (widget)
        QPointF nodeX, nodeY;        // unit directions of the node's own axes
        double parentDet = 1;        // < 0: the parent mirrors
        QTransform parentToContent;  // parent space -> clip space
    };

    void updateStage();
    void rebuild();
    Gizmo gizmo() const;
    Drag handleAt(const QPointF& w, const Gizmo& g) const;
    int itemAt(const QPointF& w) const;   // index into m_items, -1 = none
    QPointF parentPointAt(const NodePath& parent, const QPointF& contentPos) const;
    void applyDrag(const QPointF& w, Qt::KeyboardModifiers mods);
    void drawGizmo(QPainter& p, const Gizmo& g) const;

    AnimDocument* m_doc;
    Tool m_tool = Tool::Move;
    bool m_snap = false;
    bool m_grid = true;
    QRectF m_stage{-128, -128, 256, 256};
    std::vector<toms::anim::NodePose> m_poses;
    std::vector<DrawItem> m_items;
    QStringList m_missing;
    int m_hover = -1;

    Drag m_drag = Drag::None;
    QPointF m_dragStart;          // widget
    Gizmo m_dragGizmo;
    animed::Value m_startPos, m_startRot, m_startScale;
    double m_lastAngle = 0, m_angleSum = 0;
    int m_dragSerial = 0;
};
