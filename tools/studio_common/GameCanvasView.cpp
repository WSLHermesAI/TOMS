#include "GameCanvasView.h"

#include "Theme.h"
#include "anim_player.h"
#include "atlas_file.h"
#include "bgfx_host.h"
#include "bgfx_renderer.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <tuple>

using toms::next::BgfxRenderer;

namespace {

// The one bgfx and renderer of the process, shared by its game canvases.
struct Host {
    std::unique_ptr<BgfxRenderer> m_upRen;
    GameCanvasView* m_pCurrent = nullptr;   // the canvas bgfx renders into now
    int m_iCanvases = 0;                    // game canvases alive
    bool m_bFailed = false;
};
Host& f_Host()
{
    static Host l_oH;
    return l_oH;
}

std::vector<uint8_t> f_StraightRgba(const QImage& l_oImg, int& l_iW, int& l_iH)
{
    const QImage l_oS = l_oImg.convertToFormat(QImage::Format_RGBA8888);   // un-premultiplied, as the game's PNGs
    l_iW = l_oS.width();
    l_iH = l_oS.height();
    std::vector<uint8_t> l_vOut(size_t(l_iW) * l_iH * 4);
    for (int l_iY = 0; l_iY < l_iH; l_iY++) std::memcpy(l_vOut.data() + size_t(l_iY) * l_iW * 4, l_oS.constScanLine(l_iY), size_t(l_iW) * 4);
    return l_vOut;
}

}  // namespace

// ---- GameScene -------------------------------------------------------------------------------------

glm::mat3 GameScene::f_Placement(const QTransform& l_oT)
{
    glm::mat3 l_mat3M(1.0f);
    l_mat3M[0][0] = float(l_oT.m11());
    l_mat3M[0][1] = float(l_oT.m12());
    l_mat3M[1][0] = float(l_oT.m21());
    l_mat3M[1][1] = float(l_oT.m22());
    l_mat3M[2][0] = float(l_oT.dx());
    l_mat3M[2][1] = float(l_oT.dy());
    return l_mat3M;
}

void GameScene::f_Quad(const QPointF& l_oA, const QPointF& l_oB, const QPointF& l_oC, const QPointF& l_oD, const QColor& l_oCol) const
{
    Quad l_oQ;
    l_oQ.m_bSolid = true;
    l_oQ.m_bHasCorners = true;
    const QPointF l_arrP[4] = {l_oA, l_oB, l_oC, l_oD};
    double l_dX0 = l_oA.x(), l_dY0 = l_oA.y(), l_dX1 = l_dX0, l_dY1 = l_dY0;
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        l_oQ.m_arrCorners[l_iI * 2] = float(l_arrP[l_iI].x());
        l_oQ.m_arrCorners[l_iI * 2 + 1] = float(l_arrP[l_iI].y());
        l_dX0 = std::min(l_dX0, l_arrP[l_iI].x()); l_dX1 = std::max(l_dX1, l_arrP[l_iI].x());
        l_dY0 = std::min(l_dY0, l_arrP[l_iI].y()); l_dY1 = std::max(l_dY1, l_arrP[l_iI].y());
    }
    l_oQ.m_arrRect[0] = float(l_dX0); l_oQ.m_arrRect[1] = float(l_dY0); l_oQ.m_arrRect[2] = float(l_dX1 - l_dX0); l_oQ.m_arrRect[3] = float(l_dY1 - l_dY0);
    l_oQ.m_arrUv[0] = l_oQ.m_arrUv[1] = l_oQ.m_arrUv[2] = l_oQ.m_arrUv[3] = 0;
    l_oQ.m_arrTint[0] = float(l_oCol.redF()); l_oQ.m_arrTint[1] = float(l_oCol.greenF()); l_oQ.m_arrTint[2] = float(l_oCol.blueF()); l_oQ.m_arrTint[3] = float(l_oCol.alphaF());
    m_oRen.f_DrawSprite(l_oQ);
}

void GameScene::f_Rect(const QRectF& l_oR, const QColor& l_oCol) const { f_Quad(l_oR.topLeft(), l_oR.topRight(), l_oR.bottomRight(), l_oR.bottomLeft(), l_oCol); }

void GameScene::f_Line(const QPointF& l_oA, const QPointF& l_oB, double l_dW, const QColor& l_oCol) const
{
    const QPointF l_oV = l_oB - l_oA;
    const double l_dLen = std::hypot(l_oV.x(), l_oV.y());
    if (l_dLen < 1e-6) return;
    const QPointF l_oN(-l_oV.y() / l_dLen * l_dW / 2, l_oV.x() / l_dLen * l_dW / 2);
    f_Quad(l_oA + l_oN, l_oB + l_oN, l_oB - l_oN, l_oA - l_oN, l_oCol);
}

void GameScene::f_Image(uint16_t l_u16Texture, const QRectF& l_oR, const QRectF& l_oUv, float l_fAlpha) const
{
    if (l_u16Texture == g_u16SpriteAtlasTexture) return;
    Quad l_oQ;
    l_oQ.m_arrRect[0] = float(l_oR.x()); l_oQ.m_arrRect[1] = float(l_oR.y()); l_oQ.m_arrRect[2] = float(l_oR.width()); l_oQ.m_arrRect[3] = float(l_oR.height());
    l_oQ.m_arrUv[0] = float(l_oUv.left()); l_oQ.m_arrUv[1] = float(l_oUv.top()); l_oQ.m_arrUv[2] = float(l_oUv.right()); l_oQ.m_arrUv[3] = float(l_oUv.bottom());
    l_oQ.m_arrTint[0] = l_oQ.m_arrTint[1] = l_oQ.m_arrTint[2] = 1;
    l_oQ.m_arrTint[3] = l_fAlpha;
    l_oQ.m_u16Texture = l_u16Texture;
    m_oRen.f_DrawSprite(l_oQ);
}

// ---- GameTextures ----------------------------------------------------------------------------------

void GameTextures::f_Attach(BgfxRenderer* l_pRen)
{
    if (l_pRen == m_pRen) return;
    f_Clear();
    m_pRen = l_pRen;
}

void GameTextures::f_Clear()
{
    if (m_pRen)
        for (uint16_t l_u16T : m_vAll) m_pRen->f_ReleaseTexture(l_u16T);
    m_vAll.clear();
    m_vCached.clear();
    m_vAtlases.clear();
    m_upSet.reset();
}

uint16_t GameTextures::f_Upload(const QImage& l_oImg)
{
    if (!m_pRen || l_oImg.isNull()) return g_u16SpriteAtlasTexture;
    int l_iW = 0, l_iH = 0;
    const std::vector<uint8_t> l_vRgba = f_StraightRgba(l_oImg, l_iW, l_iH);
    const uint16_t l_u16T = m_pRen->f_LoadTexture(l_vRgba, uint32_t(l_iW), uint32_t(l_iH));
    if (l_u16T != g_u16SpriteAtlasTexture) m_vAll.push_back(l_u16T);
    return l_u16T;
}

uint16_t GameTextures::f_Cached(const QString& l_oKey, const QImage& l_oImg)
{
    for (Cached& l_oC : m_vCached)
        if (l_oC.m_oKey == l_oKey) {
            if (l_oC.m_i64CacheKey == l_oImg.cacheKey()) return l_oC.m_u16Tex;
            if (m_pRen && l_oC.m_u16Tex != g_u16SpriteAtlasTexture) {
                m_pRen->f_ReleaseTexture(l_oC.m_u16Tex);
                m_vAll.erase(std::remove(m_vAll.begin(), m_vAll.end(), l_oC.m_u16Tex), m_vAll.end());
            }
            l_oC.m_u16Tex = f_Upload(l_oImg);
            l_oC.m_i64CacheKey = l_oImg.cacheKey();
            return l_oC.m_u16Tex;
        }
    m_vCached.push_back({l_oKey, l_oImg.cacheKey(), f_Upload(l_oImg)});
    return m_vCached.back().m_u16Tex;
}

void GameTextures::f_SetAtlases(const std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>>& l_vAtlases)
{
    // Only the atlas textures go; cached images stay.
    std::vector<uint16_t> l_vKeep;
    for (const Cached& l_oC : m_vCached) l_vKeep.push_back(l_oC.m_u16Tex);
    if (m_pRen)
        for (uint16_t l_u16T : m_vAll)
            if (std::find(l_vKeep.begin(), l_vKeep.end(), l_u16T) == l_vKeep.end()) m_pRen->f_ReleaseTexture(l_u16T);
    m_vAll = l_vKeep;
    m_vAtlases.clear();
    m_upSet = std::make_unique<toms::anim::AtlasSet>();
    if (!m_pRen) return;
    for (const auto& [l_strId, l_strPath, l_pParsed] : l_vAtlases) {
        if (!l_pParsed) continue;
        auto l_upAtlas = std::make_unique<toms::AtlasFile>(*l_pParsed);
        const QDir l_oDir = QFileInfo(QString::fromStdString(l_strPath)).absoluteDir();
        std::vector<uint16_t> l_vPages;
        for (toms::AtlasPage& l_oPg : l_upAtlas->m_vPages) {
            const QImage l_oImg(l_oDir.filePath(QString::fromStdString(l_oPg.m_strFile)));
            const uint16_t l_u16T = f_Upload(l_oImg);
            if (l_oPg.m_strFilter.rfind("Linear", 0) == 0) m_pRen->f_SetTextureFilter(l_u16T, true);
            if (!l_oImg.isNull()) {
                l_oPg.m_iW = l_oImg.width();
                l_oPg.m_iH = l_oImg.height();
            }
            l_vPages.push_back(l_u16T);
        }
        l_upAtlas->f_ComputeUVs();
        m_upSet->f_Add(*l_upAtlas, l_strId, l_vPages);
        m_vAtlases.push_back(std::move(l_upAtlas));
    }
}

// ---- GameCanvasView --------------------------------------------------------------------------------

GameCanvasView::GameCanvasView(bool l_bGameRenderer, QWidget* l_pParent)
    : CanvasView(l_pParent)
    , m_bWanted(l_bGameRenderer && !f_Host().m_bFailed)
{
    if (m_bWanted) {   // bgfx renders into this widget's own native window
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
        f_Host().m_iCanvases++;
    }
}

GameCanvasView::~GameCanvasView()
{
    if (!m_bWanted) return;
    f_ReleaseGameRenderer();
    Host& l_oH = f_Host();
    if (--l_oH.m_iCanvases == 0 && l_oH.m_upRen) {   // the last game canvas: bgfx goes, while a native window still exists
        l_oH.m_upRen->f_Destroy();
        l_oH.m_upRen.reset();
        toms::next::f_BgfxHostShutdown();
    }
}

bool GameCanvasView::f_GameRendererWanted(const QString& l_oApp)
{
    const QString l_oPlatform = QGuiApplication::platformName();
    if (l_oPlatform == QLatin1String("offscreen") || l_oPlatform == QLatin1String("minimal")) return false;
    return QSettings(QStringLiteral("TOMS"), l_oApp).value(QStringLiteral("preview/gameRenderer"), true).toBool();
}

QString GameCanvasView::f_RendererName() const
{
    return m_bActive ? tr("%1 (game renderer)").arg(QString::fromStdString(toms::next::f_BgfxHostRendererName())) : tr("QPainter");
}

QPaintEngine* GameCanvasView::paintEngine() const { return m_bWanted && !m_bFailed ? nullptr : CanvasView::paintEngine(); }

BgfxRenderer* GameCanvasView::f_GameRenderer() const { return m_bActive ? f_Host().m_upRen.get() : nullptr; }

bool GameCanvasView::f_SaveGameRendererShot(const QString& l_oPath)
{
    if (!m_bActive) return false;
    m_oShotPath = l_oPath;
    update();
    return true;
}

QImage GameCanvasView::f_RenderOffscreen(int l_iW, int l_iH, const std::function<void(const GameScene&)>& l_fnDraw)
{
    if (!f_Attach() || l_iW <= 0 || l_iH <= 0 || l_iW > 4096 || l_iH > 4096) return QImage();
    BgfxRenderer& l_oRen = *f_Host().m_upRen;
    l_oRen.f_SetCapture(uint16_t(l_iW), uint16_t(l_iH));
    BgfxRenderer::f_SetDesignSizeExact(uint32_t(l_iW), uint32_t(l_iH));
    l_oRen.f_SetDeviceSize(uint32_t(l_iW), uint32_t(l_iH));
    l_oRen.begin();
    l_fnDraw(GameScene{l_oRen, double(l_iW), double(l_iH)});
    l_oRen.end();
    const uint32_t l_uReady = l_oRen.f_CaptureFrame();
    QImage l_oOut;
    if (l_uReady) {
        uint32_t l_uFrame = 0;
        for (int l_iI = 0; l_iI < 16 && l_uFrame < l_uReady; l_iI++) l_uFrame = toms::next::f_BgfxHostFrame();
        if (l_uFrame >= l_uReady) {
            l_oOut = QImage(l_iW, l_iH, QImage::Format_RGBA8888_Premultiplied);
            const std::vector<uint8_t>& l_vPx = l_oRen.f_CapturePixels();
            for (int l_iY = 0; l_iY < l_iH; l_iY++) std::memcpy(l_oOut.scanLine(l_iY), l_vPx.data() + size_t(l_iY) * size_t(l_iW) * 4, size_t(l_iW) * 4);
            l_oOut = l_oOut.convertToFormat(QImage::Format_RGBA8888);   // straight alpha
        }
    }
    l_oRen.f_SetCapture(0, 0);
    update();   // the window's own picture again
    return l_oOut;
}

bool GameCanvasView::event(QEvent* l_pE)
{
    if (l_pE->type() == QEvent::WinIdChange && m_bActive && f_Host().m_pCurrent == this) {
        f_Host().m_pCurrent = nullptr;   // attach() points bgfx at the new window handle
        update();
    }
    return CanvasView::event(l_pE);
}

void GameCanvasView::showEvent(QShowEvent* l_pE)
{
    CanvasView::showEvent(l_pE);
    if (m_bWanted && !m_bFailed) update();
}

void GameCanvasView::resizeEvent(QResizeEvent* l_pE)
{
    CanvasView::resizeEvent(l_pE);
    if (m_bActive && f_Host().m_pCurrent == this)
        toms::next::f_BgfxHostReset(uint32_t(std::lround(width() * devicePixelRatioF())), uint32_t(std::lround(height() * devicePixelRatioF())));
}

// Starts bgfx on this window (the first canvas), or moves it here (another canvas was current).
bool GameCanvasView::f_Attach()
{
    Host& l_oH = f_Host();
    if (!m_bWanted || m_bFailed || !isVisible()) return false;
    const uint32_t l_uPw = uint32_t(std::lround(width() * devicePixelRatioF())), l_uPh = uint32_t(std::lround(height() * devicePixelRatioF()));
    if (!l_oH.m_upRen) {
        toms::next::BgfxHostConfig l_oCfg;
        l_oCfg.m_pNativeWindow = reinterpret_cast<void*>(winId());
        l_oCfg.m_uWidth = l_uPw;
        l_oCfg.m_uHeight = l_uPh;
        l_oCfg.m_bFlipAfterRender = true;   // drawn on demand (a click, an edit): show this frame now, not with the next one
        if (const char* l_pszR = std::getenv("TOMS_RENDERER")) l_oCfg.m_strRenderer = l_pszR;
        std::string l_strErr;
        if (!toms::next::f_BgfxHostInit(l_oCfg, l_strErr)) {   // QPainter from now on, for every canvas
            l_oH.m_bFailed = m_bFailed = true;
            setAttribute(Qt::WA_PaintOnScreen, false);
            std::fprintf(stderr, "[editor] the game renderer did not start (%s): QPainter\n", l_strErr.c_str());
            emit f_RendererChanged();
            return false;
        }
        l_oH.m_upRen = std::make_unique<BgfxRenderer>();
        l_oH.m_upRen->f_Init(l_uPw, l_uPh);
        l_oH.m_pCurrent = this;
    } else if (l_oH.m_pCurrent != this) {
        toms::next::f_BgfxHostSetWindow(reinterpret_cast<void*>(winId()), l_uPw, l_uPh);
        l_oH.m_pCurrent = this;
    }
    if (!m_bActive) {
        m_bActive = true;
        f_GameRendererChanged(l_oH.m_upRen.get());
        emit f_RendererChanged();
    }
    return true;
}

void GameCanvasView::f_ReleaseGameRenderer()
{
    if (!m_bActive) return;
    Host& l_oH = f_Host();
    f_GameRendererChanged(nullptr);   // the subclass releases its textures (and GPU particle emitters)
    if (m_u16Overlay != g_u16SpriteAtlasTexture) l_oH.m_upRen->f_ReleaseTexture(m_u16Overlay);
    m_u16Overlay = g_u16SpriteAtlasTexture;
    if (l_oH.m_pCurrent == this) l_oH.m_pCurrent = nullptr;
    m_bActive = false;
}

void GameCanvasView::paintEvent(QPaintEvent* l_pE)
{
    if (!m_bWanted || m_bFailed || !f_Attach()) {
        CanvasView::paintEvent(l_pE);
        return;
    }
    if (m_bHold) return;
    BgfxRenderer& l_oRen = *f_Host().m_upRen;
    f_PreparePaint();
    const double l_dDpr = devicePixelRatioF();
    const uint32_t l_uPw = uint32_t(std::lround(width() * l_dDpr)), l_uPh = uint32_t(std::lround(height() * l_dDpr));
    BgfxRenderer::f_SetDesignSizeExact(uint32_t(width()), uint32_t(height()));   // design space = the widget
    l_oRen.f_SetDeviceSize(l_uPw, l_uPh);
    l_oRen.begin();
    const GameScene l_oS{l_oRen, double(width()), double(height())};

    // The background, as CanvasView::paintBackground.
    const Theme::Colors& l_oTc = Theme::f_Colors();
    l_oS.f_Rect(QRectF(0, 0, width(), height()), l_oTc.m_oCanvasBackground);
    const QRectF l_oArea = f_ToWidget(f_ContentRect()).intersected(QRectF(0, 0, width(), height()));
    if (f_DrawCheckerboard() && !l_oArea.isEmpty()) {
        l_oS.f_Rect(l_oArea, l_oTc.m_oCheckerLight);
        const double l_dCell = 8;   // screen pixels, from the widget's corner (as the QPainter brush)
        for (double l_dY = std::floor(l_oArea.top() / l_dCell) * l_dCell; l_dY < l_oArea.bottom(); l_dY += l_dCell)
            for (double l_dX = std::floor(l_oArea.left() / l_dCell) * l_dCell; l_dX < l_oArea.right(); l_dX += l_dCell)
                if ((int(l_dX / l_dCell) + int(l_dY / l_dCell)) % 2 == 0) l_oS.f_Rect(QRectF(l_dX, l_dY, l_dCell, l_dCell).intersected(l_oArea), l_oTc.m_oCheckerDark);
    }
    f_PaintGameScene(l_oS);

    // The subclass's QPainter drawing (outlines, handles, labels) over it, through a texture.
    QImage l_oOverlay(int(l_uPw), int(l_uPh), QImage::Format_ARGB32_Premultiplied);
    l_oOverlay.setDevicePixelRatio(l_dDpr);
    l_oOverlay.fill(Qt::transparent);
    {
        QPainter l_oP(&l_oOverlay);
        if (f_DrawCheckerboard()) {   // the content's border, as paintBackground draws it
            l_oP.setPen(QPen(l_oTc.m_oPageBorder, 1));
            l_oP.drawRect(f_ToWidget(f_ContentRect()).adjusted(-0.5, -0.5, 0.5, 0.5));
        }
        m_bOverGame = true;
        f_PaintContent(l_oP);
        m_bOverGame = false;
        f_PaintCoordinates(l_oP);
    }
    if (m_oOverlaySize != l_oOverlay.size()) {
        if (m_u16Overlay != g_u16SpriteAtlasTexture) l_oRen.f_ReleaseTexture(m_u16Overlay);
        m_u16Overlay = l_oRen.f_CreateDynamicTexture(l_uPw, l_uPh);
        m_oOverlaySize = l_oOverlay.size();
    }
    int l_iW = 0, l_iH = 0;
    const std::vector<uint8_t> l_vRgba = f_StraightRgba(l_oOverlay, l_iW, l_iH);
    l_oRen.f_UpdateTexture(m_u16Overlay, l_vRgba.data(), uint32_t(l_iW), uint32_t(l_iH));
    l_oS.f_Image(m_u16Overlay, QRectF(0, 0, width(), height()));
    l_oRen.end();
    if (!m_oShotPath.isEmpty()) {
        l_oRen.f_SavePNG(m_oShotPath.toStdString());
        m_oShotPath.clear();
    }
    toms::next::f_BgfxHostFrame();
    m_iFrames++;
}
