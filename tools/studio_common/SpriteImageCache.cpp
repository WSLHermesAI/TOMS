#include "SpriteImageCache.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

QString rectKey(int page, const QRect& r)
{
    return QStringLiteral("%1:%2,%3,%4,%5").arg(page).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

int to255(float v) { return int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

}  // namespace

void SpriteImageCache::setPages(const QVector<QImage>& pages)
{
    clear();
    m_pages.clear();
    for (const QImage& p : pages) m_pages.push_back(p.convertToFormat(QImage::Format_ARGB32_Premultiplied));
}

void SpriteImageCache::clear()
{
    m_cut.clear();
    m_tinted.clear();
}

QImage SpriteImageCache::cut(int page, const QRect& rect) const
{
    if (page < 0 || page >= m_pages.size() || rect.isEmpty()) return QImage();
    const QString key = rectKey(page, rect);
    auto it = m_cut.find(key);
    if (it == m_cut.end()) it = m_cut.insert(key, m_pages[page].copy(rect));
    return *it;
}

QImage SpriteImageCache::tinted(int page, const QRect& rect, const float rgba[4]) const
{
    const int r = to255(rgba[0]), g = to255(rgba[1]), b = to255(rgba[2]), a = to255(rgba[3]);
    if (r == 255 && g == 255 && b == 255 && a == 255) return cut(page, rect);
    const QString key = rectKey(page, rect) + QStringLiteral("#%1.%2.%3.%4").arg(r).arg(g).arg(b).arg(a);
    auto it = m_tinted.find(key);
    if (it != m_tinted.end()) return *it;
    QImage img = cut(page, rect);
    if (img.isNull()) return img;
    img.detach();
    // Premultiplied pixels: colour channels scale by tint.rgb * tint.a, alpha by tint.a.
    const int mr = r * a, mg = g * a, mb = b * a;   // 0..65025
    for (int y = 0; y < img.height(); y++) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); x++) {
            const QRgb p = line[x];
            line[x] = qRgba(qRed(p) * mr / 65025, qGreen(p) * mg / 65025, qBlue(p) * mb / 65025, qAlpha(p) * a / 255);
        }
    }
    if (m_tinted.size() > 512) m_tinted.clear();   // colour animations make many; keep it bounded
    m_tinted.insert(key, img);
    return img;
}

QImage SpriteImageCache::thumbnail(const QImage& image, int size)
{
    if (image.isNull()) return image;
    const QSize fit = image.size().scaled(size, size, Qt::KeepAspectRatio);
    const bool shrink = fit.width() < image.width();
    return image.scaled(fit, Qt::KeepAspectRatio, shrink ? Qt::SmoothTransformation : Qt::FastTransformation);
}
