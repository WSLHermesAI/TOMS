#pragma once

#include <QHash>
#include <QImage>
#include <QRect>
#include <QVector>

// Pixels of sprites packed into atlas pages, for editors that draw sprites with QPainter
// (tools/studio_common). Pages are kept premultiplied; cut-outs and tinted copies are cached.
class SpriteImageCache
{
public:
    void setPages(const QVector<QImage>& pages);   // converted to ARGB32_Premultiplied
    void clear();
    const QVector<QImage>& pages() const { return m_pages; }

    // The pixels of `rect` on `page` (empty when out of range).
    QImage cut(int page, const QRect& rect) const;
    // The same, every pixel multiplied by rgba (0..1) -- what a sprite shader's texture * tint
    // gives, for straight-alpha blending with the result's (premultiplied) alpha. White = cut().
    QImage tinted(int page, const QRect& rect, const float rgba[4]) const;

    // A square-fitting copy for list icons: never upscaled by more than `size`, pixel art kept crisp.
    static QImage thumbnail(const QImage& image, int size);

private:
    QVector<QImage> m_pages;
    mutable QHash<QString, QImage> m_cut;
    mutable QHash<QString, QImage> m_tinted;
};
