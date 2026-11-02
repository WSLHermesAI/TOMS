#pragma once

#include "CanvasView.h"

#include <QColor>
#include <QImage>
#include <QRectF>

#include <glm/glm.hpp>

#include <functional>
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
    toms::next::BgfxRenderer& m_oRen;
    double m_dWidth, m_dHeight;

    // Content -> widget (CanvasView::viewTransform) as the placement matrix the game's drawing
    // functions take (anim::appendQuads, fx::EffectInstance::draw).
    static glm::mat3 f_Placement(const QTransform& l_oContentToWidget);

    void f_Quad(const QPointF& l_oA, const QPointF& l_oB, const QPointF& l_oC, const QPointF& l_oD, const QColor& l_oCol) const;
    void f_Rect(const QRectF& l_oR, const QColor& l_oCol) const;
    void f_Line(const QPointF& l_oA, const QPointF& l_oB, double l_dWidth, const QColor& l_oCol) const;
    // A texture (the renderer's) on a widget rectangle; uv in 0..1 of the texture.
    void f_Image(uint16_t l_u16Texture, const QRectF& l_oR, const QRectF& l_oUv = QRectF(0, 0, 1, 1), float l_fAlpha = 1.0f) const;
};

// Textures for an editor: images uploaded to the game renderer (straight-alpha RGBA, as the game
// loads its PNGs), released together.
class GameTextures {
public:
    ~GameTextures() { f_Clear(); }
    void f_Attach(toms::next::BgfxRenderer* l_pRen);    // nullptr: everything released (the renderer goes)
    bool f_Ready() const { return m_pRen != nullptr; }
    uint16_t f_Upload(const QImage& l_oImg);            // 0xFFFF: failed
    // An image kept by key (re-uploaded when cacheKey differs, e.g. a page that was rebuilt).
    uint16_t f_Cached(const QString& l_oKey, const QImage& l_oImg);
    // .atlas files with their page images as textures: an AtlasSet the game's draw functions take.
    // `atlases` = (id, absolute .atlas path, parsed atlas); pages are read from disk next to it.
    void f_SetAtlases(const std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>>& l_vAtlases);
    const toms::anim::AtlasSet& f_AtlasSet() const { return *m_upSet; }
    // The meshes of the atlas with this id (kept by the caller; nullptr = none): skinned and deformed nodes bend them.
    void f_SetMeshes(const std::string& l_strId, const toms::SpriteMeshFile* l_pMeshes) {
        if (m_upSet) for (auto& l_oE : m_upSet->m_vEntries) if (l_oE.m_strId == l_strId) l_oE.m_pMeshes = l_pMeshes;
    }
    void f_Clear();

private:
    toms::next::BgfxRenderer* m_pRen = nullptr;
    std::vector<uint16_t> m_vAll;
    struct Cached { QString m_oKey; qint64 m_i64CacheKey = 0; uint16_t m_u16Tex = 0xFFFF; };
    std::vector<Cached> m_vCached;
    std::vector<std::unique_ptr<toms::AtlasFile>> m_vAtlases;
    std::unique_ptr<toms::anim::AtlasSet> m_upSet;
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
    GameCanvasView(bool l_bGameRenderer, QWidget* l_pParent = nullptr);
    ~GameCanvasView() override;

    // An editor's choice: the setting "preview/gameRenderer" of QSettings("TOMS", app) (default on),
    // never on a windowless platform (offscreen, minimal).
    static bool f_GameRendererWanted(const QString& l_oApp);

    bool f_UsingGameRenderer() const { return m_bActive; }
    QString f_RendererName() const;                 // "Direct3D 11 (game renderer)" / "QPainter"
    QPaintEngine* paintEngine() const override;
    bool f_SaveGameRendererShot(const QString& l_oPath);   // a bgfx screenshot of the next frame
    int f_FramesDrawn() const { return m_iFrames; }
    // Tests: draw no frame while on (a capture of the window, which asks it to paint, then sees what was shown).
    void f_HoldFrames(bool l_bOn) { m_bHold = l_bOn; }
    toms::next::BgfxRenderer* f_GameRenderer() const;   // while usingGameRenderer()
    // One picture of w x h drawn by `draw` through the game renderer, offscreen: a transparent background, the design
    // space 0..w x 0..h, nothing of the editor (BgfxRenderer::setCapture). Waits for the GPU. Format_RGBA8888 with
    // straight alpha; a null image without the game renderer.
    QImage f_RenderOffscreen(int l_iW, int l_iH, const std::function<void(const GameScene&)>& l_fnDraw);

signals:
    void f_RendererChanged();                       // the game renderer started, or failed (QPainter)

protected:
    virtual void f_PaintGameScene(const GameScene& l_oS) { (void)l_oS; }
    // The renderer starts (ren) or stops (nullptr): upload / release the canvas's textures.
    virtual void f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen) { (void)l_pRen; }
    // The checkerboard under the scene (as CanvasView::paintBackground); false = the scene fills it.
    virtual bool f_DrawCheckerboard() const { return true; }
    bool f_OverGameScene() const { return m_bOverGame; }
    // Stops using the renderer (gameRendererChanged(nullptr) first). A subclass calls this in its
    // destructor: from the base destructor the virtual call would not reach it any more.
    void f_ReleaseGameRenderer();

    void paintEvent(QPaintEvent* l_pE) override;
    void showEvent(QShowEvent* l_pE) override;
    // A new native window (the canvas moved to another top-level window): bgfx moves to it on the next paint.
    bool event(QEvent* l_pE) override;
    void resizeEvent(QResizeEvent* l_pE) override;

private:
    bool f_Attach();

    bool m_bWanted = false, m_bActive = false, m_bFailed = false, m_bOverGame = false;
    uint16_t m_u16Overlay = 0xFFFF;
    QSize m_oOverlaySize;
    QString m_oShotPath;
    int m_iFrames = 0;
    bool m_bHold = false;
};
