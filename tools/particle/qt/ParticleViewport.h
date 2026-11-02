#pragma once

#include "GameCanvasView.h"
#include "render_iface.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>

#include <vector>

class ParticleDocument;
class ParticlePlayback;

// The preview: the playing effect drawn by the game's own renderer (BgfxRenderer in a native window,
// the same sprite batch, textures and GPU particle simulation as toms_game), over a checkerboard, a
// solid colour or a picture. Where bgfx cannot start (no GPU, the offscreen selftest) it falls back
// to QPainter: the runtime's quads, each an affine image of its sprite, tinted; additive =
// CompositionMode_Plus (GPU-simulated emitters then simulate on the CPU). The selected emitter has a gizmo:
//   centre square   drag: move the emitter (its offset)
//   arrow           drag: direction (Shift: 15 degree steps)
//   arc ends        drag: spread
//   round handle    drag: the shape's size (radius, box corner, line end)
// Left click on empty space fires the effect there (it plays again from that point); Ctrl+drag
// moves it while it plays (world-space particles stay behind); double-click puts it back at 0,0.
// Right-drag / middle / Space+drag pans, the wheel zooms. Sprites dropped from the Sprites dock
// become a new emitter there (several = a flipbook).
class ParticleViewport : public GameCanvasView
{
    Q_OBJECT

public:
    enum class Background { Checker, Dark, Light, Image };

    // gameRenderer: draw with the game's renderer (bgfx); false = QPainter.
    ParticleViewport(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, bool l_bGameRenderer, QWidget* l_pParent = nullptr);
    ~ParticleViewport() override;

    void f_SetBackground(Background l_eB);
    Background f_Background() const { return m_eBackground; }
    bool f_SetBackgroundImage(const QString& l_oPath);   // also switches to Background::Image
    QString f_BackgroundImagePath() const { return m_oBgPath; }
    void f_SetGridVisible(bool l_bOn) { m_bGrid = l_bOn; update(); }
    bool f_GridVisible() const { return m_bGrid; }
    void f_SetGizmoVisible(bool l_bOn) { m_bGizmo = l_bOn; update(); }

    // Paints quads as the game would, `view` mapping content to the painter (the headless
    // renderer uses this too).
    static void f_DrawQuads(QPainter& l_oP, const std::vector<Quad>& l_vQuads, const ParticleDocument& l_oDoc, const QTransform& l_oView);
    static int f_Batches(const std::vector<Quad>& l_vQuads);   // texture / blend changes + 1

    // For the selftest: where a gizmo handle is now (widget coordinates; invalid = not shown).
    enum class Handle { None, Move, Direction, SpreadA, SpreadB, Shape };
    QPointF f_HandlePos(Handle l_eH) const;
    const std::vector<Quad>& f_LastQuads() const { return m_vQuads; }

signals:
    void f_SpritesDropped(const QStringList& l_oRefs, const QPointF& l_oEffectPos);

protected:
    // A 320x240 stage around the origin: fits at 2x, near the game's pixel scale. Effects may spill out.
    QSize f_ContentSize() const override { return QSize(320, 240); }
    QRectF f_ContentRect() const override { return QRectF(-160, -120, 320, 240); }
    void f_PaintContent(QPainter& l_oP) override;
    // The game renderer (ParticleViewportGpu.cpp): background, grid, origin and the effect, as the game draws it.
    void f_PaintGameScene(const GameScene& l_oS) override;
    void f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen) override;
    bool f_DrawCheckerboard() const override { return m_eBackground == Background::Checker; }
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void mouseDoubleClickEvent(QMouseEvent* l_pE) override;
    void dragEnterEvent(QDragEnterEvent* l_pE) override;
    void dragMoveEvent(QDragMoveEvent* l_pE) override;
    void dropEvent(QDropEvent* l_pE) override;

private:
    QPointF f_EmitterPos() const;          // content coordinates of the selected emitter
    Handle f_HitHandle(const QPointF& l_oWidgetPos) const;
    void f_DragTo(const QPointF& l_oContentPos, Qt::KeyboardModifiers l_oMods);
    void f_PaintGizmo(QPainter& l_oP);
    QString f_StatusText() const;

    ParticleDocument* m_pDoc;
    ParticlePlayback* m_pPlay;
    std::vector<Quad> m_vQuads;
    Background m_eBackground = Background::Checker;
    QImage m_oBgImage;
    QString m_oBgPath;
    bool m_bGrid = false;
    bool m_bGizmo = true;

    Handle m_eDrag = Handle::None;
    Handle m_eHover = Handle::None;
    bool m_bMovingOrigin = false;
    bool m_bPressedEmpty = false;
    QPointF m_oPressWidget, m_oPressContent, m_oStartOffset, m_oStartOrigin;
    int m_iGesture = 0;                   // merge key serial: one undo step per drag

    // Game renderer mode.
    GameTextures m_oTex;                  // the document's atlases and the background picture, as GPU textures
    bool m_bAtlasDirty = true;
    QPointer<ParticlePlayback> m_oPlayGuard;   // the playback may go first at shutdown
    QElapsedTimer m_oFpsClock;
    int m_iFpsFrames = 0;
    double m_dFps = 0;
};
