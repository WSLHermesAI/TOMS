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
    f_ReleaseGameRenderer();   // GPU emitters and textures go while the renderer is there
}

void ParticleViewport::f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen)
{
    m_oTex.f_Attach(l_pRen);
    m_bAtlasDirty = true;
    // The effect's big / "gpu" emitters are simulated on the GPU with the renderer, as in the game
    // (nullptr: back on the CPU, their GPU buffers released).
    if (m_oPlayGuard) m_oPlayGuard->f_SetGpuRenderer(l_pRen, m_oPlayGuard->f_GpuThreshold());
    m_oFpsClock.start();
}

void ParticleViewport::f_PaintGameScene(const GameScene& l_oS)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    if (m_bAtlasDirty) {
        m_bAtlasDirty = false;
        std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>> l_vAtlases;
        for (int l_iI = 0; l_iI < m_pDoc->f_AtlasCount(); l_iI++)
            if (m_pDoc->f_AtlasAt(l_iI).m_bOk)
                l_vAtlases.emplace_back(m_pDoc->f_AtlasId(l_iI).toStdString(), m_pDoc->f_AtlasAt(l_iI).m_oPath.toStdString(), &m_pDoc->f_AtlasAt(l_iI).m_oAtlas);
        m_oTex.f_SetAtlases(l_vAtlases);
    }
    // The stage's background (the checkerboard is the base's).
    const QRectF l_oArea = f_ToWidget(f_ContentRect());
    if (m_eBackground == Background::Dark) l_oS.f_Rect(l_oArea, QColor(24, 24, 30));
    else if (m_eBackground == Background::Light) l_oS.f_Rect(l_oArea, QColor(225, 225, 230));
    else if (m_eBackground == Background::Image && !m_oBgImage.isNull())
        l_oS.f_Image(m_oTex.f_Cached(QStringLiteral("background"), m_oBgImage),
                f_ToWidget(QRectF(-m_oBgImage.width() / 2.0, -m_oBgImage.height() / 2.0, m_oBgImage.width(), m_oBgImage.height())));
    if (m_bGrid && f_Zoom() * 32 >= 8) {
        const QPointF l_oA = f_ToContent(QPointF(0, 0)), l_oB = f_ToContent(QPointF(width(), height()));
        for (double l_dX = std::floor(l_oA.x() / 32) * 32; l_dX <= l_oB.x(); l_dX += 32) {
            const double l_dWx = std::round(f_ToWidget(QPointF(l_dX, 0)).x()) + 0.5;
            l_oS.f_Line(QPointF(l_dWx, 0), QPointF(l_dWx, height()), 1, l_oTc.m_oGrid);
        }
        for (double l_dY = std::floor(l_oA.y() / 32) * 32; l_dY <= l_oB.y(); l_dY += 32) {
            const double l_dWy = std::round(f_ToWidget(QPointF(0, l_dY)).y()) + 0.5;
            l_oS.f_Line(QPointF(0, l_dWy), QPointF(width(), l_dWy), 1, l_oTc.m_oGrid);
        }
    }
    const QPointF l_oO = f_ToWidget(m_pPlay->f_Origin());
    l_oS.f_Line(l_oO + QPointF(-10, 0), l_oO + QPointF(10, 0), 1, l_oTc.m_oGridAxis);
    l_oS.f_Line(l_oO + QPointF(0, -10), l_oO + QPointF(0, 10), 1, l_oTc.m_oGridAxis);

    // The effect, as the game draws it (CPU emitters as quads, GPU ones simulated by compute shaders).
    std::vector<std::string> l_vMissing;
    m_pPlay->f_InstanceForDraw().f_Draw(&l_oS.m_oRen, m_oTex.f_AtlasSet(), GameScene::f_Placement(f_ViewTransform()), nullptr, &l_vMissing);

    m_iFpsFrames++;
    if (m_oFpsClock.elapsed() >= 500) {
        m_dFps = m_iFpsFrames * 1000.0 / double(m_oFpsClock.restart());
        m_iFpsFrames = 0;
    }
}
