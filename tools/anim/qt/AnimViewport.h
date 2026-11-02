#pragma once

#include "AnimDocument.h"
#include "anim_weights.h"
#include "GameCanvasView.h"

#include <QHash>
#include <QPixmap>
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
    // What a click in the viewport picks (docs/engine/16_MESH_ANIMATION.md): Object = sprites (the node moves, and with it
    // the bones under it); Bones = the bones of skinned nodes (their joints and segments), never sprites -- the skin
    // is shown, and the gizmo poses the bone (Auto-Key on) or places it in the sprite (Auto-Key off: its rest pose).
    // Lattice: the selected sprite's deform lattice is shown; dragging a point keys the deform at the playhead (always a
    // key: a deform has no rest). A click away from the points picks a sprite, as Object mode.
    // Weights: the skins that the selected bone is in, coloured by how much each point follows it (blue = not, red =
    // fully); dragging paints that bone's weights (Ctrl subtracts, Shift smooths), a click on a joint picks that bone.
    enum class EditMode { Object, Bones, Lattice, Weights };

    // One sprite quad as drawn, and where it came from.
    struct DrawItem {
        Quad m_oQuad;
        NodePath m_vPath;
        const SpriteImageCache* m_pImages = nullptr;   // the atlas the quad's texture is a page of
        int m_iPage = 0;
        QRect m_oSrc;          // the pixels on the page (from the quad's uv)
    };

    AnimViewport(AnimDocument* l_pDoc, bool l_bGameRenderer, QWidget* l_pParent = nullptr);
    ~AnimViewport() override;

    Tool f_Tool() const { return m_eTool; }
    void f_SetTool(Tool l_eT);
    void f_SetSnap(bool l_bOn) { m_bSnap = l_bOn; }
    bool f_Snap() const { return m_bSnap; }
    // View > Show Skin: over every skinned node, its mesh (lines), its points coloured by the bones they follow, and
    // its bones; with a bone selected, the points shade by how much they follow that bone (docs/engine/16_MESH_ANIMATION.md).
    void f_SetShowSkin(bool l_bOn) { m_bShowSkin = l_bOn; update(); }
    bool f_ShowSkin() const { return m_bShowSkin; }
    void f_SetEditMode(EditMode l_eM);
    EditMode f_EditMode() const { return m_eMode; }
    // The bone (node name) whose joint or segment is under widget point w, nearest first; "" = none. Bones of
    // skinned nodes that are drawn (visible, solo) only.
    QString f_BoneAt(const QPointF& l_oW, bool l_bJointsOnly = false) const;
    // Weights mode: the brush, in widget pixels (its size on screen) and its strength (0..1, how far one dab moves a
    // point at its centre).
    void f_SetBrushRadius(double l_dR);
    double f_BrushRadius() const { return m_dBrushRadius; }
    void f_SetBrushStrength(double l_dS);
    double f_BrushStrength() const { return m_dBrushStrength; }
    // One dab of the weight brush at widget point w on every drawn skin the selected bone is in (one undo step per
    // stroke: `stroke` names it). False when no skin has the selected bone or no point is under the brush. Public for
    // the selftest.
    bool f_PaintWeightsAt(const QPointF& l_oW, animed::WeightBrush l_eMode, int l_iStroke);
    // The heat colour of a weight (0 blue .. 1 red), as the Weights mode shows it.
    static QColor f_WeightColor(double l_dW);
    // Lattice mode: the selected node's lattice point under widget point w, -1 = none.
    int f_LatticePointAt(const QPointF& l_oW) const;
    // The colour the skin view gives bone b of a skin (a fixed palette, repeating).
    static QColor f_BoneColor(int l_iB);

    // What the viewport draws at time t of the current clip, in clip space, in draw order (one
    // appendQuads() call per pose, so every quad knows its node). Public for the selftest.
    void f_BuildFrame(float l_fT, std::vector<toms::anim::NodePose>& l_vPoses, std::vector<DrawItem>& l_vItems,
                    std::vector<std::string>* l_pMissing) const;
    // The QPainter transform that maps the sprite's pixels (0,0)..(w,h) onto the quad's corners.
    static QTransform f_QuadTransform(const Quad& l_oQ, const QSizeF& l_oSrcSize);
    QStringList f_MissingSprites() const { return m_oMissing; }
    QPointF f_WidgetPos(const QPointF& l_oContent) const { return f_ToWidget(l_oContent); }

public slots:
    void f_FrameClip();   // fit the stage (all poses of the clip) into the view
    // A clip (and a held clip of the same time at its `hand`) at time t through the game renderer, offscreen: w x h, the
    // clip placed by `place`, over `background` (alpha 0: transparent). What File > Export Clips writes. Null without the
    // game renderer.
    QImage f_RenderClipFrame(const toms::anim::Clip& l_oClip, const toms::anim::Clip* l_pHeld, float l_fT, int l_iW, int l_iH,
                           const glm::mat3& l_mat3Place, const QColor& l_oBackground);
    const toms::anim::AtlasSet& f_AtlasSetForExport() const { return m_oTex.f_AtlasSet(); }   // as renderClipFrame draws

signals:
    void f_ToolChanged(AnimViewport::Tool l_eT);
    void f_DragStarted();   // a gizmo / sprite / bone drag begins (the editor pauses playback)
    void f_BrushChanged();  // the weight brush's size or strength

protected:
    QSize f_ContentSize() const override;
    QRectF f_ContentRect() const override { return m_oStage; }
    void f_PaintContent(QPainter& l_oP) override;
    void f_PaintGameScene(const GameScene& l_oS) override;   // grid, origin, the clip's sprites (as the game)
    void f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void leaveEvent(QEvent* l_pE) override;
    void dragEnterEvent(QDragEnterEvent* l_pE) override;
    void dragMoveEvent(QDragMoveEvent* l_pE) override;
    void dropEvent(QDropEvent* l_pE) override;

private:
    enum class Drag { None, MoveFree, MoveX, MoveY, Rotate, ScaleX, ScaleY, ScaleUniform };
    struct Gizmo {
        bool m_bValid = false;
        QPointF m_oOrigin;              // widget
        QPointF m_oParentX, m_oParentY;    // unit directions of the parent's axes (widget)
        QPointF m_oNodeX, m_oNodeY;        // unit directions of the node's own axes
        double m_dParentDet = 1;        // < 0: the parent mirrors
        QTransform m_oParentToContent;  // parent space -> clip space
    };

    void f_UpdateStage();
    void f_Rebuild();
    Gizmo f_Gizmo() const;
    Drag f_HandleAt(const QPointF& l_oW, const Gizmo& l_oG) const;
    int f_ItemAt(const QPointF& l_oW) const;   // index into m_items, -1 = none
    QPointF f_ParentPointAt(const NodePath& l_vParent, const QPointF& l_oContentPos) const;
    void f_ApplyDrag(const QPointF& l_oW, Qt::KeyboardModifiers l_oMods);
    void f_DrawGizmo(QPainter& l_oP, const Gizmo& l_oG) const;
    void f_DrawSkins(QPainter& l_oP, const QTransform& l_oView) const;
    void f_DrawLattice(QPainter& l_oP, const QTransform& l_oView) const;
    void f_DrawBrush(QPainter& l_oP) const;
    QString f_WeightsUnderCursor() const;   // Weights mode: the point nearest the cursor and the bones it follows
    bool f_SelectedLattice(toms::anim::LatticeView& l_oOut) const;   // the selected node's, placed in content space

    AnimDocument* m_pDoc;
    Tool m_eTool = Tool::Move;
    bool m_bSnap = false;
    bool m_bShowSkin = false;
    EditMode m_eMode = EditMode::Object;
    QString m_oHoverBone;   // Bones mode: the bone under the cursor
    int m_iHoverPoint = -1;   // Lattice mode: the point under the cursor
    double m_dBrushRadius = 40, m_dBrushStrength = 0.5;   // Weights mode
    QPointF m_oCursor{-1e9, -1e9};    // widget; far away = the cursor is not over the view
    bool m_bPainting = false;         // a weight stroke is under way
    animed::WeightBrush m_ePaintMode = animed::WeightBrush::Add;
    QPointF m_oLastDab;               // widget: where the stroke last painted
    int m_iDragPoint = -1;    // ... being dragged
    toms::anim::Deform m_mapDragStartDeform;   // the deform at the playhead when the drag began
    glm::mat2 m_mat2DragToSprite{1.0f};         // content -> sprite pixels (the drag's direction and scale)
    QRectF m_oStage{-128, -128, 256, 256};
    std::vector<toms::anim::NodePose> m_vPoses;
    std::vector<DrawItem> m_vItems;
    QStringList m_oMissing;
    int m_iHover = -1;

    Drag m_eDrag = Drag::None;
    QPointF m_oDragStart;          // widget
    Gizmo m_oDragGizmo;
    animed::Value m_oStartPos, m_oStartRot, m_oStartScale;
    double m_dLastAngle = 0, m_dAngleSum = 0;
    int m_iDragSerial = 0;
    void f_PrepareAtlases();             // the document's atlases (and meshes) as the renderer's textures, when changed
    const QPixmap& f_VertexDot(const QColor& l_oCol, double l_dRadius, double l_dDpr) const;   // drawSkins' cached point
    mutable QHash<quint64, QPixmap> m_oDots;
    GameTextures m_oTex;                // the document's atlases as GPU textures (game renderer)
    bool m_bAtlasDirty = true;
};
