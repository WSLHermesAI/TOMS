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
    std::unique_ptr<BgfxRenderer> ren;
    GameCanvasView* current = nullptr;   // the canvas bgfx renders into now
    int canvases = 0;                    // game canvases alive
    bool failed = false;
};
Host& host()
{
    static Host h;
    return h;
}

std::vector<uint8_t> straightRgba(const QImage& img, int& w, int& h)
{
    const QImage s = img.convertToFormat(QImage::Format_RGBA8888);   // un-premultiplied, as the game's PNGs
    w = s.width();
    h = s.height();
    std::vector<uint8_t> out(size_t(w) * h * 4);
    for (int y = 0; y < h; y++) std::memcpy(out.data() + size_t(y) * w * 4, s.constScanLine(y), size_t(w) * 4);
    return out;
}

}  // namespace

// ---- GameScene -------------------------------------------------------------------------------------

glm::mat3 GameScene::placement(const QTransform& t)
{
    glm::mat3 m(1.0f);
    m[0][0] = float(t.m11());
    m[0][1] = float(t.m12());
    m[1][0] = float(t.m21());
    m[1][1] = float(t.m22());
    m[2][0] = float(t.dx());
    m[2][1] = float(t.dy());
    return m;
}

void GameScene::quad(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d, const QColor& col) const
{
    Quad q;
    q.solid = true;
    q.hasCorners = true;
    const QPointF p[4] = {a, b, c, d};
    double x0 = a.x(), y0 = a.y(), x1 = x0, y1 = y0;
    for (int i = 0; i < 4; i++) {
        q.corners[i * 2] = float(p[i].x());
        q.corners[i * 2 + 1] = float(p[i].y());
        x0 = std::min(x0, p[i].x()); x1 = std::max(x1, p[i].x());
        y0 = std::min(y0, p[i].y()); y1 = std::max(y1, p[i].y());
    }
    q.rect[0] = float(x0); q.rect[1] = float(y0); q.rect[2] = float(x1 - x0); q.rect[3] = float(y1 - y0);
    q.uv[0] = q.uv[1] = q.uv[2] = q.uv[3] = 0;
    q.tint[0] = float(col.redF()); q.tint[1] = float(col.greenF()); q.tint[2] = float(col.blueF()); q.tint[3] = float(col.alphaF());
    ren.drawSprite(q);
}

void GameScene::rect(const QRectF& r, const QColor& col) const { quad(r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), col); }

void GameScene::line(const QPointF& a, const QPointF& b, double w, const QColor& col) const
{
    const QPointF v = b - a;
    const double len = std::hypot(v.x(), v.y());
    if (len < 1e-6) return;
    const QPointF n(-v.y() / len * w / 2, v.x() / len * w / 2);
    quad(a + n, b + n, b - n, a - n, col);
}

void GameScene::image(uint16_t texture, const QRectF& r, const QRectF& uv, float alpha) const
{
    if (texture == kSpriteAtlasTexture) return;
    Quad q;
    q.rect[0] = float(r.x()); q.rect[1] = float(r.y()); q.rect[2] = float(r.width()); q.rect[3] = float(r.height());
    q.uv[0] = float(uv.left()); q.uv[1] = float(uv.top()); q.uv[2] = float(uv.right()); q.uv[3] = float(uv.bottom());
    q.tint[0] = q.tint[1] = q.tint[2] = 1;
    q.tint[3] = alpha;
    q.texture = texture;
    ren.drawSprite(q);
}

// ---- GameTextures ----------------------------------------------------------------------------------

void GameTextures::attach(BgfxRenderer* ren)
{
    if (ren == m_ren) return;
    clear();
    m_ren = ren;
}

void GameTextures::clear()
{
    if (m_ren)
        for (uint16_t t : m_all) m_ren->releaseTexture(t);
    m_all.clear();
    m_cached.clear();
    m_atlases.clear();
    m_set.reset();
}

uint16_t GameTextures::upload(const QImage& img)
{
    if (!m_ren || img.isNull()) return kSpriteAtlasTexture;
    int w = 0, h = 0;
    const std::vector<uint8_t> rgba = straightRgba(img, w, h);
    const uint16_t t = m_ren->loadTexture(rgba, uint32_t(w), uint32_t(h));
    if (t != kSpriteAtlasTexture) m_all.push_back(t);
    return t;
}

uint16_t GameTextures::cached(const QString& key, const QImage& img)
{
    for (Cached& c : m_cached)
        if (c.key == key) {
            if (c.cacheKey == img.cacheKey()) return c.tex;
            if (m_ren && c.tex != kSpriteAtlasTexture) {
                m_ren->releaseTexture(c.tex);
                m_all.erase(std::remove(m_all.begin(), m_all.end(), c.tex), m_all.end());
            }
            c.tex = upload(img);
            c.cacheKey = img.cacheKey();
            return c.tex;
        }
    m_cached.push_back({key, img.cacheKey(), upload(img)});
    return m_cached.back().tex;
}

void GameTextures::setAtlases(const std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>>& atlases)
{
    // Only the atlas textures go; cached images stay.
    std::vector<uint16_t> keep;
    for (const Cached& c : m_cached) keep.push_back(c.tex);
    if (m_ren)
        for (uint16_t t : m_all)
            if (std::find(keep.begin(), keep.end(), t) == keep.end()) m_ren->releaseTexture(t);
    m_all = keep;
    m_atlases.clear();
    m_set = std::make_unique<toms::anim::AtlasSet>();
    if (!m_ren) return;
    for (const auto& [id, path, parsed] : atlases) {
        if (!parsed) continue;
        auto atlas = std::make_unique<toms::AtlasFile>(*parsed);
        const QDir dir = QFileInfo(QString::fromStdString(path)).absoluteDir();
        std::vector<uint16_t> pages;
        for (toms::AtlasPage& pg : atlas->pages) {
            const QImage img(dir.filePath(QString::fromStdString(pg.file)));
            const uint16_t t = upload(img);
            if (!img.isNull()) {
                pg.w = img.width();
                pg.h = img.height();
            }
            pages.push_back(t);
        }
        atlas->computeUVs();
        m_set->add(*atlas, id, pages);
        m_atlases.push_back(std::move(atlas));
    }
}

// ---- GameCanvasView --------------------------------------------------------------------------------

GameCanvasView::GameCanvasView(bool gameRenderer, QWidget* parent)
    : CanvasView(parent)
    , m_wanted(gameRenderer && !host().failed)
{
    if (m_wanted) {   // bgfx renders into this widget's own native window
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
        host().canvases++;
    }
}

GameCanvasView::~GameCanvasView()
{
    if (!m_wanted) return;
    releaseGameRenderer();
    Host& h = host();
    if (--h.canvases == 0 && h.ren) {   // the last game canvas: bgfx goes, while a native window still exists
        h.ren->destroy();
        h.ren.reset();
        toms::next::bgfxHostShutdown();
    }
}

bool GameCanvasView::gameRendererWanted(const QString& app)
{
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) return false;
    return QSettings(QStringLiteral("TOMS"), app).value(QStringLiteral("preview/gameRenderer"), true).toBool();
}

QString GameCanvasView::rendererName() const
{
    return m_active ? tr("%1 (game renderer)").arg(QString::fromStdString(toms::next::bgfxHostRendererName())) : tr("QPainter");
}

QPaintEngine* GameCanvasView::paintEngine() const { return m_wanted && !m_failed ? nullptr : CanvasView::paintEngine(); }

BgfxRenderer* GameCanvasView::gameRenderer() const { return m_active ? host().ren.get() : nullptr; }

bool GameCanvasView::saveGameRendererShot(const QString& path)
{
    if (!m_active) return false;
    m_shotPath = path;
    update();
    return true;
}

void GameCanvasView::showEvent(QShowEvent* e)
{
    CanvasView::showEvent(e);
    if (m_wanted && !m_failed) update();
}

void GameCanvasView::resizeEvent(QResizeEvent* e)
{
    CanvasView::resizeEvent(e);
    if (m_active && host().current == this)
        toms::next::bgfxHostReset(uint32_t(std::lround(width() * devicePixelRatioF())), uint32_t(std::lround(height() * devicePixelRatioF())));
}

// Starts bgfx on this window (the first canvas), or moves it here (another canvas was current).
bool GameCanvasView::attach()
{
    Host& h = host();
    if (!m_wanted || m_failed || !isVisible()) return false;
    const uint32_t pw = uint32_t(std::lround(width() * devicePixelRatioF())), ph = uint32_t(std::lround(height() * devicePixelRatioF()));
    if (!h.ren) {
        toms::next::BgfxHostConfig cfg;
        cfg.nativeWindow = reinterpret_cast<void*>(winId());
        cfg.width = pw;
        cfg.height = ph;
        if (const char* r = std::getenv("TOMS_RENDERER")) cfg.renderer = r;
        std::string err;
        if (!toms::next::bgfxHostInit(cfg, err)) {   // QPainter from now on, for every canvas
            h.failed = m_failed = true;
            setAttribute(Qt::WA_PaintOnScreen, false);
            std::fprintf(stderr, "[editor] the game renderer did not start (%s): QPainter\n", err.c_str());
            emit rendererChanged();
            return false;
        }
        h.ren = std::make_unique<BgfxRenderer>();
        h.ren->init(pw, ph);
        h.current = this;
    } else if (h.current != this) {
        toms::next::bgfxHostSetWindow(reinterpret_cast<void*>(winId()), pw, ph);
        h.current = this;
    }
    if (!m_active) {
        m_active = true;
        gameRendererChanged(h.ren.get());
        emit rendererChanged();
    }
    return true;
}

void GameCanvasView::releaseGameRenderer()
{
    if (!m_active) return;
    Host& h = host();
    gameRendererChanged(nullptr);   // the subclass releases its textures (and GPU particle emitters)
    if (m_overlay != kSpriteAtlasTexture) h.ren->releaseTexture(m_overlay);
    m_overlay = kSpriteAtlasTexture;
    if (h.current == this) h.current = nullptr;
    m_active = false;
}

void GameCanvasView::paintEvent(QPaintEvent* e)
{
    if (!m_wanted || m_failed || !attach()) {
        CanvasView::paintEvent(e);
        return;
    }
    BgfxRenderer& ren = *host().ren;
    preparePaint();
    const double dpr = devicePixelRatioF();
    const uint32_t pw = uint32_t(std::lround(width() * dpr)), ph = uint32_t(std::lround(height() * dpr));
    BgfxRenderer::setDesignSizeExact(uint32_t(width()), uint32_t(height()));   // design space = the widget
    ren.setDeviceSize(pw, ph);
    ren.begin();
    const GameScene s{ren, double(width()), double(height())};

    // The background, as CanvasView::paintBackground.
    const Theme::Colors& tc = Theme::colors();
    s.rect(QRectF(0, 0, width(), height()), tc.canvasBackground);
    const QRectF area = toWidget(contentRect()).intersected(QRectF(0, 0, width(), height()));
    if (drawCheckerboard() && !area.isEmpty()) {
        s.rect(area, tc.checkerLight);
        const double cell = 8;   // screen pixels, from the widget's corner (as the QPainter brush)
        for (double y = std::floor(area.top() / cell) * cell; y < area.bottom(); y += cell)
            for (double x = std::floor(area.left() / cell) * cell; x < area.right(); x += cell)
                if ((int(x / cell) + int(y / cell)) % 2 == 0) s.rect(QRectF(x, y, cell, cell).intersected(area), tc.checkerDark);
    }
    paintGameScene(s);

    // The subclass's QPainter drawing (outlines, handles, labels) over it, through a texture.
    QImage overlay(int(pw), int(ph), QImage::Format_ARGB32_Premultiplied);
    overlay.setDevicePixelRatio(dpr);
    overlay.fill(Qt::transparent);
    {
        QPainter p(&overlay);
        if (drawCheckerboard()) {   // the content's border, as paintBackground draws it
            p.setPen(QPen(tc.pageBorder, 1));
            p.drawRect(toWidget(contentRect()).adjusted(-0.5, -0.5, 0.5, 0.5));
        }
        m_overGame = true;
        paintContent(p);
        m_overGame = false;
        paintCoordinates(p);
    }
    if (m_overlaySize != overlay.size()) {
        if (m_overlay != kSpriteAtlasTexture) ren.releaseTexture(m_overlay);
        m_overlay = ren.createDynamicTexture(pw, ph);
        m_overlaySize = overlay.size();
    }
    int w = 0, h = 0;
    const std::vector<uint8_t> rgba = straightRgba(overlay, w, h);
    ren.updateTexture(m_overlay, rgba.data(), uint32_t(w), uint32_t(h));
    s.image(m_overlay, QRectF(0, 0, width(), height()));
    ren.end();
    if (!m_shotPath.isEmpty()) {
        ren.savePNG(m_shotPath.toStdString());
        m_shotPath.clear();
    }
    toms::next::bgfxHostFrame();
    m_frames++;
}
