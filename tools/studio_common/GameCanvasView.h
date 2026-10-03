#pragma once

#include "CanvasView.h"

#include <QColor>
#include <QImage>
#include <QRectF>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "anim_player.h"   // toms::anim::AtlasSet (the game's sprite lookup)
#include "atlas_file.h"

namespace toms::next {
class BgfxRenderer;
}

// Helpers for drawing a canvas's game scene through the game's renderer, in widget coordinates
// (logical pixels; the renderer's design space is the widget).
struct GameScene {
    toms::next::BgfxRenderer& ren;
    double width, height;

    // Content -> widget (CanvasView::viewTransform) as the placement matrix the game's drawing
    // functions take (anim::appendQuads, fx::EffectInstance::draw).
    static glm::mat3 placement(const QTransform& contentToWidget);

    void quad(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d, const QColor& col) const;
    void rect(const QRectF& r, const QColor& col) const;
    void line(const QPointF& a, const QPointF& b, double width, const QColor& col) const;
    // A texture (the renderer's) on a widget rectangle; uv in 0..1 of the texture.
    void image(uint16_t texture, const QRectF& r, const QRectF& uv = QRectF(0, 0, 1, 1), float alpha = 1.0f) const;
};

// Textures for an editor: images uploaded to the game renderer (straight-alpha RGBA, as the game
// loads its PNGs), released together.
class GameTextures {
public:
    ~GameTextures() { clear(); }
    void attach(toms::next::BgfxRenderer* ren);    // nullptr: everything released (the renderer goes)
    bool ready() const { return m_ren != nullptr; }
    uint16_t upload(const QImage& img);            // 0xFFFF: failed
    // An image kept by key (re-uploaded when cacheKey differs, e.g. a page that was rebuilt).
    uint16_t cached(const QString& key, const QImage& img);
    // .atlas files with their page images as textures: an AtlasSet the game's draw functions take.
    // `atlases` = (id, absolute .atlas path, parsed atlas); pages are read from disk next to it.
    void setAtlases(const std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>>& atlases);
    const toms::anim::AtlasSet& atlasSet() const { return *m_set; }
    void clear();

private:
    toms::next::BgfxRenderer* m_ren = nullptr;
    std::vector<uint16_t> m_all;
    struct Cached { QString key; qint64 cacheKey = 0; uint16_t tex = 0xFFFF; };
    std::vector<Cached> m_cached;
    std::vector<std::unique_ptr<toms::AtlasFile>> m_atlases;
    std::unique_ptr<toms::anim::AtlasSet> m_set;
};

// A CanvasView whose game content is drawn by the game's own renderer (toms_game's BgfxRenderer)
// in the widget's native window, so an editor's preview is pixel for pixel what the game shows.
// The subclass draws its scene (atlas pages, sprites, an animation, particles) in paintGameScene();
// everything else it already paints with QPainter in paintContent() -- outlines, handles, labels,
// status text -- is painted into a transparent image that the renderer draws on top
// (overGameScene() is true then: skip the content QPainter would otherwise draw).
//
// One bgfx serves the whole process: it renders into whichever game canvas is painting (the atlas
// editor's two canvases share a stack, one visible at a time). Where bgfx cannot start (no GPU, the
// offscreen selftest platform) the canvas paints with QPainter as before.
class GameCanvasView : public CanvasView
{
    Q_OBJECT

public:
    GameCanvasView(bool gameRenderer, QWidget* parent = nullptr);
    ~GameCanvasView() override;

    // An editor's choice: the setting "preview/gameRenderer" of QSettings("TOMS", app) (default on),
    // never on a windowless platform (offscreen, minimal).
    static bool gameRendererWanted(const QString& app);

    bool usingGameRenderer() const { return m_active; }
    QString rendererName() const;                 // "Direct3D 11 (game renderer)" / "QPainter"
    QPaintEngine* paintEngine() const override;
    bool saveGameRendererShot(const QString& path);   // a bgfx screenshot of the next frame
    int framesDrawn() const { return m_frames; }
    toms::next::BgfxRenderer* gameRenderer() const;   // while usingGameRenderer()

signals:
    void rendererChanged();                       // the game renderer started, or failed (QPainter)

protected:
    virtual void paintGameScene(const GameScene& s) { (void)s; }
    // The renderer starts (ren) or stops (nullptr): upload / release the canvas's textures.
    virtual void gameRendererChanged(toms::next::BgfxRenderer* ren) { (void)ren; }
    // The checkerboard under the scene (as CanvasView::paintBackground); false = the scene fills it.
    virtual bool drawCheckerboard() const { return true; }
    bool overGameScene() const { return m_overGame; }
    // Stops using the renderer (gameRendererChanged(nullptr) first). A subclass calls this in its
    // destructor: from the base destructor the virtual call would not reach it any more.
    void releaseGameRenderer();

    void paintEvent(QPaintEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    bool attach();

    bool m_wanted = false, m_active = false, m_failed = false, m_overGame = false;
    uint16_t m_overlay = 0xFFFF;
    QSize m_overlaySize;
    QString m_shotPath;
    int m_frames = 0;
};
