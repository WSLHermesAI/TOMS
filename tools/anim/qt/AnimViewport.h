#pragma once

#include "AnimDocument.h"
#include "anim_weights.h"
#include "GameCanvasView.h"
#include "render_iface.h"

#include <QTransform>

#include <vector>

// The clip at the playhead, drawn by the game's own renderer (GameCanvasView: toms_game's
// BgfxRenderer) from the same evaluate() + appendQuads() the game uses (anim_player.h), so it shows
// exactly what the game draws; outlines, the gizmo and the status text are QPainter on top. Where
// the game renderer cannot start (the offscreen selftest) the quads are painted with QPainter. Content coordinates are clip
// space (pixels, y down, origin = where the game places the clip). Click selects the topmost
// node under the cursor; the selected node gets a Move (W) / Rotate (E) / Scale (R) gizmo whose
// drags go through AnimDocument::setChannel (auto-key on: a key at the playhead; off: rest value).
// Sprites dragged in from the Sprites dock become nodes under the selected node.
class AnimViewport : public GameCanvasView
{
    Q_OBJECT

public:
    enum class Tool { Move, Rotate, Scale };
    // What a click in the viewport picks (docs/34_MESH_ANIMATION.md): Object = sprites (the node moves, and with it
    // the bones under it); Bones = the bones of skinned nodes (their joints and segments), never sprites -- the skin
    // is shown, and the gizmo poses the bone (Auto-Key on) or places it in the sprite (Auto-Key off: its rest pose).
    // Lattice: the selected sprite's deform lattice is shown; dragging a point keys the deform at the playhead (always a
    // key: a deform has no rest). A click away from the points picks a sprite, as Object mode.
    // Weights: the skins that the selected bone is in, coloured by how much each point follows it (blue = not, red =
    // fully); dragging paints that bone's weights (Ctrl subtracts, Shift smooths), a click on a joint picks that bone.
    enum class EditMode { Object, Bones, Lattice, Weights };

    // One sprite quad as drawn, and where it came from.
    struct DrawItem {
        Quad quad;
        NodePath path;
        const SpriteImageCache* images = nullptr;   // the atlas the quad's texture is a page of
        int page = 0;
        QRect src;          // the pixels on the page (from the quad's uv)
    };

    AnimViewport(AnimDocument* doc, bool gameRenderer, QWidget* parent = nullptr);
    ~AnimViewport() override;

    Tool tool() const { return m_tool; }
    void setTool(Tool t);
    void setSnap(bool on) { m_snap = on; }
    bool snap() const { return m_snap; }
    // View > Show Skin: over every skinned node, its mesh (lines), its points coloured by the bones they follow, and
    // its bones; with a bone selected, the points shade by how much they follow that bone (docs/34_MESH_ANIMATION.md).
    void setShowSkin(bool on) { m_showSkin = on; update(); }
    bool showSkin() const { return m_showSkin; }
    void setEditMode(EditMode m);
    EditMode editMode() const { return m_mode; }
    // The bone (node name) whose joint or segment is under widget point w, nearest first; "" = none. Bones of
    // skinned nodes that are drawn (visible, solo) only.
    QString boneAt(const QPointF& w, bool jointsOnly = false) const;
    // Weights mode: the brush, in widget pixels (its size on screen) and its strength (0..1, how far one dab moves a
    // point at its centre).
    void setBrushRadius(double r);
    double brushRadius() const { return m_brushRadius; }
    void setBrushStrength(double s);
    double brushStrength() const { return m_brushStrength; }
    // One dab of the weight brush at widget point w on every drawn skin the selected bone is in (one undo step per
    // stroke: `stroke` names it). False when no skin has the selected bone or no point is under the brush. Public for
    // the selftest.
    bool paintWeightsAt(const QPointF& w, animed::WeightBrush mode, int stroke);
    // The heat colour of a weight (0 blue .. 1 red), as the Weights mode shows it.
    static QColor weightColor(double w);
    // Lattice mode: the selected node's lattice point under widget point w, -1 = none.
    int latticePointAt(const QPointF& w) const;
    // The colour the skin view gives bone b of a skin (a fixed palette, repeating).
    static QColor boneColor(int b);

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
    void dragStarted();   // a gizmo / sprite / bone drag begins (the editor pauses playback)
    void brushChanged();  // the weight brush's size or strength

protected:
    QSize contentSize() const override;
    QRectF contentRect() const override { return m_stage; }
    void paintContent(QPainter& p) override;
    void paintGameScene(const GameScene& s) override;   // grid, origin, the clip's sprites (as the game)
    void gameRendererChanged(toms::next::BgfxRenderer* ren) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
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
    void drawSkins(QPainter& p, const QTransform& view) const;
    void drawLattice(QPainter& p, const QTransform& view) const;
    void drawBrush(QPainter& p) const;
    QString weightsUnderCursor() const;   // Weights mode: the point nearest the cursor and the bones it follows
    bool selectedLattice(toms::anim::LatticeView& out) const;   // the selected node's, placed in content space

    AnimDocument* m_doc;
    Tool m_tool = Tool::Move;
    bool m_snap = false;
    bool m_showSkin = false;
    EditMode m_mode = EditMode::Object;
    QString m_hoverBone;   // Bones mode: the bone under the cursor
    int m_hoverPoint = -1;   // Lattice mode: the point under the cursor
    double m_brushRadius = 40, m_brushStrength = 0.5;   // Weights mode
    QPointF m_cursor{-1e9, -1e9};    // widget; far away = the cursor is not over the view
    bool m_painting = false;         // a weight stroke is under way
    animed::WeightBrush m_paintMode = animed::WeightBrush::Add;
    QPointF m_lastDab;               // widget: where the stroke last painted
    int m_dragPoint = -1;    // ... being dragged
    toms::anim::Deform m_dragStartDeform;   // the deform at the playhead when the drag began
    glm::mat2 m_dragToSprite{1.0f};         // content -> sprite pixels (the drag's direction and scale)
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
    GameTextures m_tex;                // the document's atlases as GPU textures (game renderer)
    bool m_atlasDirty = true;
};
