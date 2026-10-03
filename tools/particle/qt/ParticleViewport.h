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
    ParticleViewport(ParticleDocument* doc, ParticlePlayback* playback, bool gameRenderer, QWidget* parent = nullptr);
    ~ParticleViewport() override;

    void setBackground(Background b);
    Background background() const { return m_background; }
    bool setBackgroundImage(const QString& path);   // also switches to Background::Image
    QString backgroundImagePath() const { return m_bgPath; }
    void setGridVisible(bool on) { m_grid = on; update(); }
    bool gridVisible() const { return m_grid; }
    void setGizmoVisible(bool on) { m_gizmo = on; update(); }

    // Paints quads as the game would, `view` mapping content to the painter (the headless
    // renderer uses this too).
    static void drawQuads(QPainter& p, const std::vector<Quad>& quads, const ParticleDocument& doc, const QTransform& view);
    static int batches(const std::vector<Quad>& quads);   // texture / blend changes + 1

    // For the selftest: where a gizmo handle is now (widget coordinates; invalid = not shown).
    enum class Handle { None, Move, Direction, SpreadA, SpreadB, Shape };
    QPointF handlePos(Handle h) const;
    const std::vector<Quad>& lastQuads() const { return m_quads; }

signals:
    void spritesDropped(const QStringList& refs, const QPointF& effectPos);

protected:
    // A 320x240 stage around the origin: fits at 2x, near the game's pixel scale. Effects may spill out.
    QSize contentSize() const override { return QSize(320, 240); }
    QRectF contentRect() const override { return QRectF(-160, -120, 320, 240); }
    void paintContent(QPainter& p) override;
    // The game renderer (ParticleViewportGpu.cpp): background, grid, origin and the effect, as the game draws it.
    void paintGameScene(const GameScene& s) override;
    void gameRendererChanged(toms::next::BgfxRenderer* ren) override;
    bool drawCheckerboard() const override { return m_background == Background::Checker; }
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QPointF emitterPos() const;          // content coordinates of the selected emitter
    Handle hitHandle(const QPointF& widgetPos) const;
    void dragTo(const QPointF& contentPos, Qt::KeyboardModifiers mods);
    void paintGizmo(QPainter& p);
    QString statusText() const;

    ParticleDocument* m_doc;
    ParticlePlayback* m_play;
    std::vector<Quad> m_quads;
    Background m_background = Background::Checker;
    QImage m_bgImage;
    QString m_bgPath;
    bool m_grid = false;
    bool m_gizmo = true;

    Handle m_drag = Handle::None;
    Handle m_hover = Handle::None;
    bool m_movingOrigin = false;
    bool m_pressedEmpty = false;
    QPointF m_pressWidget, m_pressContent, m_startOffset, m_startOrigin;
    int m_gesture = 0;                   // merge key serial: one undo step per drag

    // Game renderer mode.
    GameTextures m_tex;                  // the document's atlases and the background picture, as GPU textures
    bool m_atlasDirty = true;
    QPointer<ParticlePlayback> m_playGuard;   // the playback may go first at shutdown
    QElapsedTimer m_fpsClock;
    int m_fpsFrames = 0;
    double m_fps = 0;
};
