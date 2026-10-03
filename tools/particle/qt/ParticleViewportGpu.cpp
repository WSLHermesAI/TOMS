// ParticleViewportGpu.cpp -- ParticleViewport's scene through the game's renderer (GameCanvasView,
// tools/studio_common): the background, grid, origin and the effect, drawn by toms_game's
// BgfxRenderer with EffectInstance::draw -- the same sprite batch, textures, blending and GPU
// particle simulation as the game. The gizmo and status line are the QPainter overlay
// (ParticleViewport::paintContent with overGameScene()).
#include "ParticleViewport.h"

#include "ParticleDocument.h"
#include "ParticlePlayback.h"
#include "Theme.h"
#include "bgfx_renderer.h"

#include <cmath>

ParticleViewport::~ParticleViewport()
{
    releaseGameRenderer();   // GPU emitters and textures go while the renderer is there
}

void ParticleViewport::gameRendererChanged(toms::next::BgfxRenderer* ren)
{
    m_tex.attach(ren);
    m_atlasDirty = true;
    // The effect's big / "gpu" emitters are simulated on the GPU with the renderer, as in the game
    // (nullptr: back on the CPU, their GPU buffers released).
    if (m_playGuard) m_playGuard->setGpuRenderer(ren, m_playGuard->gpuThreshold());
    m_fpsClock.start();
}

void ParticleViewport::paintGameScene(const GameScene& s)
{
    const Theme::Colors& tc = Theme::colors();
    if (m_atlasDirty) {
        m_atlasDirty = false;
        std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>> atlases;
        for (int i = 0; i < m_doc->atlasCount(); i++)
            if (m_doc->atlasAt(i).ok)
                atlases.emplace_back(m_doc->atlasId(i).toStdString(), m_doc->atlasAt(i).path.toStdString(), &m_doc->atlasAt(i).atlas);
        m_tex.setAtlases(atlases);
    }
    // The stage's background (the checkerboard is the base's).
    const QRectF area = toWidget(contentRect());
    if (m_background == Background::Dark) s.rect(area, QColor(24, 24, 30));
    else if (m_background == Background::Light) s.rect(area, QColor(225, 225, 230));
    else if (m_background == Background::Image && !m_bgImage.isNull())
        s.image(m_tex.cached(QStringLiteral("background"), m_bgImage),
                toWidget(QRectF(-m_bgImage.width() / 2.0, -m_bgImage.height() / 2.0, m_bgImage.width(), m_bgImage.height())));
    if (m_grid && zoom() * 32 >= 8) {
        const QPointF a = toContent(QPointF(0, 0)), b = toContent(QPointF(width(), height()));
        for (double x = std::floor(a.x() / 32) * 32; x <= b.x(); x += 32) {
            const double wx = std::round(toWidget(QPointF(x, 0)).x()) + 0.5;
            s.line(QPointF(wx, 0), QPointF(wx, height()), 1, tc.grid);
        }
        for (double y = std::floor(a.y() / 32) * 32; y <= b.y(); y += 32) {
            const double wy = std::round(toWidget(QPointF(0, y)).y()) + 0.5;
            s.line(QPointF(0, wy), QPointF(width(), wy), 1, tc.grid);
        }
    }
    const QPointF o = toWidget(m_play->origin());
    s.line(o + QPointF(-10, 0), o + QPointF(10, 0), 1, tc.gridAxis);
    s.line(o + QPointF(0, -10), o + QPointF(0, 10), 1, tc.gridAxis);

    // The effect, as the game draws it (CPU emitters as quads, GPU ones simulated by compute shaders).
    std::vector<std::string> missing;
    m_play->instanceForDraw().draw(&s.ren, m_tex.atlasSet(), GameScene::placement(viewTransform()), nullptr, &missing);

    m_fpsFrames++;
    if (m_fpsClock.elapsed() >= 500) {
        m_fps = m_fpsFrames * 1000.0 / double(m_fpsClock.restart());
        m_fpsFrames = 0;
    }
}
